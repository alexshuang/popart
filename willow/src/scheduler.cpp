// Copyright (c) 2018 Graphcore Ltd. All rights reserved.
#include <boost/filesystem.hpp>

#include <algorithm>
#include <array>
#include <numeric>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <poprithms/logging/timepartitionlogger.hpp>
#include <popart/error.hpp>
#include <popart/filereader.hpp>
#include <popart/graph.hpp>
#include <popart/ir.hpp>
#include <popart/op.hpp>
#include <popart/op/remote.hpp>
#include <popart/scheduler.hpp>
#include <popart/tensor.hpp>
#include <popart/tensorindex.hpp>
#include <popart/tensors.hpp>
#include <popart/topocons.hpp>
#include <popart/transforms/pipeline.hpp>
#include <poparttracepoint.hpp>

#include <poprithms/logging/logging.hpp>
#include <poprithms/logging/timepartitionlogger.hpp>
#include <poprithms/schedule/anneal/graph.hpp>
#include <poprithms/schedule/anneal/transitiveclosureoptimizations.hpp>

namespace popart {

using poprithms::schedule::anneal::AllocAddress;
using poprithms::schedule::anneal::AllocWeight;
using poprithms::schedule::anneal::OpAddress;
using poprithms::schedule::anneal::ScheduleIndex;
using RithmicGraph   = poprithms::schedule::anneal::Graph;
using KahnTieBreaker = poprithms::schedule::anneal::KahnTieBreaker;

namespace {
std::string ioNames(Op *op) {
  std::ostringstream oss;
  for (auto elem : op->input->tensorIdMap()) {
    oss << elem.second << '_';
  }
  for (auto elem : op->output->tensorIdMap()) {
    oss << elem.second << '_';
  }
  return oss.str();
}

class GraphGrower {

private:
  const popart::Graph &pg;
  const uint64_t nOps;
  const std::vector<TensorId> allPopartTensorIds;

  // Populate, 1-1 for popart::Op <-> poprithms Op and
  //               popart::Tensor <-> poprithms Alloc.
  std::unordered_map<popart::Tensor *, AllocAddress> allocAddresses;
  std::unordered_map<popart::Op *, OpAddress> opAddresses;
  std::vector<Op *> addressToOp;
  RithmicGraph g;

public:
  GraphGrower(const Graph &_pg_)
      : pg(_pg_), nOps(pg.getOps().size()),
        allPopartTensorIds(pg.getTensors().getAllTensorIds()) {}

  bool operator==(const GraphGrower &rhs) const {
    return g == rhs.g && allocAddresses == rhs.allocAddresses &&
           opAddresses == rhs.opAddresses;
  }

  // Get the schedule from the RithmicGraph as a vector of Op pointers.
  // The RithmicGraph must have already been initialised through a call to
  // `GraphGrower::initialize`.
  std::vector<Op *> getSchedule() const {

    // 1. Get vector of all ops.
    // We know all op addresses are 0..nOps
    std::vector<OpAddress> opAddrs(nOps);
    std::iota(opAddrs.begin(), opAddrs.end(), 0);

    // 2.
    const auto schToOpAddr = g.getSubSchedule(opAddrs);

    // 3. Convert schedule on OpAddress to schedule on popart::Op.
    std::vector<Op *> schToOp;
    schToOp.reserve(nOps);

    std::transform(schToOpAddr.cbegin(),
                   schToOpAddr.cend(),
                   std::back_inserter(schToOp),
                   [this](const auto &opAddr) { return addressToOp[opAddr]; });

    return schToOp;
  }

