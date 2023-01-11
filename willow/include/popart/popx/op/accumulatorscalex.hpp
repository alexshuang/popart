// Copyright (c) 2020 Graphcore Ltd. All rights reserved.
#ifndef POPART_WILLOW_INCLUDE_POPART_POPX_OP_ACCUMULATORSCALEX_HPP_
#define POPART_WILLOW_INCLUDE_POPART_POPX_OP_ACCUMULATORSCALEX_HPP_

#include <popart/popx/op/varupdatex.hpp>

namespace poplar {
namespace program {
class Sequence;
} // namespace program
} // namespace poplar

namespace popart {
class Op;

namespace popx {
class Devicex;

class AccumulatorScaleOpx : public VarUpdateOpx {
public:
  AccumulatorScaleOpx(Op *, Devicex *);
  void grow(poplar::program::Sequence &) const final;
};

} // namespace popx
} // namespace popart

#endif // POPART_WILLOW_INCLUDE_POPART_POPX_OP_ACCUMULATORSCALEX_HPP_
