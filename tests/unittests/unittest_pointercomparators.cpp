// Copyright (c) 2021 Graphcore Ltd. All rights reserved.
#define BOOST_TEST_MODULE PointerComparators

#include <boost/test/data/monomorphic.hpp>
#include <boost/test/data/test_case.hpp>
#include <boost/test/unit_test.hpp>
#include <utility>
#include <popart/ir.hpp>
#include <popart/op.hpp>
#include <popart/op/init.hpp>
#include <popart/pointercomparators.hpp>
#include <popart/popx/creatorx.hpp>
#include <popart/tensor.hpp>
#include <popart/tensorinfo.hpp>

#include "popart/graph.hpp"
#include "popart/graphcoreoperators.hpp"
#include "popart/names.hpp"
#include "popart/popx/viewchangers.hpp"
#include "popart/tensordebuginfo.hpp"

using namespace popart;

/* NOTE:
 * Invalid pointers are undefined behaviour, thus no tests are made for these,
 * even when POPART_STRICT_COMPARATOR_CHECKS is set does not guarantee to throw
 * on invalid pointers
 */

BOOST_AUTO_TEST_CASE(testPOpCmpPositive) {
  // Test that the Op* comparator is working
  Ir ir;
  auto &graph = ir.getMainGraph();

  // NOTE:
  // 1. All the op constructors explicitly set the id by calling
  //    Ir::getAndIncrOpsCounter()
  //    This means that we can be sure that the id of the ops are
  //    monotonically increasing
  // 2. We use InitOp as Op is a virtual class
  // 3. As the op needs to be in the Ir (at least when
  // POPART_STRICT_COMPARATOR_CHECKS is
  //    defined), we use the createOp function
  TensorInfo tInfo{};
  auto op1Ptr = graph.createOp<InitOp>(Onnx::CustomOperators::Init_1,
                                       tInfo,
                                       TensorType::ActGrad,
                                       InitType::Zero,
                                       Op::Settings{graph, ""});
  auto op2Ptr = graph.createOp<InitOp>(Onnx::CustomOperators::Init_1,
                                       tInfo,
                                       TensorType::ActGrad,
                                       InitType::Zero,
                                       Op::Settings{graph, ""});

  POpCmp cmp;
  BOOST_CHECK(cmp(op1Ptr, op2Ptr));  // Less than
  BOOST_CHECK(!cmp(op1Ptr, op1Ptr)); // Equal
  BOOST_CHECK(!cmp(op2Ptr, op1Ptr)); // Greater
}

#ifdef POPART_STRICT_COMPARATOR_CHECKS
BOOST_AUTO_TEST_CASE(testPOpCmpNegative) {
  // Test that the Op* comparator throws error on nullptr

  // We use InitOp as Op is a virtual class
  InitOp *op1Ptr = nullptr;
  InitOp *op2Ptr = nullptr;

  POpCmp cmp;

  auto checkError = [](const internal_error &error) {
    return std::string(error.what()) == "[POpCmp] Invalid pointer.";
  };
  BOOST_CHECK_EXCEPTION(cmp(op1Ptr, op2Ptr), internal_error, checkError);
  BOOST_CHECK_EXCEPTION(cmp(op1Ptr, op1Ptr), internal_error, checkError);
  BOOST_CHECK_EXCEPTION(cmp(op2Ptr, op1Ptr), internal_error, checkError);
}
#endif

BOOST_AUTO_TEST_CASE(testPTensorCmpPositive) {
  // Test that the Tensor* comparator is working
  Ir ir;
  auto &graph = ir.getMainGraph();

  // NOTE: The tensor needs to be in the Ir (at least when
  // POPART_STRICT_COMPARATOR_CHECKS
  //       is defined), we use the createOp function
  TensorInfo tInfo{};
  TensorId tId1 = "tId1";
  TensorId tId2 = "tId2";
  graph.addInput(tId1, tInfo);
  graph.addInput(tId2, tInfo);

  auto t1Ptr = graph.getTensor(tId1);
  auto t2Ptr = graph.getTensor(tId2);

  PTensorCmp cmp;
  BOOST_CHECK(cmp(t1Ptr, t2Ptr));  // Less than
  BOOST_CHECK(!cmp(t1Ptr, t1Ptr)); // Equal
  BOOST_CHECK(!cmp(t2Ptr, t1Ptr)); // Greater
}

