# Copyright (c) 2020 Graphcore Ltd. All rights reserved.
import numpy as np
import pytest
import popart
import test_util as tu
import pprint
import json
import onnx
from onnx import numpy_helper


def test_streamingmemory_momentum(tmpdir):
    def model():
        np.random.seed(1984)
        input_data = np.random.randint(0, 20, (8, )).astype(np.uint32)
        weight_data = np.random.rand(4, 20).astype(np.float32)

        builder = popart.Builder()

        d0 = builder.addInputTensor(
            popart.TensorInfo('UINT32', input_data.shape), 'data0')

        w0 = builder.addInitializedInputTensor(weight_data, 'weight0')

        with builder.executionPhase(0), builder.virtualGraph(
                0), builder.nameScope("pp0"):
            w0_t = builder.aiOnnx.transpose([w0])
            x = builder.aiOnnx.gather([w0_t, d0])
            x = builder.aiGraphcore.detach([x])

        with builder.executionPhase(1), builder.virtualGraph(
                1), builder.nameScope("pp1"):
            x = builder.aiOnnx.add([
                x,
                builder.addInitializedInputTensor(
                    np.random.rand(4).astype(np.float32), 'weight1')
            ])

        with builder.executionPhase(2), builder.virtualGraph(
                0), builder.nameScope("pp2"):
            x = builder.aiOnnx.sub([
                x,
                builder.addInitializedInputTensor(
                    np.random.rand(4).astype(np.float32), 'weight2')
            ])

        with builder.executionPhase(3), builder.virtualGraph(
                1), builder.nameScope("pp3"):
            x = builder.aiOnnx.mul([
                x,
                builder.addInitializedInputTensor(
                    np.random.rand(4).astype(np.float32), 'weight3')
            ])

        with builder.executionPhase(4), builder.virtualGraph(
                0), builder.nameScope("pp4"):
            x = builder.aiOnnx.matmul([x, w0])
            loss = builder.aiGraphcore.l1loss([x], 0.1, debugPrefix='loss')

        return builder.getModelProto(), {d0: input_data}, x, loss

    def run_test(aliaszerocopy):
        proto, data, x, loss = model()

        options = popart.SessionOptions()
        patterns = popart.Patterns()

        optimizer = popart.SGD({
            "defaultLearningRate": (0.1, True),
            "defaultMomentum": (0.9, True),
            "defaultDampening": (0, True)
        })

        options.enableOutlining = True
        options.outlineThreshold = -np.inf
        options.enableOutliningCopyCostPruning = False
        options.autoRecomputation = popart.RecomputationType.Standard
        options.virtualGraphMode = popart.VirtualGraphMode.ExecutionPhases
        options.explicitRecomputation = True
        options.aliasZeroCopy = aliaszerocopy
        options.executionPhaseSettings.phases = 5
        request_ipus = 2

        device = tu.create_test_device(2, pattern=popart.SyncPattern.Full)

        dataFlow = popart.DataFlow(1, {x: popart.AnchorReturnType("ALL")})

        session = popart.TrainingSession(fnModel=proto,
                                         dataFlow=dataFlow,
                                         userOptions=options,
                                         loss=loss,
                                         optimizer=optimizer,
                                         patterns=patterns,
                                         deviceInfo=device)

        session.prepareDevice()

        session.weightsFromHost()

        anchors = session.initAnchorArrays()

        stepio = popart.PyStepIO(data, anchors)

        session.run(stepio)

        file_path = str(tmpdir / f"aliaszerocopy_model_test.onnx")
        session.modelToHost(file_path)
        post_proto = onnx.load(file_path)

        device.detach()

        graph_report = json.loads(session.getGraphReport())
        max_tile_memory = max(graph_report["memory"]["byTile"]["total"])
        total_memory = np.sum(graph_report["memory"]["byTile"]["total"])
        return anchors[x], post_proto, total_memory

    outputs_1, proto_1, total_memory_1 = run_test(False)
    outputs_2, proto_2, total_memory_2 = run_test(True)

    # Reference values: 2551446 2530304
    print(total_memory_1, total_memory_2)

    assert np.allclose(outputs_1, outputs_2)

    for j in range(len(proto_1.graph.initializer)):
        print(f"Checking initializer {j}")
        gt = proto_1.graph.initializer[j]
        gt = numpy_helper.to_array(gt)
        val = proto_2.graph.initializer[j]
        val = numpy_helper.to_array(val)
        assert np.allclose(gt, val)

    assert total_memory_1 > total_memory_2
