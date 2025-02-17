// Copyright (c) 2021 Graphcore Ltd. All rights reserved.
#include <snap/Graph.hpp>
#include <snap/Program.hpp>
#include <snap/Tensor.hpp>
#include <vector>
#include <poplar/ArrayRef.hpp>
#include <popops/GatherStatistics.hpp>
#include <poputil/TileMapping.hpp>
#include <popart/op/histogram.hpp>
#include <popart/popx/op/histogramx.hpp>
#include <popart/popx/opxmanager.hpp>

#include "popart/graphcoreoperators.hpp"
#include "popart/popx/popopx.hpp"

namespace popart {
class Op;

namespace popx {
class Devicex;

void HistogramOpx::grow(snap::program::Sequence &prog) const {
  auto &op    = getOp<HistogramOp>();
  auto levels = op.getLevels();

  auto levelsT = graph().addConstant(getInTensor(op.getInIndex()).elementType(),
                                     {levels.size()},
                                     poplar::ArrayRef<float>(levels),
                                     debugContext("levels"));
  poputil::mapTensorLinearly(graph().getPoplarGraph(),
                             levelsT.getPoplarTensor());

  auto out = popops::histogram(
      graph().getPoplarGraph(),
      getInTensor(op.getInIndex()).flatten().getPoplarTensor(),
      levelsT.getPoplarTensor(),
      op.getAbsoluteOfInput(),
      prog.getPoplarSequence(),
      debugContext());

  setOutTensor(op.getOutIndex(), snap::Tensor{out, graph()});
}

HistogramOpx::HistogramOpx(Op *op, Devicex *devicex) : PopOpx(op, devicex) {
  verifyOp<HistogramOp>(op);
}

namespace {
OpxCreator<HistogramOpx> histogramOpxCreator(Onnx::CustomOperators::Histogram);
} // namespace

} // namespace popx
} // namespace popart