  void minSumLivenessAnneal(const std::map<std::string, std::string> &a) {

    const auto scopedStopwatch =
        pg.getIr().timePartitionLogger().scopedStopwatch(
            "Scheduler annealing step");

    std::ostringstream oss;
    oss << "[Scheduler] Graph with N=" << g.nOps() << " Poprithms Ops and "
        << " N=" << opAddresses.size() << " PopART Ops, entering annealing. ";
    logging::ir::debug(oss.str());

    auto ll = logging::Level::Trace;
    std::string strBefore;
    if (logging::shouldLog(logging::Module::ir, ll)) {
      strBefore = g.getLivenessString();
    }
    g.minSumLivenessAnneal(a);
    if (logging::shouldLog(logging::Module::ir, ll)) {
      auto strAfter = g.getLivenessString();
      std::ostringstream oss;
      oss << "Liveness string BEFORE annealing:\n" << strBefore << "\n\n";
      oss << "Liveness string AFTER  annealing:\n" << strAfter << "\n\n";
      logging::log(logging::Module::ir, ll, oss.str());
    }

    logging::ir::debug("[Scheduler] annealing step complete.");
  }
  void initialize(KahnTieBreaker ktb, bool requireOptimal_b) {

    const auto scopedStopwatch =
        pg.getIr().timePartitionLogger().scopedStopwatch(
            "Initializing scheduler");
    POPART_TRACEPOINT();

    // if an optimal schedule is not required, or if there are too many Ops in
    // this Graph, initialize cheaply, otherwise invest in a good initialization
    // by running transitive closure optimizations. See poprithms::anneal
    // project for more information about the transitive closure optimizations.
    if (!requireOptimal_b ||
        g.nOps() > pg.getIr()
                       .getSessionOptions()
                       .transitiveClosureOptimizationThreshold) {
      // the cheap (fast) initialization:
      g.initialize(ktb);
    } else {
      // the expensive (slow) initialization:
      g.initialize(
          ktb,
          1011,
          poprithms::schedule::anneal::TransitiveClosureOptimizations::allOn());
    }
  }
  void finalize() { g.finalize(); }
  bool isSchedulable() const { return g.isSchedulable(); }
  std::string getSerializationString() const {
    return g.getSerializationString();
  }

  Op *toOp(OpAddress a) const { return addressToOp.at(a); }

  void setBasic() {
    addressToOp.reserve(nOps);
    for (const auto &popartTensorId : allPopartTensorIds) {
      auto t = pg.getTensors().get(popartTensorId);
      // We ignore Variable Tensors contribution, as they are always live.
      // TODO(jn) confirm that ping-pong and host reduction agree with this.
      if (t->tensorType() != TensorType::Variable) {
        auto w            = static_cast<AllocWeight>(t->info.nbytes());
        allocAddresses[t] = g.insertAlloc(w);
      }
    }
    for (const auto &x : pg.getOps()) {
      auto op         = x.second.get();
      opAddresses[op] = g.insertOp({}, {}, op->str());
      addressToOp.push_back(op);
    }
    for (const auto &x : pg.getOps()) {
      auto op        = x.second.get();
      auto opAddress = opAddresses[op];
      for (const auto t : op->input->tensors()) {
        if (auto producer = t->getProducerUnsafe()) {
          g.insertConstraint(opAddresses[producer], opAddress);
        }
        // Don't consider allocs for variables and overwritten tensors
        if (t->tensorType() != TensorType::Variable &&
            !op->overwritesTensor(t)) {
          g.insertOpAlloc(opAddress, allocAddresses[t]);
        }
      }
      for (const auto popartTensor : op->output->tensors()) {
        g.insertOpAlloc(opAddress, allocAddresses[popartTensor]);
      }
      for (const auto before : pg.topoCons->getBefores(op)) {
        g.insertConstraint(opAddresses[before], opAddress);
      }
    }
  }

  void annotateExecutionPhase() {
    // Insert bin constraints to ensure ops are sorted by execution phase.
    std::vector<std::vector<OpAddress>> bins;
    for (const auto &x : pg.getOps()) {
      auto op = x.second.get();
      if (op->getOptionalExecutionPhase()) {
        auto opAddress = opAddresses[op];
        auto phase     = *op->getOptionalExecutionPhase();
        if (phase < -1) {
          throw internal_error(
              "phase < -1 unexpected. This function needs adjustment");
        }
        uint64_t binIndex = static_cast<uint64_t>(1LL + phase);
        if (binIndex >= bins.size()) {
          bins.resize(binIndex + 1);
        }
        bins[binIndex].push_back(opAddress);
      }
    }
    g.insertBinConstraints(bins, "executionPhaseStart_");
  }

