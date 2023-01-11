// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#include <cstddef>
#include <cstdint>
#include <ext/new_allocator.h>
#include <string>
#include <poplar/ArrayRef.hpp>
#include <poplar/Graph.hpp>
#include <poplar/Program.hpp>
#include <poplar/Tensor.hpp>
#include <poplar/Type.hpp>
#include <popops/DynamicSlice.hpp>
#include <popops/ElementWise.hpp>
#include <popart/op/stash.hpp>
#include <popart/popx/devicex.hpp>
#include <popart/popx/irlowering.hpp>
#include <popart/popx/op/stashx.hpp>
#include <popart/popx/opxmanager.hpp>

#include "popart/graphcoreoperators.hpp"
#include "popart/popx/opx.hpp"

namespace popart {
class Op;

namespace popx {

void StashOpx::growStaticStashUpdate(poplar::program::Sequence &prog,
                                     const poplar::Tensor &stashIndex,
                                     const poplar::Tensor &inTensor,
                                     const poplar::Tensor &outTensor) const {
  /*
    We cannot do a dynamic update based on tensor stashIndex, but we can do a
    dynamic switch-case on stashIndex. There are hStashSize cases, with each
    case i being the program that should be run if stashIndex has the value i.
    We have thus "unrolled" the dynamic update in a way.
  */

  poplar::program::Switch switchCase(stashIndex.reshape({}),
                                     debugContext("static-stash/switch"));

  for (unsigned i = 0; i != hStashSize; ++i) {
    const auto outSliceAtIdx = outTensor.slice(i, i + 1, 0);
    switchCase.add(i,
                   poplar::program::Copy(inTensor,
                                         outSliceAtIdx,
                                         false,
                                         debugContext("static-stash/switch-" +
                                                      std::to_string(i))));
  }

  prog.add(switchCase);
}

void StashOpx::growDynamicStashUpdate(poplar::program::Sequence &prog,
                                      const poplar::Tensor &stashIndex,
                                      const poplar::Tensor &inTensor,
                                      const poplar::Tensor &outTensor) const {
  // Update the stash.
  popops::dynamicUpdate(graph(),
                        outTensor,
                        inTensor.expand({0}),
                        stashIndex,
                        {0},
                        {1},
                        prog,
                        debugContext("stash"));
}

void StashOpx::grow(poplar::program::Sequence &prog) const {
  // Create the stash size tensor.
  const auto stashSize =
      getConst(poplar::UNSIGNED_INT, {}, hStashSize, "stash_size");

  // Create the stash index tensor.
  const poplar::Tensor stashIndex =
      getScalarVariable(poplar::UNSIGNED_INT, "stash_index").reshape({1});
  graph().setInitialValue(stashIndex, poplar::ArrayRef<uint32_t>({0}));
  dv_p->lowering().addPipelineIndexTensor(stashIndex);

  // Retrieve the input tensor.
  const auto &inTensor = getInTensor(StashOp::getInIndex());

  // Create the output tensor.
  const auto outTensor =
      popops::createSliceableTensorFromSlice(graph(),
                                             inTensor.expand({0}),
                                             {0},
                                             {hStashSize},
                                             outId(StashOp::getOutIndex()));

  // Create the stash tensor (the output) and grow the program to update it.
  if (canDynamicUpdateStash) {
    growDynamicStashUpdate(prog, stashIndex, inTensor, outTensor);
  } else {
    growStaticStashUpdate(prog, stashIndex, inTensor, outTensor);
  }
  setOutTensor(StashOp::getOutIndex(), outTensor);

  // Create a "1" tensor and grow program to increment stash index by 1.
  const auto one = getConst(poplar::UNSIGNED_INT, {}, 1.0, "one");
  popops::addInPlace(graph(), stashIndex, one, prog, debugContext());
  popops::remInPlace(graph(), stashIndex, stashSize, prog, debugContext());
}

StashOpx::StashOpx(Op *op, Devicex *devicex) : Opx(op, devicex) {
  verifyOp<StashOp>(op);
  hStashSize = static_cast<size_t>(getOp<StashOp>().getStashSize());
  // INT8/UINT8 now supported. Leaving the fallbacks until stashx/restorex
  // will get removed wholesale
  // TODO: T51331
  canDynamicUpdateStash = true;
}

namespace {
OpxCreator<StashOpx> stashOpxCreator(Onnx::CustomOperators::Stash);
} // namespace

} // namespace popx
} // namespace popart
