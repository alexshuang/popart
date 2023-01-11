// Copyright (c) 2020 Graphcore Ltd. All rights reserved.
#include <gcl/CollectiveBalancedReorder.hpp>
#include <gcl/Collectives.hpp>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <poplar/Program.hpp>
#include <poplar/Tensor.hpp>
#include <popops/Zero.hpp>
#include <popart/error.hpp>
#include <popart/ir.hpp>
#include <popart/op/collectives/replicatedallgather.hpp>
#include <popart/op/collectives/replicatedreducescatter.hpp>
#include <popart/popx/devicex.hpp>
#include <popart/popx/irlowering.hpp>
#include <popart/popx/op/collectives/collectivesx.hpp>
#include <popart/popx/op/collectives/replicatedreducescatterx.hpp>
#include <popart/popx/opxmanager.hpp>

#include "popart/graphcoreoperators.hpp"
#include "popart/logging.hpp"
#include "popart/names.hpp"
#include "popart/op.hpp"
#include "popart/op/collectives/collectives.hpp"
#include "popart/popx/debugcontextx.hpp"
#include "popart/popx/opx.hpp"
#include "popart/popx/viewchangers.hpp"
#include "popart/replicatedtensorsharding.hpp"
#include "popart/tensor.hpp"
#include "popart/tensorinfo.hpp"
#include "popart/util.hpp"

namespace poplar {

class OptionFlags;
} // namespace poplar

