// Copyright (c) 2020 Graphcore Ltd. All rights reserved.
#include <vector>

#include <boost/algorithm/string.hpp>

#include <popart/graph.hpp>
#include <popart/ir.hpp>
#include <popart/op/accumulate.hpp>
#include <popart/op/adamupdater.hpp>
#include <popart/op/adamvarupdate.hpp>
#include <popart/op/div.hpp>
#include <popart/op/ipucopy.hpp>
#include <popart/op/max.hpp>
#include <popart/op/mul.hpp>
#include <popart/op/reducesumsquare.hpp>
#include <popart/op/scale.hpp>
#include <popart/op/scaledadd.hpp>
#include <popart/op/sgd0varupdate.hpp>
#include <popart/op/sgd1varupdate.hpp>
#include <popart/op/slice.hpp>
#include <popart/op/sqrt.hpp>
#include <popart/op/sum.hpp>
#include <popart/opidentifier.hpp>
#include <popart/transforms/clipweightgradientsbynorm.hpp>
#include <popart/util.hpp>

namespace popart {

namespace {

// Helper method for creating new ops.
// Taken from Pattern::transferBaseProperties.
void transferBaseProperties(Op *from, Op *to) {
  if (from->hasVirtualGraphId()) {
    to->setVirtualGraphId(from->getVirtualGraphId());
  }
  if (from->hasExecutionPhase()) {
    to->setExecutionPhase(from->getExecutionPhase());
  }
  if (from->hasPipelineStage()) {
    to->setPipelineStage(from->getPipelineStage());
  }
  if (from->hasBatchSerializedPhase()) {
    to->setBatchSerializedPhase(from->getBatchSerializedPhase());
  }

  to->settings.scope            = from->settings.scope;
  to->settings.recomputeType    = from->settings.recomputeType;
  to->settings.tensorLocation   = from->settings.tensorLocation;
  to->fromLoss                  = from->fromLoss;
  to->toLoss                    = from->toLoss;
  to->settings.schedulePriority = from->settings.schedulePriority;
}

ExecutionContext decideExecutionContext(Graph &graph) {
  auto &ir   = graph.getIr();
  auto &opts = ir.getSessionOptions();

  if (opts.enableGradientAccumulation) {
    return ExecutionContext::AccumulateOuterFragment;
  } else {
    return ExecutionContext::Normal;
  }
}

Tensor *addReduceSumSquare(Tensor *grad, Graph &graph) {
  logging::debug("addReduceSumSuareOp({}, graph)", grad->id);

  Op::Settings settings(graph, "", {});
  settings.executionContext = decideExecutionContext(graph);

  nonstd::optional<std::vector<int64_t>> axes;
  Op *reduction = graph.createOp<ReduceSumSquareOp>(
      Onnx::AiOnnx::OpSet11::ReduceSumSquare, axes, false, settings);

  transferBaseProperties(grad->consumers.getOps().at(0), reduction);

  auto &ir = graph.getIr();

  auto clippedGradId =
      logging::format("{}_{}", getBaseTensorId(grad->id), "clipping");

  reduction->connectInTensor(ReduceSumSquareOp::getInIndex(), grad->id);
  reduction->createAndConnectOutTensor(
      ReduceSumSquareOp::getOutIndex(),
      ir.createIntermediateTensorId(clippedGradId));
  reduction->setup();
  return reduction->outTensor(ReduceSumSquareOp::getOutIndex());
}

// Return a consuming var update op for each tensor in weightIds.
std::vector<Op *> getVarUpdates(Graph &graph,
                                const std::vector<TensorId> &weightIds) {
  auto getVarUpdate = [](Tensor *t) -> std::vector<Op *> {
    logging::debug("Getting var updates for {}", t->id);
    std::vector<Op *> result;
    for (auto op : t->consumers.getOps()) {
      if (op->isConvertibleTo<SGD0VarUpdateOp>() ||
          op->isConvertibleTo<SGD1VarUpdateOp>() ||
          op->isConvertibleTo<AdamVarUpdateOp>()) {
        result.push_back(op);
      } else if (op->isConvertibleTo<SliceInplaceOp>()) {
        // SerializeMatMuls transform can insert an inplace slice between the
        // weight and the var update.
        for (auto x : op->getFollowingOps(SliceInplaceOp::getOutIndex())) {
          if (x->isConvertibleTo<SGD0VarUpdateOp>() ||
              x->isConvertibleTo<SGD1VarUpdateOp>() ||
              x->isConvertibleTo<AdamVarUpdateOp>()) {
            result.push_back(x);
          }
        }
      }
    }
    if (result.empty()) {
      throw error("Could not find a varupdate op for tensor {}", t->id);
    }
    return result;
  };

  std::vector<Op *> varUpdates;
  for (auto &tid : weightIds) {
    auto tensor           = graph.getTensors().get(tid);
    auto tensorVarUpdates = getVarUpdate(tensor);
    for (auto vu : tensorVarUpdates) {
      varUpdates.push_back(vu);
    }
  }

  return varUpdates;
}

Tensor *createCopyOnVGraph(Tensor *t, VGraphId destination, Graph &graph) {
  auto &ir = graph.getIr();

  Op::Settings settings(graph, "", {});
  settings.executionContext = decideExecutionContext(graph);

  Op *op = graph.createOp<IpuCopyOp>(
      Onnx::CustomOperators::IpuCopy, destination, settings);
  IpuCopyOp *ipuCopy = dynamic_cast<IpuCopyOp *>(op);
  transferBaseProperties(t->getProducer(), op);

  ipuCopy->connectInTensor(0, t->id, t->getVirtualGraphId());
  ipuCopy->createAndConnectOutTensor(0, ir.createIntermediateTensorId(t->id));
  ipuCopy->setup();

  return ipuCopy->outTensor(0);
}

std::vector<Tensor *> copyToSameVGraph(const std::vector<Tensor *> ts,
                                       VGraphId destination,
                                       Graph &graph) {
  auto vGraphId = 0;
  std::vector<Tensor *> result;

  for (auto t : ts) {
    if (t->getVirtualGraphId() == destination) {
      result.push_back(t);
    } else {
      auto x = createCopyOnVGraph(t, vGraphId, graph);
      result.push_back(x);
    }
  }

  return result;
}

// globalNorm = sqrt(sum(gradNorms))
Tensor *createGlobalNorm(int clipGroupIndex,
                         std::vector<Tensor *> gradNorms,
                         Graph &graph) {
  Op::Settings settings(graph, "", {});
  settings.executionContext = decideExecutionContext(graph);

  Op *sum = graph.createOp<SumOp>(Onnx::AiOnnx::OpSet8::Sum, settings);
  transferBaseProperties(gradNorms.at(0)->getProducer(), sum);

  for (int i = 0; i < gradNorms.size(); i++) {
    auto gradNorm = gradNorms.at(i);
    sum->connectInTensor(i, gradNorm->id);
  }

  auto &ir = graph.getIr();
  sum->createAndConnectOutTensor(SumOp::getOutIndex(),
                                 ir.createIntermediateTensorId("normsSum"));
  sum->setup();

  Op *sqrt = graph.createOp<SqrtOp>(Onnx::AiOnnx::OpSet6::Sqrt, settings);
  transferBaseProperties(gradNorms.at(0)->getProducer(), sqrt);

  sqrt->connectInTensor(SqrtOp::getInIndex(), sum->outId(SumOp::getOutIndex()));
  sqrt->createAndConnectOutTensor(
      SqrtOp::getOutIndex(),
      logging::format("{}{}", reservedGlobalNormPrefix(), clipGroupIndex));
  sqrt->setup();

  return sqrt->outTensor(SqrtOp::getOutIndex());
}

void addClipByNorm(Tensor *grad, Tensor *clipFactor, Graph &graph) {
  auto &ir = graph.getIr();

  Op::Settings settings(graph, "", {});
  settings.executionContext = decideExecutionContext(graph);

  Op *mulOp = graph.createOp<MulOp>(Onnx::AiOnnx::OpSet6::Mul, settings);
  transferBaseProperties(grad->consumers.getOps().at(0), mulOp);

  auto clipFactorId = clipFactor->id;

  if (mulOp->hasVirtualGraphId() &&
      mulOp->getVirtualGraphId() != clipFactor->getVirtualGraphId()) {
    auto destIpu          = mulOp->getVirtualGraphId();
    auto copiedClipFactor = createCopyOnVGraph(clipFactor, destIpu, graph);
    clipFactorId          = copiedClipFactor->id;
  }

  auto clippedGradId =
      logging::format("{}_{}", getBaseTensorId(grad->id), "clipped");

  mulOp->connectInTensor(MulOp::getArg0InIndex(), grad->id);
  mulOp->connectInTensor(MulOp::getArg1InIndex(), clipFactorId);
  mulOp->createAndConnectOutTensor(
      MulOp::getOutIndex(), ir.createIntermediateTensorId(clippedGradId));
  mulOp->setup();

  std::stringstream ss;
  ss << "Consumers of " << grad->id << " are:";
  for (auto op : grad->consumers.getOps()) {
    ss << "\n  " << op->str();
  }
  logging::debug("{}", ss.str());

  for (auto op : grad->consumers.getOps()) {
    if (op != mulOp && !op->isConvertibleTo<ReduceSumSquareOp>()) {
      auto indices = op->input->indices(grad);
      for (auto idx : indices) {
        op->disconnectInTensor(idx);
        op->connectInTensor(idx, mulOp->outId(MulOp::getOutIndex()));
      }
    }
  }
}

void addClipByNorms(std::vector<Tensor *> grads,
                    Tensor *clipFactor,
                    Graph &graph) {
  for (auto grad : grads) {
    addClipByNorm(grad, clipFactor, graph);
  }
}

// clip_factor = clip_norm / max(clip_norm, global_norm)
Tensor *createClipFactor(Tensor *globalNorm, Tensor *clipNorm, Graph &graph) {
  auto &ir = graph.getIr();

  Op::Settings settings(graph, "", {});
  settings.executionContext = decideExecutionContext(graph);

  Op *maxOp = graph.createOp<MaxOp>(Onnx::AiOnnx::OpSet6::Max, settings);
  transferBaseProperties(globalNorm->getProducer(), maxOp);
  maxOp->connectInTensor(0, globalNorm->id);
  maxOp->connectInTensor(1, clipNorm->id);
  maxOp->createAndConnectOutTensor(MaxOp::getOutIndex(),
                                   ir.createIntermediateTensorId("clipByNorm"));
  maxOp->setup();

  Op *divOp = graph.createOp<DivOp>(Onnx::AiOnnx::OpSet6::Div, settings);
  transferBaseProperties(maxOp, divOp);
  divOp->connectInTensor(DivOp::getArg0InIndex(), clipNorm->id);
  divOp->connectInTensor(DivOp::getArg1InIndex(),
                         maxOp->outId(MaxOp::getOutIndex()));
  divOp->createAndConnectOutTensor(DivOp::getOutIndex(),
                                   ir.createIntermediateTensorId("clipByNorm"));
  divOp->setup();

  return divOp->outTensor(DivOp::getOutIndex());
}

// Take the maxNorm and create the clipNorm tensor.
Tensor *createClipNorm(float maxNorm, Graph &graph) {
  auto &ir          = graph.getIr();
  auto clipByNormId = ir.createIntermediateTensorId("clipByNorm");
  TensorInfo info{DataType::FLOAT, {}};
  std::vector<float> data{maxNorm};

  graph.getTensors().addConstInit(clipByNormId, info, data.data());
  return graph.getTensors().get(clipByNormId);
}

std::vector<Tensor *> getGrads(Graph &graph,
                               const std::vector<TensorId> &weightIds) {
  std::vector<Tensor *> result;

  auto varUpdates = getVarUpdates(graph, weightIds);
  for (auto op : varUpdates) {
    if (op->isConvertibleTo<SGD0VarUpdateOp>() ||
        op->isConvertibleTo<SGD1VarUpdateOp>()) {
      auto grad = op->inTensor(VarUpdateWithUpdaterOp::getUpdaterInIndex());
      result.push_back(grad);
    } else if (op->isConvertibleTo<AdamVarUpdateOp>()) {
      auto adamUpdater =
          op->inTensor(VarUpdateWithUpdaterOp::getUpdaterInIndex())
              ->getProducer();
      if (!adamUpdater->isConvertibleTo<AdamUpdaterOp>()) {
        throw internal_error("This should be a AdamUpdaterOp.");
      }
      auto accl1 = adamUpdater->inTensor(AdamUpdaterOp::getAccl1InIndex())
                       ->getProducer();
      if (!accl1->isConvertibleTo<AccumulateOp>()) {
        throw internal_error("These should be AccumulateOps.");
      }
      auto grad = accl1->inTensor(AccumulateOp::getUpdaterInIndex());
      result.push_back(grad);
    } else {
      throw internal_error("Unable to handle op {}", op->str());
    }
  }

  return result;
}

void clipWeightGradientsByNorm(int clipGroupIndex,
                               const std::vector<TensorId> &weightIds,
                               float maxNorm,
                               Graph &graph) {
  auto grads = getGrads(graph, weightIds);

  std::vector<Tensor *> gradNorms;
  for (auto grad : grads) {
    auto gradNorm = addReduceSumSquare(grad, graph);
    gradNorms.push_back(gradNorm);
  }

  auto &ir   = graph.getIr();
  auto &opts = ir.getSessionOptions();
  if (opts.enablePipelining) {
    gradNorms = copyToSameVGraph(gradNorms, 0, graph);
  }

  auto globalNorm = createGlobalNorm(clipGroupIndex, gradNorms, graph);
  auto clipNorm   = createClipNorm(maxNorm, graph);
  auto clipFactor = createClipFactor(globalNorm, clipNorm, graph);
  addClipByNorms(grads, clipFactor, graph);
}

// Find all the gradient clipping ops linked to the `globalNormProducer`.
std::vector<Op *> findGradientClippingOps(Op *globalNormProducer) {
  if (!globalNormProducer->isConvertibleTo<SqrtOp>()) {
    throw internal_error("Global norm op should be a SqrtOp.");
  }
  auto sum = globalNormProducer->getPrecedingOp<SumOp>(SqrtOp::getInIndex());

  std::vector<Op *> result{globalNormProducer, sum};

  // The sums inputs are the ReduceSumSquareOps.
  // These might go through an IpuCopyOp
  for (auto &index_tensor : sum->input->tensorMap()) {
    auto index = index_tensor.first;
    auto x     = sum->getPrecedingOp(index);
    if (x->isConvertibleTo<IpuCopyOp>()) {
      result.push_back(x);
      x = x->getPrecedingOp(0);
    }

    if (!x->isConvertibleTo<ReduceSumSquareOp>()) {
      throw error("Unexpected op {}. Expected ReduceSumSquareOp here.",
                  x->debugName());
    }
    result.push_back(x);
  }

  // Add the clip factor ops
  auto maxOp = globalNormProducer->getFollowingOp<MaxOp>();
  result.push_back(maxOp);
  auto divOp = maxOp->getFollowingOp<DivOp>();
  result.push_back(divOp);

  // Finally add the MulOp that does the scaling
  for (auto x : divOp->getFollowingOps()) {
    if (x->isConvertibleTo<IpuCopyOp>()) {
      result.push_back(x);
      x = x->getFollowingOp();
    }

    if (x->isConvertibleTo<MulOp>() || x->isConvertibleTo<MulLhsInplaceOp>() ||
        x->isConvertibleTo<MulRhsInplaceOp>()) {
      result.push_back(x);
    } else {
      throw error("Expected a MulOp following the clip factor, found op {}",
                  x->debugName());
    }
  }

  return result;
}

} // namespace

std::size_t ClipWeightGradientsByNorm::id() {
  return typeid(ClipWeightGradientsByNorm).hash_code();
}

bool ClipWeightGradientsByNorm::apply(Graph &graph) const {
  auto &ir        = graph.getIr();
  auto &optimizer = ir.getOptimizer();
  auto &opts      = ir.getSessionOptions();

  if (opts.enablePipelining && opts.accumulateOuterFragmentSettings.schedule ==
                                   AccumulateOuterFragmentSchedule::Serial) {
    throw error(
        "Incompatible accumulateOuterFragmentSchedule used with gradient "
        "clipping, SessionOptions::accumulateOuterFragmentSettings.schedule "
        "can not be set to AccumulateOuterFragmentSchedule::Serial");
  }

  auto &clipNormSettings = optimizer.getClipNormSettings();
  for (int clipNormIndex = 0; clipNormIndex < clipNormSettings.size();
       clipNormIndex++) {
    auto &clipGroup = clipNormSettings.at(clipNormIndex);
    clipWeightGradientsByNorm(
        clipNormIndex, clipGroup.weightIds, clipGroup.maxNorm, graph);
  }

  return true;
}

std::vector<std::vector<Op *>>
ClipWeightGradientsByNorm::findGradientClippingGroups(const Graph &graph) {
  using boost::algorithm::starts_with;

  // Find all the global norm tensors and get their producers.
  std::vector<Op *> globalNorms;
  for (auto tid : graph.getTensors().getIds(TensorType::ActGrad)) {
    if (starts_with(tid, reservedGlobalNormPrefix())) {
      auto t = graph.getTensors().get(tid);
      globalNorms.push_back(t->getProducer());
    }
  }

  // If no global norms were found, there are no gradient clipping ops in the
  // graph.
  if (globalNorms.size() == 0) {
    return {};
  }

  std::vector<std::vector<Op *>> result;
  for (auto x : globalNorms) {
    result.push_back(findGradientClippingOps(x));
  }
  return result;
}

namespace {
bool init = Transform::registerTransform(new ClipWeightGradientsByNorm);
} // namespace

} // namespace popart
