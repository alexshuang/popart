// Copyright (c) 2022 Graphcore Ltd. All rights reserved.
#ifndef GUARD_NEURALNET_ROIALIGNX_HPP
#define GUARD_NEURALNET_ROIALIGNX_HPP

#include <popart/popx/devicex.hpp>
#include <popart/popx/popopx.hpp>

namespace popart {
namespace popx {

class RoiAlignOpx : public Opx {
public:
  RoiAlignOpx(Op *, Devicex *);
  ~RoiAlignOpx() override = default;
  virtual void grow(poplar::program::Sequence &) const final;

private:
  poplar::Tensor roiAlignImpl(poplar::program::Sequence &prog,
                              poplar::Tensor &bottomData,
                              poplar::Tensor &bottomRois,
                              poplar::Tensor &bottomBatchIndex) const;
};

class RoiAlignGradOpx : public Opx {
public:
  RoiAlignGradOpx(Op *, Devicex *);
  ~RoiAlignGradOpx() override = default;
  virtual void grow(poplar::program::Sequence &) const final;

private:
  poplar::Tensor roiAlignImpl(poplar::program::Sequence &prog,
                              poplar::Tensor &topDiff,
                              poplar::Tensor &bottomData,
                              poplar::Tensor &bottomRois,
                              poplar::Tensor &bottomBatchIndex) const;
};

} // namespace popx
} // namespace popart
#endif