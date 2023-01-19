# Copyright (c) 2022 Graphcore Ltd. All rights reserved.
from typing import Tuple
import pytest
import torch
import numpy as np
import popxl
import popxl.ops as ops
from popxl.tensor import Tensor, graph_input

import sys
from pathlib import Path

sys.path.append(str(Path(__file__).resolve().parent.parent))
import test_util as tu


def test_session():
    w_input = [2.0]
    c_input = [1.0]

    ir = popxl.Ir()
    with ir.main_graph, popxl.in_sequence():
        w = popxl.variable(w_input, popxl.dtypes.float32)
        x_h2d = popxl.h2d_stream(w.shape, w.dtype)
        x = ops.host_load(x_h2d)
        y = w * x
        y_d2h = popxl.d2h_stream(y.shape, y.dtype)
        ops.host_store(y_d2h, y)
        ops.var_updates.accumulate_(
            w, popxl.constant(c_input, popxl.dtypes.float32), 1.0
        )

    session, inputs, outputs = run_session(ir, [x], [x_h2d], 1)

    w_data = session.get_tensor_data(w)
    assert np.allclose(outputs[y_d2h], inputs[x_h2d] * w_input)
    assert np.allclose(
        w_data,
        np.array(w_input).astype(np.float32) + np.array(c_input).astype(np.float32),
    )


class LinearHostLoad(popxl.Module):
    def __init__(self):
        self.W: Tensor = None
        self.b: Tensor = None

        self.h2d = popxl.h2d_stream((2, 2), popxl.float32, name="x_stream")
        self.d2h = popxl.d2h_stream((2, 2), popxl.float32, name="y_stream")

    def build(self, out_features: int, bias: bool = True) -> Tuple[Tensor, ...]:
        x = ops.host_load(self.h2d, "x")
        self.W = graph_input((x.shape[-1], out_features), popxl.float32, "W")
        y = x @ self.W
        if bias:
            self.b = graph_input((out_features,), popxl.float32, "b")
            y = y + self.b
        ops.host_store(self.d2h, y)
        return self.W, self.b


def test_session_multi_iteration():
    ir = popxl.Ir()
    bps = 8
    with ir.main_graph:
        W_data = np.random.normal(0, 0.1, (2, 2)).astype(np.float32)
        W = popxl.variable(W_data, name="W")
        b_data = np.random.normal(0, 0.4, (2)).astype(np.float32)
        b = popxl.variable(b_data, name="b")

        linear = LinearHostLoad()
        linear_graph = ir.create_graph(linear, out_features=2)

        W, b = ops.repeat(linear_graph, bps, inputs_dict={linear.W: W, linear.b: b})

    _, inputs, outputs = run_session(ir, [linear.h2d.spec], [linear.h2d], bps)

    input_: np.ndarray = inputs[linear.h2d]
    output: np.ndarray = outputs[linear.d2h]
    for i in range(bps):
        assert np.allclose(output[i, ...], (input_[i, ...] @ W_data) + b_data)


# fmt: off
@pytest.mark.parametrize("data,shape,dtype", [
    [[0.0, 1.0, 2.0, 3.0], (4,), popxl.float32],
    [np.array([0.0, 1.0, 2.0, 3.0], dtype='float64'), (4,), popxl.float32],
    [np.array([0.0, 1.0, 2.0, 3.0], dtype='float32'), (4,), popxl.float32],
    [torch.tensor([0.0, 1.0, 2.0, 3.0], dtype=torch.float64, requires_grad=True), (4,), popxl.float32],
    [torch.tensor([0.0, 1.0, 2.0, 3.0], dtype=torch.float32, requires_grad=True), (4,), popxl.float32],
    [0, tuple(), popxl.int32],
    [0.0, tuple(), popxl.float32],
    [True, tuple(), popxl.bool],
])
def test_session_input_types(data, shape, dtype):
    ir = popxl.Ir()
    with ir.main_graph, popxl.in_sequence():
        x_h2d = popxl.h2d_stream(shape, dtype)
        y = ops.host_load(x_h2d)
        y_d2h = popxl.d2h_stream(y.shape, y.dtype)
        ops.host_store(y_d2h, y)

    with popxl.Session(ir, device_desc="ipu_model") as session:
        _ = session.run(inputs={x_h2d: data})
# fmt: on


def run_session(
    ir, input_tensors, input_d2hs, num_host_transfers, weights_to_host_on_exit=True
):
    ir.num_host_transfers = num_host_transfers

    session = popxl.Session(
        ir, device_desc="ipu_model", weights_to_host_on_exit=weights_to_host_on_exit
    )

    inputs = {}
    for t, t_d2h in zip(input_tensors, input_d2hs):
        shape = (num_host_transfers,) + t.shape if num_host_transfers > 1 else t.shape
        t_input = np.random.normal(0, 0.4, shape).astype(t.dtype.as_numpy())
        inputs[t_d2h] = t_input

    with session:
        outputs = session.run(inputs)
        # Could also do `outputs = session.create_host_outputs()`
        # session.run_with_outputs(inputs, outputs)
    return session, inputs, outputs


def test_get_tensor_data_bad_argument_type():
    ir = popxl.Ir()
    mg = ir.main_graph

    with mg:
        c = popxl.constant(1.0)
        w = popxl.variable(2.0)
        h2d = popxl.h2d_stream(w.shape, w.dtype)
        x = ops.host_load(h2d)

        w2 = ops.var_updates.accumulate_(w, x)
        ops.var_updates.accumulate_(w2, c)

    session = popxl.Session(ir)
    session.device = tu.create_test_device(numIpus=1).device

    with session:
        session.run({h2d: np.array(1.0)})

    # Variable permitted.
    session.get_tensor_data(w)
    session.get_tensors_data([w])
    # Constant permitted.
    session.get_tensor_data(c)
    session.get_tensors_data([c])
    # ActGrad tensor not permitted.
    with pytest.raises(TypeError):
        session.get_tensor_data(x)
    with pytest.raises(TypeError):
        session.get_tensors_data([x])
    # Stream object not permitted.
    with pytest.raises(TypeError):
        session.get_tensor_data(h2d)
    with pytest.raises(TypeError):
        session.get_tensors_data([h2d])


def test_session_no_weights_to_host_on_exit():
    w_input = [2.0]
    c_input = [1.0]

    ir = popxl.Ir()
    with ir.main_graph, popxl.in_sequence():
        w = popxl.variable(w_input, popxl.dtypes.float32)
        x_h2d = popxl.h2d_stream(w.shape, w.dtype)
        x = ops.host_load(x_h2d)
        y = w * x
        y_d2h = popxl.d2h_stream(y.shape, y.dtype)
        ops.host_store(y_d2h, y)
        ops.var_updates.accumulate_(
            w, popxl.constant(c_input, popxl.dtypes.float32), 1.0
        )

    session, inputs, outputs = run_session(
        ir, [x], [x_h2d], 1, weights_to_host_on_exit=False
    )

    w_data = session.get_tensor_data(w)
    assert np.allclose(outputs[y_d2h], inputs[x_h2d] * w_input)
    assert np.allclose(
        w_data,
        w_input,
    )
