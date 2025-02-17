// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#include <snap/Graph.hpp>
#include <snap/Program.hpp>
#include <snap/Tensor.hpp>
#include <vector>
#include <popops/Cast.hpp>
#include <popart/op/cast.hpp>
#include <popart/popx/devicex.hpp>
#include <popart/popx/op/castx.hpp>
#include <popart/popx/opxmanager.hpp>

#include "popart/op.hpp"
#include "popart/operatoridentifier.hpp"
#include "popart/operators.hpp"
#include "popart/popx/popopx.hpp"

namespace popart {
namespace popx {

CastOpx::CastOpx(Op *op, Devicex *devicex) : PopOpx(op, devicex) {
  verifyOp<CastOp>(op);
}

void CastOpx::grow(snap::program::Sequence &prog) const {
  auto out = popops::cast(graph().getPoplarGraph(),
                          getInTensor(CastOp::getInIndex()).getPoplarTensor(),
                          popType(op_p->outInfo(CastOp::getOutIndex())),
                          prog.getPoplarSequence(),
                          debugContext());

  if (hasInViewChangers(CastOp::getInIndex())) {
    setOutViewChangers(CastOp::getOutIndex(),
                       getInViewChangers(CastOp::getInIndex()));
  }

  setOutTensor(CastOp::getOutIndex(), snap::Tensor{out, graph()});
}

CastGradOpx::CastGradOpx(Op *op, Devicex *devicex) : CastOpx(op, devicex) {
  verifyOp<CastGradOp>(op, Onnx::GradOperators::CastGrad);
}

namespace {
OpxCreator<CastOpx> castOpxCreator({Onnx::Operators::Cast_1,
                                    Onnx::Operators::Cast_6,
                                    Onnx::Operators::Cast_9});
OpxCreator<CastGradOpx> castGradOpxCreator(Onnx::GradOperators::CastGrad);
} // namespace

} // namespace popx
} // namespace popart
