// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#include <memory>
#include <popart/graph.hpp>
#include <popart/ir.hpp>
#include <popart/op/iotilecopy.hpp>
#include <popart/opmanager.hpp>
#include <popart/opserialiser.hpp>
#include <popart/tensor.hpp>
#include <popart/tensorindex.hpp>

namespace popart {

IoTileCopyOp::IoTileCopyOp(const OperatorIdentifier &_opid,
                           const Op::Settings &settings_)
    : Op(_opid, settings_) {}

std::unique_ptr<Op> IoTileCopyOp::clone() const {
  return std::make_unique<IoTileCopyOp>(*this);
}

void IoTileCopyOp::setup() {
  for (auto &idx_tensor : input->tensorMap()) {
    auto idx     = idx_tensor.first;
    outInfo(idx) = inInfo(idx);
  }
}

VGraphIdAndTileSet
IoTileCopyOp::getIntrospectionInVirtualGraphId(InIndex) const {
  return {hasVirtualGraphId() ? getVirtualGraphId() : unusedVGraphId,
          settings.tileSet == TileSet::Compute ? TileSet::IO
                                               : TileSet::Compute};
}

VGraphIdAndTileSet
IoTileCopyOp::getIntrospectionOutVirtualGraphId(OutIndex) const {
  return {hasVirtualGraphId() ? getVirtualGraphId() : unusedVGraphId,
          settings.tileSet};
}

} // namespace popart
