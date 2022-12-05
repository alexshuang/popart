// Copyright (c) 2021 Graphcore Ltd. All rights reserved.

#include "bindings/tensordata.hpp"

#include <initializer_list>
#include <pybind11/buffer_info.h>
#include <pybind11/cast.h>
#include <pybind11/detail/common.h> // IWYU pragma: keep
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <popart/tensordata.hpp>

#include "../../popart/shared_cpp/np_utils.hpp"

namespace py = pybind11;

namespace popart {
class TensorInfo;

namespace _internal {
namespace ir {

void bindTensorData(py::module &m) {
  py::class_<TensorData>(m, "TensorData")
      .def(py::init([](const TensorInfo &info, py::array data) {
             data = makeContiguous(data);
             return TensorData::fromCopyOf(data.request().ptr, info.nbytes());
           }),
           py::arg("tensorInfo"),
           py::arg("src"))
      .def(
          "resetData",
          [](TensorData &self, const TensorInfo &info, py::array data) {
            data = makeContiguous(data);
            self.resetData(info, data.request().ptr);
          },
          py::arg("tensorInfo"),
          py::arg("src"));
}

} // namespace ir
} // namespace _internal
} // namespace popart
