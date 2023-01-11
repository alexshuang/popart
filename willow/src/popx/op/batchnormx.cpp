// Copyright (c) 2018 Graphcore Ltd. All rights reserved.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <ext/new_allocator.h>
#include <tuple>
#include <vector>
#include <poplar/Graph.hpp>
#include <poplar/Tensor.hpp>
#include <poplar/Type.hpp>
#include <popnn/BatchNorm.hpp>
#include <popops/Cast.hpp>
#include <popops/ElementWise.hpp>
#include <popops/Expr.hpp>
#include <popops/ExprOp.hpp>
#include <poprithms/logging/timepartitionlogger.hpp>
#include <popart/ir.hpp>
#include <popart/op/batchnorm.hpp>
#include <popart/popx/op/batchnormx.hpp>
#include <popart/popx/op/normx.hpp>
#include <popart/popx/opxmanager.hpp>

#include "popart/op.hpp"
#include "popart/operatoridentifier.hpp"
#include "popart/operators.hpp"
#include "popart/sessionoptions.hpp"
#include "popart/vendored/optional.hpp"

namespace popart {
namespace popx {
class Devicex;
} // namespace popx
} // namespace popart

namespace poplar {
namespace program {
class Sequence;
} // namespace program

using Shape = std::vector<std::size_t>;
} // namespace poplar

namespace pe = popops::expr;

