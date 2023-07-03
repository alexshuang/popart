// Copyright (c) 2023 Graphcore Ltd. All rights reserved.
#define BOOST_TEST_MODULE init_tensor_offset_map

#include <algorithm>
#include <any>
#include <boost/test/unit_test.hpp>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <poplar/Graph.hpp>
#include <poplar/Interval.hpp>

#include "popart/builder.gen.hpp"
#include "popart/dataflow.hpp"
#include "popart/debugcontext.hpp"
#include "popart/inputshapeinfo.hpp"
#include "popart/logging.hpp"
#include "popart/names.hpp"
#include "popart/patterns/patterns.hpp"
#include "popart/sessionoptions.hpp"
#include "popart/tensorinfo.hpp"
#include "popart/util.hpp"
#include "popart/voiddata.hpp"

// This trick is required to access the Devicex's poplar::Tensors.

#ifdef __clang__
#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif
#define protected public
#define private public

#include <testdevice.hpp>
#include <popart/builder.hpp>
#include <popart/error.hpp>
#include <popart/popx/devicex.hpp>
#include <popart/popx/irlowering.hpp>
#include <popart/session.hpp>
#include <popart/sgd.hpp>

#include "popart/popx/poptensors.hpp"

#undef private
#undef protected

BOOST_AUTO_TEST_CASE(InitTensorOffsetMap) {

  using namespace popart;

  auto builder = Builder::create();
  auto aiOnnx  = builder->aiOnnxOpset9();

  std::vector<int64_t> inputShape{4, 4};
  TensorInfo inputInfo("FLOAT", inputShape);

  // auto a = builder->addInputTensor(inputInfo, {TileSet:IO, ExchangeStrategy::OverlapInnerLoop});
  // auto b = builder->addInputTensor(inputInfo, {TileSet:IO, ExchangeStrategy::OverlapInnerLoop});
  // auto c = builder->addInputTensor(inputInfo, {TileSet:IO, ExchangeStrategy::OverlapInnerLoop});
  auto a = builder->addInputTensor(inputInfo);
  auto b = builder->addInputTensor(inputInfo);
  auto c = builder->addInputTensor(inputInfo);
  auto x = aiOnnx.add({a, b});
  x = aiOnnx.add({x, c});
  builder->addOutputTensor(x);

  auto proto    = builder->getModelProto();
  auto dataFlow = DataFlow(5);

  SessionOptions opts;
  opts.virtualGraphMode = VirtualGraphMode::Auto;
  opts.enableExplicitMainLoops = true;
  opts.useHostCopyOps = true;
  opts.numIOTiles = 32;
  opts.experimentalSettings.createHostTransferableTensorWithOffset = true;

  auto device = createTestDevice(TestDeviceType::IpuModel21, 1, 128);

  auto session = popart::InferenceSession::createFromOnnxModel(
      proto,
      dataFlow,
      device,
      InputShapeInfo(),
      opts,
      popart::Patterns(PatternsLevel::Default));

  session->prepareDevice();

  std::cout << "compile done" << std::endl;

  using Mapping = poplar::Graph::TileToTensorMapping;

  // Tensor shape: tile mapping
  std::map<std::vector<size_t>, std::vector<Mapping>> mappings;
  const auto &graph = session->getDevice().lowering().graph();
  for (auto id : session->getDevice().lowering().tensors().tensors_) {
    auto shape    = id.second->shape();
    const auto tm = graph.getTileMapping(*id.second);
    mappings[shape].push_back(tm);
    std::cout << tm << std::endl;
  }

  // for (const auto &shape_mappings : mappings) {

  //   auto maps = shape_mappings.second;
  //   for (auto m : maps) {
  //     if (m != maps[0]) {
  //       std::ostringstream oss;
  //       oss << "In this test, we expect all Tensors of the same shape to have "
  //           << "the same tile mappings, It is a test of the slice grad (pad) "
  //              "to "
  //           << "correctly locate and clone a corresponding forward Tensor. "
  //           << "If correctly cloned, the tile mapping should be identical. ";
  //       throw popart::error(oss.str());
  //     }
  //   }
  // }
}
