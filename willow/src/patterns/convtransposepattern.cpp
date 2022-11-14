// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#include <cstdint>
#include <string>
#include <vector>
#include <popart/graph.hpp>
#include <popart/ir.hpp>
#include <popart/op/conv.hpp>
#include <popart/op/convtranspose.hpp>
#include <popart/patterns/convtransposepattern.hpp>
#include <popart/patterns/patterns.hpp>
#include <popart/tensor.hpp>

#include "popart/logging.hpp"
#include "popart/op.hpp"
#include "popart/op/convbase.hpp"
#include "popart/operatoridentifier.hpp"
#include "popart/operators.hpp"
#include "popart/tensorinfo.hpp"
#include "popart/util.hpp"

namespace popart {

bool ConvTransposePattern::matches(Op *op) const {
  return op->isConvertibleTo<ConvTransposeOp>();
}

std::vector<const Tensor *> ConvTransposePattern::touches(Op *) const {
  return {};
}

bool ConvTransposePattern::apply(Op *op) const {
  const auto convTranspose = dynamic_cast<ConvTransposeOp *>(op);
  auto &graph              = op->getGraph();
  auto &ir                 = op->getIr();

  auto inTensor = convTranspose->inTensor(ConvTransposeOp::getInIndex());
  auto kernelTensor =
      convTranspose->inTensor(ConvTransposeOp::getWeightsInIndex());
  auto outTensor = convTranspose->outTensor(ConvTransposeOp::getOutIndex());

  // remember if this had a log2 scale tensor input before we disconnect
  // inputs because we want to reconnect them later to the conv op
  Tensor *log2ScaleTensor;
  auto isPow2ScaledConvTranspose = convTranspose->isPow2ScaledConvTranspose();
  if (isPow2ScaledConvTranspose) {
    log2ScaleTensor =
        convTranspose->inTensor(ConvTransposeOp::getLog2ScaleInIndex());
  }

  op->disconnectAllInputs();
  op->disconnectAllOutputs();

  auto flip = dynamic_cast<ConvFlipWeightsOp *>(
      makeReplacementOpInIr(Onnx::CustomOperators::ConvFlipWeights, op));

  flip->setConvOptions(convTranspose->convOpts);

  // Configure the flip weight op
  flip->connectInTensor(ConvFlipWeightsOp::getInIndex(), kernelTensor->id);
  flip->createAndConnectOutTensor(
      ConvFlipWeightsOp::getOutIndex(),
      ir.createIntermediateTensorId(kernelTensor->id));

  flip->setParameters(convTranspose->params);
  flip->setGroupReshape(true);
  flip->setup();

  OperatorIdentifier gradOpId = Onnx::Operators::Conv_1;

  auto paddingNum    = kernelTensor->info.dim(2) - 1;
  int convDimensions = inTensor->info.rank() - 2;
  std::vector<int64_t> padding(convDimensions * 2, paddingNum);

  auto strides   = convTranspose->strides;
  auto dilations = convTranspose->dilations;

  logging::debug("Creating ConvOp");
  logging::debug("  strides: {}", strides);
  logging::debug("  padding: {}", padding);
  logging::debug("  dilations: {}", dilations);

  Op *newOp    = graph.createOp<ConvOp>(Onnx::Operators::Conv_1,
                                     convTranspose->settings,
                                     strides,
                                     padding,
                                     dilations,
                                     convTranspose->group,
                                     convTranspose->padType,
                                     convTranspose->convOpts);
  ConvOp *conv = dynamic_cast<ConvOp *>(newOp);
  transferBaseProperties(convTranspose, conv);

  conv->connectInTensor(ConvOp::getDataInIndex(), inTensor->id);
  conv->connectInTensor(ConvOp::getWeightsInIndex(), flip->outId(0));

  if (isPow2ScaledConvTranspose) {
    conv->connectInTensor(ConvOp::getLog2ScaleInIndex(), log2ScaleTensor->id);
  }

  conv->connectOutTensor(ConvOp::getOutIndex(), outTensor->id);

  conv->restoreAttributesFromParams({convTranspose->params});
  conv->setup();

  flip->setParameters(dynamic_cast<ConvOp *>(conv)->getParameters());
  flip->setup();

  // Remove the ConvTransposeOp.
  graph.eraseOp(op->id);

  return true;
}

namespace {
static PatternCreator<ConvTransposePattern>
    convTransposePattern("ConvTranspose", true, true);
}

} // namespace popart
