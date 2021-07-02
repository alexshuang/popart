#ifndef POPART_TESTS_UNITTESTS_OPTIMIZER_SGDTESTCASE_HPP
#define POPART_TESTS_UNITTESTS_OPTIMIZER_SGDTESTCASE_HPP

#include <popart/graph.hpp>
#include <popart/ir.hpp>
#include <popart/optimizer.hpp>
#include <popart/sessionoptions.hpp>
#include <popart/tensor.hpp>

namespace _detail {
using namespace popart;

struct SGDTestCase {
  SGD sgd;
  Ir ir;
  const TensorId wId = "w";
  Tensor *w; // is FLOAT.

  Graph &graph() { return ir.getMainGraph(); }

  // Must call this first before using most of SGD functionality.
  void setFactorsFromOptions() {
    sgd.setFactorsFromOptions(ir.getSessionOptions());
  }

protected:
  explicit SGDTestCase(SGD sgd_) : sgd{std::move(sgd_)} {
    // Warning: Instead of creating stubs with known semantics of the
    // dependencies of the methods under test (Tensor, etc.), we are going
    // through a bunch of code that we hope gives dependencies with the intended
    // semantics. This includes the below code as well as, say, the
    // default-constructed SGD member. Thus, these tests of the SGD class are
    // dependent on the semantics of these other classes, and so could break in
    // the future.

    Graph &graph = ir.getMainGraph();
    std::vector<float> wHost(2 * 2);
    graph.getTensors().addVarInit(
        wId, TensorInfo{DataType::FLOAT, Shape{2, 2}}, wHost.data());

    w = graph.getTensors().get(wId);
  }
};

} // namespace _detail

/**
 * SGD0; unset OptimizerValues; all weights default.
 */
struct SGD0TestCase : public _detail::SGDTestCase {
  SGD0TestCase() : SGDTestCase(popart::SGD{}) {}
};

/**
 * SGD1 as gradient accumulation factor is 2, SGDAccumulatorAndMomentum is
 * Combined; unset OptimizerValues; all weights default.
 */
struct SGD1TestCase : public _detail::SGDTestCase {
  SGD1TestCase()
      : SGDTestCase(popart::SGD{{{"defaultMomentum", {0.20f, true}}},
                                {},
                                popart::SGDAccumulatorAndMomentum::Combined}) {
    using namespace popart;

    SessionOptions opts;
    opts.enableGradientAccumulation = true;
    opts.accumulationFactor         = 2;
    ir.setUserOptions(opts);
  }
};

/**
 * SGD2 as gradient accumulation factor is 2, SGDAccumulatorAndMomentum is
 * Separate; non-zero momentum; other OptimizerValues unset; all weights
 * default.
 */
struct SGD2TestCase : public _detail::SGDTestCase {
  SGD2TestCase()
      : SGDTestCase(popart::SGD{{{"defaultMomentum", {0.20f, true}}},
                                {},
                                popart::SGDAccumulatorAndMomentum::Separate}) {
    using namespace popart;

    SessionOptions opts;
    opts.enableGradientAccumulation = true;
    opts.accumulationFactor         = 2;
    ir.setUserOptions(opts);
  }
};

/**
 * Allows user to pass in SGD and set options.
 */
struct SGDCustomTestCase : public _detail::SGDTestCase {
  SGDCustomTestCase(popart::SGD sgd_) : SGDTestCase(std::move(sgd_)) {}
};

#endif
