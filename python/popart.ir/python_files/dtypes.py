# Copyright (c) 2021 Graphcore Ltd. All rights reserved.
"""Definition and instances of a class to represent `Tensor` data types."""

import builtins
from typing import Any, Mapping
import numpy as np
import popart._internal.ir as _ir

# A dictionary to map from numpy to popart.ir types.
_NP_TO_PIR: Mapping[np.dtype, 'dtype'] = {}
# A dictionary to map from string to popart.ir types.
_STR_TO_PIR: Mapping[str, 'dtype'] = {}
# A dictionary to map from Python to popart.ir types.
_PY_TO_PIR: Mapping[Any, 'dtype'] = {}
# A dictionart to map from popart._ir to popart.ir types.
_PB_TO_PIR: Mapping[_ir.DataType, 'dtype'] = {}


class dtype:
    def __init__(self) -> None:
        """A class to represent the type of elements in a tensor.

        Available data types are:
            - `bool`
            - `int8`
            - `int16`
            - `int32`
            - `int64`
            - `uint8`
            - `uint16`
            - `uint32`
            - `uint64`
            - `float16`
            - `float32`
            - `float64`
            - `complex64`
            - `complex128`

        Some data types have aliases:
            - `half` aliases `float16`
            - `float` aliases `float32`
            - `double` aliases `float64`

        Raises:
            TypeError: This class cannot be initialised.
        """
        self._is_complex: bool = None
        self._is_int: bool = None
        self._is_floating_point: bool = None
        self._is_signed: bool = None
        self._name: str = None
        self._np_type: np.dtype = None
        self._pb_dtype: _ir.DataType = None
        raise TypeError(f"Cannot create {self.__module__}.dtype instances.")

    @property
    def is_complex(self) -> bool:
        return self._is_complex

    @property
    def is_int(self) -> bool:
        return self._is_int

    @property
    def is_floating_point(self) -> bool:
        return self._is_floating_point

    @property
    def is_signed(self) -> bool:
        return self._is_signed

    @classmethod
    def as_dtype(cls, type_value) -> 'dtype':
        """Converts the given `type_value` to a `popart.ir.dtype`.

        Args:
            type_value:
                A `numpy.dtype`, string, a built-in Python type or a
                `numpy.ndarray` which can be converted to a `popart.ir.dtype`.

        Raises:
            ValueError:
                If `type_value` cannot be converted to a `popart.ir.dtype`.

        Returns:
            dtype: A `popart.ir.dtype` corresponding to `type_value`.
        """
        try:
            return _STR_TO_PIR[type_value]
        except (KeyError, TypeError):
            pass

        try:
            return _PY_TO_PIR[type_value]
        except (KeyError, TypeError):
            pass

        try:
            return _PB_TO_PIR[type_value]
        except (KeyError, TypeError):
            pass

        try:
            return _NP_TO_PIR[type_value]
        except (KeyError, TypeError):
            pass

        try:
            return _NP_TO_PIR[np.dtype(type_value)]
        except (KeyError, TypeError):
            pass

        if hasattr(type_value, 'dtype'):
            try:
                return _NP_TO_PIR[type_value.dtype]
            except (KeyError, TypeError):
                pass

        raise ValueError(
            f'There is not a `popart.ir.dtype` that is compatible'
            f' with value: {type_value}, type: {type(type_value)}.')

    def as_numpy(self) -> np.dtype:
        """Converts the `popart.ir.dtype` to a corresponding `numpy.dtype`.

        Raises:
            TypeError:
                If the `popart.ir.dtype` cannot be converted to a `numpy.dtype`.

        Returns:
            np.dtype:
                A `numpy.dtype` corresponding to `popart.ir.dtype`.
        """
        if self._np_type is not None:
            return self._np_type
        else:
            raise TypeError(f'`popart.ir.{self._name}` does not have a '
                            f'corresponding NumPy type.')

    def __repr__(self) -> str:
        return f'{self.__module__}.{self._name}'

    @classmethod
    def _factory(
            cls,
            name: str,
            is_complex: bool,
            is_int: bool,
            is_floating_point: bool,
            is_signed: bool,
            np_type: np.dtype,
            py_type: str,
            pb_type: _ir.DataType,
    ) -> 'dtype':
        """Factory method to construct `dtype` instances.

        Args:
            name (str):
                The name of the `dtype`. Used in `__repr__()`.
            is_complex (bool):
                Is the type complex.
            is_int (bool):
                Is the type an integer (signed or unsiged).
            is_floating_point (bool):
                Is the type floating point.
            is_signed (bool):
                Is the type signed.
            np_type (np.dtype):
                A `numpy.dtype` that corresponds to the created `dtype`.
            pb_type (_ir.DataType):
                The corresponding low-level `pybind11` `DataType`.

        Returns:
            dtype:
                A new `dtype` instance.
        """
        global _NP_TO_PIR, _STR_TO_PIR, _PY_TO_PIR

        self: 'dtype' = super().__new__(cls)
        self._is_complex = is_complex
        self._is_int = is_int
        self._is_floating_point = is_floating_point
        self._is_signed = is_signed
        self._name = name
        self._np_type = np_type
        self._pb_dtype = pb_type

        if np_type is not None:
            assert np_type not in _NP_TO_PIR
            _NP_TO_PIR[np_type] = self

        assert name not in _STR_TO_PIR
        _STR_TO_PIR[name] = self

        if py_type is not None:
            assert py_type not in _PY_TO_PIR
            _PY_TO_PIR[py_type] = self

        if pb_type is not None:
            assert py_type not in _PB_TO_PIR
            _PB_TO_PIR[pb_type] = self

        return self


