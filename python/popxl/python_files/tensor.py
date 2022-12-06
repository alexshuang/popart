# Copyright (c) 2021 Graphcore Ltd. All rights reserved.
# Pylint has a False positive on Type as it's used as a type hint encapsulated in a string
# pylint: disable=unused-import
import math
from typing import Any, Dict, Iterable, Optional, Tuple, Type, Union, TYPE_CHECKING
from typing_extensions import Literal
from collections.abc import Mapping
import numpy as np

import popart._internal.ir as _ir
from popxl import dtypes
from popxl.context import (
    gcg,
    gmg,
    debug_context_frame_offset,
    _execution_context,
    get_main_graph,
)
from popxl.typing_ import NewAliasAnnotation
from popxl.errors import UndefinedValue
from popxl.utils import to_numpy
from popxl.replica_grouping import ReplicaGrouping
import popxl

if TYPE_CHECKING:
    from popxl import Ir
    from popxl.remote_buffer import RemoteBuffer

ScalarType = Union[int, float, bool]
"""Scalar types that can be coerced into a Tensor"""

HostTensor = Union[np.ndarray, Iterable[ScalarType]]
"""Container types that can be coerced into a Tensor"""

host_tensor_types = tuple([np.ndarray, Iterable])

try:
    import torch

    HostTensor = Union[HostTensor, torch.Tensor]
    host_tensor_types = tuple([*host_tensor_types, torch.Tensor])
except ModuleNotFoundError:
    pass

HostScalarTensor = Union[ScalarType, HostTensor]
"""Container and scalar types that can be coerced into a Tensor"""

TensorLike = Union["Tensor", HostScalarTensor]
"""Tensors and types that can be coerced into a Tensor"""

TILE_SET_MAP = {
    _ir.TileSet.Compute: "compute",
    _ir.TileSet.IO: "io",
    _ir.TileSet.Undefined: "undefined",
}


class TensorSpec(Mapping):
    def __init__(
        self,
        shape: Tuple[int, ...],
        dtype: dtypes.dtype,
        meta_shape: Tuple[int, ...] = (),
    ):
        """
        Construct a description of a tensor.

        Instances of this class can be used as arguments in `ir.create_graph()` to provide
        a specification of the input tensors.

        Args:
            shape (Tuple[int, ...]): shape of the tensor.
            dtype (dtypes.dtype): data type of the tensor.
            meta_shape (Tuple[int, ...], optional):
                Shape of the full tensor when using replicated tensor sharding. Defaults to ().
        """
        self.shape = shape
        self.dtype = dtype
        self.meta_shape = meta_shape

    def to_dict(self):
        return {"shape": self.shape, "dtype": self.dtype, "meta_shape": self.meta_shape}

    def to_tuple(self):
        return self.shape, self.dtype, self.meta_shape

    def __iter__(self):
        return iter(self.to_dict())

    def __len__(self):
        return len(self.to_dict())

    def __getitem__(self, key):
        return self.to_dict()[key]

    def __hash__(self) -> int:
        return hash(self.to_tuple())

    def __repr__(self) -> str:
        return f"TensorSpec(shape={self.shape}, dtype={self.dtype}, meta_shape={self.meta_shape})"


