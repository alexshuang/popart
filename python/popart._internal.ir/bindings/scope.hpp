// Copyright (c) 2021 Graphcore Ltd. All rights reserved.
#ifndef POPART__INTERNAL_IR_BINDINGS_SCOPE_HPP
#define POPART__INTERNAL_IR_BINDINGS_SCOPE_HPP

#include <pybind11/pybind11.h>

namespace py = pybind11;

namespace popart {
namespace _internal {
namespace ir {

/**
 * Add bindings for `popart::Scope` class to pybind module.
 **/
void bindScope(py::module &m);

} // namespace ir
} // namespace _internal
} // namespace popart

#endif // POPART__INTERNAL_IR_BINDINGS_SCOPE_HPP