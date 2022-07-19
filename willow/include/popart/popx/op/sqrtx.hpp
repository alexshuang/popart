// Copyright (c) 2018 Graphcore Ltd. All rights reserved.
#ifndef POPART_WILLOW_INCLUDE_POPART_POPX_OP_SQRTX_HPP_
#define POPART_WILLOW_INCLUDE_POPART_POPX_OP_SQRTX_HPP_

#include <popart/popx/op/elementwisex.hpp>

namespace snap {
namespace program {
class Sequence;
} // namespace program
} // namespace snap

namespace popart {
class Op;

namespace popx {
class Devicex;

class SqrtOpx : public ElementWiseUnaryOpx {
public:
  SqrtOpx(Op *, Devicex *);
  void grow(snap::program::Sequence &) const final;
};

} // namespace popx
} // namespace popart

#endif // POPART_WILLOW_INCLUDE_POPART_POPX_OP_SQRTX_HPP_
