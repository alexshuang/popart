#include <memory>
#include <vector>
#include <popart/op/not.hpp>
#include <popart/opmanager.hpp>
#include <popart/tensor.hpp>

namespace popart {

NotOp::NotOp(const OperatorIdentifier &_opid, const Op::Settings &settings_)
    : ElementWiseUnaryOp(_opid, settings_) {}

std::unique_ptr<Op> NotOp::clone() const {
  return std::make_unique<NotOp>(*this);
}

std::vector<std::unique_ptr<Op>> NotOp::getGradOps() {
  throw error("PopART does not have a valid grad op corresponding to NotOp");
}

namespace {
static OpCreator<NotOp> NotOpCreator({Onnx::Operators::Not_1});
} // namespace

} // namespace popart
