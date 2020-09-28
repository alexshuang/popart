# Copyright (c) 2020 Graphcore Ltd. All rights reserved.
import os
import popart
import numpy as np
import torch
import onnx
from onnx import numpy_helper
import math
import pytest

# `import test_util` requires adding to sys.path
import sys
from pathlib import Path
sys.path.append(str(Path(__file__).resolve().parent.parent))
import test_util as tu


def run_model(tmpdir,
              model_file_name,
              execution_mode="normal",
              enable_matmul_serialization=False,
              enable_outlining=False,
              enable_accum=False,
              accum_factor=1,
              activation_tensor_location_settings=None,
              weight_tensor_location_settings=None,
              optimizer_state_tensor_location_settings=None,
              accumulator_tensor_location_settings=None,
              num_layers=3,
              dsize=48,
              batch_size=1,
              num_iterations=1,
              num_replicas=1,
              optimizer=popart.SGD({"defaultLearningRate": (0.5, False)})):

    np.random.seed(10911)
    matmul_serialization_mode = 'output_channels'
    matmul_serialization_factor = 2

    builder = popart.Builder()
    ip = builder.addInputTensor(
        popart.TensorInfo("FLOAT", [batch_size, dsize, dsize]))

    def add_layer(index, in_id):
        w = builder.addInitializedInputTensor(
            np.random.rand(dsize, dsize).astype(np.float32), f"W{index}")
        matmul_id = builder.aiOnnx.matmul([in_id, w])
        if enable_matmul_serialization:
            builder.setSerializeMatMul({matmul_id}, matmul_serialization_mode,
                                       matmul_serialization_factor)
        return matmul_id

    out = ip
    l1 = ""

    for i in range(num_layers):
        if execution_mode == "normal":
            vgid = 0
        elif execution_mode == "phased":
            vgid = i % 2
        elif execution_mode == "pipelined":
            vgid = i
        else:
            raise ValueError(f"Execution mode {execution_mode} unsupported")
        with builder.executionPhase(i), builder.pipelineStage(
                i), builder.virtualGraph(vgid):
            out = add_layer(i, out)

        if i == num_layers - 1:
            with builder.executionPhase(i), builder.pipelineStage(
                    i), builder.virtualGraph(vgid):
                l1 = builder.aiGraphcore.l1loss([out], 0.1,
                                                popart.ReductionType.Sum)

    anchorIds = []

    builder.addOutputTensor(out)

    if execution_mode == "normal":
        num_ipus = 1
    elif execution_mode == "phased":
        num_ipus = 2
    elif execution_mode == "pipelined":
        num_ipus = 2**math.ceil(math.log2(num_layers))
    else:
        raise ValueError(f"Execution mode {execution_mode} unsupported")

    device = tu.create_test_device(num_replicas * num_ipus,
                                   pattern=popart.SyncPattern.Full)

    dfAnchors = {}
    for anchorId in anchorIds:
        dfAnchors.update({anchorId: popart.AnchorReturnType("All")})

    opts = popart.SessionOptions()
    opts.enableOutlining = enable_outlining
    opts.enableReplicatedGraphs = True if num_replicas > 1 else False
    opts.replicatedGraphCount = num_replicas
    opts.enableGradientAccumulation = enable_accum
    opts.accumulationFactor = accum_factor

    if activation_tensor_location_settings is not None:
        opts.activationTensorLocationSettings = activation_tensor_location_settings
    if weight_tensor_location_settings is not None:
        opts.weightTensorLocationSettings = weight_tensor_location_settings
    if optimizer_state_tensor_location_settings is not None:
        opts.optimizerStateTensorLocationSettings = optimizer_state_tensor_location_settings
    if accumulator_tensor_location_settings is not None:
        opts.accumulatorTensorLocationSettings = accumulator_tensor_location_settings

    if execution_mode == "normal":
        opts.virtualGraphMode = popart.VirtualGraphMode.Manual
    elif execution_mode == "phased":
        opts.executionPhaseSettings.phases = num_layers
        opts.autoRecomputation = popart.RecomputationType.NoRecompute
        opts.virtualGraphMode = popart.VirtualGraphMode.ExecutionPhases
        opts.explicitRecomputation = False
    elif execution_mode == "pipelined":
        opts.enablePipelining = True
        opts.virtualGraphMode = popart.VirtualGraphMode.Manual
        opts.autoRecomputation = popart.RecomputationType.Standard

    proto = builder.getModelProto()

    session = popart.TrainingSession(fnModel=proto,
                                     dataFlow=popart.DataFlow(1, dfAnchors),
                                     optimizer=optimizer,
                                     loss=l1,
                                     patterns=popart.Patterns(
                                         popart.PatternsLevel.All),
                                     userOptions=opts,
                                     deviceInfo=device)

    session.prepareDevice()
    session.weightsFromHost()
    anchors = session.initAnchorArrays()

    for i in range(num_iterations):
        ip_data = np.random.rand(num_replicas, accum_factor, batch_size, dsize,
                                 dsize).astype(np.float32)
        stepio = popart.PyStepIO({ip: ip_data}, anchors)
        session.run(stepio)

    print("anchors:")
    print(anchors)
    session.modelToHost(str(tmpdir / model_file_name))


