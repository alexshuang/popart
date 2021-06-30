#ifndef POPART__INTERNAL_IR_BINDINGS_IR_HPP
#define POPART__INTERNAL_IR_BINDINGS_IR_HPP

#include <pybind11/pybind11.h>

namespace py = pybind11;

namespace popart {
namespace _internal {
namespace ir {

/**
 * Add bindings for `popart::Ir` class to pybind module.
 **/
void bindIr(py::module &m);

} // namespace ir
} // namespace _internal
} // namespace popart

#endif // POPART__INTERNAL_IR_BINDINGS_IR_HPP