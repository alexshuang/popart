# Copyright (c) 2018 Graphcore Ltd. All rights reserved.
import numpy as np
import popart
import test_util as tu


def inference_add_to_variable(np_type):
    builder = popart.Builder()

    shape = popart.TensorInfo(np_type, [2])

    i1 = builder.addInputTensor(shape)
    i2 = builder.addInitializedInputTensor(np.array([2., 4.], dtype=np_type))
    o = builder.aiOnnx.add([i1, i2])
    builder.addOutputTensor(o)

    proto = builder.getModelProto()

    dataFlow = popart.DataFlow(1, {o: popart.AnchorReturnType("All")})

    with tu.create_test_device() as device:
        session = popart.InferenceSession(fnModel=proto,
                                          dataFlow=dataFlow,
                                          deviceInfo=device)

        session.prepareDevice()

        anchors = session.initAnchorArrays()

        inputs = {i1: np.array([1., 3.], dtype=np_type)}
        stepio = popart.PyStepIO(inputs, anchors)

        session.run(stepio)

    assert (np.allclose(anchors[o], np.array([3., 7.], dtype=np_type)))


def test_add_variable_fp32():
    inference_add_to_variable(np.float32)


def test_add_variable_fp16():
    inference_add_to_variable(np.float16)
