// Copyright (c) 2020 Graphcore Ltd. All rights reserved.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ext/new_allocator.h>
#include <vector>
#include <poplar/Graph.hpp>
#include <poplar/VariableMappingMethod.hpp>
#include <poprand/RandomGen.hpp>
#include <popart/op/randomnormal.hpp>
#include <popart/popx/op/randomnormalx.hpp>

#include "popart/operators.hpp"
#include "popart/popx/devicex.hpp"
#include "popart/popx/opx.hpp"
#include "popart/popx/opxmanager.hpp"
#include "popart/tensorinfo.hpp"
#include "popart/util.hpp"

namespace poplar {
namespace program {
class Sequence;
} // namespace program
} // namespace poplar

namespace popart {
class Op;

namespace popx {

RandomNormalOpx::RandomNormalOpx(Op *op, Devicex *devicex) : Opx(op, devicex) {
  verifyOp<RandomNormalOp>(op, Onnx::Operators::RandomNormal_1);
}

void RandomNormalOpx::grow(poplar::program::Sequence &prog) const {
  auto &op        = getOp<RandomNormalOp>();
  auto outputInfo = op.outInfo(op.getOutIndex());
  auto shape      = vXtoY<int64_t, std::size_t>(outputInfo.shape());
  auto poplarType = popType(op.outInfo(op.getOutIndex()));

  auto refTensor = graph().addVariable(poplarType,
                                       shape,
                                       poplar::VariableMappingMethod::LINEAR,
                                       debugContext("refTensor"));

  auto output = poprand::normal(graph(),
                                &getInTensor(op.getSeedInIndex()),
                                0u,
                                refTensor,
                                poplarType,
                                op.getMean(),
                                op.getScale(),
                                prog);

  setOutTensor(op.getOutIndex(), output);
}

namespace {
OpxCreator<RandomNormalOpx>
    randomNormalOpxCreator(Onnx::Operators::RandomNormal_1);
}

} // namespace popx
} // namespace popart
