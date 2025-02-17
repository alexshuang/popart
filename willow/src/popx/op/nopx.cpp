// Copyright (c) 2020 Graphcore Ltd. All rights reserved.
#include <popart/op/nop.hpp>
#include <popart/popx/op/nopx.hpp>
#include <popart/popx/opxmanager.hpp>

#include "popart/graphcoreoperators.hpp"
#include "popart/popx/popopx.hpp"

namespace snap {
namespace program {
class Sequence;
} // namespace program
} // namespace snap

namespace popart {
class Op;

namespace popx {
class Devicex;

NopOpx::NopOpx(Op *op, Devicex *devicex) : PopOpx(op, devicex) {
  verifyOp<NopOp>(op, Onnx::CustomOperators::Nop_1);
}

void NopOpx::grow(snap::program::Sequence &prog) const {
  auto input  = getInTensor(NopOp::getInIndex());
  auto output = cloneNcopy(prog, input);
  setOutTensor(NopOp::getOutIndex(), output);
}

namespace {
OpxCreator<NopOpx> nopOpxCreator(Onnx::CustomOperators::Nop_1);
} // namespace

} // namespace popx
} // namespace popart
