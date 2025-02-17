
// Copyright (c) 2018 Graphcore Ltd. All rights reserved.
#include <boost/container_hash/hash.hpp>
#include <boost/random/normal_distribution.hpp>
#include <boost/random/uniform_real_distribution.hpp>
#include <builder_impl.hpp>
#include <customtransformapplier.hpp>
#include <graphfromlosstolossupdater.hpp>
#include <onnxutil.hpp>
#include <poprithms/logging/timepartitionlogger.hpp>
#include <popart/alias/aliasmodelgrower.hpp>
#include <popart/ces/constexpr.hpp>
#include <popart/devicemanager.hpp>
#include <popart/error.hpp>
#include <popart/graph.hpp>
#include <popart/ir.hpp>
#include <popart/logging.hpp>
#include <popart/onnxdebuginfo.hpp>
#include <popart/op/dropout.hpp>
#include <popart/op/exchange/hostcopy.hpp>
#include <popart/op/exchange/multiexchange.hpp>
#include <popart/op/if.hpp>
#include <popart/op/init.hpp>
#include <popart/optimizer.hpp>
#include <popart/pbwrap.hpp>
#include <popart/pointercomparators.hpp>
#include <popart/replicatedstreammode.hpp>
#include <popart/sessionoptions.hpp>
#include <popart/tensor.hpp>
#include <popart/tensordata.hpp>
#include <popart/tensorindex.hpp>
#include <popart/tensorinfo.hpp>
#include <popart/tensornames.hpp>
#include <popart/tensors.hpp>
#include <popart/topocons.hpp>
#include <popart/util.hpp>
#include <popart/variablesettings.hpp>
#include <poparttracepoint.hpp>
// The transformations
#include <onnx/onnx_pb.h>
#include <stochasticroundingassumptionverifier.hpp>
#include <poplar/Graph.hpp>
#include <poplar/StringRef.hpp>
#include <poprithms/memory/inplace/allowmultigatealias.hpp>
#include <poprithms/memory/inplace/checkparallelwriteable.hpp>
#include <poprithms/memory/inplace/constraint.hpp>
#include <poprithms/memory/inplace/graph.hpp>
#include <poprithms/memory/inplace/proposal.hpp>
#include <poprithms/memory/inplace/result.hpp>
#include <poprithms/util/typedinteger.hpp>
#include <popart/alias/aliasmodel.hpp>
#include <popart/dotvisualizer.hpp>
#include <popart/op/copyvarupdate.hpp>
#include <popart/op/ipucopy.hpp>
#include <popart/op/placeholder.hpp>
#include <popart/patterns/adamdecompose.hpp>
#include <popart/patterns/adaptivedecompose.hpp>
#include <popart/patterns/inplace.hpp>
#include <popart/patterns/sgd0decompose.hpp>
#include <popart/patterns/sgd1decompose.hpp>
#include <popart/patterns/sgd2decompose.hpp>
#include <popart/patterns/updateinplaceprioritiesforipu.hpp>
#include <popart/patterns/viewsimplifypattern.hpp>
#include <popart/recompute.hpp>
#include <popart/transforms/accumulateouterfragmentparallelizer.hpp>
#include <popart/transforms/auto_virtual_graph.hpp>
#include <popart/transforms/autodiff.hpp>
#include <popart/transforms/automaticlossscaling.hpp>
#include <popart/transforms/batchserialize.hpp>
#include <popart/transforms/clipweightgradientsbynorm.hpp>
#include <popart/transforms/contiguatecollectivesformerging.hpp>
#include <popart/transforms/decomposegradsum.hpp>
#include <popart/transforms/dynamicoptransform.hpp>
#include <popart/transforms/ensurefp32lossscale.hpp>
#include <popart/transforms/explicitrecompute.hpp>
#include <popart/transforms/hostiosetup.hpp>
#include <popart/transforms/inferpipelinestages.hpp>
#include <popart/transforms/inplaceaccumulategradpartialsintooptimizeraccumtensor.hpp>
#include <popart/transforms/interipucopy.hpp>
#include <popart/transforms/iocomputetilecopy.hpp>
#include <popart/transforms/mainloops.hpp>
#include <popart/transforms/mergecollectives.hpp>
#include <popart/transforms/mergecopies.hpp>
#include <popart/transforms/mergeduplicateops.hpp>
#include <popart/transforms/mergeexchange.hpp>
#include <popart/transforms/mergevarupdates.hpp>
#include <popart/transforms/overlapio.hpp>
#include <popart/transforms/pipeline.hpp>
#include <popart/transforms/preautomaticlossscaling.hpp>
#include <popart/transforms/prune.hpp>
#include <popart/transforms/randomsetup.hpp>
#include <popart/transforms/remotesetup.hpp>
#include <popart/transforms/serializematmuls.hpp>
#include <popart/transforms/stochasticrounding.hpp>
#include <popart/transforms/streamingmemory.hpp>
#include <popart/transforms/subgraphoutline.hpp>
// used to get the packageHash()
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include "popart/attributes.hpp"
#include "popart/basicoptionals.hpp"
#include "popart/bimap.hpp"
#include "popart/commgroup.hpp"
#include "popart/dataflow.hpp"
#include "popart/datatype.hpp"
#include "popart/debugcontext.hpp"
#include "popart/graphid.hpp"
#include "popart/inputshapeinfo.hpp"
#include "popart/names.hpp"
#include "popart/op.hpp"
#include "popart/op/exchange/exchange.hpp"
#include "popart/op/subgraph.hpp"
#include "popart/op/varupdate.hpp"
#include "popart/operatoridentifier.hpp"
#include "popart/operators.hpp"
#include "popart/patterns/pattern.hpp"
#include "popart/patterns/patterns.hpp"
#include "popart/region.hpp"
#include "popart/scheduler_requireoptimal.hpp"
#include "popart/scope.hpp"
#include "popart/tensordebuginfo.hpp"
#include "popart/tensorlocation.hpp"
#include "popart/transforms/transform.hpp"
#include "popart/vendored/optional.hpp"
#include "popart/vertex.hpp"
#include "popart/voiddata.hpp"

#include <popart/popx/popefserializer.hpp>

