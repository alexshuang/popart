// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#include "popart/popx/debugcontextx.hpp"
#include <snap/Graph.hpp>
#include <snap/Program.hpp>
#include <snap/Tensor.hpp>
#include <string>
#include <popnn/NonLinearity.hpp>
#include <popnn/NonLinearityDef.hpp>
#include <popops/Rearrange.hpp>
#include <popart/op/gelu.hpp>
#include <popart/popx/op/gelux.hpp>
#include <popart/popx/opxmanager.hpp>

#include "popart/graphcoreoperators.hpp"
#include "popart/popx/op/elementwisex.hpp"
#include "popart/popx/popopx.hpp"

namespace popart {
class Op;

namespace popx {
class Devicex;

GeluOpx::GeluOpx(Op *op, Devicex *devicex)
    : ElementWiseUnaryOutplaceOpx(op, devicex, GeluComputex::get()) {
  verifyOp<GeluOp>(op, {Onnx::CustomOperators::Gelu_1});
}

snap::Tensor GeluComputex::outplace(snap::program::Sequence &prog,
                                    snap::Graph &graph,
                                    const snap::Tensor &tensor,
                                    const poplar::DebugNameAndId &dnai,
                                    const std::string &debug_prefix) const {
  auto out_tensor = cloneNcopy(prog, graph, tensor, dnai);
  inplace(prog, graph, out_tensor, dnai, debug_prefix);
  return out_tensor;
}

void GeluComputex::inplace(snap::program::Sequence &prog,
                           snap::Graph &graph,
                           const snap::Tensor &tensor,
                           const poplar::DebugNameAndId &dnai,
                           const std::string &debug_prefix) const {
  popnn::nonLinearityInPlace(graph.getPoplarGraph(),
                             popnn::NonLinearityType::GELU,
                             tensor.getPoplarTensor(),
                             prog.getPoplarSequence(),
                             {dnai, debug_prefix});
}

GeluInplaceOpx::GeluInplaceOpx(Op *op, Devicex *devicex)
    : ElementWiseUnaryInplaceOpx(op, devicex, GeluComputex::get()) {
  verifyOp<GeluInplaceOp>(op, Onnx::CustomOperators::GeluInplace);
}

GeluGradOpx::GeluGradOpx(Op *op, Devicex *devicex) : PopOpx(op, devicex) {
  verifyOp<GeluGradOp>(op, Onnx::GradOperators::GeluGrad);
}

void GeluGradOpx::grow(snap::program::Sequence &prog) const {
  const auto grad = getInTensor(GeluGradOp::getGradInIndex()).getPoplarTensor();
  const auto input =
      getInTensor(GeluGradOp::getFwdArgInIndex()).getPoplarTensor();

  auto gradRearranged =
      popops::rearrange::regroupIfBeneficial(graph().getPoplarGraph(),
                                             grad,
                                             input,
                                             prog.getPoplarSequence(),
                                             debugContext("regroup"));

  auto output = popnn::nonLinearityInputGradient(graph().getPoplarGraph(),
                                                 popnn::NonLinearityType::GELU,
                                                 input,
                                                 gradRearranged,
                                                 prog.getPoplarSequence(),
                                                 debugContext("gelu_grad"));

  setOutTensor(GeluGradOp::getOutIndex(), snap::Tensor{output, graph()});
}

namespace {
OpxCreator<GeluOpx> geluOpxCreator(Onnx::CustomOperators::Gelu_1);
OpxCreator<GeluInplaceOpx>
    geluInplaceOpxCreator(Onnx::CustomOperators::GeluInplace);
OpxCreator<GeluGradOpx> geluGradOpxCreator(Onnx::GradOperators::GeluGrad);
} // namespace

} // namespace popx
} // namespace popart
