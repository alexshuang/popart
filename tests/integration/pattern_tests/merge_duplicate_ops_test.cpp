// Copyright (c) 2020 Graphcore Ltd. All rights reserved.
#define BOOST_TEST_MODULE MergeDuplicatesTest

#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <filereader.hpp>
#include <memory>
#include <string>
#include <vector>
#include <popart/builder.hpp>
#include <popart/dataflow.hpp>
#include <popart/inputshapeinfo.hpp>
#include <popart/ir.hpp>
#include <popart/sgd.hpp>
#include <popart/testdevice.hpp>

#include "popart/builder.gen.hpp"
#include "popart/names.hpp"
#include "popart/operators.hpp"
#include "popart/patterns/patterns.hpp"
#include "popart/tensorinfo.hpp"

using namespace popart;

// where we test that with 3 relu ops consuming the input, all three of them are
// merged.
BOOST_AUTO_TEST_CASE(MergeDuplicates0) {

  // clang-format off
  //
  // Consider the graph
  //
  //           | -- [Relu] -- (h0) -- |
  //           |                      | --- [Add] -- (h3) -|
  // (in0) >---| -- [Relu] -- (h1) -- |                    |
  //           |                                           | -> [Add] -- (out)
  //           | -- [Relu] -- (h2) ----------------------- |
  //
  // We expect the MergeDuplicateOps transform to merge the 3 relu ops:
  //
  //                             | -- |
  //                             |    | -- [Add] -- (h3) -|
  // (in0) > -- [Relu] -- (h0) - | -- |                   |
  //                             |                        | -> [Add] -- (out)
  //                             | -- [Cos] -- (h2) ----- |
  //
  // clang-format on

  // Build an onnx model (for training)
  auto builder = Builder::create();
  auto aiOnnx  = builder->aiOnnxOpset9();

  TensorInfo shape{"FLOAT", std::vector<int64_t>{1}};
  auto in0 = builder->addInputTensor(shape);
  auto h0  = aiOnnx.relu({in0});
  auto h1  = aiOnnx.relu({in0});
  auto h2  = aiOnnx.relu({in0});
  auto h3  = aiOnnx.add({h0, h1});
  auto out = aiOnnx.add({h2, h3});
  auto l1  = builder->aiGraphcoreOpset1().l1loss({out}, 0.1);

  auto proto      = builder->getModelProto();
  auto modelProto = io::getModelFromString(proto);

  // Create the IR
  auto dataFlow  = DataFlow(1, {{out, AnchorReturnType("All")}});
  auto optimizer = ConstSGD(0.01);
  auto device    = createTestDevice(TEST_TARGET);

  Ir ir;
  ir.prepare({modelProto,
              InputShapeInfo(),
              dataFlow,
              l1,
              &optimizer,
              *device,
              {},
              Patterns(PatternsLevel::NoPatterns)
                  .enableRuntimeAsserts(false)
                  .enableInPlace(true)});

  // Check the ir
  // All 3 relus have been merged and the remaining one will have been inplaced.
  auto opsOfTypeRelu        = ir.opsOfType(Onnx::AiOnnx::OpSet9::Relu);
  auto opsOfTypeReluInplace = ir.opsOfType(Onnx::CustomOperators::ReluInplace);
  BOOST_CHECK(opsOfTypeRelu.size() == 0);
  BOOST_CHECK(opsOfTypeReluInplace.size() == 1);
}