  void annotateExecutionContext() {

    std::vector<OpAddress> weightsToOps;
    std::vector<OpAddress> normalOps;
    std::vector<OpAddress> accumulateOuter;
    std::vector<OpAddress> weightsFromOps;
    std::vector<OpAddress> optimizerFromOps;
    for (const auto &x : pg.getOps()) {
      auto op        = x.second.get();
      auto opAddress = opAddresses[op];
      switch (op->settings.executionContext) {
      case (ExecutionContext::WeightsFromHostFragment): {
        weightsFromOps.push_back(opAddress);
        break;
      }
      case (ExecutionContext::Normal): {
        normalOps.push_back(opAddress);
        break;
      }
      case (ExecutionContext::AccumulateOuterFragment): {
        accumulateOuter.push_back(opAddress);
        break;
      }
      case (ExecutionContext::WeightsToHostFragment): {
        weightsToOps.push_back(opAddress);
        break;
      }
      case (ExecutionContext::OptimizerFromHostFragment): {
        // do nothing.
        break;
      }
      case (ExecutionContext::Subgraph): {
        // do nothing.
        break;
      }
      default: {
        throw error("Unsupported ExecutionContext ({})",
                    op->settings.executionContext);
      }
      }
    }
    std::vector<std::vector<OpAddress>> bins;
    if (!weightsFromOps.empty()) {
      bins.push_back(weightsFromOps);
    }
    if (!normalOps.empty()) {
      bins.push_back(normalOps);
    }
    if (!accumulateOuter.empty()) {
      bins.push_back(accumulateOuter);
    }
    if (!weightsToOps.empty()) {
      bins.push_back(weightsToOps);
    }
    if (bins.size() > 1) {

      g.insertBinConstraints(bins, "executionContext_");
    }
  }

  void annotatePipelineStages() {
    // Adding pipelineStage bins is not required for correctness.
    // Constraining the Ops to be within their pipelineStage improves
    // scheduling runtime as swaps with no effect are invalid.
    std::vector<std::vector<OpAddress>> bins;
    for (const auto &x : pg.getOps()) {
      auto op = x.second.get();
      if (op->hasPipelineStage() &&
          op->settings.executionContext == ExecutionContext::Normal) {
        auto opAddress = opAddresses[op];
        auto stage     = *op->getOptionalPipelineStage();
        if (stage < -1) {
          throw internal_error(
              "stage < -1 unexpected. This function needs adjustment");
        }
        uint64_t binIndex = static_cast<uint64_t>(1LL + stage);
        if (binIndex >= bins.size()) {
          bins.resize(binIndex + 1);
        }
        bins[binIndex].push_back(opAddress);
      }
    }

    g.insertBinConstraints(bins, "PipelineStageStart_");
  }

  // The general setting of an op's scheduledPreLoss setting may look like:
  //
  //         scheduledPreLoss?
  // Op0     Yes
  // Op1     Yes
  //     ...
  // Loss    No
  // Loss'   No
  //     ...
  // OpN-1   No
  // OpN     No
  //
  // However, the loss final loss can be computed arbitrarily, and therefore
  // gradient operations can be grown in the auto-diff transform that do not
  // have a dependency of any operations with a path to the loss. For example,
  // if:
  //   loss = Mul(ReduceSum(Reshape(probs)), const)
  // the ReshapeGrad, ReduceSumGrad and MulGrad operations that produce the
  // gradient of 'loss' tensor do not depend on operations with a path to the
  // 'loss' tensor. Therefore they can be scheduled early, leading to corrupted
  // scheduledPreLoss settings, such as:
  //
  //         scheduledPreLoss?
  // Op0     Yes
  // Loss'   No
  // Op1     No
  //     ...
  // Loss    No
  //     ...
  // OpN-1   No
  // OpN     No.
  //
  // The implicit recomputation transform depends on this setting
  // correctly indicating whether an op is in the forward or backward
  // pass, so insert scheduler constraints to prevent this from happening.
  void annotateToLossFromLoss() {
    // All ops that have:
    //   op.fromLoss == PathFromLoss::Yes, and
    //   op.toLoss == PathToLoss::No
    // are scheduled after all ops that have:
    //   op.toLoss == PathToLoss::Yes
    std::vector<OpAddress> toLoss;
    std::vector<OpAddress> fromLossOnly;

    for (const auto &x : pg.getOps()) {
      auto op        = x.second.get();
      auto opAddress = opAddresses[op];

      if (op->toLoss == PathToLoss::Yes) {
        toLoss.push_back(opAddress);
      } else if (op->toLoss == PathToLoss::No &&
                 op->fromLoss == PathFromLoss::Yes) {
        fromLossOnly.push_back(opAddress);
      }
    }
    g.insertBinConstraints({toLoss, fromLossOnly}, "PreOrPostLoss_");
  }

