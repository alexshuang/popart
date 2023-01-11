// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#include <vector>
#include <popops/ElementWise.hpp>
#include <popops/ExprOp.hpp>
#include <popart/op/greater.hpp>
#include <popart/popx/op/greaterx.hpp>
#include <popart/popx/opxmanager.hpp>

#include "popart/operatoridentifier.hpp"
#include "popart/operators.hpp"
#include "popart/popx/op/elementwisex.hpp"

namespace poplar {
namespace program {
class Sequence;
} // namespace program
} // namespace poplar

namespace popart {
class Op;

namespace popx {
class Devicex;

GreaterOpx::GreaterOpx(Op *op, Devicex *devicex)
    : BinaryComparisonOpx(op, devicex) {
  verifyOp<GreaterOp>(op,
                      {Onnx::Operators::Greater_7, Onnx::Operators::Greater_9});
}

void GreaterOpx::grow(poplar::program::Sequence &prog) const {

  insert(outId(GreaterOp::getOutIndex()),
         popops::map(graph(),
                     popops::expr::BinaryOpType::GREATER_THAN,
                     get(inId(GreaterOp::getArg0InIndex())),
                     get(inId(GreaterOp::getArg1InIndex())),
                     prog,
                     debugContext()));
}

namespace {

OpxCreator<GreaterOpx> greaterOpxCreator_7(Onnx::Operators::Greater_7);
OpxCreator<GreaterOpx> greaterOpxCreator_9(Onnx::Operators::Greater_9);

} // namespace

} // namespace popx
} // namespace popart
