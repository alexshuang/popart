// Copyright (c) 2019 Graphcore Ltd. All rights reserved.
#ifndef POPART_WILLOW_INCLUDE_POPART_TRANSFORMS_MERGECOPIES_HPP_
#define POPART_WILLOW_INCLUDE_POPART_TRANSFORMS_MERGECOPIES_HPP_

#include <cstddef>
#include <string>
#include <popart/transforms/transform.hpp>

namespace popart {
class Graph;

class MergeCopies : public Transform {
public:
  static std::size_t id();

  MergeCopies() : Transform() {}
  virtual ~MergeCopies() override {}

  virtual bool apply(Graph &graph) const final;

  virtual std::size_t getId() const final { return id(); }

  virtual std::string getName() const final { return "MergeCopies"; }
};

} // namespace popart

#endif // POPART_WILLOW_INCLUDE_POPART_TRANSFORMS_MERGECOPIES_HPP_
