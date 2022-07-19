// Copyright (c) 2020 Graphcore Ltd. All rights reserved.
#ifndef POPART_WILLOW_INCLUDE_POPART_PATTERNS_ATAN2ARG0GRADOPPATTERN_HPP_
#define POPART_WILLOW_INCLUDE_POPART_PATTERNS_ATAN2ARG0GRADOPPATTERN_HPP_
#include <popart/patterns/binarygradoppattern.hpp>

#include "popart/names.hpp"

namespace popart {
class Ir;
class Op;
class Tensor;

// Replace a Atan2Arg0GradOp with x/(x^2+y^2)
// {(fwd_in_y)} -> [Square] -> (tmp1)
// {(fwd_in_x)} -> [Square] -> (tmp2)
// {(tmp1), (tmp2)} -> [Add] -> (tmp3)
// {(fwd_in_x), (tmp3)} -> [Div] -> [ReduceSum] -> (grad_out)
class Atan2Arg0GradOpPattern : public BinaryGradOpPattern {
public:
  bool matches(Op *) const override;

protected:
  virtual TensorId makeAllReplacementOps(Op *op,
                                         Ir *ir,
                                         const Tensor &gradIn,
                                         const Tensor &fwdIn0,
                                         const Tensor &fwdIn1,
                                         const Tensor &fwdOut) const override;
};

} // namespace popart

#endif // POPART_WILLOW_INCLUDE_POPART_PATTERNS_ATAN2ARG0GRADOPPATTERN_HPP_
