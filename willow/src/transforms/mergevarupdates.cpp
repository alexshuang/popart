// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#include <memory>
#include <tuple>
#include <popart/graph.hpp>
#include <popart/ir.hpp>
#include <popart/op/concat.hpp>
#include <popart/op/copyvarupdate.hpp>
#include <popart/op/reshape.hpp>
#include <popart/op/sgd0combo.hpp>
#include <popart/op/sgd1combo.hpp>
#include <popart/op/sgd2combo.hpp>
#include <popart/op/slice.hpp>
#include <popart/op/varupdate.hpp>
#include <popart/opidentifier.hpp>
#include <popart/opmanager.hpp>
#include <popart/sessionoptions.hpp>
#include <popart/tensorindex.hpp>
#include <popart/tensors.hpp>
#include <popart/transforms/mergevarupdates.hpp>

namespace popart {

namespace {
std::string getConcatWeightsPrefix() { return "concatWeights___"; }

std::string getConcatGradsPrefix() { return "concatGrads___"; }

std::string getReshapedPrefix() { return "flattened___"; }

std::string getSlicedPrefix() { return "sliced___"; }
} // namespace

std::size_t MergeAllVarUpdates::id() {
  return typeid(MergeAllVarUpdates).hash_code();
}

std::size_t MergeTightThreshold::id() {
  return typeid(MergeTightThreshold).hash_code();
}

std::size_t MergeLooseThreshold::id() {
  return typeid(MergeLooseThreshold).hash_code();
}

namespace {

void appendPartitionIdForSGDMComboBaseOp(const SGDMComboBaseOp *svu,
                                         std::stringstream &ss) {
  // momentum
  if (svu->initSmm1.isConst()) {
    ss << "_constLr_" << svu->initSmm1.val();
  } else {
    ss << "_nonConstLr_" << svu->inId(svu->getSmm1InIndex());
  }
  // dampening scale factor
  if (svu->initDpsf1.isConst()) {
    ss << "_constWd_" << svu->initDpsf1.val();
  } else {
    ss << "_nonConstWd_" << svu->inId(svu->getDpsf1InIndex());
  }
  // weight decay scale factor
  if (svu->initSwd1.isConst()) {
    ss << "_constWd_" << svu->initSwd1.val();
  } else {
    ss << "_nonConstWd_" << svu->inId(svu->getSwd1InIndex());
  }
  // scaled learning rate
  if (svu->initSlr1.isConst()) {
    ss << "_constWd_" << svu->initSlr1.val();
  } else {
    ss << "_nonConstWd_" << svu->inId(svu->getSlr1InIndex());
  }
}

} // namespace

MergeVarUpdates::PartitionId MergeVarUpdates::getPartitionId(Op *op) const {
  std::stringstream ss;

  // same virtual graph
  ss << "vg_" << op->settings.vgraphId;

  // 1) SGD settings
  if (op->isConvertibleTo<SGD0ComboOp>()) {
    auto svu = dynamic_cast<SGD0ComboOp *>(op);
    ss << "_SGD0C_";

    if (svu->initSlr0.isConst()) {
      ss << "_constLr_" << svu->initSlr0.val();
    } else {
      ss << "_nonConstLr_" << svu->inId(svu->getSlr0InIndex());
    }

    if (svu->initWdsf0.isConst()) {
      ss << "_constWd_" << svu->initWdsf0.val();
    } else {
      ss << "_nonConstWd_" << svu->inId(svu->getWdsf0InIndex());
    }
  }

  else if (op->isConvertibleTo<SGD1ComboOp>()) {
    auto svu = dynamic_cast<SGD1ComboOp *>(op);
    ss << "_SGD1C_";
    appendPartitionIdForSGDMComboBaseOp(svu, ss);
  }

  else if (op->isConvertibleTo<SGD2ComboOp>()) {
    auto svu = dynamic_cast<SGD2ComboOp *>(op);

    ss << "_SGD2C_";

    appendPartitionIdForSGDMComboBaseOp(svu, ss);

    ss << "_withGradAccum_" << std::boolalpha << svu->withGradAccum
       << std::noboolalpha;

    ss << "_accumType_" << svu->accumType;
    ss << "_accl1Type_" << svu->accl1Type;
  }

  // 2) CopyVarUpdate settings
  else if (op->isConvertibleTo<CopyVarUpdateOp>()) {
    // there are no attributes to sub-partition CopyVarUpdatOps by
    ss << "_copyVar_";
  }

  // 4) unknown. New CopyVarUpdateOps will need their cases here
  else {
    throw error("{} is not a VarUpdateOp supported in Merge Pattern",
                op->str());
  }
  return ss.str();
}

// Return a map, keys being all unique PartitionIds of VarUpdateOps in "graph",
// and values, the vectors of the VarUpdateOps (with information about Var size)
// with the corresponding key
MergeVarUpdates::PartitionMap
MergeVarUpdates::getLargestGroupTargetsMap(const Graph &graph) const {
  PartitionMap targetsMap;
  for (auto &id_upop : graph.getOps()) {
    auto op   = id_upop.second.get();
    auto vuop = dynamic_cast<VarUpdateOp *>(op);
    if (vuop) {
      auto partitionId = getPartitionId(vuop);
      int64_t start    = 0;
      auto end = op->inInfo(VarUpdateOp::getVarToUpdateInIndex()).nelms();
      VarUpdateStartEnd vse(vuop, start, end);
      auto found = targetsMap.find(partitionId);
      if (found == targetsMap.end()) {
        targetsMap.insert({partitionId, {vse}});
      } else {
        found->second.push_back(vse);
      }
    }
    // nothing to do for non-VarUpdateOps
    else {
    }
  }
  return targetsMap;
}

int64_t MergeAuto::getThresholdMemory(const Graph &g) const {

  int64_t thresholdMemory =
      g.getIr().getSessionOptions().mergeVarUpdateMemThreshold;

  if (thresholdMemory < 0) {
    throw error("Negative memory {} threshold detected in MergeAuto. The "
                "option mergeVarUpdateMemThreshold must be positive. ",
                thresholdMemory);
  }

  return thresholdMemory;
}

int64_t MergeLooseThreshold::getMemToPlayWithAtPeak(const Graph &g) const {

  int64_t thresholdMemory = g.getIr().getSessionOptions().looseThresholdAtPeak;

  if (thresholdMemory < 0) {
    throw error("Negative memory {} threshold detected in MergeLoose. The "
                "option looseThresholdAtPeak must be non-negative. ",
                thresholdMemory);
  }

  return thresholdMemory;
}

MergeVarUpdates::PartitionMap
MergeTightThreshold::getFinal(const Graph &g) const {

  int64_t thresholdMemory = getThresholdMemory(g);
  auto parentPartitions   = getLargestGroupTargetsMap(g);

  // We will decompose the parentPartitions into smaller, child partitions. This
  // is what will be returned
  PartitionMap childPartitions;

  auto opSched = g.getOpSchedule({}, RequireOptimalSchedule::Yes);
  // a map from Ops to their position in the schedule
  std::map<Op *, int> schedIndex;
  for (int i = 0; i < opSched.size(); ++i) {
    schedIndex[opSched[i]] = i;
  }

  // for each of the parent (largest) partitions, keep track of pending memory
  std::map<PartitionId, int64_t> pendingMemories;
  // the var updates responsible for the above pending memory
  PartitionMap pendingVarUpdates;
  // the parent partition to which VarUpdateOps belong
  std::map<VarUpdateOp *, PartitionId> parentPartitionId;
  // All VarUpdateStartEnds, sorted by index in the schedule
  std::vector<std::tuple<int, VarUpdateStartEnd>> bySchedIndex;

  // initialise the above variables
  for (auto x : parentPartitions) {
    auto id         = x.first;
    auto varUpdates = x.second;
    pendingMemories.insert({id, 0});
    pendingVarUpdates.insert({id, {}});
    for (auto op_start_end : varUpdates) {
      auto vop = op_start_end.vop;
      parentPartitionId.insert({vop, id});
      auto tensorToUpdate = vop->input->tensor(vop->getVarToUpdateInIndex());
      int64_t start       = 0;
      auto end            = tensorToUpdate->info.nelms();
      std::tuple<int, VarUpdateStartEnd> tup(schedIndex.at(vop),
                                             {vop, start, end});
      bySchedIndex.push_back(tup);
    }
  }
  std::sort(bySchedIndex.begin(), bySchedIndex.end());

  auto insertCompleteChild =
      [&childPartitions](const std::string &parPartId,
                         const std::vector<VarUpdateStartEnd> &vuops) {
        auto childNumString      = std::to_string(childPartitions.size());
        auto newSubPartitionName = parPartId + "__spn__" + childNumString;
        childPartitions.insert({newSubPartitionName, vuops});
      };

  int varUpdateNumber = 0;
  // iterate over all VarUpdates, slicing as needed to meet the threshold
  // exactly
  while (varUpdateNumber < bySchedIndex.size()) {

    // taking by reference, as it may be modified
    auto &opStartEnd = std::get<1>(bySchedIndex[varUpdateNumber]);
    VarUpdateOp *vop = opStartEnd.vop;
    auto start       = opStartEnd.start;
    auto end         = opStartEnd.end;

    auto parPartId   = parentPartitionId.at(vop);
    Tensor *toUpdate = vop->input->tensor(vop->getVarToUpdateInIndex());

    auto bytesPerElm = toUpdate->info.getDataTypeInfo()->nbytes();
    auto varMemSize  = (end - start) * bytesPerElm;

    // there will be a new sub-partition created and MAYBE we will be finished
    // with the Variable being updated by Op at index VarUpdateNumber
    if (pendingMemories[parPartId] + varMemSize >= thresholdMemory) {

      // the number of bytes to take us up to the threshold
      auto bytesToTake = thresholdMemory - pendingMemories[parPartId];
      auto elmsToTake  = bytesToTake / bytesPerElm;

      // child, complete.
      auto toPop                   = pendingVarUpdates[parPartId];
      pendingVarUpdates[parPartId] = {};
      pendingMemories[parPartId]   = 0;
      if (elmsToTake != 0) {
        toPop.push_back({vop, start, start + elmsToTake});
      }
      insertCompleteChild(parPartId, toPop);

      // if the Var has still got outstanding memory, increment its start
      if (start + elmsToTake != end) {
        opStartEnd.start += elmsToTake;
      }
      // otherwise, move onto the next variable
      else {
        ++varUpdateNumber;
      }
    }

    // still below threshold, even with the whole variable.
    else {
      pendingVarUpdates[parPartId].push_back({vop, start, end});
      pendingMemories[parPartId] += varMemSize;
      ++varUpdateNumber;
    }
  }

  // flush the remaining
  for (auto parid_varsToUpdate : pendingVarUpdates) {
    insertCompleteChild(parid_varsToUpdate.first, parid_varsToUpdate.second);
  }

  return childPartitions;
}

MergeVarUpdates::PartitionMap
MergeLooseThreshold::getFinal(const Graph &g) const {

  int64_t thresholdMemory = getThresholdMemory(g);
  auto parentPartitions   = getLargestGroupTargetsMap(g);

  // check that there is a chance of a partition with more than 1 Op.
  // If not, sub-partitioning is not possible, so return early
  bool isNonTrivialPartition = false;
  for (auto x : parentPartitions) {
    if (x.second.size() > 1) {
      isNonTrivialPartition = true;
      break;
    }
  }
  if (!isNonTrivialPartition) {
    return parentPartitions;
  }

  auto opSched = g.getOpSchedule({}, RequireOptimalSchedule::Yes);
  std::map<Op *, int> schedIndex;
  for (int i = 0; i < opSched.size(); ++i) {
    schedIndex[opSched[i]] = i;
  }

  // find the point at which the forward part of the compute graph ends
  int switchIndex = -1;
  for (int i = 0; i < opSched.size(); ++i) {
    if (opSched[i]->toLoss == PathToLoss::Yes) {
      switchIndex = i;
    }
  }
  if (switchIndex < 0) {
    throw internal_error(
        "failed to set switchIndex, is the graph in training mode?");
  }

  // for every tensor which is
  // 1) created on the forward path and
  // 2) consumed on the backward path,
  // insert "+mem" at creation point and "-mem" at final consumption.
  // This vector will look something like,
  // ..+...+.+..+...+..S...-.-...-...-.-,
  //
  // where S above is switchIndex.
  //
  std::vector<int64_t> deltaMemFwdLiveForBwd(opSched.size(), 0);
  for (int i = 0; i < switchIndex; ++i) {
    for (Tensor *t : opSched[i]->output->tensors()) {
      // final consumption time
      int fct = -1;
      for (Op *consumer : t->consumers.getOps()) {
        fct = std::max<int>(fct, schedIndex.at(consumer));
      }
      if (fct > switchIndex) {
        deltaMemFwdLiveForBwd[i] += t->info.nbytes();
        deltaMemFwdLiveForBwd[fct] -= t->info.nbytes();
      }
    }
  }

  // cumulative sum of deltaMemFwdLiveForBwd
  std::vector<int64_t> cumMemFwdLiveForBwd(opSched.size(), 0);
  int64_t maxCumMemFwdLiveForBwd = 0;
  cumMemFwdLiveForBwd[0]         = deltaMemFwdLiveForBwd[0];
  for (int i = 1; i < opSched.size(); ++i) {
    cumMemFwdLiveForBwd[i] =
        deltaMemFwdLiveForBwd[i] + cumMemFwdLiveForBwd[i - 1];
    maxCumMemFwdLiveForBwd =
        std::max<int64_t>(maxCumMemFwdLiveForBwd, cumMemFwdLiveForBwd[i]);
  }

  if (cumMemFwdLiveForBwd[opSched.size() - 1] != 0) {
    throw internal_error("expected final cumulative memory to be zero");
  }

  // An estimate of how much memory there is,  to use for delaying weight
  // updates without effecting max-liveness, looks something like
  //
  // clang-format off
  //
  // *                         *
  // *                         *
  // **                       **
  // ****                 ******
  // *******        ************
  // **********   **************
  // ***************************  (this final line: memToPlayWith at peak liveness)
  //
  // clang-format on
  //
  // -----------------------------> schedule index
  // where above: vertical is memory to play with
  // and horizontal is schedule position

  // At peak, can delay scheduling while below this number of bytes:
  int64_t memToPlayWithAtPeak = getMemToPlayWithAtPeak(g);

  std::vector<int64_t> memToPlayWith(opSched.size(), 0);
  for (int i = 0; i < opSched.size(); ++i) {
    memToPlayWith[i] =
        maxCumMemFwdLiveForBwd - cumMemFwdLiveForBwd[i] + memToPlayWithAtPeak;
  }

  std::map<VarUpdateOp *, PartitionId> parentPartitionId;
  std::vector<std::tuple<int, VarUpdateOp *>> bySchedIndex;

  // variables to monitor memory as we perform sub-partitioning on
  // parentPartitions.
  // 1) the VarUpdates which we've delayed scheduling of
  PartitionMap pendingVarUpdates;
  // 2) the total memory of the delayed updates for each parent partition id
  //    Example : CopyVarUpdate-xx:20, SGDVarUpdate-xx:50
  std::map<PartitionId, int64_t> pendingMemories;
  // 3) the gross total memory of the delayer partitions
  //    Eaxmple : 70
  int64_t totalPendingMemory = 0;

  // initialise the above variables
  for (auto x : parentPartitions) {
    auto id         = x.first;
    auto varUpdates = x.second;
    pendingMemories.insert({id, 0});
    pendingVarUpdates.insert({id, {}});
    for (auto op_start_end : varUpdates) {
      auto vop = op_start_end.vop;
      parentPartitionId.insert({vop, id});

      std::tuple<int, VarUpdateOp *> tup(schedIndex.at(vop), vop);
      bySchedIndex.push_back(tup);
    }
  }

  // sort from earliest schedule position to last schedule position
  std::sort(bySchedIndex.begin(), bySchedIndex.end());

  // for every VarUpdateOp, what is the minimum memory to play with
  // from its schedule position to the next VarUpdateOps schedule position?
  std::vector<int64_t> minToPlayWithTilNextVarUpdate;
  for (int i = 0; i < bySchedIndex.size() - 1; ++i) {
    minToPlayWithTilNextVarUpdate.push_back(
        std::numeric_limits<int64_t>::max());
    for (int j = std::get<0>(bySchedIndex[i]);
         j < std::get<0>(bySchedIndex[i + 1]);
         ++j) {
      minToPlayWithTilNextVarUpdate[i] =
          std::min<int64_t>(minToPlayWithTilNextVarUpdate[i], memToPlayWith[j]);
    }
  }
  // we say that the final VarUpdateOp has no memory to play with, which
  // guratantees that the VarUpdateOps are all flushed at this point
  minToPlayWithTilNextVarUpdate.push_back(0);

  // Now prepare the sub-partitioning,
  PartitionMap childPartitions;

  auto insertCompleteChild =
      [&childPartitions](const std::string &parPartId,
                         const std::vector<VarUpdateStartEnd> &vuops) {
        auto newSubPartitionName =
            parPartId + "__spn__" + std::to_string(childPartitions.size());
        childPartitions.insert({newSubPartitionName, vuops});
      };

  // iterating through all the VarUpdateOps in order they appear in the schedule
  for (int varUpdateNumber = 0; varUpdateNumber < bySchedIndex.size();
       ++varUpdateNumber) {

    auto vu_tuple      = bySchedIndex[varUpdateNumber];
    VarUpdateOp *vop   = std::get<1>(vu_tuple);
    auto parPartId     = parentPartitionId.at(vop);
    Tensor *toUpdate   = vop->input->tensor(vop->getVarToUpdateInIndex());
    int64_t varMemSize = toUpdate->info.nbytes();

    // add the new VarUpdateOp to the list of pending VarUpdateOps and update
    // the memory monitoring variables
    totalPendingMemory += varMemSize;
    pendingMemories[parPartId] += varMemSize;

    auto end = toUpdate->info.nelms();
    pendingVarUpdates[parPartId].push_back({vop, 0, end});

    // check for a merger: is the pending memory too large (as compared to
    // threshold and as compared to memory to play with) ?
    while (totalPendingMemory >
               minToPlayWithTilNextVarUpdate[varUpdateNumber] ||
           totalPendingMemory > thresholdMemory) {
      // need to merge some VarUpdateOps as memory limit exceeded.
      // Which type of VarUpdateOps to merge?
      // We choose the one with the largest pending memory, found below,
      PartitionId largestLivePartitionId;
      int64_t largestLivePartitionSize = -1;
      for (auto id_size : pendingMemories) {
        if (id_size.second >= largestLivePartitionSize) {
          largestLivePartitionId   = id_size.first;
          largestLivePartitionSize = id_size.second;
        }
      }

      auto newSubPartition = pendingVarUpdates[largestLivePartitionId];
      pendingVarUpdates[largestLivePartitionId].clear();
      totalPendingMemory -= largestLivePartitionSize;
      pendingMemories[largestLivePartitionId] = 0;

      insertCompleteChild(largestLivePartitionId, newSubPartition);
    }
  }

  // add any remaining var-updates
  for (auto x : pendingVarUpdates) {
    insertCompleteChild(x.first, x.second);
  }

  return childPartitions;
}

bool MergeVarUpdates::apply(Graph &graph) const {

  // does this call to "apply" change the Graph input?
  // Will become true if any partition is not a singleton.
  bool changed = false;

  // then concat to shape (1, n1 + n2 + ... + nT)
  const int concatAxis = 1;

  auto targetsMap = getFinal(graph);
  logging::transform::debug("The number of VarUpdate groups to merge is {}",
                            targetsMap.size());

  // the replaced VarUpdateOps which are replaced will be removed at the end
  std::set<VarUpdateOp *> toRemove;

  for (auto &targetMap : targetsMap) {
    logging::transform::debug(
        "Processing the VarUpdate group {}, which has {} VarUpdates in it.",
        targetMap.first,
        targetMap.second.size());

    auto target = targetMap.second;

    if (target.size() > 1 ||
        (target.size() == 1 &&
         (target[0].end - target[0].start !=
          target[0]
              .vop->inInfo(VarUpdateOp::getVarToUpdateInIndex())
              .nelms()))) {
      auto partitionId = targetMap.first;

      // The variable update type
      changed = true;

      //  replace individual weight updates;
      //  ---------------------------------
      //
      //   W0  dW0     W1  dW1
      //   |    |       |   |
      //  VarUpdate   VarUpdate
      //     |            |
      //   W0new        W1new
      //
      //
      //   with a merged weight update:
      //   ----------------------------
      //
      //   W0           W1      dW0         dW1
      //   |            |        |           |
      // ReshapeInplace |  ReshapeInplace    |
      //   |     ReshapeInplace |     ReshapeInplace
      //   |            |       |            |
      //   \           /        \           /
      //   ConcatInplace        ConcatInplace
      //             \             /
      //               \          /
      //                 \       /
      //                  VarUpdate
      //                     |
      //                ConcatedWsNew
      //
      //  Similarly for non-const SGDVarUpdates and CopyUpdate.
      //
      //  The ReshapeInplace ops, are actually just flattening to {1. nelms} as
      //  we prefer a reshape op as there is no Flatten* Opx implementation in
      //  PopART.
      //
      //  It might be that a weight is reshaped (flattened) and then sliced, so
      //  that only a part of it is updated (the rest being updated elsewhere)

      // The Ops which reshape (flatten, and possibly slice) the Variable
      // Tensors
      std::vector<Op *> reshapeWeightOps;
      // The Ops which reshape (flatten, and possibly slice) the Updater Tensors
      // (grads, sources of copies)
      std::vector<Op *> reshapeUpdaterOps;

      // Build up the name of the ConcatInplaceOp for the weight concat.
      std::stringstream concatWeightsNameStream;
      concatWeightsNameStream << getConcatWeightsPrefix();

      std::stringstream concatUpdatersNameStream;
      concatUpdatersNameStream << getConcatGradsPrefix();

      const Op::Settings &canonSettings = target[0].vop->settings;

      // optimizer specific input tensor names
      auto optimizerInputs =
          dynamic_cast<VarUpdateOp *>(target[0].vop)->optimizerInputs();

      auto upCanonTargetOp = target[0].vop->clone();
      auto canonTargetOp   = dynamic_cast<VarUpdateOp *>(upCanonTargetOp.get());

      for (auto opStartEnd : target) {

        auto makeReshaped = [canonSettings,
                             &graph](const Tensor *tensor,
                                     VarUpdateStartEnd::Start start,
                                     VarUpdateStartEnd::End end) {
          const Shape &outShape = {1, tensor->info.nelms()};
          // create ReshapeInplaceOp
          auto tempReshapeOp = std::make_unique<ReshapeInplaceOp>(
              Onnx::CustomOperators::ReshapeInplace, outShape, canonSettings);

          // connect ReshapeInplaceOp with input, create output
          //
          auto reshapeOp = tempReshapeOp.get();

          graph.moveIntoGraph(std::move(tempReshapeOp));

          reshapeOp->connectInTensor(ReshapeBaseOp::getInIndex(), tensor->id);

          auto reshapeOutId = getReshapedPrefix() + tensor->id.str() + "_s" +
                              std::to_string(start) + "_e" +
                              std::to_string(end);

          reshapeOp->createAndConnectOutTensor(ReshapeBaseOp::getOutIndex(),
                                               reshapeOutId);

          reshapeOp->setup();

          // create slice if necessary
          if (end - start != tensor->info.nelms()) {

            auto tempSliceOp = std::make_unique<SliceInplaceOp>(
                Onnx::CustomOperators::SliceInplace,
                std::vector<int64_t>{start}, // starts
                std::vector<int64_t>{end},   // ends
                std::vector<int64_t>{1},     // axes
                std::vector<int64_t>{},      // flips
                canonSettings);

            auto sliceOp = tempSliceOp.get();

            graph.moveIntoGraph(std::move(tempSliceOp));

            sliceOp->connectInTensor(BaseSliceOp::getInIndex(), reshapeOutId);

            std::ostringstream sliceOutIdSs;
            sliceOutIdSs << getSlicedPrefix() << "_s" << start << "-e" << end
                         << "_id" << reshapeOutId;

            sliceOp->createAndConnectOutTensor(BaseSliceOp::getOutIndex(),
                                               sliceOutIdSs.str());

            sliceOp->setup();

            return dynamic_cast<Op *>(sliceOp);
          }
          return dynamic_cast<Op *>(reshapeOp);
        };

        // create ReshapeInplaceOp (and possible SliceInplaceOp)
        // for the weight being updated
        auto weightIn =
            opStartEnd.vop->inTensor(VarUpdateOp::getVarToUpdateInIndex());

        auto flWeOp = makeReshaped(weightIn, opStartEnd.start, opStartEnd.end);

        reshapeWeightOps.push_back(flWeOp);
        concatWeightsNameStream << '_' << weightIn->id << "_"
                                << opStartEnd.start << "-" << opStartEnd.end;

        // create ReshapeInplaceOp (and possibly SliceInplaceOp) for the
        // gradient, or source of copy
        auto gIn = opStartEnd.vop->inTensor(
            VarUpdateWithUpdaterOp::getUpdaterInIndex());
        auto flGrOp = makeReshaped(gIn, opStartEnd.start, opStartEnd.end);
        reshapeUpdaterOps.push_back(flGrOp);
        concatUpdatersNameStream << '_' << gIn->id << "_" << opStartEnd.start
                                 << "-" << opStartEnd.end;

        auto weightOutId =
            opStartEnd.vop->outTensor(VarUpdateOp::getUpdatedVarOutIndex())->id;

        toRemove.emplace(opStartEnd.vop);
      }

      auto getConcatInplace = [&graph,
                               canonSettings](const std::vector<Op *> &reshaped,
                                              TensorId newId) {
        // create ConcatInplaceOp for the reshaped input tensors
        auto tempOp =
            std::unique_ptr<Op>(new ConcatInplaceOp(concatAxis, canonSettings));
        auto concatOp = tempOp.get();
        graph.moveIntoGraph(std::move(tempOp));
        for (int i = 0; i < reshaped.size(); ++i) {
          concatOp->connectInTensor(
              i, reshaped[i]->outTensor(ReshapeBaseOp::getOutIndex())->id);
        }

        concatOp->createAndConnectOutTensor(ConcatOp::getOutIndex(), newId);
        concatOp->setup();
        return concatOp;
      };

      // create ConcatInplaceOp for the reshaped weights
      auto concatWeightsOp =
          getConcatInplace(reshapeWeightOps, concatWeightsNameStream.str());
      auto concatedWeightsTensorId =
          concatWeightsOp->outTensor(ConcatOp::getOutIndex())->id;

      // create ConcatInplaceOp for the reshaped grads (or sources op copies)
      auto concatGradsOp =
          getConcatInplace(reshapeUpdaterOps, concatUpdatersNameStream.str());
      auto concatedGradsTensorId =
          concatGradsOp->outTensor(ConcatOp::getOutIndex())->id;

      // create the new, merged variable update
      auto tempOp = canonTargetOp->clone();
      for (auto &x : optimizerInputs) {
        tempOp->connectInTensor(x.first, x.second);
      }

      Op *multiUpdateOp = tempOp.get();
      graph.moveIntoGraph(std::move(tempOp));
      multiUpdateOp->connectInTensor(VarUpdateOp::getVarToUpdateInIndex(),
                                     concatedWeightsTensorId);
      multiUpdateOp->connectInTensor(
          VarUpdateWithUpdaterOp::getUpdaterInIndex(), concatedGradsTensorId);

      multiUpdateOp->createAndConnectOutTensor(
          VarUpdateOp::getUpdatedVarOutIndex(),
          "updated___" + concatedWeightsTensorId);

      multiUpdateOp->setup();
      logging::transform::debug("Multi-update of {} created.",
                                multiUpdateOp->str());
    }
  }

  for (auto vop : toRemove) {

    auto outTensor    = vop->outTensor(VarUpdateOp::getUpdatedVarOutIndex());
    auto outTensorId  = outTensor->id;
    auto outTensorStr = outTensor->str();

    // disconnect and delete the single var updater and its output
    logging::transform::debug("Removing inputs of {}", vop->str());
    vop->disconnectAllInputs();
    logging::transform::debug("Removing outputs of {}", vop->str());
    vop->disconnectAllOutputs();
    logging::transform::debug("Removing {}", vop->str());
    graph.eraseOp(vop->id);
    logging::transform::debug("Removing {}", outTensorStr);
    graph.getTensors().remove(outTensorId);
  }
  logging::transform::debug("Removed all ops VarUpdateOps");

  return changed;
}

namespace {
bool initAll   = Transform::registerTransform(new MergeAllVarUpdates);
bool initAuto  = Transform::registerTransform(new MergeTightThreshold);
bool initAuto2 = Transform::registerTransform(new MergeLooseThreshold);
} // namespace

} // namespace popart