class Tensor:
    def __init__(self):
        """Representation of a tensor."""
        self._pb_tensor: _ir.Tensor
        raise RuntimeError("popxl.Tensor cannot be constructed directly.")

    # Dictionary to track Tensor subclasses
    _tensor_types: Dict[str, "Type[Tensor]"] = {}

    def __init_subclass__(cls, tensor_type: Optional[str] = None, **kwargs) -> None:
        """
        Construct a subclass.

        Used as a hook which is called when creating a `Tensor` subclass.

        Argument `tensor_type` is used to allow `_from_pb_tensor` to return
        the correct subclass for any Tensor retrieved from the internal IR
        """
        super().__init_subclass__(**kwargs)
        if tensor_type is not None:
            Tensor._tensor_types[tensor_type] = cls

    @classmethod
    def _from_pb_tensor(cls, pb_tensor: _ir.Tensor) -> "Tensor":
        ir = popxl.Ir._from_pb(pb_tensor.getIr())
        id = str(pb_tensor.id)
        if id in ir._tensor_cache:
            return ir._tensor_cache[id]

        specifc_cls = cls._tensor_types.get(
            pb_tensor.tensor_type(), None
        )  # type: ignore
        if specifc_cls is not None and cls != specifc_cls:
            return specifc_cls._from_pb_tensor(pb_tensor)

        self = super().__new__(cls)
        self._pb_tensor = pb_tensor
        ir._tensor_cache[id] = self
        return self

    ## Properties
    @property
    def id(self) -> str:
        """Fully-qualified identifier of the tensor (for example, 'graph1/Gradient___x')."""
        return str(self._pb_tensor.id)

    @property
    def name(self) -> str:
        """Id of the tensor with the graph scope removed (for example, 'Gradient___x')."""
        return _ir.removeScope(self._pb_tensor.getGraph(), self.id)

    @property
    def scope(self) -> str:
        """Graph scope component of the tensor's identifier (for example, 'graph1')."""
        return self._pb_tensor.getGraph().getScope().str()

    @property
    def dtype(self) -> dtypes.dtype:
        return dtypes.dtype.as_dtype(self._pb_tensor.info.dataType())

    @property
    def shape(self) -> Tuple[int, ...]:
        """Return a tuple of the shape of the tensor."""
        return tuple(self._pb_tensor.info.shape())

    @property
    def meta_shape(self) -> Tuple[int, ...]:
        """
        Return the meta shape of the tensor.

        The meta shape of the tensor can be used, for example,
        to store the original tensor shape before
        replicated tensor sharding was applied.
        """
        return tuple(self._pb_tensor.info.metaShape())

    @property
    def spec(self) -> TensorSpec:
        """Return a `TensorSpec` instance using the properties of this tensor."""
        return TensorSpec(self.shape, self.dtype, self.meta_shape)

    @property
    def rank(self) -> int:
        """Return the total number of dimensions in this tensor."""
        return self._pb_tensor.info.rank()

    @property
    def nelms(self) -> int:
        """Return the total number of elements in this tensor."""
        return self._pb_tensor.info.nelms()

    @property
    def location_info(self):
        # TODO T53608: needs clean up. Exposing private object without documentation
        return self._pb_tensor.tensorLocationInfo

    def strides(self, shape=None) -> Tuple[int]:
        """Get the strides of the tensor.

        The strides of the tensor is the number of bytes to step in each
        dimension when traversing an array in memory. See :py:attr:`numpy.ndarray.strides`.

        Returns:
            List[int]: The strides of the tensor.
        """
        if shape is None:
            shape = self.shape
        return tuple(self._pb_tensor.info.strides(shape))

    @property
    @debug_context_frame_offset(1)
    def T(self) -> "Tensor":
        """Return the tensor transposed with reversed axes."""
        return self.transpose()

    @property
    @debug_context_frame_offset(1)
    def T_(self) -> "Tensor":
        """Return the tensor transposed with reversed axes in-place."""
        return self.transpose_()

    @property
    def ipu(self) -> int:
        """
        Return the IPU that the tensor is assigned to.

        Raises:
            UndefinedValue: If the IPU is undefined.
        """
        ipu, _ = self._get_ipu_and_tile_set(
            raise_on_undefined_tile_set=False, raise_on_undefined_ipu=True
        )
        return ipu

    @property
    def tile_set(self) -> Literal["compute", "io"]:
        """
        Return the tile set (`compute` or `io`) that the tensor is assigned to.

        Raises:
            UndefinedValue: If the tile set is undefined.
        """
        _, tile_set = self._get_ipu_and_tile_set(
            raise_on_undefined_tile_set=True, raise_on_undefined_ipu=False
        )
        return tile_set

    @property
    def ir(self) -> "Ir":
        """Return the `Ir` that the tensor is a member of."""
        from popxl import Ir

        return Ir._from_pb(self._pb_tensor.getIr())

    @property
    def in_sync_with_ipu(self) -> bool:
        """
        Check whether the host side buffer data is in sync with the data on the IPU device.

        This only applies to variable tensors which can become out of sync if
        `session.weights_from_host` and `session.weights_to_host` are not called.
        Without a transfer from device to host and visa-versa the buffers and the
        data on the IPU can fall out of sync after either is updated.
        """
        return self._pb_tensor.isInSyncWithIPU()

    ## Methods
    @debug_context_frame_offset(1)
    def transpose(self, permutation: Optional[Iterable[int]] = None) -> "Tensor":
        """
        Permute the axes of a tensor.

        By default this operation reverses the axes of the tensor.

        Args:
            permutation (Optional[Iterable[int]]): Iterable containing the permutation of [0, N-1] where N is the
             rank of the tensor. If not provided, the axes will be reversed.
        Returns:
            Tensor:
                The transposed tensor.
        """
        import popxl.ops as ops

        return ops.transpose(self, permutation)

    @debug_context_frame_offset(1)
    def transpose_(self, permutation: Optional[Iterable[int]] = None) -> "Tensor":
        """
        Permute the axes of a tensor in place.

        By default this operation reverses the axes of the tensor.

        This is the in-place version of :func:`~popxl.Tensor.transpose`.
        The behaviour is the same, but it modifies the
        tensor in place.

        Args:
            permutation (Optional[Tuple[int, ...]]):
                Tuple containing the a permutation of [0, N-1] where N is the
                rank of input `t`. If not provided, the axes will be reversed.
        Returns:
            Tensor:
                The transposed tensor.
        """
        import popxl.ops as ops

        return ops.transpose_(self, permutation)

    @debug_context_frame_offset(1)
    def reshape(self, shape: Iterable[int]) -> "Tensor":
        """Return `ops.reshape(self, shape)`."""
        import popxl.ops as ops

        return ops.reshape(self, shape)

    @debug_context_frame_offset(1)
    def reshape_(self, shape: Iterable[int]) -> "Tensor":
        """Return ops.reshape_(self, shape) inplace."""
        import popxl.ops as ops

        return ops.reshape_(self, shape)

    @debug_context_frame_offset(1)
    def flatten(self) -> "Tensor":
        """Return ops.flatten(self)."""
        import popxl.ops as ops

        return ops.flatten(self)

    @debug_context_frame_offset(1)
    def flatten_(self) -> "Tensor":
        """Return ops.flatten_(self) inplace."""
        import popxl.ops as ops

        return ops.flatten_(self)

    @debug_context_frame_offset(1)
    def detach(self) -> "Tensor":
        """Return the detached tensor."""
        import popxl.ops as ops

        return ops.detach(self)

    @debug_context_frame_offset(1)
    def diag(self) -> "Tensor":
        """Return the diagonal of a 2d tensor.

        Raises:
            ValueError: If the tensor is not 2-dimensional
        """
        if self.rank != 2:
            raise ValueError("Diag expected a 2d tensor.")

        return self.flatten()[:: self.shape[1] + 1]

    @debug_context_frame_offset(1)
    def detach_(self) -> "Tensor":
        """Return this tensor detached inplace."""
        import popxl.ops as ops

        return ops.detach_(self)

    @debug_context_frame_offset(1)
    def copy_to_ipu(self, destination: int, source: Optional[int] = None) -> "Tensor":
        """
        Copy a tensor to an IPU.

        Args:
            destination (int):
                ID of the IPU to copy the tensor to.
            source (Optional[int]):
                ID of the IPU to copy the tensor from.
                By default, the source will be taken from the producer of the tensor.
                If the tensor does not have a producer a source **must** be provided.
        """
        import popxl.ops as ops

        return ops.ipu_copy(self, destination, source)

    ## Private functions
    def _get_ipu_and_tile_set(
        self,
        raise_on_undefined_tile_set: bool = True,
        raise_on_undefined_ipu: bool = True,
    ) -> Tuple[int, Literal["compute", "io", "undefined"]]:
        """
        Determine the IPU and tile set of the tensor.
        Raise:
            UndefinedValue: If either the IPU or the tile are underfined and
            the corresponding flag is set to True.
        """
        ipu, tile_set = self._pb_tensor.getVirtualGraphIdAndTileSetUnsafe()
        tile_set = TILE_SET_MAP[tile_set]
        if raise_on_undefined_tile_set and tile_set == "undefined":
            raise UndefinedValue("Tensor's tile set is undefined.")
        if raise_on_undefined_ipu and ipu == -1:
            raise UndefinedValue("Tensor's IPU is undefined.")
        return ipu, tile_set

    def _ensure_tensor(
        self,
        value: TensorLike,
        dtype: Optional[dtypes.dtype] = None,
        raise_on_empty=True,
    ) -> "Tensor":
        """
        Ensure that all operands are of type `Tensor`.

        If any are not, an attempt is made to convert the operands to a
        constant tensor with the same dtype as `self`.

        Raises:
            ValueError: If the value has 0 elements.

        Returns:
            Tensor:
                A `popxl.Tensor`.
        """
        if isinstance(value, Tensor):
            return value
        else:
            dtype = self.dtype if dtype is None else dtype
            t = constant(value, dtype)
            if raise_on_empty and t.nelms == 0:
                raise ValueError(
                    "The value has 0 elements - this is most likely a mistake. "
                    "If not, initialise the tensor explicitly before using in an operation. For example: `popxl.variable([])`. "
                    f"Type: {type(value)}. Value: {value}"
                )
            return t

    ## Dunders
    def __repr__(self) -> str:
        class_name = type(self).__name__
        return f"{class_name}[{self.id} {self.dtype} {self.shape}]"

    def __hash__(self):
        """Hash the Tensor, based on Tensor and Ir `id`."""
        return hash((self.id, self.ir))

    def __eq__(self, other: Any) -> bool:
        """Return the Tensor equality, based on Tensor and Ir `id`."""
        return isinstance(other, Tensor) and self.id == other.id and self.ir == other.ir

    def __len__(self) -> int:
        """Return the size of the 0th axis or raises a UndefinedValue."""
        if len(self.shape) > 0:
            return self.shape[0]
        else:
            raise ValueError("Tensor is a scalar and doesn't have a length.")

    @debug_context_frame_offset(1)
    def __add__(self, other: TensorLike) -> "Tensor":
        """Return `ops.add(self, other)`."""
        import popxl.ops as ops

        return ops.add(self, self._ensure_tensor(other))

    @debug_context_frame_offset(1)
    def __radd__(self, other: TensorLike) -> "Tensor":
        """Return `ops.add(other, self)`."""
        import popxl.ops as ops

        return ops.add(self._ensure_tensor(other), self)

    @debug_context_frame_offset(1)
    def __iadd__(self, other: TensorLike) -> "Tensor":
        """Return the result of +=.

        Uses ops.add_ to add 'other' inplace on this tensor (on the left hand side, i.e on to this tensor).
        """
        import popxl.ops as ops

        ops.add_(self, self._ensure_tensor(other))
        return self

    @debug_context_frame_offset(1)
    def __sub__(self, other: TensorLike) -> "Tensor":
        """Return `ops.sub(self, other)`."""
        import popxl.ops as ops

        return ops.sub(self, self._ensure_tensor(other))

    @debug_context_frame_offset(1)
    def __rsub__(self, other: TensorLike) -> "Tensor":
        """Return `ops.sub(other, self)`."""
        import popxl.ops as ops

        return ops.sub(self._ensure_tensor(other), self)

    @debug_context_frame_offset(1)
    def __mul__(self, other: TensorLike) -> "Tensor":
        """Return `ops.mul(self, other)`."""
        import popxl.ops as ops

        return ops.mul(self, self._ensure_tensor(other))

    @debug_context_frame_offset(1)
    def __rmul__(self, other: TensorLike) -> "Tensor":
        """Return `ops.mul(other, self)`."""
        import popxl.ops as ops

        return ops.mul(self._ensure_tensor(other), self)

    @debug_context_frame_offset(1)
    def __pow__(self, other: TensorLike) -> "Tensor":
        """Return `ops.pow(self, other)`."""
        import popxl.ops as ops

        return ops.pow(self, other)

    @debug_context_frame_offset(1)
    def __rpow__(self, other: TensorLike) -> "Tensor":
        """Return `ops.pow(other, self)`."""
        import popxl.ops as ops

        return ops.pow(constant(other), self)

    @debug_context_frame_offset(1)
    def __truediv__(self, other: TensorLike) -> "Tensor":
        """Return `ops.div(self, other)`."""
        import popxl.ops as ops

        return ops.div(self, self._ensure_tensor(other))

    @debug_context_frame_offset(1)
    def __rtruediv__(self, other: TensorLike) -> "Tensor":
        """Return `ops.div(other, self)`."""
        import popxl.ops as ops

        return ops.div(self._ensure_tensor(other), self)

    @debug_context_frame_offset(1)
    def __mod__(self, other: TensorLike) -> "Tensor":
        """Return `ops.fmod(self, other)`."""
        import popxl.ops as ops

        return ops.fmod(self, self._ensure_tensor(other))

    @debug_context_frame_offset(1)
    def __rmod__(self, other: TensorLike) -> "Tensor":
        """Return `ops.fmod(other, self)`."""
        import popxl.ops as ops

        return ops.fmod(self._ensure_tensor(other), self)

    @debug_context_frame_offset(1)
    def __neg__(self) -> "Tensor":
        """Return `ops.negate(self)`."""
        import popxl.ops as ops

        return ops.negate(self)

    @debug_context_frame_offset(1)
    def __matmul__(self, other: TensorLike) -> "Tensor":
        """Return `ops.matmul(self, other)`."""
        import popxl.ops as ops

        return ops.matmul(self, self._ensure_tensor(other))

    @debug_context_frame_offset(1)
    def __rmatmul__(self, other: TensorLike) -> "Tensor":
        """Return `ops.matmul(other, self)`."""
        import popxl.ops as ops

        return ops.matmul(self._ensure_tensor(other), self)

    @debug_context_frame_offset(1)
    def __and__(self, other: TensorLike) -> "Tensor":
        """Return `ops.logical_and(self, other)`."""
        import popxl.ops as ops

        return ops.logical_and(self, self._ensure_tensor(other))

    @debug_context_frame_offset(1)
    def __rand__(self, other: TensorLike) -> "Tensor":
        """Return `ops.logical_and(other, self)`."""
        import popxl.ops as ops

        return ops.logical_and(self._ensure_tensor(other), self)

    @debug_context_frame_offset(1)
    def __or__(self, other: TensorLike) -> "Tensor":
        """Return `ops.logical_or(self, other)`."""
        import popxl.ops as ops

        return ops.logical_or(self, self._ensure_tensor(other))

    @debug_context_frame_offset(1)
    def __ror__(self, other: TensorLike) -> "Tensor":
        """Return `ops.logical_or(other, self)`."""
        import popxl.ops as ops

        return ops.logical_or(self._ensure_tensor(other), self)

    @debug_context_frame_offset(1)
    def __invert__(self) -> "Tensor":
        """Return `ops.logical_not(self)`."""
        import popxl.ops as ops

        return ops.logical_not(self)

    def __getitem__(
        self,
        key: Union[int, slice, Tuple[Union[int, slice], ...], "Tensor", HostTensor],
    ) -> "Tensor":
        """
        Support for slicing, integer and boolean indexing.

        Tensors or host tensors (NumPy/PyTorch arrays and sequences) will be converted to a constant
        Tensor and can be used for integer or boolean indexing.

        Slicing is triggered when the input is an integer, slice (for example, `0:2`) or a tuple of the two. Slicing
        either selects a single index of an axis using an integer or range using a slice. If a single index
        is selected the dimension will be squeezed - this matches NumPy slicing rules.

        Integer indexing is triggered when the input is a tensor or host tensor of integers.
        Elements are selected using the indices in the input - see :py:func:`~popxl.ops.gather` for details.

        Boolean indexing is triggered when the input is a tensor or host tensor of booleans.
        The input is interpreted as a mask: True propagates the value to the output while False zeroes
        the element. This differs from NumPy-style boolean indexing because NumPy *removes* elements indicated
        by False and so the output shape is dynamic, depending on the mask's data.

        Examples:

        .. code-block:: python

            # Slicing
            x[
                0
            ]  # Select all elements where i==0 for axis 0. The output will not include the 0th axis (squeezed)
            x[0, 1]  # Select all elements where i==0, j==1 for axis 0 and 1
            x[0:2]  # Slice axis 0 between index 0 and 2
            x[:2, 3:]  # Slice axis 0 upto 2 and axis 1 from index 3
            x[:, ::-1]  # Select all elements for axis 0 and reverse axis 1

            # Integer indexing
            indices = popxl.variable([0, 2], dtype=popxl.int32)
            x[indices] == Tensor([x[0], x[2]])  # Select elements [0, 2] from `x`

            # Boolean indexing
            x.shape == (3, 1)
            mask = popxl.variable([True, False, True], dtype=popxl.bool)
            x[mask] == Tensor([x[0], 0, x[1]])  # Keep elements 0 and 2. Zero element 1.

            x.shape == (3, 2)
            mask = popxl.variable([True, False, True], dtype=popxl.bool)
            x[mask] == Tensor([x[0], 0, x[1]])  # Broadcast mask: zero row 1

        """

        import popxl.ops as ops

        if isinstance(key, (bool, str)):
            pass  # will raise error at end of function

        elif isinstance(key, (slice, int)) or (
            isinstance(key, tuple) and all(isinstance(e, (slice, int)) for e in key)
        ):
            # Basic slicing (integer or slices)
            key = (key,) if isinstance(key, (slice, int)) else key

            start = []
            stop = []
            slice_step = []
            subsample_step = []
            int_slices = []

            for i, key_i in enumerate(key):
                if isinstance(key_i, int):
                    start += [key_i]
                    stop += [key_i + 1]
                    slice_step += [1]
                    subsample_step += [1]
                    int_slices += [i]

                elif isinstance(key_i, slice):
                    start += [key_i.start]
                    stop += [key_i.stop]
                    # Step sent to slice must always be one of {-1, 1}
                    # To handle abs(step) > 1, we'll slice first, then subsample
                    # To do this, clamp the step in the range {-1, 1}
                    slice_step += [
                        None if key_i.step is None else max(-1, min(1, key_i.step))
                    ]
                    # Because the slice with negative step reverses direction already, we need abs(step) for the subsample
                    subsample_step += [1 if key_i.step is None else abs(key_i.step)]

            out = ops.slice(self, start, stop, slice_step)

            if any(x != 1 for x in subsample_step):
                out = ops.subsample(out, subsample_step)

            if len(int_slices) > 0:
                out = ops.squeeze(out, axes=int_slices)

            return out

        # Don't capture scalars
        elif isinstance(key, Tensor) or isinstance(key, tuple(host_tensor_types)):
            if not isinstance(key, Tensor):
                key = constant(key)

            if key.dtype.is_int:
                # Integer indexing
                return ops.gather(self, key)
            elif key.dtype == dtypes.bool:
                # Boolean indexing
                zero = constant(0, dtype=self.dtype)
                return ops.where(condition=key, lhs=self, rhs=zero)

        raise TypeError(
            "Only integers, slices (`:`), integer tensors and boolean tensors are valid indices. "
            f"Not a valid Type: {type(key)}. Value: {key}."
        )

    # Prevents fallback of __iter__ and __contains__ to __getitem__
    # which can produce unhelpful errors
    __iter__ = None
    __contains__ = None

    # Prevents numpy from calling its dunder methods when we want to
    # use our reflected dunder methods. e.g. np.ndarray(...) @ popxl.Tensor(...)
    __array_ufunc__ = None


