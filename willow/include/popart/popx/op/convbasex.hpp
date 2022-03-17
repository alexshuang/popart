// Copyright (c) 2020 Graphcore Ltd. All rights reserved.
#ifndef GUARD_NEURALNET_CONVBASEX_HPP
#define GUARD_NEURALNET_CONVBASEX_HPP

#include <poplin/Convolution.hpp>
#include <popart/op/convbase.hpp>
#include <popart/popx/popopx.hpp>

namespace popart {

// class MultiConvBaseOp;

namespace popx {

class MultiConvBaseOpx : public PopOpx {
public:
  MultiConvBaseOpx(Op *op, Devicex *dv) : PopOpx(op, dv) {}
  snap::Tensor
  createInputTensor(InIndex index,
                    const poplar::DebugNameAndId &dnai) const final;
  std::set<TensorId> mustExistBeforeCreate(InIndex index0) const final;
  InputCreatorType getInputCreatorType(InIndex) const final;
  void grow(snap::program::Sequence &) const final;

  poplar::OptionFlags getConvOptions(int, std::string pass = "") const;
  std::string getFwdPassFlagString() const;

  virtual std::vector<snap::Tensor>
  convolve(snap::program::Sequence &prog,
           const std::vector<snap::Tensor> &weights) const {
    throw error("No 'convolve' implementation for {}", op_p->opid);
  }
  virtual snap::Tensor createDataInput(const poplar::DebugNameAndId &dnai,
                                       int convIndex) const {
    throw error("No 'createDataInput' implementation for {}", op_p->opid);
  }
  virtual snap::Tensor createWeightsInput(const poplar::DebugNameAndId &dnai,
                                          int convIndex) const {
    throw error("No 'createWeightsInput' implementation for {}", op_p->opid);
  }
  bool isWeightsInIndex(InIndex) const;
  bool isDataInIndex(InIndex) const;

  void verifyCacheSizeUnchanged(size_t beforeCacheSize) const;
};

// Returned the canonicalized for of the conv parameters
ConvParameters canonicalizeConvParams(const ConvParameters &param);

// Convert the conv parameters from the fwd conv into the form that
// can be used by the data grad conv
ConvParameters getConvGradParameters(const ConvParameters &fwdParams);

// Convert the conv parameters from the fwd conv into the form that
// can be used by the weights grad conv
ConvParameters getConvWeightUpdateParameters(const ConvParameters &fwdParams);

poplin::ConvParams getPoplarConvParams(const ConvParameters &param);
ConvParameters convertPoplarConvParameters(const poplin::ConvParams &popParams);

snap::Tensor reshapeOnnxWeightsForPoplar(const snap::Tensor &weights,
                                         std::size_t chansOut,
                                         std::size_t chansIn,
                                         const ConvParameters &params);

class MultiConvWeightsGradBaseOpx : public PopOpx {
public:
  MultiConvWeightsGradBaseOpx(Op *op, Devicex *dv) : PopOpx(op, dv) {}
  void grow(snap::program::Sequence &) const final;
  virtual std::vector<snap::Tensor>
  calculateWeightDeltas(snap::program::Sequence &) const {
    throw error("No 'calculateWeightDeltas' implementation for {}", op_p->opid);
  }

  poplar::OptionFlags getConvOptions(int convIndex = 0) const;

  void verifyCacheSizeUnchanged(size_t beforeCacheSize) const;
};

} // namespace popx
} // namespace popart

#endif
