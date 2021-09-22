# Copyright (c) 2021 Graphcore Ltd. All rights reserved.
from popart.ir.tensor import Tensor
from popart.ir.graph import Graph
import popart._internal.ir as _ir
from typing import Optional
from popart.ir import dtypes


def cast_if_needed(t: Tensor, data_type: dtypes.dtype) -> Tensor:
    from popart.ir.ops.cast import cast
    if t.dtype != data_type:
        return cast(t, data_type)
    return t


def check_in_graph(graph: Graph, *tensors: Tensor):
    """Checks if tensors are in graph. If not, raises a ValueError."""
    for tensor in tensors:
        if tensor not in graph:
            raise ValueError(
                f"Tensor {tensor.name} is not in the current Graph {graph.name}."
            )


def convert_optional_float(v: Optional[float]):
    return _ir.OptionalFloat(v) if v is not None else _ir.OptionalFloat()
