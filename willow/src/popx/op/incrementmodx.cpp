// Copyright (c) 2021 Graphcore Ltd. All rights reserved.
#include <cstdint>
#include <memory>
#include <snap/Tensor.hpp>
#include <snap/popops/ElementWise.hpp>
#include <string>
#include <popops/Expr.hpp>
#include <popart/error.hpp>
#include <popart/op/incrementmod.hpp>
#include <popart/popx/op/incrementmodx.hpp>
#include <popart/popx/opxmanager.hpp>

#include "popart/datatype.hpp"
#include "popart/graphcoreoperators.hpp"
#include "popart/op.hpp"
#include "popart/popx/debugcontextx.hpp"
#include "popart/popx/op/elementwisex.hpp"
#include "popart/tensorinfo.hpp"

namespace snap {
class Graph;

namespace program {
class Sequence;
} // namespace program
} // namespace snap

namespace popart {
namespace popx {
class Devicex;

namespace {

std::unique_ptr<EwuComputex> getIncrementModComputex(const Op *op) {
  auto inType = op->inInfo(IncrementModOp::getInIndex()).dataType();
  switch (inType) {
  case DataType::UINT8: {
    return IncrementModComputex<uint8_t>::get(op);
  }
  case DataType::UINT16: {
    return IncrementModComputex<uint16_t>::get(op);
  }
  case DataType::UINT32: {
    return IncrementModComputex<uint32_t>::get(op);
  }
  case DataType::INT8: {
    return IncrementModComputex<int8_t>::get(op);
  }
  case DataType::INT16: {
    return IncrementModComputex<int16_t>::get(op);
  }
  case DataType::INT32: {
    return IncrementModComputex<int32_t>::get(op);
  }
  case DataType::FLOAT16: {
    return IncrementModComputex<float>::get(op);
  }
  case DataType::FLOAT: {
    return IncrementModComputex<float>::get(op);
  }
  default:
    throw internal_error("[] Unsupported DataType {}", inType);
  }
}

} // namespace

template <typename T>
IncrementModComputex<T>::IncrementModComputex(const Op *op_) : op(op_) {
  if (auto incrementModOp = dynamic_cast<const IncrementModOp *>(op)) {
    increment = static_cast<T>(incrementModOp->getIncrement());
    modulus   = static_cast<T>(incrementModOp->getModulus());
  } else if (auto incrementModOp =
                 dynamic_cast<const IncrementModInplaceOp *>(op)) {
    increment = static_cast<T>(incrementModOp->getIncrement());
    modulus   = static_cast<T>(incrementModOp->getModulus());
  } else {
    throw internal_error("[IncrementModComputex] Cannot parse Op {}",
                         op->debugName());
  }
}

template <typename T>
snap::Tensor
IncrementModComputex<T>::outplace(snap::program::Sequence &p,
                                  snap::Graph &g,
                                  const snap::Tensor &t,
                                  const poplar::DebugNameAndId &dnai,
                                  const std::string &s) const {

  popops::expr::Any expr = popops::expr::Rem(
      popops::expr::Add(popops::expr::_1, popops::expr::Const(increment)),
      popops::expr::Const(modulus));

  return snap::popops::map(g, expr, {t}, p, {dnai, s});
}

template <typename T>
void IncrementModComputex<T>::inplace(snap::program::Sequence &p,
                                      snap::Graph &g,
                                      const snap::Tensor &t,
                                      const poplar::DebugNameAndId &dnai,
                                      const std::string &s) const {
  popops::expr::Any expr = popops::expr::Rem(
      popops::expr::Add(popops::expr::_1, popops::expr::Const(increment)),
      popops::expr::Const(modulus));

  snap::popops::mapInPlace(g, expr, {t}, p, {dnai, s});
}

IncrementModOpx::IncrementModOpx(Op *op, Devicex *devicex)
    : ElementWiseUnaryOutplaceOpx(op, devicex, getIncrementModComputex(op)) {
  verifyOp<IncrementModOp>(op, {Onnx::AiGraphcore::OpSet1::IncrementMod});
}

IncrementModInplaceOpx::IncrementModInplaceOpx(Op *op, Devicex *devicex)
    : ElementWiseUnaryInplaceOpx(op, devicex, getIncrementModComputex(op)) {
  verifyOp<IncrementModOp>(op,
                           {Onnx::AiGraphcore::OpSet1::IncrementModInplace});
}

namespace {
OpxCreator<IncrementModOpx>
    incrementModOpxCreator({Onnx::AiGraphcore::OpSet1::IncrementMod});
OpxCreator<IncrementModInplaceOpx> incrementModInplaceOpxCreator(
    Onnx::AiGraphcore::OpSet1::IncrementModInplace);
} // namespace

} // namespace popx
} // namespace popart
