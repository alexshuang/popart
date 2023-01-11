// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#include <memory>
#include <utility>
#include <vector>
#include <poplar/Tensor.hpp>
#include <poplar/Type.hpp>
#include <popops/Cast.hpp>
#include <popops/ElementWise.hpp>
#include <popops/Expr.hpp>
#include <popops/ExprOp.hpp>
#include <poprand/RandomGen.hpp>
#include <popart/ir.hpp>
#include <popart/op/dropout.hpp>
#include <popart/popx/op/dropoutx.hpp>
#include <popart/popx/opxmanager.hpp>

#include "popart/names.hpp"
#include "popart/op.hpp"
#include "popart/operatoridentifier.hpp"
#include "popart/operators.hpp"
#include "popart/popx/op/elementwisex.hpp"
#include "popart/popx/opx.hpp"
#include "popart/tensorindex.hpp"

namespace poplar {
class Graph;
namespace program {
class Sequence;
} // namespace program
} // namespace poplar

namespace pe = popops::expr;

namespace popart {
namespace popx {
class Devicex;

namespace {

std::pair<poplar::Tensor, poplar::Tensor>
growDropout(poplar::Graph &graph,
            const poplar::Tensor &input,
            const poplar::Tensor &seed,
            const poplar::Tensor &refTensor,
            float ratio,
            const Opx &opx,
            poplar::program::Sequence &prog) {
  double dropoutProbability = 1. - static_cast<double>(ratio);

  // When ratio is outside of (0,1), an error is thrown in op creation,
  // so we avoid div/0 errors here.
  float scale = 1.f / (1.f - ratio);

  // Calculate the dropout mask using poplibs and a tensor of ones.
  auto mask = poprand::bernoulli(graph,
                                 &seed,
                                 0u,
                                 refTensor,
                                 refTensor.elementType(),
                                 dropoutProbability,
                                 prog,
                                 opx.debugContext("mask"));

  // Use the mask to multiply by the input tensor and scale up.
  auto dropout = popops::map(graph,
                             pe::Mul(pe::Mul(pe::_1, pe::_2), pe::Const(scale)),
                             {mask, input},
                             prog,
                             opx.debugContext("dropout"));

  return {dropout, mask};
}

} // namespace

DropoutOpx::DropoutOpx(Op *op, Devicex *devicex)
    : ElementWiseUnaryOpx(op, devicex) {
  verifyOp<DropoutOp>(op,
                      {Onnx::Operators::Dropout_6,
                       Onnx::Operators::Dropout_7,
                       Onnx::Operators::Dropout_10});
}

void DropoutOpx::grow(poplar::program::Sequence &prog) const {
  auto &op = getOp<DropoutOp>();

  if (op_p->getIr().canTrain()) {
    const poplar::Tensor &refTensor = get(op.getReferenceTensorId());

    if (op.getOutputMask()) {
      auto dropout_mask = growDropout(graph(),
                                      getInTensor(DropoutOp::getInIndex()),
                                      getInTensor(op.getSeedInIndex()),
                                      refTensor,
                                      op.getRatio(),
                                      *this,
                                      prog);
      auto dropout      = dropout_mask.first;
      auto mask         = dropout_mask.second;

      setOutTensor(op.getOutIndex(), dropout);
      if (op.output->hasIndex(DropoutOp::getMaskOutIndex())) {
        setOutTensor(
            DropoutOp::getMaskOutIndex(),
            popops::cast(
                graph(), mask, poplar::BOOL, prog, debugContext("mask")));
      }
    } else {
      double dropoutProbability = 1. - static_cast<double>(op.getRatio());
      double scale = 1. / (1. - static_cast<double>(op.getRatio()));
      auto dropout = poprand::dropout(graph(),
                                      &getInTensor(op.getSeedInIndex()),
                                      0u,
                                      getInTensor(DropoutOp::getInIndex()),
                                      refTensor,
                                      dropoutProbability,
                                      scale,
                                      prog,
                                      debugContext("dropout"));
      setOutTensor(op.getOutIndex(), dropout);
    }
  } else {
    // In inference mode, dropout is an identity function
    auto output = cloneNcopy(prog, getInTensor(DropoutOp::getInIndex()));
    setOutTensor(DropoutOp::getOutIndex(), output);
    // In inference mask is just a tensor of true values.
    if (op.getOutputMask()) {
      auto mask = getConst(poplar::BOOL,
                           getInTensor(DropoutOp::getInIndex()).shape(),
                           true,
                           "mask");
      setOutTensor(DropoutOp::getMaskOutIndex(), mask);
    }
  }
}

InputCreatorType DropoutOpx::getInputCreatorType(InIndex inIndex) const {
  if (inIndex == DropoutOp::getInIndex()) {
    return ElementWiseUnaryOpx::getInputCreatorType(inIndex);
  }
  return Opx::getInputCreatorType(inIndex);
}

namespace {
OpxCreator<DropoutOpx> dropoutOpxCreator({Onnx::Operators::Dropout_6,
                                          Onnx::Operators::Dropout_7,
                                          Onnx::Operators::Dropout_10});
} // namespace

} // namespace popx
} // namespace popart
