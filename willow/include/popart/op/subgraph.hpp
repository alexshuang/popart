// Copyright (c) 2020 Graphcore Ltd. All rights reserved.
#ifndef GUARD_NEURALNET_SUBGRAPHOP_HPP
#define GUARD_NEURALNET_SUBGRAPHOP_HPP

#include <popart/op.hpp>

namespace popart {

class SubgraphOp : public Op {
public:
  // parent: Graph this CallOp belongs to
  SubgraphOp(const OperatorIdentifier &_opid, const Op::Settings &settings_);

  view::Regions modifies(InIndex) const override;
  view::Regions aliases(InIndex, OutIndex) const override;
  void addAlias(InIndex in,
                OutIndex out,
                view::Chains fwdChains,
                view::Chains bwdChains);

  void addModified(InIndex in, view::Regions regions);

  view::RegMap fwdRegMap(InIndex, OutIndex) const final;
  view::RegMap bwdRegMap(InIndex, OutIndex) const final;

  virtual InIndex subgraphInToOpInIndex(InIndex index) const = 0;
  virtual InIndex opInToSubgraphInIndex(InIndex index) const = 0;

  virtual OutIndex subgraphOutToOpOutIndex(OutIndex index) const = 0;
  virtual OutIndex opOutToSubgraphOutIndex(OutIndex index) const = 0;

  virtual Graph &getCalledGraph() const = 0;
  virtual void setCalledGraph(Graph &)  = 0;

  VGraphIdAndTileSet
  getIntrospectionInVirtualGraphId(InIndex index) const override;
  VGraphIdAndTileSet
  getIntrospectionOutVirtualGraphId(OutIndex index) const override;

  bool hasSideEffect() const override;

private:
  // Regions of Input Tensors (InIndex) are aliased by Output Tensors (OutIndex)
  std::map<std::pair<InIndex, OutIndex>, std::pair<view::Chains, view::Chains>>
      aliasMap;
  std::map<InIndex, view::Regions> modifiesMap;
};

} // namespace popart

#endif
