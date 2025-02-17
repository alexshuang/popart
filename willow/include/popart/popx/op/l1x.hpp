// Copyright (c) 2018 Graphcore Ltd. All rights reserved.
#ifndef POPART_WILLOW_INCLUDE_POPART_POPX_OP_L1X_HPP_
#define POPART_WILLOW_INCLUDE_POPART_POPX_OP_L1X_HPP_

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

class L1Opx : public PopOpx {
public:
  L1Opx(Op *, Devicex *);
  void grow(snap::program::Sequence &) const final;
  InputCreatorType getInputCreatorType(InIndex) const final;

  snap::Tensor
  unwindTensorLayout(snap::Tensor tensor, InIndex, OutIndex) const override;
  view::RegMap unwindRegion(InIndex, OutIndex) const override;
};

class L1GradOpx : public PopOpx {
public:
  L1GradOpx(Op *, Devicex *);
  void grow(snap::program::Sequence &) const final;
};

} // namespace popx
} // namespace popart

#endif // POPART_WILLOW_INCLUDE_POPART_POPX_OP_L1X_HPP_
