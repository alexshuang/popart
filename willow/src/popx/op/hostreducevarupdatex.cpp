#include <popart/error.hpp>
#include <popart/ir.hpp>
#include <popart/op/hostreducevarupdate.hpp>
#include <popart/optimizer.hpp>
#include <popart/popx/devicex.hpp>
#include <popart/popx/op/hostreducevarupdatex.hpp>
#include <popart/popx/opxmanager.hpp>

#include <popops/Collectives.hpp>
#include <popops/ElementWise.hpp>
#include <popops/ScaledAdd.hpp>

namespace pe = popops::expr;

namespace popart {
namespace popx {

HostReduceGradCopyOpx::HostReduceGradCopyOpx(Op *op, Devicex *devicex)
    : Opx(op, devicex) {
  verifyOp<HostReduceGradCopyOp>(op, Onnx::CustomOperators::HostReduceGradCopy);
}

void HostReduceGradCopyOpx::grow(poplar::program::Sequence &prog) const {
  if (dv_p->getHostReduceSyncInserted()) {
    throw error("Internal Logic Error: all host reductions should happen after "
                "all gradients sent to host");
  }

  auto vu_op                  = getOp<HostReduceGradCopyOp>();
  const auto updater_index    = HostReduceGradCopyOp::getInIndex();
  poplar::Tensor weightDeltas = getInTensor(updater_index);

  const auto grad_id = inId(updater_index);
  auto deviceToHostStream =
      dv_p->insertGradientStoreStream(grad_id, inInfo(updater_index), graph());

  poplar::program::Copy gradientsToHostProg(weightDeltas, deviceToHostStream);
  prog.add(gradientsToHostProg);
}

HostReduceVarCopyOpx::HostReduceVarCopyOpx(Op *op, Devicex *devicex)
    : VarUpdateOpx(op, devicex) {
  verifyOp<HostSGD0VarUpdate>(op, Onnx::CustomOperators::HostSGD0VarUpdate);
}

void HostReduceVarCopyOpx::grow(poplar::program::Sequence &prog) const {
  if (!dv_p->getHostReduceSyncInserted()) {
    // A sync is added here to enforce that gradient copies are executed
    // before weight copies. Gradient copies are scheduled to happen before
    // weight copies in PopART. However, if multiple stream copies are
    // performed with a single sync id then a host read can be scheduled
    // before a host write in the Poplar engine but the actual
    // callback might still be executed after. This happens when Poplar
    // merges two host syncs during compilation into one.
    // See IPUTarget::prepareForStreamAccess() and
    // IPUTarget::completeStreamAccess() for details
    prog.add(poplar::program::Sync(poplar::SyncType::INTERNAL));
    dv_p->setHostReduceSyncInserted(true);
  }

  auto vu_op = getOp<HostSGD0VarUpdate>();

  const auto var_update_index = HostSGD0VarUpdate::getVarToUpdateInIndex();

  const auto grad_id     = getGradId(vu_op.getVarId());
  poplar::Tensor weights = getInTensor(var_update_index);

  const auto weight_id    = inId(var_update_index);
  auto hostToDeviceStream = dv_p->insertWeightLoadStream(
      weight_id, inInfo(var_update_index), graph());

  dv_p->getGradAndVarStreamIds().emplace_back(
      std::make_pair(dv_p->gradientStoreStreamId(grad_id),
                     dv_p->weightLoadStreamId(weight_id)));

  poplar::program::Copy hostWeightsToDeviceProg(hostToDeviceStream, weights);
  prog.add(hostWeightsToDeviceProg);

  // output is a reference to the updated input
  setOutTensor(HostSGD0VarUpdate::getUpdatedVarOutIndex(),
               getInTensor(var_update_index));
}

namespace {
OpxCreator<HostReduceGradCopyOpx>
    HostReduceGradCopyOpxCreator(Onnx::CustomOperators::HostReduceGradCopy);

OpxCreator<HostReduceVarCopyOpx>
    HostReduceVarCopyOpxCreator(Onnx::CustomOperators::HostSGD0VarUpdate);

} // namespace

} // namespace popx
} // namespace popart