#ifdef POPART_STRICT_COMPARATOR_CHECKS
BOOST_AUTO_TEST_CASE(testPTensorCmpNegative) {
  // Test that the Tensor* comparator throws error on nullptr

  Tensor *t1Ptr = nullptr;
  Tensor *t2Ptr = nullptr;

  PTensorCmp cmp;

  auto checkError = [](const internal_error &error) {
    return std::string(error.what()) == "[PTensorCmp] Invalid pointer.";
  };
  BOOST_CHECK_EXCEPTION(cmp(t1Ptr, t2Ptr), internal_error, checkError);
  BOOST_CHECK_EXCEPTION(cmp(t2Ptr, t1Ptr), internal_error, checkError);
  BOOST_CHECK_EXCEPTION(cmp(t1Ptr, t1Ptr), internal_error, checkError);
}
#endif

BOOST_AUTO_TEST_CASE(testVectorPTensorCmpPositive) {
  // Test that the std::vector<Tensor *> comparator is working
  Ir ir;
  auto &graph = ir.getMainGraph();

  // NOTE: The tensor needs to be in the Ir (at least when
  // POPART_STRICT_COMPARATOR_CHECKS
  //       is defined), we use the createOp function
  TensorInfo tInfo{};
  TensorId tId1 = "tId1";
  TensorId tId2 = "tId2";
  graph.addInput(tId1, tInfo);
  graph.addInput(tId2, tInfo);

  std::vector<Tensor *> v1 = {graph.getTensor(tId1)};
  std::vector<Tensor *> v2 = {graph.getTensor(tId2)};

  VectorPTensorCmp cmp;
  BOOST_CHECK(cmp(v1, v2));  // Less than
  BOOST_CHECK(!cmp(v1, v1)); // Equal
  BOOST_CHECK(!cmp(v2, v1)); // Greater
}

BOOST_AUTO_TEST_CASE(testPOpBoolCmpPositive) {
  // Test that the pair<Op *, bool> comparator is working
  Ir ir;
  auto &graph = ir.getMainGraph();

  // NOTE:
  // 1. All the op constructors explicitly set the id by calling
  //    Ir::getAndIncrOpsCounter()
  //    This means that we can be sure that the id of the ops are
  //    monotonically increasing
  // 2. We use InitOp as Op is a virtual class
  // 3. As the op needs to be in the Ir (at least when
  // POPART_STRICT_COMPARATOR_CHECKS is
  //    defined), we use the createOp function
  TensorInfo tInfo{};
  auto op1Ptr = graph.createOp<InitOp>(Onnx::CustomOperators::Init_1,
                                       tInfo,
                                       TensorType::ActGrad,
                                       InitType::Zero,
                                       Op::Settings{graph, ""});
  auto op2Ptr = graph.createOp<InitOp>(Onnx::CustomOperators::Init_1,
                                       tInfo,
                                       TensorType::ActGrad,
                                       InitType::Zero,
                                       Op::Settings{graph, ""});

  std::pair<Op *, bool> op1False{op1Ptr, false};
  std::pair<Op *, bool> op1True{op1Ptr, true};
  std::pair<Op *, bool> op2False{op2Ptr, false};
  std::pair<Op *, bool> op2True{op2Ptr, true};

  POpBoolCmp cmp;
  // When comparing pairs, the first element if compared first, if not equal the
  // second element is compared
  // true is converted to 1 and false to 0 during comparison
  BOOST_CHECK(cmp(op1False, op2True));  // First is less than
  BOOST_CHECK(cmp(op1False, op2False)); // First is less than

  BOOST_CHECK(cmp(op1False, op1True));   // First is equal, second is less than
  BOOST_CHECK(!cmp(op1False, op1False)); // First is equal, second is equal
  BOOST_CHECK(
      !cmp(op1True, op1False)); // First is equal, second is greater than

  BOOST_CHECK(!cmp(op2False, op1True));  // First is greater than
  BOOST_CHECK(!cmp(op2False, op1False)); // First is greater than
}

