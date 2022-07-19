// Copyright (c) 2018 Graphcore Ltd. All rights reserved.
#ifndef POPART_WILLOW_INCLUDE_POPART_POPX_OP_SINHX_HPP_
#define POPART_WILLOW_INCLUDE_POPART_POPX_OP_SINHX_HPP_

#include "popart/popx/debugcontextx.hpp"
#include <memory>
#include <snap/Tensor.hpp>
#include <string>
#include <popart/popx/op/elementwisex.hpp>

#include "popart/popx/popopx.hpp"

namespace snap {
class Graph;

namespace program {
class Sequence;
} // namespace program
} // namespace snap

namespace popart {
class Op;

namespace popx {
class Devicex;

class SinhComputex : public EwuComputex {

public:
  SinhComputex() = default;

  snap::Tensor outplace(snap::program::Sequence &,
                        snap::Graph &,
                        const snap::Tensor &,
                        const poplar::DebugNameAndId &,
                        const std::string &) const final;

  void inplace(snap::program::Sequence &,
               snap::Graph &,
               const snap::Tensor &,
               const poplar::DebugNameAndId &,
               const std::string &) const final;

  static std::unique_ptr<EwuComputex> get() {
    return std::unique_ptr<EwuComputex>(new SinhComputex);
  }
};

class SinhOpx : public ElementWiseUnaryOutplaceOpx {
public:
  SinhOpx(Op *, Devicex *);
};

class SinhInplaceOpx : public ElementWiseUnaryInplaceOpx {
public:
  SinhInplaceOpx(Op *, Devicex *);
};

class SinhGradOpx : public PopOpx {
public:
  SinhGradOpx(Op *, Devicex *);
  void grow(snap::program::Sequence &) const final;
};

} // namespace popx
} // namespace popart

#endif // POPART_WILLOW_INCLUDE_POPART_POPX_OP_SINHX_HPP_
