// Copyright (c) 2020 Graphcore Ltd. All rights reserved.
#ifndef POPART_WILLOW_INCLUDE_POPART_POPX_OP_CONVBASEX_HPP_
#define POPART_WILLOW_INCLUDE_POPART_POPX_OP_CONVBASEX_HPP_

#include "popart/popx/debugcontextx.hpp"
#include <cstddef>
#include <set>
#include <snap/Tensor.hpp>
#include <string>
#include <vector>
#include <poplar/OptionFlags.hpp>
#include <poplin/ConvParams.hpp>
#include <popart/op/convbase.hpp>
#include <popart/popx/popopx.hpp>

#include "popart/error.hpp"
#include "popart/logging.hpp"
#include "popart/names.hpp"
#include "popart/op.hpp"
#include "popart/operatoridentifier.hpp"

namespace snap {
namespace program {
class Sequence;
} // namespace program
} // namespace snap

namespace popart {

// class MultiConvBaseOp;

namespace popx {
class Devicex;

class MultiConvBaseOpx : public PopOpx {
public:
  MultiConvBaseOpx(Op *op, Devicex *dv) : PopOpx(op, dv) {}
  snap::Tensor
  createInputTensor(InIndex index,
                    const poplar::DebugNameAndId &dnai) const final;
  std::set<TensorId> mustExistBeforeCreate(InIndex index0) const final;
  InputCreatorType getInputCreatorType(InIndex) const;
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

#endif // POPART_WILLOW_INCLUDE_POPART_POPX_OP_CONVBASEX_HPP_
