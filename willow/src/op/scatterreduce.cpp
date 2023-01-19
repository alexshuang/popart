// Copyright (c) 2021 Graphcore Ltd. All rights reserved.
#include <algorithm>
#include <boost/algorithm/string/case_conv.hpp>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <poprithms/ndarray/shape.hpp>
#include <popart/op/scatterreduce.hpp>
#include <popart/opmanager.hpp>
#include <popart/opserialiser.hpp>

#include "popart/attributes.hpp"
#include "popart/datatype.hpp"
#include "popart/error.hpp"
#include "popart/graphcoreoperators.hpp"
#include "popart/logging.hpp"
#include "popart/names.hpp"
#include "popart/op.hpp"
#include "popart/tensorinfo.hpp"
#include "popart/vendored/optional.hpp"

namespace popart {
struct OperatorIdentifier;

std::string ScatterReduceOp::reductionToString(ScatterReduction reduction) {
  if (reduction == ScatterReduction::Sum) {
    return "sum";
  }
  if (reduction == ScatterReduction::Max) {
    return "max";
  }
  if (reduction == ScatterReduction::Min) {
    return "min";
  }
  if (reduction == ScatterReduction::Mul) {
    return "mul";
  }
  if (reduction == ScatterReduction::None) {
    return "none";
  }

  throw internal_error("Unknown ScatterReduction");
}

ScatterReduction
ScatterReduceOp::reductionFromString(const std::string &reduction) {
  auto lower_arg = boost::algorithm::to_lower_copy(reduction);
  if (lower_arg == "sum") {
    return ScatterReduction::Sum;
  }
  if (lower_arg == "max") {
    return ScatterReduction::Max;
  }
  if (lower_arg == "min") {
    return ScatterReduction::Min;
  }
  if (lower_arg == "mul") {
    return ScatterReduction::Mul;
  }
  if (lower_arg == "none") {
    return ScatterReduction::None;
  }

  throw internal_error("Unknown ScatterReduction");
}

ScatterReduceOp::ScatterReduceOp(
    const OperatorIdentifier &_opid,
    int64_t axis_,
    int64_t axis_size_,
    ScatterReduction reduction_,
    int64_t group_size_,
    const nonstd::optional<float> &available_memory_proportion_,
    const Op::Settings &settings_)
    : Op(_opid, settings_), backward_shape(), axis(axis_),
      axis_size(axis_size_), reduction(reduction_), group_size(group_size_),
      available_memory_proportion(available_memory_proportion_),
      index_broadcasted(true) {}

std::unique_ptr<Op> ScatterReduceOp::clone() const {
  return std::make_unique<ScatterReduceOp>(*this);
}

std::vector<std::unique_ptr<Op>> ScatterReduceOp::getGradOps() {
  std::vector<std::unique_ptr<Op>> result;
  result.emplace_back(std::make_unique<ScatterReduceGradOp>(*this));
  return result;
}

void ScatterReduceOp::setup() {
  auto dataInfo    = inInfo(dataInIndex());
  int64_t dataRank = static_cast<int64_t>(dataInfo.shape().size());

  if (-dataRank > axis || axis > dataRank - 1) {
    throw error("ScatterReduceOp::setup axis = {} is outside the acceptable "
                "range [{}, {}]",
                axis,
                -dataRank,
                dataRank - 1);
  }

  // Canonicalise negative axis input
  if (axis < 0) {
    axis += dataRank;
  }

  checkIndexBroadcasted();

  if (hasInput(initialValuesInIndex())) {
    outInfo(outIndex()) = inInfo(initialValuesInIndex());
    backward_shape      = dataInfo.shape();
    return;
  }

  // Output has the same data type as the input and has the same size as the
  // input, except in the axis where the scatter reduction is applied.
  auto outputInfo   = dataInfo;
  auto outputShape  = outputInfo.shape();
  outputShape[axis] = axis_size;
  outputInfo.set(outputInfo.dataType(), outputShape);
  outInfo(outIndex()) = outputInfo;
  backward_shape      = dataInfo.shape();
}

void ScatterReduceOp::appendOutlineAttributes(OpSerialiserBase &os) const {
  Op::appendOutlineAttributes(os);
  os.appendAttribute("axis", axis);
  os.appendAttribute("reduction", reductionToString(reduction));
  os.appendAttribute("group_size", group_size);
  os.appendAttribute("backward_shape", backward_shape);
  os.appendAttribute("available_memory_proportion",
                     available_memory_proportion);
  os.appendAttribute("index_broadcasted", index_broadcasted);
}

void ScatterReduceOp::checkIndexBroadcasted() {
  // Checks if the index is broadcasted.
  // By default we assume that the index input has already been broadcasted to
  // match the shape of the data input.  This can be inefficient when lowering
  // to poplar so this operator also supports a vectorised implementation where
  // the index is provided as a vector. Further complicating matters is the
  // possibility of partial broadcasting which is not currently supported.
  namespace nd           = poprithms::ndarray;
  nd::Shape dataShape    = inShape(dataInIndex());
  nd::Shape indicesShape = inShape(indicesInIndex());

  if (dataShape == indicesShape) {
    // Default constructed with index_broadcasted = true
    return;
  }

  auto indicesRank = indicesShape.rank_u64();
  auto dataRank    = dataShape.rank_u64();

  if (indicesRank > dataRank) {
    throw error("Invalid rank for indices input. "
                "Indices rank {} must be <= data input rank {}.",
                indicesRank,
                dataRank);
  }

  // Allow shape mismatches when the index can be expanded to match the data
  nd::Shape expandedShape(indicesShape);

  // Skip group dim in comparison.
  const bool isGrouped      = group_size > 1;
  const size_t isGroupedSzt = static_cast<size_t>(isGrouped);
  if ((indicesRank - isGroupedSzt) == 1) {
    // Insert leading singleton dimensions ahead of the reduction axis
    for (size_t i = isGroupedSzt; i < axis; i++) {
      expandedShape = expandedShape.unsqueeze(isGroupedSzt);
    }
  }

  // Insert trailing singleton dimensions following reduction axis
  for (size_t i = expandedShape.rank_u64(); i < dataRank; i++) {
    expandedShape = expandedShape.unsqueeze(expandedShape.rank_u64());
  }

  // Check that the dimensions of the input data and the expanded indices either
  // match or are singleton.
  for (size_t i = 0; i < dataRank; i++) {
    if (!(expandedShape.dim(i) == dataShape.dim(i) ||
          expandedShape.dim(i) == 1)) {
      throw error("Failed to expand 'indices' shape {} to match 'src' shape {} "
                  "using reduction axis = {}.",
                  indicesShape,
                  dataShape,
                  axis);
    }
  }

  // We can use a vectorised implementation in the case when the indices are a
  // vector +/- singleton dimensions.
  // Check first not group dim with axis.
  auto nonSingletonDims = expandedShape.nonSingletonDimensions();
  if (nonSingletonDims.size() == (1 + isGroupedSzt) &&
      nonSingletonDims[isGroupedSzt] == axis) {
    index_broadcasted = false;
  } else {
    // This can likely be supported through a pattern that inserts the
    // appropriate expand operator but throw an error for now.
    throw error("Partial broadcasting of indices is not currently supported.");
  }
}

ScatterReduceGradOp::ScatterReduceGradOp(const ScatterReduceOp &op)
    : Op(Onnx::CustomGradOperators::ScatterReduceGradOp, op.getSettings()),
      mapper(), grad_out_info(), backward_shape(op.getBackwardShape()),
      axis(op.getAxis()), reduction(op.getReduction()),
      group_size(op.getGroupSize()),
      available_memory_proportion(op.getAvailableMemoryProportion()),
      index_broadcasted(op.indexBroadcasted()),
      has_initial_values(op.hasInput(op.initialValuesInIndex())) {

  // The GradInOutMapper depends on the reduction used.
  mapper.emplace_back(
      gradInIndex(), ScatterReduceOp::outIndex(), GradOpInType::GradOut);
  mapper.emplace_back(
      indicesInIndex(), ScatterReduceOp::indicesInIndex(), GradOpInType::In);

  // min/max reduction needs to know the data source for masking the gradient
  if (reduction == ScatterReduction::Max ||
      reduction == ScatterReduction::Min ||
      reduction == ScatterReduction::Mul) {
    mapper.emplace_back(
        dataInIndex(), ScatterReduceOp::dataInIndex(), GradOpInType::In);
    mapper.emplace_back(
        fwdOutInIndex(), ScatterReduceOp::outIndex(), GradOpInType::Out);

    if (hasInitialValues()) {
      mapper.emplace_back(initialValuesInIndex(),
                          ScatterReduceOp::initialValuesInIndex(),
                          GradOpInType::In);
    }
  }

  // none reduction needs the data source to apply a scatter of zeros
  if (reduction == ScatterReduction::None) {
    mapper.emplace_back(
        dataInIndex(), ScatterReduceOp::dataInIndex(), GradOpInType::In);
  }

  grad_out_info.emplace(gradDataOutIndex(), ScatterReduceOp::dataInIndex());

  if (hasInitialValues()) {
    grad_out_info.emplace(gradInitialValuesOutIndex(),
                          ScatterReduceOp::initialValuesInIndex());
  }
}

std::unique_ptr<Op> ScatterReduceGradOp::clone() const {
  return std::make_unique<ScatterReduceGradOp>(*this);
}

void ScatterReduceGradOp::setup() {
  const auto gradInInfo = inInfo(gradInIndex());
  outInfo(gradDataOutIndex()) =
      TensorInfo(gradInInfo.dataType(), backward_shape);

  if (hasInitialValues()) {
    outInfo(gradInitialValuesOutIndex()) = gradInInfo;
  }
}

void ScatterReduceGradOp::appendOutlineAttributes(OpSerialiserBase &os) const {
  Op::appendOutlineAttributes(os);
  os.appendAttribute("axis", axis);
  os.appendAttribute("reduction",
                     ScatterReduceOp::reductionToString(reduction));
  os.appendAttribute("group_size", group_size);
  os.appendAttribute("available_memory_proportion",
                     available_memory_proportion);
  os.appendAttribute("index_broadcasted", index_broadcasted);
  os.appendAttribute("has_initial_values", has_initial_values);
}

namespace {
static OpDefinition::DataTypes T = {DataType::UINT8,
                                    DataType::UINT16,
                                    DataType::UINT32,
                                    DataType::UINT64,
                                    DataType::INT8,
                                    DataType::INT16,
                                    DataType::INT32,
                                    DataType::INT64,
                                    DataType::FLOAT16,
                                    DataType::FLOAT,
                                    DataType::BOOL};

static OpDefinition::DataTypes Tind = {DataType::UINT8,
                                       DataType::UINT16,
                                       DataType::UINT32,
                                       DataType::UINT64,
                                       DataType::INT8,
                                       DataType::INT16,
                                       DataType::INT32,
                                       DataType::INT64};

static OpDefinition
    scatterReduceOpDef({OpDefinition::Inputs({
                            {"data", T},
                            {"indices", Tind},
                            {"initial_values", T}, // optional
                        }),
                        OpDefinition::Outputs({{"output", T}}),
                        OpDefinition::Attributes({{"axis", {"*"}},
                                                  {"axis_shape", {"*"}},
                                                  {"reduction", {"*"}},
                                                  {"group_size", {"*"}}})});

static OpCreator<ScatterReduceOp> ScatterReduceOpCreator(
    OpDefinitions({
        {Onnx::CustomOperators::ScatterReduce, scatterReduceOpDef},
    }),
    [](const OpCreatorInfo &info) {
      int64_t axis_size =
          info.attributes.getAttribute<Attributes::Int>("axis_size");

      if (axis_size < 1) {
        throw error("ScatterReduceOp axis_size = {} is not valid: must be > 0",
                    axis_size);
      }

      int64_t axis = info.attributes.getAttribute<Attributes::Int>("axis", -1);

      int64_t group_size =
          info.attributes.getAttribute<Attributes::Int>("group_size", 1);

      if (group_size < 1) {
        throw error("ScatterReduceOp group_size = {} is not valid: must be > 0",
                    group_size);
      }

      auto reduction =
          info.attributes.getAttribute<Attributes::String>("reduction", "sum");

      nonstd::optional<float> available_memory_proportion;

      if (info.attributes.hasAttribute(sAvailMemAttribute)) {
        available_memory_proportion =
            info.attributes.getAttribute<Attributes::Float>(sAvailMemAttribute);
      }

      return std::unique_ptr<Op>(
          new ScatterReduceOp(info.opid,
                              axis,
                              axis_size,
                              ScatterReduceOp::reductionFromString(reduction),
                              group_size,
                              available_memory_proportion,
                              info.settings));
    },
    true);
} // namespace

} // namespace popart
