// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <popart/op/instancenorm.hpp>
#include <popart/opmanager.hpp>
#include <popart/opserialiser.hpp>
#include <popart/tensor.hpp>
#include <popart/tensorindex.hpp>

#include "popart/attributes.hpp"
#include "popart/datatype.hpp"
#include "popart/names.hpp"
#include "popart/op.hpp"
#include "popart/operators.hpp"
#include "popart/tensorinfo.hpp"

namespace popart {
struct OperatorIdentifier;

InstanceNormOp::InstanceNormOp(const OperatorIdentifier &_opid,
                               float _epsilon,
                               const Op::Settings &settings_)
    : Op(_opid, settings_), epsilon(_epsilon) {}

std::unique_ptr<Op> InstanceNormOp::clone() const {
  return std::make_unique<InstanceNormOp>(*this);
}

std::vector<std::unique_ptr<Op>> InstanceNormOp::getGradOps() {
  std::vector<std::unique_ptr<Op>> upops;
  upops.emplace_back(std::make_unique<InstanceNormGradOp>(*this));
  return upops;
}

void InstanceNormOp::setup() {
  auto input_info  = inInfo(getInputInIndex());
  auto input_shape = input_info.shape();
  auto batch_size  = input_shape[0];
  auto features    = input_shape[1];

  outInfo(getOutIndex()) = input_info;

  if (!output->hasIndex(getMeanOutIndex())) {
    createAndConnectOutTensor(getMeanOutIndex(),
                              outTensor(getOutIndex())->id + "_mean");
  }
  outInfo(getMeanOutIndex()) = {input_info.dataType(), {batch_size * features}};

  if (!output->hasIndex(getInvStdDevOutIndex())) {
    createAndConnectOutTensor(getInvStdDevOutIndex(),
                              outTensor(getOutIndex())->id + "_invStdDev");
  }
  outInfo(getInvStdDevOutIndex()) = {input_info.dataType(),
                                     Shape{batch_size * features}};
}

void InstanceNormOp::appendOutlineAttributes(OpSerialiserBase &os) const {
  Op::appendOutlineAttributes(os);
  os.appendAttribute("epsilon", epsilon);
}

InstanceNormGradOp::InstanceNormGradOp(const InstanceNormOp &fwd_op)
    : Op(Onnx::GradOperators::InstanceNormalizationGrad, fwd_op.getSettings()) {
}

const std::vector<GradInOutMapper> &InstanceNormGradOp::gradInputInfo() const {
  static const std::vector<GradInOutMapper> inInfo = {
      {getInputInIndex(), InstanceNormOp::getInputInIndex(), GradOpInType::In},
      {getScaleInIndex(), InstanceNormOp::getScaleInIndex(), GradOpInType::In},
      {getOutGradInIndex(),
       InstanceNormOp::getInputInIndex(),
       GradOpInType::GradOut},
      {getMeanInIndex(), InstanceNormOp::getMeanOutIndex(), GradOpInType::Out},
      {getInvStdDevInIndex(),
       InstanceNormOp::getInvStdDevOutIndex(),
       GradOpInType::Out},
  };

  return inInfo;
}

const std::map<int, int> &InstanceNormGradOp::gradOutToNonGradIn() const {
  static const std::map<int, int> outInfo = {
      {getInputOutIndex(), InstanceNormOp::getInputInIndex()},
      {getScaleOutIndex(), InstanceNormOp::getScaleInIndex()},
      {getBOutIndex(), InstanceNormOp::getBInIndex()}};
  return outInfo;
}

void InstanceNormGradOp::setup() {
  const auto in_info  = inInfo(getOutGradInIndex());
  const auto in_type  = in_info.dataType();
  const auto in_shape = in_info.shape();

  outInfo(getInputOutIndex()) = in_info;
  outInfo(getScaleOutIndex()) = {in_type, {in_shape[1]}};
  outInfo(getBOutIndex())     = {in_type, {in_shape[1]}};
}

std::unique_ptr<Op> InstanceNormGradOp::clone() const {
  return std::make_unique<InstanceNormGradOp>(*this);
}

namespace {

static OpDefinition::DataTypes T = {DataType::FLOAT16, DataType::FLOAT};

static OpDefinition instanceNormOpDef({OpDefinition::Inputs({
                                           {"input", T},
                                           {"scale", T},
                                           {"B", T},
                                       }),
                                       OpDefinition::Outputs({{"output", T}}),
                                       OpDefinition::Attributes({
                                           {"epsilon", {"*"}},
                                       })});

static OpCreator<InstanceNormOp> instanceNormOpCreator(
    OpDefinitions({
        {Onnx::Operators::InstanceNormalization_6, instanceNormOpDef},
    }),
    [](const OpCreatorInfo &info) {
      // default epsilon is 10**(-5)
      float epsilon =
          info.attributes.getAttribute<Attributes::Float>("epsilon", 1e-5f);

      return std::unique_ptr<Op>(
          new InstanceNormOp(info.opid, epsilon, info.settings));
    },
    true);

} // namespace

} // namespace popart
