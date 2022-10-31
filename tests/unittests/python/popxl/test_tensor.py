# Copyright (c) 2021 Graphcore Ltd. All rights reserved.
import numpy as np
import pytest

import popart._internal.ir as _ir
import popxl
from popxl.dtypes import dtype
from popxl.tensor import Variable, Constant
from popxl.errors import UndefinedValue
from popxl.utils import downcast_np_dtypes

type_map = {Variable: _ir.TensorType.Variable, Constant: _ir.TensorType.Const}
ctor_map = {Variable: popxl.variable, Constant: popxl.constant}


class TestTensor:
    @pytest.mark.parametrize("t_class", [Variable, Constant])
    @pytest.mark.parametrize(
        "data",
        [
            np.random.rand(1, 2, 3),
            [[[1, 2, 3], [4, 5, 6]]],
            (((1, 2, 3), (4, 5, 6)),),
        ],
    )
    @pytest.mark.parametrize("dtype", [popxl.float16, None])
    @pytest.mark.parametrize("name", ["a", None])
    def test_construction0(self, t_class, data, dtype, name):
        """Test construction of tensors that hold n-d data at graph creation."""
        ir = popxl.Ir()
        main = ir.main_graph

        with main:
            kwargs = {}
            if name is not None:
                kwargs["name"] = name
            if dtype is not None:
                kwargs["dtype"] = dtype

            exp_name = f"{name}" if name is not None else "t"

            def exp_np_dtype_(dtype):
                if dtype is not None:
                    return dtype
                else:
                    np_data = np.array(data)
                    if np_data.dtype in downcast_np_dtypes:
                        return popxl.dtypes.dtype.as_dtype(
                            downcast_np_dtypes[np_data.dtype]
                        )
                    else:
                        return popxl.dtypes.dtype.as_dtype(np_data.dtype)

            exp_dtype = exp_np_dtype_(dtype)

            t = ctor_map[t_class](data, **kwargs)

            assert isinstance(t, t_class)
            assert t.dtype == exp_dtype
            assert t.shape == (1, 2, 3)
            assert len(t) == 1
            assert t.nelms == 6
            assert t.name == exp_name

            pb_t: _ir.Tensor = main._pb_graph.getTensor(exp_name)
            assert pb_t.id == t.name
            assert pb_t.id == t.id
            assert pb_t.tensorType() == type_map[t_class]
            assert pb_t.hasTensorData()
            hash(t)

    @pytest.mark.parametrize("t_class", [Variable, Constant])
    def test_construction1(self, t_class):
        """Test construction of tensors that hold 0-d data at graph creation."""
        ir = popxl.Ir()
        main = ir.main_graph

        with main:
            t = ctor_map[t_class](1.0)
            assert t.dtype == popxl.float32
            assert t.shape == ()
            assert t.nelms == 1

    def test__ensure_tensor(self):
        """Test the `_ensure_tensor()` method."""
        ir = popxl.Ir()
        main = ir.main_graph

        with main:
            a = popxl.variable(1)
            b = popxl.variable(2)
            c = a._ensure_tensor(b)
            d = a._ensure_tensor(3)

            assert c == b
            assert isinstance(d, Constant)
            assert d.dtype == a.dtype

    def test_from_pb_type(self):
        """Test the from_pb_tensor returns the correct python type"""
        ir = popxl.Ir()
        main = ir.main_graph

        with main:
            a = popxl.variable(1)
            c = popxl.constant(2)

        assert isinstance(a, Variable)
        new_a = popxl.Tensor._from_pb_tensor(a._pb_tensor)
        assert isinstance(new_a, Variable)
        assert isinstance(c, Constant)
        new_c = popxl.Tensor._from_pb_tensor(c._pb_tensor)
        assert isinstance(new_c, Constant)

    def test_get_ir(self):
        ir = popxl.Ir()
        main = ir.main_graph

        with main:
            a = popxl.variable(1)
            assert a.ir == ir

    def test_cmp(self):
        ir = popxl.Ir()
        main = ir.main_graph

        with main:
            a = popxl.variable(1)
            b = popxl.variable(1)
            assert a != b  # test __eq__
            assert len(set([a, b])) == 2  # test __hash__
            str(a)  # test __repr__

    def test_iter_dunder(self):
        with popxl.Ir().main_graph:
            x = popxl.variable(0)
            l = []
            with pytest.raises(ValueError):
                l += x

    def test_contains_dunder(self):
        with popxl.Ir().main_graph:
            x = popxl.variable(0)
            with pytest.raises(TypeError):
                1 in x

    def test_len(self):
        with popxl.Ir().main_graph:
            x = popxl.variable(
                [
                    [
                        1,
                    ],
                    [2.0],
                ]
            )
            assert len(x) == 2

    def test_len_scalar(self):
        with popxl.Ir().main_graph:
            x = popxl.variable(1)
            with pytest.raises(ValueError):
                len(x)

    def test_subgraph_variable_error(self):
        ir = popxl.Ir()
        with ir.main_graph:
            subgraph = ir.create_empty_graph()
            with subgraph:
                with pytest.raises(ValueError):
                    _ = popxl.variable(1)

    def test_repr(self):
        def subgraph1(a: popxl.Tensor):
            return a + a

        ir = popxl.Ir()
        with ir.main_graph:
            a = popxl.variable([1], name="bob")
            g = ir.create_graph(subgraph1, a)

            assert repr(a) == "Tensor[bob popxl.dtypes.int32 (1,)]"
            assert (
                repr(g.inputs[0])
                == "Tensor[TestTensor.test_repr.subgraph1_subgraph(0)/a popxl.dtypes.int32 (1,)]"
            )


