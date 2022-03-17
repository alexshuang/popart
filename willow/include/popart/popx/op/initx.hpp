// Copyright (c) 2018 Graphcore Ltd. All rights reserved.
#ifndef GUARD_NEURALNET_INITX_HPP
#define GUARD_NEURALNET_INITX_HPP

#include <popart/names.hpp>
#include <popart/popx/popopx.hpp>

namespace popart {
namespace popx {

class InitOpx : public PopOpx {
public:
  InitOpx(Op *, Devicex *);
  void grow(snap::program::Sequence &) const final;
  bool outputCreatedExternally(OutIndex) const final { return true; }
};

} // namespace popx
} // namespace popart

#endif
