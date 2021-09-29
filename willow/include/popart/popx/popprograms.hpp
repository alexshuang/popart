// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#ifndef GUARD_NEURALNET_POPPROGRAMS_HPP
#define GUARD_NEURALNET_POPPROGRAMS_HPP

#include <set>
#include <unordered_map>

#include <poplar/Program.hpp>

#include <snap/Program.hpp>

#include <popart/names.hpp>
#include <popart/popx/pritask.hpp>

namespace popart {

enum class ScheduledPreLoss;

namespace popx {

class IrLowering;

class PopPrograms {

public:
  // We may want to run some programs multiple times without having
  // to communicate with the host to call the 'run'. By supplying a
  // count, we can loop a repeatable program inside a Poplar repeat
  // program
  PopPrograms(IrLowering *ir_lowering_p_);

  enum ProgramIndex {
    WeightsFromHost = 0,
    OptimizerFromHost,
    SetRandomSeedFromHost,
    RngStateFromHost,
    Program,
    RngStateToHost,
    WeightstoHost,
    CycleCountTensortoHost,
    N // The number of programs
  };

  // Order of these enums is used for scheduling
  enum class ProgramFragmentIndex {
    StreamWeightsFromHost = 0,
    StreamOptimizerFromHost,
    SetRandomSeedFromHost,
    RngStateFromHost,
    Init,
    PreForward,
    Forward,
    Backward,
    VarUpdateFromAccumulator,
    RngStateToHost,
    WeightstoHost,
    ToHostFinalCopy,
    CycleCountTensortoHost,
    N // The number of program fragments
  };

  // Program fragments are not necessarily complete program that can be given to
  // a poplar engine.
  const snap::program::Sequence &streamWeightsFromHostFragment() const;
  snap::program::Sequence &streamWeightsFromHostFragment();
  const snap::program::Sequence &streamOptimizerFromHostFragment() const;
  snap::program::Sequence &streamOptimizerFromHostFragment();
  const snap::program::Sequence &setRandomSeedFromHostFragment() const;
  snap::program::Sequence &setRandomSeedFromHostFragment();
  const snap::program::Sequence &cycleCountTensorToHostFragment() const;
  snap::program::Sequence &rngStateFromHostFragment();
  const snap::program::Sequence &rngStateFromHostFragment() const;
  snap::program::Sequence &rngStateToHostFragment();
  const snap::program::Sequence &rngStateToHostFragment() const;
  snap::program::Sequence &cycleCountTensorToHostFragment();
  const snap::program::Sequence &toHostFinalCopyFragment() const;
  snap::program::Sequence &toHostFinalCopyFragment();
  const snap::program::Sequence &initFragment() const;
  snap::program::Sequence &initFragment();
  const snap::program::Sequence &preForwardFragment() const;
  snap::program::Sequence &preForwardFragment();
  const snap::program::Sequence &forwardFragment() const;
  snap::program::Sequence &forwardFragment();
  const snap::program::Sequence &backwardFragment() const;
  snap::program::Sequence &backwardFragment();
  const snap::program::Sequence &accumulateOuterFragment() const;
  snap::program::Sequence &accumulateOuterFragment();
  const snap::program::Sequence &weightsToHostFragment() const;
  snap::program::Sequence &weightsToHostFragment();
  // If ScheduledPreLoss::Yes, then return forwardFragment(), else return
  // backwardFragment()
  snap::program::Sequence &forwardOrBackwardFragment(ScheduledPreLoss);

  // A list of programs that can be run by the Poplar engine.
  const std::vector<snap::program::Program> progs() const;

  snap::program::Sequence &programFragment(PopPrograms::ProgramFragmentIndex);

  // Sub-graph program fragments, getters and setters for poplar sequences and
  // functions for subgraphs.

