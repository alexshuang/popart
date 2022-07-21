// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#define BOOST_TEST_MODULE sgd_mixed_mode_test_1_8

#include <array>
#include <boost/test/unit_test.hpp>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <string>

#include "get_results.hpp"
#include "popart/sgd.hpp"

BOOST_AUTO_TEST_CASE_TEMPLATE(SgdMixedModeTestCpp1_8,
                              TestConfig,
                              SGD1And2TestConfigs) {
  // as test case 2, but with gradient accumulation AND graph replication

  float learningRate = 1.0f / 4.0f;
  popart::SGD opt0(
      {
          {"defaultLearningRate", {learningRate, false}},
          {"defaultMomentum", {1.0f, false}},
          {"defaultVelocityScaling", {14.15f, false}},
          {"lossScaling", {0.15f, false}},
      },
      {},
      TestConfig::sgdAccMm);
  auto opt1  = opt0;
  auto opt2  = opt0;
  bool wAccl = true;

  for (auto wRepl : {true, false}) {
    auto results = getResults<float>(opt0, opt1, opt2, wAccl, wRepl);

    if (!acquisitionFailure(results)) {
      // int64_t correction = replicationFactor * accumulationFactor;
      int64_t correction =
          (wRepl ? replicationFactor : 1) * (wAccl ? accumulationFactor : 1);

      // including factor 2 for accumulation factor and 3 for replication factor
      auto absdiff0 =
          getAbsDiff(100 - correction * 6 * learningRate, std::get<0>(results));

      BOOST_CHECK(absdiff0 < 1e-9f);
      auto absdiff1 =
          getAbsDiff(200 - correction * 6 * learningRate, std::get<1>(results));
      BOOST_CHECK(absdiff1 < 1e-9f);
    }

    else {
      std::cout << "Failed to acquire device, test not run!";
    }
  }
}

BOOST_AUTO_TEST_CASE_TEMPLATE(SgdMixedModeTestCpp1_8_nesterov,
                              TestConfig,
                              SGD1And2TestConfigs) {
  // Test nesterov momentum

  // as test case 2, but with gradient accumulation AND graph replication

  float learningRate = 1.0f / 4.0f;
  popart::SGD opt0(
      {
          {"defaultLearningRate", {learningRate, false}},
          {"defaultMomentum", {1.0f, false}},
          {"defaultVelocityScaling", {14.15f, false}},
          {"lossScaling", {0.15f, false}},
          {"nesterov", {true, true}},
      },
      {},
      TestConfig::sgdAccMm);
  auto opt1  = opt0;
  auto opt2  = opt0;
  bool wAccl = true;

  for (auto wRepl : {true, false}) {
    auto results = getResults<float>(opt0, opt1, opt2, wAccl, wRepl);

    if (!acquisitionFailure(results)) {
      // int64_t correction = replicationFactor * accumulationFactor;
      int64_t correction =
          (wRepl ? replicationFactor : 1) * (wAccl ? accumulationFactor : 1);

      // including factor 2 for accumulation factor and 3 for replication factor
      auto absdiff0 =
          getAbsDiff(100 - correction * 9 * learningRate, std::get<0>(results));

      BOOST_CHECK(absdiff0 < 1e-9f);
      auto absdiff1 =
          getAbsDiff(200 - correction * 9 * learningRate, std::get<1>(results));
      BOOST_CHECK(absdiff1 < 1e-9f);
    }

    else {
      std::cout << "Failed to acquire device, test not run!";
    }
  }
}
