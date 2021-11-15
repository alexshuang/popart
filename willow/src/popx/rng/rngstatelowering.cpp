// Copyright (c) 2021 Graphcore Ltd. All rights reserved.
#ifndef GUARD_NEURALNET_POPX_RNG_RNG_STATE_HELPER_HPP
#define GUARD_NEURALNET_POPX_RNG_RNG_STATE_HELPER_HPP

#include <poplar/CSRFunctions.hpp>
#include <poplar/RandomSeed.hpp>

#include <poprand/RandomGen.hpp>

#include <popops/ElementWise.hpp>

#include <popx/rng/rngstatelowering.hpp>

#include <popart/popx/pritask.hpp>

namespace popart {
namespace popx {

RngStateLowering::RngStateLowering(IrLowering &irLowering_, snap::Graph &graph_)
    : irLowering{irLowering_}, graph{graph_}, differingSeedsRngStateTensor{},
      identicalSeedsRngStateTensor{} {

  // Create the PRNG state tensor tensors.
  // Create tensor to hold the inacive RNG state.
  std::vector<size_t> rngStateTensorShape{
      getNumTiles(), getNumWorkersPerTile(), rngStateSizePerWorker};
  differingSeedsRngStateTensor =
      snap::Tensor{graph.get().getPoplarGraph().addVariable(
                       poplar::UNSIGNED_INT,
                       rngStateTensorShape,
                       {"differingSeedsRngStateTensor"}),
                   graph.get()};
  // Layout tensor carefully to avoid exchanges.
  setTensorLayout(differingSeedsRngStateTensor);

  // Create tensor to hold the inacive RNG state.
  identicalSeedsRngStateTensor =
      snap::Tensor{graph.get().getPoplarGraph().addVariable(
                       poplar::UNSIGNED_INT,
                       rngStateTensorShape,
                       {"identicalSeedsRngStateTensor"}),
                   graph.get()};
  // Layout tensor carefully to avoid exchanges.
  setTensorLayout(identicalSeedsRngStateTensor);
}

RngStateLowering::~RngStateLowering() = default;

void RngStateLowering::lowerInitRngStatesFromSeed(
    snap::program::Sequence &seq,
    const snap::Tensor &seed,
    const poplar::DebugContext &dbgCtx) {

  // The call to `setSeed` below is what triggers deriving the RNG state from
  // `seed`. Note that at this point the value of `seed` should be identical
  // across replicas (this is a precondition of this function) and hence the
  // derived RNG state will also be identical across replicas.
  poprand::setSeed(graph.get().getPoplarGraph(),
                   seed.getPoplarTensor(),
                   0,
                   seq.getPoplarSequence(),
                   dbgCtx);

  // Copy the replica-identical RNG state into the tensor we use to hold the
  // inactive RNG state. We will call `poplar::setHwSeeds` with this tensor
  // later before Ops with stochastic rounding method
  // `StochasticRoundingMethod::IdenticalSeeds` are ran.
  lowerGetHwSeeds(seq, identicalSeedsRngStateTensor, dbgCtx);

  // Now update the RNG state in a replica-differing way (without affecting
  // the seed). We get an offset value that's different for each replica, add
  // it to the seed and call `setSeed` to derive a replica-differing RNG state
  // from this value.
  auto offset = graph.get().getPoplarGraph().addReplicationIndexConstant();
  graph.get().getPoplarGraph().setTileMapping(offset, 0);
  auto replicaDifferentValue = popops::add(graph.get().getPoplarGraph(),
                                           seed.getPoplarTensor(),
                                           offset,
                                           seq.getPoplarSequence(),
                                           dbgCtx);
  poprand::setSeed(graph.get().getPoplarGraph(),
                   replicaDifferentValue,
                   0,
                   seq.getPoplarSequence(),
                   dbgCtx);

  // Copy the replica-differing RNG state into the tensor we use to hold the
  // inactive RNG state. We will call `poplar::setHwSeeds` with this tensor
  // later before Ops with stochastic rounding method
  // `StochasticRoundingMethod::DifferingSeeds` are ran.
  lowerGetHwSeeds(seq, differingSeedsRngStateTensor, dbgCtx);

  // Set initial RNG state to IdenticalSeeds.
  lowerSetHwSeeds(seq, identicalSeedsRngStateTensor, dbgCtx);
}

void RngStateLowering::lowerSetRngState(snap::program::Sequence &seq,
                                        PopOpx *opx) {

  Op *op = opx->op_p;

  if (op->hasStochasticRoundingMethod() &&
      // TODO(T48752): Remove _enableRngStateManagement.
      op->getIr().getSessionOptions()._enableRngStateManagement) {
    if (op->getStochasticRoundingMethod() ==
        StochasticRoundingMethod::DifferingSeeds) {
      lowerSetHwSeeds(seq,
                      differingSeedsRngStateTensor,
                      opx->debugContext("lowerSetRngState/DifferingSeeds"));
    } else if (op->getStochasticRoundingMethod() ==
               StochasticRoundingMethod::IdenticalSeeds) {
      lowerSetHwSeeds(seq,
                      identicalSeedsRngStateTensor,
                      opx->debugContext("lowerSetRngState/IdenticalSeeds"));
    }
  }
}

void RngStateLowering::lowerGetRngState(snap::program::Sequence &seq,
                                        PopOpx *opx) {

  Op *op = opx->op_p;

  if (op->hasStochasticRoundingMethod() &&
      // TODO(T48752): Remove _enableRngStateManagement.
      op->getIr().getSessionOptions()._enableRngStateManagement) {
    if (op->getStochasticRoundingMethod() ==
        StochasticRoundingMethod::DifferingSeeds) {
      lowerGetHwSeeds(seq,
                      differingSeedsRngStateTensor,
                      opx->debugContext("lowerGetRngState/DifferingSeeds"));
    } else if (op->getStochasticRoundingMethod() ==
               StochasticRoundingMethod::IdenticalSeeds) {
      lowerGetHwSeeds(seq,
                      identicalSeedsRngStateTensor,
                      opx->debugContext("lowerGetRngState/IdenticalSeeds"));
    }
  }
}

void RngStateLowering::setTensorLayout(snap::Tensor &tensor) {

  auto numTiles = graph.get().getPoplarGraph().getTarget().getNumTiles();
  if (tensor.rank() >= 1 && tensor.shape()[0] == numTiles) {

    for (auto tile = 0U; tile != numTiles; ++tile) {
      auto slice = tensor.slice({tile, tile + 1}, 0);
      graph.get().getPoplarGraph().setTileMapping(slice.getPoplarTensor(),
                                                  tile);
    }

  } else {
    throw internal_error("[RngStateLowering] Expected tensor with first "
                         "dimension of {} (got tensor shape {})",
                         numTiles,
                         tensor.shape());
  }
}

void RngStateLowering::lowerSetHwSeeds(
    snap::program::Sequence &seq,
    snap::Tensor &rngState,
    const poplar::DebugContext &dbgCtx) const {
  poplar::setHwSeeds(graph.get().getPoplarGraph(),
                     rngState.getPoplarTensor(),
                     seq.getPoplarSequence(),
                     dbgCtx);
}

void RngStateLowering::lowerGetHwSeeds(
    snap::program::Sequence &seq,
    snap::Tensor &rngState,
    const poplar::DebugContext &dbgCtx) const {
  auto tmp = snap::Tensor{poplar::getHwSeeds(graph.get().getPoplarGraph(),
                                             seq.getPoplarSequence(),
                                             dbgCtx),
                          graph.get()};
  seq.getPoplarSequence().add(poplar::program::Copy(
      tmp.getPoplarTensor(), rngState.getPoplarTensor(), false, dbgCtx));
}

PriTask RngStateLowering::initRngStateTensor() {
  // Set up combinedRngStateTensor
  auto initRngStateTensorTask = [this]() {
    SequenceMap seqs(graph.get());
    std::vector<size_t> combinedRngStateTensorShape{numRngStateTensors,
                                                    getNumTiles(),
                                                    getNumWorkersPerTile(),
                                                    rngStateSizePerWorker};
    combinedRngStateTensor = snap::Tensor{
        graph.get().getPoplarGraph().addVariable(poplar::UNSIGNED_INT,
                                                 combinedRngStateTensorShape,
                                                 {"combinedRngStateTensor"}),
        graph.get()};
    irLowering.get().getLinearMapper().mapTensor(graph.get(),
                                                 combinedRngStateTensor);
    return SequenceMap(graph.get());
  };
  return {+1e6, initRngStateTensorTaskId, {}, initRngStateTensorTask};
}

PriTask RngStateLowering::rngStateFromHost() {
  auto rngStateFromHostTask = [this]() {
    auto streamRngFromHost = graph.get().getPoplarGraph().addHostToDeviceFIFO(
        "h2d_rngStateTensor",
        poplar::UNSIGNED_INT,
        getCombinedRngStateSize(),
        poplar::ReplicatedStreamMode::REPLICATE);

    logging::devicex::debug("Initializing RNG h2d.");
    logging::devicex::debug("RNG size {}", getCombinedRngStateSize());

    SequenceMap seqs(graph.get());
    auto &seq =
        seqs.getSequence(&irLowering.get().progs.rngStateFromHostFragment());

    // Stream newRngState to combinedRngStateTensor
    seq.getPoplarSequence().add(
        poplar::program::Copy(streamRngFromHost,
                              combinedRngStateTensor.getPoplarTensor(),
                              false,
                              {"copyStreamRngStateTensor"}));
    // Copy first half of combinedRngStateTensor to identicalSeedsRngStateTensor
    seq.getPoplarSequence().add(poplar::program::Copy(
        combinedRngStateTensor.getPoplarTensor()[0],
        identicalSeedsRngStateTensor.getPoplarTensor(),
        false,
        {"copyRngStateTensorToIdenticalSeedsRngStateTensor"}));
    // Copy second half of combinedRngStateTensor to
    // differingSeedsRngStateTensor
    seq.getPoplarSequence().add(poplar::program::Copy(
        combinedRngStateTensor.getPoplarTensor()[1],
        differingSeedsRngStateTensor.getPoplarTensor(),
        false,
        {"copyRngStateTensorToDifferingSeedsRngStateTensor"}));
    return seqs;
  };
  return {0,
          rngStateFromHostTaskId,
          {{initRngStateTensorTaskId, DependencyType::Tensor}},
          rngStateFromHostTask};
}

PriTask RngStateLowering::rngStateToHost() {
  auto rngStateToHostTask = [this]() {
    auto streamRngToHost = graph.get().getPoplarGraph().addDeviceToHostFIFO(
        "d2h_rngStateTensor", poplar::UNSIGNED_INT, getCombinedRngStateSize());

    logging::devicex::debug("Initializing RNG d2h.");
    logging::devicex::debug("RNG size {}", getCombinedRngStateSize());

    SequenceMap seqs(graph.get());
    auto &seq =
        seqs.getSequence(&irLowering.get().progs.rngStateToHostFragment());

    // Update combinedRngStateTensor with the new values of
    // identicalSeedsRngStateTensor and differingSeedsRngStateTensor
    seq.getPoplarSequence().add(poplar::program::Copy(
        poplar::concat(
            identicalSeedsRngStateTensor.expand({0}).getPoplarTensor(),
            differingSeedsRngStateTensor.expand({0}).getPoplarTensor()),
        combinedRngStateTensor.getPoplarTensor(),
        false,
        "seedsToRngStateTensor"));
    // Stream combinedRngStateTensor to host
    seq.getPoplarSequence().add(
        poplar::program::Copy(combinedRngStateTensor.getPoplarTensor(),
                              streamRngToHost,
                              false,
                              {"rngStateToHost"}));
    return seqs;
  };

  return {0,
          rngStateToHostTaskId,
          {{initRngStateTensorTaskId, DependencyType::Tensor}},
          rngStateToHostTask};
}

unsigned RngStateLowering::getNumWorkersPerTile() {
  return graph.get().getPoplarGraph().getTarget().getNumWorkerContexts();
}

unsigned RngStateLowering::getNumTiles() {
  return graph.get().getPoplarGraph().getTarget().getNumTiles();
}

unsigned RngStateLowering::getCombinedRngStateSize() {
  return combinedRngStateTensor.numElements();
}

const TaskId RngStateLowering::initRngStateTensorTaskId =
    TaskId(TaskId::Type::InitRngStateTensorTask);
const TaskId RngStateLowering::rngStateFromHostTaskId =
    TaskId(TaskId::Type::RngStateFromHostTask);
const TaskId RngStateLowering::rngStateToHostTaskId =
    TaskId(TaskId::Type::RngStateToHostTask);

} // namespace popx
} // namespace popart

#endif