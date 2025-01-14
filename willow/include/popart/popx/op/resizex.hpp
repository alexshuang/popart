// Copyright (c) 2018 Graphcore Ltd. All rights reserved.
#ifndef POPART_WILLOW_INCLUDE_POPART_POPX_OP_RESIZEX_HPP_
#define POPART_WILLOW_INCLUDE_POPART_POPX_OP_RESIZEX_HPP_

#include <cstdint>
#include <snap/Tensor.hpp>
#include <popart/popx/popopx.hpp>

namespace snap {
namespace program {
class Sequence;
} // namespace program
} // namespace snap

namespace popart {
class Op;

namespace popx {
class Devicex;

struct ResizeParams {
  Shape inShape;
  Shape outShape;
  std::vector<float> scales;
  ResizeMode mode;
  ResizeNearestMode nearestMode;
  ResizeCoordinateTransformationMode coordinateTransformationMode;
};

class ResizeOpx : public PopOpx {
public:
  ResizeOpx(Op *, Devicex *);
  void grow(snap::program::Sequence &) const final;
};

class ResizeGradOpx : public PopOpx {
public:
  ResizeGradOpx(Op *, Devicex *);
  void grow(snap::program::Sequence &) const final;

private:
  snap::Tensor resizeNearestGrad(ResizeGradOp &op,
                                 const snap::Tensor &input,
                                 ResizeParams &params,
                                 snap::program::Sequence &prog) const;
  snap::Tensor reduceDimension(snap::program::Sequence &,
                               const snap::Tensor &input,
                               int dimension,
                               float scale) const;
  snap::Tensor padDimension(snap::program::Sequence &,
                            const snap::Tensor &input,
                            int dimension,
                            int64_t newSize,
                            float scale) const;
};

} // namespace popx
} // namespace popart

#endif // POPART_WILLOW_INCLUDE_POPART_POPX_OP_RESIZEX_HPP_