# yapf: disable
# Fixed point types
bool = dtype._factory('bool', False, False, False, False, np.dtype('bool_'), builtins.bool, _ir.DataType.BOOL)
int8 = dtype._factory('int8', False, True, False, True, np.dtype('int8'), None, _ir.DataType.INT8)
int16 = dtype._factory('int16', False, True, False, True, np.dtype('int16'), None, _ir.DataType.INT16)
int32 = dtype._factory('int32', False, True, False, True, np.dtype('int32'), None, _ir.DataType.INT32)
int64 = dtype._factory('int64', False, True, False, True, np.dtype('int64'), None, _ir.DataType.INT64)
uint8 = dtype._factory('uint8', False, True, False, False, np.dtype('uint8'), None, _ir.DataType.UINT8)
uint16 = dtype._factory('uint16', False, True, False, False, np.dtype('uint16'), None, _ir.DataType.UINT16)
uint32 = dtype._factory('uint32', False, True, False, False, np.dtype('uint32'), None, _ir.DataType.UINT32)
uint64 = dtype._factory('uint64', False, True, False, False, np.dtype('uint64'), None, _ir.DataType.UINT64)

# Floating point types
float16 = dtype._factory('float16', False, False, True, True, np.dtype('float16'), None, _ir.DataType.FLOAT16)
float32 = dtype._factory('float32', False, False, True, True, np.dtype('float32'), builtins.float, _ir.DataType.FLOAT)
float64 = dtype._factory('float64', False, False, True, True, np.dtype('float64'), None, _ir.DataType.DOUBLE)

# Complex types
complex64 = dtype._factory('complex64', True, False, False, True, np.dtype('complex64'), None, _ir.DataType.COMPLEX64)
complex128 = dtype._factory('complex128', True, False, False, True, np.dtype('complex128'), None, _ir.DataType.COMPLEX128)
# yapf: enable

# Type aliases
half = float16
float = float32
double = float64

# Delete the `dtype` factory from the `dtype` class.
del dtype._factory

# Must define all statically so language servers can parse import *
__all__ = [
    "dtype", "bool", "int8", "int16", "int32", "int64", "uint8", "uint16",
    "uint32", "uint64", "half", "float16", "float", "float32", "double",
    "float64", "complex64", "complex128"
]
