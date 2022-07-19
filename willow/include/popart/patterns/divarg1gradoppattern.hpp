// Copyright (c) 2018 Graphcore Ltd. All rights reserved.
#ifndef POPART_WILLOW_INCLUDE_POPART_PATTERNS_DIVARG1GRADOPPATTERN_HPP_
#define POPART_WILLOW_INCLUDE_POPART_PATTERNS_DIVARG1GRADOPPATTERN_HPP_

#include <popart/patterns/binarygradoppattern.hpp>

#include "popart/names.hpp"

namespace popart {
class Ir;
class Op;
class Tensor;

// Replace a DivArg1GradOp with
// (fwd_in1) -> [Square] -> (tmp1)
// {(grad_in), (fwd_in0)} -> [Mul] -> (tmp2)
// {(tmp2), (tmp1)} -> [Div] -> [Negate] -> [ReduceSum] -> (grad_out)
class DivArg1GradOpPattern : public BinaryGradOpPattern {
public:
  // Does op at the root of the
  // pattern make a match?
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

#endif // POPART_WILLOW_INCLUDE_POPART_PATTERNS_DIVARG1GRADOPPATTERN_HPP_