class Variable(Tensor, tensor_type="Variable"):
    """
    A variable tensor.
    This tensor can be used to represent a model weight or any other
    parameter that can change while running a model.
    """

    def __init__(self):
        super().__init__()
        self._replica_grouping: ReplicaGrouping
        # Has a .base that is an np.memmap
        self._memmap_arr: Optional[np.memmap]

    @debug_context_frame_offset(1)
    def copy_to_ipu(self, dst: int, src: int) -> "Tensor":
        """
        Return ``ops.ipu_copy(self, dst, src)``.

        Must provide a src value.
        """
        import popxl.ops as ops

        return ops.ipu_copy(self, dst, src)

    @property
    def replica_grouping(self) -> ReplicaGrouping:
        """Return the ReplicaGrouping settings for this tensor.

        Returns:
            ReplicaGrouping: The ReplicaGrouping object, if set.
        """
        return self._replica_grouping

    @property
    def retrieval_mode(self) -> Literal["one_per_group", "all_replicas"]:
        """Return the string representation of the retrieval mode.

        One of:

            - "one_per_group": Return only the first replica's variable per group.
            - "all_replicas": Return all replica's variables in every group.

        Raises:
            ValueError: If an unsupported VariableRetrievalMode is present on the popart tensor.

        Returns:
            Literal["one_per_group", "all_replicas"]: The string representing the retieval_mode.
        """
        import popart

        if (
            self._pb_tensor.getVariableSettings().getRetrievalMode()
            == popart.VariableRetrievalMode.OnePerGroup
        ):
            return "one_per_group"
        elif (
            self._pb_tensor.getVariableSettings().getRetrievalMode()
            == popart.VariableRetrievalMode.AllReplicas
        ):
            return "all_replicas"
        else:
            raise ValueError(
                f"Unsupported VariableRetrievalMode :{self._pb_tensor.getVariableSettings().getRetrievalMode()}"
            )

    @property
    def shape_on_replica(self):
        """Return the reduced shape on an individual replica.

        The full shape on host may have an outer group_num dimension on the host, depending on the
        replica_grouping argument. This function takes the full shape and removes the outer
        dimension safely (ie. checks if the outer dimension matches an expected
        outer dimension).
        """
        return tuple(
            self._pb_tensor.getVariableSettings().shapeOnReplica(
                self.shape_on_host, self.ir.replication_factor, self.name
            )
        )

    @property
    def shape_on_host(self):
        """Return the full tensor shape on the host.

        The full shape on host may have an outer group_num dimension on the host, depending on the
        replica_grouping argument. This function takes the reduced on-replica shape and adds the outer
        dimension safely (ie. checks if the outer dimension matches an expected
        outer dimension).
        """
        num_groups = self.replica_grouping.num_groups
        if num_groups == 1:
            return self.shape
        else:
            return tuple([num_groups, *self.shape])

    def _flush_data_if_memmap(self):
        if self._memmap_arr is not None:
            assert isinstance(self._memmap_arr, np.memmap)
            self._memmap_arr.flush()