class TestTensorIpuAndTileSet:
    def test_ipu_undefined(self):
        ir = popxl.Ir()
        main = ir.main_graph

        with main:
            a = popxl.variable(1)

            with pytest.raises(UndefinedValue):
                a.ipu

    def test_ipu_defined_default(self):
        ir = popxl.Ir()
        main = ir.main_graph

        with main:
            a = popxl.variable(1) + 0
            assert a.ipu == 0

    def test_ipu_set(self):
        ir = popxl.Ir()
        main = ir.main_graph

        with main:
            with popxl.ipu(1):
                a = popxl.variable(1) + 0
            assert a.ipu == 1

    def test_tile_set_undefined(self):
        ir = popxl.Ir()
        main = ir.main_graph

        with main:
            a = popxl.variable(1)

            with pytest.raises(UndefinedValue):
                a.tile_set

    def test_tile_set_compute(self):
        ir = popxl.Ir()
        main = ir.main_graph

        with main:
            a = popxl.variable(1) + 0
            assert a.tile_set == "compute"

    def test_ipu_defined_default_with_io_tiles(self):
        ir = popxl.Ir()
        main = ir.main_graph

        with main:
            with popxl.io_tiles():
                a = popxl.variable(1) + 0
            assert a.tile_set == "io"


class TestTensorGetItem:
    def test_integer_slicing(self):
        with popxl.Ir().main_graph:
            x = popxl.variable(np.arange(10))
            y = x[1]
            assert y.shape == tuple()  # Empty as dim squeezed

    def test_slice_slicing(self):
        with popxl.Ir().main_graph:
            x = popxl.variable(np.random.rand(10, 10))
            y = x[1:3]
            assert y.shape == (2, 10)

    def test_both_int_and_slice_slicing(self):
        with popxl.Ir().main_graph:
            x = popxl.variable(np.random.rand(10, 10))
            y = x[1:3, 2]
            assert y.shape == (2,)

    @pytest.mark.parametrize("tensorlike", [popxl.variable, np.array, list])
    def test_integer_indexing_tensor(self, tensorlike):
        with popxl.Ir().main_graph:
            indices = [[0, 1], [1, 1]]
            indices = tensorlike(indices)
            x = popxl.variable(np.random.rand(10, 10))
            y = x[indices]
            assert y.shape == (2, 2, 10)

    @pytest.mark.parametrize("tensorlike", [popxl.variable, np.array, list])
    def test_bool_indexing_tensor(self, tensorlike):
        with popxl.Ir().main_graph:
            mask = [[True, False], [True, False], [False, True], [True, True]]
            mask = tensorlike(mask)
            x = popxl.variable(np.random.rand(4, 2))
            y = x[mask]
            assert y.shape == (4, 2)

    @pytest.mark.parametrize("tensorlike", [popxl.variable, np.array, list])
    def test_bool_indexing_tensor_broadcast(self, tensorlike):
        with popxl.Ir().main_graph:
            mask = [[True], [True], [False], [True]]
            mask = tensorlike(mask)
            x = popxl.variable(np.random.rand(4, 2))
            y = x[mask]
            assert y.shape == (4, 2)

    @pytest.mark.parametrize("key", ["a", True, 1.1])
    def test_bad_key(self, key):
        with popxl.Ir().main_graph:
            x = popxl.variable(np.arange(2))
            with pytest.raises(TypeError):
                _ = x[key]

    def test_slice_step_1d(self):
        with popxl.Ir().main_graph:
            x = popxl.variable(np.random.rand(10, 10))
            y = x[1:5:2]
            assert y.shape == (2, 10)

    def test_slice_step(self):
        with popxl.Ir().main_graph:
            x = popxl.variable(np.random.rand(10, 10))
            y = x[1:5:2, 2:6]
            assert y.shape == (2, 4)

    def test_slice_step_and_int(self):
        with popxl.Ir().main_graph:
            x = popxl.variable(np.random.rand(10, 10))
            y = x[1:5:2, 1]
            assert y.shape == (2,)

    def test_slice_negative_step(self):
        with popxl.Ir().main_graph:
            x = popxl.variable(np.random.rand(10, 10))
            y = x[9:4:-2]
            assert y.shape == (3, 10)

    def test_slice_step_only_1d(self):
        with popxl.Ir().main_graph:
            x = popxl.variable(np.random.rand(10, 10))
            y = x[::5]
            assert y.shape == (2, 10)

    def test_slice_step_only(self):
        with popxl.Ir().main_graph:
            x = popxl.variable(np.random.rand(10, 10))
            y = x[::5, ::2]
            assert y.shape == (2, 5)


