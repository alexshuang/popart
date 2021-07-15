# Copyright (c) 2019 Graphcore Ltd. All rights reserved.
import numpy as np
import popart
import pytest
import test_util as tu


@tu.requires_ipu_model
def test_ipu_copy_bca1():

    popart.getLogger().setLevel("TRACE")

    builder = popart.Builder()

    i1 = builder.addInputTensor(popart.TensorInfo("FLOAT", [1]))
    i2 = builder.addInputTensor(popart.TensorInfo("FLOAT", [1]))
    o1 = builder.aiOnnx.add([i1, i2])
    o2 = builder.aiOnnx.add([i1, i2])
    o = builder.aiOnnx.add([o1, o2])
    builder.addOutputTensor(o)

    builder.virtualGraph(o1, 0)
    builder.virtualGraph(o2, 0)
    builder.virtualGraph(o, 1)

    proto = builder.getModelProto()

    dataFlow = popart.DataFlow(1, {o: popart.AnchorReturnType("All")})

    opts = popart.SessionOptions()
    opts.virtualGraphMode = popart.VirtualGraphMode.Manual

    s = popart.InferenceSession(fnModel=proto,
                                dataFlow=dataFlow,
                                userOptions=opts,
                                deviceInfo=tu.create_test_device(numIpus=3))

    s.prepareDevice()


# Will fail due to an invalid virtual graph
@tu.requires_ipu_model
def test_ipu_copy_aca1():

    popart.getLogger().setLevel("TRACE")

    builder = popart.Builder()

    i1 = builder.addInputTensor(popart.TensorInfo("FLOAT", [1]))
    i2 = builder.addInputTensor(popart.TensorInfo("FLOAT", [1]))
    o1 = builder.aiOnnx.add([i1, i2])
    o2 = builder.aiOnnx.add([i1, i2])
    o = builder.aiOnnx.add([o1, o2])
    builder.addOutputTensor(o)

    builder.virtualGraph(o1, 0)
    builder.virtualGraph(o2, 0)
    builder.virtualGraph(o, 10)  # << Invalid virtual graph

    proto = builder.getModelProto()

    dataFlow = popart.DataFlow(1, {o: popart.AnchorReturnType("All")})

    opts = popart.SessionOptions()
    opts.virtualGraphMode = popart.VirtualGraphMode.Manual

    s = popart.InferenceSession(fnModel=proto,
                                dataFlow=dataFlow,
                                userOptions=opts,
                                deviceInfo=tu.create_test_device(numIpus=3))

    with pytest.raises(popart.popart_exception) as e_info:
        s.prepareDevice()

    assert (("inputs=[{}, {}], outputs=[{}]) " +
             "has been assigned to an invalid virtual graph 10").format(
                 o1 + "_c10", o2 + "_c10", o) in e_info.value.args[0])


# Test that an input stream tensor is correctly mapped to multiple ipus
@tu.requires_ipu_model
def test_ipu_copy_bca4():

    popart.getLogger().setLevel("TRACE")

    builder = popart.Builder()

    i1 = builder.addInputTensor(popart.TensorInfo("FLOAT", [1]))
    i2 = builder.addInputTensor(popart.TensorInfo("FLOAT", [1]))
    o1 = builder.aiOnnx.add([i1, i2])
    o2 = builder.aiOnnx.add([i1, i2])
    t1 = builder.aiOnnx.transpose([i1], [])
    o3 = builder.aiOnnx.add([o1, o2])
    o = builder.aiOnnx.add([o3, t1])
    builder.addOutputTensor(o)

    builder.virtualGraph(o1, 0)
    builder.virtualGraph(o2, 2)
    builder.virtualGraph(t1, 2)
    builder.virtualGraph(o3, 1)
    builder.virtualGraph(o, 1)

    proto = builder.getModelProto()

    dataFlow = popart.DataFlow(1, {o: popart.AnchorReturnType("All")})

    opts = popart.SessionOptions()
    opts.virtualGraphMode = popart.VirtualGraphMode.Manual

    s = popart.InferenceSession(fnModel=proto,
                                dataFlow=dataFlow,
                                userOptions=opts,
                                deviceInfo=tu.create_test_device(numIpus=3))

    s.prepareDevice()


