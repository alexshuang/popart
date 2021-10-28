// Copyright (c) 2021 Graphcore Ltd. All rights reserved.
#include "bindings/region.hpp"

#include <pybind11/operators.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <sstream>

#include <popart/region.hpp>

namespace py = pybind11;

namespace popart {
namespace _internal {
namespace ir {

void bindRegion(py::module &m) {
  auto sm = m.def_submodule("view");

  // Not binding AccessType for now.

  py::class_<view::Region>(sm, "Region")
      .def("__str__",
           [](view::Region &self) {
             std::stringstream ss;
             ss << self;
             return ss.str();
           })
      .def("isEmpty", &view::Region::isEmpty)
      .def_static("getFull",
                  [](Shape shape) { return view::Region::getFull(shape); })
      .def_static("getEmpty", &view::Region::getEmpty);
}

} // namespace ir
} // namespace _internal
} // namespace popart