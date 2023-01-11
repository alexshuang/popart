// Copyright (c) 2020 Graphcore Ltd. All rights reserved.
#ifndef POPART_WILLOW_INCLUDE_POPART_POPX_VIEWCHANGERS_HPP_
#define POPART_WILLOW_INCLUDE_POPART_POPX_VIEWCHANGERS_HPP_

#include "poplar/Tensor.hpp"
#include <memory>
#include <typeinfo>
#include <vector>
namespace popart {
namespace popx {

// The identity view changer base class
class ViewChanger {
public:
  virtual ~ViewChanger() {}
  virtual poplar::Tensor apply(poplar::Tensor tensor) const { return tensor; }
  virtual bool containsAllDataRegions() const { return true; }
  virtual bool operator==(const ViewChanger &rhs) const {
    return typeid(&rhs) == typeid(ViewChanger);
  }
  virtual bool operator!=(const ViewChanger &rhs) const {
    return !(*this == rhs);
  }
};

// Chain of view changers
class ViewChangers {
public:
  ViewChangers();
  ViewChangers(std::vector<std::shared_ptr<ViewChanger>> viewChangers_);
  poplar::Tensor apply(poplar::Tensor tensor) const;
  bool empty() const { return viewChangers.empty(); }

  bool operator==(const ViewChangers &rhs) const;
  bool operator!=(const ViewChangers &rhs) const;

private:
  std::vector<std::shared_ptr<ViewChanger>> viewChangers;
};

} // namespace popx
} // namespace popart

#endif // POPART_WILLOW_INCLUDE_POPART_POPX_VIEWCHANGERS_HPP_
