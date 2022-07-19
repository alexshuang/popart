// Copyright (c) 2021 Graphcore Ltd. All rights reserved.
#ifndef POPART_WILLOW_INCLUDE_POPART_PATTERNS_SCANTOLOOPPATTERN_HPP_
#define POPART_WILLOW_INCLUDE_POPART_PATTERNS_SCANTOLOOPPATTERN_HPP_

#include <vector>

#include "popart/patterns/pattern.hpp"

namespace popart {
class Op;
class Tensor;

// Replace a ScanOp with LoopOp
class ScanToLoopPattern : public PreAliasPattern {
public:
  // Does op at the root of the
  // pattern make a match?
  bool matches(Op *) const override;
  // If this Pattern were to be applied at op, which
  // Tensors in the subgraph centered (rooted) on op
  // would be touched?
  std::vector<const Tensor *> touches(Op *) const override;
  // apply the pattern,
  // changes the graph of the op
  bool apply(Op *) const override;
};

} // namespace popart

#endif // POPART_WILLOW_INCLUDE_POPART_PATTERNS_SCANTOLOOPPATTERN_HPP_
