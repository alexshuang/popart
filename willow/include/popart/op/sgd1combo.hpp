// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#ifndef GUARD_NEURALNET_SGD1VARUPDATECOMBOOP_HPP
#define GUARD_NEURALNET_SGD1VARUPDATECOMBOOP_HPP

#include <popart/op/varupdate.hpp>
#include <popart/optimizer.hpp>
#include <popart/optimizervalue.hpp>

namespace popart {

// The "1" in the name signifies that there is 1 persistant Tensor required and
// assocatiated to the Variable Tensor being updated. This is the Op generated
// if gradient accumulation is used, or if there is non-zero momentum term for
// the Variable Tensor being updated.

// The "Combo" in the name signfies that this Op will be decomposed into 3
// smaller Ops : (1) SGD1AccumlateOp (2) SGD1VarUpdateOp (3) SGD1AcclUpdateOp

class SGD1ComboOp : public VarUpdateWithUpdaterOp {
public:
  SGD1ComboOp(OptimizerValue initialSmm1,
              OptimizerValue initialDpsf1,
              OptimizerValue initialSwd1,
              OptimizerValue initialSlr1,
              OptimizerReductionType reductionType_,
              const Op::Settings &);

  std::unique_ptr<Op> clone() const final;

  // map of size 0/1/2, containing all non-const optimizer Tensors for this Op
  std::map<InIndex, TensorId> optimizerInputs() const final;

  void appendOutlineAttributes(OpSerialiserBase &) const final;

  // momentum
  const OptimizerValue initSmm1;

  // dampening scale factor
  const OptimizerValue initDpsf1;

  // weight decay scale factor
  const OptimizerValue initSwd1;

  // scaled learning rate
  const OptimizerValue initSlr1;

  const OptimizerReductionType reductionType;

  static InIndex getSmm1InIndex() { return 2; }
  static InIndex getDpsf1InIndex() { return 3; }
  static InIndex getSwd1InIndex() { return 4; }
  static InIndex getSlr1InIndex() { return 5; }

  std::set<InIndex> optionalInputs() const final;

  // this Op should not be present when outlining is performed
  float getSubgraphValue() const final { return -1.0f; }
};

} // namespace popart

#endif