def check_model(lhs_model, rhs_model):
    for i in range(len(lhs_model.graph.initializer)):
        lhs = lhs_model.graph.initializer[i]
        for j in range(len(rhs_model.graph.initializer)):
            rhs = rhs_model.graph.initializer[j]
            if (rhs.name == lhs.name):
                print(f'Checking initializer {i} ({lhs.name} - {rhs.name})')
                lhsa = numpy_helper.to_array(lhs)
                rhsa = numpy_helper.to_array(rhs)
                assert np.allclose(lhsa, rhsa, rtol=1.e-4, atol=1.e-6)


# Standard OnChip settings
onChipLocation = popart.TensorLocationSettings(
    location=popart.TensorLocation(
        storage=popart.TensorStorage.OnChip,
        loadTileSet=popart.TileSet.Compute,
        storageTileSet=popart.TileSet.Compute,
        replicatedTensorSharding=popart.ReplicatedTensorSharding.Off),
    minElementsForOffChip=0,
    minElementsForReplicatedTensorSharding=2)

# Standard OffChip settings
offChipLocation = popart.TensorLocationSettings(
    location=popart.TensorLocation(
        storage=popart.TensorStorage.OffChip,
        loadTileSet=popart.TileSet.Compute,
        storageTileSet=popart.TileSet.Compute,
        replicatedTensorSharding=popart.ReplicatedTensorSharding.Off),
    minElementsForOffChip=0,
    minElementsForReplicatedTensorSharding=2)

# Replicated tensor sharding OffChip settings
offChipRtsLocation = popart.TensorLocationSettings(
    location=popart.TensorLocation(
        storage=popart.TensorStorage.OffChip,
        loadTileSet=popart.TileSet.Compute,
        storageTileSet=popart.TileSet.Compute,
        replicatedTensorSharding=popart.ReplicatedTensorSharding.On),
    minElementsForOffChip=0,
    minElementsForReplicatedTensorSharding=2)


@tu.requires_ipu
def test_weight_update(tmpdir):
    run_model(tmpdir, 'without_phased.onnx', "normal", False, True)
    run_model(tmpdir,
              'with_phased.onnx',
              execution_mode="phased",
              enable_matmul_serialization=False,
              activation_tensor_location_settings=offChipLocation,
              weight_tensor_location_settings=offChipLocation,
              optimizer_state_tensor_location_settings=offChipLocation,
              accumulator_tensor_location_settings=offChipLocation)

    without_phased = onnx.load(str(tmpdir / 'without_phased.onnx'))
    with_phased = onnx.load(str(tmpdir / 'with_phased.onnx'))

    check_model(without_phased, with_phased)


