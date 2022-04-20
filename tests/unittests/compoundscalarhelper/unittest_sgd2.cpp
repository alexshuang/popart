// Copyright (c) 2021 Graphcore Ltd. All rights reserved.
#define BOOST_TEST_MODULE Test_SGD2_CompoundScalarHelpers
#include <boost/test/unit_test.hpp>
#include <string>
#include <utility>
#include <popart/compoundscalarhelper.hpp>
#include <popart/sessionoptions.hpp>
#include <popart/sgd.hpp>

#include "popart/clipnormsettings.hpp"
#include "popart/op.hpp"
#include "popart/optimizervalue.hpp"

using namespace popart;

namespace {
void validate(SGD sgd,
              float expected_lr,
              float expected_wd,
              float expected_dp,
              float expected_mm) {
  ScaledLearningRate2Helper lr;
  ScaledWeightDecay1Helper wd;
  DampeningScaleFactor2Helper dp;
  ScaledMomentum2Helper mm;

  BOOST_CHECK_EQUAL(lr.val("", sgd), expected_lr);
  BOOST_CHECK_EQUAL(wd.val("", sgd), expected_wd);
  BOOST_CHECK_EQUAL(dp.val("", sgd), expected_dp);
  BOOST_CHECK_EQUAL(mm.val("", sgd), expected_mm);
}
} // namespace

BOOST_AUTO_TEST_CASE(TestSGD2_Base) {
  SGD sgd{{
              {"defaultLearningRate", {0.1f, true}},
              {"defaultWeightDecay", {0.01f, true}},
              {"defaultDampening", {0.8f, true}},
              {"defaultMomentum", {0.9f, true}},
          },
          {},
          SGDAccumulatorAndMomentum::Separate};
  SessionOptions opts;
  sgd.setFactorsFromOptions(opts);
  validate(sgd, 0.1f, (1.0f - 0.8f) * 0.01f, 1.0f - 0.8f, 0.9f);
}

BOOST_AUTO_TEST_CASE(TestSGD2_lossScaling) {
  SGD sgd{{
              {"defaultLearningRate", {0.1f, true}},
              {"defaultWeightDecay", {0.01f, true}},
              {"defaultDampening", {0.8f, true}},
              {"defaultMomentum", {0.9f, true}},
              {"lossScaling", {4.0f, true}},
          },
          {},
          SGDAccumulatorAndMomentum::Separate};
  SessionOptions opts;
  sgd.setFactorsFromOptions(opts);
  validate(sgd, 0.1f, (1.0f - 0.8f) * 0.01f, (1.0f - 0.8f) / 4.0f, 0.9f);
}

BOOST_AUTO_TEST_CASE(TestSGD2_velocityScaling) {
  SGD sgd{{
              {"defaultLearningRate", {0.1f, true}},
              {"defaultWeightDecay", {0.01f, true}},
              {"defaultDampening", {0.8f, true}},
              {"defaultMomentum", {0.9f, true}},
              {"defaultVelocityScaling", {2.0f, true}},
          },
          {},
          SGDAccumulatorAndMomentum::Separate};
  SessionOptions opts;
  sgd.setFactorsFromOptions(opts);
  validate(sgd,
           0.1f / 2.0f,
           (1.0f - 0.8f) * 0.01f * 2.0f,
           (1.0f - 0.8f) * 2.0f,
           0.9f);
}

BOOST_AUTO_TEST_CASE(TestSGD2_ReplicaSum) {
  SGD sgd{{
              {"defaultLearningRate", {0.1f, true}},
              {"defaultWeightDecay", {0.01f, true}},
              {"defaultDampening", {0.8f, true}},
              {"defaultMomentum", {0.9f, true}},
          },
          {},
          SGDAccumulatorAndMomentum::Separate};
  SessionOptions opts;
  opts.enableReplicatedGraphs = true;
  opts.replicatedGraphCount   = 2;
  sgd.setFactorsFromOptions(opts);
  validate(sgd, 0.1f, (1.0f - 0.8f) * 0.01f, (1.0f - 0.8f), 0.9f);
}

