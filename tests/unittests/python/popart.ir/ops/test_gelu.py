# Copyright (c) 2021 Graphcore Ltd. All rights reserved.
import numpy as np
import popart.ir as pir
import popart.ir.ops as ops
import popart._internal.ir as _ir
from utils import contains_op_of_type


class TestGelu:
    def test_fn(self):
        ir = pir.Ir()
        g = ir.main_graph()

        with g:
            a = pir.variable(np.ones((1, 2, 3)))
            c = ops.gelu(a)
        assert len(g.get_tensors()) == 2
        assert contains_op_of_type("Gelu", _ir.op.GeluOp, g)