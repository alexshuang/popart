// Copyright (c) 2018 Graphcore Ltd. All rights reserved.
#include "popart/util/float8util.hpp"
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <popart/error.hpp>
#include <popart/ir.hpp>
#include <popart/op/conv.hpp>
#include <popart/opmanager.hpp>
#include <popart/opserialiser.hpp>

#include "popart/attributes.hpp"
#include "popart/datatype.hpp"
#include "popart/logging.hpp"
#include "popart/names.hpp"
#include "popart/op.hpp"
#include "popart/op/convbase.hpp"
#include "popart/op/receptive.hpp"
#include "popart/operators.hpp"
#include "popart/sessionoptions.hpp"
#include "popart/tensorinfo.hpp"
#include "popart/util.hpp"

namespace popart {
struct OperatorIdentifier;

ConvOp::ConvOp(const OperatorIdentifier &_opid,
               const Settings &settings_,
               std::vector<int64_t> strides,
               std::vector<int64_t> pads,
               std::vector<int64_t> dilations,
               int64_t group_,
               const AutoPad &padType,
               const MultiConvOptions &convOpts)
    : MultiConvBaseOp(_opid,
                      settings_,
                      strides,
                      pads,
                      dilations,
                      padType,
                      convOpts),
      group(group_) {}

std::vector<std::unique_ptr<Op>> ConvOp::getGradOps() {
  if (isPow2ScaledConv()) {
    throw error(
        "Using a scaled convolution in a backwards pass is currently not "
        "supported.");
  }
  std::vector<std::unique_ptr<Op>> upops;
  upops.emplace_back(std::make_unique<ConvDataGradOp>(*this));
  upops.emplace_back(std::make_unique<ConvWeightsGradOp>(*this));
  return upops;
}

std::unique_ptr<Op> ConvOp::clone() const {
  return std::make_unique<ConvOp>(*this);
}

void ConvOp::setup() {
  validateOpFloat8Inputs(input.get(), getLog2ScaleInIndex(), debugName());

  // The non-optional 'group' argument can always be determined based
  // on input shapes. Check that they match
  if (group < 1) {
    throw error("group attribute in {} must be greater than zero", debugName());
  }

  if (group != getGroups()) {
    throw error(
        "Invalid value for group ({}) in {}. number of input channels ({}) / "
        "group ({}) should be equal to the weight inputs second dimension ({})",
        group,
        debugName(),
        getNInChans(),
        group,
        inInfo(getWeightsInIndex()).dim(1));
  }

  if (isPow2ScaledConv()) {
    verifyPartialsTypesAreHalf();
    outInfo(getOutIndex()) = {DataType::FLOAT16, getOutShape(0, getPads(0))};
  }

  MultiConvBaseOp::setup();
}

void ConvOp::verifyPartialsTypesAreHalf() const {
  auto opts = getConvOptions();
  if (opts.partialsTypes.size()) {
    auto partialsType = opts.partialsTypes[0];
    if (partialsType.compare("half") != 0) {
      throw error("Invalid partials type: {}. Partials type must be half for "
                  "FLOAT8 conv operand types. ",
                  partialsType);
    }
  }
}

bool ConvOp::isPow2ScaledConv() const {
  return opInputsAreValidPow2ScaledInputs(input.get(),
                                          ConvOp::getLog2ScaleInIndex());
}

void ConvOp::restoreAttributesFromParams(
    const std::vector<ConvParameters> &params) {
  setGroup();
  MultiConvBaseOp::restoreAttributesFromParams(params);
}

ConvWeightsGradOp::ConvWeightsGradOp(const ConvOp &op_)
    : MultiConvWeightsGradBaseOp(op_, Onnx::GradOperators::ConvWeightsGrad) {}

std::unique_ptr<Op> ConvWeightsGradOp::clone() const {
  return std::make_unique<ConvWeightsGradOp>(*this);
}

ConvDataGradOp::ConvDataGradOp(const ConvOp &op_)
    : MultiConvDataGradBaseOp(op_, Onnx::GradOperators::ConvDataGrad) {}

std::unique_ptr<Op> ConvDataGradOp::clone() const {
  return std::make_unique<ConvDataGradOp>(*this);
}

ConvFlipWeightsOp::ConvFlipWeightsOp(const OperatorIdentifier &opid_,
                                     const Op::Settings &settings_)
    : Op(opid_, settings_), groupReshape(false), convOpts({}, {}) {}

std::unique_ptr<Op> ConvFlipWeightsOp::clone() const {
  return std::make_unique<ConvFlipWeightsOp>(*this);
}

ConvFlipWeightsOp::~ConvFlipWeightsOp() {}

void ConvFlipWeightsOp::setup() {

  auto &weightsIn = inInfo(getInIndex());

  // Switch the first two dimensions, reshape for groups
  Shape weightsOutShape;
  if (groupReshape) {
    weightsOutShape.push_back(weightsIn.dim(1) * params.numGroups);
    weightsOutShape.push_back(weightsIn.dim(0) / params.numGroups);
  } else {
    weightsOutShape.push_back(weightsIn.dim(1));
    weightsOutShape.push_back(weightsIn.dim(0));
  }
  for (int i = 2; i < weightsIn.shape().size(); ++i) {
    weightsOutShape.push_back(weightsIn.dim(i));
  }

  outInfo(getOutIndex()) = {weightsIn.dataType(), weightsOutShape};
}

std::vector<std::unique_ptr<Op>> ConvFlipWeightsOp::getGradOps() {
  std::vector<std::unique_ptr<Op>> upops;
  upops.emplace_back(std::make_unique<ConvFlipWeightsGradOp>(*this));
  return upops;
}

void ConvFlipWeightsOp::appendOutlineAttributes(OpSerialiserBase &os) const {
  Op::appendOutlineAttributes(os);

  // Append conv options
  for (auto key_val : getConvOptions()) {
    os.appendAttribute(key_val.first, key_val.second);
  }
  os.appendAttribute("groupReshape", groupReshape);
}

ConvFlipWeightsGradOp::ConvFlipWeightsGradOp(
    const ConvFlipWeightsOp &convFlipWeightsOp)
    : ConvFlipWeightsOp(Onnx::GradOperators::ConvFlipWeightsGrad,
                        convFlipWeightsOp.settings) {
  setGroupReshape(convFlipWeightsOp.getGroupReshape());
  setParameters(convFlipWeightsOp.getParameters());
  setConvOptions(convFlipWeightsOp.getMultiConvOptions());
}

std::unique_ptr<Op> ConvFlipWeightsGradOp::clone() const {
  return std::make_unique<ConvFlipWeightsGradOp>(*this);
}

const std::vector<GradInOutMapper> &
ConvFlipWeightsGradOp::gradInputInfo() const {
  static const std::vector<GradInOutMapper> inInfo = {
      // Simply drop the op in place such that the grad associated with the
      // output becomes the input
      {getInIndex(), getOutIndex(), GradOpInType::GradOut}};
  return inInfo;
}

const std::map<int, int> &ConvFlipWeightsGradOp::gradOutToNonGradIn() const {
  // The only output of the grap op (which is the same flip) matches the only
  // input
  static const std::map<int, int> outInfo = {{getOutIndex(), getInIndex()}};
  return outInfo;
}

namespace {

static OpDefinition::DataTypes T = {DataType::FLOAT16, DataType::FLOAT};

static OpDefinition
    convOpDef({OpDefinition::Inputs({
                   {"X",
                    {{DataType::FLOAT,
                      DataType::FLOAT16,
                      DataType::FLOAT8_143,
                      DataType::FLOAT8_152}}},
                   {"W",
                    {{DataType::FLOAT,
                      DataType::FLOAT16,
                      DataType::FLOAT8_143,
                      DataType::FLOAT8_152}}},
                   {"B", {{DataType::FLOAT, DataType::FLOAT16}}},
               }),
               OpDefinition::Outputs({{"Y", T}}),
               OpDefinition::Attributes({
                   {"auto_pad", {"NOTSET"}},
                   // deprecated from conv
                   {"dilations", {"*"}},
                   {"group", {"*"}},
                   {"kernel_shape", {"*"}}, // Do we support this?
                   {"pads", {"*"}},
                   {"strides", {"*"}},
               })});

static OpCreator<ConvOp> convOpCreator(
    OpDefinitions({
        {Onnx::Operators::Conv_1, convOpDef},
        {Onnx::Operators::Conv_11, convOpDef},
    }),
    [](const OpCreatorInfo &info) -> std::unique_ptr<Op> {
      auto strides =
          info.attributes.getAttribute<Attributes::Ints>("strides", {});
      auto pads = info.attributes.getAttribute<Attributes::Ints>("pads", {});
      auto dilations =
          info.attributes.getAttribute<Attributes::Ints>("dilations", {});
      auto group   = info.attributes.getAttribute<Attributes::Int>("group", 1);
      auto padType = info.attributes.getAttribute<Attributes::String>(
          "auto_pad", "NOTSET");

      auto sessOpts =
          info.settings.getIr().getSessionOptions().convolutionOptions;
      auto convOpts = MultiConvOptions(sessOpts, info.attributes);

      return std::unique_ptr<Op>(
          new ConvOp(info.opid,
                     info.settings,
                     strides,
                     pads,
                     dilations,
                     group,
                     HasReceptiveFieldOp::getAutoPad(padType),
                     convOpts));
    },
    true);

static OpDefinition
    convFlipWeightsOpDef({OpDefinition::Inputs({{"input", T}}),
                          OpDefinition::Outputs({{"output", T}}),
                          OpDefinition::Attributes({})});

static OpCreator<ConvFlipWeightsOp> convFlipWeightsOpCreator(OpDefinitions({
    {Onnx::CustomOperators::ConvFlipWeights, convFlipWeightsOpDef},
}));
} // namespace

} // namespace popart
