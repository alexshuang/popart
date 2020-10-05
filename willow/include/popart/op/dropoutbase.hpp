// Copyright (c) 2020 Graphcore Ltd. All rights reserved.
#ifndef GUARD_NEURALNET_DROPOUTBASE_HPP
#define GUARD_NEURALNET_DROPOUTBASE_HPP

#include <popart/op.hpp>

namespace popart {

// Forward declare for use in static function signature
class OpCreatorInfo;

// Base class for dropout ops
class DropoutBaseOp : public Op {
public:
  DropoutBaseOp(const OperatorIdentifier &opid_,
                float ratio_,
                uint32_t seedModifier_,
                const Op::Settings &settings_);

  DropoutBaseOp(const OperatorIdentifier &_opid,
                float ratio_,
                const Op::Settings &settings_);

  // Inputs
  static InIndex getInIndex() { return 0; }

  // Outputs
  static OutIndex getOutIndex() { return 0; }

  bool canBeReplacedByIdentity() final;

  uint32_t getSeedModifier() const { return seedModifier; }

  float getRatio() const { return ratio; }
  void setRatio(float r) { ratio = r; }

  bool requiresRandomSeed() const final { return true; }
  InIndex getSeedInIndex() const final { return 1; }

  float getSubgraphValue() const final { return getLowSubgraphValue(); }

  bool canShard() const final { return true; }

  std::map<TensorId, std::vector<TensorId>>
  shard(const std::map<TensorId, std::vector<TensorId>> &inputs) final;

  static float validateRatioAttribute(const OpCreatorInfo &info);

private:
  // Update the seed modifier with a unique value as determined by the IR
  void updateSeedModifier();

  float ratio;
  uint32_t seedModifier;
};

} // namespace popart

#endif
