// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#define BOOST_TEST_MODULE VertexVgidTest

#include <algorithm>
#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <filereader.hpp>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <testdevice.hpp>
#include <utility>
#include <vector>
#include <popart/builder.hpp>
#include <popart/dataflow.hpp>
#include <popart/ir.hpp>
#include <popart/op/ipucopy.hpp>
#include <popart/sgd.hpp>
#include <popart/tensor.hpp>

#include "popart/builder.gen.hpp"
#include "popart/inputshapeinfo.hpp"
#include "popart/names.hpp"
#include "popart/op.hpp"
#include "popart/patterns/patterns.hpp"
#include "popart/scheduler_requireoptimal.hpp"
#include "popart/sessionoptions.hpp"
#include "popart/tensorindex.hpp"
#include "popart/tensorinfo.hpp"
#include "popart/voiddata.hpp"

BOOST_AUTO_TEST_CASE(VertexVgidTest0) {

  // withSharding : Ops get virtual graph ids in {0,1,2}
  // else they have no virtual graph ids.
  //
  auto test = [](bool withSharding) {
    //
    // build an ~20 layer model
    //
    using namespace popart;
    auto builder     = Builder::create();
    auto aiOnnx      = builder->aiOnnxOpset9();
    auto aiGraphcore = builder->aiGraphcoreOpset1();
    int64_t nelms    = 16;
    TensorInfo info{"FLOAT", std::vector<int64_t>{nelms}};
    auto input1 = builder->addInputTensor(info);
    auto act    = aiOnnx.relu({input1});
    for (int i = 0; i < 6; ++i) {
      act = aiGraphcore.scale({act}, 0.5);
      act = aiOnnx.sigmoid({act});
      act = aiOnnx.relu({act});
    }
    std::vector<float> w0Vals(nelms, 1.0f);
    ConstVoidData w0Data = {w0Vals.data(), info};
    auto w0              = builder->addInitializedInputTensor(w0Data);
    act                  = aiOnnx.add({w0, act}, "act0");
    act                  = aiOnnx.relu({act});
    std::vector<float> w1Vals(nelms, 1.0f);
    ConstVoidData w1Data = {w1Vals.data(), info};
    auto w1              = builder->addInitializedInputTensor(w1Data);
    act                  = aiOnnx.add({w1, act}, "act");
    act                  = aiOnnx.relu({act});
    act                  = builder->aiGraphcoreOpset1().l1loss({act}, 0.1);
    builder->addOutputTensor(act);
    auto proto      = builder->getModelProto();
    auto modelProto = io::getModelFromString(proto);
    //
    // model building complete
    //

    SessionOptions userOptions;
    if (withSharding) {
      userOptions.virtualGraphMode = VirtualGraphMode::Auto;
    }
    std::map<std::string, std::string> deviceOpts{{"numIPUs", "3"}};

    //
    // prepare the training graph
    //
    auto dataFlow  = DataFlow(1, {{act, AnchorReturnType("All")}});
    auto optimizer = ConstSGD(0.01);
    auto device    = createTestDevice(TEST_TARGET, 3);

    Ir ir;
    ir.prepare({modelProto,
                InputShapeInfo(),
                dataFlow,
                act,
                &optimizer,
                *device,
                userOptions,
                Patterns(PatternsLevel::Default)});
    //
    // training graph prepared
    //

    //
    // confirm that sharded into 3 if withSharding (as numIPUs set to 3)
    //
    std::set<int> vGraphs;
    for (auto &id_op : ir.getMainGraphOps()) {
      if (id_op.second->hasVirtualGraphId()) {
        vGraphs.emplace(id_op.second->getVirtualGraphId());
      }
    }
    if (withSharding) {
      std::set<int> expected;
      expected.emplace(0);
      expected.emplace(1);
      expected.emplace(2);
      BOOST_CHECK(vGraphs == expected);
    }

    //  --tests--
    // if withSharding:
    //  check that tensor virtual graph ids agree
    //  with their consumers and producers
    // else:
    //  check that no tensors have virtual graph ids set
    //  ----------
    //
    auto opSchedule = ir.getOpSchedule({}, RequireOptimalSchedule::No);
    for (auto op : opSchedule) {

      if (withSharding) {
        auto ipucopy = dynamic_cast<IpuCopyOp *>(op);
        if (ipucopy) {
          for (auto inTensor : op->input->tensors()) {
            BOOST_CHECK(inTensor->getVirtualGraphId() ==
                        ipucopy->getSourceIpus().at(inTensor->id));
          }

          for (auto outTensor : op->output->tensors()) {
            BOOST_CHECK(outTensor->getVirtualGraphId() ==
                        ipucopy->getDestIpu());
          }
        }

        // not an IPUCopyOp
        else {
          for (auto inTensor : op->input->tensors()) {
            BOOST_CHECK(inTensor->getVirtualGraphId() ==
                        op->getVirtualGraphId());
          }

          for (auto outTensor : op->output->tensors()) {
            BOOST_CHECK(outTensor->getVirtualGraphId() ==
                        op->getVirtualGraphId());
          }
        }
      }

      else {
        for (auto inTensor : op->input->tensors()) {
          BOOST_CHECK(inTensor->hasVirtualGraphId() == false);
        }

        for (auto outTensor : op->output->tensors()) {
          BOOST_CHECK(outTensor->hasVirtualGraphId() == false);
        }
      }
    }
  };

  // test with sharding
  test(true);

  // test without sharding
  test(false);
}
