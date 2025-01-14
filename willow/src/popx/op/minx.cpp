// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <snap/Graph.hpp>
#include <snap/Program.hpp>
#include <snap/Tensor.hpp>
#include <snap/popops/ElementWise.hpp>
#include <string>
#include <vector>
#include <poplar/Tensor.hpp>
#include <popops/Expr.hpp>
#include <popops/ExprOp.hpp>
#include <popops/OperationDef.hpp>
#include <popops/Reduce.hpp>
#include <popart/op/min.hpp>
#include <popart/popx/op/minx.hpp>
#include <popart/popx/opxmanager.hpp>
#include <popart/tensorindex.hpp>
#include <popart/util.hpp>

#include "popart/names.hpp"
#include "popart/op.hpp"
#include "popart/operatoridentifier.hpp"
#include "popart/operators.hpp"
#include "popart/popx/popopx.hpp"
#include "popart/region.hpp" // IWYU pragma: keep
#include "popart/tensorinfo.hpp"

namespace pe = popops::expr;

namespace popart {

namespace popx {
class Devicex;

MinOpx::MinOpx(Op *op, Devicex *devicex) : PopOpx(op, devicex) {
  verifyOp<MinOp>(op, {Onnx::Operators::Min_8, Onnx::Operators::Min_6});
}

InputCreatorType MinOpx::getInputCreatorType(InIndex index) const {
  // If one of the operations is a broadcast then the tensor cannot be unwound.
  if (op_p->inInfo(index) != op_p->outInfo(0)) {
    return InputCreatorType::Deadend;
  }

  // Default behaviour.
  return InputCreatorType::CanUnwind;
}

snap::Tensor
MinOpx::unwindTensorLayout(snap::Tensor tensor, InIndex, OutIndex) const {
  return tensor;
}

view::RegMap MinOpx::unwindRegion(InIndex, OutIndex) const {
  return [](const view::Region &r) { return view::Regions(1, r); };
}

void MinOpx::grow(snap::program::Sequence &prog) const {
  auto outTensor = cloneNcopy(prog, getInTensor(0));

  if (op_p->input->n() > 1) {

    for (int i = 1; i < op_p->input->n(); ++i) {
      outTensor =
          snap::popops::map(graph(),
                            popops::expr::BinaryOpType::MINIMUM,
                            outTensor,
                            getInTensor(i),
                            prog,
                            debugContext(std::string("min") + sNameDelimiter +
                                         std::to_string(i)));
    }
  }

  setOutTensor(MinOp::getOutIndex(), outTensor);
}

MinArgGradOpx::MinArgGradOpx(Op *op_, Devicex *devicex_)
    : PopOpx(op_, devicex_) {}

void MinArgGradOpx::grow(snap::program::Sequence &prog) const {

  // Create a mask of the min input tensor. Set a element to 1 if it is
  // the minimum element value of all inputs (i.e. is in the fwd output) else 0
  // 1. Subtract the output of the forward op tensor from the input of the
  // forward op.
  //    We will be left with '0' of elements are the minimum in the input tensor
  //    and all other values < 0
  // 2. Signum the result to give a tensor of 0's and -1's.
  // 3. Add 1 to the result to give a mask tensor
  // 4. Multiply by the gradient tensor.
  auto result = snap::popops::map(
      graph(),
      pe::Mul(pe::Add(pe::Signum(pe::Sub(pe::_1, pe::_2)), pe::Const(1)),
              pe::_3),
      {getInTensor(MinArgGradOp::getFwdOutInIndex()),
       getInTensor(MinArgGradOp::getFwdInIndex()),
       getInTensor(MinArgGradOp::getGradInIndex())},
      prog,
      debugContext("result"));

  auto shapeOfOutputOfFwdOp = inInfo(MinArgGradOp::getFwdOutInIndex()).shape();
  auto shapeOfInputToFwdOp  = inInfo(MinArgGradOp::getFwdInIndex()).shape();

  // Create the axes to reduce along.
  std::vector<int64_t> axes =
      npReductionAxis(shapeOfInputToFwdOp, shapeOfOutputOfFwdOp);

  // Remove axes from the result that were not present ( or 1) in the input to
  // the fwd op
  auto out = popops::reduce(graph().getPoplarGraph(),
                            result.getPoplarTensor(),
                            vXtoY<int64_t, std::size_t>(axes),
                            {popops::Operation::ADD},
                            prog.getPoplarSequence(),
                            debugContext("out"));

  // Reshape the output, to add 1's if needed
  setOutTensor(
      MinArgGradOp::getOutIndex(),
      snap::Tensor{
          out.reshape(outInfo(MinArgGradOp::getOutIndex()).shape_szt()),
          graph()});
}

namespace {
OpxCreator<MinOpx> minOpxCreator({Onnx::Operators::Min_6,
                                  Onnx::Operators::Min_8});
OpxCreator<MinArgGradOpx> minGradOpxCreator(Onnx::GradOperators::MinArgGrad);
} // namespace

} // namespace popx
} // namespace popart
