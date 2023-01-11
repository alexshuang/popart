// Copyright (c) 2021 Graphcore Ltd. All rights reserved.
#include <vector>
#include <popops/ElementWise.hpp>
#include <popops/ExprOp.hpp>
#include <popart/error.hpp>
#include <popart/op/bitwise.hpp>
#include <popart/popx/op/bitwisex.hpp>
#include <popart/popx/opxmanager.hpp>

#include "popart/graphcoreoperators.hpp"
#include "popart/logging.hpp"
#include "popart/op.hpp"
#include "popart/operatoridentifier.hpp"
#include "popart/popx/op/elementwisex.hpp"

namespace poplar {
namespace program {
class Sequence;
} // namespace program
} // namespace poplar

namespace popart {
namespace popx {
class Devicex;

BitwiseNotOpx::BitwiseNotOpx(Op *op, Devicex *devicex)
    : ElementWiseUnaryOpx(op, devicex) {
  verifyOp<BitwiseNotOpx>(op, {Onnx::AiGraphcore::OpSet1::BitwiseNot});
}

void BitwiseNotOpx::grow(poplar::program::Sequence &prog) const {
  insert(outId(BitwiseNotOp::getOutIndex()),
         popops::map(graph(),
                     popops::expr::UnaryOpType::BITWISE_NOT,
                     get(inId(BitwiseNotOp::getInIndex())),
                     prog,
                     debugContext()));
}

BitwiseBinaryOpx::BitwiseBinaryOpx(Op *op, Devicex *devicex)
    : ElementWiseBinaryOpx(op, devicex) {
  verifyOp<BitwiseBinaryOpx>(op,
                             {Onnx::AiGraphcore::OpSet1::BitwiseAnd,
                              Onnx::AiGraphcore::OpSet1::BitwiseOr,
                              Onnx::AiGraphcore::OpSet1::BitwiseXor,
                              Onnx::AiGraphcore::OpSet1::BitwiseXnor});
}

void BitwiseBinaryOpx::grow(poplar::program::Sequence &prog) const {
  insert(outId(BitwiseBinaryOp::getOutIndex()),
         popops::map(graph(),
                     determineOpType(),
                     getInTensor(BitwiseBinaryOp::getArg0InIndex()),
                     getInTensor(BitwiseBinaryOp::getArg1InIndex()),
                     prog,
                     debugContext()));
}

popops::expr::BinaryOpType BitwiseBinaryOpx::determineOpType() const {
  if (op_p->opid == Onnx::AiGraphcore::OpSet1::BitwiseAnd) {
    return popops::expr::BinaryOpType::BITWISE_AND;
  }
  if (op_p->opid == Onnx::AiGraphcore::OpSet1::BitwiseOr) {
    return popops::expr::BinaryOpType::BITWISE_OR;
  }
  if (op_p->opid == Onnx::AiGraphcore::OpSet1::BitwiseXor) {
    return popops::expr::BinaryOpType::BITWISE_XOR;
  }
  if (op_p->opid == Onnx::AiGraphcore::OpSet1::BitwiseXnor) {
    return popops::expr::BinaryOpType::BITWISE_XNOR;
  }
  throw error("Unknown opx type {}", op_p->opid);
}

namespace {

OpxCreator<BitwiseNotOpx>
    bitwiseNotOpxCreator(Onnx::AiGraphcore::OpSet1::BitwiseNot);

OpxCreator<BitwiseBinaryOpx>
    bitwiseAndOpxCreator(Onnx::AiGraphcore::OpSet1::BitwiseAnd);

OpxCreator<BitwiseBinaryOpx>
    bitwiseOrOpxCreator(Onnx::AiGraphcore::OpSet1::BitwiseOr);
OpxCreator<BitwiseBinaryOpx>
    bitwiseXorOpxCreator(Onnx::AiGraphcore::OpSet1::BitwiseXor);
OpxCreator<BitwiseBinaryOpx>
    bitwiseXnorOpxCreator(Onnx::AiGraphcore::OpSet1::BitwiseXnor);

} // namespace

} // namespace popx
} // namespace popart
