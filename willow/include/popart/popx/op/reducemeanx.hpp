// Copyright (c) 2018 Graphcore Ltd. All rights reserved.
#ifndef GUARD_NEURALNET_REDUCEMEANX_HPP
#define GUARD_NEURALNET_REDUCEMEANX_HPP

#include <popart/names.hpp>
#include <popart/popx/opx.hpp>

namespace popart {
namespace popx {

class ReduceMeanOpx : public Opx {
public:
  ReduceMeanOpx(Op *, Devicex *);
  void grow(poplar::program::Sequence &) const override;
};

class ReduceMeanGradOpx : public Opx {
public:
  ReduceMeanGradOpx(Op *, Devicex *);
  void grow(poplar::program::Sequence &) const override;
};

} // namespace popx
} // namespace popart

#endif
