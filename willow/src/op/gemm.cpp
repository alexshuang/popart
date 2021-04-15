// Copyright (c) 2018 Graphcore Ltd. All rights reserved.
#include <algorithm>
#include <memory>
#include <popart/op/gemm.hpp>
#include <popart/opmanager.hpp>
#include <popart/opserialiser.hpp>
#include <popart/tensor.hpp>

namespace popart {

GemmOp::GemmOp(const OperatorIdentifier &_opid,
               float alpha_,
               float beta_,
               bool transA_,
               bool transB_,
               bool broadcast_,
               const Op::Settings &settings_)
    : Op(_opid, settings_), alpha(alpha_), beta(beta_), transA(transA_),
      transB(transB_), broadcast(broadcast_) {}

std::unique_ptr<Op> GemmOp::clone() const {
  return std::make_unique<GemmOp>(*this);
}

std::vector<std::unique_ptr<Op>> GemmOp::getGradOps() {
  throw error(
      "GemmOp should be removed by pattern 'GemmOp' before call to getGradOps");
}

void GemmOp::setup() {

  outInfo(getOutIndex()) = {inInfo(getAInIndex()).dataType(), getOutputShape()};
}

Shape GemmOp::getOutputShape() {
  auto a_shape = inInfo(getAInIndex()).shape();
  if (transA) {
    std::reverse(a_shape.begin(), a_shape.end());
  }

  auto b_shape = inInfo(getBInIndex()).shape();
  if (transB) {
    std::reverse(b_shape.begin(), b_shape.end());
  }

  return {a_shape[0], b_shape[1]};
}

float GemmOp::getAlpha() const { return alpha; }

float GemmOp::getBeta() const { return beta; }

bool GemmOp::getTransA() const { return transA; }
bool GemmOp::getTransB() const { return transB; }

void GemmOp::appendOutlineAttributes(OpSerialiserBase &os) const {
  Op::appendOutlineAttributes(os);

  os.appendAttribute("alpha", alpha);
  os.appendAttribute("beta", beta);
  os.appendAttribute("transA", transA);
  os.appendAttribute("transB", transB);

  if (opid.version == 6)
    os.appendAttribute("broadcast", broadcast);
}
namespace {

static OpDefinition::DataTypes T = {DataType::FLOAT16,
                                    DataType::FLOAT,
                                    DataType::UINT32,
                                    DataType::UINT64,
                                    DataType::INT32,
                                    DataType::INT64};

static OpDefinition gemmOpDef({OpDefinition::Inputs({
                                   {"A", T},
                                   {"B", T},
                                   {"C", T},
                               }),
                               OpDefinition::Outputs({{"Y", T}}),
                               OpDefinition::Attributes({{"alpha", {"*"}},
                                                         {"beta", {"*"}},
                                                         {"transA", {"*"}},
                                                         {"transB", {"*"}}})});

static OpCreator<GemmOp> gemmOpCreator(
    OpDefinitions({
        {Onnx::Operators::Gemm_6, gemmOpDef},
        {Onnx::Operators::Gemm_7, gemmOpDef},
        {Onnx::Operators::Gemm_9, gemmOpDef},
        {Onnx::Operators::Gemm_11, gemmOpDef},
    }),
    [](const OpCreatorInfo &info) {
      float alpha =
          info.attributes.getAttribute<Attributes::Float>("alpha", 1.0);
      float beta = info.attributes.getAttribute<Attributes::Float>("beta", 1.0);
      bool transA =
          info.attributes.getAttribute<Attributes::Int>("transA", false);
      bool transB =
          info.attributes.getAttribute<Attributes::Int>("transB", false);

      // broadcast is only valid for version 6
      bool broadcast =
          info.attributes.getAttribute<Attributes::Int>("broadcast", false);

      return std::unique_ptr<Op>(new GemmOp(
          info.opid, alpha, beta, transA, transB, broadcast, info.settings));
    },
    true);

} // namespace

} // namespace popart
