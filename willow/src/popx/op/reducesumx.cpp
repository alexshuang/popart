// Copyright (c) 2018 Graphcore Ltd. All rights reserved.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <snap/Graph.hpp>
#include <snap/Program.hpp>
#include <snap/Tensor.hpp>
#include <vector>
#include <poplar/Tensor.hpp>
#include <popops/OperationDef.hpp>
#include <popops/Reduce.hpp>
#include <popart/op/reducesum.hpp>
#include <popart/popx/op/reducesumx.hpp>
#include <popart/popx/opxmanager.hpp>
#include <popart/util.hpp>

#include "popart/operatoridentifier.hpp"
#include "popart/operators.hpp"
#include "popart/popx/popopx.hpp"
#include "popart/tensorinfo.hpp"

namespace popart {
class Op;

namespace popx {
class Devicex;

ReduceSumOpx::ReduceSumOpx(Op *op, Devicex *devicex) : PopOpx(op, devicex) {
  verifyOp<ReduceSumOp>(op);
}

void ReduceSumOpx::grow(snap::program::Sequence &prog) const {
  const auto &op   = getOp<ReduceSumOp>();
  const auto input = getInTensor(ReduceSumOp::getInIndex()).getPoplarTensor();

  auto output_tensor = popops::reduce(graph().getPoplarGraph(),
                                      input,
                                      vector_cast<std::size_t>(op.getAxes()),
                                      {popops::Operation::ADD},
                                      prog.getPoplarSequence(),
                                      debugContext("add"));

  setOutTensor(
      ReduceSumOp::getOutIndex(),
      snap::Tensor{output_tensor.reshape(
                       outInfo(ReduceSumOp::getOutIndex()).shape_szt()),
                   graph()});
}

ReduceSumGradOpx::ReduceSumGradOpx(Op *op, Devicex *devicex)
    : PopOpx(op, devicex) {
  verifyOp<ReduceSumGradOp>(op, Onnx::GradOperators::ReduceSumGrad);
}

void ReduceSumGradOpx::grow(snap::program::Sequence &prog) const {
  const auto &op = getOp<ReduceSumGradOp>();
  auto output    = cloneNcopy(prog, getInTensor(ReduceSumGradOp::getInIndex()))
                    .getPoplarTensor();
  auto input_shape     = inShape(ReduceSumGradOp::getInIndex());
  auto output_shape    = outShape(ReduceSumGradOp::getOutIndex());
  const auto new_shape = vector_cast<std::size_t>(op.backwardShape());

  output = output.reshape(new_shape);

  // Broadcasting across each dimension
  for (int dim = 0; dim < std::min(new_shape.size(), output_shape.size());
       ++dim) {
    if (new_shape[dim] != output_shape[dim]) {
      output = output.broadcast(static_cast<uint32_t>(output_shape[dim]), dim);
    }
  }

  // output now matches the shape of output_shape
  setOutTensor(
      ReduceSumGradOp::getOutIndex(),
      snap::Tensor{
          output.reshape(outInfo(ReduceSumGradOp::getOutIndex()).shape_szt()),
          graph()});
}

namespace {
OpxCreator<ReduceSumOpx> reduceSumOpxCreator({Onnx::Operators::ReduceSum_1,
                                              Onnx::Operators::ReduceSum_11});
OpxCreator<ReduceSumGradOpx>
    reduceSumGradGradOpxCreator(Onnx::GradOperators::ReduceSumGrad);
} // namespace

} // namespace popx
} // namespace popart