class Constant(Tensor, tensor_type="Const"):
    """
    A constant tensor.
    This tensor cannot change during the runtime of a model.
    """

    @debug_context_frame_offset(1)
    def copy_to_ipu(self, dst: int, src: int) -> "Tensor":
        """
        Return ``ops.ipu_copy(self, dst, src)``.

        Must provide a src value.
        """
        import popxl.ops as ops

        return ops.ipu_copy(self, dst, src)


def variable(
    data: HostScalarTensor,
    dtype: Optional[dtypes.dtype] = None,
    name: Optional[str] = None,
    downcast: bool = True,
    replica_grouping: Optional[ReplicaGrouping] = None,
    retrieval_mode: Optional[Literal["one_per_group", "all_replicas"]] = None,
    log2_scale: int = None,
    nan_on_overflow: bool = None,
) -> Variable:
    """
    Create a variable tensor that is initialised with data during graph creation.

    This tensor can be used to represent a model weight or any other
    parameter that can change while running a model.

    Must be created in the main graph scope. Example:

    .. code-block:: python

        import popxl

        with popxl.Ir().main_graph:
            a = popxl.variable(0)

    To optimise the host memory used by compilation/runtime, you can pass an
    `np.memmap` as the `data` parameter.

    Note, if you do this, PopXL will not internally copy `data` into a buffer it
    solely owns, but instead takes joint ownership of the object you passed in.
    This means it is up to you to not clobber the contents of `data`. Letting it
    go out of scope is ok, because PopXL maintains a reference to it.

    Sometimes, PopXL has to internally make a copy of `data` into a buffer with
    a layout and dtype that it can handle natively. Doing this on an `np.memmap`
    would defeat the point of the memory-mapping. Consequently, if `data` is an
    `np.memmap`, in order to avoid this, ALL of the following conditions
    must hold, or an error is thrown.

      - The `data` array must be a C-array
      - No downcasting should be required to a dtype natively supported by PopXL
      - The `dtype` parameter must be `None` or exactly the same as
        `data.dtype`

    Furthermore, the implementation of non-const replica groupings requires
    making copies of various slices within `data`. Therefore, if you pass a
    non-const replica grouping with an `np.memmap`, you will get a warning. See
    :py:meth:`popxl.Ir.replica_grouping_from_assignment` for how to create such
    groupings.

    Args:
        data:
            The data used to initialise the tensor.
            This can be an np.ndarray, or a value NumPy can use to construct an
            np.ndarray.
            This can also be an np.memmap.
        dtype:
            The data type of the tensor to be created. If not specified NumPy
            will infer the data type and downcast to 32 bits if necessary. For
            float8 dtypes automatic inference of dtype is not currently
            possible, please explicitly specify the dtype.
        name:
            The name of the tensor. Defaults to `None`.
        downcast:
            If True and no dtype is provided, 64-bit float/ints will be downcast
            to 32-bit variants. Defaults to True.
        replica_grouping:
            The grouping of replicas to use when getting and setting variable
            values. Generally it makes sense to group replicas together that
            are guaranteed to agree on value based on the collective operations
            you add to the IR. Replicas within a group are always initialised
            with a shared value and by default, when retrieving values from
            replicas, only one value is returned per group. By default all
            replicas are in one group.
        retrieval_mode:
            One of:

            - "one_per_group": Return only the first replica's variable per group.
            - "all_replicas": Return all replica's variables in every group.

            Defaults to None.
        log2_scale:
            If dtype is either popxl.float8_143 or popxl.float8_152 then
            multiply the incoming data by pow2(log2_scale) before casting.
        nan_on_overflow:
            If dtype is either popxl.float8_143 or popxl.float8_152 and this
            flag is set then replace values that cannot be represented by the
            requested dtype with np.nan values.

    Raises:
        RuntimeError: If a non-default replica group is used
        ValueError: If the tensor is tried initialised within a graph
        ValueError: If the `data` parameter is a `np.memmap` and any of the
        following is true:

        - It is not a C-array,
        - It requires downcasting to a dtype natively supported by PopXL,
        - The `dtype` parameter is not `None` and conflicts with `data.dtype`.

    Returns:
        Variable: The desired variable.
    """
    g = gcg()
    pb_g = g._pb_graph

    if g != gmg():
        raise ValueError(
            "You cannot initialise a variable tensor within a graph. "
            "It can only be initialised within the main graph. "
            "Please create a graph input for this variable (`popxl.graph_input`) "
            "or use a constant tensor (`popxl.constant`). "
            "See popxl user guide for more details."
        )

    is_mmap = isinstance(data, np.memmap)

    # `addVarInit` will copy the data so it's not required here
    np_data = to_numpy(
        data,
        dtype,
        downcast,
        copy=False,
        log2_scale=log2_scale,
        nan_on_overflow=nan_on_overflow,
    )
    popxl_dt = dtypes.dtype.as_dtype(np_data)

    pb_id = g._create_tensor_id(name)

    replica_grouping = replica_grouping if replica_grouping else g.ir.replica_grouping()

    pb_rg = replica_grouping
    if not replica_grouping.is_const:
        if is_mmap:
            import popart

            popart.getLogger().warn(
                f"In creation of variable {pb_id}, you have passed np.memmap "
                f"data and the non-const replica grouping `{pb_rg}`, which "
                "requires us to make a copy of your np.memmap data."
            )
        np_data = np_data[replica_grouping.to_device_map]
        pb_rg = replica_grouping.const_rg

    info = _ir.TensorInfo(popxl_dt._pb_dtype, np_data.shape)

    pb_g.addVarInit(
        pb_id,
        info,
        np_data,
        pb_rg._to_variable_settings(retrieval_mode),
        copyData=not is_mmap,
    )

    var = Variable._from_pb_tensor(pb_g.getTensor(pb_id))
    var._replica_grouping = replica_grouping
    var._memmap_arr = np_data if is_mmap else None

    return var


