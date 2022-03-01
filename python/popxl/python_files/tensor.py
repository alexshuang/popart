# Copyright (c) 2021 Graphcore Ltd. All rights reserved.
# Pylint has a False positive on Type as it's used as a type hint encapsulated in a string
# pylint: disable=unused-import
from typing import Any, Dict, Iterable, Optional, Tuple, Type, Union, TYPE_CHECKING
from typing_extensions import Literal
from collections.abc import Mapping
import numpy as np

import popart._internal.ir as _ir
from popxl import dtypes
from popxl.context import gcg, gmg, debug_context_frame_offset, _execution_context, get_main_graph
from popxl.typing_ import NewAliasAnnotation
from popxl.errors import UndefinedValue

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

TensorLike = Union['Tensor', HostScalarTensor]
"""Tensors and types that can be coerced into a Tensor"""

TILE_SET_MAP = {
    _ir.TileSet.Compute: 'compute',
    _ir.TileSet.IO: 'io',
    _ir.TileSet.Undefined: 'undefined',
}


class TensorSpec(Mapping):
    def __init__(self,
                 shape: Tuple[int, ...],
                 dtype: dtypes.dtype,
                 meta_shape: Tuple[int, ...] = ()):
        """
        Description of a tensor.

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
        return {
            'shape': self.shape,
            'dtype': self.dtype,
            'meta_shape': self.meta_shape
        }

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
    _tensor_types: Dict[str, 'Type[Tensor]'] = {}

    def __init_subclass__(cls, tensor_type: Optional[str] = None,
                          **kwargs) -> None:
        """Hook called when creating a Tensor subclass.
           Argument `tensor_type` is used to allow `_from_pb_tensor` to return
           the correct subclass for any Tensor retrieved from the internal IR"""
        super().__init_subclass__(**kwargs)
        if tensor_type is not None:
            Tensor._tensor_types[tensor_type] = cls

    @classmethod
    def _from_pb_tensor(cls, pb_tensor: _ir.Tensor) -> 'Tensor':
        specifc_cls = cls._tensor_types.get(pb_tensor.tensor_type(),
                                            None)  # type: ignore
        if specifc_cls is not None and cls != specifc_cls:
            return specifc_cls._from_pb_tensor(pb_tensor)

        self = super().__new__(cls)
        self._pb_tensor = pb_tensor
        return self

    ## Properties
    @property
    def id(self) -> str:
        """The fully qualified identifier of the tensor (e.g. 'graph1/Gradient___x')."""
        return str(self._pb_tensor.id)

    @property
    def name(self) -> str:
        """The identifier of the tensor with the graph scope removed (e.g. 'Gradient___x')."""
        return _ir.removeScope(self._pb_tensor.getGraph(), self.id)

    @property
    def scope(self) -> str:
        """The graph scope component of the tensor's identifier (e.g. 'graph1')."""
        return self._pb_tensor.getGraph().getScope().str()

    @property
    def dtype(self) -> dtypes.dtype:
        return dtypes.dtype.as_dtype(self._pb_tensor.info.dataType())

    @property
    def shape(self) -> Tuple[int, ...]:
        """A tuple of the shape of the tensor."""
        return tuple(self._pb_tensor.info.shape())

    @property
    def meta_shape(self) -> Tuple[int, ...]:
        """
        The meta shape of the tensor, which can
        be used, for example, to store the original tensor shape before
        replicated tensor sharding was applied.
        """
        return tuple(self._pb_tensor.info.metaShape())

    @property
    def spec(self) -> TensorSpec:
        """Return a `TensorSpec` instance using the properties of this tensor."""
        return TensorSpec(self.shape, self.dtype, self.meta_shape)

    @property
    def rank(self) -> int:
        """The total number of dimensions in this tensor."""
        return self._pb_tensor.info.rank()

    @property
    def nelms(self) -> int:
        """The total number of elements in this tensor."""
        return self._pb_tensor.info.nelms()

    @property
    def location_info(self):
        # TODO T53608: needs clean up. Exposing private object without documentation
        return self._pb_tensor.tensorLocationInfo

    @property
    def strides(self) -> Tuple[int]:
        """ Get the strides of the tensor, that is the number of bytes to step in each
        dimension when traversing an array in memory. See
        https://numpy.org/doc/stable/reference/generated/numpy.ndarray.strides.html

        Returns:
            List[int]: The strides of the tensor.
        """
        return tuple(self._pb_tensor.info.strides())

    @property
    def name(self) -> str:
        return _ir.removeScope(self._pb_tensor.getGraph(), self.id)

    @property
    @debug_context_frame_offset(2)
    def T(self) -> 'Tensor':
        """The tensor transposed with reversed axes."""
        return self.transpose()

    @property
    @debug_context_frame_offset(2)
    def T_(self) -> 'Tensor':
        """The tensor transposed with reversed axes in-place."""
        return self.transpose_()

    @property
    def ipu(self) -> int:
        """
        The IPU that the tensor is assigned to.
        Raises:
            UndefinedValue: If the IPU is undefined.
        """
        ipu, _ = self._get_ipu_and_tile_set(raise_on_undefined_tile_set=False,
                                            raise_on_undefined_ipu=True)
        return ipu

    @property
    def tile_set(self) -> Literal["compute", "io"]:
        """
        The tile set (`compute` or `io`) that the tensor is assigned to.
        Raises:
            UndefinedValue: If the tile set is undefined.
        """
        _, tile_set = self._get_ipu_and_tile_set(
            raise_on_undefined_tile_set=True, raise_on_undefined_ipu=False)
        return tile_set

    @property
    def ir(self) -> 'Ir':
        """Return the `Ir` that the tensor is a member of."""
        from popxl import Ir
        return Ir._from_pb(self._pb_tensor.getIr())

    @property
    def in_sync_with_ipu(self) -> bool:
        """
        Is the host side buffer data in sync with the data on the IPU device?
        This only applies to variable tensors which can become out of sync if
        `session.weights_from_host` and `session.weights_to_host` are not called.
        Without a transfer from device to host and visa-versa the buffers and the
        data on the IPU can fall out of sync after either is updated.
        """
        return self._pb_tensor.isInSyncWithIPU()

    ## Methods
    @debug_context_frame_offset(1)
    def transpose(self,
                  permutation: Optional[Iterable[int]] = None) -> 'Tensor':
        """
        Permute the axes of a tensor.

        By default this operation reverses the axes of the tensor.

        Args:
            permutation (Optional[Iterable[int]]): Iterable containing the permutation of [0, N-1] where N is the
             rank of the tensor. If not provided, the axes will be reversed.
        Returns:
            out (Tensor): The transposed tensor.
        """
        import popxl.ops as ops
        return ops.transpose(self, permutation)

    @debug_context_frame_offset(1)
    def transpose_(self,
                   permutation: Optional[Iterable[int]] = None) -> 'Tensor':
        """
        Permutes the axes of a tensor in place.

        By default this operation reverses the axes of the tensor.

        This is the in-place version of :func:`~popxl.Tensor.transpose`.
        The behaviour is the same, but it modifies the
        tensor in place.

        Args:
            permutation (Optional[Tuple[int, ...]]):
                Tuple containing the a permutation of [0, N-1] where N is the
                rank of input `t`. If not provided, the axes will be reversed.
        Returns:
            out (Tensor):
                The transposed tensor.
        """
        import popxl.ops as ops
        return ops.transpose_(self, permutation)

    @debug_context_frame_offset(1)
    def reshape(self, shape: Iterable[int]) -> 'Tensor':
        """Returns `ops.reshape(self, shape)`."""
        import popxl.ops as ops
        return ops.reshape(self, shape)

    @debug_context_frame_offset(1)
    def reshape_(self, shape: Iterable[int]) -> 'Tensor':
        """Returns ops.reshape_(self, shape) inplace."""
        import popxl.ops as ops
        return ops.reshape_(self, shape)

    @debug_context_frame_offset(1)
    def flatten(self) -> 'Tensor':
        """Returns ops.flatten(self)."""
        import popxl.ops as ops
        return ops.flatten(self)

    @debug_context_frame_offset(1)
    def flatten_(self) -> 'Tensor':
        """Returns ops.flatten_(self) inplace."""
        import popxl.ops as ops
        return ops.flatten_(self)

    @debug_context_frame_offset(1)
    def detach(self) -> 'Tensor':
        """Returns the detached tensor."""
        import popxl.ops as ops
        return ops.detach(self)

    @debug_context_frame_offset(1)
    def detach_(self) -> 'Tensor':
        """Returns this tensor detached inplace."""
        import popxl.ops as ops
        return ops.detach_(self)

    @debug_context_frame_offset(1)
    def copy_to_ipu(self, destination: int,
                    source: Optional[int] = None) -> 'Tensor':
        """
        Copies a tensor to an IPU.

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
        if raise_on_undefined_tile_set and tile_set == 'undefined':
            raise UndefinedValue("Tensor's tile set is undefined.")
        if raise_on_undefined_ipu and ipu == -1:
            raise UndefinedValue("Tensor's IPU is undefined.")
        return ipu, tile_set

    def _ensure_tensor(
            self,
            value: TensorLike,
            dtype: Optional[dtypes.dtype] = None,
            raise_on_empty=True,
    ) -> 'Tensor':
        """A helper method that's used in operator overloading to ensure that
        all operands are of type `Tensor`.
        If any are not, an attempt is made to convert the operands to a
        constant tensor.

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
                    f"Type: {type(value)}. Value: {value}")
            return t

    ## Dunders
    def __repr__(self) -> str:
        return f"Tensor[{self.id} {self.dtype} {self.shape}]"

    def __hash__(self):
        """Hashes the Tensor, based on Tensor and Ir `id`"""
        return hash((self.id, self.ir))

    def __eq__(self, other: Any) -> bool:
        """Tensor equality, based on Tensor and Ir `id`"""
        return isinstance(
            other, Tensor) and self.id == other.id and self.ir == other.ir

    def __len__(self) -> int:
        """Size of 0th axis or raises a UndefinedValue."""
        if len(self.shape) > 0:
            return self.shape[0]
        else:
            raise ValueError("Tensor is a scalar and doesn't have a length.")

    @debug_context_frame_offset(1)
    def __add__(self, other: TensorLike) -> 'Tensor':
        """Returns `ops.add(self, other)`."""
        import popxl.ops as ops
        return ops.add(self, self._ensure_tensor(other))

    @debug_context_frame_offset(1)
    def __radd__(self, other: TensorLike) -> 'Tensor':
        """Returns `ops.add(other, self)`."""
        import popxl.ops as ops
        return ops.add(self._ensure_tensor(other), self)

    @debug_context_frame_offset(1)
    def __iadd__(self, other: TensorLike) -> 'Tensor':
        """Uses ops.add_ to add 'other' inplace on this tensor (on the left hand side,
            i.e on to this tensor)."""
        import popxl.ops as ops
        ops.add_(self, self._ensure_tensor(other))
        return self

    @debug_context_frame_offset(1)
    def __sub__(self, other: TensorLike) -> 'Tensor':
        """Returns `ops.sub(self, other)`."""
        import popxl.ops as ops
        return ops.sub(self, self._ensure_tensor(other))

    @debug_context_frame_offset(1)
    def __rsub__(self, other: TensorLike) -> 'Tensor':
        """Returns `ops.sub(other, self)`."""
        import popxl.ops as ops
        return ops.sub(self._ensure_tensor(other), self)

    @debug_context_frame_offset(1)
    def __mul__(self, other: TensorLike) -> 'Tensor':
        """Returns `ops.mul(self, other)`."""
        import popxl.ops as ops
        return ops.mul(self, self._ensure_tensor(other))

    @debug_context_frame_offset(1)
    def __rmul__(self, other: TensorLike) -> 'Tensor':
        """Returns `ops.mul(other, self)`."""
        import popxl.ops as ops
        return ops.mul(self._ensure_tensor(other), self)

    @debug_context_frame_offset(1)
    def __truediv__(self, other: TensorLike) -> 'Tensor':
        """Returns `ops.div(self, other)`."""
        import popxl.ops as ops
        return ops.div(self, self._ensure_tensor(other))

    @debug_context_frame_offset(1)
    def __rtruediv__(self, other: TensorLike) -> 'Tensor':
        """Returns `ops.div(other, self)`."""
        import popxl.ops as ops
        return ops.div(self._ensure_tensor(other), self)

    @debug_context_frame_offset(1)
    def __mod__(self, other: TensorLike) -> 'Tensor':
        """Returns `ops.fmod(self, other)`."""
        import popxl.ops as ops
        return ops.fmod(self, self._ensure_tensor(other))

    @debug_context_frame_offset(1)
    def __rmod__(self, other: TensorLike) -> 'Tensor':
        """Returns `ops.fmod(other, self)`."""
        import popxl.ops as ops
        return ops.fmod(self._ensure_tensor(other), self)

    @debug_context_frame_offset(1)
    def __neg__(self) -> 'Tensor':
        """Returns `ops.negate(self)`."""
        import popxl.ops as ops
        return ops.negate(self)

    @debug_context_frame_offset(1)
    def __matmul__(self, other: TensorLike) -> 'Tensor':
        """Returns `ops.matmul(self, other)`."""
        import popxl.ops as ops
        return ops.matmul(self, self._ensure_tensor(other))

    @debug_context_frame_offset(1)
    def __rmatmul__(self, other: TensorLike) -> 'Tensor':
        """Returns `ops.matmul(other, self)`."""
        import popxl.ops as ops
        return ops.matmul(self._ensure_tensor(other), self)

    @debug_context_frame_offset(1)
    def __and__(self, other: TensorLike) -> 'Tensor':
        """Returns `ops.logical_and(self, other)`."""
        import popxl.ops as ops
        return ops.logical_and(self, self._ensure_tensor(other))

    @debug_context_frame_offset(1)
    def __rand__(self, other: TensorLike) -> 'Tensor':
        """Returns `ops.logical_and(other, self)`."""
        import popxl.ops as ops
        return ops.logical_and(self._ensure_tensor(other), self)

    @debug_context_frame_offset(1)
    def __or__(self, other: TensorLike) -> 'Tensor':
        """Returns `ops.logical_or(self, other)`."""
        import popxl.ops as ops
        return ops.logical_or(self, self._ensure_tensor(other))

    @debug_context_frame_offset(1)
    def __ror__(self, other: TensorLike) -> 'Tensor':
        """Returns `ops.logical_or(other, self)`."""
        import popxl.ops as ops
        return ops.logical_or(self._ensure_tensor(other), self)

    @debug_context_frame_offset(1)
    def __invert__(self) -> 'Tensor':
        """Returns `ops.logical_not(self)`."""
        import popxl.ops as ops
        return ops.logical_not(self)

    def __getitem__(self, key: Union[int, slice, Tuple[Union[int, slice], ...],
                                     'Tensor', HostTensor]) -> 'Tensor':
        """
        Supports slicing, integer and boolean indexing. Tensors or host tensors (NumPy/PyTorch arrays and sequences)
        will be converted to a constant Tensor and can be used for integer or boolean indexing.

        Slicing is triggered when the input is an integer, slice (for example, `0:2`) or a tuple of the two. Slicing
        either selects a single index of an axis using an integer or range using a slice. If a single index
        is selected the dimension will be squeezed - this matches numpy slicing rules.

        Integer indexing is triggered when the input is a tensor or host tensor of integers.
        Elements are selected using the indices in the input - see `ops.gather` for details.

        Boolean indexing is triggered when the input is a tensor or host tensor of booleans.
        The input is interpreted as a mask: True propagates the value to the output while False zeros
        the element. This differs to numpy-style boolean indexing, as numpy removed elements indicated
        by False and the output shape is dynamic dependent on the mask's data.

        Examples:

        .. code-block:: python

            # Slicing
            x[0]        # Select all elements where i==0 for axis 0. The output will not include the 0th axis (squeezed)
            x[0,1]      # Select all elements where i==0, j==1 for axis 0 and 1
            x[0:2]      # Slice axis 0 between index 0 and 2
            x[:2,3:]    # Slice axis 0 upto 2 and axis 1 from index 3
            x[:,::-1]   # Select all elements for axis 0 and reverse axis 1

            # Integer indexing
            indices = popxl.variable([0, 2], dtype=popxl.int32)
            x[indices] == Tensor([x[0], x[2]]) # Select elements [0, 2] from `x`

            # Boolean indexing
            x.shape == (3, 1)
            mask = popxl.variable([True, False, True], dtype=popxl.bool)
            x[mask] == Tensor([x[0], 0, x[1]]) # Keep elements 0 and 2. Zero element 1.

            x.shape == (3, 2)
            mask = popxl.variable([True, False, True], dtype=popxl.bool)
            x[mask] == Tensor([x[0], 0, x[1]]) # Broadcast mask: zero row 1

        """

        import popxl.ops as ops

        if isinstance(key, (bool, str)):
            pass  # will raise error at end of function

        elif (isinstance(key, (slice, int))
              or (isinstance(key, tuple)
                  and all(isinstance(e, (slice, int)) for e in key))):
            # Basic slicing (integer or slices)
            key = (key, ) if isinstance(key, (slice, int)) else key

            start = []
            stop = []
            step = []
            int_slices = []

            for i, key_i in enumerate(key):
                if isinstance(key_i, int):
                    start += [key_i]
                    stop += [key_i + 1]
                    step += [1]
                    int_slices += [i]

                elif isinstance(key_i, slice):
                    start += [key_i.start]
                    stop += [key_i.stop]
                    step += [key_i.step]

            out = ops.slice(self, start, stop, step)

            if len(int_slices) > 0:
                out = ops.squeeze(out, axes=int_slices)

            return out

        # Don't capture scalars
        elif isinstance(key, Tensor) or isinstance(key,
                                                   tuple(host_tensor_types)):
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
            f"Not a valid Type: {type(key)}. Value: {key}.")

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

    @debug_context_frame_offset(1)
    def copy_to_ipu(self, dst: int, src: int) -> 'Tensor':
        """Returns `ops.ipu_copy(self, dst, src)`.
            Must provide a src value."""
        import popxl.ops as ops
        return ops.ipu_copy(self, dst, src)


