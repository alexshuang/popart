// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#ifndef POPART_WILLOW_INCLUDE_POPART_OP_LESS_HPP_
#define POPART_WILLOW_INCLUDE_POPART_OP_LESS_HPP_

#include <memory>
#include <vector>
#include <popart/op.hpp>
#include <popart/op/elementwise.hpp>

namespace popart {
struct OperatorIdentifier;

class LessOp : public BinaryComparisonOp {
public:
  LessOp(const OperatorIdentifier &_opid, const Op::Settings &settings);
  std::unique_ptr<Op> clone() const final;
  std::vector<std::unique_ptr<Op>> getGradOps() final;
};

} // namespace popart

#endif // POPART_WILLOW_INCLUDE_POPART_OP_LESS_HPP_
