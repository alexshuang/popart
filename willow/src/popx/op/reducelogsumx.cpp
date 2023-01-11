// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ext/new_allocator.h>
#include <vector>
#include <poplar/Tensor.hpp>
#include <popops/ElementWise.hpp>
#include <popops/ExprOp.hpp>
#include <popops/OperationDef.hpp>
#include <popops/Reduce.hpp>
#include <popart/op/reducelogsum.hpp>
#include <popart/popx/op/reducelogsumx.hpp>
#include <popart/popx/opxmanager.hpp>
#include <popart/util.hpp>

#include "popart/operatoridentifier.hpp"
#include "popart/operators.hpp"
#include "popart/popx/opx.hpp"
#include "popart/tensorinfo.hpp"

namespace poplar {
namespace program {
class Sequence;
} // namespace program
} // namespace poplar

namespace pe = popops::expr;

namespace popart {
class Op;

namespace popx {
class Devicex;

ReduceLogSumOpx::ReduceLogSumOpx(Op *op, Devicex *devicex) : Opx(op, devicex) {
  verifyOp<ReduceLogSumOp>(op);
}

void ReduceLogSumOpx::grow(poplar::program::Sequence &prog) const {
  const auto &op   = getOp<ReduceLogSumOp>();
  const auto input = getInTensor(ReduceLogSumOp::getInIndex());

  auto output_tensor = popops::reduce(graph(),
                                      input,
                                      vector_cast<std::size_t>(op.getAxes()),
                                      {popops::Operation::ADD},
                                      prog,
                                      debugContext("output"));
  popops::logInPlace(graph(), output_tensor, prog, debugContext("log"));

  setOutTensor(ReduceLogSumOp::getOutIndex(),
               output_tensor.reshape(
                   outInfo(ReduceLogSumOp::getOutIndex()).shape_szt()));
}

ReduceLogSumGradOpx::ReduceLogSumGradOpx(Op *op, Devicex *devicex)
    : Opx(op, devicex) {
  verifyOp<ReduceLogSumGradOp>(op, Onnx::GradOperators::ReduceLogSumGrad);
}

void ReduceLogSumGradOpx::grow(poplar::program::Sequence &prog) const {
  const auto &op       = getOp<ReduceLogSumGradOp>();
  auto output          = getInTensor(ReduceLogSumGradOp::getInIndex());
  auto scale           = getInTensor(ReduceLogSumGradOp::getFwdOutInIndex());
  auto input_shape     = inShape(ReduceLogSumGradOp::getInIndex());
  auto output_shape    = outShape(ReduceLogSumGradOp::getOutIndex());
  const auto new_shape = vector_cast<std::size_t>(op.backwardShape());

  output = output.reshape(new_shape);
  scale  = scale.reshape(new_shape);
  scale  = popops::exp(graph(), scale, prog);

  // Broadcasting across each dimension
  for (int dim = 0; dim < new_shape.size(); ++dim) {
    if (new_shape[dim] != output_shape[dim]) {
      output = output.broadcast(static_cast<uint32_t>(output_shape[dim]), dim);
      scale  = scale.broadcast(static_cast<uint32_t>(output_shape[dim]), dim);
    }
  }

  output = popops::div(graph(), output, scale, prog, debugContext("div"));

  // output now matches the shape of output_shape
  setOutTensor(ReduceLogSumGradOp::getOutIndex(), output);
}

namespace {
OpxCreator<ReduceLogSumOpx> reduceLogSumOpxCreator(
    {Onnx::Operators::ReduceLogSum_1, Onnx::Operators::ReduceLogSum_11});
OpxCreator<ReduceLogSumGradOpx>
    reduceLogSumGradGradOpxCreator(Onnx::GradOperators::ReduceLogSumGrad);
} // namespace

} // namespace popx
} // namespace popart
