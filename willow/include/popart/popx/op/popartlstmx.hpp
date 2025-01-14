// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#ifndef POPART_WILLOW_INCLUDE_POPART_POPX_OP_POPARTLSTMX_HPP_
#define POPART_WILLOW_INCLUDE_POPART_POPX_OP_POPARTLSTMX_HPP_

#include <memory>
#include <set>
#include <snap/Program.hpp>
#include <snap/Tensor.hpp>
#include <poplar/Type.hpp>
#include <popnn/Lstm.hpp>
#include <popops/Zero.hpp>
#include <popart/names.hpp>
#include <popart/popx/devicex.hpp>
#include <popart/popx/irlowering.hpp>
#include <popart/popx/op/lstmxutil.hpp>
#include <popart/popx/popopx.hpp>

#include "popart/logging.hpp"
#include "popart/popx/debugcontextx.hpp"
#include "popart/tensordebuginfo.hpp"

namespace poplar {
class Tensor;
} // namespace poplar

namespace popart {
class Op;
class PopartLSTMGradOp;
class PopartLSTMOp;

namespace popx {

template <typename LSTMOP> class PopartLSTMOpxBase : public PopOpx {
public:
  PopartLSTMOpxBase(Op *op, Devicex *devicex) : PopOpx(op, devicex) {}

protected:
  popnn::lstm::LstmParams
  createLSTMParams(const LSTMOP &lstm_op,
                   const snap::Tensor &seq_lens_t) const {
    auto inInfo = lstm_op.inInfo(LSTMOP::getInputInIndex());

    auto inputSize    = static_cast<unsigned>(lstm_op.getInputSize());
    auto maxSeqLength = static_cast<unsigned>(lstm_op.getMaxSeqLength());
    auto batchSize    = static_cast<unsigned>(lstm_op.getBatchSize());
    auto hiddenSize   = static_cast<unsigned>(lstm_op.getHiddenSize());

    if (seq_lens_t.valid()) {

      auto params =
          popnn::lstm::LstmParams(popType(inInfo),
                                  batchSize,
                                  maxSeqLength,
                                  seq_lens_t.getPoplarTensor(),
                                  {inputSize, hiddenSize},
                                  convert(lstm_op.getActivation()),
                                  convert(lstm_op.getRecurrentActivation()));
      params.outputFullSequence = lstm_op.outputFullSequence;
      return params;
    }
    auto params =
        popnn::lstm::LstmParams(popType(inInfo),
                                batchSize,
                                maxSeqLength,
                                {inputSize, hiddenSize},
                                convert(lstm_op.getActivation()),
                                convert(lstm_op.getRecurrentActivation()));
    params.outputFullSequence = lstm_op.outputFullSequence;
    return params;
  }

  snap::Tensor createBiasesInput() const {
    auto &lstmOp = getOp<LSTMOP>();
    auto seq_len = getSeqLens();
    return snap::Tensor{
        popnn::lstm::createWeightsBiases(graph().getPoplarGraph(),
                                         createLSTMParams(lstmOp, seq_len),
                                         debugContext("createWeights"),
                                         dv_p->lowering().lstmOptions,
                                         &dv_p->matmulCache),
        graph()};
  }

  snap::Tensor getBiases(snap::program::Sequence &prog) const {
    auto &lstmOp = getOp<LSTMOP>();

    if (hasInput(lstmOp.getBiasesInIndex())) {
      return getInTensor(lstmOp.getBiasesInIndex());
    } else {
      auto biases = createBiasesInput();
      popops::zero(graph().getPoplarGraph(),
                   biases.getPoplarTensor(),
                   prog.getPoplarSequence(),
                   debugContext("zeroBiases"));
      return biases;
    }
  }

  popnn::lstm::LstmState createInitialStateInput() const {
    auto &lstmOp = getOp<LSTMOP>();
    auto seq_len = getSeqLens();
    return createInitialState(graph().getPoplarGraph(),
                              createLSTMParams(lstmOp, seq_len),
                              debugContext("createInitialState"),
                              dv_p->lowering().lstmOptions,
                              &dv_p->matmulCache);
  }

  popnn::lstm::LstmState getInitialState(snap::program::Sequence &prog) const {
    auto &lstmOp = getOp<LSTMOP>();

    if (hasInput(lstmOp.getInitialStateInIndex())) {
      auto initialState =
          getInTensor(lstmOp.getInitialStateInIndex()).getPoplarTensor();
      auto initialOutput    = initialState.slice(0, 1).squeeze({0});
      auto initialCellState = initialState.slice(1, 2).squeeze({0});
      return {initialOutput, initialCellState};
    } else {
      auto initialState = createInitialStateInput();
      zeroInitialState(graph().getPoplarGraph(),
                       initialState,
                       prog.getPoplarSequence(),
                       debugContext());
      return initialState;
    }
  }

  snap::Tensor getSeqLens() const {
    if (hasInput(LSTMOP::getSequenceLensInIndex())) {
      auto &lstmOp = getOp<LSTMOP>();
      logging::opx::debug("Checking seq len for {} index {}",
                          lstmOp.debugName(),
                          LSTMOP::getSequenceLensInIndex());
      return getInTensor(LSTMOP::getSequenceLensInIndex())
          .reinterpret(poplar::UNSIGNED_INT);
    } else {
      return snap::Tensor{};
    }
  }

  popnn::lstm::LstmWeights getWeights(snap::program::Sequence &prog) const {
    auto &lstmOp    = getOp<LSTMOP>();
    auto inputSize  = lstmOp.getInputSize();
    auto hiddenSize = lstmOp.getHiddenSize();

    auto weights = getInTensor(lstmOp.getWeightsInIndex()).getPoplarTensor();
    auto biases  = getBiases(prog).getPoplarTensor();

    auto inputWeights  = weights.slice(0, inputSize, 1);
    auto outputWeights = weights.slice(inputSize, inputSize + hiddenSize, 1);
    popnn::lstm::LstmWeights lstmWeights = {
        inputWeights, outputWeights, biases};
    return lstmWeights;
  }
};

class PopartLSTMOpx : public PopartLSTMOpxBase<PopartLSTMOp> {
public:
  PopartLSTMOpx(Op *, Devicex *);
  void grow(snap::program::Sequence &) const final;

  InputCreatorType getInputCreatorType(InIndex) const final;
  snap::Tensor
  createInputTensor(InIndex index,
                    const poplar::DebugNameAndId &dnai) const final;
  std::set<TensorId> mustExistBeforeCreate(InIndex) const;

private:
  snap::Tensor createLSTMInput() const;
  snap::Tensor createWeightsInput() const;
  std::unique_ptr<poplar::Tensor> getIntermediates() const;
};

class PopartLSTMGradOpx : public PopartLSTMOpxBase<PopartLSTMGradOp> {
public:
  PopartLSTMGradOpx(Op *, Devicex *);
  void grow(snap::program::Sequence &) const final;
};

} // namespace popx
} // namespace popart

#endif // POPART_WILLOW_INCLUDE_POPART_POPX_OP_POPARTLSTMX_HPP_
