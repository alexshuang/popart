// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#include <popart/error.hpp>
#include <popart/op/restore.hpp>
#include <popart/popx/devicex.hpp>
#include <popart/popx/op/restorex.hpp>
#include <popart/popx/opxmanager.hpp>
#include <popart/tensor.hpp>

#include <popops/DynamicSlice.hpp>
#include <popops/ElementWise.hpp>

namespace popart {
namespace popx {

namespace {

poplar::Tensor grow_restore_dynamic_slice(const Opx &opx,
                                          int64_t stashSize,
                                          poplar::Tensor &stash,
                                          poplar::program::Sequence &prog) {
  auto &graph = opx.graph();

  // Create the index tensor
  poplar::Tensor stashIndex =
      graph.addVariable(poplar::UNSIGNED_INT, {1}, opx.debugContext());
  graph.setTileMapping(stashIndex, 0);
  graph.setInitialValue(stashIndex, poplar::ArrayRef<uint32_t>({0}));

  // Read the stash
  auto actFromStash =
      popops::dynamicSlice(graph,
                           stash,
                           stashIndex,
                           {0},
                           {1},
                           prog,
                           opx.debugContext("grow_restore_dynamic_slice"));

  // Increment the index
  auto one = opx.getConst(poplar::UNSIGNED_INT, {}, 1.0, "one");
  popops::addInPlace(graph, stashIndex, one, prog, opx.debugContext());

  // Wrap the index
  auto stashSizeTensor =
      opx.getConst(poplar::UNSIGNED_INT, {}, stashSize, "stash_size");
  popops::remInPlace(
      graph, stashIndex, stashSizeTensor, prog, opx.debugContext());

  return actFromStash;
}

} // namespace

void RestoreInplaceOpx::grow(poplar::program::Sequence &prog) const {
  auto &op          = getOp<RestoreInplaceOp>();
  auto actToRestore = getInTensor(RestoreInplaceOp::getActToRestoreInIndex());
  auto stash        = getInTensor(RestoreInplaceOp::getStashInIndex());

  auto actFromStash =
      grow_restore_dynamic_slice(*this, op.getStashSize(), stash, prog);

  prog.add(poplar::program::Copy(
      actFromStash.squeeze({0}), actToRestore, false, debugContext()));
  setOutTensor(RestoreInplaceOp::getRestoredActOutIndex(), actToRestore);
}

RestoreInplaceOpx::RestoreInplaceOpx(Op *op, Devicex *devicex)
    : Opx(op, devicex) {
  verifyOp<RestoreInplaceOp>(op);
}

void RestoreOpx::grow(poplar::program::Sequence &prog) const {
  auto &op   = getOp<RestoreOp>();
  auto stash = getInTensor(RestoreOp::getStashInIndex());

  auto actFromStash =
      grow_restore_dynamic_slice(*this, op.getStashSize(), stash, prog);

  setOutTensor(RestoreOp::getRestoredActOutIndex(), actFromStash.squeeze({0}));
}

RestoreOpx::RestoreOpx(Op *op, Devicex *devicex) : Opx(op, devicex) {
  verifyOp<RestoreOp>(op);
}

namespace {
OpxCreator<RestoreOpx> restoreOpxCreator(Onnx::CustomOperators::Restore);
OpxCreator<RestoreInplaceOpx>
    restoreInplaceOpxCreator(Onnx::CustomOperators::RestoreInplace);
} // namespace

} // namespace popx
} // namespace popart
