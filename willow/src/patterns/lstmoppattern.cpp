// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#include "popart/names.hpp"
#include <popart/graph.hpp>
#include <popart/ir.hpp>
#include <popart/op/concat.hpp>
#include <popart/op/lstm.hpp>
#include <popart/op/split.hpp>
#include <popart/op/transpose.hpp>
#include <popart/patterns/lstmoppattern.hpp>
#include <popart/patterns/patterns.hpp>
#include <popart/tensor.hpp>
#include <popart/tensorindex.hpp>
#include <popart/transforms/transformbuilder.hpp>

namespace popart {

bool LSTMPattern::matches(Op *op) const {
  return op->isConvertibleTo<LSTMOp>();
}

bool LSTMPattern::apply(Op *op) const {
  TransformBuilder builder(op->getGraph());
  auto lstmOp         = dynamic_cast<LSTMOp *>(op);
  auto hiddenSize     = lstmOp->getHiddenSize();
  auto sequenceLength = lstmOp->getMaxSeqLength();
  auto vgraph         = op->getOptionalVGraphId();
  auto pstage         = op->getOptionalPipelineStage();
  auto pphase         = op->getOptionalExecutionPhase();

  const int onnxInputGateIndex  = 0;
  const int onnxOutputGateIndex = 1;
  const int onnxForgetGateIndex = 2;
  const int onnxCellGateIndex   = 3;

  auto concat = [&](std::vector<TensorId> inputs, auto axis) {
    return builder.concat(
        inputs,
        axis,
        vgraph,
        pstage,
        pphase,
        "concat",
        op->getIr().createIntermediateTensorId(inputs.at(0)).str());
  };

  auto split = [&](TensorId w, auto axis, std::vector<int64_t> splitSizes) {
    return builder.split(w, axis, splitSizes, vgraph, pstage, pphase, "split");
  };

  auto transpose = [&](TensorId x, std::vector<int64_t> perm) {
    return builder.transpose(x,
                             perm,
                             vgraph,
                             pstage,
                             pphase,
                             "transpose",
                             op->getIr().createIntermediateTensorId(x).str());
  };

  auto add = [&](std::vector<TensorId> inputs) {
    return builder.add(
        inputs,
        vgraph,
        pstage,
        pphase,
        "add",
        op->getIr().createIntermediateTensorId(inputs.at(0)).str());
  };

  auto reshapeWeights = [&](TensorId w) {
    auto splits = split(w, 1, std::vector<int64_t>(4, hiddenSize));

    std::vector<TensorId> transposes;
    for (auto &s : splits) {
      auto t = transpose(s, {0, 2, 1});
      transposes.push_back(t);
    }

    return concat({transposes.at(onnxForgetGateIndex),
                   transposes.at(onnxInputGateIndex),
                   transposes.at(onnxCellGateIndex),
                   transposes.at(onnxOutputGateIndex)},
                  0);
  };

  auto reshapeBiases = [&](TensorId b) {
    auto splits  = split(b, 1, std::vector<int64_t>(8, hiddenSize));
    auto concat0 = concat({splits.at(onnxForgetGateIndex),
                           splits.at(onnxInputGateIndex),
                           splits.at(onnxCellGateIndex),
                           splits.at(onnxOutputGateIndex)},
                          0);
    auto concat1 = concat({splits.at(onnxForgetGateIndex + 4),
                           splits.at(onnxInputGateIndex + 4),
                           splits.at(onnxCellGateIndex + 4),
                           splits.at(onnxOutputGateIndex + 4)},
                          0);
    return add({concat0, concat1});
  };

  auto inputWeights      = lstmOp->inId(LSTMOp::getWeightsInIndex());
  auto recurrenceWeights = lstmOp->inId(LSTMOp::getRecurrenceInIndex());
  inputWeights           = reshapeWeights(inputWeights);
  recurrenceWeights      = reshapeWeights(recurrenceWeights);
  auto concatWeights     = concat({inputWeights, recurrenceWeights}, 1);

  auto biases = TensorId();
  if (lstmOp->input->hasIndex(LSTMOp::getBiasInIndex())) {
    auto x = lstmOp->inId(LSTMOp::getBiasInIndex());
    biases = reshapeBiases(x);
  }

  auto initialState = TensorId();
  if (lstmOp->input->hasIndex(LSTMOp::getInitialHInIndex()) &&
      lstmOp->input->hasIndex(LSTMOp::getInitialCInIndex())) {
    auto initH   = lstmOp->inId(LSTMOp::getInitialHInIndex());
    auto initC   = lstmOp->inId(LSTMOp::getInitialCInIndex());
    initialState = concat({initH, initC}, 0);
  }

  auto input = lstmOp->inId(LSTMOp::getInputInIndex());

  auto output      = lstmOp->outId(LSTMOp::getOutputOutIndex());
  auto hiddenState = lstmOp->outId(LSTMOp::getHiddenStateOutIndex());
  auto cellState   = lstmOp->outId(LSTMOp::getCellStateOutIndex());

  lstmOp->disconnectAllInputs();
  lstmOp->disconnectAllOutputs();

  Graph &graph = op->getGraph();
  auto newLstm = graph.createOp<PopartLSTMOp>(Onnx::CustomOperators::LSTM_1,
                                              true,
                                              lstmOp->getActivation(),
                                              lstmOp->getRecurrentActivation(),
                                              op->settings.copy("PopartLSTM"));

  newLstm->connectInTensor(PopartLSTMOp::getInputInIndex(), input);
  newLstm->connectInTensor(PopartLSTMOp::getWeightsInIndex(), concatWeights);
  if (biases != TensorId()) {
    newLstm->connectInTensor(PopartLSTMOp::getBiasesInIndex(), biases);
  }
  if (initialState != TensorId()) {
    newLstm->connectInTensor(PopartLSTMOp::getInitialStateInIndex(),
                             initialState);
  }
  if (lstmOp->hasSeqLenInput()) {
    auto seq_lens = lstmOp->inId(LSTMOp::getSequenceLensInIndex());
    newLstm->connectInTensor(PopartLSTMOp::getSequenceLensInIndex(), seq_lens);
  }
  newLstm->createAndConnectOutTensor(
      PopartLSTMOp::getOutputOutIndex(),
      newLstm->getIr().createIntermediateTensorId(output));
  newLstm->createAndConnectOutTensor(
      PopartLSTMOp::getCellStateOutIndex(),
      newLstm->getIr().createIntermediateTensorId(cellState));
  newLstm->setup();

  // Unsqueeze the num_directions axis on the outputs
  builder.unsqueeze(newLstm->outId(PopartLSTMOp::getOutputOutIndex()),
                    {1},
                    output,
                    vgraph,
                    pstage,
                    pphase,
                    "");
  builder.unsqueeze(newLstm->outId(PopartLSTMOp::getCellStateOutIndex()),
                    {0},
                    cellState,
                    vgraph,
                    pstage,
                    pphase,
                    "");
  builder.slice(newLstm->outId(PopartLSTMOp::getOutputOutIndex()),
                {sequenceLength - 1},
                {sequenceLength},
                {0},
                hiddenState,
                vgraph,
                pstage,
                pphase,
                "");

  op->getGraph().eraseOp(op->id);
  return true;
}

// Disabled by default
namespace {
static PatternCreator<LSTMPattern>
    lstmPattern(PreAliasPatternType::LSTMOp, "LSTMOp", false);
} // namespace

} // namespace popart