@tu.requires_ipu
def test_onchip_memory(tmpdir):
    onchip_settings = popart.TensorLocationSettings(
        popart.TensorStorage.OnChip, 0)
    run_model(tmpdir, 'model_normal.onnx', execution_mode="normal")
    run_model(tmpdir,
              'model_onchip_act.onnx',
              execution_mode="phased",
              activation_tensor_location_settings=onChipLocation,
              weight_tensor_location_settings=offChipLocation,
              optimizer_state_tensor_location_settings=offChipLocation,
              accumulator_tensor_location_settings=onChipLocation)
    run_model(tmpdir,
              'model_onchip_weights.onnx',
              execution_mode="phased",
              activation_tensor_location_settings=offChipLocation,
              weight_tensor_location_settings=onChipLocation,
              optimizer_state_tensor_location_settings=offChipLocation,
              accumulator_tensor_location_settings=onChipLocation)
    run_model(tmpdir,
              'model_onchip_opt_state.onnx',
              execution_mode="phased",
              activation_tensor_location_settings=offChipLocation,
              weight_tensor_location_settings=offChipLocation,
              optimizer_state_tensor_location_settings=onChipLocation,
              accumulator_tensor_location_settings=onChipLocation)

    normal = onnx.load(str(tmpdir / 'model_normal.onnx'))
    onchip_act = onnx.load(str(tmpdir / 'model_onchip_act.onnx'))
    onchip_weights = onnx.load(str(tmpdir / 'model_onchip_weights.onnx'))
    onchip_opt_state = onnx.load(str(tmpdir / 'model_onchip_opt_state.onnx'))

    check_model(normal, onchip_act)
    check_model(normal, onchip_weights)
    check_model(normal, onchip_opt_state)


@tu.requires_ipu
def test_inplacing_phased_constraints(tmpdir):
    # This used to fail, see T23985
    run_model(tmpdir,
              'phased.onnx',
              execution_mode="phased",
              num_layers=5,
              optimizer=popart.SGD({
                  "defaultLearningRate": (0.1, True),
                  "defaultMomentum": (0.0, False),
                  "defaultWeightDecay": (0.0, False),
                  "defaultDampening": (0.0, True)
              }),
              activation_tensor_location_settings=offChipLocation,
              weight_tensor_location_settings=offChipLocation,
              optimizer_state_tensor_location_settings=offChipLocation,
              accumulator_tensor_location_settings=offChipLocation)


# Check that 2 batches on 1 replica or 1 batch per replica on 2 replicas
# results in the same updated weight with SGD0
@tu.requires_ipu
def test_replicated_sgd0_weight_update(tmpdir):

    run_model(tmpdir,
              'phased.onnx',
              execution_mode="phased",
              batch_size=2,
              num_replicas=1,
              activation_tensor_location_settings=offChipLocation,
              weight_tensor_location_settings=offChipLocation,
              optimizer_state_tensor_location_settings=offChipLocation,
              accumulator_tensor_location_settings=offChipLocation)
    run_model(tmpdir,
              'phased_replicated.onnx',
              execution_mode="phased",
              batch_size=1,
              num_replicas=2,
              activation_tensor_location_settings=offChipLocation,
              weight_tensor_location_settings=offChipLocation,
              optimizer_state_tensor_location_settings=offChipLocation,
              accumulator_tensor_location_settings=offChipLocation)
    run_model(tmpdir,
              'phased_replicated_rws.onnx',
              execution_mode="phased",
              batch_size=1,
              num_replicas=2,
              activation_tensor_location_settings=offChipLocation,
              weight_tensor_location_settings=offChipRtsLocation,
              optimizer_state_tensor_location_settings=offChipRtsLocation,
              accumulator_tensor_location_settings=offChipRtsLocation)

    phased = onnx.load(str(tmpdir / 'phased.onnx'))
    phased_replicated = onnx.load(str(tmpdir / 'phased_replicated.onnx'))
    phased_replicated_rws = onnx.load(
        str(tmpdir / 'phased_replicated_rws.onnx'))

    check_model(phased, phased_replicated)
    check_model(phased, phased_replicated_rws)