# Test to ensure that same tensor it not copied multiple times to the same IPU
@tu.requires_ipu_model
def test_ipu_copy_bca2():

    popart.getLogger().setLevel("TRACE")

    builder = popart.Builder()

    i1 = builder.addInputTensor(popart.TensorInfo("FLOAT", [1]))
    i2 = builder.addInputTensor(popart.TensorInfo("FLOAT", [1]))
    o1 = builder.aiOnnx.add([i1, i2])
    o2 = builder.aiOnnx.add([i1, i2])

    o3 = builder.aiOnnx.add([o1, o2])
    o4 = builder.aiOnnx.add([o1, o2])

    o = builder.aiOnnx.add([o3, o4])
    builder.addOutputTensor(o)

    builder.virtualGraph(o1, 0)
    builder.virtualGraph(o2, 0)
    builder.virtualGraph(o3, 1)
    builder.virtualGraph(o4, 1)

    builder.virtualGraph(o, 2)

    proto = builder.getModelProto()

    dataFlow = popart.DataFlow(1, {o: popart.AnchorReturnType("All")})

    opts = popart.SessionOptions()
    opts.virtualGraphMode = popart.VirtualGraphMode.Manual

    s = popart.InferenceSession(fnModel=proto,
                                dataFlow=dataFlow,
                                userOptions=opts,
                                deviceInfo=tu.create_test_device(numIpus=3))

    s.prepareDevice()


# Test to make sure that if a single op has multiple it mapped to multiple inputs then the copy does
# the right thing
@tu.requires_ipu_model
def test_ipu_copy_bca3():

    popart.getLogger().setLevel("TRACE")

    builder = popart.Builder()

    i1 = builder.addInputTensor(popart.TensorInfo("FLOAT", [1]))
    i2 = builder.addInputTensor(popart.TensorInfo("FLOAT", [1]))
    o1 = builder.aiOnnx.add([i1, i2])
    o = builder.aiOnnx.add([o1, o1])
    builder.addOutputTensor(o)

    builder.virtualGraph(o1, 0)
    builder.virtualGraph(o, 1)

    proto = builder.getModelProto()

    dataFlow = popart.DataFlow(1, {o: popart.AnchorReturnType("All")})

    opts = popart.SessionOptions()
    opts.virtualGraphMode = popart.VirtualGraphMode.Manual

    s = popart.InferenceSession(fnModel=proto,
                                dataFlow=dataFlow,
                                userOptions=opts,
                                deviceInfo=tu.create_test_device(numIpus=2))

    s.prepareDevice()


@tu.requires_ipu_model
def test_ipu_copy_bca5():

    popart.getLogger().setLevel("TRACE")

    builder = popart.Builder()

    constData = np.random.rand(2, 2).astype(np.float32)
    c1 = builder.aiOnnx.constant(constData, "constShapeData")
    i2 = builder.addInputTensor(popart.TensorInfo("FLOAT", [2, 2]))
    o1 = builder.aiOnnx.add([c1, i2])
    o2 = builder.aiOnnx.add([c1, i2])
    t1 = builder.aiOnnx.transpose([c1], [])
    o3 = builder.aiOnnx.add([o1, o2])
    o = builder.aiOnnx.add([o3, t1])
    builder.addOutputTensor(o)

    builder.virtualGraph(o1, 0)
    builder.virtualGraph(o2, 2)
    builder.virtualGraph(t1, 2)
    builder.virtualGraph(o3, 1)
    builder.virtualGraph(o, 1)

    proto = builder.getModelProto()

    dataFlow = popart.DataFlow(1, {o: popart.AnchorReturnType("All")})

    opts = popart.SessionOptions()
    opts.virtualGraphMode = popart.VirtualGraphMode.Manual

    s = popart.InferenceSession(fnModel=proto,
                                dataFlow=dataFlow,
                                userOptions=opts,
                                deviceInfo=tu.create_test_device(numIpus=3))

    s.prepareDevice()


#     IPU 0      *        IPU 1
# =========================================
#                *
#     i1 -----> copy ---> mul
#     |          *        |
#     v          *        v
#    add -----> copy --> add
#                *        |
#                *        v
#                *      output
@tu.requires_ipu_model
def test_copy_to_op_with_duplicate_inputs():
    popart.getLogger().setLevel("TRACE")

    builder = popart.Builder()

    i1 = builder.addInputTensor(popart.TensorInfo("FLOAT", [1]))

    o1 = builder.aiOnnx.add([i1, i1])
    builder.virtualGraph(o1, 0)

    o2 = builder.aiOnnx.mul([i1, i1])
    builder.virtualGraph(o2, 1)

    o3 = builder.aiOnnx.add([o1, o2])
    builder.virtualGraph(o3, 1)

    o = o3
    builder.addOutputTensor(o)

    proto = builder.getModelProto()

    dataFlow = popart.DataFlow(1, {o: popart.AnchorReturnType("All")})

    opts = popart.SessionOptions()
    opts.virtualGraphMode = popart.VirtualGraphMode.Manual

    s = popart.InferenceSession(fnModel=proto,
                                dataFlow=dataFlow,
                                userOptions=opts,
                                deviceInfo=tu.create_test_device(numIpus=3))

    s.prepareDevice()
