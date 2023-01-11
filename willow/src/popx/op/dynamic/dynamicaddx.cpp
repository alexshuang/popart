// Copyright (c) 2020 Graphcore Ltd. All rights reserved.
#include <cstddef>
#include <string>
#include <vector>
#include <poplar/Tensor.hpp>
#include <poplar/Type.hpp>
#include <popops/Cast.hpp>
#include <popops/DynamicSlice.hpp>
#include <popops/ElementWise.hpp>
#include <popops/ExprOp.hpp>
#include <popart/popx/op/dynamic/dynamicaddx.hpp>
#include <popart/popx/opxmanager.hpp>

#include "popart/graphcoreoperators.hpp"
#include "popart/op/dynamic/dynamicbase.hpp"

namespace poplar {
namespace program {
class Sequence;
} // namespace program
} // namespace poplar

namespace popart {
namespace popx {

void DynamicAddOpx::grow(poplar::program::Sequence &prog) const {
  auto &op    = getOp<DynamicTernaryBaseOp>();
  auto tensor = getInTensor(DynamicTernaryBaseOp::getUpdateInIndex());
  auto index  = getInTensor(DynamicTernaryBaseOp::getIndexInIndex());
  auto slice  = getInTensor(DynamicTernaryBaseOp::getInIndex());

  std::vector<size_t> paxes(op.getAxes().begin(), op.getAxes().end());
  std::vector<size_t> psizes(op.getSizes().begin(), op.getSizes().end());

  auto outTensor = cloneNcopyOpt(prog, tensor);

  // Get the slice that is to be added to: s = t[index:index+psizes]
  auto s = popops::dynamicSlice(
      graph(),
      tensor,
      popops::cast(graph(),
                   index.reshape({op.getAxes().size()}),
                   poplar::UNSIGNED_INT,
                   prog,
                   debugContext()),
      paxes,
      psizes,
      prog,
      debugContext("dynamic_add_slice_" +
                   op.inId(DynamicTernaryBaseOp::getUpdateInIndex())));

  // Add inplace: s += slice
  popops::mapInPlace(
      graph(),
      popops::expr::BinaryOpType::ADD,
      s,
      slice,
      prog,
      debugContext("dynamic_add_mip_" +
                   op.inId(DynamicTernaryBaseOp::getUpdateInIndex())));

  // Update: t[index:index+psizes] = s
  popops::dynamicUpdate(
      graph(),
      outTensor,
      s,
      popops::cast(graph(),
                   index.reshape({op.getAxes().size()}),
                   poplar::UNSIGNED_INT,
                   prog,
                   debugContext()),
      paxes,
      psizes,
      prog,
      debugContext("dynamic_add_" +
                   op.inId(DynamicTernaryBaseOp::getUpdateInIndex())));

  setOutTensor(DynamicTernaryBaseOp::getOutIndex(), outTensor);
}

poplar::Tensor
DynamicAddInplaceOpx::cloneNcopyOpt(poplar::program::Sequence &s,
                                    const poplar::Tensor &t) const {
  if (t.isParallelWriteable()) {
    return t;
  } else {
    // Outplace because t has internal aliases
    return cloneNcopy(s, t);
  }
}

namespace {
// Ops
OpxCreator<DynamicAddOpx>
    dynamicAddOpxCreator(Onnx::CustomOperators::DynamicAdd_1);
OpxCreator<DynamicAddInplaceOpx>
    dynamicAddInplaceOpxCreator(Onnx::CustomOperators::DynamicAddInplace);

} // namespace

} // namespace popx
} // namespace popart
