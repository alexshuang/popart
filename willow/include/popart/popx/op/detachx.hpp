// Copyright (c) 2021 Graphcore Ltd. All rights reserved.
#ifndef POPART_WILLOW_INCLUDE_POPART_POPX_OP_DETACHX_HPP_
#define POPART_WILLOW_INCLUDE_POPART_POPX_OP_DETACHX_HPP_

#include <popart/popx/op/elementwisex.hpp>

namespace poplar {
namespace program {
class Sequence;
} // namespace program
} // namespace poplar

namespace popart {
class Op;

namespace popx {
class Devicex;

class DetachOpx : public ElementWiseUnaryOpx {

public:
  DetachOpx(popart::Op *, popart::popx::Devicex *);
  void grow(poplar::program::Sequence &) const;
};

class DetachInplaceOpx : public ElementWiseUnaryOpx {
public:
  DetachInplaceOpx(Op *, Devicex *);
  void grow(poplar::program::Sequence &) const final;
};

} // namespace popx
} // namespace popart

#endif // POPART_WILLOW_INCLUDE_POPART_POPX_OP_DETACHX_HPP_
