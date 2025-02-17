// Copyright (c) 2021 Graphcore Ltd. All rights reserved.
#ifndef POPART_WILLOW_INCLUDE_POPART_POPX_OP_EXCHANGE_MULTIEXCHANGEX_HPP_
#define POPART_WILLOW_INCLUDE_POPART_POPX_OP_EXCHANGE_MULTIEXCHANGEX_HPP_

#include "popart/popx/debugcontextx.hpp"
#include <map>
#include <set>
#include <snap/Program.hpp>
#include <snap/Tensor.hpp>
#include <utility>
#include <vector>
#include <popart/popx/op/exchange/exchangex.hpp>
#include <popart/popx/opxstate.hpp>

#include "popart/names.hpp"
#include "popart/popx/popopx.hpp"

namespace popart {
class Op;
class Tensor;

namespace popx {
class Devicex;

class MultiExchangeOpx : public ExchangeBaseOpx {
public:
  MultiExchangeOpx(Op *, Devicex *);
  void grow(snap::program::Sequence &) const final;
  std::vector<std::pair<int, int>> getSegments() const;

  // Resolves the part ID by fist resolving which exchange descriptors consume
  // the input tensor, then returns the part IDs matching the descriptor index
  std::set<OpxGrowPartId> getInGrowPartIds(Tensor *inTensor) const final;

  // Resolves the part ID by fist resolving which exchange descriptors produce
  // the output tensor, then returns the part IDs matching the descriptor index
  OpxGrowPartId getOutGrowPartId(Tensor *outTensor) const final;

  // Each part ID 1:1 matches an exchange descriptor index
  void growPart(OpxGrowPartId id) const final;

  InputCreatorType getInputCreatorType(InIndex index) const final;
  snap::Tensor
  createInputTensor(InIndex index,
                    const poplar::DebugNameAndId &dnai) const final;
  bool canUnwind(InIndex, OutIndex) const final;
  snap::Tensor unwindTensorLayout(snap::Tensor, InIndex, OutIndex) const final;
  view::RegMap unwindRegion(InIndex, OutIndex) const final;
};

class MultiExchangeOpxState : public OpxState {
public:
  std::map<int, snap::program::Sequence> preSeqs;
  std::map<int, snap::program::Sequence> exchangeSeqs;
  std::map<int, snap::program::Sequence> postSeqs;
};

} // namespace popx
} // namespace popart

#endif // POPART_WILLOW_INCLUDE_POPART_POPX_OP_EXCHANGE_MULTIEXCHANGEX_HPP_