#ifdef POPART_STRICT_COMPARATOR_CHECKS
BOOST_AUTO_TEST_CASE(testPOpBoolCmpNegative) {
  // Test that the pair<Op *, bool> comparator throws error on nullptr

  // We use InitOp as Op is a virtual class
  InitOp *op1Ptr = nullptr;
  InitOp *op2Ptr = nullptr;

  std::pair<Op *, bool> op1False{op1Ptr, false};
  std::pair<Op *, bool> op1True{op1Ptr, true};
  std::pair<Op *, bool> op2False{op2Ptr, false};
  std::pair<Op *, bool> op2True{op2Ptr, true};

  POpBoolCmp cmp;

  auto checkError = [](const internal_error &error) {
    return std::string(error.what()) == "[POpBoolCmp] Invalid pointer.";
  };

  BOOST_CHECK_EXCEPTION(cmp(op1False, op2True), internal_error, checkError);
  BOOST_CHECK_EXCEPTION(cmp(op1False, op2False), internal_error, checkError);
  BOOST_CHECK_EXCEPTION(cmp(op1False, op1True), internal_error, checkError);
  BOOST_CHECK_EXCEPTION(cmp(op1False, op1False), internal_error, checkError);
  BOOST_CHECK_EXCEPTION(cmp(op1True, op1False), internal_error, checkError);
  BOOST_CHECK_EXCEPTION(cmp(op2False, op1True), internal_error, checkError);
  BOOST_CHECK_EXCEPTION(cmp(op2False, op1False), internal_error, checkError);
}
#endif

BOOST_AUTO_TEST_CASE(testPOpIntCmpPositive) {
  // Test that the pair<Op *, int> comparator is working
  Ir ir;
  auto &graph = ir.getMainGraph();

  // NOTE:
  // 1. All the op constructors explicitly set the id by calling
  //    Ir::getAndIncrOpsCounter()
  //    This means that we can be sure that the id of the ops are
  //    monotonically increasing
  // 2. We use InitOp as Op is a virtual class
  // 3. As the op needs to be in the Ir (at least when
  // POPART_STRICT_COMPARATOR_CHECKS is
  //    defined), we use the createOp function
  TensorInfo tInfo{};
  auto op1Ptr = graph.createOp<InitOp>(Onnx::CustomOperators::Init_1,
                                       tInfo,
                                       TensorType::ActGrad,
                                       InitType::Zero,
                                       Op::Settings{graph, ""});
  auto op2Ptr = graph.createOp<InitOp>(Onnx::CustomOperators::Init_1,
                                       tInfo,
                                       TensorType::ActGrad,
                                       InitType::Zero,
                                       Op::Settings{graph, ""});

  std::pair<Op *, int> op1Int0{op1Ptr, 0};
  std::pair<Op *, int> op1Int1{op1Ptr, 1};
  std::pair<Op *, int> op2Int0{op2Ptr, 0};
  std::pair<Op *, int> op2Int1{op2Ptr, 1};

  POpIntCmp cmp;
  // When comparing pairs, the first element if compared first, if not equal the
  // second element is compared
  // true is converted to 1 and false to 0 during comparison
  BOOST_CHECK(cmp(op1Int0, op2Int1)); // First is less than
  BOOST_CHECK(cmp(op1Int0, op2Int0)); // First is less than

  BOOST_CHECK(cmp(op1Int0, op1Int1));  // First is equal, second is less than
  BOOST_CHECK(!cmp(op1Int0, op1Int0)); // First is equal, second is equal
  BOOST_CHECK(!cmp(op1Int1, op1Int0)); // First is equal, second is greater than

  BOOST_CHECK(!cmp(op2Int0, op1Int1)); // First is greater than
  BOOST_CHECK(!cmp(op2Int0, op1Int0)); // First is greater than
}

#ifdef POPART_STRICT_COMPARATOR_CHECKS
BOOST_AUTO_TEST_CASE(testPOpIntCmpNegative) {
  // Test that the pair<Op*, int> comparator throws error on nullptr

  // We use InitOp as Op is a virtual class
  InitOp *op1Ptr = nullptr;
  InitOp *op2Ptr = nullptr;

  std::pair<Op *, int> op1Int0{op1Ptr, 0};
  std::pair<Op *, int> op1Int1{op1Ptr, 1};
  std::pair<Op *, int> op2Int0{op2Ptr, 0};
  std::pair<Op *, int> op2Int1{op2Ptr, 1};

  POpIntCmp cmp;

  auto checkError = [](const internal_error &error) {
    return std::string(error.what()) == "[POpIntCmp] Invalid pointer.";
  };

  BOOST_CHECK_EXCEPTION(cmp(op1Int0, op2Int1), internal_error, checkError);
  BOOST_CHECK_EXCEPTION(cmp(op1Int0, op2Int0), internal_error, checkError);
  BOOST_CHECK_EXCEPTION(cmp(op1Int0, op1Int1), internal_error, checkError);
  BOOST_CHECK_EXCEPTION(cmp(op1Int0, op1Int0), internal_error, checkError);
  BOOST_CHECK_EXCEPTION(cmp(op1Int1, op1Int0), internal_error, checkError);
  BOOST_CHECK_EXCEPTION(cmp(op2Int0, op1Int1), internal_error, checkError);
  BOOST_CHECK_EXCEPTION(cmp(op2Int0, op1Int0), internal_error, checkError);
}
#endif

