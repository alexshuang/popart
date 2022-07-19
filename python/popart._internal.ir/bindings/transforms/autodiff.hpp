// Copyright (c) 2021 Graphcore Ltd. All rights reserved.
#ifndef POPART_PYTHON_POPART__INTERNAL_IR_BINDINGS_TRANSFORMS_AUTODIFF_HPP_
#define POPART_PYTHON_POPART__INTERNAL_IR_BINDINGS_TRANSFORMS_AUTODIFF_HPP_

#include <pybind11/pybind11.h>

namespace py = pybind11;

namespace popart {
namespace _internal {
namespace ir {
namespace transforms {

/**
 * Add bindings for `popart::Autodiff` class to pybind module.
 **/
void bindAutodiff(py::module &m);

} // namespace transforms
} // namespace ir
} // namespace _internal
} // namespace popart

#endif // POPART_PYTHON_POPART__INTERNAL_IR_BINDINGS_TRANSFORMS_AUTODIFF_HPP_