namespace popart {

std::ostream &operator<<(std::ostream &ost, const OpsBeforeKey &o) {
  for (auto after_befores : o) {
    ost << '\n' << after_befores.first->str();
    ost << "   <-   (";
    for (auto b : after_befores.second) {
      ost << " " << b->str();
    }
    ost << " ).";
  }
  return ost;
}

poprithms::logging::TimePartitionLogger &Ir::timePartitionLogger() const {
  return *timePartitionLogger_;
}

std::string Ir::timePartitionLoggerStr() const {
  // Only log scopes which took 1% or more of the total time:
  const auto thresholdPercentage =
      getSessionOptions()
          .developerSettings.timePartitionLoggerThresholdPercentage;
  return timePartitionLogger().str(thresholdPercentage);
}

Ir::~Ir() = default;

void Ir::confirmNonReservedId(const TensorId &tenId) const {
  for (auto reservedPrefix : reservedPrefixes()) {
    if (tenId.find(reservedPrefix) != std::string::npos) {
      throw error("Provided tensor " + tenId +
                  " has an invalid name: clash with reserved prefix " +
                  reservedPrefix);
    }
  }
}

const ONNX_NAMESPACE::ModelProto &Ir::getModel() const {
  if (!hasOnnxModel()) {
    throw error("Ir::getModel: Ir has no Onnx model");
  }
  return *onnxModel;
}

void Ir::setExternalTensorDataInfo(
    TensorId tId,
    const ONNX_NAMESPACE::TensorProto &tpReference) {
  if (!onnxModel) {
    throw error("Ir::setExternalTensorDataInfo: Ir has no Onnx model");
  }

  // Check tpReference has external info
  if (!tpReference.has_data_location() ||
      tpReference.data_location() != ONNX_NAMESPACE::TensorProto::EXTERNAL) {
    throw error("Trying to set external tensor info for '{}'. Refernce tensor "
                "does not have an external data_location",
                tId);
  }

  ONNX_NAMESPACE::TensorProto &tp = onnxutil::getTensorProto(*onnxModel, tId);

  tp.clear_data_location();
  tp.set_data_location(ONNX_NAMESPACE::TensorProto::EXTERNAL);

  tp.clear_external_data();
  auto externalDataInfo = tp.mutable_external_data();
  *externalDataInfo     = tpReference.external_data();
  for (int i = 0; i < tp.external_data_size(); i++) {
    auto edi = tp.external_data(i);
  }
}

// Data stream tensors are all tensors, excluding:
//  - optimizer tensors
//  - the random seed tensor
std::vector<Tensor *> Ir::dataStreamTensors() const {
  std::vector<Tensor *> dsTensors;
  for (auto tensor : getTensors().getOfType(TensorType::Stream)) {
    if (!tensor->isOptimizerTensor()) {
      if (!tensor->isRandomSeedTensor()) {
        dsTensors.push_back(tensor);
      }
    }
  }
  return dsTensors;
}

std::map<TensorId, std::vector<Tensor *>> Ir::getHostLoadTensors() const {
  std::map<TensorId, std::vector<Tensor *>> hlTensors;
  for (auto op : getAllOps()) {
    if (HostLoadOp *hlop = dynamic_cast<HostLoadOp *>(op)) {
      hlTensors[hlop->getHostStreamTensorId()].push_back(
          hlop->output->tensor(HostLoadOp::getLocalTensorOutIndex()));
    }
    if (MultiExchangeOp *exchangeOp = dynamic_cast<MultiExchangeOp *>(op)) {
      for (int index = 0; index < exchangeOp->getNumExchanges(); ++index) {
        auto descriptor = exchangeOp->getExchangeDescriptor(index);
        if (descriptor.isHostExchange() &&
            descriptor.getDirection() == ExchangeDirection::Load) {
          hlTensors[descriptor.getHostStreamTensorId()].push_back(
              op->input->tensor(
                  exchangeOp->descriptorIndexToInIndices(index).front()));
        }
      }
    }
  }
  return hlTensors;
}

std::map<TensorId, std::vector<Tensor *>> Ir::getHostStoreTensors() const {
  std::map<TensorId, std::vector<Tensor *>> hsTensors;
  for (auto op : getAllOps()) {
    if (HostStoreOp *hsOp = dynamic_cast<HostStoreOp *>(op)) {
      hsTensors[hsOp->getHostStreamTensorId()].push_back(
          op->input->tensor(HostStoreOp::getLocalTensorInIndex()));
    }
    if (MultiExchangeOp *exchangeOp = dynamic_cast<MultiExchangeOp *>(op)) {
      for (int index = 0; index < exchangeOp->getNumExchanges(); ++index) {
        auto descriptor = exchangeOp->getExchangeDescriptor(index);
        if (descriptor.isHostExchange() &&
            descriptor.getDirection() == ExchangeDirection::Store) {
          hsTensors[descriptor.getHostStreamTensorId()].push_back(
              op->input->tensor(
                  exchangeOp->descriptorIndexToInIndices(index).front()));
        }
      }
    }
  }
  return hsTensors;
}

std::vector<Tensor *> Ir::optimizerTensors() const {
  std::vector<Tensor *> optimizerTensors;
  for (auto tensor : getTensors().getOfType(TensorType::Stream)) {
    if (tensor->isOptimizerTensor()) {
      optimizerTensors.push_back(tensor);
    }
  }
  return optimizerTensors;
}

std::vector<Tensor *> Ir::optimizerStateTensors() const {
  std::vector<Tensor *> optimizerStateTensors;
  for (auto tensor : additionalModelProtoTensors) {
    if (tensor->isOptimizerStateTensor()) {
      optimizerStateTensors.push_back(tensor);
    }
  }
  return optimizerStateTensors;
}

void Ir::updateOptimizer(const Optimizer &newOptimizer) {
  auto newOptimizerClone = newOptimizer.clone();
  newOptimizerClone->setFactorsFromOptions(getSessionOptions());
  // Throws if newOptimizerClone is not a valid replacement optimizer.
  optimizer->validReplacement(*newOptimizerClone);
  optimizer = std::move(newOptimizerClone);
}

void Ir::dotCheckpoint(const Ir &ir, std::string check) const {
  DotVisualizer viz(check);
  viz.write(ir);
}

void Ir::confirmNoReservedIds() const {

  if (hasOnnxModel()) {
    auto &onnxGraph = onnxModel->graph();

    for (const auto &in_ : onnxGraph.input()) {
      confirmNonReservedId(in_.name());
    }

    for (const auto &out_ : onnxGraph.output()) {
      confirmNonReservedId(out_.name());
    }
  }

  for (const auto &tenId : inputShapeInfo.getAllTensorIds()) {
    confirmNonReservedId(tenId);
  }
}

IrBundle::IrBundle(const ONNX_NAMESPACE::ModelProto &modelProto_,
                   const InputShapeInfo &inputShapeInfo_,
                   const DataFlow &dataFlow_,
                   const TensorId &loss_,
                   const Optimizer *optimizer_,
                   DeviceInfo &deviceInfo_,
                   const SessionOptions &userOptions_,
                   const Patterns &patterns_,
                   const std::string sessionName_)
    : modelProto(modelProto_), inputShapeInfo(inputShapeInfo_),
      dataFlow(dataFlow_), loss(loss_), optimizer(optimizer_),
      deviceInfo(deviceInfo_), userOptions(userOptions_), patterns(patterns_),
      sessionName(sessionName_) {}

namespace {

const constexpr char *const partitionLoggerName{"TimePartitionLogger"};
static uint64_t static_id = 0;

} // namespace

Ir::Ir()
    : id(static_id++),
      timePartitionLogger_(
          std::make_unique<poprithms::logging::SwitchingTimePartitionLogger>(
              partitionLoggerName)),
      onnxModel(nullptr) {

  graphs.insert(
      {GraphId::root(), std::make_unique<Graph>(*this, GraphId::root())});
}

void Ir::setOnnxModel(const ONNX_NAMESPACE::ModelProto &model) {
  onnxModel.reset(new ONNX_NAMESPACE::ModelProto(model));
}

void Ir::setDataFlow(const DataFlow &df) {
  // Inference  mode require an anchor
  if (!canTrain() && df.nAnchors() == 0) {
    throw error("User must specify an anchor tensor when doing inference.");
  } else {
    dataFlow = df;
  }

  // Populate anchor remap
  for (auto &anchor : dataFlow.anchors()) {
    anchorRemap.insert(anchor, anchor);
  }
}

bool Ir::virtualGraphsEnabled() const {
  return userOptions.virtualGraphMode != VirtualGraphMode::Off;
}

SyntheticDataMode Ir::syntheticDataMode() const {
  return getSessionOptions().syntheticDataMode;
}

bool Ir::useSyntheticData() const {
  return syntheticDataMode() != SyntheticDataMode::Off;
}

bool Ir::usingEngineCache(const SessionOptions &opts, const DeviceInfo *di) {
  return opts.enableEngineCaching && !opts.cachePath.empty() &&
         di->isHwCompatible();
}

void Ir::setUserOptions(const SessionOptions &flags) { userOptions = flags; }

void Ir::setInputShapeInfo(const InputShapeInfo &info) {
  inputShapeInfo = info;
}

void Ir::setPatterns(const Patterns &p) {
  logging::pattern::info("Enabling {} patterns", getPatternLevelStr(p));
  patterns = p;
}

std::string Ir::getPatternLevelStr(const Patterns &p) {
  if (isPatternsLevel(p, PatternsLevel::All)) {
    return "all";
  } else if (isPatternsLevel(p, PatternsLevel::Default)) {
    return "default";
  } else if (isPatternsLevel(p, PatternsLevel::Minimal)) {
    return "minimal";
  } else if (isPatternsLevel(p, PatternsLevel::NoPatterns)) {
    return "no";
  } else {
    return "custom";
  }
}

bool Ir::isPatternsLevel(const Patterns &p, PatternsLevel level) {
  Patterns refPatterns(level);
  if (refPatterns == p) {
    return true;
  } else {
    return false;
  }
}

void Ir::removeIsolatedTensors(bool retainUsedIOTensors,
                               bool retainAllIOTensors,
                               bool retainVarTensors,
                               bool retainConstTensors) {
  auto scopedStopwatch =
      timePartitionLogger().scopedStopwatch("Removing isolated Tensors");
  getTensors().removeIsolated(retainUsedIOTensors,
                              retainAllIOTensors,
                              retainVarTensors,
                              retainConstTensors);
}

void Ir::removeIsolatedGraphs() {
  auto sorted = getGraphSchedule(getMainGraph().id);

  if (sorted.size() != graphs.size()) {
    std::vector<GraphId> sortedIds;
    std::transform(sorted.begin(),
                   sorted.end(),
                   std::back_inserter(sortedIds),
                   [](const Graph *g) { return g->id; });
    for (auto it = graphs.begin(); it != graphs.end();) {
      it = (std::find(sortedIds.begin(), sortedIds.end(), it->first) ==
            sortedIds.end())
               ? graphs.erase(it)
               : std::next(it);
    }
  };
}

void Ir::setExecutionMode(const ExecutionMode &mode) { executionMode = mode; }

void Ir::setOptimizer(const Optimizer &o) {
  optimizer = o.clone();
  optimizer->setFactorsFromOptions(getSessionOptions());

  // We create scale factor Tensors now (they will be removed later if not
  // used). All other optimizer Tensors are created just-in-time during Graph
  // construction
  for (DataType dt : {DataType::FLOAT, DataType::FLOAT16}) {
    auto id = optimizer->getLossScalingTensorId(dt);
    DebugInfo debugInfo(optimizer->getDebugContext(), "popartbuilder");
    ensureOptimizerTensorCreated(id, {dt, {}}, {debugInfo, id});
  }
}

void Ir::setDeviceInfo(DeviceInfo &di) { deviceInfo = &di; }

const DeviceInfo *Ir::getDeviceInfo() const { return deviceInfo; }

void Ir::logIr() const {
  logging::ir::debug("Logging the IR:");
  std::stringstream ss2;
  append(ss2);
  logging::ir::debug(ss2.str());
  logging::ir::debug("End IR");
}

void Ir::compareWithSavedHash(const HashesMap &cacheEntries) {
  if (false == Ir::usingEngineCache(userOptions, deviceInfo)) {
    logging::ir::info("Engine caching disabled. Skipping Ir hashing.");
    return;
  }

  // Is the hash present in cacheEntries?
  bool possibleMatch = cacheEntries.count(*hash_) > 0;

  if (possibleMatch) {
    // Check that the cache file is valid and that the hash found in it matches
    // the current IR.
    const auto &filePath = cacheEntries.at(*hash_);
    auto possibleHash =
        popx::serialization::Reader::checkFileForValidPoplarExecutable(
            filePath);
    if (possibleHash.has_value()) {
      hashMatched_ = *hash_ == *possibleHash;
      if (!hashMatched_) {
        logging::session::warn("Cache file hash did not match the IR hash, "
                               "ignoring false cache hit.");
      }
    }
  }
}

void Ir::computeHash(size_t hashSeed) {
  hash_ = hashSeed;
  boost::hash_combine(*hash_, *this);
}

void Ir::verifyPipelineSettings() const {
  if (!getSessionOptions().enablePipelining) {
    // If pipelining is disabled, make sure no ops have a pipeline stage set.
    for (auto &id_graph : graphs) {
      auto &graph = id_graph.second;
      for (auto &id_op : graph->getOps()) {
        auto &op = id_op.second;
        // no pipeline stage
        op->setPipelineStage({});
      }
    }
  } else {

    if (getSessionOptions().implicitPipeliningEnabled() &&
        (!virtualGraphsEnabled() || getNumVirtualGraphIds() == 1)) {
      throw error("Pipelining requires more than 1 IPU (currently {}) and the "
                  "'virtualGraphMode' session option "
                  "to not be VirtualGraphMode::Off (currently {}).",
                  getNumVirtualGraphIds(),
                  getSessionOptions().virtualGraphMode);
    }

    auto getPipelineStage = [](auto x) -> PipelineStage {
      if (x->hasPipelineStage()) {
        return x->getPipelineStage();
      } else {
        return unusedPipelineStage;
      }
    };

    auto getVirtualGraphId = [](auto x) -> VGraphId {
      if (x->hasVirtualGraphId()) {
        return x->getVirtualGraphId();
      } else {
        return unusedVGraphId;
      }
    };

    // collect a set of vgraph ids for each pipeline stage
    std::map<PipelineStage, std::vector<Op *>> pipelineStages;
    std::map<VGraphId, std::set<PipelineStage>> pipelineStagesPerVGraph;

    for (auto &id_op : getMainGraph().getOps()) {
      auto op = id_op.second.get();
      if (!op->isConvertibleTo<IpuCopyOp>()) {
        auto ps = getPipelineStage(op);
        pipelineStages[ps].push_back(op);

        auto vgraph = getVirtualGraphId(op);
        pipelineStagesPerVGraph[vgraph].insert(ps);
      }
    }

    // if no ops have had the pipeline stage attribute set, the virtual graph id
    // will be used.

    // some ops have not had the pipeline stage attribute set
    if (pipelineStages.count(-1) != 0 && pipelineStages.size() > 1) {
      std::stringstream ss;
      ss << "Only some ops have had their pipeline stage set. Ops missing the "
            "pipeline stage:";
      for (auto &id_op : getMainGraph().getOps()) {
        auto op = id_op.second.get();
        if (!op->isConvertibleTo<IpuCopyOp>()) {
          if (getPipelineStage(op) == -1) {
            ss << logging::format("\n  {}", op->debugName());
          }
        }
      }
      throw error(ss.str());
    }
    // all ops have had the pipeline stage attribute set
    else if (pipelineStages.count(-1) == 0) {

      // check that all ops in a pipeline stage have the same virtualGraph
      for (auto &ps_ops : pipelineStages) {
        auto ps   = ps_ops.first;
        auto &ops = ps_ops.second;

        std::set<VGraphId> vgraphs;
        for (auto op : ops) {
          // ops may not have a virtual graph id yet as the virtualGraphMode may
          // be Auto. In this case getVirtualGraphId returns -1 and we just
          // check that all ops in the pipeline stage are on virtual graph -1
          vgraphs.insert(getVirtualGraphId(op));
        }

        if (vgraphs.size() > 1) {
          std::vector<std::string> opNames;
          for (auto op : ops) {
            opNames.push_back(op->debugName());
          }

          throw error("Ops {} have the same pipeline stage {}, but different "
                      "virtual graph ids {}. All ops with the same pipeline "
                      "stage must also have the same virtual graph id",
                      opNames,
                      ps,
                      vgraphs);
        }
      }
    }
  }

  if (getSessionOptions().createImplicitPipeliningFwdOnlyProgram) {
    logging::ir::warn("Implicit pipelining forward-only program is deprecated "
                      "and will be removed in future releases.");

    if (getSessionOptions().explicitPipeliningEnabled()) {
      throw error("Implicit pipelining forward-only program is not supported "
                  "with explicit pipelining.");
    }
    if (!getSessionOptions().implicitPipeliningEnabled()) {
      throw error("Implicit pipelining forward-only program is not supported "
                  "without implicit pipelining.");
    }
    if (!getSessionOptions().enableGradientAccumulation ||
        getSessionOptions().accumulationFactor < 1) {
      throw error("Implicit pipelining forward-only program is not supported "
                  "without gradient accumulation.");
    }
  }
}

void Ir::verifyExecutionPhaseSettings() const {
  // check for mismatched settings
  if (userOptions.executionPhaseSettings.phases > 1 &&
      userOptions.virtualGraphMode != VirtualGraphMode::ExecutionPhases) {
    throw error(
        "> 1 execution phases requires VirtualGraphMode::ExecutionPhases");
  }

  // if phased execution is enabled
  if (userOptions.virtualGraphMode == VirtualGraphMode::ExecutionPhases &&
      userOptions.executionPhaseSettings.phases > 1) {
    // Currently there are no checks for when phased execution is enabled.
  } else {
    // if phased execution is disabled, make sure all ops execution phases
    // are set to nonstd::nullopt.
    for (auto &id_graph : graphs) {
      auto &graph = id_graph.second;
      for (auto &id_op : graph->getOps()) {
        auto &op = id_op.second;
        op->setExecutionPhase({});
      }
    }
  }

  // Warn user that execution phases are not used if set to 0 or 1
  if ((userOptions.virtualGraphMode == VirtualGraphMode::ExecutionPhases &&
       userOptions.executionPhaseSettings.phases == 0) ||
      userOptions.executionPhaseSettings.phases == 1) {
    logging::ir::warn(
        "Phased execution was enabled but only {} phases were defined. Phased "
        "execution only works with >=2 phases. Disabling.",
        userOptions.executionPhaseSettings.phases);
  }
}

void Ir::verifyAliasZeroCopySettings() const {
  if (userOptions.aliasZeroCopy) {
    if (userOptions.implicitPipeliningEnabled()) {
      throw error("Alias zero copy is not supported with implicit pipelining.");
    }
    if (!userOptions.explicitRecomputation) {
      throw error("Alias zero copy is currently not supported with implicit "
                  "recomputation.");
    }
  }
}

void Ir::verifyExplicitMainLoopsSettings() const {
  if (userOptions.enableExplicitMainLoops && !userOptions.useHostCopyOps) {
    throw error("enableExplicitMainLoops requires useHostCopyOps.");
  }
}

void Ir::verifyOverlapIOSettings() const {
  auto isOverlappingExchangeStrategy = [this](ExchangeStrategy strategy) {
    if (strategy == ExchangeStrategy::OverlapStep) {
      throw error("ExchangeStrategy::OverlapStep is not yet supported.");
    }
    if ((strategy == ExchangeStrategy::OverlapInnerLoop ||
         strategy == ExchangeStrategy::OverlapLoops) &&
        !(getSessionOptions().useHostCopyOps &&
          getSessionOptions().enableExplicitMainLoops &&
          getSessionOptions().virtualGraphMode != VirtualGraphMode::Off)) {
      throw error("ExchangeStrategy::OverlapInnerLoop, "
                  "ExchangeStrategy::OverlapLoops require "
                  "SessionOptions::useHostCopyOps, "
                  "SessionOptions::enableExplicitMainLoops, "
                  "VirtualGraphMode::(Manual, Auto, ExecutionPhases) "
                  "to be enabled.");
    }
  };

  for (auto anchor : getRootAnchors()) {
    auto art = getDataFlow().getAnchorReturnTypeMap().at(anchor);
    isOverlappingExchangeStrategy(art.exchangeStrategy());
  }

  for (auto stream :
       getMainGraph().getTensors().getOfType(TensorType::Stream)) {
    isOverlappingExchangeStrategy(stream->inputSettings.exchangeStrategy());
  }
}

void Ir::verifyBatchSerializationSettings() const {
  if (userOptions.batchSerializationSettings.method ==
          BatchSerializationMethod::Loop &&
      userOptions.batchSerializationSettings.transformContext ==
          BatchSerializationTransformContext::Fwd &&
      isTraining()) {
    throw error(
        "Loop batch serialization is only supported in "
        "BatchSerializationTransformContext::Bwd due to LoopGradOp missing.");
  }
}

void Ir::verifyOpOutputConnectivity(const Graph &graph) const {
  logging::ir::debug("Checking op output tensor producers for graph '{}'",
                     graph.id.str());

  // Check op output tensor producers
  for (auto &op_pair : graph.getOps()) {
    auto &op = op_pair.second;

    for (auto &tensor_pair : op->output->tensorMap()) {
      auto t = tensor_pair.second;

      if (!t->hasProducer()) {
        throw error("Tensor {} should have a producer", t->str());
      }

      if (t->getProducer() != op.get()) {
        throw error(
            "Op {} should produce {}, but it's not the assigned producer",
            op->str(),
            t->str());
      }
    }
  }
}

void Ir::verifyOpInputConnectivity(const Graph &graph) const {
  logging::ir::debug("Checking op input tensor consumers for graph '{}'",
                     graph.id.str());

  // Count the number of times an op consumes its input tensors
  std::map<std::pair<Tensor *, Op *>, int> consumption_count;
  for (auto &op_pair : graph.getOps()) {
    auto &op = op_pair.second;

    for (auto &tensor_pair : op->input->tensorMap()) {
      auto t = tensor_pair.second;

      consumption_count[{t, op.get()}]++;
    }
  }

  // Check that the consumption count matches the value reported by Consumers::n
  for (auto &cons_count : consumption_count) {
    auto tensor = cons_count.first.first;
    auto op     = cons_count.first.second;
    auto count  = cons_count.second;

    if (tensor->consumers.n(op) != count) {
      throw error("Op {} should consume {} {} times, but it "
                  "consumes it {} times",
                  op->str(),
                  tensor->str(),
                  count,
                  tensor->consumers.n(op));
    }
  }
}

void Ir::verifyTensorProducerConnectivity() const {
  logging::ir::debug("Checking tensor producer outputs");

  for (auto &tid : getTensors().getAllTensorIds()) {
    auto tensor = getTensors().get(tid);

    if (tensor->hasProducer() && tensor->tensorType() == TensorType::Stream) {
      auto op = tensor->getProducer();
      throw error("Tensor {} is a stream tensor, but has op {} as a producer",
                  tensor->str(),
                  op->str());
    }

    if (tensor->hasProducer() && tensor->tensorType() == TensorType::Const) {
      auto op = tensor->getProducer();
      throw error("Tensor {} is a const tensor, but has op {} as a producer",
                  tensor->str(),
                  op->str());
    }

    if (tensor->hasProducer() && tensor->tensorType() == TensorType::Variable) {
      auto op = tensor->getProducer();
      if (!dynamic_cast<VarUpdateOp *>(op) && !dynamic_cast<InitOp *>(op)) {
        throw error(
            "Tensor {} is a variable tensor, but has op {} as a producer",
            tensor->str(),
            op->str());
      }
    }

    if (!(tensor->isRootAnchor() || tensor->hasProducer()) &&
        tensor->tensorType() == TensorType::ActGrad) {
      throw error("Tensor {} is an actgrad tensor, but doesn't have a producer",
                  tensor->str());
    }

    // Check that the producer op has the tensor as an output
    if (tensor->hasProducer()) {
      auto op = tensor->getProducer();

      if (op->output == nullptr) {
        throw error("Op {} output tensor index map is null", op->str());
      }

      if (op->output->indices(tensor).empty()) {
        throw error(
            "Tensor {} has op {} as a producer, but it doesn't appear in "
            "the op's outputs",
            tensor->str(),
            op->str());
      }

      if (op->output->indices(tensor).size() > 1) {
        throw error("Tensor {} has op {} as a producer, but it appears in "
                    "the op's outputs {} times",
                    tensor->str(),
                    op->str(),
                    op->output->indices(tensor).size());
      }
    }
  }
}

void Ir::verifyTensorConsumerConnectivity() const {
  logging::ir::debug("Checking tensor consumer inputs");

  // Count the number of times a tensor is consumed by an op
  std::map<std::pair<Tensor *, Op *>, int> consumption_count;
  for (auto &tid : getTensors().getAllTensorIds()) {
    auto tensor = getTensors().get(tid);

    for (auto op : tensor->consumers.getOps()) {
      consumption_count[{tensor, op}] += tensor->consumers.n(op);
    }
  }

  // Check that the consumption count matches the value reported by
  // op->input->indices(tensor).size()
  for (auto &cons_count : consumption_count) {
    auto tensor = cons_count.first.first;
    auto op     = cons_count.first.second;
    auto count  = cons_count.second;

    if (op->input == nullptr) {
      throw error("Op {} input tensor index map is null", op->str());
    }

    if (op->input->indices(tensor).size() != count) {
      throw error("Tensor {} should have op {} as a consumer {} times, but it "
                  "consumes it {} times",
                  tensor->str(),
                  op->str(),
                  op->input->indices(tensor).size(),
                  count);
    }
  }
}

void Ir::verifyConnectivity() const {
  logging::ir::info("Checking IR connectivity");

  for (auto &x : graphs) {
    auto &graph = *x.second.get();
    verifyOpInputConnectivity(graph);
    verifyOpOutputConnectivity(graph);
  }
  verifyTensorProducerConnectivity();
  verifyTensorConsumerConnectivity();

  logging::ir::info("IR connectivity check passed");
}

void Ir::verifyTensorIds() const {
  logging::ir::info("Checking TensorIds are unique");

  // Check that all TensorIds are unique
  std::set<TensorId> seen;

  for (auto &id_graph : graphs) {
    auto graph = id_graph.second.get();
    for (auto &id : graph->getTensors().getAllTensorIds()) {
      if (seen.find(id) != seen.end()) {
        throw error("TensorId '{}' is not unique", id);
      } else {
        seen.insert(id);
      }
    }
  }

  logging::ir::info("TensorId check passed");
}

void Ir::verifyTensorInfos() const {
  logging::ir::info("Checking TensorInfos are valid");
  for (auto tensorIdAndTensor : getAllTensors()) {
    auto tensor = tensorIdAndTensor.second;
    if (tensor->info.getDataTypeInfo() == nullptr ||
        tensor->info.dataType() == DataType::UNDEFINED) {
      throw error("Tensor {} invalid DataType/Info", tensorIdAndTensor.first);
    }
  }
  logging::ir::info("TensorInfo check passed");
}

void Ir::verifyRecomputeAttributes() const noexcept(false) {
  // If explicit recomputation is turned on
  // No op is allowed to have its recompute type set to Recompute
  if (userOptions.explicitRecomputation) {
    for (auto op : getAllOps()) {
      if (op->settings.recomputeType == RecomputeType::Recompute) {
        throw error("Explicit recomputation is turned on for op '{}', but its "
                    "recompute type is set to '{}'",
                    op->debugName(),
                    op->settings.recomputeType);
      }
    }
  }
}

void Ir::verifyReplicatedTensorSharding() const {
  for (const auto &op : getAllOps()) {

    // Subgraph Ops are currently excluded from this check, because they
    // delegate RTS to the Ops within the subgraph.
    if (op->isConvertibleTo<SubgraphOp>() || op->isConvertibleTo<IfOp>()) {
      continue;
    }

    auto rtsIndices = op->getReplicatedTensorShardingIndices();

    for (const auto &input : op->input->tensorMap()) {
      const auto &inInfo = input.second->info;
      if (inInfo.metaShape().size()) {
        if (!std::any_of(rtsIndices.begin(),
                         rtsIndices.end(),
                         [&input](const auto &rtsIndex) {
                           return std::any_of(rtsIndex.first.begin(),
                                              rtsIndex.first.end(),
                                              [&input](const auto &inIndex) {
                                                return inIndex == input.first;
                                              });
                         })) {
          throw internal_error("Op {} encountered on a replicated tensor "
                               "sharding (RTS) path, but the Op does not "
                               "specify that it can consume an RTS tensor "
                               "at InIndex {}",
                               op->debugName(),
                               input.first);
        }
      }
    }

    for (auto &rtsIndex : rtsIndices) {
      for (const InIndex &inIndex : rtsIndex.first) {
        for (const OutIndex &outIndex : rtsIndex.second) {
          if (op->hasInput(inIndex) && op->hasOutput(outIndex)) {
            const auto &inInfo  = op->inInfo(inIndex);
            const auto &outInfo = op->outInfo(outIndex);

            if (inInfo.shape() == outInfo.shape() &&
                inInfo.metaShape().size() &&
                inInfo.metaShape() != outInfo.metaShape()) {

              throw internal_error("Op {} encountered on a replicated tensor "
                                   "sharding (RTS) path, but the "
                                   "tensor shapes ("
                                   "input: {} shape: {} / meta-shape: {} -> "
                                   "output: {} shape: {} / meta-shape: {}"
                                   ") are not handled correctly.",
                                   op->debugName());
            }
          }
        }
      }
    }
  }
}

bool Ir::hasOverlappedIO() const {
  auto isOverlappingExchangeStrategy = [](ExchangeStrategy strategy) {
    return strategy == ExchangeStrategy::OverlapStep ||
           strategy == ExchangeStrategy::OverlapInnerLoop ||
           strategy == ExchangeStrategy::OverlapLoops;
  };

  bool overlap = false;

  for (auto anchor : getRootAnchors()) {
    auto art = getDataFlow().getAnchorReturnTypeMap().at(anchor);
    overlap |= isOverlappingExchangeStrategy(art.exchangeStrategy());
  }

  for (auto stream :
       getMainGraph().getTensors().getOfType(TensorType::Stream)) {
    overlap |=
        isOverlappingExchangeStrategy(stream->inputSettings.exchangeStrategy());
  }

  return overlap;
}

void Ir::verifyDistributedReplicatedGraphSettings() const {
  if (userOptions.enableDistributedReplicatedGraphs) {
    auto localReplicationFactor  = userOptions.replicatedGraphCount;
    auto globalReplicationFactor = userOptions.globalReplicationFactor;
    auto globalReplicaOffset     = userOptions.globalReplicaOffset;
    if (globalReplicationFactor < 1) {
      throw error("Invalid globalReplicationFactor value: {}, must be greater "
                  "or equal than 1",
                  globalReplicationFactor);
    }

    if (globalReplicaOffset < 0) {
      throw error("Invalid globalReplicaOffset value: {}, must be greater or "
                  "equal than 0",
                  globalReplicaOffset);
    }

    if (globalReplicaOffset > globalReplicationFactor) {
      throw error("Global replica offset: {}, is larger than global "
                  "replication factor: {}",
                  globalReplicaOffset,
                  globalReplicationFactor);
    }

    if (userOptions.enableReplicatedGraphs) {
      if (localReplicationFactor == 1) {
        throw error(
            "Local replicated graphs enabled but replication factor is 1");
      }
      if (localReplicationFactor > globalReplicationFactor) {
        throw error("Invalid local replication factor: {}, larger than global "
                    "replication factor: {}",
                    localReplicationFactor,
                    globalReplicationFactor);
      }
    }
  }
}

void Ir::verifyExecutionContexts() const {
  if (getSessionOptions().enableExplicitMainLoops) {
    for (Op *op : getAllOps()) {
      if (op->settings.executionContext ==
          ExecutionContext::AccumulateOuterFragment) {
        throw error("With explicit main loops, no Op should have "
                    "ExecutionContext::AccumulateOuterFragment when the IR is "
                    "finished preparing.");
      }
    }
  }
}

void Ir::verifyPipelineStageAttributes() const {
  if (getSessionOptions().enableExplicitMainLoops) {
    for (Op *op : getAllOps()) {
      if (op->settings.pipelineStage) {
        throw error("With explicit main loops, no Op should have "
                    "pipelineStage attributes when the IR is "
                    "finished preparing (offending Op: {}: {} stage: {}).",
                    op->id,
                    op->debugName(),
                    *(op->settings.pipelineStage));
      }
    }
  }
}

bool Ir::isCandidateForConstExprFolding(const Tensor &tensor) const {
  // A tensor is computable as a const expression if it is Const. This would
  // also be true for Variable tensors during inference, unless the user calls
  // resetHostWeights. Because of this, am choosing to ignore case of Variable
  // tensors during inference.
  auto tt = tensor.tensorType();
  return tt == TensorType::Const;
}

TensorSet Ir::getRootInputsToOp(Op *op) {
  if (opAndRootInputs.find(op->id) != opAndRootInputs.end()) {
    // We have already stored the root inputs for this op
    // in a map. Retrieve here instead of performing search
    return opAndRootInputs.at(op->id);
  } else {
    TensorSet rootInputs;

    // Get input tensors Ids
    std::vector<TensorId> inputIds = getTensors().getNoProducerIds();
    for (Tensor *tensor : op->input->tensors()) {
      if (std::find(inputIds.begin(), inputIds.end(), tensor->id) !=
          inputIds.end()) {
        // Tensor is a root input
        rootInputs.insert(tensor);
      } else {
        for (auto rootInputTensor : getRootInputsToOp(tensor->getProducer())) {
          rootInputs.insert(rootInputTensor);
        }
      }
    }

    // Add what we've found to the IR's map to speed up
    // future searches
    opAndRootInputs.emplace(op->id, rootInputs);

    return rootInputs;
  }
}

// Verify ConstExpr folding has removed input tensors that should have
// been removed:
//  - that initializer inputs are removed when possible in
//    inference mode
//  - that constant inputs are removed when possible in all modes
//
// 1. Get only the tensors we care about checking
// 2. For each tensor, get consumers
// 3. For each consumer, find its root input tensors
// 4. Confirm that at least on root input is not a candidate for
//    ConstExpr folding
//
// Note: this doesn't check that ConstExpr folding has removed
// tenosors that it shouldn't have
void Ir::verifyConstExprFolding() {
  for (auto id : getTensors().getNoProducerIds()) {
    Tensor *tensor = getTensors().get(id);

    // 1
    if (!isCandidateForConstExprFolding(*tensor)) {
      continue;
    }

    // 2 & 3
    TensorSet rootInputs;
    for (auto consumingOp : tensor->consumers.getOps()) {
      for (auto rootInput : getRootInputsToOp(consumingOp)) {
        rootInputs.insert(rootInput);
      }
    }

    // 4
    bool shouldHaveFoldedTensor = true;
    for (auto rootInput : rootInputs) {
      if (!isCandidateForConstExprFolding(*rootInput)) {
        shouldHaveFoldedTensor = false;
      }
    }
    if (shouldHaveFoldedTensor) {
      logging::ir::info(
          "ConstExpr folding has failed to remove input tensor {}, even though "
          "none of the root inputs to its consumers are variable tensors",
          tensor->id);
    }
  }
}

void Ir::prepareCache(const HashesMap &cacheEntries, size_t hashSeed) {
  if (getDeviceInfo() == nullptr) {
    throw("Device info must be set before calling prepareCache.");
  }

  computeHash(hashSeed);

  compareWithSavedHash(cacheEntries);
  if (hashMatched()) {
    logging::ir::info("Ir hash matched cached value. Skipping Ir preparation");
    setIsPrepared();
    return;
  }
}

void Ir::prepare(const IrBundle &gb,
                 const HashesMap &cacheEntries,
                 size_t hashSeed) {
  auto tryDumpIr = [&](auto logLevel) {
    auto irDumpDest = getPopartEnvVar("IR_DUMP");
    if (irDumpDest) {
      logging::log(logging::Module::ir,
                   logLevel,
                   logging::format("Writing ir to {}", *irDumpDest));
      std::ofstream ofs;
      ofs.open(*irDumpDest, std::ofstream::out);
      if (ofs.is_open()) {
        std::stringstream ss;
        serialise(Ir::SerialiseFormat::JSON, ss, false);
        ofs << ss.str();
      } else {
        logging::ir::err("Failed to open file {} to dump ir.", *irDumpDest);
      }
    }
  };

  try {
    prepareImpl(gb, cacheEntries, hashSeed);
  } catch (...) {
    tryDumpIr(logging::Level::Err);
    throw;
  }
  tryDumpIr(logging::Level::Debug);
}

void Ir::prepareImpl(const IrBundle &gb,
                     const HashesMap &cacheEntries,
                     size_t hashSeed) {
  setDeviceInfo(gb.deviceInfo);

  if (isPrepared()) {
    throw error("Ir::prepare called more than once");
  }

  if (gb.optimizer) {
    setExecutionMode(ExecutionMode::Training);
  } else {
    setExecutionMode(ExecutionMode::Inference);
  }

  setDataFlow(gb.dataFlow);
  setInputShapeInfo(gb.inputShapeInfo);
  setUserOptions(gb.userOptions);
  setPatterns(gb.patterns);
  setOnnxModel(gb.modelProto);
  setSessionName(gb.sessionName);

  if (graphs.size() == 1) {
    if (isPrepared()) {
      throw error("There is more than one graph at the loss insertion stage, "
                  "which should not happen. This is an internal error.");
    }
  }

  if (canTrain()) {
    getMainGraph().setLoss(gb.loss);
  }

  confirmNoReservedIds();

  registerInputTensors();

  if (!canTrain() && getSessionOptions().enableGradientAccumulation) {
    throw error("Gradient Accumulation only available when training.");
  }

  logging::ir::info("Patterns : {}", patterns);
  // todo : validate the selected patterns

  // construct the forward pass from ONNX,
  constructForwards();

  // Check if cached Ir hash matches the current one and skip
  // the rest of the Ir preparation if true.
  setIrBundleHash(std::hash<popart::IrBundle>()(gb));

  computeHash(hashSeed);
  compareWithSavedHash(cacheEntries);
  if (hashMatched()) {
    logging::ir::info("Ir hash matched cached value. Skipping Ir preparation");
    if (gb.optimizer) {
      optimizer = gb.optimizer->clone();
      optimizer->setFactorsFromOptions(getSessionOptions());
    }
    setIsPrepared();
    return;
  }

  if (!virtualGraphsEnabled()) {
    unsetAllVirtualGraphIds();
  }

  // Check virtual graph settings and annotations are consistent
  verifyVirtualGraphIds(false);
  verifyPipelineSettings();
  verifyExecutionPhaseSettings();
  verifyDistributedReplicatedGraphSettings();
  verifyAliasZeroCopySettings();
  verifyExplicitMainLoopsSettings();
  verifyOverlapIOSettings();

  dotCheckpoint(*this, "Fwd0");

  CustomTransformApplier customTransformApplier(*this);
  customTransformApplier.applyCustomTransforms("Fwd0");

  for (auto &id_graph : graphs) {
    auto &graph = getGraph(id_graph.first);
    applyPreAliasPatterns(graph);
  }
  dotCheckpoint(*this, "Fwd1");

  customTransformApplier.applyCustomTransforms("Fwd1");

  if (RandomSetup::requiresRandomSeed(*this)) {
    setRequiresRandomSeed();
  }

  if (getSessionOptions().automaticLossScalingSettings.enabled &&
      getSessionOptions()
          .automaticLossScalingSettings.toTrackTensors.has_value()) {
    applyTransform(PreAutomaticLossScale::id(), getMainGraph());
  }

  applyTransform(RandomSetup::id(), getMainGraph());

  enableTransform(AutoVirtualGraph::id(),
                  userOptions.virtualGraphMode == VirtualGraphMode::Auto);
  applyTransform(AutoVirtualGraph::id(), getMainGraph());

  // Required transform order for StreamingMemory is:
  // FWD -> StreamingMemory1 -> BWD -> IpuCopy -> StreamingMemory2 ->
  // Outline -> RemoteSetup

  if (getSessionOptions().enablePipelining) {
    applyTransform(InferPipelineStages::id(), getMainGraph());
  }

  if (canTrain()) {
    setFinalLoss(gb.loss);
    updateVertices();
  }

  // First streaming memory transformation pass (fwd)
  applyTransform(StreamingMemory::id(1), getMainGraph());
  if (userOptions.virtualGraphMode == VirtualGraphMode::ExecutionPhases &&
      userOptions.executionPhaseSettings.phases > 1) {
    verifyVirtualGraphIds(true);
  }

  // Batch serialisation, step 1
  // (has to occur after setFinalLoss)
  if (userOptions.batchSerializationSettings.factor > 1 &&
      userOptions.batchSerializationSettings.transformContext ==
          BatchSerializationTransformContext::Fwd) {
    applyTransform(BatchSerialize::id(1), getMainGraph());
    removeIsolatedTensors(true);
    updateVertices();
  }

  if (autoRecomputationEnabled() && getMainGraph().hasUserRecomputeOps() &&
      getSessionOptions().executionPhaseSettings.phases < 2) {
    throw error("A mixture of auto and manual recomputation is not supported");
  }

  // tensors with no producer and no consumers are removed
  // at this point. We may want something more subtle.
  // (For streaming memory, the subtle thing here is to not remove
  // cached tensors, even though they are not consumed by IR ops)
  removeIsolatedTensors(true);

  if (gb.optimizer) {
    setOptimizer(*gb.optimizer);
  }

  updateVertices();
  if (canTrain()) {
    constructBackwards();
    verifyPipelineSettings();
  }

  updateVertices();
  dotCheckpoint(*this, "Bwd0");

  customTransformApplier.applyCustomTransforms("Bwd0");

  // Delaying this preserves all "compute" tensor names a user might want
  // to anchor, so it should be called after the transforms relevant for the
  // computational functionality of the graph are done
  if (getSessionOptions().useHostCopyOps) {
    // Add input HostLoad operations
    applyTransform(HostIOSetup::id(1), getMainGraph());
  }

  applyTransform(Prune::id(), getMainGraph());

  for (auto &id_graph : graphs) {
    auto &graph = getGraph(id_graph.first);
    applyPreAliasPatterns(graph);
  }

  // tensors with no producer and no
  // consumers are removed at this point.
  removeIsolatedTensors(true);
  updateVertices();

  if (getSessionOptions().explicitRecomputation) {
    if (autoRecomputationEnabled() &&
        getSessionOptions().executionPhaseSettings.phases < 2) {
      logging::transform::info("Auto-annotating Ops for recomputation");
      recompute::autoAnnotate(getMainGraph(),
                              getSessionOptions().autoRecomputation);
    }
    // Transform from implicit to explicit recomputation
    applyTransform(ExplicitRecompute::id(), getMainGraph());
    updateVertices();
  }

  // Convert the fp16 loss scale tensor to fp32. This relies on assumptions of
  // the ability of the Opx implementations for the consumers of the loss scale
  // tensor to handle mixed-precision inputs. Loss scale being represented in
  // fp32 is a requirement for using automatic loss scaling.
  if (getSessionOptions().ensureFp32LossScaleTensor ||
      getSessionOptions().automaticLossScalingSettings.enabled) {
    applyTransform(EnsureFp32LossScale::id(), getMainGraph());
  }

  // Dynamicoptransform decomposes grad sums that contain
  // DynamicAdd/DynamicUpdate gradients, which can be decomposed efficiently
  applyTransform(DynamicOpTransform::id(), getMainGraph());

  // DecomposeGradSum decomposes remaining grad sums
  if ((getSessionOptions().batchSerializationSettings.factor <= 1 &&
       getSessionOptions().decomposeGradSum) ||
      (getSessionOptions().batchSerializationSettings.factor > 1 &&
       getSessionOptions().batchSerializationSettings.transformContext ==
           BatchSerializationTransformContext::Fwd)) {
    applyTransform(DecomposeGradSum::id(), getMainGraph());
  }

  switch (userOptions.mergeVarUpdate) {

  case (MergeVarUpdateType::All): {
    enableTransform(MergeAllVarUpdates::id(), true);
    applyTransform(MergeAllVarUpdates::id(), getMainGraph());
    updateVertices();
    break;
  }
  case (MergeVarUpdateType::AutoTight): {
    enableTransform(MergeTightThreshold::id(), true);
    applyTransform(MergeTightThreshold::id(), getMainGraph());
    updateVertices();
    break;
  }
  case (MergeVarUpdateType::AutoLoose): {
    enableTransform(MergeLooseThreshold::id(), true);
    applyTransform(MergeLooseThreshold::id(), getMainGraph());
    updateVertices();
    break;
  }

  case (MergeVarUpdateType::None): {
    // do nothing
    break;
  }

  case (MergeVarUpdateType::N):
  default: {
    // should never occur
    throw error("Unrecognised MergeVarUpdateType, bailing from merger");
  }
  }

  updateVertices();

  // we now start applying topological constraints between
  // Ops directly.
  if (canTrain()) {
    // 1. Ensure that the VarUpdate Ops are the final consumers
    //    of the Variable tensors
    getMainGraph().setVarUpdateConstraints();

    // 2. Ensure that ConvFlipWeights ops produce the transposed
    //    variable tensors only just before they are needed
    getMainGraph().setConvFlipWeightConstraints();
  }

  applyTransform(Prune::id(), getMainGraph());
  updateVertices();

  // Make sure that matmuls are serialized before gradient accumulation
  if (getSessionOptions().enableSerializedMatmuls) {
    applyTransform(SerializeMatMuls::id(), getMainGraph());
    // SerializeMatMuls could have changed aspects of aliasing
    updateVertices();
  }

  if (getSessionOptions().automaticLossScalingSettings.enabled) {
    applyTransform(AutomaticLossScale::id(), getMainGraph());
  }

  // Accumulator Tensor for gradient accumulation / momentum is added here
  SGD0Decompose sgd0Decomposer;
  applyPreAliasPattern(&sgd0Decomposer, getMainGraph());
  SGD1Decompose sgd1Decomposer;
  applyPreAliasPattern(&sgd1Decomposer, getMainGraph());
  SGD2Decompose sgd2Decomposer;
  applyPreAliasPattern(&sgd2Decomposer, getMainGraph());
  AdamDecompose adamDecomposer;
  applyPreAliasPattern(&adamDecomposer, getMainGraph());
  AdaptiveDecompose adaptiveDecomposer;
  applyPreAliasPattern(&adaptiveDecomposer, getMainGraph());
  if (canTrain()) {
    getMainGraph().setVarUpdateConstraints();
  }
  decomposedOptimizers = true;

  for (auto &id_graph : graphs) {
    auto &graph = getGraph(id_graph.first);
    // Add internal ops to copy tensors between ipu's as needed
    applyTransform(InterIpuCopy::id(), graph);
  }

  // Pipelining optimizes copies separately, so only run if this is disabled
  if (!getSessionOptions().enablePipelining) {
    applyTransform(MergeCopies::id(), getMainGraph());
  }

  updateVertices();

  // Touches optimizers which might later go through replicated tensor sharding
  // and streaming memory, and therefore needs to be applied before
  // StreamingMemory::id(2)
  if (optimizer && optimizer->getClipNormSettings().size() > 0) {
    applyTransform(ClipWeightGradientsByNorm::id(), getMainGraph());
    updateVertices();
  }

  // Second streaming memory transformation pass (cut)
  // Streaming memory transformation 2 needs up-to-date aliasing information
  applyTransform(StreamingMemory::id(2), getMainGraph());
  // Remove extra RemoteLoad, RemoteStore and Replicated ops that are not used
  applyTransform(Prune::id(), getMainGraph());
  updateVertices();
  // Check all Ops implement RTS correctly
  verifyReplicatedTensorSharding();

  if (canTrain()) {
    getMainGraph().setVarUpdateConstraints();
  }
  if (userOptions.virtualGraphMode == VirtualGraphMode::ExecutionPhases &&
      userOptions.executionPhaseSettings.phases > 1) {
    verifyVirtualGraphIds(true);
  }

  updateVertices();

  for (auto &id_graph : graphs) {
    auto &graph = getGraph(id_graph.first);
    applyTransform(IoComputeTileCopy::id(), graph);
    updateVertices();
  }

  // Optimizer accumulate outer fragment.
  if (userOptions.accumulateOuterFragmentSettings.schedule ==
          AccumulateOuterFragmentSchedule::OverlapCycleOptimized ||
      userOptions.accumulateOuterFragmentSettings.schedule ==
          AccumulateOuterFragmentSchedule::OverlapMemoryOptimized) {
    applyTransform(AccumulateOuterFragmentParallelizer::id(), getMainGraph());
  }

  for (auto &id_graph : graphs) {
    auto &graph = getGraph(id_graph.first);
    applyPreAliasPatterns(graph);
  }

  updateVertices();

  // Batch serialisation, step 2 (needs IoTileCopy ops to have been inserted)
  if (userOptions.batchSerializationSettings.factor > 1) {
    if (userOptions.batchSerializationSettings.transformContext ==
        BatchSerializationTransformContext::Bwd) {
      applyTransform(BatchSerialize::id(1), getMainGraph());
      // DecomposeGradSum decomposes remaining grad sums
      applyTransform(DecomposeGradSum::id(), getMainGraph());
      applyTransform(Prune::id(), getMainGraph());
      removeIsolatedTensors(true);
    }
    applyTransform(BatchSerialize::id(2), getMainGraph());
    updateVertices();
  }

  // Must be called after optimiser decomposition and decomposegradsum.
  // Must be called before outlining.
  applyTransform(InplaceAccumulateGradPartialsIntoOptimizerAccumTensor::id(),
                 getMainGraph());

  if (userOptions.replicatedCollectivesSettings
          .prepareScheduleForMergingCollectives) {
    applyTransform(ContiguateCollectivesTransform::id(), getMainGraph());
    updateVertices();
  }
  if (userOptions.replicatedCollectivesSettings.mergeAllReduceCollectives ||
      userOptions.replicatedCollectivesSettings.mergeReduceScatterCollectives ||
      userOptions.replicatedCollectivesSettings.mergeAllGatherCollectives) {
    applyTransform(MergeCollectivesTransform::id(), getMainGraph());
    updateVertices();
  }

  dotCheckpoint(*this, "PreAlias");

  customTransformApplier.applyCustomTransforms("PreAlias");

  if (getSessionOptions().enableExplicitMainLoops) {
    // Add explicit training loops
    applyTransform(MainLoops::id(), getMainGraph());
    removeIsolatedTensors(true);
    dotCheckpoint(*this, "MainLoops");
    customTransformApplier.applyCustomTransforms("MainLoops");
  }

  if (getSessionOptions().useHostCopyOps) {
    // Add anchor HostStore operations
    applyTransform(HostIOSetup::id(2), getMainGraph());
    updateVertices();
  }

  // Repeat IoComputeTileCopy to also insert IO tile copies before e.g.
  // HostStore ops
  for (auto &id_graph : graphs) {
    auto &graph = getGraph(id_graph.first);
    applyTransform(IoComputeTileCopy::id(), graph);
  }

  if (autoRecomputationEnabled() && !getSessionOptions().enablePipelining &&
      !getSessionOptions().explicitRecomputation &&
      getSessionOptions().executionPhaseSettings.phases < 2) {
    updateVertices();
    logging::transform::info("Auto-annotating Ops for recomputation");
    recompute::autoAnnotate(getMainGraph(),
                            getSessionOptions().autoRecomputation);
  }

  updateVertices();

  // Each virtual graph is a pipeline stage in the pipeline.
  // Transform the graph to cache forward-pass tensors, and
  // restore them when needed in the backwards pass, allowing
  // for greater parallelism during compute.
  if (getSessionOptions().enablePipelining) {
    if (getSessionOptions().explicitPipeliningEnabled()) {
      applyTransform(Pipeline::id(), (MainLoops::getInnerLoopSubgraph(*this)));
    } else {
      applyTransform(Pipeline::id(), getMainGraph());
    }
    updateVertices();
  }

  if (getSessionOptions().enableExplicitMainLoops &&
      getSessionOptions().useHostCopyOps) {
    applyTransform(OverlapIO::id(), getMainGraph());
    updateVertices();
  }

  // Merge remote loads/stores into exchanges
  if (getSessionOptions().enableMergeExchange) {
    for (auto &id_graph : graphs) {
      applyTransform(MergeExchange::id(), *id_graph.second);
    }
  }

  if (getSessionOptions().enableOutlining) {
    if (getSessionOptions().batchSerializationSettings.factor <= 1) {
      // This pattern attempts to remove aliasing chains that outlining
      // is prone to break up causing outplace copies where it is not
      // required.
      ViewSimplifyPattern viewSimplifier;
      applyPreAliasPattern(&viewSimplifier, getMainGraph());
    }

    applyTransform(SubgraphOutline::id(), getMainGraph());
    updateVertices();

    if (getSessionOptions().batchSerializationSettings.factor > 1) {
      // Run a second outlining step.
      // This is necessary because in the first outlining pass we help the
      // outlining algorithm by inserting boundaries between
      // batch serialization phases.
      // Because batch serialization phases are not copied from the ops to their
      // parent subgraph, the second pass will ignore batch serialization phases
      // and outline the repeated per-batch-element subgraphs/ops.
      applyTransform(SubgraphOutline::id(), getMainGraph());
      updateVertices();
    }
  }

  if (getSessionOptions().implicitPipeliningEnabled() &&
      getSessionOptions().autoRecomputation == RecomputationType::Pipeline) {
    // Mechanism only relevant for implicit pipelining, explicit recomputation
    // has separate mechanism dealing with this
    const auto scopedStopwatch =
        timePartitionLogger().scopedStopwatch("setFinalFwdStageRecomputation");
    Pipeline::setFinalFwdStageRecomputation(getMainGraph());
  }

  removeIsolatedTensors(true);
  updateVertices();

  applyTransform(MergeDuplicateOps::id(), getMainGraph());

  // Now, we apply the Patterns which can handle and create
  // topological constraints. Currently, this is only one
  // in-placing Pattern.
  if (patterns.isInPlaceEnabled()) {

    const auto scopedStopwatch =
        timePartitionLogger().scopedStopwatch("Inplacing (Ir)");

    // Update the inplace priorities of ops before inplacing
    if (patterns.isUpdateInplacePrioritiesForIpuEnabled()) {
      applyUpdateInplacePrioritiesForIpu();
    }
    for (auto &id_graph : graphs) {
      logging::ir::debug("Applying Inplace Pattern to Graph \"{}\"",
                         id_graph.first);
      applyInplacePattern(*id_graph.second);
    }
    updateVertices();
  }

  applyTransform(RemoteSetup::id(), getMainGraph());

  if (getSessionOptions().enableStochasticRounding) {
    applyTransform(StochasticRounding::id(), getMainGraph());
  }

  removeIsolatedTensors(true);

  // confirm that all the anchor names provided
  // are indeed real tensor names. This is a check
  // that the user has not provided incorrect names.
  // We allow duplicates.
  validateAnchors();

  dotCheckpoint(*this, "Final");
  customTransformApplier.applyCustomTransforms("Final");
  logIr();

  finalizeOpDebugInfo();

  // some checks, now that prepare is complete
  for (auto &id_op : getMainGraph().getOps()) {
    if (id_op.second->opid == Onnx::CustomGradOperators::NllGrad) {
      logging::ir::info("Computing gradient of the probabilities to Nll "
                        "might be less efficient than computing "
                        "pre-probability gradients directly with Pattern "
                        "SoftMaxGradDirect");
    }
  }

  addAdditionalModelProtoTensors();
  {

    auto scopedTimer = timePartitionLogger().scopedStopwatch("Verifying Ir");

    verifyConstExprFolding();
    verifyConnectivity();
    verifyTensorIds();
    verifyVirtualGraphIds(true);
    verifyRecomputeAttributes();
    verifyExecutionContexts();
    verifyPipelineStageAttributes();
    verifyReplicatedTensorSharding();

    StochasticRoundingAssumptionVerifier srVerifier{*this};
    srVerifier.verify();
  }
  // end of checks

  setIsPrepared();

  logging::ir::info(
      std::string(
          "\nIr preparation complete. Breakdown of compile time so far:\n") +
      timePartitionLoggerStr());
}

void Ir::setIsPrepared() {
  if (isPrepared_) {
    logging::warn("[Ir::setIsPrepared] setIsPrepared was already called. It "
                  "should only be called once.");
  }

  // Collect all tensors
  std::set<Tensor *, PTensorCmp> allTensors;
  for (auto &graph : getAllGraphs()) {
    auto &curGraph = getGraph(graph->id);
    curGraph.finalizeSchedule();
    auto tensors = curGraph.getTensors().getAll();
    std::copy(tensors.begin(),
              tensors.end(),
              std::inserter(allTensors, allTensors.end()));
  }

  // Set preparedVGraphIdAndTileSet for all tensors
  for (auto &tensor : allTensors) {
    tensor->setPreparedVGraphIdAndTileSet();
  }

  isPrepared_ = true;
}

void Ir::addAdditionalModelProtoTensors() {
  if (!additionalModelProtoTensors.empty() && !hasOnnxModel()) {
    throw error(
        "Ir::addAdditionalModelProtoTensors: There are additional model proto "
        "tensors, but the Ir has no Onnx model to add them to.");
  }

  ONNX_NAMESPACE::GraphProto *onnxGraph =
      hasOnnxModel() ? onnxModel->mutable_graph() : nullptr;

  for (const Tensor *tensor : additionalModelProtoTensors) {
    const std::string &tId = tensor->id;
    // For additional tensors we want to save in the onnx modelproto, we copy
    // their info into across to the proto.
    if (onnxutil::isInitializer(*onnxModel, tId)) {
      throw error("Tensor id {} already in initializers, duplicate tensor "
                  "Ids not allowed in onnx specification.",
                  tId);
    } else {
      ONNX_NAMESPACE::TensorProto *init = onnxGraph->add_initializer();
      init->set_name(tId);

      ConstVoidData cvData;
      cvData.data = tensor->tensorData()->data();
      cvData.info = tensor->info;
      BuilderImpl::populateTensorProtoFromConstVoidData(cvData, tId, init);

      // If optimizer state tensor, and its corresponding initializer is saved
      // externally, then save the this tensor to the same external location
      if (tensor->isOptimizerStateTensor()) {
        // Get corresponding initializer from optimizer state TensorId
        TensorId initializerId = tId;
        for (auto prefix : reservedOptimizerStatePrefixes()) {
          size_t pos = initializerId.find(prefix);
          if (pos != std::string::npos) {
            initializerId.erase(pos, prefix.length());
            break;
          }
        }
        if (!onnxutil::isInitializer(*onnxModel, initializerId)) {
          // No candidate path to save tensor data externally
          continue;
        } else if (onnxutil::isExternallySavedInitializer(*onnxModel,
                                                          initializerId)) {
          std::string fn = onnxutil::getExternallySavedTensorLocation(
              *onnxModel, initializerId);
          logging::ir::debug(
              "Saving additional optimizer state tensor data for tensor '{}' "
              "alongside corresponidng initializer '{}' in file '{}'",
              tId,
              initializerId,
              fn);
          onnxutil::saveInitializersExternally(*onnxModel, {tId}, fn, true);
        }
      }
    }
  }
  additionalModelProtoTensorsAdded = true;
}

void Ir::addAdditionalModelProtoTensor(const TensorId &id) {
  auto tensor = getMainGraph().getTensors().get(id);
  addAdditionalModelProtoTensor(tensor);
}

void Ir::addAdditionalModelProtoTensor(Tensor *tensor) {
  if (additionalModelProtoTensors.find(tensor) ==
          additionalModelProtoTensors.end() &&
      !tensorExistsInInitialisers(tensor->id)) {
    // If we are not going to stream the tensors from the host,
    // don't add them to the set of additional tensors to be saved
    // in the onnx modelproto
    if (!storingIsDisabledForTensor(tensor)) {
      additionalModelProtoTensors.insert(tensor);
    }
  }
}

void Ir::verifyVirtualGraphIds(bool postAutoVirtualGraphTransform) const {
  if (!virtualGraphsEnabled()) {
    verifyVirtualGraphIdsNotInitialized();
    return;
  }

  logging::ir::debug("Verifying virtual graph id consistency");

  std::set<int64_t> vGraphs;
  std::map<int64_t, int> vGraphCounts;

  for (auto graph : getAllGraphs()) {
    auto gvGraphs = graph->getAllVirtualGraphIds(true);
    vGraphs.insert(gvGraphs.begin(), gvGraphs.end());
    auto gvGraphCounts = graph->getVirtualGraphCounts();
    for (auto gvGrapCount : gvGraphCounts) {
      vGraphCounts[gvGrapCount.first] += gvGrapCount.second;
    }
  }

  // a mix of annotated and not annotated Ops : suggests a problem
  if (vGraphs.count(Graph::NoVGraph) != 0 && vGraphs.size() > 1) {

    std::ostringstream errm;
    errm << "Either all Ops must have their virtual "
         << "graph ids set, or none must. Op count per virtual graph id\n";

    for (auto &vgidAndSize : vGraphCounts) {
      errm << "  " << vgidAndSize.first << " : " << vgidAndSize.second << "\n";
    }

    errm << "Ops with no virtual graph id :  \n";
    for (auto &op : getAllOps()) {
      if (!op->isConvertibleTo<IpuCopyOp>() &&
          (!op->hasVirtualGraphId() ||
           op->getVirtualGraphId() == unusedVGraphId)) {
        errm << "  " << op->str() << "\n";
      }
    }

    throw error(errm.str());
  }

  // Sanity check the virtual graph ids. Only -1's, no Op has a virtual graph
  // annotation implies a problem.
  if (vGraphs.size() == 1 && vGraphs.count(-1) != 0) {
    // Manual virtual graphing, the user should have annotated ops.
    if (getSessionOptions().virtualGraphMode == VirtualGraphMode::Manual) {
      throw error("SessionOptions flag virtualGraphMode is {}, but no Ops "
                  "have been annotated with virtual graph information. This "
                  "is an inconsistent combination. ",
                  getSessionOptions().virtualGraphMode);
    }
    // Auto virtual graphing, why has the auto-sharder not run?
    else if (postAutoVirtualGraphTransform) {
      throw error("SessionOptions flag virtualGraphMode is {}, but no Ops have "
                  "been "
                  "annotated with virtual graph information. Moreover, the "
                  "paramater "
                  "postAutoVirtualGraphTransform is true, so AutoVirtualGraph "
                  "should have been run. This is an inconsistent combination, "
                  "possibly an internal logic error has occurred",
                  getSessionOptions().virtualGraphMode);
    }
  }
}

void Ir::verifyVirtualGraphIdsNotInitialized() const {
  for (auto &id_graph : graphs) {
    auto &graph = id_graph.second;
    for (auto &id_op : graph->getOps()) {
      auto op = id_op.second.get();
      if (op->hasVirtualGraphId()) {
        std::ostringstream errm;
        errm << "SessionOptions flag virtualGraphMode is ";
        errm << getSessionOptions().virtualGraphMode;
        errm << ", but at least one op (";
        errm << op->debugName();
        errm << ") has virtualGraphId set.";

        throw error(errm.str());
      }
    }
  }
}

std::vector<TensorId> Ir::getModelInputIds() const {
  if (!hasOnnxModel()) {
    return {};
  }

  const auto &onnxGraph = onnxModel->graph();
  std::vector<TensorId> modelProtoInputIds;
  modelProtoInputIds.reserve(onnxGraph.input_size());
  for (const auto &valueInfo : onnxGraph.input()) {
    modelProtoInputIds.push_back(valueInfo.name());
  }
  return modelProtoInputIds;
}

namespace {

void checkForDimParams(const TensorId &id, const ONNX_NAMESPACE::TypeProto &t) {
  auto dimString = [&]() {
    std::stringstream ss;
    ss << "[";
    int element_counter = 0;
    for (auto &v : t.tensor_type().shape().dim()) {
      if (element_counter > 0) {
        ss << ", ";
      }

      if (v.has_dim_param()) {
        ss << v.dim_param();
      } else {
        ss << v.dim_value();
      }
      element_counter += 1;
    }
    ss << "]";

    return ss.str();
  };

  for (auto &v : t.tensor_type().shape().dim()) {
    if (v.has_dim_param()) {
      throw error("Input tensor '{}' must be specified in InputShapeInfo, as "
                  "it has shape {}, which uses an unknown value '{}'.",
                  id,
                  dimString(),
                  v.dim_param());
    } else if (v.dim_value() < 0) {
      throw error("Input tensor '{}' must be specified in InputShapeInfo, as "
                  "it has shape {}, which uses an unknown value '{}'.",
                  id,
                  dimString(),
                  std::to_string(v.dim_value()));
    }
  }
}

template <typename T, typename U = T>
static void generateUniformIntDist(std::vector<char> &data, int64_t nelms) {
  constexpr T int_min = std::numeric_limits<T>::lowest();
  constexpr T int_max = std::numeric_limits<T>::max();

  std::mt19937 generator;
  boost::random::uniform_int_distribution<U> uniformDistribution(int_min,
                                                                 int_max);
  while (nelms--) {
    const T val = uniformDistribution(generator);
    std::vector<char> converted_data;
    converted_data.resize(sizeof(T));
    *reinterpret_cast<T *>(converted_data.data()) = val;
    data.insert(data.end(), converted_data.begin(), converted_data.end());
  }
}

static void generateSyntheticUniformData(std::vector<char> &data,
                                         const popart::TensorInfo &info) {
  const DataType dtype = info.dataType();
  auto nelms           = info.nelms();

  switch (dtype) {
  case DataType::FLOAT16:
  case DataType::FLOAT: {
    float min, max;
    if (dtype == DataType::FLOAT16) {
      max = 65504.0f;
      min = -max;
    } else {
      min = std::numeric_limits<float>::lowest();
      max = std::numeric_limits<float>::max();
    }
    std::mt19937 generator;
    boost::random::uniform_real_distribution<float> uniformDistribution(min,
                                                                        max);

    while (nelms--) {
      const auto val           = uniformDistribution(generator);
      const auto convertedData = convertFloatToDataType(dtype, val);
      data.insert(data.end(), convertedData.begin(), convertedData.end());
    }
    break;
  }
  case DataType::INT32:
    generateUniformIntDist<int32_t>(data, nelms);
    break;
  case DataType::INT16:
    generateUniformIntDist<int16_t>(data, nelms);
    break;
  case DataType::INT8:
    generateUniformIntDist<int8_t>(data, nelms);
    break;
  case DataType::UINT32:
    generateUniformIntDist<uint32_t>(data, nelms);
    break;
  case DataType::UINT16:
    generateUniformIntDist<uint16_t>(data, nelms);
    break;
  case DataType::UINT8:
    generateUniformIntDist<uint8_t>(data, nelms);
    break;
  case DataType::BOOL:
    generateUniformIntDist<bool, int>(data, nelms);
    break;
  default:
    throw error("Can't generate synthetic data for DataType {}",
                getDataTypeInfoMap().at(dtype).name());
  }
}

} // namespace

void Ir::registerInputTensors() {

  if (!hasOnnxModel()) {
    throw error("Ir::registerInputTensors: Ir has no Onnx model.");
  }

  auto &onnxGraph = onnxModel->graph();

  // Log the input tensor names, catch the
  // invalid case where they are repeated
  std::stringstream ss;
  std::set<TensorId> inputIds;
  bool repeatedInput = false;
  TensorId repeater  = "";
  ss << "Registering Input Tensors. ONNX Graph Inputs : [ ";
  for (auto &valueInfo : onnxGraph.input()) {
    TensorId id = valueInfo.name();
    ss << id << " ";
    if (inputIds.count(id) != 0) {
      // already seen, this is not valid. Will throw an error below.
      repeatedInput = true;
      repeater      = id;
    }
    inputIds.insert(id);
  }
  ss << "]";
  logging::debug(ss.str());
  if (repeatedInput) {
    throw error("Invalid ONNX Model : repeated name: ({}) in input list",
                repeater);
  }
  // we create a map of the tensors to their consumers' types
  std::map<TensorId, std::vector<std::string>> consumerTypes;
  auto addConsumerType = [&](const TensorId &tenId, const Node &node, int i) {
    auto found      = consumerTypes.find(tenId);
    auto consumerId = logging::format("{}@{}", node.op_type(), i);
    if (found == consumerTypes.end()) {
      consumerTypes[tenId] = {consumerId};
    } else {
      found->second.push_back(consumerId);
    }
  };

  std::function<void(const Attributes::Graph &)> addGraphNode =
      [&](const Attributes::Graph &graph) {
        // populate consumerTypes
        for (auto &node : graph.node()) {
          logging::ir::trace(
              "[addGraphNode] Node: {} {}", node.op_type(), node.name());
          for (int i = 0; i < node.input_size(); ++i) {
            addConsumerType(node.input(i), node, i);
          }

          // need to look at the subgraph inputs for If, Call, Loop, Scan nodes
          auto addSubgraphInputs = [&](std::string branchName,
                                       Attributes attr) {
            auto branch = attr.getAttribute<Attributes::Graph>(branchName);
            for (int i = 0; i < branch.input_size(); i++) {
              auto inputId = branch.input(i).name();
              addConsumerType(inputId, node, i);
            }

            // need to look at the subgraph consumers of parent scope tensors
            addGraphNode(branch);
          };
          if (node.op_type() == Onnx::AiOnnx::OpSet9::If.type) {
            Attributes attr{node.attribute()};
            addSubgraphInputs("then_branch", attr);
            addSubgraphInputs("else_branch", attr);
          }
          if (node.op_type() == Onnx::AiGraphcore::OpSet1::Call.type) {
            Attributes attr{node.attribute()};
            addSubgraphInputs("callee", attr);
          }
          if (node.op_type() == Onnx::AiOnnx::OpSet9::Loop.type ||
              node.op_type() == Onnx::AiOnnx::OpSet9::Scan.type ||
              node.op_type() == Onnx::AiOnnx::OpSet11::Loop.type ||
              node.op_type() == Onnx::AiOnnx::OpSet11::Scan.type) {
            Attributes attr{node.attribute()};
            addSubgraphInputs("body", attr);
          }
        }
      };
  addGraphNode(onnxGraph);

  auto logCreationInfo = [&consumerTypes](std::string tensor_type,
                                          TensorId tensorId) {
    std::string consumerString = "";
    auto found                 = consumerTypes.find(tensorId);

    if (found == consumerTypes.end()) {
      consumerString = "with no consumers in the ONNX GraphProto";
    }

    else {
      consumerString = "with consumers [ ";
      for (auto &i : found->second) {
        consumerString += i;
        consumerString += " ";
      }
    }
    consumerString += "]";
    logging::info(
        "Adding {} Tensor {} to Ir {}.", tensor_type, tensorId, consumerString);
  };

  std::set<TensorId> onnxInitializers, unusedInitializers;

  for (const auto &initializer : onnxGraph.initializer()) {
    TensorId tenId = initializer.name();
    if (consumerTypes.find(tenId) == consumerTypes.end()) {
      logging::info("Not creating Tensor for unused initializer, {}", tenId);
      unusedInitializers.emplace(tenId);
    } else {

      uint32_t debugid                    = 0;
      CommGroupType type                  = CommGroupType::All;
      auto size                           = 0;
      VariableRetrievalMode retrievalMode = VariableRetrievalMode::OnePerGroup;
      {
        auto key = std::string(onnxDebugIdInputMetaDataKey) + tenId;
        for (auto m : onnxModel->metadata_props()) {
          if (m.key() == key) {
            debugid = std::stoi(m.value());
            break;
          }
        }
      }
      {
        auto key =
            std::string(sCommGroupType) + std::string(sNameDelimiter) + tenId;
        for (auto m : onnxModel->metadata_props()) {
          if (m.key() == key) {
            type = static_cast<CommGroupType>(std::stoi(m.value()));
            break;
          }
        }
      }
      {
        auto key =
            std::string(sCommGroupSize) + std::string(sNameDelimiter) + tenId;
        for (auto m : onnxModel->metadata_props()) {
          if (m.key() == key) {
            size = static_cast<unsigned>(std::stoi(m.value()));
            break;
          }
        }
      }
      {
        auto key = std::string(sVariableSettings) +
                   std::string(sNameDelimiter) + tenId;
        for (auto m : onnxModel->metadata_props()) {
          if (m.key() == key) {
            retrievalMode =
                static_cast<VariableRetrievalMode>(std::stoi(m.value()));
            break;
          }
        }
      }

      DebugNameAndId dnid(debugid);
      DebugContext onnxDc(dnid);
      OnnxVariableDebugInfo onnxDi(onnxDc, initializer);
      VariableSettings vs(CommGroup(type, size), retrievalMode);

      // If inference mode add initializers as constants if option enabled
      bool inference_constants =
          getExecutionMode() == ExecutionMode::Inference &&
          getSessionOptions().constantWeights == true;
      if (inference_constants && vs.numReplicasReturningVariable(
                                     userOptions.replicatedGraphCount) == 1) {
        logCreationInfo("Constant", tenId);
        getTensors().addConstInit(tenId, &initializer, DebugContext(onnxDi));
      } else {
        logCreationInfo("Variable", tenId);
        if (inference_constants) {
          logging::warn("Tensor {} was declined as a target of optimization "
                        "\"constantWeights\" "
                        "on the grounds that the tensor's {} do not allow for "
                        "the TensorType::Variable "
                        "to be initialized as a TensorType::Const",
                        tenId,
                        vs);
        }
        getTensors().addVarInit(tenId, &initializer, vs, DebugContext(onnxDi));
      }
      onnxInitializers.emplace(tenId);
    }
  }

  // used onnx inputs which are not initializers are true inputs
  for (auto &valueInfo : onnxGraph.input()) {
    TensorId id = valueInfo.name();
    if (onnxInitializers.count(id) == 0 && unusedInitializers.count(id) == 0) {

      // Suppressing cppcheck below as this is maybe not decided?
      // Should we allow unused stream tensors in the ONNX Model? To be decided.
      bool allowUnusedStreamTensors = true;
      if (consumerTypes.find(id) == consumerTypes.end() &&
          // cppcheck-suppress knownConditionTrueFalse
          !allowUnusedStreamTensors) {
        throw error("Request to create popart Stream Tensor {} failed, "
                    "as it has no consumers in the ONNX GraphProto. ",
                    id);
      }
      logCreationInfo("Stream", id);

      uint32_t debugid = 0;
      {
        auto key = std::string(onnxDebugIdInputMetaDataKey) + id;
        for (auto m : onnxModel->metadata_props()) {
          if (m.key() == key) {
            debugid = std::stoi(m.value());
          }
        }
      }

      // Construct InputSettings from ONNX metadata
      InputSettings settings;
      {
        {
          TileSet tileSet = TileSet::Compute;
          auto key =
              std::string(sTileSetAttribute) + std::string(sNameDelimiter) + id;
          for (auto m : onnxModel->metadata_props()) {
            if (m.key() == key) {
              tileSet = static_cast<TileSet>(std::stoi(m.value()));
            }
          }
          settings.setTileSet(tileSet);
        }

        {
          ExchangeStrategy strategy = ExchangeStrategy::JustInTime;
          auto key                  = std::string(sExchangeStrategyAttribute) +
                     std::string(sNameDelimiter) + id;
          for (auto m : onnxModel->metadata_props()) {
            if (m.key() == key) {
              strategy = static_cast<ExchangeStrategy>(std::stoi(m.value()));
            }
          }
          settings.setExchangeStrategy(strategy);
        }

        {
          ReplicatedStreamMode replicatedStreamMode =
              ReplicatedStreamMode::Replicate;
          auto key = std::string(sReplicatedStreamMode) +
                     std::string(sNameDelimiter) + id;
          for (auto m : onnxModel->metadata_props()) {
            if (m.key() == key) {
              replicatedStreamMode =
                  static_cast<ReplicatedStreamMode>(std::stoi(m.value()));
            }
          }
          settings.setReplicatedStreamMode(replicatedStreamMode);
        }
      }

      logging::ir::trace("Tensor: {} input settings: {}", id, settings);

      DebugNameAndId dnid(debugid);
      DebugContext onnxDc(dnid);

      if (inputShapeInfo.has(id)) {
        popart::OnnxVariableDebugInfo onnxDi(
            onnxDc, valueInfo, inputShapeInfo.get(id));
        getTensors().addStream(id, inputShapeInfo.get(id), settings, {onnxDi});
      } else if (valueInfo.has_type() &&
                 valueInfo.type().tensor_type().has_shape()) {
        checkForDimParams(id, valueInfo.type());
        popart::OnnxVariableDebugInfo onnxDi(onnxDc, valueInfo);
        getTensors().addStream(
            id, TensorInfo(valueInfo.type()), settings, {onnxDi});
      } else {
        throw error("Could not find tensor {} in InputShapeInfo, but no shape "
                    "is specified in the onnx model",
                    id);
      }

      // We will not be streaming data for this tensor from the host. Instead
      // initialise the tensor data once, here, based on the session option
      // syntheticDataMode
      if (useSyntheticData()) {
        Tensor *synStreamTensor = getTensor(id);
        const auto &info        = synStreamTensor->info;
        const auto dtype        = info.dataType();
        auto nelems             = info.nelms();
        std::vector<char> data;

        switch (syntheticDataMode()) {
        case SyntheticDataMode::Zeros: {
          while (nelems--) {
            const auto convertedData = convertFloatToDataType(dtype, 0.0f);
            data.insert(data.end(), convertedData.begin(), convertedData.end());
          }
          break;
        }
        case SyntheticDataMode::RandomNormal: {
          // Random normal number generator: mean 0, variance 1
          // Boost Random ensures numerical consistency across implementations
          std::mt19937 generator;
          boost::random::normal_distribution<float> normalDistribution(0.0,
                                                                       1.0);

          while (nelems--) {
            const auto val           = normalDistribution(generator);
            const auto convertedData = convertFloatToDataType(dtype, val);
            data.insert(data.end(), convertedData.begin(), convertedData.end());
          }
          break;
        }
        case SyntheticDataMode::RandomUniform:
          generateSyntheticUniformData(data, info);
          break;
        case SyntheticDataMode::Off:
        case SyntheticDataMode::N:
        default:
          throw error("Cannot set tensor data for current SyntheticDataMode");
        }
        POPART_ASSERT_EQ(data.size(), info.nbytes());
        synStreamTensor->setTensorDataByEmplaceOf(std::move(data));
      }
    }
  }
}

void Ir::validateAnchors() const {

  auto check = [this](TensorId id) {
    auto allTensorIds = getAllTensorIds();
    if (allTensorIds.find(id) == allTensorIds.end()) {
      std::stringstream ss;
      ss << "Anchor tensor `" << id << "' not in Ir Tensors. ";
      // add some trouble-shooting for a case I stumbled upon:
      if (id.find(reservedGradientPrefix()) != std::string::npos) {
        std::string degrad = getNonGradId(id);
        if (allTensorIds.find(degrad) != allTensorIds.end()) {
          ss << "\nInterestingly, `" << degrad << '\'' << " IS in tensors.\n";
          ss << "Note that not all tensors can have their gradients "
             << "anchored:\nif an activation tensor does not lead "
             << "to the loss,\nits gradient is zero and never computed.";
        }
      } else {
        ss << "The tensors are:\n";
        ss << allTensorIds;
      }
      throw error(ss.str());
    }
  };

  for (auto ids : anchorRemap.leftMap()) {
    // Check the anchor tensor providing the data
    check(ids.first);
    // Check the anchor root providing metainformation
    check(ids.second);
  }
}

bool Ir::applyPreAliasPattern(const PreAliasPattern *pattern, Graph &graph) {

  const auto scopedTimer =
      timePartitionLogger().scopedStopwatch(pattern->getPatternName());

  bool result = false;

  PopartTracepoint tp(
      logging::format("Applying pattern '{}'", pattern->getPatternName()));

  auto touchesInputToLoss = [&graph, pattern](Op *op) {
    for (auto &tensor : pattern->touches(op)) {
      if (graph.getTensors().contains(graph.getLoss())) {
        if (graph.getLoss() == tensor->id) {
          return true;
        }
      }
    }
    return false;
  };

  auto canApplyPattern = [this, &touchesInputToLoss, pattern](Op *op) {
    if (op->isExcludedFromPattern(pattern) || !pattern->matches(op) ||
        pattern->touchesAnchored(op)) {
      return false;
    }

    // If the ir will construct a loss, but hasn't yet, check that the pattern
    // doesn't touch the inputs to the loss.
    if (this->canTrain() && !this->constructedFinalLoss &&
        touchesInputToLoss(op)) {
      return false;
    }

    return true;
  };

  // the pattern chooses what order to go through the ops in

  std::vector<OpId> v_ops;
  v_ops.reserve(graph.getOps().size());

  for (auto &id_op : graph.getOps()) {
    v_ops.push_back(id_op.first);
  }

  for (auto opId : v_ops) {
    auto itr = graph.getOps().find(opId);

    // If the op still exists
    if (itr != graph.getOps().end()) {
      Op *op = itr->second.get();
      if (canApplyPattern(op)) {
        logging::pattern::debug("Applying pattern {} to {}",
                                pattern->getPatternName(),
                                op->debugName());
        result |= pattern->apply(op);
      }
    }
  }

  return result;
}

void Ir::applyPreAliasPatterns(Graph &graph) {

  bool keepRunning = true;
  std::vector<std::unique_ptr<PreAliasPattern>> pList =
      patterns.getPreAliasList();

  while (keepRunning) {
    foldConstants(graph);

    keepRunning = false;
    for (auto &pattern : pList) {
      keepRunning |= applyPreAliasPattern(pattern.get(), graph);
    }
  }
}

void Ir::applyTransform(std::size_t transformId, Graph &graph) {

  // Unless explictly set, a transform is enabled
  if (transformEnableMap.count(transformId) == 0 ||
      transformEnableMap.at(transformId)) {
    Transform::applyTransform(transformId, graph);
  }
}

void Ir::enableTransform(std::size_t transformId, bool enable) {
  transformEnableMap[transformId] = enable;
}

std::vector<Op *> Ir::opsOfType(const OperatorIdentifier &opid) const {
  std::vector<Op *> typedOps;
  for (auto &id_graph : graphs) {
    auto graph = id_graph.second.get();

    for (auto &id_op : graph->getOps()) {
      if (id_op.second->opid == opid) {
        typedOps.push_back(id_op.second.get());
      }
    }
  }
  return typedOps;
}

bool Ir::isConsumedByOpOfType(TensorId tid, const OperatorIdentifier &opid) {
  auto tensor       = getTensors().get(tid);
  auto tidConsumers = tensor->consumers.getOps();

  for (Op *op : tidConsumers) {
    if (op->opid == opid) {
      return true;
    }
  }
  return false;
}

bool Ir::isAnchored(const TensorId &tenId) const {
  return anchorRemap.hasLeft(tenId);
}

bool Ir::isRootAnchor(const TensorId &tenId) const {
  return anchorRemap.hasRight(tenId);
}

std::set<TensorId> Ir::getAnchors() const {
  std::set<TensorId> anchors;

  for (auto &anchor : anchorRemap.leftMap()) {
    anchors.insert(anchor.first);
  }

  return anchors;
}

std::set<TensorId> Ir::getRootAnchors() const {
  std::set<TensorId> anchors;

  for (auto &anchor : anchorRemap.rightMap()) {
    anchors.insert(anchor.first);
  }

  return anchors;
}

void Ir::remapAnchor(const TensorId &from, const TensorId &to) {
  if (!anchorRemap.hasLeft(from)) {
    throw error("[Ir::remapAnchor] {} is not an anchor.", from);
  }
  anchorRemap.remapLeft(from, to);
}

void Ir::addAnchor(const TensorId &t) { anchorRemap.insert(t, t); }

const BiMap<TensorId, TensorId> &Ir::getAnchorRemap() const {
  return anchorRemap;
}

bool Ir::streamingIsDisabledForTensor(const TensorId &tensorId) const {
  const Tensor *tensor = getTensors().get(tensorId);
  return streamingIsDisabledForTensor(tensor);
}

bool Ir::streamingIsDisabledForTensor(const Tensor *tensor) const {
  // What conditions mean that this tensor should not be streamed?

  // 1. Streams have been turned off globally
  if (useSyntheticData()) {
    return true;
  }

  // 2. Disable streaming as per the following table:
  //
  //  .----- tensor->isOptimizerStateTensor()
  //  | .--- tensor->isAccumulatorTensor()
  //  | |
  //  v v  | Disable if expression holds
  // ======|=======================================
  //  N N  | false
  //  N Y  | disableAccu
  //  Y N  | disableOpt
  //  Y Y  | disableAccu && disableOpt
  // ======|=======================================
  //
  // Where:
  //   disableAccu = getSessionOptions().disableGradAccumulationTensorStreams
  //   disableOpt  = getSessionOptions().disableOptimiserStateTensorStreams

  if (tensor->isAccumulatorTensor() || tensor->isOptimizerStateTensor()) {

    bool disable = true;

    if (tensor->isAccumulatorTensor() &&
        !getSessionOptions().disableGradAccumulationTensorStreams) {
      disable = false;
    }

    if (tensor->isOptimizerStateTensor() &&
        !getSessionOptions().disableOptimizerStateTensorStreams) {
      disable = false;
    }

    if (disable) {
      return true;
    }
  }

  // 3. The tensor is remote
  if (tensor->tensorLocationInfo.isRemote()) {
    return true;
  }

  return false;
}

bool Ir::storingIsDisabledForTensor(const TensorId &tensorId) const {
  const Tensor *tensor = getTensors().get(tensorId);
  return storingIsDisabledForTensor(tensor);
}

bool Ir::storingIsDisabledForTensor(const Tensor *tensor) const {
  // What conditions mean that this tensor should not be streamed?

  // 1. Streams have been turned off globally
  if (useSyntheticData()) {
    return true;
  }

  // 2. Disable storing (see comment in Ir::streamingIsDisabledForTensor).
  if (tensor->isAccumulatorTensor() || tensor->isOptimizerStateTensor()) {

    bool disable = true;

    if (tensor->isAccumulatorTensor() &&
        !getSessionOptions().disableGradAccumulationTensorStreams) {
      disable = false;
    }

    if (tensor->isOptimizerStateTensor() &&
        !getSessionOptions().disableOptimizerStateTensorStreams) {
      disable = false;
    }

    if (disable) {
      return true;
    }
  }

  // 3. Tensor is variable but has a producer
  if (tensor->hasProducer()) {
    return true;
  }

  // 4. The tensor is an Accum__ or Counter__ tensor - these will be zero in the
  // current implementation
  if (tensor->isAccumulatorTensor() &&
      (tensor->id.find(reservedAccumPrefix()) != std::string::npos ||
       tensor->id.find(reservedCounterPrefix()) != std::string::npos)) {
    return true;
  }

  return false;
}

void Ir::constructForwards() {
  if (!hasOnnxModel()) {
    throw error("Ir::constructForwards: Ir has no Onnx model");
  }

  const auto scopedStopwatch =
      timePartitionLogger().scopedStopwatch("Constructing forwards (Ir)");

  constructFromOnnxGraph(onnxModel->graph(), {});
  for (auto &id_op : getMainGraph().getOps()) {
    auto op      = id_op.second.get();
    op->fromLoss = PathFromLoss::No;
  }
}

Graph &Ir::constructFromOnnxGraph(const ONNX_NAMESPACE::GraphProto &graph,
                                  const Scope &scope) {
  auto scope_id = scope.str();
  if (graphs.find(scope_id) == graphs.end()) {
    logging::ir::debug("Adding new graph for scope {}", scope_id);
    graphs.insert({scope_id, std::make_unique<Graph>(*this, scope_id)});
  }

  graphs.at(scope_id)->constructFromOnnxGraph(graph);

  return *graphs.at(scope_id);
}

void Ir::foldConstants(Graph &graph) {
  logging::ces::trace("Folding constants");
  ConstExprUtil::foldConstants(graph);
}

OpId Ir::getAndIncrOpsCounter() {
  OpId nOps0 = opsCounter;
  ++opsCounter;
  return nOps0;
}

OpId Ir::getOpsCounter() const { return opsCounter; }

OptionalVGraphId
Ir::getVirtualGraphIdFromTensorProducers(std::vector<Tensor *> ts) {
  // Count which vgraph's the producer ops are on.
  std::map<int64_t, int64_t> vgraphIdMap;
  for (auto &t : ts) {
    Op *producer = t->getProducerUnsafe();
    if (producer) {
      if (producer->hasVirtualGraphId()) {
        vgraphIdMap[producer->getVirtualGraphId()]++;
      }
    }
  }

  if (vgraphIdMap.size() == 0) {
    std::vector<TensorId> ts_ids;
    for (auto t : ts) {
      ts_ids.push_back(t->id);
    }
    throw internal_error(
        "None of the producers of the tensors in {} have virtual "
        "graph ids",
        ts_ids);
  }

  // Find the vgraph id with the most occurrences.
  auto it = std::max_element(vgraphIdMap.begin(),
                             vgraphIdMap.end(),
                             [](const std::pair<int64_t, int64_t> &p1,
                                const std::pair<int64_t, int64_t> &p2) {
                               return p1.second < p2.second;
                             });

  return OptionalVGraphId(it->first);
}

PipelineStage Ir::getFinalLossPipelineStage() const {
  auto finalLossOpFound = getMainGraph().getOps().find(finalLossOpId);
  if (finalLossOpFound != getMainGraph().getOps().end()) {
    auto lossOp = finalLossOpFound->second.get();
    return lossOp->getPipelineStage();
  } else {
    throw error("Could not find final loss to get PipelineStage from");
  }
}

PipelineStage Ir::getMaxPipelineStage() const {
  auto finalLossStage = getFinalLossPipelineStage();
  if (getSessionOptions().createImplicitPipeliningFwdOnlyProgram) {
    // Separate first backward stage from last forward stage when using
    // a shared training and inference graph in order to cleanly separate
    // forward and backward pass (and thereby stages)
    return 2 * finalLossStage + 1;
  }
  // First backward stage shared with last forward stage
  return 2 * finalLossStage;
}

int64_t Ir::getNumPipelineStages() const {
  std::set<PipelineStage> pStages;

  for (auto op : getAllOps()) {
    if (op->hasPipelineStage()) {
      pStages.insert(op->getPipelineStage());
    }
  }
  int64_t numStages = pStages.size();

  // Check there are no 'missing' pipeline stages
  for (int64_t i = 0; i < numStages; i++) {
    if (!pStages.count(i)) {
      throw error("The set of pipeline stages for all Ops contains {} stages, "
                  "but stage {} is missing",
                  numStages,
                  i);
    }
  }
  return numStages;
}

PipelineInfo Ir::pipelineInfo() const {
  PipelineInfo pInfo;
  if (getSessionOptions().enablePipelining) {
    pInfo = PipelineInfo(static_cast<int64_t>(getDataFlow().batchesPerStep()),
                         getSessionOptions().accumulationFactor,
                         getNumPipelineStages(),
                         getSessionOptions().enableGradientAccumulation,
                         Pipeline::withStages(*this));
  }
  return pInfo;
}

// design choice: we could have an "irHasChanged"
// flag which is set to true whenever the Ir changes,
// and then if irHasChanged is false, calls
// to this (and other) functions can do nothing.
// The cost of maintaining irHasChanged is non-trivial
// and would require runtime overhead, for now not
// going to implement it.
//

void Ir::updateVertices() {
  // for all vertices (Ops and Tensors), set
  //  1) toLoss (is there a path to the final loss?)
  //  2) fromLoss (is there a path from the final loss?)
  //  3) scheduledPreLoss (is it scheduled before the final loss?)

  auto scopedStopwatch =
      timePartitionLogger().scopedStopwatch("Updating Vertices.");

  logging::ir::info(
      "Updating all Vertices (toLoss, fromLoss, scheduledPreLoss)");

  for (auto &graphIdAndGraphPt : graphs) {
    auto &graph = *graphIdAndGraphPt.second.get();

    // 1, 2)
    graphFromLossToLossUpdater::propagate(graph);

    // 3.1) scheduledPreLoss for Ops.
    // Op which have PathFromLoss::Yes are ScheduledPreLoss::No
    for (auto &id_op : graph.getOps()) {
      auto op = id_op.second.get();

      if (op->fromLoss == PathFromLoss::Yes ||
          op->settings.executionContext ==
              ExecutionContext::AccumulateOuterFragment) {
        op->scheduledPreLoss = ScheduledPreLoss::No;
      } else {
        op->scheduledPreLoss = ScheduledPreLoss::Yes;
      }
      if (op->scheduledPreLoss == ScheduledPreLoss::No &&
          op->settings.recomputeType != RecomputeType::Recomputed) {
        op->settings.recomputeType = RecomputeType::Checkpoint;
      }
    }
    if (graph.id == getMainGraph().id) {
      logging::ir::debug(
          "setting scheduledPreLoss for Tensors in updateVertices");
      // 3.2) scheduledPreLoss for Tensors and any ops occurring post the loss
      // in the schedule
      bool postLoss = false;
      auto ops      = graph.getOpSchedule({}, RequireOptimalSchedule::Yes);
      for (auto op : ops) {
        postLoss |= op->scheduledPreLoss == ScheduledPreLoss::No;
        if (postLoss) {
          // The loss has been crossed, everything ScheduledPreLoss::No from
          // here on
          op->scheduledPreLoss = ScheduledPreLoss::No;
        }
        for (auto tensor : op->input->tensors()) {
          // inputs to pre-loss are pre-loss
          if (op->scheduledPreLoss == ScheduledPreLoss::Yes) {
            tensor->scheduledPreLoss = ScheduledPreLoss::Yes;
            // inputs to post-loss are post-loss if not already pre-loss
          } else if (op->scheduledPreLoss == ScheduledPreLoss::No) {
            if (tensor->scheduledPreLoss != ScheduledPreLoss::Yes) {
              tensor->scheduledPreLoss = ScheduledPreLoss::No;
            }
          }
        }

        // Outputs are always the same as the producer Op, this rule takes
        // priority over all input annotation rules.
        for (auto tensor : op->output->tensors()) {
          tensor->scheduledPreLoss = op->scheduledPreLoss;
        }
      }
    }
  }
}

void Ir::unsetAllVirtualGraphIds() {
  bool hadToUnsetAny = false;

  for (auto &id_graph : graphs) {
    auto &graph = id_graph.second;
    for (auto &id_op : graph->getOps()) {
      auto op = id_op.second.get();

      if (op->hasVirtualGraphId()) {
        // no virtual graph id
        op->setVirtualGraphId({});
        hadToUnsetAny = true;
      }
    }
  }

  if (hadToUnsetAny) {
    logging::ir::info("Virtual graph settings ignored because virtual "
                      "graphs are not enabled.");
  }
}

void Ir::constructBackwards() {

  logging::ir::info("Constructing backwards pass");

  applyTransform(Autodiff::id(), getMainGraph());

  AliasModel mainGraphAliasModel;
  AliasModelGrower aliasModelGrower{mainGraphAliasModel};
  aliasModelGrower.growFullGraph(getMainGraph(), DataDependenciesOnly::Yes);

  logging::ir::info("Creating Variable Tensor update Ops");
  // add weight update ops (we are ignoring momentums for now)
  for (auto &varId : getTensors().getIds(TensorType::Variable)) {

    Tensor *tensor = getTensors().get(varId);
    switch (tensor->getVariableUpdateType()) {
    case VariableUpdateType::Copy:
      // Updates the var by copying it from another tensor
      growCopyVarUpdateOp(
          varId, tensor->getCopyFromTensor(), mainGraphAliasModel);
      break;
    case VariableUpdateType::Gradient:
      // Updates the var by looking for the matching gradient
      growGradientVarUpdateOp(varId, mainGraphAliasModel);
      break;
    case VariableUpdateType::None:
      logging::info("Tensor {} does not need a variable update.", tensor->id);
      break;
    default:
      throw error("Unknown variable update approach");
    }
  }

  setMainGraphPathFromLoss();

  logging::ir::info("Constructing backwards complete");
  constructedBackwards = true;
}

void Ir::growCopyVarUpdateOp(const TensorId &varId,
                             const TensorId &from,
                             AliasModel &mainGraphAliasModel) {
  OpId opId = getMainGraph().moveIntoGraph(
      std::unique_ptr<Op>(new CopyVarUpdateOp({getMainGraph(), ""})));

  // The order of inputs is important
  std::vector<TensorId> inputs{varId, from};
  getMainGraph().connectInputs(InputVecWrapper(inputs), opId);

  growVarUpdateOpInternal(opId, mainGraphAliasModel);
}

void Ir::growGradientVarUpdateOp(const TensorId &varId,
                                 AliasModel &mainGraphAliasModel) {

  logging::ir::info("Growing gradient var update op for {}", varId);

  // A sanity check that the Tensor is not fixed point type
  if (getTensors().get(varId)->info.getDataTypeInfo()->isFixedPoint()) {
    throw error("Currently only floating point variable tensors are updatable");
  }

  const Tensor &var = *getTensors().get(varId);
  auto inputIds     = optimizer->getInputIds(var);

  auto optimizerInputs = optimizer->getOptimizerInputs(var);

  // If there is no weight gradient, we assume that the gradient has been
  // forced to zero somewhere else in the backwards pass
  bool updaterAvailable = getMainGraph().getTensors().contains(
      inputIds.at(VarUpdateWithUpdaterOp::getUpdaterInIndex()));

  if (updaterAvailable) {

    // create the required optimizer tensors as needed
    for (auto opt : optimizerInputs) {
      auto optId   = std::get<0>(opt);
      auto optInfo = std::get<1>(opt);
      DebugInfo debugInfo(optimizer->getDebugContext(), "popartbuilder");
      ensureOptimizerTensorCreated(optId, optInfo, {debugInfo, optId});
    }

    OpId opId =
        getMainGraph().moveIntoGraph(optimizer->createOp(var, getMainGraph()));

    getMainGraph().connectInputs(InputVecWrapper(inputIds), opId);
    growVarUpdateOpInternal(opId, mainGraphAliasModel);
  }
}

void Ir::ensureOptimizerTensorCreated(const TensorId &optId,
                                      const TensorInfo &info,
                                      const DebugContext &debugContext) {
  if (!getTensors().contains(optId)) {

    getTensors().addStream(optId, info, debugContext);
    Tensor &optTensor = *getTensors().get(optId);
    optimizer->setTensorData(optTensor);

    // optimizer tensors are a special type of stream which is broadcast
    optTensor.setReplicatedStreamMode(ReplicatedStreamMode::Broadcast);
  }
}

void Ir::growVarUpdateOpInternal(OpId opId, AliasModel &mainGraphAliasModel) {
  Op *op           = getMainGraph().getOps()[opId].get();
  auto varUpdateOp = dynamic_cast<VarUpdateOp *>(op);
  if (varUpdateOp == nullptr) {
    throw internal_error("Op {} expected to be a VarUpdateOp", op->str());
  }
  TensorId updatedVarId =
      getUpdatedVarId(varUpdateOp->inId(VarUpdateOp::getVarToUpdateInIndex()));
  std::vector<TensorId> outputs{updatedVarId};
  getMainGraph().connectOutputs(OutputVecWrapper(outputs), opId);
  op->setup();
  op->inheritPlacementAttributes(false, mainGraphAliasModel);
}

void Ir::setFinalLoss(const TensorId &loss) {
  logging::ir::info("Growing final loss");

  if (getMainGraph().getTensors().contains(loss)) {
    if (getMainGraph().getTensors().get(loss)->info.nelms() > 1) {
      throw error("Loss tensor, '{}', must be a scalar tensor", loss);
    }

    // The final Loss Op is the only Op which (we say) has both
    // paths to and from
    auto finalLossOp      = getTensors().get(loss)->getProducer();
    finalLossOp->toLoss   = PathToLoss::Yes;
    finalLossOp->fromLoss = PathFromLoss::Yes;
    finalLossId           = loss;
    finalLossOpId         = finalLossOp->id;

    logging::ir::trace("Final loss Op id set to {} ({})",
                       finalLossOpId,
                       finalLossOp->debugName());
  } else {
    throw error("Could not find loss tensor '{}' in main graph tensors", loss);
  }

  constructedFinalLoss = true;
}

TensorId Ir::getFinalLossId() const { return finalLossId; }

void Ir::append(std::stringstream &ss) const {
  ss << "\n";

  int i           = 0;
  auto printGraph = [&](const Graph *graph) {
    if (i > 0) {
      ss << "============================================================\n";
    }
    i += 1;

    if (graph->id.str() != "") {
      ss << graph->id.str() << ":"
         << "\n";
    }

    for (auto &op : graph->getOpSchedule({}, RequireOptimalSchedule::Yes)) {
      op->append(ss);
    }
  };

  // Print the main graph first.
  printGraph(&getMainGraph());

  // Print all subgraphs.
  for (auto graph : getAllGraphs()) {
    if (graph->id != getMainGraph().id) {
      printGraph(graph);
    }
  }
}

void Ir::finalizeOpDebugInfo() {

  for (auto graph : getGraphSchedule()) {
    for (auto &op : graph->getOpSchedule({}, RequireOptimalSchedule::Yes)) {
      op->finalizeDebugInfo();
    }
  }
}

void Ir::serialise(SerialiseFormat format,
                   std::stringstream &ss,
                   bool useScheduler) const {

  auto getGraphs = [this, useScheduler]() {
    if (useScheduler) {
      return getGraphSchedule();
    } else {
      std::vector<const Graph *> result;
      for (auto &id_graph : graphs) {
        auto graph = id_graph.second.get();
        result.push_back(graph);
      }
      return result;
    }
  };

  auto getOps = [useScheduler](auto *graph) {
    if (useScheduler) {
      return graph->getOpSchedule({}, RequireOptimalSchedule::Yes);
    } else {
      std::vector<Op *> result;
      for (auto &id_op : graph->getOps()) {
        auto op = id_op.second.get();
        result.push_back(op);
      }
      return result;
    }
  };

  auto appendGraphName = [this](const GraphId &id, std::stringstream &ss) {
    GraphId nameToUse{""};

    // If it's the main graph AND the user did
    // not override the name in the builder, display "maingraph", otherwise the
    // graph's id.
    if (id == getMainGraph().id) {
      // NOTE: The maingraph id MUST be GraphId::root(). It is not valid to set
      // the id, as the maingraph is immediately created in the Ir constructor
      // and entered into the `graphs` map with id = GraphId::root().

      const bool mainGraphHasCustomNameFromBuilder =
          hasOnnxModel() && (getModel().graph().name().find("BuilderGraph_") ==
                             std::string::npos);

      if (mainGraphHasCustomNameFromBuilder) {
        nameToUse = getModel().graph().name();
      } else {
        nameToUse = GraphId{"maingraph"};
      }
    } else {
      nameToUse = id;
    }

    ss << "\"" << nameToUse << "\" :[";
  };

  // TODO use the format to serialize the ir
  (void)format;

  ss << "{";

  bool firstGraph = true;
  for (auto graph : getGraphs()) {

    if (!firstGraph)
      ss << ",";

    appendGraphName(graph->id, ss);

    bool firstOp = true;
    for (auto &op : getOps(graph)) {

      if (!firstOp)
        ss << ",";

      op->toJSON(ss);

      firstOp = false;
    }

    ss << "]";

    firstGraph = false;
  }

  ss << "}";
}

int Ir::getDefaultOpsetVersion(const std::string &domain) const {
  if (domain == Domain::ai_onnx) {
    return defaultAiOnnxOpset;
  } else if (domain == Domain::ai_onnx_ml) {
    return defaultAiOnnxMlOpset;
  } else if (domain == Domain::ai_graphcore) {
    return defaultAiGraphcoreOpset;
  } else {
    throw error("No default opset version defined for domain \'{}\'", domain);
  }
}

int Ir::getOpSetVersionFromModel(const std::string &node_domain) const {
  // If the node.domain is blank it means the default ai.onnx
  auto domain = node_domain;
  if (domain == "") {
    domain = Domain::ai_onnx;
  }

  // Ideally, this method would throw on no Onnx model, and the callsites would
  // be decoupled from Onnx. For now, we just return the default.
  if (!hasOnnxModel()) {
    return getDefaultOpsetVersion(domain);
  }

  // Get the version of the opset from the model based on the domain
  int version    = 0;
  auto opsetList = getModel().opset_import();
  for (auto &opset : opsetList) {

    std::string opset_domain;
    if (opset.has_domain() == false || opset.domain() == "") {
      opset_domain = Domain::ai_onnx;
    } else {
      opset_domain = opset.domain();
    }

    if (domain == opset_domain) {

      auto opset_version = static_cast<int>(opset.version());

      // If the same domain is mentioned multiple times find the largest
      if (opset_version > version)
        version = opset_version;
    }
  }

  // If the version has not be set use the default
  if (version == 0) {
    version = getDefaultOpsetVersion(domain);
  }

  return version;
}

unsigned Ir::getNumVirtualGraphIds() const {
  unsigned numVirtualGraphIds = 1;
  unsigned replGraphCount =
      static_cast<unsigned>(getSessionOptions().replicatedGraphCount);
  unsigned numIPUs = static_cast<unsigned>(deviceInfo->getNumIpus());
  if (getSessionOptions().enableReplicatedGraphs) {
    if (numIPUs % replGraphCount != 0) {
      throw error("For replicated graphs, the number of IPUs must be divisible "
                  "by the replication factor.");
    } else {
      numVirtualGraphIds = numIPUs / replGraphCount;
    }
  } else {
    numVirtualGraphIds = numIPUs;
  }
  return numVirtualGraphIds;
}

OpId Ir::getFinalLossOpId() const { return finalLossOpId; }

std::vector<const Graph *> Ir::getGraphSchedule() const {

  auto sorted = getGraphSchedule(getMainGraph().id);

  if (sorted.size() != graphs.size()) {
    std::stringstream ss;
    ss << logging::format("Unable to schedule all graphs. {} != {}. ",
                          sorted.size(),
                          graphs.size());
    std::vector<GraphId> sortedIds;
    std::transform(sorted.begin(),
                   sorted.end(),
                   std::back_inserter(sortedIds),
                   [](const Graph *g) { return g->id; });
    ss << "Missing: " << std::endl;
    for (const auto &g : graphs) {
      if (std::find(sortedIds.begin(), sortedIds.end(), g.first) ==
          sortedIds.end()) {
        ss << "  " << g.first << std::endl;
      }
    }
    throw error(ss.str());
  }

  return sorted;
}

std::vector<const Graph *> Ir::getGraphSchedule(GraphId root) const {
  std::vector<const Graph *> sorted;
  std::set<const Graph *> seen;

  std::function<void(const Graph *)> scheduleGraph;
  scheduleGraph = [&](const Graph *graph) {
    // only try schedule a graph once
    if (seen.find(graph) == seen.end()) {
      seen.insert(graph);
    } else {
      return;
    }

    // add graph to schedule
    sorted.push_back(graph);

    // schedule all called graphs
    for (auto g : graph->getCalledGraphs()) {
      scheduleGraph(g);
    }
  };

  scheduleGraph(&getGraph(root));

  return sorted;
}

std::vector<Op *> Ir::getOpSchedule(const OpsBeforeKey &gCons,
                                    const RequireOptimalSchedule ros) const {
  std::vector<Op *> sorted;
  std::set<const Graph *> addedGraphs;

  std::function<void(const Graph *)> addGraph;
  addGraph = [&](const Graph *graph) {
    // Only add each graph once
    if (addedGraphs.find(graph) != addedGraphs.end()) {
      return;
    }
    addedGraphs.insert(graph);

    // Add each op in the graph
    for (auto op : graph->getOpSchedule(gCons, ros)) {
      // If the op calls another graph
      // the ops in that graph should be scheduled first
      for (auto calledGraph : op->getCalledGraphs()) {
        addGraph(calledGraph);
      }

      sorted.push_back(op);
    }
  };

  // Start adding ops from the main graph
  addGraph(&getMainGraph());

  return sorted;
}

// Are the Ops with all the dependencies a DAG?
bool Ir::isSchedulable(const OpsBeforeKey &gCons) const {
  for (auto &id_graph : graphs) {
    if (!id_graph.second->isSchedulable(gCons)) {
      return false;
    }
  }
  return true;
}

Ir::ExecutionMode Ir::getExecutionMode() const { return executionMode; }

bool Ir::canInfer() const {
  return getExecutionMode() == ExecutionMode::Inference || canTrain();
}

bool Ir::canTrain() const {
  return getExecutionMode() == ExecutionMode::Training;
}

bool Ir::hasConstructedBackwards() const { return constructedBackwards; }

bool Ir::hasDecomposedOptimizers() const { return decomposedOptimizers; }

bool Ir::containsInitialisers() const {
  return hasOnnxModel() && !onnxModel->graph().initializer().empty();
}

bool Ir::tensorExistsInInitialisers(TensorId tId) const {
  // If there is no Onnx model, then there are not any initialisers anyway.
  if (!hasOnnxModel()) {
    return false;
  }

  for (int init_index = 0; init_index < onnxModel->graph().initializer_size();
       ++init_index) {
    if (onnxModel->graph().initializer(init_index).name() == tId) {
      return true;
    }
  }
  return false;
}

void Ir::applyUpdateInplacePrioritiesForIpu() {
  UpdateInplacePrioritiesForIpu pattern;

  for (auto &id_graph : graphs) {
    auto graph = id_graph.second.get();
    for (auto &id_op : graph->getOps()) {
      Op *op = id_op.second.get();
      if (!op->isExcludedFromPattern(&pattern)) {
        pattern.apply(op);
      }
    }
  }
}

void Ir::applyInplacePattern(Graph &graph) {

  // The decision of where topological constraints need to be inserted is made
  // by a poprithms Graph whose Ops mirror those in \a graph.
  AliasModel popMem;
  AliasModelGrower aliasModelGrower{popMem};
  aliasModelGrower.growFullGraph(graph, DataDependenciesOnly::No);

  Inplace inplace;

  // <0> the id of the Op to inplace
  // <1> the type of the inplace Op
  // <2> the priority of this inplacing
  using Triplet = std::tuple<OpId, OperatorIdentifier, float>;

  std::vector<Triplet> priorities;
  for (auto &id_op : graph.getOps()) {
    Op *op = id_op.second.get();

    // first see if the user has overridden the default priorities
    std::set<OpType> prioritized;
    for (auto ip : op->settings.inplacePriorityVeto) {
      OpType inplaceId = std::get<0>(ip);
      priorities.push_back({
          op->id,
          {
              Domain::ai_graphcore, // the domain (same for all inplace ops)
              inplaceId,            // the name of the Operator (OpId)
              1                     // version
          },
          std::get<1>(ip) // the priority value
      });
      prioritized.insert(inplaceId);
    }

    // for all the inplacers not in the user list, take the default
    for (auto ip : op->inplacePriorityDefault()) {
      OperatorIdentifier identifier = std::get<0>(ip);
      if (prioritized.count(identifier.type) == 0) {
        priorities.push_back({op->id, identifier, std::get<1>(ip)});
      }
    }
  }

  auto tripletComparator = [](const Triplet &a, const Triplet &b) {
    if (std::get<2>(a) - std::get<2>(b) != 0.0f) {
      return std::get<2>(a) > std::get<2>(b);
    }
    // if same priority, fall back to ID to keep it deterministic
    //  if (std::get<0>(a) != std::get<0>(b)) {
    return std::get<0>(a) > std::get<0>(b);
    //    }
    // need lhs to go before rhs (see also T23594)
    //     return std::get<1>(a) < std::get<1>(b);
  };

  if (priorities.size() != 0) {

    // sort in decreasing order of priority,
    std::sort(priorities.begin(), priorities.end(), tripletComparator);

    // removing all negative priorities. We use std::lower_bound
    // instead of std::find_if, taking advantage of the fact that priorities
    // are sorted at this point.

    // (1) we create a "pivot" with priority 0
    Triplet zeroPriority      = priorities[0];
    std::get<2>(zeroPriority) = 0.;

    // (2) we find the first elements in priorities which is not less than the
    // pivot, and erase all elements from there to the end. Note that
    // priority 0 elements will be removed.
    auto found = std::lower_bound(
        priorities.begin(), priorities.end(), zeroPriority, tripletComparator);
    priorities.erase(found, priorities.end());

    // we keep track of which ops have already been inplaced
    std::set<OpId> inplacedAlready;

    for (auto &ip : priorities) {

      OpId id                       = std::get<0>(ip);
      OperatorIdentifier identifier = std::get<1>(ip);

      // check that the op has not already been inplaced
      auto inplaced_already_it = inplacedAlready.find(id);
      if (inplaced_already_it != inplacedAlready.end()) {
        std::ostringstream oss;
        oss << "[Inplacing] The Op being considered for inplacing, " << id
            << ", is already inplace.";
        logging::pattern::debug(oss.str());
        continue;
      }

      Op *op = graph.getOps().at(id).get();

      if (op->isExcludedFromPattern(&inplace)) {
        std::ostringstream oss;
        oss << "[Inplacing] The Op being considered for inplacing, "
            << op->str() << ", is excluded from the Inplacing Pattern.";
        logging::pattern::debug(oss.str());
        continue;
      }
      if (!op->isOutplace()) {
        std::ostringstream oss;
        oss << "[Inplacing] The Op being considered for inplacing, "
            << op->str() << ", is already inplace.";
        logging::pattern::debug(oss.str());
        continue;
      }

      auto proposal = op->mapInplaceProposal(popMem, identifier);

      const auto result = popMem.g.tryOpeningPartial(
          proposal,
          poprithms::memory::inplace::CheckParallelWriteable::No,
          poprithms::memory::inplace::AllowMultiGateAlias::No);

      if (!result.isValid()) {
        std::ostringstream oss;
        oss << "[Inplacing] Proposal " << proposal << " result : " << result;
        logging::pattern::debug(oss.str());
        popMem.g.backoutOpening(proposal);
        continue;
      }

      // Convert poprithms topological constraints into popart constraints
      OpsBeforeKey newTopoCons;
      for (auto from_to : result.constraints()) {
        const auto rithmFrom = std::get<0>(from_to);
        const auto rithmTo   = std::get<1>(from_to);
        if (popMem.contains(rithmFrom) && popMem.contains(rithmTo)) {
          const auto fromOpId = popMem.getOpId(rithmFrom);
          const auto toOpId   = popMem.getOpId(rithmTo);
          const auto from     = graph.getOp(fromOpId);
          const auto to       = graph.getOp(toOpId);
          if (fromOpId != toOpId) {
            auto found = newTopoCons.find(to);
            if (found == newTopoCons.cend()) {
              newTopoCons.insert({to, {from}});
            } else {
              found->second.push_back(from);
            }
          }
        } else {
          std::ostringstream oss;
          oss << "No PopART Ops for either " << rithmFrom << " or " << rithmTo
              << ", skipping constraint. ";
          logging::pattern::debug(oss.str());
        }
      }

      // beforeProducesOutput flag is used to prevent inplacing if any of the
      // new constraints requried to inplace a node has a before node that
      // produces an output of the graph. this is prevented because if the graph
      // is executed using a call op, then the out from the nodes are copied
      // after all the nodes of the sub graph have executed. this would cause
      // the inplaced data to be corrupted even if the constraints are in place
      // as the tensor output copy is delayed.
      bool beforeProducesOutput = false;
      for (auto &before_after : newTopoCons) {
        auto &befores = before_after.second;

        for (auto &before : befores) {
          if (before->producesGraphOutput()) {
            beforeProducesOutput =
                true; // before node of the topocon constraint produces output
            std::ostringstream oss;
            oss << "[Inplacing] " << op->str()
                << ", Excluded due to the required topological constraint with "
                   "output node, "
                << before->str();
            logging::pattern::debug(oss.str());
            break;
          }
        }
        if (beforeProducesOutput) {
          popMem.g.backoutOpening(proposal);
          break;
        }
      }
      if (beforeProducesOutput) {
        popMem.g.backoutOpening(proposal);
        continue;
      }

      ExternOpTensorBundle eot_bun(op, op->getInplaceVariant(identifier));
      const Op *inplaceOp = eot_bun.getOp();

      // check if input is a variable or aliases a variable, check if output is
      // modified by any consumer.
      // if input is variable: check by using aliasChainsTo(input), if the
      // aliases are updated properly, check any connected variable tensor if
      // the aliasing chain is non-empty.
      // If output is modified:
      // check by using aliasChainsFrom(output), check any connected tensor if
      // the aliasing chain is non-empty & any consumer of any aliased tensor
      // downstream modifies a non-empty region.
      // If both conditions true: do not inplace current op.

      bool inplaceBlocking = false;
      for (const auto &in_index : inplaceOp->input->tensorMap()) {
        for (const auto &out_index : inplaceOp->output->tensorMap()) {

          auto regions = inplaceOp->aliases(in_index.first, out_index.first);
          bool opAliases =
              std::any_of(regions.begin(),
                          regions.end(),
                          [](const view::Region &r) { return !r.isEmpty(); });

          auto isConflictTensor = [](Tensor *t) {
            if (t->isUnmodifiable() || t->isImplicitRecomputeTensor()) {
              return true;
            }
            for (Op *consumer : t->consumers.getOps()) {
              if (consumer->isIpuCopyOp()) {
                return true;
              }
            }
            return false;
          };

          auto restoreInplaceTensor = [](Tensor *t) {
            return t->isRestoreInplaceTensor();
          };
          auto isImplicitRecomputeTensor = [](Tensor *t) {
            return t->isImplicitRecomputeTensor();
          };

          bool restoreInplaceIn =
              op->input->tensor(in_index.first)->anyAlias(restoreInplaceTensor);
          bool restoreInplaceOut = op->output->tensor(out_index.first)
                                       ->anyAlias(restoreInplaceTensor);

          bool conflictIn =
              op->input->tensor(in_index.first)->anyAlias(isConflictTensor);
          bool conflictOut =
              op->output->tensor(out_index.first)->anyAlias(isConflictTensor);

          // Check that no conflict tensors, through aliasing, can be consumed
          // by a RestoreInplaceOp
          bool restoreInplaceConflict = (restoreInplaceIn && conflictOut) ||
                                        (restoreInplaceOut && conflictIn);

          // If the inplaced Op creates an alias between input and output,
          // which would lead to an aliased tensor being both consumed by an
          // RestoreInpaceOp and at the same time a "conflict" tensor.
          if (opAliases && restoreInplaceConflict) {
            logging::pattern::trace(
                "[Inplacing] Not inplacing {} with {} as it aliases a "
                "restore inplace tensor and a tensor consumed by an IpuCopyOp: "
                "{} -> {} ",
                op->debugName(),
                inplaceOp->opid,
                in_index.second->id,
                out_index.second->id);
            inplaceBlocking = true;
          }

          // Unmodifiable
          // 1. Is the input unmodifiable?
          bool unmodifiable = op->inputUnmodifiable(in_index.first);
          // 2. Does it indirectly modify this tensor and alias it?
          bool indirectModify =
              (op->hasAliasedModifiers(out_index.first) && opAliases);
          // 3. Does it directly modify a weight?
          bool directModify = inplaceOp->modifiesIndex(in_index.first);
          // If ((1 and 2) or 3) : do not inplace.
          if (unmodifiable && (indirectModify || directModify)) {
            logging::pattern::trace(
                "[Inplacing] Not inplacing {} with {} as it aliases an "
                "unmodifiable tensor: {} and either a downstream op "
                "modifies an alias of an output {}, or the inplace op itself "
                "modifies the tensor.",
                op->debugName(),
                inplaceOp->opid,
                in_index.second->id,
                out_index.second->id);
            inplaceBlocking = true;
          }

          if ((indirectModify || directModify) &&
              op->input->tensor(in_index.first)
                  ->anyAlias(isImplicitRecomputeTensor)) {
            logging::pattern::trace("[Inplacing] Not inplacing {} with {} as "
                                    "it would be modified by a recomputation "
                                    "{} -> {} ",
                                    op->debugName(),
                                    inplaceOp->opid,
                                    in_index.second->id,
                                    out_index.second->id);
            inplaceBlocking = true;
          }

          if (getSessionOptions().implicitPipeliningEnabled() &&
              Pipeline::inplaceRecomputationConflict(
                  op, in_index.first, out_index.first)) {
            logging::pattern::trace(
                "[Inplacing] Not inplacing {} with {} due to "
                "an inplace recomputation conflict between "
                "{} and {} ",
                op->debugName(),
                inplaceOp->opid,
                in_index.second->id,
                out_index.second->id);
            inplaceBlocking = true;
          }

          if (!inplaceBlocking && (restoreInplaceIn || restoreInplaceOut)) {
            logging::pattern::trace(
                "[Inplacing] Inplacing of {} with {} not blocked, but an {} "
                "tensor is a restore inplace tensor (alias).",
                op->debugName(),
                inplaceOp->opid,
                (restoreInplaceIn && restoreInplaceOut)
                    ? "input/output"
                    : (restoreInplaceIn ? "input" : "output"));
          }
        }
      }

      if (inplaceBlocking) {
        popMem.g.backoutOpening(proposal);
        continue;
      }

      // Next we prevent inplacing where aliased inputs that would be written to
      // result in a potential race condition. Due to inplacing priority order,
      // we need to cover two cases:
      //
      // 1) Downstream op mustn't be inplaced due to would-be-written-to, alised
      // inputs as a result of previous inplacing of some upstream op.
      //
      // 2) Upstream op mustn't be inplaced because its inplacing would result
      // in a potential race condition in an already inplaced downstream op.

      // Case 1): conservatively prevent inplacing if any changed input tensor
      // is aliased by any other input tensor.
      for (const auto &in_tensor0 : op->input->tensorMap()) {
        if (inplaceBlocking) {
          break;
        }
        if (!inplaceOp->modifiesIndex(in_tensor0.first)) {
          continue;
        }
        auto aliases = popMem.allAliases(*in_tensor0.second);
        for (const auto &in_tensor1 : op->input->tensorMap()) {
          if (in_tensor0.first == in_tensor1.first) {
            continue;
          }
          if (std::find(aliases.begin(), aliases.end(), in_tensor1.second) !=
              aliases.end()) {
            logging::pattern::trace(
                "[Inplacing] Not inplacing {} with {} due to input "
                "{} being an alias of {} which would be changed inplace.",
                op->debugName(),
                inplaceOp->opid,
                in_tensor1.second->id,
                in_tensor0.second->id);
            inplaceBlocking = true;
            break;
          }
        }
      }

      if (inplaceBlocking) {
        popMem.g.backoutOpening(proposal);
        continue;
      }

      // Case 2): if, after inplacing, any of input tensor aliases end up as
      // inputs (more than one of them) to an op that modifies at least one of
      // those aliased inputs, we introduce a potential race condition.

      // We first identify disjoint sets of input/output tensors
      // (insOutsToBeAliased) that would be aliased if we did inplacing. We
      // do this in order to predict the effect of inplacing in the current
      // graph where inplacing has actually not been done yet.
      std::set<std::vector<Tensor *>, VectorPTensorCmp> insOutsToBeAliased;
      for (const auto &in_tensor : op->input->tensorMap()) {
        std::vector<Tensor *> currentInsOuts{in_tensor.second};
        auto aliases = popMem.allAliases(*in_tensor.second);
        for (const auto &out_tensor : op->output->tensorMap()) {
          if (std::find(aliases.begin(), aliases.end(), out_tensor.second) !=
              aliases.end()) {
            currentInsOuts.push_back(out_tensor.second);
          }
        }
        if (currentInsOuts.size() > 1) {
          insOutsToBeAliased.insert(currentInsOuts);
        }
      }

      // Look at all aliases of all disjoint input/output sets and detect cases
      // where more than one of them end up as inputs to the same op and at
      // least one of them is changed by that op.
      for (const auto &currentInsOuts : insOutsToBeAliased) {
        if (inplaceBlocking) {
          break;
        }
        std::map<Op *, std::set<InIndex>, POpCmp> consumersInIndices;

        auto populateConsumersInIndices = [&consumersInIndices, op](Tensor *t) {
          for (auto consumer : t->consumers.getOps()) {
            if (consumer == op) {
              continue;
            }
            consumersInIndices.insert({consumer, {}});
            const auto &in_indices = consumer->input->indices(t);
            consumersInIndices.at(consumer).insert(in_indices.begin(),
                                                   in_indices.end());
          }
          return false;
        };

        for (auto tensor : currentInsOuts) {
          tensor->anyAlias(populateConsumersInIndices);
        }

        for (const auto &consumerInIndices : consumersInIndices) {
          if (consumerInIndices.second.size() <= 1) {
            continue;
          }
          if (std::any_of(consumerInIndices.second.begin(),
                          consumerInIndices.second.end(),
                          [&consumerInIndices](InIndex i) {
                            return consumerInIndices.first->modifiesIndex(i);
                          })) {
            logging::pattern::trace(
                "[Inplacing] Not inplacing {} with {} as doing so would "
                "introduce a potential race condition in a downstream op {} "
                "which is already inplace.",
                op->debugName(),
                inplaceOp->opid,
                consumerInIndices.first->debugName());
            inplaceBlocking = true;
            break;
          }
        }
      }

      if (inplaceBlocking) {
        popMem.g.backoutOpening(proposal);
        continue;
      }

      // finally, we check if there are cycles with the new topological
      // constraints
      const bool isPhased =
          (userOptions.virtualGraphMode == VirtualGraphMode::ExecutionPhases);
      if (!newTopoCons.empty() && !graph.isSchedulable(newTopoCons, isPhased)) {
        std::ostringstream oss;
        oss << "[Inplacing] The new topological constraints prevent Op "
            << op->id << " from being inplaced, as they would created a cycle ";
        logging::pattern::debug(oss.str());
        popMem.g.backoutOpening(proposal);
        continue;
      }

      {
        std::ostringstream oss;
        oss << "[Inplacing] Inplacing Op " << op->str();

        if (op->output->n() != 1) {
          throw error("no support for inplacing ops with n-outputs != 1, this "
                      "for Op {} ",
                      op->str());
        }
        const auto opOutput = op->output->tensorMap().cbegin()->second;

        logging::pattern::debug(oss.str());
        inplacedAlready.insert(op->id);

        inplace.apply(op, identifier, newTopoCons);

        popMem.g.completeOpening(result);
        // The Op in graph has changed, mirror the change in the poprithms
        // Graph
        popMem.update(id, opOutput->getProducer()->id);
      }
    }
  }
  logging::pattern::trace("Completed Inplacing Pattern");
}

Op &Ir::getSubgraphAnchorPlaceholder() {
  static std::unique_ptr<Op> subgraphAnchorPlaceholder = std::unique_ptr<Op>(
      new PlaceholderOp({"TempAnchorDomain", "TempAnchorType", 1},
                        Op::Settings{getMainGraph(), "TempAnchorName"}));

  return *subgraphAnchorPlaceholder.get();
}

std::set<TensorId> Ir::getAllTensorIds() const {
  std::set<TensorId> result;

  for (const auto &id_graph : graphs) {
    const Graph *graph              = id_graph.second.get();
    const std::vector<TensorId> ids = graph->getTensors().getAllTensorIds();
    result.insert(ids.begin(), ids.end());
  }

  return result;
}

std::vector<TensorId> Ir::getTensorIds(TensorType tensor_type) const {
  std::vector<TensorId> result;

  for (auto &id_graph : graphs) {
    auto graph = id_graph.second.get();
    auto ids   = graph->getTensors().getIds(tensor_type);
    result.reserve(result.size() + ids.size());
    result.insert(result.end(), ids.begin(), ids.end());
  }

  return result;
}

Tensor *Ir::getTensor(const TensorId &tensor_id) const {
  TensorId id = tensor_id;

  for (const Graph *graph : getAllGraphs()) {
    if (graph->getTensors().contains(id)) {
      return graph->getTensors().get(id);
    }
  }

  throw error("No Ir::Tensor with TensorId '" + tensor_id +
              "' in Ir::getTensor(..) ");
}

bool Ir::containsTensor(const TensorId &tensor_id) const {
  TensorId id = tensor_id;

  for (const Graph *graph : getAllGraphs()) {
    if (graph->getTensors().contains(id)) {
      return true;
    }
  }

  return false;
}

std::vector<TensorId> Ir::getGraphInputIds() const {
  std::vector<TensorId> result;

  for (auto &id_graph : graphs) {
    auto graph = id_graph.second.get();
    auto &ids  = graph->getInputIds();
    result.reserve(result.size() + ids.size());
    result.insert(result.end(), ids.begin(), ids.end());
  }

  return result;
}

std::vector<TensorId> Ir::getGraphOutputIds() const {
  std::vector<TensorId> result;
  for (auto &id_graph : graphs) {
    auto graph = id_graph.second.get();
    auto &ids  = graph->getOutputIds();
    result.reserve(result.size() + ids.size());
    result.insert(result.end(), ids.begin(), ids.end());
  }

  return result;
}

const Tensors &Ir::getTensors() const { return getMainGraph().getTensors(); }
Tensors &Ir::getTensors() { return getMainGraph().getTensors(); }

std::map<TensorId, Tensor *> Ir::getAllTensors() const {
  std::map<TensorId, Tensor *> allTensors;
  for (const Graph *graph : getAllGraphs()) {
    auto ids = graph->getTensors().getAllTensorIds();
    for (auto id : ids) {
      allTensors.insert({id, graph->getTensors().get(id)});
    }
  }
  return allTensors;
}

const Graph &Ir::getMainGraph() const { return getGraph(GraphId::root()); }
Graph &Ir::getMainGraph() { return getGraph(GraphId::root()); }

Graph &Ir::getGraph(const GraphId &graphId) const {
  if (graphs.find(graphId) != graphs.end()) {
    return *graphs.at(graphId);
  } else {
    throw error("Graph not found for GraphId {}, IR id {}", graphId, this->id);
  }
}

void Ir::setMainGraphPathFromLoss() {
  // All Ops and Tensors at this point with a reserved gradient prefix have a
  // path from the final Loss (before any Patterns and Transformations). After
  // Patterns, this is no longer true as names get mangled.
  for (auto &id_op : getMainGraph().getOps()) {
    Op *op = id_op.second.get();
    for (auto inArr : op->input->tensors()) {
      if (inArr->id.find(reservedGradientPrefix()) != std::string::npos) {
        inArr->fromLoss = PathFromLoss::Yes;
        op->fromLoss    = PathFromLoss::Yes;
      }
    }
    for (auto outArr : op->output->tensors()) {
      if (outArr->id.find(reservedGradientPrefix()) != std::string::npos) {
        outArr->fromLoss = PathFromLoss::Yes;
        op->fromLoss     = PathFromLoss::Yes;
      }
    }
  }
}

std::vector<const Graph *> Ir::getAllGraphs() const {
  std::vector<const Graph *> allGraphs;
  for (auto &id_graph : graphs) {
    allGraphs.push_back(id_graph.second.get());
  }
  return allGraphs;
}

bool Ir::hasGraph(const GraphId &graphId) const {
  return graphs.find(graphId) != graphs.end();
}

Graph &Ir::createGraph(const GraphId &graphId) {
  logging::ir::trace("Creating Graph with id \"{}\"", graphId);
  auto found = graphs.find(graphId);
  if (found != graphs.end()) {
    throw error("Graph({}) is already in Ir", graphId);
  }

  graphs.insert({graphId, std::make_unique<Graph>(*this, graphId)});
  return getGraph(graphId);
}

void Ir::removeGraph(const GraphId &graphId) { graphs.erase(graphId); }

std::map<OpId, std::unique_ptr<Op>> &Ir::getMainGraphOps() {
  return getMainGraph().getOps();
}

const std::map<OpId, std::unique_ptr<Op>> &Ir::getMainGraphOps() const {
  return getMainGraph().getOps();
}

std::vector<Op *> Ir::getAllOps() const {
  std::vector<Op *> ops;
  for (auto &graph : graphs) {
    ops.reserve(ops.size() + graph.second->getOps().size());
    for (auto &op : graph.second->getOps()) {
      ops.push_back(op.second.get());
    }
  }
  return ops;
}

Op *Ir::getOp(OpId opId) const {
  for (auto graph : getAllGraphs()) {
    // This works because opId is unique in the whole IR
    auto op = graph->getOpUnsafe(opId);
    if (op != nullptr) {
      return op;
    }
  }
  throw error("[Ir::getOp] Op {} not in IR.", opId);
}

Tensors &Ir::getMainGraphTensors() { return getMainGraph().getTensors(); }

const Tensors &Ir::getMainGraphTensors() const {
  return getMainGraph().getTensors();
}

RandomReferenceId Ir::getAndIncrementRandomReferenceId() {
  randomReferenceId += 1;
  return randomReferenceId;
}

TensorId Ir::getOrSetRandomReferenceTensor(RandomReferenceId id,
                                           TensorId defaultTensor) {
  if (randomReferenceTensorMap.find(id) == randomReferenceTensorMap.end()) {
    randomReferenceTensorMap[id] = defaultTensor;
  }
  return randomReferenceTensorMap[id];
}

void Ir::mergeRandomReferenceIds(std::set<RandomReferenceId> &ids) {
  if (ids.size() < 2) {
    return;
  }
  auto to = *ids.begin();
  for (auto op : getAllOps()) {
    auto dropout = dynamic_cast<DropoutOp *>(op);
    if (dropout && ids.find(dropout->getReferenceId()) != ids.end()) {
      dropout->setReferenceId(to);
    }
  }
}

void Ir::setRemoteBufferInfo(RemoteBufferId id, RemoteBufferInfo info) {
  remoteBufferInfoMap[id] = info;
}

const RemoteBufferInfo Ir::getRemoteBufferInfo(RemoteBufferId id) const {
  if (remoteBufferInfoMap.find(id) == remoteBufferInfoMap.end()) {
    throw error("RemoteBufferId {} not found in the remoteBufferInfoMap.", id);
  }
  return remoteBufferInfoMap.at(id);
}

const std::map<RemoteBufferId, RemoteBufferInfo>
Ir::getAllRemoteBufferInfos() const {
  return remoteBufferInfoMap;
}

TensorId Ir::createIntermediateTensorId(const TensorId &base_id) {
  auto temp_id =
      logging::format("{}__t{}", base_id, intermediate_tensor_counter);
  logging::ir::trace("Generating tensor id {}", temp_id);
  ++intermediate_tensor_counter;
  return temp_id;
}

TensorId Ir::createSliceTensorId(TensorId base_id, unsigned s, unsigned e) {
  auto slice_id = logging::format(
      "{}__s{}_{}_{}", base_id, s, e, intermediate_tensor_counter);
  logging::ir::trace("Generating tensor id {}", slice_id);
  ++intermediate_tensor_counter;
  return slice_id;
}

TensorId Ir::createConcatTensorId(TensorId base_id) {
  auto concat_id =
      logging::format("{}__cc{}", base_id, intermediate_tensor_counter);
  logging::ir::trace("Generating tensor id {}", concat_id);
  ++intermediate_tensor_counter;
  return concat_id;
}

GraphId Ir::createUniqueSubgraphId(GraphId base_id) {
  auto next_id =
      logging::format("{}_subgraph({})", base_id, subgraph_id_counter);
  ++subgraph_id_counter;
  return next_id;
}

std::vector<std::vector<Op *>>
Ir::getAccumulateOuterFragmentBinConstraints(const Graph &graph) const {
  auto &mainGraph = getMainGraph();

  if (&graph == &mainGraph) {
    // Only add bin constraints for main graph.
    AccumulateOuterFragmentParallelizer t;
    return t.getBinConstraints(graph);
  } else {
    // Return unconstrained.
    return std::vector<std::vector<Op *>>();
  }
}

size_t Ir::getHash() const {
  if (!hash_.has_value()) {
    throw error("Attempting to get Ir hash value when it hasn't been set.");
  }

  return hash_.value();
}

size_t Ir::getIrBundleHash() const { return irBundleHash; }

void Ir::setIrBundleHash(size_t v) { irBundleHash = v; }

namespace {

/**
 * Clone ops from the original graph and create tensors in the cloned graph.
 *
 * \param originalGraph The original graph to clone from
 * \param clonedGraph   The graph which is under cloning
 * \param maps          The map of original and cloned IDs to modify
 * */
void cloneOpsAndAddTensors(Graph &originalGraph,
                           Graph &clonedGraph,
                           ClonedGraphMaps &maps) {
  // Don't need the optimal schedule as any valid order would suffice to get the
  // tensors in the correct order
  auto scheduledOps =
      originalGraph.getOpSchedule({}, RequireOptimalSchedule::No);

  for (const auto op : scheduledOps) {
    // Clone the operator
    auto clonedOpUp = op->clone();

    // Change ownership of the cloned operator after obtaining the raw pointer
    auto clonedOp = clonedOpUp.get();
    clonedGraph.moveIntoGraph(std::move(clonedOpUp));

    // Change scope of the clonedOp so that it is no longer a part of the old
    // graph
    clonedOp->settings.scope = clonedGraph.getScope();

    maps.opIdMap[op->id]       = clonedOp->id;
    maps.opIdMap[clonedOp->id] = op->id;

    clonedOp->disconnectAllInputs();
    clonedOp->disconnectAllOutputs();

    // First we clone the input tensors
    auto tensorInputMap = op->input->tensorMap();
    for (const auto indexAndTensor : tensorInputMap) {
      auto index  = indexAndTensor.first;
      auto tensor = indexAndTensor.second;

      TensorId clonedInputTensorId = maps.tensorIdMap.at(tensor->id);

      // Attach to the new tensor to the cloned op
      clonedOp->connectInTensorLike(op, index, clonedInputTensorId);
    }
    // Then we clone the output tensors
    auto tensorOuputMap = op->output->tensorMap();
    for (const auto indexAndTensor : tensorOuputMap) {
      auto index  = indexAndTensor.first;
      auto tensor = indexAndTensor.second;
      // We remove the inner loop scope from the tensor
      auto clonedOutputTensorId =
          addScope(clonedGraph, removeScope(originalGraph, tensor->id));
      // Create the tensor with the tensorId made above

      clonedOp->createAndConnectOutTensor(index, clonedOutputTensorId);

      maps.tensorIdMap[tensor->id]           = clonedOutputTensorId;
      maps.tensorIdMap[clonedOutputTensorId] = tensor->id;
    }
    // Propagate tensor info
    clonedOp->setup();
  }
}

} // namespace

ClonedGraphMaps Ir::cloneGraph(GraphId originalGraphId, GraphId newGraphId) {
  ClonedGraphMaps maps;

  auto &originalGraph = getGraph(originalGraphId);
  auto &clonedGraph   = createGraph(newGraphId);

  // Add input to the graph
  auto graphInputTensorId = originalGraph.getInputIds();
  for (const auto &tensorId : graphInputTensorId) {
    auto tensorInfo = originalGraph.getTensors().get(tensorId)->info;
    auto clonedTensorId =
        addScope(clonedGraph, removeScope(originalGraph, tensorId));
    clonedGraph.addInput(clonedTensorId, tensorInfo);
    maps.tensorIdMap[tensorId]       = clonedTensorId;
    maps.tensorIdMap[clonedTensorId] = tensorId;
  }

  // Constants
  for (const auto tensor :
       originalGraph.getTensors().getOfType(TensorType::Const)) {
    auto clonedTensorId =
        addScope(clonedGraph, removeScope(originalGraph, tensor->id));
    clonedGraph.getTensors().addConstInit(
        clonedTensorId, tensor->info, tensor->tensorData()->data(), {});
    maps.tensorIdMap[tensor->id]     = clonedTensorId;
    maps.tensorIdMap[clonedTensorId] = tensor->id;
  }

  cloneOpsAndAddTensors(originalGraph, clonedGraph, maps);

  // Add output to the graph
  auto graphOutputTensorId = originalGraph.getOutputIds();
  for (const auto &tensorId : graphOutputTensorId) {
    auto tensorInfo = originalGraph.getTensors().get(tensorId)->info;
    auto clonedTensorId =
        addScope(clonedGraph, removeScope(originalGraph, tensorId));
    clonedGraph.markAsOutput(clonedTensorId);
  }

  // Topological constraints
  for (const auto &originalOpAndTopoOpSet :
       originalGraph.topoCons->getValsAfter()) {
    auto originalBeforeOp   = originalOpAndTopoOpSet.first;
    auto &originalTopoOpSet = originalOpAndTopoOpSet.second;

    auto clonedBeforeOp =
        clonedGraph.getOp(maps.opIdMap.at(originalBeforeOp->id));

    for (const auto &topoOp : originalTopoOpSet) {
      auto clonedAfterOp = clonedGraph.getOp(maps.opIdMap.at(topoOp.op->id));

      clonedGraph.topoCons->insert(clonedBeforeOp, clonedAfterOp, topoOp.tied);
    }
  }
  return maps;
}

} // namespace popart

