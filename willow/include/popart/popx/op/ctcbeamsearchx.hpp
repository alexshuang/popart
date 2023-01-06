// Copyright (c) 2021 Graphcore Ltd. All rights reserved.
#ifndef POPART_WILLOW_INCLUDE_POPART_POPX_OP_CTCBEAMSEARCHX_HPP_
#define POPART_WILLOW_INCLUDE_POPART_POPX_OP_CTCBEAMSEARCHX_HPP_

#include <memory>

#include "popart/popx/opx.hpp"

namespace poplar {
namespace program {
class Sequence;
} // namespace program
} // namespace poplar

namespace popnn {
namespace ctc {
class Plan;
} // namespace ctc
} // namespace popnn

namespace popart {
class Op;

namespace popx {
class Devicex;

class CtcBeamSearchDecoderOpx : public Opx {
public:
  CtcBeamSearchDecoderOpx(Op *op, Devicex *device);
  ~CtcBeamSearchDecoderOpx();

  void grow(poplar::program::Sequence &prog) const final;

private:
  // Unique pointer so we can forward-declare to avoid including poplar headers.
  std::unique_ptr<popnn::ctc::Plan> plan;
};
} // namespace popx
} // namespace popart

#endif // POPART_WILLOW_INCLUDE_POPART_POPX_OP_CTCBEAMSEARCHX_HPP_
