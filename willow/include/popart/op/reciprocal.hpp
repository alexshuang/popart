// Copyright (c) 2018 Graphcore Ltd. All rights reserved.
#ifndef POPART_WILLOW_INCLUDE_POPART_OP_RECIPROCAL_HPP_
#define POPART_WILLOW_INCLUDE_POPART_OP_RECIPROCAL_HPP_

#include <memory>
#include <vector>
#include <popart/op.hpp>
#include <popart/op/elementwise.hpp>

namespace popart {
struct OperatorIdentifier;

class ReciprocalOp : public ElementWiseUnaryOp {
public:
  ReciprocalOp(const OperatorIdentifier &_opid, const Op::Settings &settings_);
  std::unique_ptr<Op> clone() const final;
  std::vector<std::unique_ptr<Op>> getGradOps() final;
};

class ReciprocalGradOp : public ElementWiseNonLinearUnaryGradOp {
public:
  ReciprocalGradOp(const ReciprocalOp &);
  std::unique_ptr<Op> clone() const final;
};

} // namespace popart

#endif // POPART_WILLOW_INCLUDE_POPART_OP_RECIPROCAL_HPP_
