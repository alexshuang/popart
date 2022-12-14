// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#include "popart/popx/debugcontextx.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <snap/Graph.hpp>
#include <snap/Program.hpp>
#include <snap/Tensor.hpp>
#include <string>
#include <utility>
#include <vector>
#include <poprithms/logging/timepartitionlogger.hpp>
#include <poputil/TileMapping.hpp>
#include <popart/op/elementwise.hpp>
#include <popart/popx/devicex.hpp>
#include <popart/popx/irlowering.hpp>
#include <popart/popx/op/elementwisex.hpp>

#include "popart/error.hpp"
#include "popart/ir.hpp"
#include "popart/logging.hpp"
#include "popart/names.hpp"
#include "popart/op.hpp"
#include "popart/popx/popopx.hpp"
#include "popart/popx/poptensors.hpp"
#include "popart/popx/viewchangers.hpp"
#include "popart/tensor.hpp"
#include "popart/tensordebuginfo.hpp"
#include "popart/tensorindex.hpp"
#include "popart/tensorinfo.hpp"

namespace popart {
namespace view {
class Region;
} // namespace view

namespace popx {

ElementWiseUnaryOutplaceOpx::ElementWiseUnaryOutplaceOpx(
    Op *op,
    Devicex *devx,
    std::unique_ptr<EwuComputex> cx_)
    : ElementWiseUnaryOpx(op, devx), cx(std::move(cx_)) {}

ElementWiseUnaryOpx::ElementWiseUnaryOpx(Op *op, Devicex *devicex)
    : PopOpx(op, devicex) {}

InputCreatorType ElementWiseUnaryOpx::getInputCreatorType(InIndex) const {
  return InputCreatorType::CanUnwind;
}

snap::Tensor ElementWiseUnaryOpx::unwindTensorLayout(snap::Tensor tensor,
                                                     InIndex,
                                                     OutIndex) const {
  return tensor;
}

view::RegMap ElementWiseUnaryOpx::unwindRegion(InIndex, OutIndex) const {
  return [](const view::Region &r) { return view::Regions(1, r); };
}

ElementWiseBinaryOpx::ElementWiseBinaryOpx(Op *op, Devicex *devicex)
    : PopOpx(op, devicex) {}

bool ElementWiseBinaryOpx::broadcastCreatorAvailable(InIndex index) const {
  // Not broadcasting this arg.
  if (inInfo(index) == outInfo(ElementWiseBinaryBaseOp::getOutIndex())) {
    return false;
  }
  // Both args are broadcasted, this is not currently supported.
  if (inInfo(1 - index) != outInfo(ElementWiseBinaryBaseOp::getOutIndex())) {
    return false;
  }
  // Ignore scalars.
  if (inInfo(index).rank() == 0 || inInfo(index).nelms() == 1) {
    return false;
  }
  return true;
}

InputCreatorType
ElementWiseBinaryOpx::getInputCreatorType(InIndex index) const {
  // Is this type of broadcasting supported?
  if (broadcastCreatorAvailable(index)) {
    return InputCreatorType::CanCreate;
  }
  // Check shape doesn't change due to numpy-style broadcasting.
  // Design choice: even without broadcasting, it is possible for the
  // two inputs (of same shape) have different layout.
  // The poplar binary op can choose the layout of the output to take
  // the layout of either input.
  // However, let's layout both inputs in the same way. That way we can
  // definitely unwind through this opx, and it will also be efficient
  // when performing the op.
  if (op_p->inInfo(index) !=
      op_p->outInfo(ElementWiseBinaryBaseOp::getOutIndex())) {
    return InputCreatorType::Deadend;
  }

  const auto &settings = this->op_p->settings;
  const auto arg0Idx   = ElementWiseBinaryBaseOp::getArg0InIndex();
  const auto arg1Idx   = ElementWiseBinaryBaseOp::getArg1InIndex();

  const auto itArg0 = settings.inferTensorMappingToFrom.find(arg0Idx);
  const auto itArg1 = settings.inferTensorMappingToFrom.find(arg1Idx);

  const bool inferArg0FromArg1 =
      itArg0 != settings.inferTensorMappingToFrom.end() &&
      itArg0->second == arg1Idx;
  const bool inferArg1FromArg0 =
      itArg1 != settings.inferTensorMappingToFrom.end() &&
      itArg1->second == arg0Idx;

  if (index == arg0Idx) {
    if (inferArg0FromArg1) {
      return InputCreatorType::CanCreateOrUnwind;
    } else if (inferArg1FromArg0) {
      return InputCreatorType::Deadend;
    }
  } else if (index == arg1Idx) {
    if (inferArg1FromArg0) {
      return InputCreatorType::CanCreateOrUnwind;
    } else if (inferArg0FromArg1) {
      return InputCreatorType::Deadend;
    }
  }

  return InputCreatorType::CanUnwind;
}

std::set<TensorId>
ElementWiseBinaryOpx::mustExistBeforeCreate(InIndex index) const {
  // Broadcast
  if (broadcastCreatorAvailable(index)) {
    return {inId(1 - index)};
  }

  const auto &settings = this->op_p->settings;
  const auto arg0Idx   = ElementWiseBinaryBaseOp::getArg0InIndex();
  const auto arg1Idx   = ElementWiseBinaryBaseOp::getArg1InIndex();

  std::set<TensorId> mustExist;

  auto it = settings.inferTensorMappingToFrom.find(index);

  if (it != settings.inferTensorMappingToFrom.end() &&
      ((it->first == arg0Idx && it->second == arg1Idx) ||
       (it->first == arg1Idx && it->second == arg0Idx))) {
    mustExist.insert(op_p->input->tensor(it->second)->id);
  }

  return mustExist;
}

snap::Tensor ElementWiseBinaryOpx::createInputTensor(
    InIndex index,
    const poplar::DebugNameAndId &dnai) const {
  // Broadcast
  if (broadcastCreatorAvailable(index)) {
    auto otherOperand    = getInTensor(1 - index);
    auto thisOperandInfo = inInfo(index);
    logging::debug(
        "Using `createBroadcastOperand` for {}. Shapes: otherOperand "
        "{}. thisOperand {}",
        inId(index),
        otherOperand.shape(),
        thisOperandInfo.shape_szt());

    // Iterate over dimensions in reverse order, aligning at the last dimension
    // (because of broadcasting rules, see:
    // https://numpy.org/doc/stable/user/basics.broadcasting.html) and collate
    // non-broadcastable dimensions in a set that sorts by ascending order.
    std::set<unsigned> nonBroadcastDimSet;
    for (int64_t i = 1; i <= otherOperand.rank(); ++i) {
      if (i > thisOperandInfo.rank()) {
        // Missing dimensions are always broadcast.
        break;
      }
      auto otherOperandDim = otherOperand.rank() - i;
      auto thisOperandDim  = thisOperandInfo.rank() - i;
      if (otherOperand.dim(otherOperandDim) ==
          thisOperandInfo.dim(thisOperandDim)) {
        nonBroadcastDimSet.insert(otherOperandDim);
      }
    }

    // Create a permutation of the other operator tensor, which puts all the
    // non-broadcastable dimensions together at the start. Note we are careful
    // here to ensure the non-broadcastable and broadcastable dimensions are in
    // ascending order. The permutation we want to end up with is
    //
    //    [*nonBroadcastDimSet, *broadcastDimSet]
    std::vector<unsigned> permutation(otherOperand.rank());
    std::copy(nonBroadcastDimSet.begin(),
              nonBroadcastDimSet.end(),
              permutation.begin());
    auto nonBroadcastDims = nonBroadcastDimSet.size();
    for (int64_t i = 0; i < otherOperand.rank(); ++i) {
      if (nonBroadcastDimSet.find(i) == nonBroadcastDimSet.end()) {
        permutation[nonBroadcastDims] = i;
        nonBroadcastDims++;
      }
    }

    // Apply the permutation.
    otherOperand = otherOperand.dimShuffle(permutation);

    // Collapse all the non-broadcastable dimensions so we end up with
    //
    //    [sum(nonBroadcastDimSet), *broadcastDimSet]
    otherOperand = otherOperand.flatten(0, nonBroadcastDimSet.size());
    if (otherOperand.dim(0) != thisOperandInfo.nelms()) {
      throw internal_error("Expected flattened non-broadcastable dimensions "
                           "({}) to equal the candidate tensor size ({})",
                           otherOperand.dim(0),
                           thisOperandInfo.nelms());
    }

    // Create the tensor.
    auto created =
        poputil::createBroadcastOperand(graph().getPoplarGraph(),
                                        otherOperand.getPoplarTensor(),
                                        popType(thisOperandInfo),
                                        0,
                                        false,
                                        dnai);

    // Reshape to the required format.
    return snap::Tensor{created.reshape(thisOperandInfo.shape_szt()), graph()};
  }

  const auto arg0Idx = ElementWiseBinaryBaseOp::getArg0InIndex();
  const auto arg1Idx = ElementWiseBinaryBaseOp::getArg1InIndex();

  if (index == arg0Idx) {
    if (dv_p->lowering().tensors().contains(op_p->input->id(arg1Idx))) {
      return graph().clone(getInTensor(arg1Idx), dnai);
    }
  }

  if (index == arg1Idx) {
    if (dv_p->lowering().tensors().contains(op_p->input->id(arg0Idx))) {
      return graph().clone(getInTensor(arg0Idx), dnai);
    }
  }

  throw error("ElementWiseBinaryOpx::createInput : Invalid index = " +
              std::to_string(index));
}

void ElementWiseUnaryInplaceOpx::grow(snap::program::Sequence &prog) const {

  const auto growTimeTracker =
      op_p->getIr().timePartitionLogger().scopedStopwatch(
          "Lowering ElementwiseUnaryInplace to Poplar (\"grow\")");

  auto outTensor = getInTensor(ElementWiseUnaryOp::getInIndex());

  // if all of the elements in the tensor are distinct in memory,
  // them we can use the poplar inplace version. Otherwise, we must
  // use a non-inplace version.  See T7110 for a possible improvement
  if (!outTensor.isParallelWriteable()) {
    outTensor = cx->outplace(prog,
                             graph(),
                             outTensor,
                             getDebugNameAndId(),
                             "nonLinearityOutplaceFallback");
  } else {
    cx->inplace(
        prog, graph(), outTensor, getDebugNameAndId(), "nonLinearityInplace");
  }
  outTensor = cx->reshape(outTensor);
  if (hasInViewChangers(ElementWiseUnaryOp::getInIndex())) {
    setOutViewChangers(ElementWiseUnaryOp::getOutIndex(),
                       getInViewChangers(ElementWiseUnaryOp::getInIndex()));
  }
  setOutTensor(ElementWiseUnaryOp::getOutIndex(), outTensor);
}

void ElementWiseUnaryOutplaceOpx::grow(snap::program::Sequence &prog) const {
  auto outTensor = cx->outplace(prog,
                                graph(),
                                getInTensor(ElementWiseUnaryOp::getInIndex()),
                                getDebugNameAndId(),
                                "nonLinearityOutplace");

  outTensor = cx->reshape(outTensor);
  setOutTensor(ElementWiseUnaryOp::getOutIndex(), outTensor);
}

snap::Tensor ElementWiseBinaryOpx::unwindTensorLayout(snap::Tensor tensor,
                                                      InIndex,
                                                      OutIndex) const {
  return tensor;
}

view::RegMap ElementWiseBinaryOpx::unwindRegion(InIndex, OutIndex) const {
  return [](const view::Region &r) { return view::Regions(1, r); };
}

snap::Tensor EwuComputex::cloneNcopy(snap::program::Sequence &prog,
                                     snap::Graph &graph,
                                     const snap::Tensor &tensor,
                                     const poplar::DebugNameAndId &dnai) const {
  auto outTensor = graph.clone(tensor, {dnai});
  poplar::program::Copy copyProg(tensor, outTensor, false, {dnai});
  prog.getPoplarSequence().add(copyProg);
  return outTensor;
}

snap::Tensor EwuComputex::outplace(snap::program::Sequence &prog,
                                   snap::Graph &graph,
                                   const snap::Tensor &tensor,
                                   const poplar::DebugNameAndId &dnai,
                                   const std::string &debug_prefix) const {
  auto out_tensor = cloneNcopy(prog, graph, tensor, dnai);
  inplace(prog, graph, out_tensor, dnai, debug_prefix);
  return out_tensor;
}

snap::Tensor EwuComputex::coerceTo2D(const snap::Tensor &t, int64_t axis) {
  const auto in_shape = t.shape();
  auto k              = in_shape.begin();
  std::advance(k, axis);

  auto n = std::accumulate(
      in_shape.begin(), k, std::size_t{1}, std::multiplies<std::size_t>());
  auto d = std::accumulate(
      k, in_shape.end(), std::size_t{1}, std::multiplies<std::size_t>());

  return t.reshape({n, d});
}

bool EwbComputex::inplaceSupported() const {
  return inplacePolicy != EwbComputex::InplacePolicy::NEVER;
}

InIndex EwbComputex::getInplaceArgInIndex() const {
  if (inplacePolicy == InplacePolicy::LHS) {
    return ElementWiseBinaryBaseOp::getArg0InIndex();
  } else if (inplacePolicy == InplacePolicy::RHS) {
    return ElementWiseBinaryBaseOp::getArg1InIndex();
  } else {
    throw internal_error(
        "Invalid InplacePolicy. This class instance was not configured for "
        "inplacing and is attempting to compute in-place");
  }
}

InIndex EwbComputex::getOutplaceArgInIndex() const {
  // The out-of-place index is the input that isn't in-place
  const auto inplaceIdx = getInplaceArgInIndex();
  const auto arg0Idx    = ElementWiseBinaryBaseOp::getArg0InIndex();
  const auto arg1Idx    = ElementWiseBinaryBaseOp::getArg1InIndex();
  return inplaceIdx == arg0Idx ? arg1Idx : arg0Idx;
}

void ElementWiseBinaryOutplaceOpx::grow(snap::program::Sequence &prog) const {
  if (cx->inplaceSupported()) {
    throw internal_error(
        "Operation {} was configured for inplacing and attempting "
        "to compute out-of-place",
        debugContext().getPathName());
  }

  const auto arg0Idx = ElementWiseBinaryBaseOp::getArg0InIndex();
  const auto arg1Idx = ElementWiseBinaryBaseOp::getArg1InIndex();
  const auto outIdx  = ElementWiseBinaryBaseOp::getOutIndex();

  auto outTensor = cx->outplace(prog,
                                graph(),
                                getInTensor(arg0Idx),
                                getInTensor(arg1Idx),
                                getDebugNameAndId(),
                                "");

  if (hasInViewChangers(ElementWiseBinaryOp::getArg0InIndex()) &&
      hasInViewChangers(ElementWiseBinaryOp::getArg1InIndex())) {
    auto arg0vc = getInViewChangers(ElementWiseBinaryOp::getArg1InIndex());
    auto arg1vc = getInViewChangers(ElementWiseBinaryOp::getArg1InIndex());
    if (arg0vc == arg1vc) {
      setOutViewChangers(
          ElementWiseBinaryOp::getOutIndex(),
          getInViewChangers(ElementWiseBinaryOp::getArg0InIndex()));
    } else {
      throw error("View changers of arg0 and arg1 do not match.");
    }
  } else if (hasInViewChangers(ElementWiseBinaryOp::getArg0InIndex())) {
    setOutViewChangers(
        ElementWiseBinaryOp::getOutIndex(),
        getInViewChangers(ElementWiseBinaryOp::getArg0InIndex()));
  } else if (hasInViewChangers(ElementWiseBinaryOp::getArg1InIndex())) {
    setOutViewChangers(
        ElementWiseBinaryOp::getOutIndex(),
        getInViewChangers(ElementWiseBinaryOp::getArg1InIndex()));
  }
  setOutTensor(outIdx, outTensor);
}

void ElementWiseBinaryInplaceOpx::grow(snap::program::Sequence &prog) const {
  if (!cx->inplaceSupported()) {
    throw error("Invalid operation {} was not configured for inplacing and "
                "attempting to compute in-place",
                debugContext().getPathName());
  }

  auto tInOut    = getInTensor(cx->getInplaceArgInIndex());
  const auto tIn = getInTensor(cx->getOutplaceArgInIndex());

  if (tInOut.isParallelWriteable()) {
    tInOut =
        cx->maybeInplace(prog, graph(), tInOut, tIn, getDebugNameAndId(), "");
  } else {
    tInOut = cx->outplace(prog, graph(), tInOut, tIn, getDebugNameAndId(), "");
  }

  if (hasInViewChangers(cx->getInplaceArgInIndex())) {
    setOutViewChangers(ElementWiseBinaryOp::getOutIndex(),
                       getInViewChangers(cx->getInplaceArgInIndex()));
  }
  setOutTensor(ElementWiseBinaryBaseOp::getOutIndex(), tInOut);
}

BinaryComparisonOpx::BinaryComparisonOpx(Op *op, Devicex *devicex)
    : PopOpx(op, devicex) {}

} // namespace popx
} // namespace popart
