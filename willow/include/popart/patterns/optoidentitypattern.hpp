// Copyright (c) 2018 Graphcore Ltd. All rights reserved.
#ifndef GUARD_NEURALNET_REDUCE_SUM_TO_IDENTITY_PATTERN_HPP
#define GUARD_NEURALNET_REDUCE_SUM_TO_IDENTITY_PATTERN_HPP

#include <memory>
#include <vector>
#include <popart/patterns/sequenceexpander.hpp>

namespace popart {
class Op;

// Replace ops that return their only input unchanged with an identity op
class OpToIdentityPattern : public SequenceExpander {
public:
  // Does op at the root of the
  // pattern make a match?
  bool matches(Op *) const override;
  // what phase should this Pattern run in? PRETOPOCONS, as it does not
  // handle topological constraints.

private:
  // Replace the given op with the returned sequence of ops
  std::vector<std::unique_ptr<Op>> sequence(Op *op) const final;
};
} // namespace popart

#endif
