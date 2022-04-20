// Copyright (c) 2022 Graphcore Ltd. All rights reserved.
#define BOOST_TEST_MODULE RngMappingTest

#include <boost/test/unit_test.hpp>
#include <snap/Graph.hpp>
#include <snap/Tensor.hpp>
#include <poplar/Graph.hpp>
#include <poplar/Target.hpp>

#include "../../willow/src/popx/rng/rngstatelowering.hpp"
#include "popart/error.hpp"
#include "popart/util.hpp" // IWYU pragma: keep

// This is the function that was previously in rngstatelowering, for setting the
// layout of the RNG state tensors.
void testLayoutRNGStateTensor(snap::Graph &graph, snap::Tensor &tensor) {

  auto numTiles = graph.getTarget().getNumTiles();
  if (tensor.rank() >= 1 && tensor.dim(0) == numTiles) {

    for (auto tile = 0U; tile != numTiles; ++tile) {
      auto slice = tensor.slice({tile, tile + 1}, 0);
      graph.getPoplarGraph().setTileMapping(slice.getPoplarTensor(), tile);
    }

  } else {
    throw popart::internal_error(
        "[RngStateLowering] Expected tensor with first "
        "dimension of {} (got tensor shape {})",
        numTiles,
        tensor.shape());
  }
}

// Derive popart::popx::RngStateLowering to provide public access to
// createRNGStateTensor and layoutRNGStateTensor.
class RngStateLoweringLayoutTester : public popart::popx::RngStateLowering {
public:
  static snap::Tensor createStateTensor(snap::Graph &graph) {
    return createRNGStateTensor(graph, "");
  }
};

// This test checks that popart::popx::layoutRNGStateTensor produces the
// correct layout. As layoutRNGStateTensor uses poplibs internally, so this
// test will give us a warning if the poplibs implementation changes at all.
BOOST_AUTO_TEST_CASE(TestRngMapping) {
  auto target = poplar::Target::createIPUTarget(1, "ipu2");
  snap::Graph graph(target);

  auto t = RngStateLoweringLayoutTester::createStateTensor(graph);

  // This is the actual layout that is used in popart.
  auto actualLayout =
      graph.getPoplarGraph().getTileMapping(t.getPoplarTensor());

  testLayoutRNGStateTensor(graph, t);

  // This is the layout we want to have.
  auto expectedLayout =
      graph.getPoplarGraph().getTileMapping(t.getPoplarTensor());

  BOOST_REQUIRE_EQUAL(actualLayout, expectedLayout);
}
