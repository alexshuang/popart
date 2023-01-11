// Copyright (c) 2021 Graphcore Ltd. All rights reserved.
#ifndef POPART_WILLOW_INCLUDE_POPART_POPX_OP_REDUCEMEDIANX_HPP_
#define POPART_WILLOW_INCLUDE_POPART_POPX_OP_REDUCEMEDIANX_HPP_

#include <cstdint>
#include <vector>
#include <popart/popx/opx.hpp>

#include "popart/names.hpp"

namespace poplar {
namespace program {
class Sequence;
} // namespace program
} // namespace poplar

namespace popart {
class Op;

namespace popx {
class Devicex;

class ReduceMedianOpx : public Opx {
public:
  ReduceMedianOpx(Op *, Devicex *);
  void grow(poplar::program::Sequence &) const override;
};

class ReduceMedianGradOpx : public Opx {
public:
  ReduceMedianGradOpx(Op *, Devicex *);
  void grow(poplar::program::Sequence &) const override;
};

namespace reducemedianinternal {

struct PreprocessingParams {
  // Axes that are not reduced during the forward pass.
  std::vector<int64_t> axes_complement;
  // Permutations to move all the reduction axes to the end and back.
  std::vector<unsigned> dim_permute;
  std::vector<unsigned> dim_permute_reverse;
};

// During the forward and backward passes we make sure all reduction axes come
// at the end, then flatten them and finally calculate the median values. The
// result is then reshaped back to the original number of axes and
// reverse-permuted. This function computes the needed permutations.
PreprocessingParams
computePreprocessingParams(const Shape &input_shape,
                           const std::vector<int64_t> &axes);

} // namespace reducemedianinternal

} // namespace popx
} // namespace popart

#endif // POPART_WILLOW_INCLUDE_POPART_POPX_OP_REDUCEMEDIANX_HPP_
