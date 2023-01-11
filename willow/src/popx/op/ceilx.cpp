// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#include <string>
#include <vector>
#include <poplar/Tensor.hpp>
#include <popops/ElementWise.hpp>
#include <popops/ExprOp.hpp>
#include <popart/popx/op/ceilx.hpp>
#include <popart/popx/opxmanager.hpp>

#include "popart/operatoridentifier.hpp"
#include "popart/operators.hpp"
#include "popart/popx/debugcontextx.hpp"
#include "popart/popx/op/elementwisex.hpp"

namespace poplar {
class Graph;
namespace program {
class Sequence;
} // namespace program
} // namespace poplar

namespace popart {
class CeilInplaceOp;
class CeilOp;
class Op;

namespace popx {
class Devicex;

poplar::Tensor CeilComputex::outplace(poplar::program::Sequence &prog,
                                      poplar::Graph &graph,
                                      const poplar::Tensor &tensor,
                                      const poplar::DebugNameAndId &dnai,
                                      const std::string &s) const {

  return popops::map(
      graph, popops::expr::UnaryOpType::CEIL, tensor, prog, {dnai, s});
}

void CeilComputex::inplace(poplar::program::Sequence &prog,
                           poplar::Graph &graph,
                           const poplar::Tensor &tensor,
                           const poplar::DebugNameAndId &dnai,
                           const std::string &s) const {

  popops::mapInPlace(
      graph, popops::expr::UnaryOpType::CEIL, tensor, prog, {dnai, s});
}

CeilOpx::CeilOpx(Op *op, Devicex *devicex)
    : ElementWiseUnaryOutplaceOpx(op, devicex, CeilComputex::get()) {
  verifyOp<CeilOp>(op, {Onnx::Operators::Ceil_1, Onnx::Operators::Ceil_6});
}

CeilInplaceOpx::CeilInplaceOpx(Op *op, Devicex *devicex)
    : ElementWiseUnaryInplaceOpx(op, devicex, CeilComputex::get()) {
  verifyOp<CeilInplaceOp>(op, Onnx::CustomOperators::CeilInplace);
}

namespace {
OpxCreator<CeilOpx> ceilOpxCreator({Onnx::Operators::Ceil_1,
                                    Onnx::Operators::Ceil_6});
OpxCreator<CeilInplaceOpx>
    ceilxInplaceOpxCreator(Onnx::CustomOperators::CeilInplace);
} // namespace

} // namespace popx
} // namespace popart