namespace popart {
namespace popx {

BatchNormOpx::BatchNormOpx(Op *op, Devicex *devicex) : NormOpx(op, devicex) {
  verifyOp<BatchNormOp>(op,
                        {Onnx::Operators::BatchNormalization_6,
                         Onnx::Operators::BatchNormalization_7,
                         Onnx::Operators::BatchNormalization_9,
                         Onnx::CustomOperators::BatchNormalization_1});
}

// Not clear to me if batchNormalise is meant update the mean/var then how can
// they be constant tensors
poplar::Tensor BatchNormOpx::batchNormalise(poplar::program::Sequence &prog,
                                            const poplar::Tensor &x,
                                            const poplar::Tensor &scale,
                                            const poplar::Tensor &b,
                                            const poplar::Tensor &mean,
                                            const poplar::Tensor &invSd) const {

  //  combinedMultiplicand = gamma / sDev
  //                       = gamma * invSd
  auto multiplicand = popops::map(graph(),
                                  pe::Mul(pe::_1, pe::_2),
                                  {scale, invSd},
                                  prog,
                                  debugContext("multiplicand"));

  // addend = beta - gamma * mean / sdDev
  //        = beta - gamma * mean * invSd
  auto addend = popops::map(graph(),
                            pe::Sub(pe::_1, pe::Mul(pe::_2, pe::_3)),
                            {b, multiplicand, mean},
                            prog,
                            debugContext("addend"));

  // Perform the batchNorm
  return popnn::bn::batchNormalise(
      graph(), x, multiplicand, addend, prog, debugContext("batchNormalise"));
}

static bool isZeroElementArray(const poplar::Shape &shape) {
  return std::all_of(
      shape.begin(), shape.end(), [](int dim) -> bool { return dim == 0; });
}

void BatchNormOpx::grow(poplar::program::Sequence &prog) const {

  const auto growTimeTracker =
      op_p->getIr().timePartitionLogger().scopedStopwatch(
          "Lowering BatchNorm to Poplar (\"grow\")");

  auto &op = getOp<BatchNormOp>();

  // Using the input names as per the onnx spec.
  auto x     = getInTensor(BatchNormOp::getXInIndex());
  auto scale = getInTensor(BatchNormOp::getScaleInIndex());
  auto b     = getInTensor(BatchNormOp::getBInIndex());
  auto mean  = getInTensor(BatchNormOp::getMeanInIndex());
  auto var   = getInTensor(BatchNormOp::getVarInIndex());

  // Variables to store the desired output shapes in case of spatial=False.
  std::vector<size_t> yShape            = x.shape();
  std::vector<size_t> otherOutputsShape = scale.shape();

  if (!op.getSpatial()) {
    // If spatial=False we must normalise every feature separately. We achieve
    // this with by transforming the inputs, running batchnorm as if
    // spatial=True and then transforming the output back again, e.g.:
    //
    // - Transforming input x from [N, C, D1, ..., Dn] to [N, CxD1x...xDn, 1].
    // - Transforming inputs scale, b, mean, var from [C, D1, ..., Dn] to
    // [CxD1x...xDn] (if available)
    // - Applying batch normalization (with spatial=True) on the transformed
    // inputs.
    // - Transforming output y from [N, CxD1x...xDn, 1] to [N, C, D1, ..., Dn].
    // - Transforming outputs runningMean, runningVar, batchMean, batchVar from
    // [CxD1x...xDn] to [C, D1, ..., Dn] (if available)

    const size_t NUM_FEATURES = x.numElements() / x.dim(0);

    // Reshape the inputs.
    x = x.reshape({x.dim(0), NUM_FEATURES, 1});

    scale = scale.reshape({NUM_FEATURES});
    b     = b.reshape({NUM_FEATURES});
    mean  = mean.reshape({NUM_FEATURES});
    var   = var.reshape({NUM_FEATURES});
  }

  // Lower the batch normalization operator for spatial=True.
  auto outputs = growSpatial(prog, op, x, scale, b, mean, var);

  if (!op.getSpatial()) {
    // As spatial=False we must transform the outputs back to their expected
    // form (see above). Note that some outputs are optional and depend on the
    // use case.
    outputs.y = outputs.y.reshape(yShape);

    if (outputs.mean)
      outputs.mean = outputs.mean->reshape(otherOutputsShape);
    if (outputs.var)
      outputs.var = outputs.var->reshape(otherOutputsShape);
    if (outputs.savedMean)
      outputs.savedMean = outputs.savedMean->reshape(otherOutputsShape);
    if (outputs.savedVar)
      outputs.savedVar = outputs.savedVar->reshape(otherOutputsShape);
  }

  // Now we need to set the output tensors, where available.
  setOutTensor(BatchNormOp::getYOutIndex(), outputs.y);

  if (outputs.mean)
    setOutTensor(BatchNormOp::getMeanOutIndex(), *outputs.mean);
  if (outputs.var)
    setOutTensor(BatchNormOp::getVarOutIndex(), *outputs.var);
  if (outputs.savedMean)
    setOutTensor(BatchNormOp::getSavedMeanOutIndex(), *outputs.savedMean);
  if (outputs.savedVar)
    setOutTensor(BatchNormOp::getSavedVarOutIndex(), *outputs.savedVar);
}

BatchNormOpx::GrowSpatialOutput
BatchNormOpx::growSpatial(poplar::program::Sequence &prog,
                          BatchNormOp &op,
                          poplar::Tensor &x,
                          poplar::Tensor &scale,
                          poplar::Tensor &b,
                          poplar::Tensor &mean,
                          poplar::Tensor &var) const {
  // ONNX specifies that it is using the biased version of running variance
  // (population size is N). However, Pytorch is using an unbiased running
  // variance (population size is N-1). Therefore, this has been parameterised
  // by the `unbiased_variance` attribute in PopART so that PopTorch can
  // specify the unbiased version.
  GrowSpatialOutput result;

  // Using the attribute names as per the onnx spec.
  float epsilon  = op.getEpsilon();
  float momentum = op.getMomentum();

  // Check for stable algorithm session option.
  bool stable_algo = op.getIr().getSessionOptions().enableStableNorm;

  if (op.isTraining()) {

    // Special case - zero sized array
    if (isZeroElementArray(x.shape())) {
      auto y =
          graph().addConstant(x.elementType(), x.shape(), 0, debugContext("y"));
      auto batchMean = graph().addConstant(
          mean.elementType(), {1}, NAN, debugContext("mean"));
      auto batchVar =
          graph().addConstant(var.elementType(), {1}, NAN, debugContext("var"));

      result = GrowSpatialOutput({y,
                                  batchMean,
                                  batchVar,
                                  nonstd::optional<poplar::Tensor>(),
                                  nonstd::optional<poplar::Tensor>()});
    } else {
      poplar::Tensor batchMean, invSd;
      std::tie(batchMean, invSd) =
          popnn::bn::batchNormStatistics(graph(),
                                         x,
                                         epsilon,
                                         prog,
                                         false,
                                         stable_algo,
                                         poplar::FLOAT,
                                         debugContext("normStats"));

      // batch normalise
      auto y = batchNormalise(prog, x, scale, b, batchMean, invSd);

      if (op.useUnbiasedVariance()) {
        // We have to convert the invSd to the unbiased version
        const float numElements = x.numElements() / x.dim(1);
        const float inv_factor  = (numElements - 1) / numElements;

        invSd = popops::map(graph(),
                            pe::Mul(pe::_1, pe::Sqrt(pe::Const(inv_factor))),
                            {invSd},
                            prog,
                            debugContext("unbiasInvSd"));
      }

      // Ensure batch mean is the same type as mean so that running mean can
      // be calculated
      if (batchMean.elementType() != mean.elementType()) {
        batchMean = popops::cast(graph(),
                                 batchMean,
                                 mean.elementType(),
                                 prog,
                                 debugContext("cast_batchMean"));
      }

      auto batchVar =
          convertInvSdToVar(prog, invSd, epsilon, var.elementType());

      // Calculate the running mean
      auto runningMean = popops::map(
          graph(),
          pe::Add(pe::Mul(pe::Sub(pe::Const(1), pe::Const(momentum)), pe::_2),
                  pe::Mul(pe::Const(momentum), pe::_1)),
          {mean, batchMean},
          prog,
          debugContext("runningMean"));

      // Calculate the running variance
      auto runningVar = popops::map(
          graph(),
          pe::Add(pe::Mul(pe::Sub(pe::Const(1), pe::Const(momentum)), pe::_2),
                  pe::Mul(pe::Const(momentum), pe::_1)),
          {var, batchVar},
          prog,
          debugContext("runningVar"));

      // return the results
      result =
          GrowSpatialOutput({y, runningMean, runningVar, batchMean, batchVar});
    }
  } else {
    // When testing

    // Special case - zero sized array
    if (isZeroElementArray(x.shape())) {
      auto y =
          graph().addConstant(x.elementType(), x.shape(), 0, debugContext("y"));

      result = GrowSpatialOutput({y,
                                  nonstd::optional<poplar::Tensor>(),
                                  nonstd::optional<poplar::Tensor>(),
                                  nonstd::optional<poplar::Tensor>(),
                                  nonstd::optional<poplar::Tensor>()});
    } else {
      // convert variant to inverse standard deviation
      auto invSd = convertVarToInvSd(prog, var, epsilon, x.elementType());

      // mean might have a different type so cast is required before
      // batchNormalise calculation
      if (mean.elementType() != x.elementType()) {
        mean = popops::cast(
            graph(), mean, x.elementType(), prog, debugContext("cast_mean"));
      }

      // batchnorm
      auto y = batchNormalise(prog, x, scale, b, mean, invSd);

      // return the result
      result = GrowSpatialOutput({y,
                                  nonstd::optional<poplar::Tensor>(),
                                  nonstd::optional<poplar::Tensor>(),
                                  nonstd::optional<poplar::Tensor>(),
                                  nonstd::optional<poplar::Tensor>()});
    }
  }

  return result;
}

std::tuple<poplar::Tensor, poplar::Tensor, poplar::Tensor>
BatchNormGradOpx::batchNormaliseGrad(poplar::program::Sequence &prog,
                                     const poplar::Tensor &x,
                                     const poplar::Tensor &scale,
                                     const poplar::Tensor &mean,
                                     const poplar::Tensor &invSd,
                                     const poplar::Tensor &yGrad) const {

  poplar::Tensor xGrad, scaleGrad, bGrad;

  poplar::Tensor xWhitened = popnn::bn::batchNormWhiten(
      graph(), x, mean, invSd, prog, debugContext("WhitenedActs"));

  // Compute the delta for the operand
  xGrad = popnn::bn::batchNormGradients(graph(),
                                        xWhitened,
                                        yGrad,
                                        invSd,
                                        scale,
                                        prog,
                                        poplar::FLOAT,
                                        debugContext("operandGrad"));

  // Compute the deltas for scaled and offset
  std::tie(scaleGrad, bGrad) =
      popnn::bn::batchNormParamGradients(graph(),
                                         xWhitened,
                                         yGrad,
                                         prog,
                                         poplar::FLOAT,
                                         debugContext("scaleOffsetGrads"));

  return std::make_tuple(xGrad, scaleGrad, bGrad);
}

BatchNormGradOpx::BatchNormGradOpx(Op *op, Devicex *devicex)
    : NormOpx(op, devicex) {
  verifyOp<BatchNormGradOp>(op, Onnx::GradOperators::BatchNormalizationGrad);
}

void BatchNormGradOpx::grow(poplar::program::Sequence &prog) const {

  auto &op = getOp<BatchNormGradOp>();

  // Inputs
  auto x     = getInTensor(BatchNormGradOp::getXInIndex());
  auto scale = getInTensor(BatchNormGradOp::getScaleInIndex());
  auto mean  = getInTensor(BatchNormGradOp::getMeanInIndex());
  auto var   = getInTensor(BatchNormGradOp::getVarInIndex());
  auto yGrad = getInTensor(BatchNormGradOp::getYGradInIndex());

  // Variables to store the desired output shapes in case of spatial=False.
  std::vector<size_t> xShape            = yGrad.shape();
  std::vector<size_t> otherOutputsShape = scale.shape();

  if (!op.getSpatial()) {
    // If spatial=False we must do some reshaping here (akin to BatchNormOpx).
    const size_t NUM_FEATURES = x.numElements() / x.dim(0);

    // Reshape the inputs.
    x     = x.reshape({x.dim(0), NUM_FEATURES, 1});
    scale = scale.reshape({NUM_FEATURES});
    mean  = mean.reshape({NUM_FEATURES});
    var   = var.reshape({NUM_FEATURES});
    yGrad = yGrad.reshape({x.dim(0), NUM_FEATURES, 1});
  }

  auto outputs = growSpatial(prog, op, x, scale, mean, var, yGrad);

  if (!op.getSpatial()) {
    // As spatial=False we must undo the reshaping here (aking to BatchNormOpx).
    outputs.xGrad     = outputs.xGrad.reshape(xShape);
    outputs.scaleGrad = outputs.scaleGrad.reshape(otherOutputsShape);
    outputs.bGrad     = outputs.bGrad.reshape(otherOutputsShape);
  }

  setOutTensor(BatchNormGradOp::getXOutIndex(), outputs.xGrad);
  setOutTensor(BatchNormGradOp::getScaleOutIndex(), outputs.scaleGrad);
  setOutTensor(BatchNormGradOp::getBOutIndex(), outputs.bGrad);
}

BatchNormGradOpx::GrowSpatialOutput
BatchNormGradOpx::growSpatial(poplar::program::Sequence &prog,
                              BatchNormGradOp &op,
                              poplar::Tensor &x,
                              poplar::Tensor &scale,
                              poplar::Tensor &mean,
                              poplar::Tensor &var,
                              poplar::Tensor &yGrad) const {
  GrowSpatialOutput result;

  // Attributes
  float epsilon = op.getEpsilon();

  // Special case - zero sized array
  if (isZeroElementArray(x.shape())) {
    auto xGrad = graph().addConstant(
        x.elementType(), x.shape(), 0, debugContext("xGrad"));
    auto scaleGrad =
        graph().addConstant(x.elementType(), {1}, 0, debugContext("scaleGrad"));
    auto bGrad =
        graph().addConstant(x.elementType(), {1}, 0, debugContext("bGrad"));
    result.xGrad     = xGrad;
    result.scaleGrad = scaleGrad;
    result.bGrad     = bGrad;
  } else {
    auto invSd = convertVarToInvSd(prog, var, epsilon, x.elementType());

    // mean might have a different type so cast is required before
    // batchNormaliseGrad calculation
    if (mean.elementType() != x.elementType()) {
      mean = popops::cast(
          graph(), mean, x.elementType(), prog, debugContext("cast_mean"));
    }

    // batchnormgrad
    poplar::Tensor xGrad, scaleGrad, bGrad;
    std::tie(xGrad, scaleGrad, bGrad) =
        batchNormaliseGrad(prog, x, scale, mean, invSd, yGrad);

    // return the results
    result.xGrad     = xGrad;
    result.scaleGrad = scaleGrad;
    result.bGrad     = bGrad;
  }

  return result;
}

namespace {
OpxCreator<BatchNormOpx>
    batchNormOpxCreator({Onnx::Operators::BatchNormalization_6,
                         Onnx::Operators::BatchNormalization_7,
                         Onnx::Operators::BatchNormalization_9,
                         Onnx::CustomOperators::BatchNormalization_1});
OpxCreator<BatchNormGradOpx>
    batchNormGradOpxCreator(Onnx::GradOperators::BatchNormalizationGrad);
} // namespace

} // namespace popx
} // namespace popart
