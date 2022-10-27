// Copyright (c) 2020 Graphcore Ltd. All rights reserved.
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <popart/ir.hpp>
#include <popart/op/collectives/replicatedallgather.hpp>
#include <popart/op/collectives/replicatedreducescatter.hpp>
#include <popart/opmanager.hpp>
#include <popart/opserialiser.hpp>

#include "popart/analysis/replicaequal/replicaequalanalysisproxy.hpp"
#include "popart/attributes.hpp"
#include "popart/commgroup.hpp"
#include "popart/datatype.hpp"
#include "popart/graphcoreoperators.hpp"
#include "popart/logging.hpp"
#include "popart/names.hpp"
#include "popart/op.hpp"
#include "popart/op/collectives/collectives.hpp"
#include "popart/replicagrouping.hpp"
#include "popart/sessionoptions.hpp"
#include "popart/tensorinfo.hpp"

namespace popart {
class AliasModel;
struct OperatorIdentifier;

ReplicatedReduceScatterOp::ReplicatedReduceScatterOp(
    const OperatorIdentifier &_opid,
    CollectiveOperator op_,
    CommGroup group_,
    bool configureOutputForReplicatedTensorSharding_,
    const Op::Settings &settings_)
    : CollectivesBaseOp(_opid, group_, settings_), op(op_),
      configureOutputForReplicatedTensorSharding(
          configureOutputForReplicatedTensorSharding_) {}

ReplicatedReduceScatterOp::ReplicatedReduceScatterOp(
    const OperatorIdentifier &_opid,
    CollectiveOperator op_,
    const ReplicaGrouping &grouping,
    bool configureOutputForReplicatedTensorSharding_,
    const Op::Settings &settings_)
    : CollectivesBaseOp(_opid, grouping, settings_), op(op_),
      configureOutputForReplicatedTensorSharding(
          configureOutputForReplicatedTensorSharding_) {}

ReplicatedReduceScatterOp::ReplicatedReduceScatterOp(
    const OperatorIdentifier &_opid,
    CollectiveOperator op_,
    CommGroup group_,
    const Op::Settings &settings_)
    : CollectivesBaseOp(_opid, group_, settings_), op(op_),
      configureOutputForReplicatedTensorSharding(false) {}

ReplicatedReduceScatterOp::ReplicatedReduceScatterOp(
    const OperatorIdentifier &_opid,
    CollectiveOperator op_,
    const ReplicaGrouping &grouping,
    const Op::Settings &settings_)
    : CollectivesBaseOp(_opid, grouping, settings_), op(op_),
      configureOutputForReplicatedTensorSharding(false) {}

ReplicatedReduceScatterOp::ReplicatedReduceScatterOp(
    const OperatorIdentifier &_opid,
    const Op::Settings &settings_)
    : CollectivesBaseOp(_opid,
                        ReplicaGrouping(settings_.getIr()
                                            .getSessionOptions()
                                            .getGlobalReplicationFactor()),
                        settings_),
      op(CollectiveOperator::Add),
      configureOutputForReplicatedTensorSharding(false) {}

std::unique_ptr<Op> ReplicatedReduceScatterOp::clone() const {
  return std::make_unique<ReplicatedReduceScatterOp>(*this);
}

void ReplicatedReduceScatterOp::setup() {

  const auto &inInfo_ = inInfo(getInIndex());
  int64_t nelms       = inInfo_.nelms();
  auto commSize       = getCommSize();

  // ceil(numElements / commSize)
  auto outElms = (nelms + commSize - 1) / commSize;

  Shape metaShape;
  if (isConfigureOutputForReplicatedTensorSharding()) {
    metaShape = inInfo_.shape();
  }

  outInfo(getOutIndex()) = TensorInfo(inInfo_.dataType(), {outElms}, metaShape);

  logging::op::trace("[ReplicatedReduceScatterOp] Global replication factor: "
                     "{}, sharding factor: {}",
                     getIr().getSessionOptions().getGlobalReplicationFactor(),
                     commSize);
}

void ReplicatedReduceScatterOp::appendOutlineAttributes(
    OpSerialiserBase &os) const {
  CollectivesBaseOp::appendOutlineAttributes(os);
  os.appendAttribute(sCollectiveOperator, static_cast<int>(op));
}

ReplicatedTensorShardingIndices
ReplicatedReduceScatterOp::getReplicatedTensorShardingIndices() const {
  return {{{}, {ReplicatedReduceScatterOp::getOutIndex()}}};
}

bool ReplicatedReduceScatterOp::isConfigureOutputForReplicatedTensorSharding()
    const {
  return configureOutputForReplicatedTensorSharding ||
         hasInput(ReplicatedReduceScatterOp::getCollectiveLinkedIndex()) ||
         !outInfo(ReplicatedReduceScatterOp::getOutIndex()).metaShape().empty();
}

std::tuple<ReplEqOutputMap, ReplEqModifiedInputMap>
ReplicatedReduceScatterOp::fwdPropagateIsReplicaEqual(
    const AliasModel &aliasModel,
    const ReplEqInputMap &inputMap,
    ReplicaEqualAnalysisProxy &proxy) const {

  // TODO(T51589): Amend logic to be more fine-grained, taking into account
  // CommGroup settings. We should work out replica-equalness over subsets
  // of replicas instead instead of having only tracking if a tensor is
  // replica-equal for all replicas or not.

  const auto groupSize                 = getReplicaGrouping().getGroupSize();
  const auto isLocal                   = (op == CollectiveOperator::Local);
  const auto isReductionOverOneReplica = groupSize == 1;

  // If a local reduction or a scatter over multiple replicas, the output is
  // definitely non-equal.
  if (isLocal || !isReductionOverOneReplica) {
    ReplEqOutputMap result;
    result[getOutIndex()] = false;
    return {result, proxy.getModifiedInputMapFromAliases(this, result)};
  } else {
    return Op::fwdPropagateIsReplicaEqual(aliasModel, inputMap, proxy);
  }
}

std::vector<std::unique_ptr<Op>> ReplicatedReduceScatterOp::getGradOps() {
  if (getCollectiveOp() != CollectiveOperator::Local) {
    throw error("ReplicatedReduceScatterOp: grad op is only implemented when "
                "CollectiveOperator==CollectiveOperator::Local");
  }
  std::vector<std::unique_ptr<Op>> result;
  result.push_back(std::make_unique<ReplicatedAllGatherOp>(
      Onnx::CustomOperators::ReplicatedAllGather,
      getReplicaGrouping(),
      settings));
  return result;
}

const std::vector<GradInOutMapper> &
ReplicatedReduceScatterOp::gradInputInfo() const {
  static const std::vector<GradInOutMapper> inInfo = {
      {getInIndex(), getOutIndex(), GradOpInType::GradOut}};
  return inInfo;
}

const std::map<int, int> &
ReplicatedReduceScatterOp::gradOutToNonGradIn() const {
  static const std::map<int, int> outInfo = {{getOutIndex(), getInIndex()}};
  return outInfo;
}

static OpDefinition::DataTypes T = {DataType::FLOAT,
                                    DataType::FLOAT16,
                                    DataType::INT32,
                                    DataType::UINT32};

static OpDefinition ReplicatedReduceScatterOpDef(
    {OpDefinition::Inputs({{"X", T}}),
     OpDefinition::Outputs({{"Y", T}}),
     OpDefinition::Attributes({{sCollectiveOperator, {"*"}},
                               {sCollectiveReplicaGrouping, {"*"}}})});

static OpCreator<ReplicatedReduceScatterOp> ReplicatedReduceScatterOpCreator(
    OpDefinitions({{Onnx::CustomOperators::ReplicatedReduceScatter,
                    ReplicatedReduceScatterOpDef}}),
    [](const OpCreatorInfo &info) {
      const auto grouping =
          extractReplicaGroupingFromAttrs(info.attributes,
                                          info.settings.getIr()
                                              .getSessionOptions()
                                              .getGlobalReplicationFactor());
      CollectiveOperator op = static_cast<CollectiveOperator>(
          info.attributes.getAttribute<Attributes::Int>(
              sCollectiveOperator, static_cast<int>(CollectiveOperator::Add)));
      bool replicatedTensorSharding =
          static_cast<bool>(info.attributes.getAttribute<Attributes::Int>(
              sReplicatedTensorSharding, 0));
      return std::unique_ptr<ReplicatedReduceScatterOp>(
          new ReplicatedReduceScatterOp(info.opid,
                                        op,
                                        grouping,
                                        replicatedTensorSharding,
                                        info.settings));
    },
    true);

} // namespace popart
