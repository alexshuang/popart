// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <snap/Graph.hpp>
#include <snap/Program.hpp>
#include <snap/Tensor.hpp>
#include <snap/popops/ElementWise.hpp>
#include <vector>
#include <poplar/Tensor.hpp>
#include <popops/Expr.hpp>
#include <popops/ExprOp.hpp>
#include <popops/OperationDef.hpp>
#include <popops/Reduce.hpp>
#include <popart/op/reducemin.hpp>
#include <popart/popx/op/reduceminx.hpp>
#include <popart/popx/opxmanager.hpp>
#include <popart/util.hpp>

#include "popart/operatoridentifier.hpp"
#include "popart/operators.hpp"
#include "popart/popx/popopx.hpp"
#include "popart/tensorinfo.hpp"

namespace pe = popops::expr;

namespace popart {
class Op;

namespace popx {
class Devicex;

ReduceMinOpx::ReduceMinOpx(Op *op, Devicex *devicex) : PopOpx(op, devicex) {
  verifyOp<ReduceMinOp>(op);
}

void ReduceMinOpx::grow(snap::program::Sequence &prog) const {
  const auto &op   = getOp<ReduceMinOp>();
  const auto input = getInTensor(ReduceMinOp::getInIndex()).getPoplarTensor();

  auto output_tensor = popops::reduce(graph().getPoplarGraph(),
                                      input,
                                      vector_cast<std::size_t>(op.getAxes()),
                                      {popops::Operation::MIN},
                                      prog.getPoplarSequence(),
                                      debugContext("min"));

  setOutTensor(
      ReduceMinOp::getOutIndex(),
      snap::Tensor{output_tensor.reshape(
                       outInfo(ReduceMinOp::getOutIndex()).shape_szt()),
                   graph()});
}

ReduceMinGradOpx::ReduceMinGradOpx(Op *op, Devicex *devicex)
    : PopOpx(op, devicex) {
  verifyOp<ReduceMinGradOp>(op, Onnx::GradOperators::ReduceMinGrad);
}

void ReduceMinGradOpx::grow(snap::program::Sequence &prog) const {
  const auto &op = getOp<ReduceMinGradOp>();
  auto output    = cloneNcopy(prog, getInTensor(ReduceMinGradOp::getInIndex()));
  auto mask =
      cloneNcopy(prog, getInTensor(ReduceMinGradOp::getFwdOutInIndex()));
  auto input_shape     = inShape(ReduceMinGradOp::getInIndex());
  auto output_shape    = outShape(ReduceMinGradOp::getOutIndex());
  const auto new_shape = vector_cast<std::size_t>(op.backwardShape());

  output = output.reshape(new_shape);
  mask   = mask.reshape(new_shape);

  // Broadcasting across each dimension
  for (int dim = 0; dim < new_shape.size(); ++dim) {
    if (new_shape[dim] != output_shape[dim]) {
      output = output.broadcast(static_cast<uint32_t>(output_shape[dim]), dim);
      mask   = mask.broadcast(static_cast<uint32_t>(output_shape[dim]), dim);
    }
  }

  output = snap::popops::map(
      graph(),
      pe::Mul(pe::Add(pe::Signum(pe::Sub(pe::_1, pe::_2)), pe::Const(1)),
              pe::_3),
      {mask, getInTensor(ReduceMinGradOp::getFwdInInIndex()), output},
      prog,
      debugContext("maskmul"));

  // output now matches the shape of output_shape
  setOutTensor(ReduceMinGradOp::getOutIndex(), output);
}

namespace {
OpxCreator<ReduceMinOpx> reduceMinOpxCreator({Onnx::Operators::ReduceMin_1,
                                              Onnx::Operators::ReduceMin_11});
OpxCreator<ReduceMinGradOpx>
    reduceMinGradGradOpxCreator(Onnx::GradOperators::ReduceMinGrad);
} // namespace

} // namespace popx
} // namespace popart
