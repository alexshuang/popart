// Copyright (c) 2020 Graphcore Ltd. All rights reserved.
#ifndef POPART_WILLOW_INCLUDE_POPART_POPX_OP_DYNAMIC_DYNAMICZEROX_HPP_
#define POPART_WILLOW_INCLUDE_POPART_POPX_OP_DYNAMIC_DYNAMICZEROX_HPP_

#include <snap/Tensor.hpp>
#include <popart/names.hpp>
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

class DynamicZeroOpx : public PopOpx {
public:
  DynamicZeroOpx(Op *op, Devicex *devicex) : PopOpx(op, devicex) {}
  void grow(snap::program::Sequence &) const override;
  InputCreatorType getInputCreatorType(InIndex index) const final;
  snap::Tensor unwindTensorLayout(snap::Tensor, InIndex, OutIndex) const final;
  view::RegMap unwindRegion(InIndex, OutIndex) const final;
  virtual snap::Tensor cloneNcopyOpt(snap::program::Sequence &,
                                     const snap::Tensor &) const;
};

class DynamicZeroInplaceOpx : public DynamicZeroOpx {
public:
  DynamicZeroInplaceOpx(Op *op, Devicex *devicex)
      : DynamicZeroOpx(op, devicex) {}
  snap::Tensor cloneNcopyOpt(snap::program::Sequence &,
                             const snap::Tensor &) const override;
};

} // namespace popx
} // namespace popart

#endif // POPART_WILLOW_INCLUDE_POPART_POPX_OP_DYNAMIC_DYNAMICZEROX_HPP_
