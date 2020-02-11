import numpy as np
import pytest
import popart
import torch
import test_util as tu
import time


def test_stepio_bufferinput(tmpdir):

    builder = popart.Builder()
    shape = popart.TensorInfo("FLOAT", [2])

    i1 = builder.addInputTensor(shape)
    i2 = builder.addInputTensor(shape)
    o = builder.aiOnnx.add([i1, i2])
    builder.addOutputTensor(o)

    proto = builder.getModelProto()

    batches_per_step = 2

    dataFlow = popart.DataFlow(
        batches_per_step, {
            i1: popart.AnchorReturnType("ALL"),
            i2: popart.AnchorReturnType("ALL"),
            o: popart.AnchorReturnType("ALL")
        })

    session = popart.InferenceSession(fnModel=proto,
                                      dataFeed=dataFlow,
                                      deviceInfo=tu.get_poplar_cpu_device())

    session.prepareDevice()

    anchors = session.initAnchorArrays()

    i1_data = np.random.rand(batches_per_step, 2).astype(np.float32)
    i2_data = np.random.rand(batches_per_step, 2).astype(np.float32)

    inputs = {i1: i1_data, i2: i2_data}
    stepio = popart.PyStepIO(inputs, anchors)

    session.run(stepio)

    return

    # confirm that writing device-to-host of a Stream Tensor returns correctly (unchanged)
    print(i1_data)
    print(anchors[i1])
    assert (np.allclose(anchors[i1], i1_data))

    print(i2_data)
    print(anchors[i2])
    assert (np.allclose(anchors[i2], i2_data))

    expected_result = i1_data + i2_data

    print(expected_result)
    print(anchors[o])

    assert (np.allclose(anchors[o], expected_result))


@tu.requires_ipu()
def test_stepio_bufferinput_ipu(tmpdir):

    builder = popart.Builder()
    shape = popart.TensorInfo("FLOAT", [2])

    i1 = builder.addInputTensor(shape)
    i2 = builder.addInputTensor(shape)
    o = builder.aiOnnx.add([i1, i2])
    builder.addOutputTensor(o)

    proto = builder.getModelProto()

    batches_per_step = 2

    dataFlow = popart.DataFlow(
        batches_per_step, {
            i1: popart.AnchorReturnType("ALL"),
            i2: popart.AnchorReturnType("ALL"),
            o: popart.AnchorReturnType("ALL")
        })

    session = popart.InferenceSession(fnModel=proto,
                                      dataFeed=dataFlow,
                                      deviceInfo=tu.acquire_ipu(1))

    session.prepareDevice()

    anchors = session.initAnchorArrays()

    i1_data = np.random.rand(batches_per_step, 2).astype(np.float32)
    i2_data = np.random.rand(batches_per_step, 2).astype(np.float32)

    inputs = {i1: i1_data, i2: i2_data}
    stepio = popart.PyStepIO(inputs, anchors)

    session.run(stepio)

    return

    # confirm that writing device-to-host of a Stream Tensor returns correctly (unchanged)
    print(i1_data)
    print(anchors[i1])
    assert (np.allclose(anchors[i1], i1_data))

    print(i2_data)
    print(anchors[i2])
    assert (np.allclose(anchors[i2], i2_data))

    expected_result = i1_data + i2_data

    print(expected_result)
    print(anchors[o])

    assert (np.allclose(anchors[o], expected_result))


