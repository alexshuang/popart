// Copyright (c) 2018 Graphcore Ltd. All rights reserved.
#ifndef POPART_WILLOW_INCLUDE_POPART_POPX_OP_REDUCEMEANX_HPP_
#define POPART_WILLOW_INCLUDE_POPART_POPX_OP_REDUCEMEANX_HPP_

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

class ReduceMeanOpx : public PopOpx {
public:
  ReduceMeanOpx(Op *, Devicex *);
  void grow(snap::program::Sequence &) const override;
};

class ReduceMeanGradOpx : public PopOpx {
public:
  ReduceMeanGradOpx(Op *, Devicex *);
  void grow(snap::program::Sequence &) const override;
};

} // namespace popx
} // namespace popart

#endif // POPART_WILLOW_INCLUDE_POPART_POPX_OP_REDUCEMEANX_HPP_
