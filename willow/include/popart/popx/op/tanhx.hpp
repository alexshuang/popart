// Copyright (c) 2018 Graphcore Ltd. All rights reserved.
#ifndef POPART_WILLOW_INCLUDE_POPART_POPX_OP_TANHX_HPP_
#define POPART_WILLOW_INCLUDE_POPART_POPX_OP_TANHX_HPP_

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

class TanhOpx : public PopOpx {
public:
  TanhOpx(Op *, Devicex *);
  void grow(snap::program::Sequence &) const final;
  InputCreatorType getInputCreatorType(InIndex) const final;
  snap::Tensor unwindTensorLayout(snap::Tensor tensor,
                                  InIndex inIndex,
                                  OutIndex outIndex) const final;
  view::RegMap unwindRegion(InIndex, OutIndex) const final;
};

class TanhGradOpx : public PopOpx {
public:
  TanhGradOpx(Op *, Devicex *);
  void grow(snap::program::Sequence &) const final;
};

} // namespace popx
} // namespace popart

#endif // POPART_WILLOW_INCLUDE_POPART_POPX_OP_TANHX_HPP_
