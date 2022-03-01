# Copyright (c) 2021 Graphcore Ltd. All rights reserved.

import numpy as np
import pytest
import torch

import popxl
from popxl.testing import get_all_dtypes, get_all_int_dtypes


# TODO(T38031): Delete this utility function.
def _get_torch_version():
    """Utility function to convert the torch version to a tuple of ints.

    Returns:
        tuple: The version - a tuple of ints.
    """
    v = torch.__version__
    v = v.split('+')[0]
    v = v.split('.')
    v = tuple([int(i) for i in v])
    return v


class Testdtype:
    def test_constructor(self):
        with pytest.raises(TypeError) as excinfo:
            popxl.dtype()

    # TODO(T38031): Unskip this test.
    @pytest.mark.skipif(_get_torch_version() < (1, 7, 1),
                        reason="Requires torch>=1.7.1.")
    def test_properties(self):
        dtypes = get_all_dtypes()
        uint_dtypes = get_all_int_dtypes(include_signed=False)
        int_dtypes = get_all_int_dtypes()
        for popxl_dtype in dtypes:
            if popxl_dtype in uint_dtypes:
                # PyTorch doesn't have unsigned integers. These are tested
                # below.
                continue
            torch_dtype = eval(f'torch.{popxl_dtype._name}')
            assert torch_dtype.is_complex == popxl_dtype.is_complex
            assert torch_dtype.is_floating_point == popxl_dtype.is_floating_point
            assert torch_dtype.is_signed == popxl_dtype.is_signed
            assert popxl_dtype.is_int == (popxl_dtype in int_dtypes)
        for popxl_dtype in uint_dtypes:
            torch_dtype = eval(f'torch.{popxl_dtype._name[1:]}')
            assert torch_dtype.is_complex == popxl_dtype.is_complex
            assert torch_dtype.is_floating_point == popxl_dtype.is_floating_point
            assert torch_dtype.is_signed == True
            assert popxl_dtype.is_int == True

    def test_aliases(self):
        assert popxl.half == popxl.float16
        assert popxl.float == popxl.float32
        assert popxl.double == popxl.float64

    def test_conversion_numpy(self):
        popxl_dtypes = get_all_dtypes()
        np_dtypes = [
            eval(f'np.{popxl_dtype._name}') for popxl_dtype in popxl_dtypes
        ]

        for popxl_dtype, np_dtype in zip(popxl_dtypes, np_dtypes):
            arr = np.zeros((1, ), np_dtype)
            assert popxl_dtype == popxl.dtype.as_dtype(arr)
            assert popxl_dtype == popxl.dtype.as_dtype(np_dtype)
            assert popxl_dtype.as_numpy() == np_dtype

        with pytest.raises(ValueError) as excinfo:
            popxl.dtype.as_dtype(np.str)

    def test_conversion_string(self):
        popxl_dtypes = get_all_dtypes()
        names = [popxl_dtype._name for popxl_dtype in popxl_dtypes]

        for popxl_dtype, name in zip(popxl_dtypes, names):
            assert popxl_dtype == popxl.dtype.as_dtype(name)

    def test_conversion_python(self):
        import builtins
        py_to_pir = {
            builtins.bool: popxl.bool,
            builtins.float: popxl.float32,
            builtins.int: popxl.int64
        }

        for py_type, popxl_dtype in py_to_pir.items():
            assert popxl_dtype == popxl.dtype.as_dtype(py_type)