// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#include <algorithm>
#include <memory>
#include <string>
#include <tuple>
#include <vector>
#include <popart/op/sinh.hpp>
#include <popart/opmanager.hpp>

#include "popart/datatype.hpp"
#include "popart/op.hpp"
#include "popart/op/elementwise.hpp"
#include "popart/operatoridentifier.hpp"
#include "popart/operators.hpp"

namespace popart {

std::vector<std::tuple<OperatorIdentifier, float>>
SinhOp::inplacePriorityDefault() const {
  // see T6768: choosing default inplace priorities
  return {{Onnx::CustomOperators::SinhInplace, 10}};
}

std::unique_ptr<Op>
SinhOp::getInplaceVariant(const OperatorIdentifier &operator_id) const {
  if (operator_id == Onnx::CustomOperators::SinhInplace) {
    return std::make_unique<SinhInplaceOp>(*this);
  }
  // catch remaining cases and throw an error
  return Op::getInplaceVariant(operator_id);
}

SinhInplaceOp::SinhInplaceOp(const SinhOp &sinh_op)
    : ElementWiseInplaceUnaryOp(Onnx::CustomOperators::SinhInplace,
                                sinh_op.getSettings()) {}

SinhInplaceOp::SinhInplaceOp(const Op::Settings &settings)
    : ElementWiseInplaceUnaryOp(Onnx::CustomOperators::SinhInplace, settings) {}

std::unique_ptr<Op> SinhInplaceOp::clone() const {
  return std::make_unique<SinhInplaceOp>(*this);
}

std::unique_ptr<Op> SinhOp::clone() const {
  return std::make_unique<SinhOp>(*this);
}

SinhOp::SinhOp(const OperatorIdentifier &_opid, const Op::Settings &settings_)
    : ElementWiseUnaryOp(_opid, settings_) {}

std::vector<std::unique_ptr<Op>> SinhOp::getGradOps() {
  std::vector<std::unique_ptr<Op>> upops;
  upops.emplace_back(std::make_unique<SinhGradOp>(*this));
  return upops;
}

std::unique_ptr<Op> SinhGradOp::clone() const {
  return std::make_unique<SinhGradOp>(*this);
}

SinhGradOp::SinhGradOp(const SinhOp &fwdop)
    : ElementWiseNonLinearUnaryGradOp(Onnx::GradOperators::SinhGrad, fwdop) {}

namespace {

static OpDefinition::DataTypes T = {DataType::FLOAT16, DataType::FLOAT};

static OpDefinition sinhOpDef({OpDefinition::Inputs({{"input", T}}),
                               OpDefinition::Outputs({{"output", T}}),
                               OpDefinition::Attributes({})});
static OpCreator<SinhOp> sinhOpCreator(OpDefinitions({
    {Onnx::Operators::Sinh_9, sinhOpDef},
}));
} // namespace

} // namespace popart