  void annotateAccumulateOuterFragmentOps() {
    // The scheduler can be slow when there are a lot of ops unconstrained in
    // the accumulate outer fragment. To battle this, we sometimes add
    // constraints here. Depending on where we are in the IR pipeline and
    // session options this may be done in different ways. If the user set
    // 'enableAccumulateOuterFragmentParallelization' we will use bin
    // constraints this transform.
    if (pg.getIr().getSessionOptions().enablePipelining) {
      auto aufSettings =
          pg.getIr().getSessionOptions().accumulateOuterFragmentSettings;
      auto schedule = aufSettings.schedule;
      if (schedule == AccumulateOuterFragmentSchedule::OverlapCycleOptimized ||
          schedule == AccumulateOuterFragmentSchedule::OverlapMemoryOptimized) {
        // Use cluster grouping from AccumulateOuterFragmentParallelizer
        // transform as bins, this should allow parallelization accross IPUs.
        std::vector<std::vector<OpAddress>> bins;
        auto opBins = pg.getIr().getAccumulateOuterFragmentBinConstraints(pg);
        for (auto opBin : opBins) {
          std::vector<OpAddress> bin;
          for (auto op : opBin) {
            bin.push_back(opAddresses[op]);
          }
          bins.push_back(bin);
        }
        g.insertBinConstraints(bins, "AccumulateOuterFragmentCluster_");
      } else if (schedule == AccumulateOuterFragmentSchedule::Serial) {
        // Revert back to default behaviour for pipelining models where we
        // serialize the ops based on virtual graph id.
        std::vector<std::vector<OpAddress>> outer_bins;
        for (const auto &x : pg.getOps()) {
          auto op         = x.second.get();
          bool should_bin = op->hasPipelineStage() && op->hasVirtualGraphId() &&
                            (op->settings.executionContext ==
                             ExecutionContext::AccumulateOuterFragment);

          if (should_bin) {
            auto opAddress = opAddresses[op];
            auto vgraph    = *op->getOptionalVGraphId();
            if (vgraph < -1) {
              throw internal_error("vgraph < -1 unexpected. This function "
                                   "needs adjustment");
            }
            uint64_t binIndex = static_cast<uint64_t>(1LL + vgraph);
            if (binIndex >= outer_bins.size()) {
              outer_bins.resize(binIndex + 1);
            }
            outer_bins[binIndex].push_back(opAddress);
          }
        }
        g.insertBinConstraints(outer_bins, "OuterPipelineStageStart_");
      }
    }
  }