class TestTensorSpec:
    def test_init(self):
        spec = popxl.TensorSpec((1, 2), popxl.int32)
        assert spec.shape == (1, 2)
        assert spec.dtype == popxl.int32

    def test_dict_unpacking(self):
        with popxl.Ir().main_graph:
            spec = popxl.TensorSpec((1, 2), popxl.int32)
            popxl.graph_input(**spec, name="w")


@pytest.mark.parametrize("dtype", [popxl.float8_143, popxl.float8_152])
@pytest.mark.parametrize("log2_scale", [-2, -1, 0, 1, 2])
@pytest.mark.parametrize("nan_on_overflow", [True, False])
class TestFloat8Constant:
    """Test creation of float 8 constants."""

    def test_create_from_scalar(
        self, dtype: dtype, log2_scale: int, nan_on_overflow: bool
    ):
        ir = popxl.Ir()
        main = ir.main_graph

        with main:
            c = popxl.constant(
                2.0,
                dtype,
                log2_scale=log2_scale,
                nan_on_overflow=nan_on_overflow,
                name="constant_fp8",
            )
            assert isinstance(c, Constant)
            assert c.shape == tuple()
            assert c.dtype == dtype

    @pytest.mark.parametrize("np_type", [np.float32, np.float64])
    def test_create_from_array(
        self, dtype: dtype, log2_scale: int, nan_on_overflow: bool, np_type: np.dtype
    ):
        ir = popxl.Ir()
        main = ir.main_graph

        with main:
            data = np.array([int(1), 24, 0.34, float(343.23)]).astype(np_type)
            c = popxl.constant(
                data,
                dtype,
                log2_scale=log2_scale,
                nan_on_overflow=nan_on_overflow,
                name="constant_fp8",
            )
            assert isinstance(c, Constant)
            assert c.shape == data.shape
            assert c.dtype == dtype
