// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include <poplar/Tensor.hpp>
#include <popops/ElementWise.hpp>
#include <popops/Expr.hpp>
#include <popops/ExprOp.hpp>
#include <popart/op/atan.hpp>
#include <popart/popx/op/atanx.hpp>
#include <popart/popx/opxmanager.hpp>

#include "popart/operators.hpp"
#include "popart/popx/debugcontextx.hpp"
#include "popart/popx/op/elementwisex.hpp"
#include "popart/popx/opx.hpp"

namespace poplar {
class Graph;

namespace program {
class Sequence;
} // namespace program
} // namespace poplar

namespace pe = popops::expr;

namespace popart {
class Op;

namespace popx {
class Devicex;

AtanInplaceOpx::AtanInplaceOpx(Op *op, Devicex *devicex)
    : ElementWiseUnaryInplaceOpx(op, devicex, AtanComputex::get()) {
  verifyOp<AtanInplaceOp>(op, Onnx::CustomOperators::AtanInplace);
}

AtanOpx::AtanOpx(Op *op, Devicex *devicex)
    : ElementWiseUnaryOutplaceOpx(op, devicex, AtanComputex::get()) {
  verifyOp<AtanOp>(op, Onnx::Operators::Atan_7);
}

poplar::Tensor AtanComputex::outplace(poplar::program::Sequence &p,
                                      poplar::Graph &g,
                                      const poplar::Tensor &t,
                                      const poplar::DebugNameAndId &dnai,
                                      const std::string &s) const {
  auto outTensor = cloneNcopy(p, g, t, dnai);
  inplace(p, g, outTensor, dnai, s);
  return outTensor;
}

void AtanComputex::inplace(poplar::program::Sequence &p,
                           poplar::Graph &g,
                           const poplar::Tensor &t,
                           const poplar::DebugNameAndId &dnai,
                           const std::string &s) const {

  //   The formula for atan in the poplar device is giving problems, mapping not
  //   to the correct interval I rewrote it using the asin function which has
  //   proven to be working correctly
  std::vector<std::unique_ptr<popops::expr::Expr>> exprs;
  exprs.push_back(
      std::make_unique<pe::Add>(pe::Const(1.0f), pe::Mul(pe::_1, pe::_1)));
  exprs.push_back(std::make_unique<pe::Sqrt>(*exprs.back()));
  exprs.push_back(std::make_unique<pe::Divide>(pe::_1, *exprs.back()));
  exprs.push_back(std::make_unique<pe::Asin>(*exprs.back()));
  popops::mapInPlace(g, *exprs.back(), {t}, p, {dnai, s});
}

AtanGradOpx::AtanGradOpx(Op *op, Devicex *devicex) : Opx(op, devicex) {
  verifyOp<AtanGradOp>(op, Onnx::GradOperators::AtanGrad);
}

void AtanGradOpx::grow(poplar::program::Sequence &prog) const {
  const auto input     = getInTensor(AtanGradOp::getGradInIndex());
  const auto fwd_input = getInTensor(AtanGradOp::getFwdArgInIndex());

  // The derivative of the atan function can be constructed from normal
  // functions d/dx atan(x) = 1/(1+x^2)
  std::vector<std::unique_ptr<popops::expr::Expr>> exprs;
  exprs.push_back(
      std::make_unique<pe::Add>(pe::Const(1.0f), pe::Mul(pe::_2, pe::_2)));
  exprs.push_back(std::make_unique<pe::Divide>(pe::Const(1.0f), *exprs.back()));
  exprs.push_back(std::make_unique<pe::Mul>(pe::_1, *exprs.back()));

  auto output = popops::map(graph(),
                            *exprs.back(),
                            {input, fwd_input},
                            prog,
                            debugContext("inverse_tangent_grad"));

  setOutTensor(AtanGradOp::getOutIndex(), output);
}

namespace {
OpxCreator<AtanOpx> atanOpxCreator(Onnx::Operators::Atan_7);
OpxCreator<AtanInplaceOpx>
    atanInplaceOpxCreator(Onnx::CustomOperators::AtanInplace);
OpxCreator<AtanGradOpx> atanGradOpxCreator(Onnx::GradOperators::AtanGrad);
} // namespace

} // namespace popx
} // namespace popart