  void annotatePriorities() {
    std::vector<std::array<OpAddress, 2>> ties;
    for (const auto &x : pg.getOps()) {
      auto op        = x.second.get();
      auto tiedAfter = opAddresses[op];
      for (auto op2 : pg.topoCons->getTiedBefores(op)) {
        ties.push_back({opAddresses[op2], tiedAfter});
      }
    }
    // more important than actual memory (use +1 otherwise)
    g.insertAttractions(ties, AllocWeight(1.0, -1));

    std::vector<OpAddress> opIotas(nOps);
    std::iota(opIotas.begin(), opIotas.end(), 0);

    // A priority which takes precedence over memory liveness:
    using OpPriority = double;
    std::vector<
        std::
            tuple<ExecutionPhase, OpPriority, BatchSerializedPhase, OpPriority>>
        super;

    // A priority which is secondary to memory liveness:
    using OpTypeStr = std::string;
    using IoNames   = std::string;
    using UniqueId  = int;
    std::vector<std::tuple<OpTypeStr, IoNames, UniqueId>> sub;

    for (const auto &x : pg.getOps()) {
      auto op             = x.second.get();
      auto op_batchserial = op->getOptionalBatchSerializedPhase();
      auto op_phase       = op->getOptionalExecutionPhase();
      auto op_priority    = op->settings.schedulePriority;

      // Executuion phase -1 to N are reserved
      // -2 : No execution phase set (unusedExecutionPhase)
      // -1 : Load weights of phase 0
      // 0 - N: Compute phase n, load weights of phase n+1
      auto op_phase_or =
          op_phase &&
                  pg.getIr().getSessionOptions().executionPhaseSettings.phases >
                      1
              ? *op_phase
              : unusedExecutionPhase;

      // Batchserial -1 to N are reserved
      // -2 : No batchserial phase set (unusedBatchSerializedPhase)
      // -1 : Init accumulator and updatee tensors
      // 0 - N : Compute batch element n
      auto op_batchserial_or =
          op_batchserial && pg.getIr()
                                    .getSessionOptions()
                                    .batchSerializationSettings.factor > 1
              ? *op_batchserial
              : unusedBatchSerializedPhase;

      auto op_priority_pre_or  = op_batchserial ? 0.0 : op_priority;
      auto op_priority_post_or = op_batchserial ? op_priority : 0.0;

      // to strongly encourage Ops to be appear in
      // 1) ascending execution phases
      // 2) descending priority for ops without batch-serial phase
      // 3) ascending batch-serial phase
      // 4) descending priority within batch-serial phase
      // 5) ScheduledPreLoss::Yes before ScheduledPreLoss::No
      super.push_back({-op_phase_or,
                       op_priority_pre_or,
                       -op_batchserial_or,
                       op_priority_post_or});
      sub.push_back({op->opid.type, ioNames(op), op->id});
    }
    g.insertStartAttractors(opIotas, super, -2);
    g.insertStartAttractors(opIotas, sub, +2);
  }

  void appendGCons(const OpsBeforeKey &gCons) {
    for (const auto &x : gCons) {
      auto after        = x.first;
      auto befores      = x.second;
      auto addressAfter = opAddresses[after];
      for (auto b : befores) {
        auto addressBefore = opAddresses[b];
        g.insertConstraint(addressBefore, addressAfter);
      }
    }
  }

  poprithms::schedule::anneal::Graph getGraph() const { return g; }
};

} // namespace

class ScheduleCacher {
public:
  ScheduleCacher(const Graph &pg) : grower(std::make_unique<GraphGrower>(pg)) {}
  const GraphGrower &getGrower() const { return *grower; }
  const std::vector<Op *> &getSchedule() const { return schedule; }
  void setSchedule(const std::vector<Op *> &s, const bool scheduleIsOptimal) {
    schedule           = s;
    scheduleIsOptimal_ = scheduleIsOptimal;
  }
  void setGrower(std::unique_ptr<GraphGrower> g) { grower = std::move(g); }
  bool scheduleIsOptimal() const { return scheduleIsOptimal_; }
  void registerHit() {
    ++nHits;
    logging::ir::debug(
        "[Scheduler] SchedulerCacher hit # {} (Misses so far : {})",
        nHits,
        nMisses);
  }
  void registerMiss() {
    ++nMisses;
    logging::ir::debug(
        "[Scheduler] SchedulerCacher miss # {} (Hits so far : {})",
        nMisses,
        nHits);
  }

private:
  std::unique_ptr<GraphGrower> grower;

  std::vector<Op *> schedule;

  // Is the currently cached schedule the optimal min sum-liveness schedule, or
  // merely any valid topological traversal?
  bool scheduleIsOptimal_;