class FakeCreatorCandidate : public popx::ICreatorCandidate {
private:
  const double max_priority_;
  const double num_elems_;
  const int64_t schedule_index_;

public:
  FakeCreatorCandidate(const double max_priority,
                       const double num_elems,
                       const int64_t schedule_index)
      : max_priority_(max_priority), num_elems_(num_elems),
        schedule_index_(schedule_index) {}

  double getMaxCreatorPriority() const override { return max_priority_; }

  int64_t getNumElems() const override { return num_elems_; }

  int64_t getScheduleIndex() const override { return schedule_index_; }

  std::pair<snap::Tensor, popx::ViewChangers>
  createInput(const poplar::DebugNameAndId &dnai) override {
    throw error(std::string(BOOST_CURRENT_FUNCTION) + "is unimplemented.");
  }

  DnfTensorIds mustExistBeforeCreate() override {
    throw error(std::string(BOOST_CURRENT_FUNCTION) + "is unimplemented.");
  }

  std::vector<std::vector<popx::OpxInAndOutIndex>>
  getPathsFromInput() override {
    throw error(std::string(BOOST_CURRENT_FUNCTION) + "is unimplemented.");
  }

  std::string str() override {
    throw error(std::string(BOOST_CURRENT_FUNCTION) + "is unimplemented.");
  }

  std::pair<snap::Tensor, popx::ViewChangers> unwind(snap::Tensor) override {
    throw error(std::string(BOOST_CURRENT_FUNCTION) + "is unimplemented.");
  }

  std::vector<popart::view::Region> unwind(popart::view::Region) override {
    throw error(std::string(BOOST_CURRENT_FUNCTION) + "is unimplemented.");
  }

  std::vector<popart::view::Region> unwind() override {
    throw error(std::string(BOOST_CURRENT_FUNCTION) + "is unimplemented.");
  }
};

typedef std::pair<FakeCreatorCandidate, FakeCreatorCandidate>
    FakeCreatorCandidatePair;

const std::vector<FakeCreatorCandidatePair> candidatess{{{2, 2, 3}, {1, 2, 3}},
                                                        {{1, 3, 3}, {1, 2, 3}},
                                                        {{1, 2, 2}, {1, 2, 3}}};

BOOST_TEST_DONT_PRINT_LOG_VALUE(FakeCreatorCandidatePair)

BOOST_DATA_TEST_CASE(testPICreatorCandidateCmpPositive,
                     boost::unit_test::data::make(candidatess),
                     candidates) {
  const auto &candidate0 = candidates.first;
  const auto &candidate1 = candidates.second;

  PICreatorCandidateCmp cmp;
  BOOST_CHECK(cmp(&candidate0, &candidate1));
  BOOST_CHECK(!cmp(&candidate0, &candidate0));
  BOOST_CHECK(!cmp(&candidate1, &candidate0));
}

#ifdef POPART_STRICT_COMPARATOR_CHECKS
BOOST_AUTO_TEST_CASE(testPICreatorCandidateCmpNegative) {
  FakeCreatorCandidate *null_candidate = nullptr;
  FakeCreatorCandidate candidate{1, 2, 3};

  auto checkError = [](const internal_error &error) {
    return std::string(error.what()) ==
           "[PICreatorCandidateCmp] Invalid pointer.";
  };

  PICreatorCandidateCmp cmp;
  BOOST_CHECK_EXCEPTION(
      cmp(null_candidate, &candidate), internal_error, checkError);
  BOOST_CHECK_EXCEPTION(
      cmp(&candidate, null_candidate), internal_error, checkError);
  BOOST_CHECK_EXCEPTION(
      cmp(null_candidate, null_candidate), internal_error, checkError);
}
#endif