namespace popart {
namespace popx {

ReplicatedReduceScatterOpx::ReplicatedReduceScatterOpx(Op *op, Devicex *devicex)
    : CollectivesBaseOpx(op, devicex) {
  verifyOp<ReplicatedReduceScatterOp>(
      op, Onnx::CustomOperators::ReplicatedReduceScatter);
}

void ReplicatedReduceScatterOpx::grow(poplar::program::Sequence &prog) const {
  const auto &rrsOp = getOp<ReplicatedReduceScatterOp>();

  const auto inIndex   = ReplicatedReduceScatterOp::getInIndex();
  auto toReduceScatter = getInTensor(inIndex);

  if (getOp<ReplicatedReduceScatterOp>()
          .isConfigureOutputForReplicatedTensorSharding()) {
    auto group = getCollectiveLinkedGroup(
        CollectivesBaseOp::getDefaultTensorShardingGroupIndex());

    ViewChangers viewChangers(
        {std::make_shared<ReplicatedGatherInScatterOutViewChanger>(
            outInfo(ReplicatedReduceScatterOp::getOutIndex()).nelms(),
            group.id)});
    setOutViewChangers(ReplicatedReduceScatterOp::getOutIndex(), viewChangers);

    if (!hasInViewChangers(ReplicatedReduceScatterOp::getInIndex()) ||
        getInViewChangers(ReplicatedReduceScatterOp::getInIndex()) !=
            viewChangers) {
      logging::opx::trace("ReplicatedReduceScatterOpx::grow rearranging {}",
                          inId(ReplicatedReduceScatterOp::getInIndex()));

      // Tensor not rearranged for reduceScatter yet, do it now
      auto cbr = createCollectiveBalancedReorder(
          toReduceScatter,
          CollectivesBaseOp::getDefaultTensorShardingGroupIndex());
      auto c = cbr->createCollectivesTensor(
          toReduceScatter.elementType(),
          inId(ReplicatedReduceScatterOp::getInIndex()));
      // Zero the pad regions
      popops::zero(graph(), c, prog, debugContext());
      auto ref = cbr->undoRearrangeForCollective(c);
      if (hasInViewChangers(ReplicatedReduceScatterOp::getInIndex())) {
        // Copy data to non-pad regions
        prog.add(poplar::program::Copy(
            getInViewChangers(ReplicatedReduceScatterOp::getInIndex())
                .apply(toReduceScatter)

                .flatten(),
            ref.flatten(),
            false,
            debugContext()));
      } else {
        // Copy data to non-pad regions
        prog.add(poplar::program::Copy(
            toReduceScatter.flatten(), ref.flatten(), false, debugContext()));
      }
      toReduceScatter = c;
    }
  }

  const poplar::OptionFlags &reduceScatterOptions = dv_p->lowering().gclOptions;

  poplar::Tensor reducedScattered = gcl::reduceScatterCrossReplica(
      graph(),
      toReduceScatter.flatten(),
      getPoplarCollectiveOperator(rrsOp.getCollectiveOp()),
      prog,
      toGclCommGroup(rrsOp.getReplicaGrouping()),
      debugContext("replicatedReduceScatter"),
      reduceScatterOptions);

  setOutTensor(ReplicatedReduceScatterOp::getOutIndex(), reducedScattered);
}

InputCreatorType
ReplicatedReduceScatterOpx::getInputCreatorType(InIndex index) const {
  const auto &rrsOp = getOp<ReplicatedReduceScatterOp>();

  bool canCreate = false;

  if (hasInput(ReplicatedAllGatherOp::getCollectiveLinkedIndex())) {
    auto group = getCollectiveLinkedGroup(
        CollectivesBaseOp::getDefaultTensorShardingGroupIndex());
    for (auto cbrOpId : group.collectiveOpIds) {
      // Can't exist on itself
      if (cbrOpId.first != rrsOp.id) {
        // This ReplicatedReduceScatterOp is not alone in a group, and can
        // use a pre-existing CBR to create the tensor layout
        // T34831 currently always disable creator, because it can lead to
        // rearrangements & padding in the created tensor that other consumers
        // may not be able to deal with
        canCreate = false;
      }
    }

    if (rrsOp.getCollectiveOp() == CollectiveOperator::Local) {
      // We currently have to disable canCreate for local reductions, because
      // it (with RTS) leads to circular dependencies where a weight's
      // layout can depend on itself, if the weight's other consumers
      // aren't higher-priority creators
      canCreate = false;
    }
  }

  return index == ReplicatedReduceScatterOp::getInIndex() && canCreate
             ? InputCreatorType::CanCreate
             : Opx::getInputCreatorType(index);
}

poplar::Tensor ReplicatedReduceScatterOpx::createInput(
    int inIndex,
    const poplar::DebugNameAndId &dnai) const {
  if (inIndex != ReplicatedReduceScatterOp::getInIndex()) {
    throw error(
        "ReplicatedReduceScatterOpx::createInput, cannot create input at {}",
        inIndex);
  }

  auto cbr = getCollectiveBalancedReorder(
      CollectivesBaseOp::getDefaultTensorShardingGroupIndex());
  if (!cbr) {
    throw error("ReplicatedReduceScatterOpx::createInput, "
                "CollectiveBalancedReorder not found for Op {}",
                op_p->debugName());
  }

  const auto &rrsOp = getOp<ReplicatedReduceScatterOp>();
  const auto &type  = popType(rrsOp.inTensor(inIndex)->info);
  auto input        = cbr->createCollectivesTensor(type, dnai.getPathName());
  return input;
}

DnfTensorIds
ReplicatedReduceScatterOpx::mustExistBeforeCreateDNF(InIndex) const {
  const auto &rrsOp = getOp<ReplicatedReduceScatterOp>();
  auto group        = getCollectiveLinkedGroup(
      CollectivesBaseOp::getDefaultTensorShardingGroupIndex());
  DnfTensorIds mustExist;
  for (auto cbrOpId : group.collectiveOpIds) {
    // Can't depend on itself
    if (cbrOpId.first != rrsOp.id) {
      auto cbrOp = dv_p->ir().getOp(cbrOpId.first);
      mustExist.push_back({cbrOp->inId(CollectivesBaseOp::getInIndex()),
                           cbrOp->outId(CollectivesBaseOp::getOutIndex())});
    }
  }

  logging::opx::trace(
      "ReplicatedReduceScatterOpx::mustExistBeforeCreateDNF, Op "
      "{}, must exist: {}",
      rrsOp.debugName(),
      mustExist);

  return mustExist;
}

bool ReplicatedReduceScatterOpx::hasCreatorViewChangers(InIndex index) const {
  return (index == ReplicatedReduceScatterOp::getInIndex());
}

ViewChangers
ReplicatedReduceScatterOpx::getCreatorViewChangers(InIndex index) const {
  if (index == ReplicatedReduceScatterOp::getInIndex()) {
    auto cbr = getCollectiveBalancedReorder(
        CollectivesBaseOp::getDefaultTensorShardingGroupIndex());
    ViewChangers viewChangers(
        {std::make_shared<ReplicatedGatherOutScatterInViewChanger>(
            cbr,
            getCollectiveLinkedGroup(
                CollectivesBaseOp::getDefaultTensorShardingGroupIndex())
                .id)});
    return viewChangers;
  }
  throw error(
      "ReplicatedReduceScatterOpx::getCreatorViewChangers: Invalid index = " +
      std::to_string(index));
}

namespace {

OpxCreator<ReplicatedReduceScatterOpx> ReplicatedReduceScatterOpxCreator(
    Onnx::CustomOperators::ReplicatedReduceScatter);

} // namespace

} // namespace popx
} // namespace popart