class Constant(Tensor, tensor_type="Const"):
    """
    A constant tensor.
    This tensor cannot change during the runtime of a model.
    """

    @debug_context_frame_offset(1)
    def copy_to_ipu(self, dst: int, src: int) -> 'Tensor':
        """Returns ops.ipu_copy(self, dst, src).
            Must provide a src value."""
        import popxl.ops as ops
        return ops.ipu_copy(self, dst, src)


downcast_np_dtypes = {
    np.dtype('int64'): np.dtype('int32'),
    np.dtype('uint64'): np.dtype('uint32'),
    np.dtype('float64'): np.dtype('float32'),
}


def variable(
        data: Union[HostTensor, float, int],
        dtype: Optional[dtypes.dtype] = None,
        name: Optional[str] = None,
        downcast: bool = True,
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

    Args:
        data (np.ndarray, or a value NumPy can use to construct an np.ndarray):
            The data used to initialise the tensor.
        dtype (Optional[dtype]):
            The data type of the tensor to be created. If not specified NumPy will infer the data
            type and downcast to 32 bits if necessary.
        name (Optional[str]):
            The name of the tensor. Defaults to `None`.
        downcast (bool):
            If True and no dtype is provided, 64-bit float/ints will be downcast to 32-bit variants. Defaults to True.
    """
    g = gcg()
    pb_g = g._pb_graph

    if g != gmg():
        raise ValueError(
            "You cannot initialise a variable tensor within a graph. "
            "It can only be initialised within the main graph. "
            "Please create a graph input for this variable (`popxl.graph_input`) "
            "or use a constant tensor (`popxl.constant`). "
            "See popxl user guide for more details.")

    np_dtype = dtype.as_numpy() if dtype is not None else None
    np_data: np.ndarray = np.array(data, dtype=np_dtype)
    if np_data.dtype in downcast_np_dtypes and downcast and dtype is None:
        np_data = np_data.astype(downcast_np_dtypes[np_data.dtype])
    popxl_dt = dtypes.dtype.as_dtype(np_data)

    info = _ir.TensorInfo(popxl_dt._pb_dtype, np_data.shape)
    pb_id = g._create_tensor_id(name)
    pb_g.addVarInit(pb_id, info, np_data)

    return Variable._from_pb_tensor(pb_g.getTensor(pb_id))


def remote_variable(data: Union[HostTensor, float, int],
                    remote_buffer: "RemoteBuffer",
                    offset: int = 0,
                    dtype: Optional[dtypes.dtype] = None,
                    name: Optional[str] = None,
                    downcast: bool = True,
                    num_shards: int = 1) -> Variable:
    """Create a variable Tensor that is stored in remote memory.

    Args:
        data (np.ndarray, or a value numpy can use to construct an np.ndarray):
            The data used to initialise the tensor.
        remote_buffer (RemoteBuffer):
            The remote buffer to store the variable.
        offset (int):
            The offset into the entries of the remote buffer to store the variable. Defaults to 0
        dtype (Optional[dtype]):
            The data type of the tensor to be created, if not specified Numpy will infer the data
            type and be downcasted to 32 bits if necessary.
        name (Optional[str]):
            The name of the tensor. Defaults to `None`.
        downcast (bool):
            If no dtype is provided 64 bit float/ints will be downcasted to 32 bit variants. Default to True.
        shards (int):
            Number of shards for this variable.
    Returns:
        Variable: The remote variable.

    Raises:
        ValueError: if the variable shape or dtype does not match remote buffer's
    """

    var = variable(data, dtype, name, downcast)
    # Check if the var matches the remote buffer in shape and dtype
    remote_buffer.validate_tensor_matches_buffer(var, num_shards)

    var._pb_tensor.setTensorLocationInfo(
        _ir.TensorLocation(_ir.TensorStorage.OffChip,
                           _ir.ReplicatedTensorSharding.Off),
        remote_buffer.remote_buffer_id, offset)
    return var


def remote_replica_sharded_variable(data: Union[HostTensor, float, int],
                                    remote_buffer: "RemoteBuffer",
                                    offset: int = 0,
                                    dtype: Optional[dtypes.dtype] = None,
                                    name: Optional[str] = None,
                                    downcast: bool = True) -> Variable:
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
        data (np.ndarray, or a value numpy can use to construct an np.ndarray):
            The data used to initialise the tensor.
        remote_buffer (RemoteBuffer): The handle to the remote buffer.
        offset (int): The offset to index the tensor shard in the remote tensor.
        dtype (Optional[dtype]):
            The data type of the tensor to be created, if not specified Numpy will infer the data
            type and be downcasted to 32 bits if necessary.
        name (Optional[str]):
            The name of the tensor. Defaults to `None`.
        downcast (bool):
            If no dtype is provided 64 bit float/ints will be downcasted to 32 bit variants. Default to True.

    Raises:
        ValueError: If replication has not been enabled.
        ValueError: If the number of elements of `var` is not divisible by the number of replicas.
        ValueError: if the variable shape or dtype does not match remote buffer's

    Returns:
        Variable: The remote sharded variable.
    """
    opts = gcg().ir._pb_ir.getSessionOptions()
    num_replicas: int = opts.replicatedGraphCount

    # Set the meta_shape for the RemoteBuffer, this will be required later in ops.remote_load
    np_dtype = dtype.as_numpy() if dtype is not None else None
    np_data: np.ndarray = np.array(data, dtype=np_dtype)

    if remote_buffer.meta_shape == ():
        remote_buffer.meta_shape = np_data.shape
    elif remote_buffer.meta_shape != np_data.shape:
        raise ValueError(
            f"Cannot use RemoteBuffer[id={remote_buffer.remote_buffer_id}] for replica sharded variable of shape {var.shape}. "
            f"The buffer's meta_shape has already been set to: {remote_buffer.meta_shape}."
        )

    var = remote_variable(data, remote_buffer, offset, dtype, name, downcast,
                          num_replicas)

    if not opts.enableReplicatedGraphs:
        raise ValueError("Replication has not been enabled on the current IR")

    if (var.nelms % num_replicas) != 0:
        raise ValueError(
            f"Variable {var} is not divisible by the number of replicas {num_replicas}."
        )

    var._pb_tensor.setTensorLocationInfo(
        _ir.TensorLocation(_ir.TensorStorage.OffChip,
                           _ir.ReplicatedTensorSharding.On),
        remote_buffer.remote_buffer_id, offset)
    return var


def replica_sharded_variable(data: Union[HostTensor, float, int],
                             dtype: Optional[dtypes.dtype] = None,
                             name: Optional[str] = None,
                             downcast: bool = True) -> Tuple[Variable, Tensor]:
    """Scatter a tensor in equal shards across replicas (data parallelism) of the
        same model/graph. Eliminates redundant data storage when the full (un-sharded) tensor does
        not need to be present on each IPU. Does not store the full tensor in remote memory.

    Args:
        data (np.ndarray, or a value numpy can use to construct an np.ndarray):
            The data used to initialise the tensor.
        dtype (Optional[dtype]):
            The data type of the tensor to be created, if not specified Numpy will infer the data
            type and be downcasted to 32 bits if necessary.
        name (Optional[str]):
            The name of the tensor. Defaults to `None`.
        downcast (bool):
            If no dtype is provided 64 bit float/ints will be downcasted to 32 bit variants. Default to True.

    Returns:
        Tuple[Variable, Tensor]:
            A tuple of tensors:
            1. The full variable. This should NOT be used directly. It can be used to interact with Session's get/set data methods
            2. The sharded variable.
    """
    import popxl.ops as ops
    from popxl.remote_buffer import remote_buffer

    _dtype = dtype or dtypes.dtype.as_dtype(dtype)

    opts = gcg().ir._pb_ir.getSessionOptions()
    replicas: int = opts.replicatedGraphCount
    shard_shape: Tuple[int, ...] = (data.size // replicas, )
    buffer = remote_buffer(shard_shape, _dtype, 1)

    # Create a remote RTS variable
    var = remote_replica_sharded_variable(data, buffer, 0, dtype, name,
                                          downcast)

    # Load/Store the variable in the WeightsFromHost/WeightsToHost programs.
    with get_main_graph():
        with _execution_context(_ir.ExecutionContext.WeightsFromHostFragment):
            var_shard = ops.remote_load(buffer, 0, var.name + "_rts")

        with _execution_context(_ir.ExecutionContext.WeightsToHostFragment):
            ops.remote_store(buffer, 0, var_shard)

    return var, var_shard


def constant(
        data: Union[HostTensor, float, int],
        dtype: Optional[dtypes.dtype] = None,
        name: Optional[str] = None,
        downcast: bool = True,
) -> Constant:
    """A constant tensor that is initialised with data during graph creation.

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
        data (np.array, or a value numpy can use to construct an np.ndarray):
            The data used to initialise the tensor.
        dtype (Optional[dtype]):
            The data type of the tensor to be created. If not specified, NumPy will infer the data
            type and downcast to 32 bits if necessary.
        name (Optional[str]):
            The name of the tensor. Defaults to `None`.
    """
    g = gcg()
    pb_g = g._pb_graph
    np_dtype = dtype.as_numpy() if dtype is not None else None
    np_data: np.ndarray = np.array(data, dtype=np_dtype)
    if np_data.dtype in downcast_np_dtypes and downcast and dtype is None:
        np_data = np_data.astype(downcast_np_dtypes[np_data.dtype])
    popxl_dt = dtypes.dtype.as_dtype(np_data)
    info = _ir.TensorInfo(popxl_dt._pb_dtype, np_data.shape)
    pb_id = g._create_tensor_id(name)
    pb_g.addConstInit(pb_id, info, np_data)
    return Constant._from_pb_tensor(pb_g.getTensor(pb_id))


def graph_input(shape: Iterable[int],
                dtype: dtypes.dtype,
                name: Optional[str] = None,
                by_ref: bool = False,
                meta_shape: Optional[Iterable[int]] = None) -> Tensor:
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
            y, = ops.call(add_w_graph, x, w)

    Args:
        shape (Tuple[int, ...])
            The shape of the tensor.
        dtype (dtype):
            The data type of the tensor.
        name (Optional[str]):
            The name of the tensor.
    """
    g = gcg()
    pb_g = g._pb_graph

    pb_id = g._create_tensor_id(name)
    if meta_shape:
        pb_info = _ir.TensorInfo(dtype._pb_dtype, list(shape),
                                 list(meta_shape))
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
            y, = ops.call(add_w_graph, x, w)

    Args:
        t (Tensor):
            The graph tensor to mark as an output in the current graph.

    Throws:
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