# Copyright (c) 2019 Graphcore Ltd. All rights reserved.
import popart
import numpy as np
import torch
import onnx
from onnx import numpy_helper
import test_util as tu


def test_weight_update(tmpdir):
    def run(model_file_name, enableOutlining):
        dsize = 10
        ratio = 0.5
        builder = popart.Builder()
        ip = builder.addInputTensor(popart.TensorInfo("FLOAT", [dsize, dsize]))
        d__ip = popart.reservedGradientPrefix() + ip

        def add_layer(in_id):
            w = builder.addInitializedInputTensor(
                np.ones([dsize, dsize], np.float32))
            matmul_id = builder.aiOnnx.matmul([in_id, w])
            return matmul_id

        m1 = add_layer(ip)
        m2 = add_layer(m1)
        m3 = add_layer(m2)

        anchorIds = []
        for i in (ip, m1, m2, m3):
            anchorIds.append(popart.reservedGradientPrefix() + i)

        out = builder.aiGraphcore.identityloss([m3])
        builder.addOutputTensor(out)

        device = tu.create_test_device()

        dfAnchors = {}
        for anchorId in anchorIds:
            dfAnchors.update({anchorId: popart.AnchorReturnType("All")})

        opts = popart.SessionOptions()
        opts.enableOutlining = enableOutlining
        opts.separateCallOpPdfs = False

        proto = builder.getModelProto()

        session = popart.TrainingSession(
            fnModel=proto,
            dataFlow=popart.DataFlow(1, dfAnchors),
            optimizer=popart.ConstSGD(0.1),
            loss=out,
            patterns=popart.Patterns(popart.PatternsLevel.All),
            userOptions=opts,
            deviceInfo=device)

        session.prepareDevice()
        session.weightsFromHost()
        anchors = session.initAnchorArrays()

        ip_data = np.ones((dsize, dsize), dtype=np.float32)
        stepio = popart.PyStepIO({ip: ip_data}, anchors)

        session.run(stepio)

        session.modelToHost(str(tmpdir / model_file_name))

    run('without_outlining.onnx', False)
    run('with_outlining.onnx', True)

    with_outlining = onnx.load(str(tmpdir / 'without_outlining.onnx'))
    without_outlining = onnx.load(str(tmpdir / 'with_outlining.onnx'))

    for i in range(len(without_outlining.graph.initializer)):
        print(f'Checking initializer {i}')
        lhs = without_outlining.graph.initializer[i]
        lhs = numpy_helper.to_array(lhs)
        rhs = with_outlining.graph.initializer[i]
        rhs = numpy_helper.to_array(rhs)
        assert np.allclose(lhs, rhs)


def test_batches_per_step_greater_than_one():
    def run(enableOutlining):
        dsize = 10
        ratio = 0.5
        batches_per_step = 2
        builder = popart.Builder()
        ip = builder.addInputTensor(popart.TensorInfo("FLOAT", [dsize, dsize]))
        d__ip = popart.reservedGradientPrefix() + ip

        def add_layer(in_id):
            w = builder.addInitializedInputTensor(
                np.ones([dsize, dsize], np.float32))
            # w = builder.aiGraphcore.printtensor([w])
            matmul_id = builder.aiOnnx.matmul([in_id, w])
            return matmul_id

        m1 = add_layer(ip)
        m2 = add_layer(m1)
        m3 = add_layer(m2)

        anchorIds = []
        for i in (ip, m1, m2, m3):
            anchorIds.append(popart.reservedGradientPrefix() + i)

        out = builder.aiGraphcore.identityloss([m3])
        builder.addOutputTensor(out)

        device = tu.create_test_device()

        dfAnchors = {}
        for anchorId in anchorIds:
            dfAnchors.update({anchorId: popart.AnchorReturnType("All")})

        opts = popart.SessionOptions()
        opts.enableOutlining = enableOutlining

        session = popart.TrainingSession(
            fnModel=builder.getModelProto(),
            dataFlow=popart.DataFlow(batches_per_step, dfAnchors),
            optimizer=popart.ConstSGD(0.1),
            loss=out,
            patterns=popart.Patterns(popart.PatternsLevel.All),
            userOptions=opts,
            deviceInfo=device)

        session.prepareDevice()
        session.weightsFromHost()
        anchors = session.initAnchorArrays()

        ip_data = np.ones((batches_per_step, dsize, dsize), dtype=np.float32)
        stepio = popart.PyStepIO({ip: ip_data}, anchors)

        session.run(stepio)

        return anchors

    without_outlining = run(False)
    with_outlining = run(True)

    for key in without_outlining.keys():
        print(f'Checking anchor {key}')
        lhs = without_outlining[key]
        rhs = with_outlining[key]

        assert np.allclose(lhs, rhs)