def test_stepio_callbackinput(tmpdir):

    builder = popart.Builder()
    shape = popart.TensorInfo("FLOAT", [2])

    i1 = builder.addInputTensor(shape)
    i2 = builder.addInputTensor(shape)
    o = builder.aiOnnx.add([i1, i2])
    builder.addOutputTensor(o)

    proto = builder.getModelProto()

    batches_per_step = 2

    dataFlow = popart.DataFlow(
        batches_per_step, {
            i1: popart.AnchorReturnType("ALL"),
            i2: popart.AnchorReturnType("ALL"),
            o: popart.AnchorReturnType("ALL")
        })

    session = popart.InferenceSession(fnModel=proto,
                                      dataFeed=dataFlow,
                                      deviceInfo=tu.get_poplar_cpu_device())

    session.prepareDevice()

    anchors = session.initAnchorArrays()

    i1_data = np.random.rand(batches_per_step, 2).astype(np.float32)
    i2_data = np.random.rand(batches_per_step, 2).astype(np.float32)

    inputs = {i1: i1_data, i2: i2_data}

    i1_c = 0
    i2_c = 0

    def input_callback(id, prefetch):
        nonlocal i1_c, i2_c

        time.sleep(2)
        print("input_callback ", id)

        t = inputs[id]

        print(t)

        if id == i1:
            print("input_callback ", id, len(t))
            if (i1_c < len(t)):
                result = t[i1_c]
                i1_c = i1_c + 1

        if id == i2:
            print("input_callback ", id, len(t))
            if (i2_c < len(t)):
                result = t[i2_c]
                i2_c = i2_c + 1

        print(result)

        return result

    def input_complete_callback(id):
        print("input_complete_callback ", id)

    i1_d = 0
    i2_d = 0
    o_d = 0

    def output_callback(id):
        nonlocal i1_d, i2_d, o_d

        time.sleep(2)
        print("output_callback ", id)

        t = anchors[id]

        if id == i1:
            result = t[i1_d]
            i1_d = i1_d + 1

        if id == i2:
            result = t[i2_d]
            i2_d = i2_d + 1

        if id == o:
            result = t[o_d]
            o_d = o_d + 1

        return result

    def output_complete_callback(id):
        print("output_complete_callback ", id)

    stepio = popart.PyStepIOCallback(input_callback, input_complete_callback,
                                     output_callback, output_complete_callback)

    session.run(stepio)

    # confirm that writing device-to-host of a Stream Tensor returns correctly (unchanged)
    assert (np.allclose(anchors[i1], i1_data))
    assert (np.allclose(anchors[i2], i2_data))

    expected_result = i1_data + i2_data
    assert (np.allclose(anchors[o], expected_result))


@tu.requires_ipu()
def test_stepio_callbackinput_ipu(tmpdir):

    builder = popart.Builder()
    shape = popart.TensorInfo("FLOAT", [2])

    i1 = builder.addInputTensor(shape)
    i2 = builder.addInputTensor(shape)
    o = builder.aiOnnx.add([i1, i2])
    builder.addOutputTensor(o)

    proto = builder.getModelProto()

    batches_per_step = 2

    dataFlow = popart.DataFlow(
        batches_per_step, {
            i1: popart.AnchorReturnType("ALL"),
            i2: popart.AnchorReturnType("ALL"),
            o: popart.AnchorReturnType("ALL")
        })

    session = popart.InferenceSession(fnModel=proto,
                                      dataFeed=dataFlow,
                                      deviceInfo=tu.acquire_ipu(1))

    session.prepareDevice()

    anchors = session.initAnchorArrays()

    i1_data = np.random.rand(batches_per_step, 2).astype(np.float32)
    i2_data = np.random.rand(batches_per_step, 2).astype(np.float32)

    inputs = {i1: i1_data, i2: i2_data}

    i1_c = 0
    i2_c = 0

    def input_callback(id, prefetch):
        nonlocal i1_c, i2_c

        if (prefetch == True):
            return None

        time.sleep(1)
        print("input_callback ", id)

        t = inputs[id]

        result = None

        print(t)

        if id == i1:
            print("input_callback ", id, len(t), i1_c)
            if (i1_c < len(t)):
                result = t[i1_c]
                i1_c = i1_c + 1

        if id == i2:
            print("input_callback ", id, len(t), i2_c)
            if (i2_c < len(t)):
                result = t[i2_c]
                i2_c = i2_c + 1

        print(result)

        return result

    def input_complete_callback(id):
        print("output_complete_callback ", id)

    i1_d = 0
    i2_d = 0
    o_d = 0

    def output_callback(id):
        nonlocal i1_d, i2_d, o_d

        time.sleep(1)
        print("output_callback ", id)

        t = anchors[id]

        if id == i1:
            result = t[i1_d]
            i1_d = i1_d + 1

        if id == i2:
            result = t[i2_d]
            i2_d = i2_d + 1

        if id == o:
            result = t[o_d]
            o_d = o_d + 1

        return result

    def output_complete_callback(id):
        print("output_complete_callback ", id)

    stepio = popart.PyStepIOCallback(input_callback, input_complete_callback,
                                     output_callback, output_complete_callback)

    session.run(stepio)

    # confirm that writing device-to-host of a Stream Tensor returns correctly (unchanged)
    assert (np.allclose(anchors[i1], i1_data))
    assert (np.allclose(anchors[i2], i2_data))

    expected_result = i1_data + i2_data
    assert (np.allclose(anchors[o], expected_result))