# Check that 2 batches on 1 replica or 1 batch per replica on 2 replicas
# results in the same updated weight with SGD1
@tu.requires_ipu
@pytest.mark.skip(reason="T26179 test produces NaNs")
def test_replicated_sgd1_weight_update(tmpdir):

    optimizer_dict = {
        "defaultLearningRate": (0.1, False),
        "defaultMomentum": (0.9, False),
        "defaultDampening": (0.2, False),
        "defaultVelocityScaling": (0.1, False),
        "lossScaling": (1.0, True),
        "defaultWeightDecay": (0.2, True)
    }

    run_model(tmpdir,
              'phased.onnx',
              execution_mode="phased",
              batch_size=2,
              num_replicas=1,
              num_iterations=5,
              optimizer=popart.SGD(optimizer_dict),
              activation_tensor_location_settings=offChipLocation,
              weight_tensor_location_settings=offChipLocation,
              optimizer_state_tensor_location_settings=offChipLocation,
              accumulator_tensor_location_settings=offChipLocation)
    run_model(tmpdir,
              'phased_replicated.onnx',
              execution_mode="phased",
              batch_size=1,
              num_replicas=2,
              num_iterations=5,
              optimizer=popart.SGD(optimizer_dict),
              activation_tensor_location_settings=offChipLocation,
              weight_tensor_location_settings=offChipLocation,
              optimizer_state_tensor_location_settings=offChipLocation,
              accumulator_tensor_location_settings=offChipLocation)
    run_model(tmpdir,
              'phased_replicated_rws.onnx',
              execution_mode="phased",
              batch_size=1,
              num_replicas=2,
              num_iterations=5,
              optimizer=popart.SGD(optimizer_dict),
              activation_tensor_location_settings=offChipLocation,
              weight_tensor_location_settings=offChipRtsLocation,
              optimizer_state_tensor_location_settings=offChipRtsLocation,
              accumulator_tensor_location_settings=offChipRtsLocation)

    phased = onnx.load(str(tmpdir / 'phased.onnx'))
    phased_replicated = onnx.load(str(tmpdir / 'phased_replicated.onnx'))
    phased_replicated_rws = onnx.load(
        str(tmpdir / 'phased_replicated_rws.onnx'))

    check_model(phased, phased_replicated)
    check_model(phased, phased_replicated_rws)


# Check that 2 batches on 1 replica or 1 batch per replica on 2 replicas
# results in the same updated weight with SGD1
@tu.requires_ipu
def test_replicated_adam_weight_update(tmpdir):

    optimizer_dict = {
        "defaultLearningRate": (0.005, True),
        "defaultBeta1": (0.7, True),
        "defaultBeta2": (0.8, True),
        "defaultWeightDecay": (0.1, True),
        "defaultEps": (1e-6, True),
        "lossScaling": (10.0, True),
    }

    run_model(tmpdir,
              'phased.onnx',
              execution_mode="phased",
              batch_size=2,
              num_replicas=1,
              num_iterations=5,
              optimizer=popart.Adam(optimizer_dict),
              activation_tensor_location_settings=offChipLocation,
              weight_tensor_location_settings=offChipLocation,
              optimizer_state_tensor_location_settings=offChipLocation,
              accumulator_tensor_location_settings=offChipLocation)
    run_model(tmpdir,
              'phased_replicated.onnx',
              execution_mode="phased",
              batch_size=1,
              num_replicas=2,
              num_iterations=5,
              optimizer=popart.Adam(optimizer_dict),
              activation_tensor_location_settings=offChipLocation,
              weight_tensor_location_settings=offChipLocation,
              optimizer_state_tensor_location_settings=offChipLocation,
              accumulator_tensor_location_settings=offChipLocation)
    run_model(tmpdir,
              'phased_replicated_rws.onnx',
              execution_mode="phased",
              batch_size=1,
              num_replicas=2,
              num_iterations=5,
              optimizer=popart.Adam(optimizer_dict),
              activation_tensor_location_settings=offChipLocation,
              weight_tensor_location_settings=offChipRtsLocation,
              optimizer_state_tensor_location_settings=offChipRtsLocation,
              accumulator_tensor_location_settings=offChipRtsLocation)

    phased = onnx.load(str(tmpdir / 'phased.onnx'))
    phased_replicated = onnx.load(str(tmpdir / 'phased_replicated.onnx'))
    phased_replicated_rws = onnx.load(
        str(tmpdir / 'phased_replicated_rws.onnx'))

    check_model(phased, phased_replicated)
    check_model(phased, phased_replicated_rws)


