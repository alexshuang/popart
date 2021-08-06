// Copyright (c) 2018 Graphcore Ltd. All rights reserved.
#include <memory>
#include <sstream>
#include <popart/error.hpp>
#include <popart/graph.hpp>
#include <popart/ir.hpp>
#include <popart/op/mean.hpp>
#include <popart/op/nll.hpp>
#include <popart/op/sum.hpp>
#include <popart/opmanager.hpp>
#include <popart/opserialiser.hpp>
#include <popart/tensor.hpp>
#include <popart/tensors.hpp>
#include <popart/topocons.hpp>

namespace popart {

std::unique_ptr<Op> NllOp::clone() const {
  return std::make_unique<NllOp>(*this);
}

std::vector<std::unique_ptr<Op>> NllOp::getGradOps() {
  std::vector<std::unique_ptr<Op>> upops;
  upops.emplace_back(std::make_unique<NllGradOp>(*this));
  return upops;
}

void NllOp::setup() {

  const auto &probsInInfo = inInfo(getProbsInIndex());
  const auto &labelInInfo = inInfo(getLabelInIndex());

  const auto &probsInShape = inShape(getProbsInIndex());
  const auto &labelInShape = inShape(getLabelInIndex());

  Shape expectedLabelShape{probsInShape.begin(), probsInShape.end() - 1};

  // We expect the labels input to have all but the last dimension of the probs
  // input. We check this here.
  if (probsInShape.size() == 0) {
    // Label input can't have the expected shape if prob input is of rank 0.
    throw error(
        "Invalid shape for prob tensor ({}) in Op {}. ", probsInInfo, str());
  }

  if (labelInShape != expectedLabelShape) {
    // Label doesn't have the expected shape.
    throw error("The label tensor ({}) must have shape {} to match all but "
                "the final dimension of the probabilities "
                "tensor ({}) in Op {}. ",
                labelInInfo,
                expectedLabelShape,
                probsInInfo,
                str());
  }

  if (!labelInInfo.getDataTypeInfo()->isFixedPoint()) {
    throw error(
        "Expected the label tensor NllOp to be fixed point, not the case "
        "for input with info: {}. This error for Op {}. ",
        labelInInfo,
        str());
  }

  Shape outShape({});

  if (getReductionType() == ReductionType::NoReduction) {
    outShape = labelInInfo.shape();
  }

  outInfo(getOutIndex()).set(probsInInfo.dataType(), outShape);
}

NllOp::NllOp(const OperatorIdentifier &_opid,
             const nonstd::optional<int> ignoreIndex,
             const ReductionType reduction,
             bool inputIsLogProbability,
             const Op::Settings &_settings)
    : LossOp(_opid, _settings, reduction), ignoreIndex_(ignoreIndex),
      inputIsLogProbability_(inputIsLogProbability) {}

void NllOp::appendOutlineAttributes(OpSerialiserBase &os) const {
  Op::appendOutlineAttributes(os);
  os.appendAttribute("reduction_type",
                     static_cast<int64_t>(getReductionType()));
  if (hasIgnoreIndex()) {
    os.appendAttribute("ignore_index", static_cast<int64_t>(*ignoreIndex_));
  }
  os.appendAttribute("input_is_log_probability", inputIsLogProbability_);
}

int NllOp::getIgnoreIndex() const {
  if (hasIgnoreIndex()) {
    return ignoreIndex_.value();
  } else {
    throw error("Cannot getIgnoreIndex for {}, as it has none", str());
  }
}

void NllGradOp::setup() {
  // gradient of probs has same shape as probs
  outInfo(getOutIndex()) = inInfo(getProbsInIndex());
}

NllGradOp::NllGradOp(const NllOp &op_)
    : Op(Onnx::CustomGradOperators::NllGrad, op_.getSettings()),
      lossId_(op_.outId(NllOp::getOutIndex())),
      reduction_(op_.getReductionType()),
      ignoreIndex_(op_.getOptionalIgnoreIndex()),
      inputIsLogProbability_(op_.inputIsLogProbability()) {}

std::unique_ptr<Op> NllGradOp::clone() const {
  return std::make_unique<NllGradOp>(*this);
}

const std::vector<GradInOutMapper> &NllGradOp::gradInputInfo() const {
  // input at index 0 : labelIn()
  // input at index 1 : probsIn()
  // input at index 2 : gradIn()
  static const std::vector<GradInOutMapper> inInfo = {
      {getLabelInIndex(), NllOp::getLabelInIndex(), GradOpInType::In},
      {getProbsInIndex(), NllOp::getProbsInIndex(), GradOpInType::In},
      {getGradInIndex(), NllOp::getOutIndex(), GradOpInType::GradOut}};
  return inInfo;
}

const std::map<int, int> &NllGradOp::gradOutToNonGradIn() const {
  // the grad-op output at index 0 corresponds
  // to the non-grad-op's input at index probsIn()
  // the op ONLY computes the gradient of probs,
  // no gradient for label (one could interpret the
  // int as a sparse vector, but not neat)
  static const std::map<int, int> outInfo = {
      {getOutIndex(), NllOp::getProbsInIndex()}};
  return outInfo;
}

void NllGradOp::appendOutlineAttributes(OpSerialiserBase &os) const {
  Op::appendOutlineAttributes(os);
  os.appendAttribute("reduction_type", static_cast<int64_t>(reduction_));
  if (hasIgnoreIndex()) {
    os.appendAttribute("ignore_index", static_cast<int64_t>(*ignoreIndex_));
  }
  os.appendAttribute("input_is_log_probability", inputIsLogProbability_);
}

int NllGradOp::getIgnoreIndex() const {
  if (hasIgnoreIndex()) {
    return ignoreIndex_.value();
  } else {
    throw error("Cannot getIgnoreIndex for {}, as it has none", str());
  }
}

float NllGradOp::getShardRescaleFactor(Op *const shardedOp,
                                       OutIndex index) const {
  if (reduction_ == ReductionType::Mean && index == getOutIndex()) {
    return static_cast<float>(shardedOp->inInfo(getProbsInIndex()).nelms()) /
           static_cast<float>(inInfo(getProbsInIndex()).nelms());
  }
  return Op::getShardRescaleFactor(shardedOp, index);
}

namespace {

static OpDefinition::DataTypes T1 = {DataType::FLOAT16, DataType::FLOAT};

static OpDefinition::DataTypes T2 = {DataType::INT32, DataType::UINT32};

static OpDefinition nlllossOpDef(
    {OpDefinition::Inputs({{"A", T1}, {"B", T2}}),
     OpDefinition::Outputs({{"C", T1}}),
     OpDefinition::Attributes({{"reduction", {"*"}},
                               {"ignore_index", {"*"}},
                               {"input_is_log_probability", {"*"}}})});

static OpCreator<NllOp> nlllossOpCreator(
    OpDefinitions({{Onnx::CustomOperators::Nll, nlllossOpDef}}),
    [](const OpCreatorInfo &info) {
      std::string reductionStr =
          info.attributes.getAttribute<Attributes::String>("reduction");
      ReductionType reduction = LossOp::reductionTypeFromString(reductionStr);

      bool inputIsLogProbability =
          info.attributes.getAttribute<Attributes::Int>("inputIsLogProbability",
                                                        0) != 0;

      nonstd::optional<int> ignoreIndex;
      if (info.attributes.hasAttribute("ignoreIndex")) {
        ignoreIndex =
            info.attributes.getAttribute<Attributes::Int>("ignoreIndex");
      }
      return std::unique_ptr<NllOp>(new NllOp(info.opid,
                                              ignoreIndex,
                                              reduction,
                                              inputIsLogProbability,
                                              info.settings));
    },
    true);

} // namespace

} // namespace popart
