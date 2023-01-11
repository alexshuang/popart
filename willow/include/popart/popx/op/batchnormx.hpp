// Copyright (c) 2018 Graphcore Ltd. All rights reserved.
#ifndef POPART_WILLOW_INCLUDE_POPART_POPX_OP_BATCHNORMX_HPP_
#define POPART_WILLOW_INCLUDE_POPART_POPX_OP_BATCHNORMX_HPP_

#include <tuple>
#include <poplar/Tensor.hpp>
#include <popart/popx/op/normx.hpp>
#include <popart/vendored/optional.hpp>

namespace poplar {
namespace program {
class Sequence;
} // namespace program
} // namespace poplar

namespace popart {

class BatchNormOp;
class BatchNormGradOp;
class Op;

namespace popx {
class Devicex;

class BatchNormOpx : public NormOpx {
public:
  BatchNormOpx(Op *, Devicex *);
  void grow(poplar::program::Sequence &) const final;

private:
  // Output type for growSpatial.
  struct GrowSpatialOutput {
    poplar::Tensor y;
    nonstd::optional<poplar::Tensor> mean;
    nonstd::optional<poplar::Tensor> var;
    nonstd::optional<poplar::Tensor> savedMean;
    nonstd::optional<poplar::Tensor> savedVar;
  };

  poplar::Tensor batchNormalise(poplar::program::Sequence &prog,
                                const poplar::Tensor &x,
                                const poplar::Tensor &scale,
                                const poplar::Tensor &b,
                                const poplar::Tensor &mean,
                                const poplar::Tensor &invSd) const;

  GrowSpatialOutput growSpatial(poplar::program::Sequence &prog,
                                BatchNormOp &op,
                                poplar::Tensor &x,
                                poplar::Tensor &scale,
                                poplar::Tensor &b,
                                poplar::Tensor &mean,
                                poplar::Tensor &var) const;
};

class BatchNormGradOpx : public NormOpx {
public:
  BatchNormGradOpx(Op *, Devicex *);
  void grow(poplar::program::Sequence &) const final;

private:
  // Output type for growSpatial.
  struct GrowSpatialOutput {
    poplar::Tensor xGrad;
    poplar::Tensor scaleGrad;
    poplar::Tensor bGrad;
  };

  std::tuple<poplar::Tensor, poplar::Tensor, poplar::Tensor>
  batchNormaliseGrad(poplar::program::Sequence &prog,
                     const poplar::Tensor &x,
                     const poplar::Tensor &scale,
                     const poplar::Tensor &mean,
                     const poplar::Tensor &invSd,
                     const poplar::Tensor &yGrad) const;

  GrowSpatialOutput growSpatial(poplar::program::Sequence &prog,
                                BatchNormGradOp &op,
                                poplar::Tensor &x,
                                poplar::Tensor &scale,
                                poplar::Tensor &mean,
                                poplar::Tensor &var,
                                poplar::Tensor &yGrad) const;
};

} // namespace popx
} // namespace popart

#endif // POPART_WILLOW_INCLUDE_POPART_POPX_OP_BATCHNORMX_HPP_
