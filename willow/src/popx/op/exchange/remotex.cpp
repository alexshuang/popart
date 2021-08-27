// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#include <popart/graph.hpp>
#include <popart/ir.hpp>
#include <popart/op/exchange/exchange.hpp>
#include <popart/op/exchange/remote.hpp>
#include <popart/popx/devicex.hpp>
#include <popart/popx/irlowering.hpp>
#include <popart/popx/op/exchange/remotex.hpp>
#include <popart/popx/opxmanager.hpp>

namespace popart {
namespace popx {

RemoteBaseOpx::RemoteBaseOpx(Op *op, Devicex *devicex)
    : ExchangeBaseOpx(op, devicex) {}

RemoteStoreOpx::RemoteStoreOpx(Op *op, Devicex *devicex)
    : RemoteBaseOpx(op, devicex) {
  verifyOp<RemoteStoreOp>(op, Onnx::CustomOperators::RemoteStore);
}

void RemoteStoreOpx::grow(poplar::program::Sequence &prog) const {
  auto &remoteStoreOp = getOp<RemoteStoreOp>();

  TensorId inTensorId =
      remoteStoreOp.input->tensor(RemoteStoreOp::getLocalTensorInIndex())->id;

  logging::opx::debug("[RemoteStoreOpx] Growing RemoteStore for tensor {}, "
                      "using RemoteBuffer {}",
                      inTensorId,
                      remoteStoreOp.getRemoteBufferId());

  snap::Tensor inTensor = getInTensor(RemoteStoreOp::getLocalTensorInIndex());

  TensorId offsetId;
  snap::Tensor offset;

  if (remoteStoreOp.input->hasIndex(
          RemoteStoreOp::getRemoteBufferOffsetInIndex())) {
    offsetId = remoteStoreOp.input
                   ->tensor(RemoteStoreOp::getRemoteBufferOffsetInIndex())
                   ->id;
    offset = getInTensor(RemoteStoreOp::getRemoteBufferOffsetInIndex());
  }

  std::shared_ptr<ExchangeDescriptorx> descriptorx =
      getExchangeDescriptorx(dv_p, remoteStoreOp.getExchangeDescriptor(0));

  descriptorx->setInTensors({{inTensorId, inTensor}, {offsetId, offset}});
  descriptorx->pre(graph(), prog, debugContext());
  descriptorx->exchange(graph(), prog, debugContext());
  descriptorx->post(graph(), prog, debugContext());
}

RemoteLoadOpx::RemoteLoadOpx(Op *op, Devicex *devicex)
    : RemoteBaseOpx(op, devicex) {
  verifyOp<RemoteLoadOp>(op, Onnx::CustomOperators::RemoteLoad);
}

void RemoteLoadOpx::grow(poplar::program::Sequence &prog) const {
  auto &remoteLoadOp = getOp<RemoteLoadOp>();

  TensorId inTensorId =
      remoteLoadOp.input->tensor(RemoteLoadOp::getLocalTensorInIndex())->id;
  TensorId outTensorId =
      remoteLoadOp.output->tensor(RemoteLoadOp::getLocalTensorOutIndex())->id;

  // Tensor completely overwritten
  logging::opx::debug("[RemoteLoadOpx] Growing RemoteLoad for tensor {} -> {}, "
                      "using RemoteBuffer {}",
                      inTensorId,
                      outTensorId,
                      remoteLoadOp.getRemoteBufferId());

  snap::Tensor inTensor = getInTensor(RemoteLoadOp::getLocalTensorInIndex());
  TensorId offsetId;
  snap::Tensor offset;

  if (remoteLoadOp.input->hasIndex(
          RemoteLoadOp::getRemoteBufferOffsetInIndex())) {
    offsetId =
        remoteLoadOp.input->tensor(RemoteLoadOp::getRemoteBufferOffsetInIndex())
            ->id;
    offset = getInTensor(RemoteLoadOp::getRemoteBufferOffsetInIndex());
  }

  std::shared_ptr<ExchangeDescriptorx> descriptorx =
      getExchangeDescriptorx(dv_p, remoteLoadOp.getExchangeDescriptor(0));

  descriptorx->setInTensors({{inTensorId, inTensor}, {offsetId, offset}});
  descriptorx->pre(graph(), prog, debugContext());
  descriptorx->exchange(graph(), prog, debugContext());
  descriptorx->post(graph(), prog, debugContext());

  if (hasInViewChangers(RemoteLoadOp::getLocalTensorInIndex())) {
    setOutViewChangers(
        RemoteLoadOp::getLocalTensorOutIndex(),
        getInViewChangers(RemoteLoadOp::getLocalTensorInIndex()));
  }

  setOutTensor(RemoteLoadOp::getLocalTensorOutIndex(),
               descriptorx->getOutTensors().at(0));
}

InputCreatorType RemoteLoadOpx::getInputCreatorType(InIndex index) const {
  return index == RemoteLoadOp::getLocalTensorInIndex()
             ? InputCreatorType::CanUnwind
             : PopOpx::getInputCreatorType(index);
}

snap::Tensor RemoteLoadOpx::unwindTensorLayout(snap::Tensor tensor,
                                               InIndex in,
                                               OutIndex out) const {
  auto &remoteLoadOp = getOp<RemoteLoadOp>();
  std::shared_ptr<ExchangeDescriptorx> descriptorx =
      getExchangeDescriptorx(dv_p, remoteLoadOp.getExchangeDescriptor(0));
  return descriptorx->unwind(srcVirtualGraph(in), tensor);
}

view::RegMap RemoteLoadOpx::unwindRegion(InIndex, OutIndex) const {
  return [](const view::Region &r) { return view::Regions(1, r); };
}

namespace {
OpxCreator<RemoteStoreOpx>
    remoteStoreOpxCreator(Onnx::CustomOperators::RemoteStore);
OpxCreator<RemoteLoadOpx>
    remoteLoadOpxCreator(Onnx::CustomOperators::RemoteLoad);
} // namespace
} // namespace popx
} // namespace popart
