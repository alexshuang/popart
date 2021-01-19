// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#include <memory>
#include <popart/ir.hpp>
#include <popart/op/sgd1combo.hpp>
#include <popart/opserialiser.hpp>

namespace popart {

SGD1ComboOp::SGD1ComboOp(OptimizerValue initialSmm1,
                         OptimizerValue initialDpsf1,
                         OptimizerValue initialSwd1,
                         OptimizerValue initialSlr1,
                         OptimizerReductionType reductionType_,
                         const Op::Settings &settings_)
    : VarUpdateWithUpdaterOp(Onnx::CustomOperators::SGD1Combo, settings_),
      initSmm1(initialSmm1), initDpsf1(initialDpsf1), initSwd1(initialSwd1),
      initSlr1(initialSlr1), reductionType(reductionType_) {}

void SGD1ComboOp::appendOutlineAttributes(OpSerialiserBase &os) const {
  if (initSmm1.isConst()) {
    os.appendAttribute("const scaled momentum", initSmm1.val());
  }

  if (initDpsf1.isConst()) {
    os.appendAttribute("const dampening scale factor", initDpsf1.val());
  }

  if (initSwd1.isConst()) {
    os.appendAttribute("const weight decay scale factor", initSwd1.val());
  }

  if (initSlr1.isConst()) {
    os.appendAttribute("const scaled learning rate", initSlr1.val());
  }

  os.appendAttribute("reduction type", static_cast<int>(reductionType));
}

std::unique_ptr<Op> SGD1ComboOp::clone() const {
  return std::make_unique<SGD1ComboOp>(*this);
}

std::map<InIndex, TensorId> SGD1ComboOp::optimizerInputs() const {

  std::map<InIndex, TensorId> m;

  if (!initSlr1.isConst()) {
    auto index = getSlr1InIndex();
    m.insert({index, inId(index)});
  }

  if (!initSwd1.isConst()) {
    auto index = getSwd1InIndex();
    m.insert({index, inId(index)});
  }

  if (!initSmm1.isConst()) {
    auto index = getSmm1InIndex();
    m.insert({index, inId(index)});
  }

  if (!initDpsf1.isConst()) {
    auto index = getDpsf1InIndex();
    m.insert({index, inId(index)});
  }

  return m;
}

std::set<InIndex> SGD1ComboOp::optionalInputs() const {
  return {
      getSmm1InIndex(), getDpsf1InIndex(), getSwd1InIndex(), getSlr1InIndex()};
}

} // namespace popart