namespace std {

std::size_t std::hash<popart::Ir>::operator()(const popart::Ir &ir) const {
  // Hash based on all the IR attributes that
  // can affect compiled program
  size_t seed = 0;

  std::stringstream ss;
  ir.append(ss);

  boost::hash_combine(seed, ss.str());
  boost::hash_combine(seed, ir.getIrBundleHash());

  return seed;
}

std::size_t
std::hash<popart::IrBundle>::operator()(const popart::IrBundle &bundle) const {
  size_t seed = 0;

  boost::hash_combine(
      seed, std::hash<popart::InputShapeInfo>()(bundle.inputShapeInfo));
  boost::hash_combine(seed, std::hash<popart::DataFlow>{}(bundle.dataFlow));
  boost::hash_combine(seed, bundle.loss);

  if (bundle.optimizer) {
    boost::hash_combine(seed,
                        std::hash<popart::Optimizer *>()(bundle.optimizer));
  }
  boost::hash_combine(seed, std::hash<popart::DeviceInfo>()(bundle.deviceInfo));
  boost::hash_combine(seed,
                      std::hash<popart::SessionOptions>{}(bundle.userOptions));
  boost::hash_combine(seed, std::hash<popart::Patterns>()(bundle.patterns));
  const std::string poplarHash = poplar::packageHash();
  boost::hash_combine(seed, poplarHash);

  return seed;
}

} // namespace std