  // The number of Poplar sequences associated with a graph.
  int getNumFragments(const Graph &graph) const;
  // Get a vector of all Poplar sequences associated with a graph.
  std::vector<snap::program::Sequence> &scopeFragments(const Graph &);
  // Get a specific Poplar sequence associated with a graph.
  snap::program::Sequence &scopeFragment(const Graph &,
                                         SubgraphPartIndex subgraphPart);
  // Determine if any Poplar sequences associated with a graph are allocated.
  bool containsFragments(const Graph &graph) const;
  // Determine whether a specific Poplar sequence associated with a graph has
  // been allocated.
  bool containsFragment(const Graph &graph,
                        SubgraphPartIndex subgraphPart) const;
  // Ensure a specific Poplar sequence is allocated.
  void createFragment(const Graph &graph, SubgraphPartIndex subgraphPart);
  // Wrap all Poplar sequences associated with a graph in to a poplar function
  // that can be called and return them all.
  std::vector<poplar::Function> &getFragmentFunctions(const Graph &graph,
                                                      snap::Graph &snapGraph);
  // Wrap all Poplar sequences associated with a graph in to a poplar function
  // that can be called and return a specific one.
  poplar::Function &getFragmentFunction(const Graph &graph,
                                        SubgraphPartIndex subgraphPart,
                                        snap::Graph &snapGraph);

  // Get the program fragment for a recomputed op. createRecomputeFragment must
  // be called first.
  std::vector<snap::program::Sequence>::iterator recomputeFragment(OpId);
  // Create the program fragment for a recomputed op.
  SequenceMap::SequenceInterval createRecomputeFragment(OpId);

  bool hasBeenRecomputed(OpId, ExecutionPhase) const;
  void recordRecomputed(OpId, ExecutionPhase);

  // Each Pipeline Stage is composed of these fragments. For a given Pipeline
  // Stage, any of these fragments may be empty.
  //
  // Note: the preForwardFragment and IpuCopy fragment do not require a
  // PipelineFragmentId, since they exist as a single fragment idependent of
  // pipeline stage, and are run every Pipeline Cycle.
  enum class PipelineFragmentId { ToDeviceStream = 0, Main, ToHostStream };
  std::string getStrFromPipelineFragmentId(PipelineFragmentId) const;

  // Program fragments specific to pipelined model. Each method to return
  // a pipeline program fragment takes a 'description' string, that describes
  // the code being added to the returned fragment. This description is added
  // to pipelineDescs to build up a full description of the program.
  snap::program::Sequence &
  pipelineFragment(PipelineStage, PipelineFragmentId, const std::string &desc);

  snap::program::Sequence &
  pipelineToDeviceStreamFragment(PipelineStage pipelineStage,
                                 const std::string &desc);
  snap::program::Sequence &pipelineMainFragment(PipelineStage,
                                                const std::string &desc);

  // To stream anchors that are computed in the pipelineMainFragment
  snap::program::Sequence &
  pipelineToHostStreamFragment(PipelineStage, const std::string &desc);
  snap::program::Sequence &pipelineIpuCopyFragment(const std::string &desc);

  void addPipelineCycle(
      PipelineCycle pCycle,
      snap::program::Sequence &sq,
      std::ostringstream &ss,
      std::map<PipelineStage, poplar::Function> &mainFunctions) const;

  IrLowering *ir_lowering_p;

private:
  std::vector<snap::program::Sequence> seqs;

  // The sub-graph program fragments will be stored here
  std::unordered_map<std::string, std::vector<snap::program::Sequence>>
      scopeSeqs;
  std::unordered_map<std::string, std::vector<poplar::Function>> funcs;

  // The recompute program fragments will be stored here. We store the sequences
  // in singleton vectors because grow code requires iterators to vectors.
  std::map<OpId, std::vector<snap::program::Sequence>> recomputeSeqs;

  // Pipelining fragments for each pipeline stage are stored here
  std::map<PipelineFragmentId, std::map<PipelineStage, snap::program::Sequence>>
      pipelineSeqs;

  // ... and their corresponding descriptions
  std::map<PipelineFragmentId, std::map<PipelineStage, std::string>>
      pipelineDescs;

  // IpuCopy program
  std::unique_ptr<snap::program::Sequence> pipelineIpuCopySeq;
  std::string pipelineIpuCopyDesc;

public:
  void initWithSnapGraph(snap::Graph &);

private:
  snap::program::Sequence getFullProgramFromPipelineFragments() const;

  std::set<std::pair<OpId, ExecutionPhase>> beenRecomputed;

  snap::program::Sequence weightsFromHost() const;
  snap::program::Sequence optimizerFromHost() const;
  snap::program::Sequence setRandomSeedFromHost() const;
  snap::program::Sequence rngStateFromHost() const;
  snap::program::Sequence cycleCountTensorToHost() const;
  snap::program::Sequence program() const;
  snap::program::Sequence rngStateToHost() const;
  snap::program::Sequence weightsToHost() const;
};

} // namespace popx
} // namespace popart

#endif