# Check that 2 batches on 1 replica or 1 batch per replica on 2 replicas
# results in the same updated weight with SGD1
@tu.requires_ipu
def test_replicated_lamb_weight_update(tmpdir):

    optimizer_dict = {
        "defaultLearningRate": (0.005, True),
        "defaultBeta1": (0.7, True),
        "defaultBeta2": (0.8, True),
        "defaultWeightDecay": (0.1, True),
        "defaultEps": (1e-6, True),
        "lossScaling": (10.0, True),
    }

    run_model(tmpdir,
              'phased.onnx',
              execution_mode="phased",
              batch_size=4,
              num_replicas=1,
              num_iterations=5,
              optimizer=popart.Adam(optimizer_dict, popart.AdamMode.Lamb),
              activation_tensor_location_settings=offChipLocation,
              weight_tensor_location_settings=offChipLocation,
              optimizer_state_tensor_location_settings=offChipLocation,
              accumulator_tensor_location_settings=offChipLocation)
    run_model(tmpdir,
              'phased_replicated.onnx',
              execution_mode="phased",
              batch_size=2,
              num_replicas=2,
              num_iterations=5,
              optimizer=popart.Adam(optimizer_dict, popart.AdamMode.Lamb),
              activation_tensor_location_settings=offChipLocation,
              weight_tensor_location_settings=offChipLocation,
              optimizer_state_tensor_location_settings=offChipLocation,
              accumulator_tensor_location_settings=offChipLocation)
    run_model(tmpdir,
              'phased_replicated_rws.onnx',
              execution_mode="phased",
              batch_size=2,
              num_replicas=2,
              num_iterations=5,
              optimizer=popart.Adam(optimizer_dict, popart.AdamMode.Lamb),
              activation_tensor_location_settings=offChipLocation,
              weight_tensor_location_settings=offChipRtsLocation,
              optimizer_state_tensor_location_settings=offChipRtsLocation,
              accumulator_tensor_location_settings=offChipLocation)
    run_model(tmpdir,
              'phased_replicated_rws_acc.onnx',
              execution_mode="phased",
              batch_size=1,
              num_replicas=2,
              num_iterations=5,
              enable_accum=True,
              accum_factor=2,
              optimizer=popart.Adam(optimizer_dict, popart.AdamMode.Lamb),
              activation_tensor_location_settings=offChipLocation,
              weight_tensor_location_settings=offChipRtsLocation,
              optimizer_state_tensor_location_settings=offChipRtsLocation,
              accumulator_tensor_location_settings=offChipLocation)

    phased = onnx.load(str(tmpdir / 'phased.onnx'))
    phased_replicated = onnx.load(str(tmpdir / 'phased_replicated.onnx'))
    phased_replicated_rws = onnx.load(
        str(tmpdir / 'phased_replicated_rws.onnx'))
    phased_replicated_rws_acc = onnx.load(
        str(tmpdir / 'phased_replicated_rws_acc.onnx'))

    check_model(phased, phased_replicated)
    check_model(phased, phased_replicated_rws)
    check_model(phased, phased_replicated_rws_acc)


@tu.requires_ipu
def test_sharding_without_replicas_warning(tmpdir):
    run_model(tmpdir,
              'warning.onnx',
              execution_mode="phased",
              num_replicas=1,
              activation_tensor_location_settings=offChipLocation,
              weight_tensor_location_settings=offChipRtsLocation,
              optimizer_state_tensor_location_settings=offChipRtsLocation,
              accumulator_tensor_location_settings=offChipRtsLocation)