  int nHits{0};
  int nMisses{0};
};

// Helpers for `Scheduler`
namespace {

KahnTieBreaker kahnTieBreakerFromString(const std::string &ktbString) {
  std::string ktbLower = ktbString;
  std::transform(ktbLower.begin(),
                 ktbLower.end(),
                 ktbLower.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  KahnTieBreaker ktb;

  if (ktbLower == "fifo") {
    ktb = KahnTieBreaker::FIFO;
  } else if (ktbLower == "greedy") {
    ktb = KahnTieBreaker::GREEDY;
  } else if (ktbLower == "random") {
    ktb = KahnTieBreaker::RANDOM;
  } else {
    throw error("Unrecognised KahnTieBreaker, {}", ktbString);
  }

  return ktb;
}

void serializePoprithmsGraph(
    const GraphGrower *grower,
    const std::string &serializedPoprithmsAnnealGraphsDir) {

  auto dirName =
      boost::filesystem::canonical(serializedPoprithmsAnnealGraphsDir).string();

  if (!boost::filesystem::exists(dirName)) {
    std::ostringstream oss;
    oss << "No directory, `" << dirName << "' exists. "
        << " The SessionOptions directory serializedPoprithmsAnnealGraphsDir "
        << "must already exist, PopART will not create it. "
        << "If you do not want to serialize Poprithms Graph, set "
        << " serializePoprithmsAnnealGraphs "
        << " to false.";
    throw error(oss.str());
  }
  auto getTargetName = [dirName](int i) {
    return io::appendDirFn(
        dirName, "poprithms_anneal_graph_" + std::to_string(i) + ".json");
  };

  // iterate through file names until a non-existant one is found
  int modelNumber{0};
  while (boost::filesystem::exists(getTargetName(modelNumber))) {
    ++modelNumber;
  }
  auto filename = getTargetName(modelNumber);

  std::ofstream ofs;
  ofs.open(filename);
  if (!ofs.is_open()) {
    throw error("Failed to open file {}", filename);
  }
  ofs << grower->getSerializationString();
  ofs.close();

  logging::ir::info("[Scheduler] written {} ", filename);
}

void defaultAnnotate(GraphGrower *grower,
                     const bool optimizeForAnnealing,
                     const OpsBeforeKey &gCons,
                     const Graph &pg,
                     const bool respectExecutionPhases) {
  grower->setBasic();
  grower->appendGCons(gCons);
  if (respectExecutionPhases &&
      pg.getIr().getSessionOptions().executionPhaseSettings.phases > 1) {
    grower->annotateExecutionPhase();
  }
  if (pg.getIr().getSessionOptions().enablePipelining) {
    grower->annotatePipelineStages();
  }
  if ((pg.getIr().autoRecomputationEnabled() ||
       pg.getIr().getMainGraph().hasUserRecomputeOps()) &&
      !pg.getIr().getSessionOptions().explicitRecomputation) {
    grower->annotateToLossFromLoss();
  }
  if (optimizeForAnnealing) {
    grower->annotateAccumulateOuterFragmentOps();
  }
  grower->annotateExecutionContext();
  if (optimizeForAnnealing) {
    grower->annotatePriorities();
  }
  grower->finalize();
}

void defaultMinSumLivenessAnneal(GraphGrower *grower,
                                 double timeLimitSeconds,
                                 int64_t swapLimitCount) {

  POPART_TRACEPOINT();
  grower->minSumLivenessAnneal(
      {{"debug", "0"},
       {"seed", "1011"},
       {"timeLimitSeconds", std::to_string(timeLimitSeconds)},
       {"swapLimitCount", std::to_string(swapLimitCount)}});
}

} // namespace

// TODO(jn)
// 1) smallest cycle function, to report with on failure.
// 2) we currently assume that each Tensor is a unique allocation. Improve this,
// so that inplace Ops are accurately described.

poprithms::schedule::anneal::Graph
getScheduledGraph(const Graph &g, const std::vector<Op *> schedule) {

  auto grower = std::make_unique<GraphGrower>(g);
  grower->setBasic();
  OpsBeforeKey cons;
  for (uint64_t i = 1; i < schedule.size(); ++i) {
    cons.insert({schedule[i], {schedule[i - 1]}});
  }
  grower->appendGCons(cons);
  grower->initialize(KahnTieBreaker::GREEDY, false);
  return grower->getGraph();
}

std::vector<Op *>
Scheduler::getSchedule(const OpsBeforeKey &gCons,
                       const Graph &pg,
                       const RequireOptimalSchedule requireOptimalSchedule,
                       const bool respectExecutionPhases,
                       const double timeLimitSeconds,
                       const int64_t swapLimitCount,
                       const std::string &kahnTieBreakerString) {

  const auto schedulerTimeTracker =
      pg.getIr().timePartitionLogger().scopedStopwatch("Scheduler");

  // TODO(jn) cache advancedOptions too

  // Do nothing on edge case where getOps is empty
  if (pg.getOps().empty()) {
    return {};
  }

  if (!cacher) {
    cacher = std::make_unique<ScheduleCacher>(pg);
  }

  auto grower = std::make_unique<GraphGrower>(pg);

  // Always call with `optimiseForAnnealing = true`, regardless of whether the
  // user is requesting optimal or not, so we construct the same graph and do
  // not get a cache miss.
  defaultAnnotate(grower.get(), true, gCons, pg, respectExecutionPhases);

  /*
    Explanation of the caching logic:

    Sometimes, the user does not require the fully optimised schedule, only
    any valid topological sort. This is controlled by the parameter
    `requireOptimalSchedule`.

    We thus keep track of, in the cacher, whether the currently cached schedule
    is optimal or not.

    These are the possible scenarious when looking up the cache:

    (1) If the schedule is optimal: doesn't matter whether the user wants the
    optimal schedule or not, we can return what we have.

    If the schedule is not optimal:
      (2)   if the user wants the optimal schedule, we have to calculate it.
      (3)   if the user doesn't want the optimal schedule, we can return the
            cached one.

    (2) is the only case where we can't return the cached schedule. In other
    words, if there is a cache hit on the graph, the only scenario that will
    cause it to still be a cache miss, is if the cached schedule is not optimal
    but we want the optimal one.

    One thing to be careful of when implementing is, in (1) when the cached
    schedule is optimal and the user is requesting non-optimal; we do not
    accidentally mark the cached schedule as now non-optimal.
  */
  const bool requireOptimal_b =
      requireOptimalSchedule == RequireOptimalSchedule::Yes;
  const bool cachedIsNotOptimalButRequireOptimal =
      !cacher->scheduleIsOptimal() && requireOptimal_b;

  if (cacher->getGrower() == *grower && !cachedIsNotOptimalButRequireOptimal) {
    cacher->registerHit();
    return cacher->getSchedule();
  }

  cacher->registerMiss();

  if (!pg.getIr()
           .getSessionOptions()
           .serializedPoprithmsAnnealGraphsDir.empty()) {

    auto scopedStopwatch = pg.getIr().timePartitionLogger().scopedStopwatch(
        "Serializing anneal Graph");
    serializePoprithmsGraph(
        grower.get(),
        pg.getIr().getSessionOptions().serializedPoprithmsAnnealGraphsDir);
  }

  const auto ktb = kahnTieBreakerFromString(kahnTieBreakerString);

  grower->initialize(ktb, requireOptimal_b);

  // Using a time and swap limit of 0 will force no annealing to happen.
  if (requireOptimal_b) {
    defaultMinSumLivenessAnneal(grower.get(), timeLimitSeconds, swapLimitCount);
  }

  const std::vector<Op *> finalSchedule = grower->getSchedule();

  cacher->setSchedule(finalSchedule, requireOptimal_b);
  cacher->setGrower(std::move(grower));

  return finalSchedule;
}

bool Scheduler::isSchedulable(const OpsBeforeKey &gCons,
                              const Graph &pg,
                              bool respectExecutionPhases) const {
  GraphGrower grower(pg);

  defaultAnnotate(&grower, false, gCons, pg, respectExecutionPhases);

  return grower.isSchedulable();
}

Scheduler::Scheduler()  = default;
Scheduler::~Scheduler() = default;

} // namespace popart
