// Copyright (c) 2021 Graphcore Ltd. All rights reserved.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ext/new_allocator.h>
#include <ostream>
#include <vector>
#include <poplar/Tensor.hpp>
#include <popops/OperationDef.hpp>
#include <popops/Reduce.hpp>
#include <popart/error.hpp>
#include <popart/op/expand.hpp>
#include <popart/popx/op/expandx.hpp>
#include <popart/popx/opxmanager.hpp>

#include "popart/logging.hpp"
#include "popart/names.hpp"
#include "popart/op.hpp"
#include "popart/operators.hpp"
#include "popart/popx/opx.hpp"
#include "popart/region.hpp" // IWYU pragma: keep
#include "popart/util.hpp"

namespace poplar {
namespace program {
class Sequence;
} // namespace program
} // namespace poplar

namespace popart {
namespace popx {
class Devicex;

BaseExpandOpx::BaseExpandOpx(Op *op_, Devicex *devicex)
    : Opx(op_, devicex), op(static_cast<ExpandOp *>(op_)) {}

ExpandOpx::ExpandOpx(Op *op_, Devicex *devicex) : BaseExpandOpx(op_, devicex) {
  verifyOp<ExpandOp>(op_, {Onnx::Operators::Expand_8});
}

InputCreatorType BaseExpandOpx::getInputCreatorType(InIndex) const {
  return InputCreatorType::CanUnwind;
}

poplar::Tensor BaseExpandOpx::unwindTensorLayout(poplar::Tensor t,
                                                 InIndex inIndex,
                                                 OutIndex) const {

  // Numpy broadcasting, some valid examples:
  //
  // input    output
  // -----    ------
  // (4)      (3,4)
  // (1)      (5)
  // (6)      (6)
  // (4,1)    (4,5)
  // (4,1)    (5,4,3)
  //
  // See
  // https://numpy.org/doc/stable/user/basics.broadcasting.html
  // for details.
  //
  // We here take a poplar tensor 'output' and slice out a part of it to get
  // an 'input' tensor of a reduced size. We arbitrarily choose the slice in
  // each dimension to start at index 0.

  poplar::Tensor tensor = t;
  if (inIndex == ExpandOp::getInTensorIndex()) {
    auto output_shape = op->getOutShape();
    auto input_shape  = inShape(inIndex);

    // The number of excess dimensions which the output has:
    auto dim_diff = output_shape.size() - input_shape.size();

    // Make tensor have the correct rank by removing the first 'dim_diff'
    // dimensions. Example: If input shape is (5,1) and output shape is
    // (3,4,5,6) then we take output[0][0] which is of shape (5,6).
    for (uint64_t i = 0; i < dim_diff; ++i) {
      tensor = tensor[0];
    }

    for (uint32_t dim = 0; dim < input_shape.size(); ++dim) {
      if (output_shape[dim + dim_diff] != input_shape[dim]) {
        tensor = tensor.slice(0ull, 1ull, dim);
      }
    }

    // confirm that the shape of the computed tensor is as expected
    if (tensor.shape() !=
        std::vector<uint64_t>{input_shape.cbegin(), input_shape.cend()}) {
      std::ostringstream oss;
      oss << "Incorrect shape of compute poplar Tensor in unwinding expand. "
          << "Expected it to have the shape of the input, " << input_shape
          << ", but it has shape " << tensor.shape() << ". ";
      throw error(oss.str());
    }
  }

  return tensor;
}

view::RegMap BaseExpandOpx::unwindRegion(InIndex inIndex,
                                         OutIndex outIndex) const {
  ExpandOp *cop = dynamic_cast<ExpandOp *>(this->op_p);
  return cop->bwdRegMap(inIndex, outIndex);
}

poplar::Tensor BaseExpandOpx::expand_broadcast(const Shape output_shape,
                                               const poplar::Tensor &t) const {
  /* Make the rank of the tensor to be expanded and the output the same by
     adding 1 as the higher dimensions example: where a Tensor of shape (3,1) is
     expanded to shape (3,2,6), the tensor to be expanded is reshaped from (3,1)
     to (3,1,1)
  */
  auto expand = t;
  for (int64_t rank_diff = output_shape.size() - expand.shape().size();
       rank_diff > 0;
       --rank_diff) {
    expand = expand.expand({0});
  }
  auto new_input_shape = expand.shape();
  // Broadcasting across each dimension
  // for (int dim = output_shape.size(); dim >= 0; --dim) {
  for (int dim = 0; dim < output_shape.size(); dim++) {
    if (output_shape[dim] != new_input_shape[dim]) {
      expand = expand.broadcast(static_cast<uint32_t>(output_shape[dim]), dim);
    }
  }

  auto tx = t;
  return expand;
}

void ExpandOpx::grow(poplar::program::Sequence &prog) const {

  auto output_shape = outShape(ExpandOp::getOutIndex());
  auto expand = cloneNcopy(prog, getInTensor(ExpandOp::getInTensorIndex()));
  expand      = expand_broadcast(output_shape, expand);
  setOutTensor(ExpandOp::getOutIndex(), expand);
}

ExpandInplaceOpx::ExpandInplaceOpx(Op *op_, Devicex *devicex)
    : BaseExpandOpx(op_, devicex) {
  verifyOp<ExpandOp>(op_);
}

void ExpandInplaceOpx::grow(poplar::program::Sequence &) const {
  auto output_shape = outShape(ExpandOp::getOutIndex());
  auto expand       = getInTensor(ExpandOp::getInTensorIndex());
  expand            = expand_broadcast(output_shape, expand);
  setOutTensor(ExpandOp::getOutIndex(), expand);
}

ExpandGradOpx::ExpandGradOpx(Op *op_, Devicex *devicex) : Opx(op_, devicex) {
  verifyOp<ExpandGradOp>(op_, Onnx::GradOperators::ExpandGrad);
  auto expand_grad_op = dynamic_cast<ExpandGradOp *>(op_);
  if (expand_grad_op) {
    xShape = expand_grad_op->getXShape();
  } else {
    throw error("ExpandGradOpx::ExpandGradOpx : Constructed with invalid op ");
  }
}

void ExpandGradOpx::grow(poplar::program::Sequence &prog) const {
  const auto dY = getInTensor(ExpandGradOp::getDYIndex());

  std::vector<size_t> axes;
  const int64_t offset = dY.rank() - xShape.size();
  for (int i = 0; i < dY.rank(); i++) {
    if (i < offset || xShape[i - offset] == 1) {
      axes.push_back(i);
    }
  }

  auto dX = popops::reduce(
      graph(), dY, axes, {popops::Operation::ADD}, prog, debugContext("add"));
  dX = dX.reshape(xShape);
  setOutTensor(ExpandGradOp::getOutIndex(), cloneNcopy(prog, dX));
}

namespace {
OpxCreator<ExpandOpx> expandOpxCreator({Onnx::Operators::Expand_8});
OpxCreator<ExpandInplaceOpx>
    expandInplaceOpxCreator(Onnx::CustomOperators::ExpandInplace);
OpxCreator<ExpandGradOpx> expandGradOpxCreator(Onnx::GradOperators::ExpandGrad);
} // namespace

} // namespace popx
} // namespace popart
