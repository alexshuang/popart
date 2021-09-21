// Copyright (c) 2018 Graphcore Ltd. All rights reserved.
#ifndef GUARD_NEURALNET_RESIZEX_HPP
#define GUARD_NEURALNET_RESIZEX_HPP

#include <popart/names.hpp>
#include <popart/popx/popopx.hpp>

namespace popart {

namespace popx {

class ResizeOpx : public PopOpx {
public:
  ResizeOpx(Op *, Devicex *);
  void grow(poplar::program::Sequence &) const final;
};

class ResizeGradOpx : public PopOpx {
public:
  ResizeGradOpx(Op *, Devicex *);
  void grow(poplar::program::Sequence &) const final;

private:
  snap::Tensor reduceDimension(poplar::program::Sequence &,
                               const snap::Tensor &input,
                               int dimension,
                               float scale) const;
  snap::Tensor padDimension(poplar::program::Sequence &,
                            const snap::Tensor &input,
                            int dimension,
                            int64_t newSize,
                            float scale) const;
};

} // namespace popx
} // namespace popart

#endif
