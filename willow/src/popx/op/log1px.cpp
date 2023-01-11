// Copyright (c) 2020 Graphcore Ltd. All rights reserved.
#include <string>
#include <poplar/Tensor.hpp>
#include <popops/ElementWise.hpp>
#include <popops/ExprOp.hpp>
#include <popart/popx/op/log1px.hpp>
#include <popart/popx/opxmanager.hpp>

#include "popart/graphcoreoperators.hpp"
#include "popart/popx/debugcontextx.hpp"
#include "popart/popx/op/elementwisex.hpp"

namespace poplar {
class Graph;
namespace program {
class Sequence;
} // namespace program
} // namespace poplar

namespace popart {
class Log1pInplaceOp;
class Log1pOp;
class Op;

namespace popx {
class Devicex;

Log1pInplaceOpx::Log1pInplaceOpx(Op *op, Devicex *devicex)
    : ElementWiseUnaryInplaceOpx(op, devicex, Log1pComputex::get()) {
  verifyOp<Log1pInplaceOp>(op, Onnx::CustomOperators::Log1pInplace);
}

Log1pOpx::Log1pOpx(Op *op, Devicex *devicex)
    : ElementWiseUnaryOutplaceOpx(op, devicex, Log1pComputex::get()) {
  verifyOp<Log1pOp>(op, Onnx::CustomOperators::Log1p_1);
}

poplar::Tensor Log1pComputex::outplace(poplar::program::Sequence &p,
                                       poplar::Graph &g,
                                       const poplar::Tensor &t,
                                       const poplar::DebugNameAndId &dnai,
                                       const std::string &dbs) const {

  return popops::map(
      g, popops::expr::UnaryOpType::LOGARITHM_ONE_PLUS, t, p, {dnai, dbs});
}

void Log1pComputex::inplace(poplar::program::Sequence &p,
                            poplar::Graph &g,
                            const poplar::Tensor &t,
                            const poplar::DebugNameAndId &dnai,
                            const std::string &dbs) const {

  popops::mapInPlace(
      g, popops::expr::UnaryOpType::LOGARITHM_ONE_PLUS, t, p, {dnai, dbs});
}

namespace {
OpxCreator<Log1pOpx> log1pOpxCreator(Onnx::CustomOperators::Log1p_1);
OpxCreator<Log1pInplaceOpx>
    log1pInplaceOpxCreator(Onnx::CustomOperators::Log1pInplace);
} // namespace

} // namespace popx
} // namespace popart
