// Copyright (c) 2021 Graphcore Ltd. All rights reserved.
#define BOOST_TEST_MODULE Test_Op_Dropout
#include <boost/test/unit_test.hpp>

#include <popart/ir.hpp>
#include <popart/op/dropout.hpp>

using namespace popart;

#define protected public

/**
 * Check that canBeReplacedByIdentity() returns true iff rate is 0 and mask
 * output is not used.
 **/
BOOST_AUTO_TEST_CASE(TestDropoutCanBeReplacedByIdentity) {
  Ir ir;
  Graph &g = ir.getMainGraph();

  TensorId fwdIn            = "fwdIn";
  DataType originalDataType = DataType::FLOAT;
  const TensorInfo fwdInInfo{originalDataType, Shape{100}};
  g.getTensors().addStream(fwdIn, fwdInInfo);

  auto test =
      [&](float rate, bool outputMask, bool expCanBeReplacedByIdentity) {
        static size_t index = 0;
        index++;

        std::cout << "Testing with rate=" << rate
                  << ", outputMask=" << outputMask << std::endl;

        std::map<OutIndex, TensorId> outputsWithoutMask = {
            {DropoutOp::getOutIndex(), "fwdOut" + std::to_string(index)}};
        std::map<OutIndex, TensorId> outputsWithMask = {
            {DropoutOp::getOutIndex(), "fwdOut" + std::to_string(index)},
            {DropoutOp::getMaskOutIndex(), "fwdMask" + std::to_string(index)}};

        auto dropout = g.createConnectedOp<DropoutOp>(
            {{DropoutOp::getInIndex(), fwdIn}},
            outputMask ? outputsWithMask : outputsWithoutMask,
            Onnx::Operators::Dropout_10,
            rate,
            Op::Settings(g, "Dropout" + std::to_string(index)));

        if (expCanBeReplacedByIdentity) {
          BOOST_REQUIRE_MESSAGE(
              dropout->canBeReplacedByIdentity(),
              logging::format(
                  "Expected DropoutOp with rate={}, outputMask={} to be "
                  "replacable by identity",
                  rate,
                  outputMask));
        } else {
          BOOST_REQUIRE_MESSAGE(
              !dropout->canBeReplacedByIdentity(),
              logging::format(
                  "Expected DropoutOp with rate={}, outputMask={} to NOT "
                  "be replacable by identity",
                  rate,
                  outputMask));
        }
      };

  // Without output mask and with rate 0 we can replace with identity.
  test(0.0f, false, true);
  // With output mask we can't replace with identity (rate 0.0f).
  test(0.0f, true, false);
  // With rate 0.4 we can't replace with identity.
  test(0.4f, false, false);
  // With output mask we can't replace with identity (rate 0.4f).
  test(0.4f, true, false);
};