def remote_variable(
    data: HostScalarTensor,
    remote_buffer: "RemoteBuffer",
    offset: int = 0,
    dtype: Optional[dtypes.dtype] = None,
    name: Optional[str] = None,
    downcast: bool = True,
    replica_grouping: Optional[ReplicaGrouping] = None,
    retrieval_mode: Optional[
        Literal["one_per_group", "all_replicas"]
    ] = "one_per_group",
    log2_scale: int = None,
    nan_on_overflow: bool = None,
) -> Variable:
    """Create a variable Tensor that is stored in remote memory.

    Args:
        data:
            The data used to initialise the tensor.
            This can be an np.ndarray, or a value numpy can use to construct an np.ndarray.
            This can also be an np.memmap, see :py:func:`~popxl.Variable`.
        remote_buffer:
            The remote buffer to store the variable.
        offset:
            The offset into the entries of the remote buffer to store the variable. Defaults to 0
        dtype:
            The data type of the tensor to be created, if not specified Numpy
            will infer the data type and be downcasted to 32 bits if necessary.
            For float8 dtypes automatic inference of dtype is not currently
            possible, please explicitly specify the dtype.
        name:
            The name of the tensor. Defaults to `None`.
        downcast:
            If no dtype is provided 64 bit float/ints will be downcasted to 32 bit variants. Default to True.
        replica_grouping:
            The grouping of replicas to use when getting and setting variable
            values. Generally it makes sense to group replicas together that
            are guaranteed to agree on value based on the collective operations
            you add to the IR. Replicas within a group are always initialised
            with a shared value and by default, when retrieving values from
            replicas, only one value is returned per group. By default all
            replicas are in one group.
        retrieval_mode:
            One of:

            - "one_per_group": Return only the first replica's variable per group, this is the
              default behaviour.
            - "all_replicas": Return all replica's variables in every group.

            Defaults to "one_per_group".
        log2_scale:
            If dtype is either popxl.float8_143 or popxl.float8_152 then
            multiply the incoming data by pow2(log2_scale) before casting.
        nan_on_overflow:
            If dtype is either popxl.float8_143 or popxl.float8_152 and this
            flag is set then replace values that cannot be represented by the
            requested dtype with np.nan values.

    Raises:
        RuntimeError: If a non-default replica group is used.
        ValueError: If the variable shape or dtype does not match remote buffer's.
        ValueError: If the `data` parameter is a `np.memmap` and any of the
        following is true:

        - It is not a C-array,
        - It requires downcasting to a dtype natively supported by PopXL,
        - The `dtype` parameter is not `None` and conflicts with `data.dtype`.

    Returns:
        Variable: The remote variable.
    """

    var = variable(
        data,
        dtype,
        name,
        downcast,
        replica_grouping,
        retrieval_mode,
        log2_scale=log2_scale,
        nan_on_overflow=nan_on_overflow,
    )

    var._pb_tensor.setTensorLocationInfo(
        _ir.TensorLocation(_ir.TensorStorage.OffChip, _ir.ReplicatedTensorSharding.Off),
        remote_buffer.remote_buffer_id,
        offset,
    )
    return var


