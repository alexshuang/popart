// Copyright (c) 2018 Graphcore Ltd. All rights reserved.
#include <cstddef>
#include <cstdint>
#include <ext/new_allocator.h>
#include <vector>
#include <poplar/Graph.hpp>
#include <poplar/Tensor.hpp>
#include <poplar/Type.hpp>
#include <popops/Cast.hpp>
#include <popops/ElementWise.hpp>
#include <popops/Encoding.hpp>
#include <popops/Expr.hpp>
#include <popops/ExprOp.hpp>
#include <popops/OperationDef.hpp>
#include <popops/Reduce.hpp>
#include <popart/names.hpp>
#include <popart/op/nll.hpp>
#include <popart/popx/op/nllx.hpp>
#include <popart/popx/opxmanager.hpp>

#include "popart/graphcoreoperators.hpp"
#include "popart/op.hpp"
#include "popart/popx/opx.hpp"

namespace poplar {
namespace program {
class Sequence;
} // namespace program
} // namespace poplar

namespace pe = popops::expr;

namespace popart {
namespace popx {
class Devicex;

NllOpx::NllOpx(Op *op, Devicex *devicex) : Opx(op, devicex) {
  verifyOp<NllOp>(op, Onnx::CustomOperators::Nll);
}

void NllOpx::grow(poplar::program::Sequence &prog) const {
  const NllOp &op = getOp<NllOp>();

  const poplar::Tensor &probs = getInTensor(NllOp::getProbsInIndex());
  const poplar::Tensor &label = getInTensor(NllOp::getLabelInIndex());
  poplar::Tensor probs2D;
  poplar::Tensor label1D;
  poplar::Tensor oneHot;

  flattenAndEncodeOneHot(*this, prog, probs, label, probs2D, label1D, oneHot);

  // oneHot, from a tensor which is sparse with a single 1 per row,
  //           to a tensor which is sparse with a single p per row.
  popops::mapInPlace(graph(),
                     popops::expr::BinaryOpType::MULTIPLY,
                     oneHot,
                     probs2D,
                     prog,
                     debugContext("mul"));

  // sum rows, so that just the p corresponding to the label remains
  poplar::Tensor reduction = popops::reduce(graph(),
                                            oneHot,
                                            {1},
                                            {popops::Operation::ADD},
                                            prog,
                                            debugContext("reduce"));

  // Create an epsilon value
  double eps_f = 1.0e-7;

  // if using fp16, increase the eps to avoid underfloat
  if (probs.elementType() == poplar::HALF)
    eps_f = 6.104e-05;

  if (!op.inputIsLogProbability()) {
    poplar::Tensor eps = getConst(probs.elementType(), {1}, eps_f, "epsilon");

    // Take max of prob and eps to reduction make sure it does not have any
    // 0's and log it,
    popops::mapInPlace(graph(),
                       pe::Log(pe::Max(pe::_1, pe::_2)),
                       {reduction, eps},
                       prog,
                       debugContext("LogMax"));
  }

  if (op.hasIgnoreIndex()) {
    auto lossMask = applyMaskInPlaceForIgnoredIndex(
        *this, reduction, label1D, op.getIgnoreIndex(), prog);
  }

  if (op.getReductionType() == ReductionType::NoReduction) {
    handleLossOutNotReducedToScalar(*this, reduction, label, label1D, prog);
  } else {
    handleLossOutReducedToScalar(*this,
                                 op.hasIgnoreIndex(),
                                 op.hasIgnoreIndex() ? op.getIgnoreIndex() : 0,
                                 op.getReductionType() == ReductionType::Mean,
                                 reduction,
                                 label1D,
                                 prog,
                                 op.getOutIndex());
  }
}

void NllOpx::flattenAndEncodeOneHot(const Opx &opx,
                                    poplar::program::Sequence &prog,
                                    const poplar::Tensor &probs,
                                    const poplar::Tensor &label,
                                    poplar::Tensor &probs2D,
                                    poplar::Tensor &label1D,
                                    poplar::Tensor &oneHot) {
  // Expect an N-d Probs tensor and (N-1)-d Label tensor.
  // Probs - a tensor of shape [D1, ..., DN, NumClasses]
  // Label - a tensor of shape [D1, ..., DN], where each element is a
  //         class index
  // If N > 2, then the inputs are flattened across all dimensions
  // (except the outer Classes dim in the case of Probs)
  probs2D = probs.flatten(0, probs.rank() - 1);
  label1D = label.flatten();
  // Tensor taking one-hot encoded output must be 2 dimensional
  oneHot = opx.graph().clone(
      probs2D.elementType(), probs2D, opx.debugContext("oneHot"));
  popops::encodeOneHot(
      opx.graph(), label1D, oneHot, prog, opx.debugContext("nll"));
}

void NllOpx::applyScalingInPlaceForMeanReduction(
    const Opx &opx,
    poplar::Tensor t,
    poplar::Tensor scale,
    poplar::program::Sequence &prog) {
  double totalSamples = static_cast<double>(t.dim(0));

  auto combined_scale = popops::div(opx.graph(),
                                    scale,
                                    totalSamples,
                                    prog,
                                    opx.debugContext("combinedLossScale"));

  // Note: if combined_scale is fp32 and t is fp16, the downcast is handled
  // here by poplar
  popops::mulInPlace(
      opx.graph(), t, combined_scale, prog, opx.debugContext("mean"));
}

void NllOpx::applyScalingInPlaceForMeanReductionWithIgnoreIndex(
    const Opx &opx,
    poplar::Tensor t,
    poplar::Tensor scale,
    poplar::Tensor mask,
    poplar::program::Sequence &prog) {
  // Determine the scale-factor for mean reduction dynamically from the
  // mask.
  // Any sample whose label index is the 'ignore index' should not be
  // counted when scaling the loss/loss grad
  auto numNonIgnoredSamples =
      popops::reduce(opx.graph(),
                     mask.flatten(),
                     {0},
                     {popops::Operation::ADD},
                     prog,
                     opx.debugContext("numNonIgnoredSamples"));

  // If the numNonIgnoredSamples is equal to zero, we have ignored all label
  // data, in this case return zero loss. Do this by taking
  // min(numIgnoredSamples, 1) and letting the result be 0 / 1 (where scale = 0
  // due to the ignored labels). See ~T36441~
  auto min_1 = opx.graph().addConstant(
      numNonIgnoredSamples.elementType(), {}, 1, opx.debugContext("const_1"));
  opx.graph().setTileMapping(min_1, 0);
  popops::maxInPlace(opx.graph(),
                     numNonIgnoredSamples,
                     min_1,
                     prog,
                     opx.debugContext("numNonIgnoredSamples_min"));

  // popops::div requires inputs of the same data type. We support the mixed
  // case where gradIn is fp32 but the mask tensor is fp32. So here we upcast
  // if required
  if (numNonIgnoredSamples.elementType() != scale.elementType()) {
    numNonIgnoredSamples = popops::cast(opx.graph(),
                                        numNonIgnoredSamples,
                                        scale.elementType(),
                                        prog,
                                        opx.debugContext("cast"));
  }

  auto combined_scale = popops::div(opx.graph(),
                                    scale,
                                    numNonIgnoredSamples,
                                    prog,
                                    opx.debugContext("combinedLossScale"));

  // Note: if combined_scale is fp32 and t is fp16, the downcast is handled
  // here by poplar
  popops::mulInPlace(
      opx.graph(), t, combined_scale, prog, opx.debugContext("mean"));
}

poplar::Tensor
NllOpx::applyMaskInPlaceForIgnoredIndex(const Opx &opx,
                                        poplar::Tensor t,
                                        poplar::Tensor labels,
                                        int ignoreIndex,
                                        poplar::program::Sequence &prog) {
  // Get the scalar ignoreIndex tensor. If it doesn't already
  // exist, create it
  auto ignoreIndexTensor = opx.graph().addConstant(
      labels.elementType(), {}, ignoreIndex, opx.debugContext("ignoreIndex"));
  opx.graph().setTileMapping(ignoreIndexTensor, 0);
  // Create the mask
  auto lossMaskBool = popops::map(opx.graph(),
                                  popops::expr::BinaryOpType::NOT_EQUAL,
                                  labels,
                                  ignoreIndexTensor,
                                  prog,
                                  opx.debugContext("notEqual"));
  auto lossMask     = popops::cast(opx.graph(),
                               lossMaskBool,
                               t.elementType(),
                               prog,
                               opx.debugContext("cast"));

  if (t.rank() != lossMask.rank()) {
    // If required, broadcast lossMask on the final dimension.
    auto t_shape          = t.shape();
    t_shape[t.rank() - 1] = 1;
    lossMask              = lossMask.reshape(t_shape);
  }

  // Apply the mask
  popops::mapInPlace(opx.graph(),
                     popops::expr::BinaryOpType::MULTIPLY,
                     t,
                     lossMask,
                     prog,
                     opx.debugContext("masked"));

  return lossMask;
}

void NllOpx::handleLossOutNotReducedToScalar(const Opx &opx,
                                             poplar::Tensor &reduction,
                                             const poplar::Tensor &label,
                                             poplar::Tensor &label1D,
                                             poplar::program::Sequence &prog) {
  popops::mapInPlace(opx.graph(),
                     popops::expr::UnaryOpType::NEGATE,
                     reduction,
                     prog,
                     opx.debugContext("neg"));
  // One loss per sample, so the output is reshaped to match label input shape
  reduction = reduction.reshape(label.shape());

  opx.setOutTensor(0, reduction);
}

void NllOpx::handleLossOutReducedToScalar(const Opx &opx,
                                          bool hasIgnoreIndex,
                                          int64_t ignoreIndex,
                                          bool meanReduce,
                                          poplar::Tensor &reduction,
                                          poplar::Tensor &label1D,
                                          poplar::program::Sequence &prog,
                                          const OutIndex outIdx) {
  double scale = 1.0;

  if (meanReduce) {
    if (hasIgnoreIndex) {
      auto lossMask = applyMaskInPlaceForIgnoredIndex(
          opx, reduction, label1D, static_cast<int>(ignoreIndex), prog);

      auto scaleT = opx.getConst(reduction.elementType(), {}, 1.0, "One");

      applyScalingInPlaceForMeanReductionWithIgnoreIndex(
          opx, reduction, scaleT, lossMask, prog);
      // Leave scale as 1.0 as already scaled
    } else {
      double totalSamples = static_cast<double>(reduction.dim(0));
      scale               = 1.0 / totalSamples;
    }
  }

  // Scale (possibly) and negate (-scale)
  auto t_scale = opx.getConst(poplar::FLOAT, {}, -scale, "scale");

  auto scalar = popops::reduce(opx.graph(),
                               reduction,
                               {0},
                               {popops::Operation::ADD, false, t_scale},
                               prog,
                               opx.debugContext("toScalar"));
  opx.setOutTensor(outIdx, scalar);
}

void NllOpx::handleLossGradScaling(const Opx &opx,
                                   bool hasIgnoreIndex,
                                   int64_t ignoreIndex,
                                   bool meanReduce,
                                   poplar::Tensor &oneHot,
                                   poplar::Tensor &gradIn,
                                   poplar::Tensor &label1D,
                                   poplar::program::Sequence &prog) {
  // To ensure gradIn has a broadcastable shape, add extra singleton
  // dimensions
  for (unsigned dim = 0; dim < oneHot.rank(); dim++) {
    if (dim > gradIn.rank() - 1) {
      gradIn = gradIn.expand({dim});
    }
  }

  // Apply mask before reduction, so that ignored class doesn't
  // contribute to the loss gradient
  if (hasIgnoreIndex) {
    auto lossMask = NllOpx::applyMaskInPlaceForIgnoredIndex(
        opx, oneHot, label1D, ignoreIndex, prog);

    if (meanReduce) {
      NllOpx::applyScalingInPlaceForMeanReductionWithIgnoreIndex(
          opx, oneHot, gradIn, lossMask, prog);
    }
  } else {
    if (meanReduce) {
      NllOpx::applyScalingInPlaceForMeanReduction(opx, oneHot, gradIn, prog);
    }
  }

  if (!meanReduce) {
    popops::mulInPlace(
        opx.graph(), oneHot, gradIn, prog, opx.debugContext("scaledGradIn"));
  }
}

NllGradOpx::NllGradOpx(Op *op, Devicex *devicex) : Opx(op, devicex) {
  verifyOp<NllGradOp>(op, Onnx::CustomGradOperators::NllGrad);
}

// NllGrad depends on whether the input contains log-probabilities:
//
// 1) inputIsLogProbability == false (default)
//    loss         = -ln (p_l), where p_l is the probability at "label" so
//
//                     0     if i != l
//    d_loss / d_p = -1/p_i if i == l
//
// 2) inputIsLogProbability == true (pytorch convention)
//    loss         = -p_l, defined as above
//
//                     0   if i != l
//    d_loss / d_p = -p_i if i == l

void NllGradOpx::grow(poplar::program::Sequence &prog) const {
  const NllGradOp &gradOp     = getOp<NllGradOp>();
  const poplar::Tensor &probs = getInTensor(NllGradOp::getProbsInIndex());
  const poplar::Tensor &label = getInTensor(NllGradOp::getLabelInIndex());
  poplar::Tensor gradIn       = getInTensor(NllGradOp::getGradInIndex());

  // As for NllOpx, flatten outer dimensions if rank(probs) > 2
  auto probs2D = probs.flatten(0, probs.rank() - 1);
  auto label1D = label.flatten();

  // inverse probabilities, we take max(eps, p) to make division safe
  float eps = 1e-10f;

  // If working with float16 increase the eps to avoid underfloat
  // Note that because of the division we would ideally use 1/65500
  // but this will underflow.
  if (probs.elementType() == poplar::HALF) {
    eps = 6.104e-05f;
  }

  // oneHot: initialised to be 1 at position "label", 0 elsewhere.
  auto oneHot =
      graph().clone(probs2D.elementType(), probs2D, debugContext("oneHot"));

  popops::encodeOneHot(graph(), label1D, oneHot, prog, debugContext("nll"));

  if (gradOp.inputIsLogProbability()) {
    // oneHot: becomes -1 at position "label", 0 elsewhere.
    popops::mapInPlace(graph(),
                       pe::UnaryOpType::NEGATE,
                       oneHot,
                       prog,
                       debugContext("negOneHot"));
  } else {
    auto smallConst =
        graph().addConstant(probs.elementType(), {1}, eps, debugContext("eps"));
    graph().setTileMapping(smallConst, 0);
    // oneHot: set to -1/p at position "label", 0 elsewhere.
    popops::mapInPlace(graph(),
                       pe::Divide(pe::Neg(pe::_1), pe::Max(pe::_2, pe::_3)),
                       {oneHot, probs2D, smallConst},
                       prog,
                       debugContext("NegDivSafeProbs"));
  }

  // Output is reshaped to match probs input shape
  oneHot = oneHot.reshape(probs.shape());

  NllOpx::handleLossGradScaling(
      *this,
      gradOp.hasIgnoreIndex(),
      gradOp.hasIgnoreIndex() ? gradOp.getIgnoreIndex() : 0,
      gradOp.getReductionType() == ReductionType::Mean,
      oneHot,
      gradIn,
      label1D,
      prog);

  setOutTensor(0, oneHot);
} // namespace popx

namespace {
static OpxCreator<NllOpx> nllOpxCreator(Onnx::CustomOperators::Nll);
static OpxCreator<NllGradOpx>
    nllGradOpxCreator(Onnx::CustomGradOperators::NllGrad);
} // namespace

} // namespace popx
} // namespace popart
