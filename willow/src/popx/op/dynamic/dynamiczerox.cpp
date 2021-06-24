// Copyright (c) 2020 Graphcore Ltd. All rights reserved.
#include <popart/error.hpp>
#include <popart/op/dynamic/dynamiczero.hpp>
#include <popart/popx/devicex.hpp>
#include <popart/popx/op/dynamic/dynamiczerox.hpp>
#include <popart/popx/opxmanager.hpp>
#include <popart/tensor.hpp>
#include <popart/tensorindex.hpp>

#include <popops/Cast.hpp>
#include <popops/DynamicSlice.hpp>
#include <popops/ElementWise.hpp>
#include <popops/Zero.hpp>

namespace popart {
namespace popx {

void DynamicZeroOpx::grow(poplar::program::Sequence &prog) const {
  auto &op    = getOp<DynamicBinaryBaseOp>();
  auto tensor = getInTensor(DynamicBinaryBaseOp::getUpdateInIndex());
  auto index  = getInTensor(DynamicBinaryBaseOp::getIndexInIndex());

  std::vector<size_t> paxes(op.getAxes().begin(), op.getAxes().end());
  std::vector<size_t> psizes(op.getSizes().begin(), op.getSizes().end());

  auto updateShape = op.inShape(DynamicBinaryBaseOp::getUpdateInIndex());

  auto slice =
      popops::createSliceTensor(
          graph().getPoplarGraph(), tensor.getPoplarTensor(), paxes, psizes, 1)
          .squeeze({0});
  popops::zero(
      graph().getPoplarGraph(), slice, prog, debugContext("dynamic_zero_zero"));

  auto outTensor = cloneNcopyOpt(prog, tensor);

  popops::dynamicUpdate(
      graph().getPoplarGraph(),
      outTensor.getPoplarTensor(),
      slice,
      popops::cast(graph().getPoplarGraph(),
                   index.getPoplarTensor().reshape({op.getAxes().size()}),
                   poplar::UNSIGNED_INT,
                   prog,
                   debugContext()),
      paxes,
      psizes,
      prog,
      debugContext("dynamic_zero_" +
                   op.inId(DynamicBinaryBaseOp::getUpdateInIndex())));

  setOutTensor(DynamicBinaryBaseOp::getOutIndex(), outTensor);
}

InputCreatorType DynamicZeroOpx::getInputCreatorType(InIndex index) const {
  return index == DynamicBinaryBaseOp::getUpdateInIndex()
             ? InputCreatorType::CanUnwind
             : PopOpx::getInputCreatorType(index);
}

snap::Tensor DynamicZeroInplaceOpx::cloneNcopyOpt(poplar::program::Sequence &s,
                                                  const snap::Tensor &t) const {
  if (t.getPoplarTensor().isParallelWriteable()) {
    return t;
  } else {
    // Outplace because t has internal aliases
    return cloneNcopy(s, t);
  }
}

namespace {
// Ops
OpxCreator<DynamicZeroOpx>
    dynamicZeroOpxCreator(Onnx::CustomOperators::DynamicZero_1);
OpxCreator<DynamicZeroInplaceOpx>
    dynamicZeroInplaceOpxCreator(Onnx::CustomOperators::DynamicZeroInplace);
} // namespace

} // namespace popx
} // namespace popart
