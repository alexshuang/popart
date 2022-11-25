// Copyright (c) 2018 Graphcore Ltd. All rights reserved.
#ifndef POPART_WILLOW_INCLUDE_POPART_TRANSFORMS_TRANSFORM_HPP_
#define POPART_WILLOW_INCLUDE_POPART_TRANSFORMS_TRANSFORM_HPP_

#include <cstddef>
#include <string>

namespace popart {

class Graph;

class Transform {
public:
  Transform() {}
  virtual ~Transform() {}

  virtual bool apply(Graph &graph) const = 0;

  virtual std::size_t getId() const = 0;

  virtual std::string getName() const = 0;

  // apply a transformation to the given Ir
  static void applyTransform(std::size_t transformId, Graph &);

  // add a transform to the list of transforms
  static bool registerTransform(Transform *transform);

  // get transform's unique Id from name
  static std::size_t getIdFromName(const std::string &transformName);
};

} // namespace popart

#endif // POPART_WILLOW_INCLUDE_POPART_TRANSFORMS_TRANSFORM_HPP_
