#define BOOST_TEST_MODULE OverlapComputeExchangeTest

#include <../random_util.hpp>
#include <boost/test/unit_test.hpp>
#include <filereader.hpp>
#include <popart/builder.hpp>
#include <popart/dataflow.hpp>
#include <popart/devicemanager.hpp>
#include <popart/inputshapeinfo.hpp>
#include <popart/ndarraywrapper.hpp>
#include <popart/op/init.hpp>
#include <popart/op/l1.hpp>
#include <popart/op/remote.hpp>
#include <popart/opmanager.hpp>
#include <popart/optimizer.hpp>
#include <popart/popx/devicex.hpp>
#include <popart/session.hpp>
#include <popart/tensor.hpp>
#include <popart/tensordata.hpp>
#include <popart/tensorinfo.hpp>
#include <popart/tensornames.hpp>
#include <popart/tensors.hpp>
#include <popart/testdevice.hpp>

#include <algorithm>
#include <chrono>
#include <map>
#include <random>
#include <tuple>
#include <vector>

using namespace popart;

// Run a PopART graph that allows overlapping compute/exchange with 2 IPUs.
// Run Full and ReplicaAndLadder sync configurations and check that the
// IPU cycles on the parallel (ReplicaAndLadder) execution is at least
// 20% lower than the serial (Full) execution mode.
BOOST_AUTO_TEST_CASE(OverlapComputeExchangeTest_0) {

  int64_t N = 128;
  int64_t K = 8;

  // we will generate random initializations
  int seed = 1337;
  DefaultRandomEngine eng(seed);
  UniformRealDistribution<float> fdis(-4.f, 4.f);

  auto bder        = Builder::create();
  auto aiOnnx      = bder->aiOnnxOpset9();
  auto aiGraphcore = bder->aiGraphcoreOpset1();

  // Tensor A of shape K x N x N
  TensorInfo A_info{"FLOAT", std::vector<int64_t>{K, N, N}};
  std::vector<float> v_A_init(A_info.nelms());
  for (auto &val : v_A_init) {
    val = fdis(eng);
  }
  TensorId A_id =
      bder->addInitializedInputTensor({v_A_init.data(), A_info}, "A");

  // Tensor B of shape 1 x N x N
  TensorInfo B_info{"FLOAT", std::vector<int64_t>{1, N, N}};
  std::vector<float> v_B_init(B_info.nelms());
  for (auto &val : v_B_init) {
    val = fdis(eng);
  }
  TensorId B_id =
      bder->addInitializedInputTensor({v_B_init.data(), B_info}, "B");

  // Tensor C of shape 1 x N x N
  TensorInfo C_info{"FLOAT", std::vector<int64_t>{1, N, N}};
  std::vector<float> v_C_init(C_info.nelms());
  for (auto &val : v_C_init) {
    val = fdis(eng);
  }
  TensorId C_id =
      bder->addInitializedInputTensor({v_C_init.data(), C_info}, "C");

  // Ensure the order of operations and mode of overlap is:
  // Prio : Task                              IPU
  //  1.0 : MatMul                            IPU 0
  //  0.0 : IpuCopy (autogenerated)           IPU 0 -> IPU 1
  // -1.0 : RemoteStore   <-- should overlap   IPU 0
  // -2.0 : MatMul       <-- should overlap   IPU 1
  // -3.0 : RemoteStore   <-- can overlap      IPU 1
  // to ensure overlapping compute and exchange can be demonstrated

  TensorId D_id =
      bder->customOp(Onnx::AiOnnx::OpSet9::MatMul,
                     9,
                     {A_id, B_id},
                     1,
                     {{"__ipu_number", 0}, {"__schedule_priority", 1.f}},
                     "MatMul")[0];

  TensorId E_id =
      bder->customOp(Onnx::AiOnnx::OpSet9::MatMul,
                     9,
                     {C_id, D_id},
                     1,
                     {{"__ipu_number", 1}, {"__schedule_priority", -2.f}},
                     "MatMul")[0];

  int index             = 0;
  ConstVoidData idxData = {&index, {"INT32", Shape({})}};

  TensorId D_idx = aiOnnx.constant(idxData, std::string("D_idx"));
  TensorId E_idx = aiOnnx.constant(idxData, std::string("E_idx"));

  bder->customOp(
      Onnx::CustomOperators::RemoteStore,
      1,
      {D_id, D_idx},
      0,
      {{"bufferid", 0}, {"__ipu_number", 0}, {"__schedule_priority", -1.f}},
      "store D");

  bder->customOp(
      Onnx::CustomOperators::RemoteStore,
      1,
      {E_id, E_idx},
      0,
      {{"bufferid", 1}, {"__ipu_number", 1}, {"__schedule_priority", -3.f}},
      "store E");

  auto proto         = bder->getModelProto();
  auto modelProto    = io::getModelFromString(proto);
  auto art           = AnchorReturnType("All");
  int batchesPerStep = 1;
  auto dataFlow      = DataFlow(batchesPerStep, {{B_id, art}});

  std::map<popart::TensorId, popart::IArray &> inputs = {};

  std::vector<float> raw_B_out(B_info.nelms());
  popart::NDArrayWrapper<float> B_wrapper(raw_B_out.data(), B_info.shape());
  std::map<popart::TensorId, popart::IArray &> anchors = {
      {B_id, B_wrapper},
  };

  double ipu_0_serial_cycles;
  double ipu_1_serial_cycles;
  double ipu_0_parallel_cycles;
  double ipu_1_parallel_cycles;

  for (auto syncPattern : {SyncPattern::Full, SyncPattern::ReplicaAndLadder}) {
    auto device = createTestDevice(TestDeviceType::Hw, 2, 0, syncPattern);
    if (device != nullptr) {
      auto opts                               = SessionOptions();
      opts.virtualGraphMode                   = VirtualGraphMode::Manual;
      opts.instrumentWithHardwareCycleCounter = true;
      opts.hardwareInstrumentations           = {Instrumentation::Inner};

      auto session = popart::InferenceSession::createFromOnnxModel(
          proto,
          dataFlow,
          device,
          popart::InputShapeInfo(),
          opts,
          popart::Patterns(PatternsLevel::Default));

      for (Op *op : session->getIr().getAllOps()) {
        if (op->isIpuCopyOp()) {
          op->settings.schedulePriority = 0.0;
        }
      }

      session->prepareDevice();
      popart::StepIO stepio(inputs, anchors);

      session->run(stepio);

      logging::debug("Cycles: {} {}",
                     session->getCycleCount("inner_ipu_0"),
                     session->getCycleCount("inner_ipu_1"));

      if (syncPattern == SyncPattern::Full) {
        ipu_0_serial_cycles = session->getCycleCount("inner_ipu_0");
        ipu_1_serial_cycles = session->getCycleCount("inner_ipu_1");
      } else {
        ipu_0_parallel_cycles = session->getCycleCount("inner_ipu_0");
        ipu_1_parallel_cycles = session->getCycleCount("inner_ipu_1");
      }
    }
  }

  // Expected > 20% savings on cycle count on IPU1
  BOOST_CHECK_LT(ipu_1_parallel_cycles, 0.8 * ipu_1_serial_cycles);
}
