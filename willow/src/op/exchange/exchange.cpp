// Copyright (c) 2021 Graphcore Ltd. All rights reserved.
#include <algorithm>
#include <popart/graph.hpp>
#include <popart/ir.hpp>
#include <popart/op/exchange/multiexchange.hpp>
#include <popart/opmanager.hpp>
#include <popart/opserialiser.hpp>
#include <popart/tensor.hpp>

namespace popart {

std::ostream &operator<<(std::ostream &ost, const ExchangeStrategy &es) {
  switch (es) {
  case (ExchangeStrategy::JustInTime): {
    ost << "JustInTime";
    break;
  }
  case (ExchangeStrategy::OverlapInnerLoop): {
    ost << "OverlapInnerLoop";
    break;
  }
  case (ExchangeStrategy::OverlapLoops): {
    ost << "OverlapLoops";
    break;
  }
  case (ExchangeStrategy::OverlapStep): {
    ost << "OverlapStep";
    break;
  }
  default: {
    throw error("Unexpected value for ExchangeStrategy {}",
                static_cast<int>(es));
  }
  }
  return ost;
}

ExchangeDescriptor::ExchangeDescriptor(ExchangeDirection direction_,
                                       TensorId id_,
                                       OptionalVGraphId vgid_,
                                       TileSet tileSet_,
                                       int numInputs_,
                                       int numOutputs_)
    : direction(direction_), remoteBufferId(-1), hostStreamTensorId(id_),
      vgid(vgid_), tileSet(tileSet_), numInputs(numInputs_),
      numOutputs(numOutputs_), inplace(false) {}

ExchangeDescriptor::ExchangeDescriptor(ExchangeDirection direction_,
                                       RemoteBufferId id_,
                                       OptionalVGraphId vgid_,
                                       TileSet tileSet_,
                                       int numInputs_,
                                       int numOutputs_,
                                       bool inplace_)
    : direction(direction_), remoteBufferId(id_), hostStreamTensorId(),
      vgid(vgid_), tileSet(tileSet_), numInputs(numInputs_),
      numOutputs(numOutputs_), inplace(inplace_) {}

std::ostream &operator<<(std::ostream &ost, const ExchangeDirection &ed) {
  switch (ed) {
  case (ExchangeDirection::Load): {
    ost << "Load";
    break;
  }
  case (ExchangeDirection::Store): {
    ost << "Store";
    break;
  }
  default: {
    throw error("Unexpected value for ExchangeDirection {}",
                static_cast<int>(ed));
  }
  }
  return ost;
}

const std::string ExchangeDescriptor::getResourceId() const {
  std::stringstream ss;
  ss << "([" << getHostStreamTensorId() << "], [";
  ss << getHostStreamTensorId() << "], [";
  ss << std::to_string(getRemoteBufferId()) << "])";
  return ss.str();
}

std::ostream &operator<<(std::ostream &ost, const ExchangeDescriptor &ed) {
  ost << "(";
  ost << "direction=" << ed.getDirection();
  ost << ", remoteBufferId=" << ed.getRemoteBufferId();
  ost << ", hostStreamTensorId=" << ed.getHostStreamTensorId();
  ost << ", vGraphId="
      << (ed.getVGraphID() ? *ed.getVGraphID() : unusedVGraphId);
  ost << ", tiles=" << ed.getTileSet();
  ost << ")";
  return ost;
}

std::pair<int, int>
ExchangeBaseOp::inIndexToDescriptorIndex(InIndex index) const {
  if (hasInput(index)) {
    return {0, index};
  } else {
    throw error("[ExchangeBaseOp] No input at index {}", index);
  }
}

std::pair<int, int>
ExchangeBaseOp::outIndexToDescriptorIndex(OutIndex index) const {
  if (hasOutput(index)) {
    return {0, index};
  } else {
    throw error("[ExchangeBaseOp] No input at index {}", index);
  }
}

std::vector<InIndex>
ExchangeBaseOp::descriptorIndexToInIndices(int index) const {
  if (index == 0) {
    std::vector<InIndex> indices;
    for (auto &i : input->tensorMap()) {
      indices.push_back(i.first);
    }
    return indices;
  } else {
    throw error("[ExchangeBaseOp] No descriptor at index {}", index);
  }
}

std::vector<OutIndex>
ExchangeBaseOp::descriptorIndexToOutIndices(int index) const {
  if (index == 0) {
    std::vector<OutIndex> indices;
    for (auto &i : output->tensorMap()) {
      indices.push_back(i.first);
    }
    return indices;
  } else {
    throw error("[ExchangeBaseOp] No descriptor at index {}", index);
  }
}

} // namespace popart
