// Copyright (c) 2020 Graphcore Ltd. All rights reserved.
#ifndef POPART_WILLOW_INCLUDE_POPART_POPX_OP_LOOPX_HPP_
#define POPART_WILLOW_INCLUDE_POPART_POPX_OP_LOOPX_HPP_

#include <snap/Tensor.hpp>
#include <vector>
#include <popart/popx/op/subgraphx.hpp>
#include <popart/popx/popopx.hpp>

#include "popart/names.hpp"

namespace snap {
namespace program {
class Sequence;
} // namespace program
} // namespace snap

namespace popart {
class Op;

namespace popx {
class Devicex;

class LoopOpx : public SubgraphOpx {
public:
  LoopOpx(Op *, Devicex *);
  void grow(snap::program::Sequence &) const final;
  InputCreatorType getInputCreatorType(InIndex) const;
  bool canUnwind(InIndex in, OutIndex out) const final;
  snap::Tensor unwindTensorLayout(snap::Tensor, InIndex, OutIndex) const final;
  view::RegMap unwindRegion(InIndex, OutIndex) const final;

private:
  // Clone the body outputs to avoid non-writable tensors
  std::vector<snap::Tensor> cloneBodyOutputs() const;

  // Copy inputs which are loop-carried from the Loop's input to the subgraph
  // body's output. If the iteration count is 0, the outputs are equal to the
  // inputs.
  void copyExplicitOpInputsToBodyOutputs(
      snap::program::Sequence &prog,
      std::vector<snap::Tensor> &clonedBodyOutputs) const;

  // Copy inputs which are not loop-carried from the Loop's input to the
  // subgraph body's input.
  void
  copyImplicitOpInputsToImplicitBodyInputs(snap::program::Sequence &prog) const;

  // Copy the body outputs to the body output clones (to avoid aliasing issues).
  void copyBodyOutputsToBodyOutputClones(
      snap::program::Sequence &prog,
      std::vector<snap::Tensor> &clonedBodyOutputs) const;

  // Copy the body outputs back to the body inputs (loop carry).
  void copyBodyOutputsToExplicitBodyInputs(
      snap::program::Sequence &prog,
      std::vector<snap::Tensor> &clonedBodyOutputs) const;

  // Copy the body outputs to the Loop's outputs (final values on loop
  // termination).
  void copyBodyOutputsToOpOutputs(
      snap::program::Sequence &prog,
      std::vector<snap::Tensor> &clonedBodyOutputs) const;

  // Copy any Loop input that the LoopOp should modify inplace from the body to
  // the Loop's inputs.
  void copyModifiedBodyInputsToOpInputs(snap::program::Sequence &prog) const;
};
} // namespace popx
} // namespace popart

#endif // POPART_WILLOW_INCLUDE_POPART_POPX_OP_LOOPX_HPP_