@tu.requires_ipu
def test_pipelined_streaming_lamb(tmpdir):

    optimizer_dict = {
        "defaultLearningRate": (0.005, True),
        "defaultBeta1": (0.7, True),
        "defaultBeta2": (0.8, True),
        "defaultWeightDecay": (0.1, True),
        "defaultEps": (1e-6, True),
        "lossScaling": (10.0, True),
    }

    run_model(tmpdir,
              'normal.onnx',
              execution_mode="normal",
              num_layers=2,
              batch_size=12,
              num_replicas=1,
              num_iterations=5,
              enable_accum=False,
              accum_factor=1,
              optimizer=popart.Adam(optimizer_dict, popart.AdamMode.Lamb),
              activation_tensor_location_settings=onChipLocation,
              weight_tensor_location_settings=onChipLocation,
              optimizer_state_tensor_location_settings=onChipLocation,
              accumulator_tensor_location_settings=onChipLocation)
    run_model(tmpdir,
              'pipelined.onnx',
              execution_mode="pipelined",
              num_layers=2,
              batch_size=2,
              num_replicas=1,
              num_iterations=5,
              enable_accum=True,
              accum_factor=6,
              optimizer=popart.Adam(optimizer_dict, popart.AdamMode.Lamb),
              activation_tensor_location_settings=onChipLocation,
              weight_tensor_location_settings=onChipLocation,
              optimizer_state_tensor_location_settings=onChipLocation,
              accumulator_tensor_location_settings=onChipLocation)
    run_model(tmpdir,
              'pipelined_streaming.onnx',
              execution_mode="pipelined",
              num_layers=2,
              batch_size=2,
              num_replicas=1,
              num_iterations=5,
              enable_accum=True,
              accum_factor=6,
              optimizer=popart.Adam(optimizer_dict, popart.AdamMode.Lamb),
              activation_tensor_location_settings=onChipLocation,
              weight_tensor_location_settings=onChipLocation,
              optimizer_state_tensor_location_settings=offChipRtsLocation,
              accumulator_tensor_location_settings=onChipLocation)
    run_model(tmpdir,
              'pipelined_streaming_rep.onnx',
              execution_mode="pipelined",
              num_layers=2,
              batch_size=1,
              num_replicas=2,
              num_iterations=5,
              enable_accum=True,
              accum_factor=6,
              optimizer=popart.Adam(optimizer_dict, popart.AdamMode.Lamb),
              activation_tensor_location_settings=onChipLocation,
              weight_tensor_location_settings=onChipLocation,
              optimizer_state_tensor_location_settings=offChipLocation,
              accumulator_tensor_location_settings=onChipLocation)
    run_model(tmpdir,
              'pipelined_streaming_rep_rts.onnx',
              execution_mode="pipelined",
              num_layers=2,
              batch_size=1,
              num_replicas=2,
              num_iterations=5,
              enable_accum=True,
              accum_factor=6,
              optimizer=popart.Adam(optimizer_dict, popart.AdamMode.Lamb),
              activation_tensor_location_settings=onChipLocation,
              weight_tensor_location_settings=onChipLocation,
              optimizer_state_tensor_location_settings=offChipRtsLocation,
              accumulator_tensor_location_settings=onChipLocation)

    normal = onnx.load(str(tmpdir / 'normal.onnx'))
    pipelined = onnx.load(str(tmpdir / 'pipelined.onnx'))
    pipelined_streaming = onnx.load(str(tmpdir / 'pipelined_streaming.onnx'))
    pipelined_streaming_rep = onnx.load(
        str(tmpdir / 'pipelined_streaming_rep.onnx'))
    pipelined_streaming_rep_rts = onnx.load(
        str(tmpdir / 'pipelined_streaming_rep_rts.onnx'))

    check_model(normal, pipelined)
    check_model(normal, pipelined_streaming)
    check_model(normal, pipelined_streaming_rep)
    check_model(normal, pipelined_streaming_rep_rts)