def remote_replica_sharded_variable(
    data: HostScalarTensor,
    remote_buffer: "RemoteBuffer",
    offset: int = 0,
    dtype: Optional[dtypes.dtype] = None,
    name: Optional[str] = None,
    downcast: bool = True,
    replica_grouping: Optional[ReplicaGrouping] = None,
    retrieval_mode: Optional[
        Literal["one_per_group", "all_replicas"]
    ] = "one_per_group",
    log2_scale: int = None,
    nan_on_overflow: bool = None,
) -> Variable:
    """Create a variable Tensor that is stored in remote memory.
       The variable is scattered in equal shards across replicas (replicated tensor sharding (RTS)
       data parallelism) of the same model/graph. Eliminates redundant data storage when the full
       (un-sharded) tensor does not need to be present on each IPU. Stores the full tensor in remote
       memory (usually DDR memory).

       Replicated tensors for which each replica needs a full copy, need to be recombined with a
       replicated AllGather operation.

       Fully updated tensors that need to be sharded and/or reduced again require a replicated
       ReduceScatter operation.

    Args:
        data:
            The data used to initialise the tensor.
            This can be an np.ndarray, or a value numpy can use to construct an np.ndarray.
            This can also be an np.memmap, see :py:func:`~popxl.Variable`.
        remote_buffer (RemoteBuffer): The handle to the remote buffer.
        offset (int): The offset to index the tensor shard in the remote tensor.
        dtype:
            The data type of the tensor to be created, if not specified Numpy will infer the data
            type and be downcasted to 32 bits if necessary.
        name:
            The name of the tensor. Defaults to `None`.
        downcast:
            If no dtype is provided 64 bit float/ints will be downcasted to 32 bit variants. Default to True.
        replica_grouping:
            The grouping of replicas to use when getting and setting variable
            values. Generally it makes sense to group replicas together that
            are guaranteed to agree on value based on the collective operations
            you add to the IR. Replicas within a group are always initialised
            with a shared value and by default, when retrieving values from
            replicas, only one value is returned per group. By default all
            replicas are in one group.
        retrieval_mode:
            One of:

            - "one_per_group": Return only the first replica's variable per group, this is the
              default behaviour.
            - "all_replicas": Return all replica's variables in every group.

            Defaults to "one_per_group".
        log2_scale:
            If dtype is either popxl.float8_143 or popxl.float8_152 then
            multiply the incoming data by pow2(log2_scale) before casting.
        nan_on_overflow:
            If dtype is either popxl.float8_143 or popxl.float8_152 and this
            flag is set then replace values that cannot be represented by the
            requested dtype with np.nan values.

    Raises:
        RuntimeError: If a non-default replica group is used.
        ValueError: If replication has not been enabled.
        ValueError: If the number of elements of `var` is not divisible by the number of replicas.
        ValueError: if the variable shape or dtype does not match remote buffer's
        ValueError: If the `data` parameter is a `np.memmap` and any of the
        following is true:

        - It is not a C-array,
        - It requires downcasting to a dtype natively supported by PopXL,
        - The `dtype` parameter is not `None` and conflicts with `data.dtype`.

    Returns:
        Variable: The remote sharded variable.
    """

    rf = gcg().ir.replication_factor
    if rf <= 1:
        raise ValueError(
            f"The IR's global replication factor is {rf}. It must "
            "be greater than 1 to create a replica sharded variable."
        )

    # Set the meta_shape for the RemoteBuffer, this will be required later in ops.remote_load
    np_data: np.ndarray = to_numpy(
        data,
        dtype,
        downcast,
        copy=False,
        log2_scale=log2_scale,
        nan_on_overflow=nan_on_overflow,
    )
    if replica_grouping and replica_grouping.num_groups > 1:
        required_shape = np_data.shape[1:]
    else:
        required_shape = np_data.shape
    if remote_buffer.meta_shape == ():
        remote_buffer.meta_shape = required_shape
    elif remote_buffer.meta_shape != required_shape:
        raise ValueError(
            f"Cannot use RemoteBuffer[id={remote_buffer.remote_buffer_id}] for replica sharded variable of shape {required_shape}. "
            f"The buffer's meta_shape has already been set to: {remote_buffer.meta_shape}."
        )

    var = remote_variable(
        np_data,
        remote_buffer,
        offset,
        dtype,
        name,
        downcast,
        replica_grouping,
        retrieval_mode,
    )

    # Note: shardingDomain is not required to be set.
    # It is only used by the StreamingMemoryOpInserter transform.
    tensor_location = _ir.TensorLocation(
        _ir.TensorStorage.OffChip, _ir.ReplicatedTensorSharding.On
    )

    var._pb_tensor.setTensorLocationInfo(
        tensor_location, remote_buffer.remote_buffer_id, offset
    )
    return var


