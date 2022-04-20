// Copyright (c) 2021 Graphcore Ltd. All rights reserved.
#ifndef POPART__INTERNAL_IR_BINDINGS_ACCUMULATORSCALE_HPP
#define POPART__INTERNAL_IR_BINDINGS_ACCUMULATORSCALE_HPP

#include <pybind11/pybind11.h>

namespace py = pybind11;

namespace popart {
namespace _internal {
namespace ir {
namespace op {

/**
 * Add bindings for the accumulatorscale op.
 **/
void bindAccumulatorScale(py::module &m);

} // namespace op
} // namespace ir
} // namespace _internal
} // namespace popart

#endif // POPART__INTERNAL_IR_BINDINGS_ACCUMULATORSCALE_HPP
