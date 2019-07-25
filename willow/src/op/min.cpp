#include <memory>
#include <popart/op/min.hpp>
#include <popart/opmanager.hpp>
#include <popart/tensorindex.hpp>

namespace popart {

MinOp::MinOp(const OperatorIdentifier &_opid, const Op::Settings &settings_)
    : VariadicOp(_opid, settings_) {}

std::unique_ptr<Op> MinOp::clone() const {
  return std::make_unique<MinOp>(*this);
}

std::unique_ptr<Op> MinOp::getIthGrad(int i) const {
  return std::make_unique<MinArgGradOp>(*this, i);
}

MinArgGradOp::MinArgGradOp(const MinOp &op_, InIndex inputIndex)
    : NonLinearVariadicGradOp(Onnx::GradOperators::MinArgGrad,
                              op_,
                              inputIndex) {}

std::unique_ptr<Op> MinArgGradOp::clone() const {
  return std::make_unique<MinArgGradOp>(*this);
}

namespace {
static OpCreator<MinOp> minOpCreator({Onnx::Operators::Min_6,
                                      Onnx::Operators::Min_8});
} // namespace

} // namespace popart