def replica_sharded_buffer(
    shape: Tuple[int, ...],
    dtype: dtypes.dtype,
    replica_grouping: Optional[ReplicaGrouping] = None,
    shard_over: Optional[int] = None,
    entries: int = 1,
):
    """Create a RemoteBuffer for use with replicated tensor sharded variables.

    The tensor_shape and meta_shape properties of the returned RemoteBuffer will
    be a flattened one-dimensional shape. This is because the data of sharded
    tensors in PopXL reside in CBR-rearranged form. This means the original
    ordering of the data you provide is not preserved inside the RemoteBuffer,
    and so the original axes are meaningless.

    Args:
        shape (Tuple[int, ...]): Shape of the variable tensor (including any replica grouping dimensions).
        dtype (dtypes.dtype): Dtype of the variable tensor.
        replica_grouping (Optional[ReplicaGrouping], optional): ReplicaGrouping of the variable tensor. Defaults to All replicas.
        shard_over (Optional[int], optional):
            The number of replicas in each replica group to shard over. Defaults
            to all replicas in the group. Note, when there are multiple
            instances, this group can span instances.
            If the replica grouping size is 4, and shard_over is 4, the value of
            the variable for each group is sharded over all 4 replicas in that
            group.
            If the replica grouping size is 4, and shard_over is 2, the value of
            each group will be sharded once over group members 0 and 1, and
            once over group members 2 and 3.
            The replica grouping size must be divisible by shard_over.
        entries (int): Number of entries in the RemoteBuffer.

    Raises:
        ValueError: If replica_grouping is not None and shard_grouping.stride != the replica_grouping.stride
        ValueError: If replica_grouping is not None and shard_grouping.group_size <!= the replica_grouping.stride
        ValueError: If replica_grouping is None and shard_grouping.stride != 1 with the default replica_grouping
        ValueError: If replica_grouping is None and shard_grouping.group_size <!= the replication_factor

    Returns:
        RemoteBuffer
    """
    from popxl.remote_buffer import remote_buffer

    g = gcg()
    replica_grouping = replica_grouping if replica_grouping else g.ir.replica_grouping()
    replica_grouping = replica_grouping.const_rg

    replica_group_size = (
        replica_grouping.group_size
        if replica_grouping is not None
        else gcg().ir.replication_factor
    )

    # Default shard_group_size to the replica group size
    shard_over = shard_over if shard_over is not None else replica_group_size

    if shard_over > replica_group_size:
        raise ValueError(
            f"The sharding group size ({shard_over}) must be less than "
            f"or equal to the replica group size ({replica_group_size}). Note "
            "that if no replica grouping was passed, the replica group size "
            "defaults to the IR's global replication factor; and if no "
            "sharding group size was passed, it defaults to the replica group "
            "size."
        )
    if replica_group_size % shard_over != 0:
        raise ValueError(
            f"The replica group size ({replica_group_size}) must be divisible "
            f"by the sharding group size ({shard_over}). Note that if no"
            "replica grouping was passed, the replica group size defaults to "
            "the IR's global replication factor; and if no sharding group size "
            "was passed, it defaults to the replica group size."
        )

    def integer_ceil_div(a: int, b: int) -> int:
        return -(a // -b)

    # If replica_grouping, the outermost dim of the actual tensor shape is 1, as
    # dim 0 is the number of groups.
    outer_shape_dim = (
        1 if replica_grouping is not None and replica_grouping.num_groups > 1 else 0
    )
    tensor_shape = shape[outer_shape_dim:]

    # We construct the RemoteBuffer using the sharded shape. The shape passed to
    # RemoteBuffer must have a flattened shape. Therefore, the shape we pass is
    # a singleton dimension of nelms/shard_group_size.
    nelms = np.prod(tensor_shape)
    sharded_shape = (integer_ceil_div(nelms, shard_over),)

    buffer = remote_buffer(sharded_shape, dtype, entries)

    # Set the meta_shape. This is the unsharded tensor shape (so not including
    # the groups dimension, if it exists).
    buffer.meta_shape = tensor_shape

    return buffer


def replica_sharded_variable(
    data: HostScalarTensor,
    dtype: Optional[dtypes.dtype] = None,
    name: Optional[str] = None,
    downcast: bool = True,
    replica_grouping: Optional[ReplicaGrouping] = None,
    shard_over: Optional[int] = None,
    retrieval_mode: Optional[
        Literal["one_per_group", "all_replicas"]
    ] = "one_per_group",
    log2_scale: int = None,
    nan_on_overflow: bool = None,
) -> Tuple[Variable, Tensor]:
    """
    Scatter a tensor in equal shards across replicas (data parallelism) of the same model/graph.

    Eliminates redundant data storage when the full (un-sharded) tensor does not need to be
    present on each IPU. Does not store the full tensor in remote memory.

    Args:
        data:
            The data used to initialise the tensor.
            This can be an np.ndarray, or a value numpy can use to construct an np.ndarray.
            This can also be an np.memmap, see :py:func:`~popxl.Variable`.
        dtype:
            The data type of the tensor to be created, if not specified Numpy
            will infer the data type and be downcasted to 32 bits if necessary.
            For float8 dtypes automatic inference of dtype is not currently
            possible, please explicitly specify the dtype.
        name:
            The name of the tensor. Defaults to `None`.
        downcast:
            If no dtype is provided 64 bit float/ints will be downcasted to 32 bit variants. Default to True.
        replica_grouping:
            The grouping of replicas to use when getting and setting variable
            values. Generally it makes sense to group replicas together that
            are guaranteed to agree on value based on the collective operations
            you add to the IR. Replicas within a group are always initialised
            with a shared value and by default, when retrieving values from
            replicas, only one value is returned per group. By default all
            replicas are in one group.
        shard_over:
            The number of replicas in each replica group to shard over. Defaults
            to all replicas in the group. Note, when there are multiple
            instances, this group can span instances.
            If the replica grouping size is 4, and shard_over is 4, the value of
            the variable for each group is sharded over all 4 replicas in that
            group.
            If the replica grouping size is 4, and shard_over is 2, the value of
            each group will be sharded once over group members 0 and 1, and
            once over group members 2 and 3.
            The replica grouping size must be divisible by shard_over.
        retrieval_mode (Optional[Literal["one_per_group", "all_replicas"]]):
            One of:

            - "one_per_group": Return only the first replica's variable per group, this is the
              default behaviour.
            - "all_replicas": Return all replica's variables in every group.

            Defaults to "one_per_group".
        log2_scale:
            If dtype is either popxl.float8_143 or popxl.float8_152 then
            multiply the incoming data by pow2(log2_scale) before casting.
        nan_on_overflow:
            If dtype is either popxl.float8_143 or popxl.float8_152 and this
            flag is set then replace values that cannot be represented by the
            requested dtype with np.nan values.

    Raises:
        ValueError: If the `data` parameter is a `np.memmap` and any of the
        following is true:

        - It is not a C-array,
        - It requires downcasting to a dtype natively supported by PopXL,
        - The `dtype` parameter is not `None` and conflicts with `data.dtype`.

    Returns:
        Tuple[Variable, Tensor]:
            A tuple of tensors:

            1. The full variable. This should NOT be used directly. It can be used to interact with Session's get/set data methods
            2. The sharded variable.
    """

    import popxl.ops as ops

    data = to_numpy(
        data,
        dtype,
        downcast,
        copy=False,
        log2_scale=log2_scale,
        nan_on_overflow=nan_on_overflow,
    )

    # Infer a dtype for `replica_sharded_buffer`.
    if dtype is not None:
        _dtype = dtype
    elif hasattr(data, "dtype"):
        _dtype = dtypes.dtype.as_dtype(data.dtype)
    else:
        try:
            _dtype = dtypes.dtype.as_dtype(data)
        except ValueError:
            _dtype = None

    buffer = replica_sharded_buffer(data.shape, _dtype, replica_grouping, shard_over)

    # Create a remote RTS variable
    # NOTE: We pass the original `dtype`, not `_dtype`, propagating exactly what
    #       the user intended.
    var = remote_replica_sharded_variable(
        data, buffer, 0, dtype, name, downcast, replica_grouping, retrieval_mode
    )

    # Load/Store the variable in the WeightsFromHost/WeightsToHost programs.
    with get_main_graph():
        with _execution_context(_ir.ExecutionContext.WeightsFromHostFragment):
            var_shard = ops.remote_load(buffer, 0, var.name + "_rts")

        with _execution_context(_ir.ExecutionContext.WeightsToHostFragment):
            ops.remote_store(buffer, 0, var_shard)

    return var, var_shard


def constant(
    data: HostScalarTensor,
    dtype: Optional[dtypes.dtype] = None,
    name: Optional[str] = None,
    downcast: bool = True,
    log2_scale: int = None,
    nan_on_overflow: bool = None,
) -> Constant:
    """
    Return a constant tensor.

    A constant tensor that is initialised with data during graph creation.

    This tensor cannot change during the runtime of a model. The intended use
    of this class is when doing operations between `popxl.Tensor`
    instances and other types, such as `numpy.ndarray` objects, numbers, or
    list or tuples of numbers.

    Example:

    .. code-block:: python

        import popxl

        ir = popxl.Ir()
        with ir.main_graph:
            a = popxl.constant(0)
            # The `1` will be implicitly converted to a `Constant`.
            b = a + 1

    Args:
        data:
            The data used to initialise the tensor.
            This can be an np.array, or a value numpy can use to construct an np.ndarray.
        dtype:
            The data type of the tensor to be created. If not specified, NumPy will infer the data
            type and downcast to 32 bits if necessary. For float8 dtypes automatic
            inference of dtype is not currently possible, please explicitly specify the dtype.
        name:
            The name of the tensor. Defaults to `None`.
        downcast:
            Whether or not to downcast the data to the given dtype. For
            float8 dtypes automatic inference of dtype is not currently
            possible, please explicitly specify the dtype.
        log2_scale:
            The user's data is multiplied by `pow2(log2Scale)` before casting.
            Only applicable when using float8 data types.
        nan_on_overflow:
            If True produce NaN when the input values exceed the numeric range of the
            destination type selected. If False saturate the results. Only applicable
            when using float8 data types.

    Raises:
        TypeError: If a float8 tensor is passed without a corresponding dtype.

    Returns:
        Tensor: The constant required.
    """
    g = gcg()
    pb_g = g._pb_graph

    if isinstance(data, np.ndarray) and (
        data.dtype == dtypes.np_dtype_float8_143
        or data.dtype == dtypes.np_dtype_float8_152
    ):
        # You must explicitly set the data type when creating a float8 constant.
        if dtype is None:
            raise TypeError(
                f"Please explicitly set the data type when calling {__name__}"
            )

    # `addConstInit` will copy the data so it's not required here
    np_data = to_numpy(
        data,
        dtype,
        downcast,
        copy=False,
        log2_scale=log2_scale,
        nan_on_overflow=nan_on_overflow,
    )
    popxl_dt = dtypes.dtype.as_dtype(np_data)
    info = _ir.TensorInfo(popxl_dt._pb_dtype, np_data.shape)
    pb_id = g._create_tensor_id(name)
    pb_g.addConstInit(pb_id, info, np_data)
    return Constant._from_pb_tensor(pb_g.getTensor(pb_id))


def graph_input(
    shape: Iterable[int],
    dtype: dtypes.dtype,
    name: Optional[str] = None,
    by_ref: bool = False,
    meta_shape: Optional[Iterable[int]] = None,
) -> Tensor:
    """
    Create a new input tensor to the current graph.

    You can use this function when defining a graph to create a new input
    tensor. When you call that graph, you will have to pass a tensor to the
    graph for this input.

    Example:

    .. code-block:: python

        import popxl


        def add_w(x):
            w = popxl.graph_input(x.shape, x.dtype, "w")
            return w + x


        ir = popxl.Ir()
        with ir.main_graph:
            w = popxl.variable(1)
            x = popxl.variable(3)
            add_w_graph = ir.create_graph(add_w, x, w)
            (y,) = ops.call(add_w_graph, x, w)

    Args:
        shape (Iterable[int]):
            The shape of the tensor.
        dtype (dtype):
            The data type of the tensor.
        name (Optional[str]):
            The name of the tensor.
        by_ref (bool):
            If the tensor should be added by reference
        meta_shape (Optional[Iterable[int]]):
            The meta shape of the tensor.

    Returns:
        Tensor:
            The created input tensor.
    """
    g = gcg()
    pb_g = g._pb_graph

    pb_id = g._create_tensor_id(name)
    if meta_shape:
        pb_info = _ir.TensorInfo(dtype._pb_dtype, list(shape), list(meta_shape))
    else:
        pb_info = _ir.TensorInfo(dtype._pb_dtype, list(shape))

    pb_g.addInput(pb_id, pb_info)

    t = Tensor._from_pb_tensor(pb_g.getTensor(pb_id))

    if by_ref:
        g._by_ref_inputs.add(t)

    return t


def graph_output(t: Tensor) -> None:
    """
    Mark a tensor as an output in the current graph.

    You can use this function when defining a graph to mark an existing
    tensor in the graph as an output. When you call that graph, it will
    return that tensor in the parent graph.

    Example:

    .. code-block:: python

        import popxl


        def add_w(x):
            w = popxl.graph_input(x.shape, x.dtype, "w")
            y = w + x
            popxl.graph_output(y)


        ir = popxl.Ir()
        with ir.main_graph:
            w = popxl.variable(1)
            x = popxl.variable(3)
            add_w_graph = ir.create_graph(add_w, x, w)
            (y,) = ops.call(add_w_graph, x, w)

    Args:
        t (Tensor):
            The graph tensor to mark as an output in the current graph.

    Raises:
        ValueError: If the tensor is not in the current graph.
    """
    g = gcg()
    pb_g = g._pb_graph

    from popxl.ops.utils import check_in_graph

    check_in_graph(g, t=t)

    pb_g.markAsOutput(t.id)


"""TensorByRef
This type alias can be used in function argument annotations to specify that
a graph input should be flagged as copy-modified. Example:

.. code-block:: python

    def increment(a: TensorByRef):
        ops.var_update.accumulate(a, popxl.constant(1))

When converted to a graph and called, the modification to the graph input `a` will be propagated to the
corresponding input tensor at the callsite.
This is the same as using `popxl.graph_input(..., by_ref=True)`.
"""
TensorByRef = NewAliasAnnotation("TensorByRef", Tensor)