BOOST_AUTO_TEST_CASE(TestSGD2_ReplicaMeanPost) {
  SGD sgd{{
              {"defaultLearningRate", {0.1f, true}},
              {"defaultWeightDecay", {0.01f, true}},
              {"defaultDampening", {0.8f, true}},
              {"defaultMomentum", {0.9f, true}},
          },
          {},
          SGDAccumulatorAndMomentum::Separate};
  SessionOptions opts;
  opts.enableReplicatedGraphs                  = true;
  opts.replicatedGraphCount                    = 2;
  opts.accumulationAndReplicationReductionType = ReductionType::Mean;
  opts.meanAccumulationAndReplicationReductionStrategy =
      MeanReductionStrategy::Post;
  sgd.setFactorsFromOptions(opts);
  validate(sgd, 0.1f, (1.0f - 0.8f) * 0.01f, (1.0f - 0.8f) / 2.0f, 0.9f);
}

BOOST_AUTO_TEST_CASE(TestSGD2_ReplicaMeanRunning) {
  SGD sgd{{
              {"defaultLearningRate", {0.1f, true}},
              {"defaultWeightDecay", {0.01f, true}},
              {"defaultDampening", {0.8f, true}},
              {"defaultMomentum", {0.9f, true}},
          },
          {},
          SGDAccumulatorAndMomentum::Separate};
  SessionOptions opts;
  opts.enableReplicatedGraphs                  = true;
  opts.replicatedGraphCount                    = 2;
  opts.accumulationAndReplicationReductionType = ReductionType::Mean;
  opts.meanAccumulationAndReplicationReductionStrategy =
      MeanReductionStrategy::Running;
  sgd.setFactorsFromOptions(opts);
  validate(sgd, 0.1f, (1.0f - 0.8f) * 0.01f, (1.0f - 0.8f), 0.9f);
}

BOOST_AUTO_TEST_CASE(TestSGD2_AccumSum) {
  SGD sgd{{
              {"defaultLearningRate", {0.1f, true}},
              {"defaultWeightDecay", {0.01f, true}},
              {"defaultDampening", {0.8f, true}},
              {"defaultMomentum", {0.9f, true}},
          },
          {},
          SGDAccumulatorAndMomentum::Separate};
  SessionOptions opts;
  opts.enableGradientAccumulation = true;
  opts.accumulationFactor         = 4;
  sgd.setFactorsFromOptions(opts);
  validate(sgd, 0.1f, (1.0f - 0.8f) * 0.01f, (1.0f - 0.8f), 0.9f);
}

BOOST_AUTO_TEST_CASE(TestSGD2_AccumMeanPost) {
  SGD sgd{{
              {"defaultLearningRate", {0.1f, true}},
              {"defaultWeightDecay", {0.01f, true}},
              {"defaultDampening", {0.8f, true}},
              {"defaultMomentum", {0.9f, true}},
          },
          {},
          SGDAccumulatorAndMomentum::Separate};
  SessionOptions opts;
  opts.enableGradientAccumulation              = true;
  opts.accumulationFactor                      = 4;
  opts.accumulationAndReplicationReductionType = ReductionType::Mean;
  opts.meanAccumulationAndReplicationReductionStrategy =
      MeanReductionStrategy::Post;
  sgd.setFactorsFromOptions(opts);
  validate(sgd, 0.1f, (1.0f - 0.8f) * 0.01f, (1.0f - 0.8f) / 4.0f, 0.9f);
}

BOOST_AUTO_TEST_CASE(TestSGD2_AccumMeanRunning) {
  SGD sgd{{
              {"defaultLearningRate", {0.1f, true}},
              {"defaultWeightDecay", {0.01f, true}},
              {"defaultDampening", {0.8f, true}},
              {"defaultMomentum", {0.9f, true}},
          },
          {},
          SGDAccumulatorAndMomentum::Separate};
  SessionOptions opts;
  opts.enableGradientAccumulation              = true;
  opts.accumulationFactor                      = 4;
  opts.accumulationAndReplicationReductionType = ReductionType::Mean;
  opts.meanAccumulationAndReplicationReductionStrategy =
      MeanReductionStrategy::Running;
  sgd.setFactorsFromOptions(opts);
  validate(sgd, 0.1f, (1.0f - 0.8f) * 0.01f, (1.0f - 0.8f), 0.9f);
}
