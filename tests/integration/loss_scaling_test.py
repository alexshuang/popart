# Copyright (c) 2021 Graphcore Ltd. All rights reserved.
from contextlib import ExitStack
import numpy as np
import popart
import pytest
import test_util as tu
import torch
import json

from loss_scaling_util_test import getModelProto, getOptimizers, run_automatic_loss_scaling_comparison_test


def loss_scaling_test(constLossScaling):

    # In previous implementations of popart loss scaling only worked for
    # nll/l1 losses. This test is to ensure it works for any loss.
    #
    # For a computation matmul(c1,p1) where c1 is a constant matrix and
    # p1 is a parameter, calculate loss as reducesum(matmul(c1,p1)) and
    # update p1 using a optimizer that 'uses' loss scaling. Do this
    # for both pytorch and popart and compare the result.

    np.random.seed(1)
    sgd_learning_rate = 0.1
    sgd_moment = 0.9
    c1_shape = (1, 1)
    p1_shape = (1, 1)
    c1_init = np.random.rand(*c1_shape).astype(np.float32)
    p1_init = np.random.rand(*p1_shape).astype(np.float32)
    out_init = np.matmul(c1_init, p1_init)

    def get_updated_p1_popart():
        builder = popart.Builder()

        # Computation is out = matmul(i1, i2)
        c1 = builder.addInputTensor(popart.TensorInfo("FLOAT", c1_shape))
        p1 = builder.addInitializedInputTensor(p1_init)
        out = builder.aiOnnx.matmul([c1, p1])

        # Set up a training session.
        device = tu.create_test_device()
        dataFlow = popart.DataFlow(
            1, {
                c1: popart.AnchorReturnType("Final"),
                p1: popart.AnchorReturnType("Final"),
                out: popart.AnchorReturnType("Final")
            })

        # We're testing losses other than nll/l1 work.
        loss = builder.aiOnnx.reducesum([out])
        optimizer = popart.SGD({
            "defaultLearningRate": (sgd_learning_rate, True),
            "defaultMomentum": (sgd_moment, False),
            "lossScaling": (200, constLossScaling)
        })
        session = popart.TrainingSession(builder.getModelProto(),
                                         deviceInfo=device,
                                         dataFlow=dataFlow,
                                         loss=loss,
                                         optimizer=optimizer)

        session.prepareDevice()
        session.weightsFromHost()

        # Run the popart session to get an answer.
        anchors = session.initAnchorArrays()
        stepio = popart.PyStepIO({c1: c1_init}, anchors)
        session.run(stepio)
        return anchors[c1], anchors[p1], anchors[out]

    def get_updated_p1_pytorch():

        # Computation is out = matmul(i1, i2)
        c1 = torch.tensor(c1_init, requires_grad=False)
        p1 = torch.tensor(p1_init, requires_grad=True)
        out = torch.matmul(c1, p1)

        # Set up optimizer, compute loss.
        optimizer = torch.optim.SGD([p1],
                                    lr=sgd_learning_rate,
                                    momentum=sgd_moment)
        optimizer.zero_grad()
        loss = torch.sum(out)

        # Compute gradient and optimize.
        loss.backward()
        optimizer.step()

        # Return the tensors.
        return c1.detach().numpy(), p1.detach().numpy(), out.detach().numpy()

    c1_popart, p1_popart, out_popart = get_updated_p1_popart()
    c1_pytorch, p1_pytorch, out_pytorch = get_updated_p1_pytorch()

    # We're not expecting changes in c1 or out, check anyway.
    assert (np.isclose(c1_popart, c1_init))
    assert (np.isclose(c1_pytorch, c1_init))
    assert (np.isclose(out_popart, out_init))
    assert (np.isclose(out_pytorch, out_init))

    # We expect p1 to match.
    assert (np.isclose(
        p1_popart, p1_pytorch
    )), f'Expected p1_popart={p1_popart} to match p1_pytorch={p1_pytorch}'


def test_loss_scaling_with_const():
    loss_scaling_test(True)


def test_loss_scaling_with_nonconst():
    loss_scaling_test(False)


def test_auto_loss_scaling_with_inference_session():
    """
    Create an InferenceSession with auto loss scaling enabled. Observe an
    error from the auto loss scale transform
    """
    builder = popart.Builder()

    t0 = builder.addInputTensor("FLOAT", [2, 2])
    out = builder.aiOnnx.matmul([t0, t0])

    opts = popart.SessionOptions()
    opts.automaticLossScalingSettings.enabled = True
    opts.automaticLossScalingSettings.binEdgeLocation = 0.5
    opts.automaticLossScalingSettings.thresholdUpperCountProportion = 0.2

    with pytest.raises(popart.popart_exception) as e_info:
        session = popart.InferenceSession(builder.getModelProto(),
                                          deviceInfo=tu.create_test_device(),
                                          dataFlow=popart.DataFlow(1, [out]),
                                          userOptions=opts)
    assert e_info.value.args[0].endswith("Only compatible when doing training")


def test_auto_loss_scaling_with_const_loss_scale_tensor():
    """
    Create a session with auto loss scaling enabled, and with an optimizer
    with a constant loss scale value. Observe an error from the auto loss
    scale transform
    """
    builder = popart.Builder()

    t0 = builder.addInputTensor("FLOAT", [2, 2])
    t1_data = np.random.rand(2, 2).astype(np.float32)
    t1 = builder.addInitializedInputTensor(t1_data)
    out = builder.aiOnnx.matmul([t0, t1])
    loss = builder.aiGraphcore.identityloss([out])

    makeLossScalingTensorConst = True
    optimizer = popart.SGD({"lossScaling": (2, makeLossScalingTensorConst)})

    opts = popart.SessionOptions()
    opts.automaticLossScalingSettings.enabled = True
    opts.automaticLossScalingSettings.binEdgeLocation = 0.5
    opts.automaticLossScalingSettings.thresholdUpperCountProportion = 0.2

    with pytest.raises(popart.popart_exception) as e_info:
        session = popart.TrainingSession(builder.getModelProto(),
                                         deviceInfo=tu.create_test_device(),
                                         dataFlow=popart.DataFlow(1, []),
                                         loss=loss,
                                         optimizer=optimizer,
                                         userOptions=opts)
    assert e_info.value.args[0].endswith(
        "The optimizer must have non-const loss scaling")


def test_auto_loss_scaling_with_no_tracked_tensors():
    """
    Build a model with ops, the outputs of which the auto loss scale transform
    does not decide to 'track'. Observe an error from the auto loss scale
    transform
    """
    builder = popart.Builder()

    t0 = builder.addInputTensor("FLOAT", [2, 2])
    out = builder.aiOnnx.relu([t0])
    loss = builder.aiGraphcore.identityloss([out])

    optimizer = popart.SGD({"lossScaling": (2, False)})

    opts = popart.SessionOptions()
    opts.automaticLossScalingSettings.enabled = True
    opts.automaticLossScalingSettings.binEdgeLocation = 0.5
    opts.automaticLossScalingSettings.thresholdUpperCountProportion = 0.2

    with pytest.raises(popart.popart_exception) as e_info:
        session = popart.TrainingSession(builder.getModelProto(),
                                         deviceInfo=tu.create_test_device(),
                                         dataFlow=popart.DataFlow(1, [loss]),
                                         loss=loss,
                                         optimizer=optimizer,
                                         userOptions=opts)
    assert e_info.value.args[0].endswith("No tracked tensors were found")


def test_auto_loss_scaling_histogram_schedule_priority():
    """
    Compile a minimal possible model with ALS, and check the schedule priority
    of the HistorgramOps have the correct schedule priority according to the
    schedule options.

    If delayVarUpdates and not using explicit Ir and
    scheduleNonWeightUpdateGradientConsumersEarly, then the schedule priority
    should be max.

    If delayVarUpdates and not using explicit Ir and not
    scheduleNonWeightUpdateGradientConsumersEarly, then we make no assertions
    about the schedule priority (it should be whatever it naturally is).

    If not delayVarUpdates or using explicit Ir, then the VarUpdateOps will not
    have min schedule priority anyway, so again we make no assertions.
    """

    optimizer = popart.SGD({"lossScaling": (1.0, False)})

    opts = popart.SessionOptions()
    opts.automaticLossScalingSettings.enabled = True
    # Following options are to test that Histogram ops will have correct
    # schedule priority.
    opts.delayVarUpdates = True
    opts.scheduleNonWeightUpdateGradientConsumersEarly = True
    opts.explicitRecomputation = False
    opts.enableExplicitMainLoops = False
    opts.useHostCopyOps = False
    # For easier inspection of the Ir
    opts.enableOutlining = False

    loss, proto, _, _, _, _, _ = getModelProto()
    bps = 4

    loss_scale_id = "finalLossScale"
    gradient_anchor_ids = ["Gradient___init_input/1", "Gradient___init_input"]
    anchor_ids = gradient_anchor_ids + [loss_scale_id]

    session = popart.TrainingSession(fnModel=proto,
                                     deviceInfo=tu.create_test_device(),
                                     dataFlow=popart.DataFlow(bps, anchor_ids),
                                     loss=loss,
                                     optimizer=optimizer,
                                     userOptions=opts)

    bigNumber = 10000000000000000
    checkExpected = lambda p: p > bigNumber

    ir = json.loads(session._serializeIr(popart.IrSerializationFormat.JSON))
    ops = ir["maingraph"]

    # Check that ALS transform actually ran by seeing if the HistogramOps exist
    # at all.
    assert len(list(filter(lambda op: op["type"] == "Histogram", ops))) > 0

    # Histogram ops should have max schedule priority. Here we just test against
    # a very high number, as we do not know exactly what the C++ double max
    # value is.
    assert all(
        map(
            lambda op: checkExpected(
                np.double(op["attributes"]["schedulePriority"])),
            filter(lambda op: op["type"] == "Histogram", ops)))


def test_auto_loss_scaling_expected_loss_scale_tensor_values():
    """
    Pick a very small loss scale value so that gradient tensor values occupy
    the lower part of the fp16 dynamic range.
    Observe that this is the case, and that the loss scale factor is adjusted
    as expected after each weight update.
    """
    init_loss_scale = np.finfo(np.float16).eps * 2
    optimizer = popart.SGD({"lossScaling": (init_loss_scale, False)})

    opts = popart.SessionOptions()
    opts.automaticLossScalingSettings.enabled = True
    opts.automaticLossScalingSettings.binEdgeLocation = 0.5
    opts.automaticLossScalingSettings.thresholdUpperCountProportion = 0.2

    loss, proto, t0, t_shape, label, label_shape, _ = getModelProto()
    bps = 4

    loss_scale_id = "finalLossScale"
    gradient_anchor_ids = ["Gradient___init_input/1", "Gradient___init_input"]
    anchor_ids = gradient_anchor_ids + [loss_scale_id]

    session = popart.TrainingSession(fnModel=proto,
                                     deviceInfo=tu.create_test_device(),
                                     dataFlow=popart.DataFlow(bps, anchor_ids),
                                     loss=loss,
                                     optimizer=optimizer,
                                     userOptions=opts)
    session.prepareDevice()
    session.weightsFromHost()
    anchors = session.initAnchorArrays()

    t0_data = np.random.rand(bps, *t_shape).astype(np.float16)
    label_data = np.random.randint(0, label_shape[0],
                                   bps * label_shape[0]).astype(np.int32)
    stepio = popart.PyStepIO({t0: t0_data, label: label_data}, anchors)
    session.run(stepio)

    # Manually determine the direction of the loss scale update
    f16_max = np.finfo(np.float16).max
    histogram_bin_edges = [-1, 0.5 * f16_max, f16_max]
    prev_loss_scale = init_loss_scale
    for i in range(bps):
        num_small_grad_elements = 0
        num_large_grad_elements = 0
        print(anchors[loss_scale_id][i])
        for id in gradient_anchor_ids:
            abs_grad_data = np.abs(anchors[id][i])
            hist, _ = np.histogram(abs_grad_data, histogram_bin_edges)
            num_small_grad_elements += hist[0]
            num_large_grad_elements += hist[1]

        proportion_small_grad_elements = num_small_grad_elements / (
            num_small_grad_elements + num_large_grad_elements)
        # Observe that the proportion of small grad elements is large, as we
        # have started with a very small loss scale
        assert proportion_small_grad_elements > 0.9

        # Therefore the loss scale will increase for each batch in the step
        if i > 0:
            assert anchors[loss_scale_id][i] == 2 * prev_loss_scale
            prev_loss_scale = anchors[loss_scale_id][i]


@tu.requires_ipu_model
def test_auto_loss_scaling_and_continuous_update_pipelining():
    """
    Create a Session with automatic loss scaling and pipelining
    enabled, but gradient accumulation disabled, and see that an
    incompatibility error is thrown.
    """
    builder = popart.Builder()

    t0 = builder.addInputTensor("FLOAT", [2, 2])
    mm0 = builder.aiOnnx.matmul([t0, t0])
    loss = builder.aiGraphcore.identityloss([mm0])

    optimizer = popart.SGD({"lossScaling": (2, False)})

    builder.virtualGraph(mm0, 0)
    builder.virtualGraph(loss, 0)

    opts = popart.SessionOptions()
    opts.automaticLossScalingSettings.enabled = True
    opts.automaticLossScalingSettings.binEdgeLocation = 0.5
    opts.automaticLossScalingSettings.thresholdUpperCountProportion = 0.2
    opts.enablePipelining = True
    opts.enableGradientAccumulation = False
    opts.virtualGraphMode = popart.VirtualGraphMode.Manual

    with pytest.raises(popart.popart_exception) as e_info:
        session = popart.TrainingSession(builder.getModelProto(),
                                         deviceInfo=tu.create_test_device(2),
                                         dataFlow=popart.DataFlow(1, [loss]),
                                         loss=loss,
                                         optimizer=optimizer,
                                         userOptions=opts)
    assert e_info.value.args[0].endswith(
        "Automatic loss scaling is not currently supported when the 'enablePipelining' SessionOption is set to 'true', but the 'enableGradientAccumulation' SessionOption is set to 'false'"
    )


@pytest.mark.parametrize("thresholdUpperCountProportion",
                         [-0.1, 0.0, 0.5, 1.0, 1.1])
def test_auto_loss_scaling_threshold_upper_count_proportion_range(
        thresholdUpperCountProportion):
    """Test if an error is thrown if the thresholdUpperCountProportion 
    hyperparameter is outside [0, 1].
    """
    builder = popart.Builder()
    t0 = builder.addInputTensor("FLOAT", [2, 2])
    t1_data = np.random.rand(2, 2).astype(np.float32)
    t1 = builder.addInitializedInputTensor(t1_data)
    mm0 = builder.aiOnnx.matmul([t0, t1])
    loss = builder.aiGraphcore.identityloss([mm0])

    optimizer = popart.SGD({"lossScaling": (2, False)})

    opts = popart.SessionOptions()
    opts.automaticLossScalingSettings.enabled = True
    opts.automaticLossScalingSettings.binEdgeLocation = 0.5
    opts.automaticLossScalingSettings.thresholdUpperCountProportion = thresholdUpperCountProportion

    with ExitStack() as stack:
        e_info = None
        if not 0 <= thresholdUpperCountProportion <= 1:
            e_info = stack.enter_context(pytest.raises(
                popart.popart_exception))

        session = popart.TrainingSession(builder.getModelProto(),
                                         deviceInfo=tu.create_test_device(),
                                         dataFlow=popart.DataFlow(1, [loss]),
                                         loss=loss,
                                         optimizer=optimizer,
                                         userOptions=opts)
        session.prepareDevice()

        if e_info:
            assert e_info.value.args[0].startswith(
                "Out of range value for 'thresholdUpperCountProportion'.")


@pytest.mark.parametrize("binEdgeLocation", [-0.1, 0.0, 0.5, 1.0, 1.1])
def test_auto_loss_scaling_bin_edge_factor_range(binEdgeLocation):
    """Test if an error is thrown if the binEdgeLocation hyperparameter is 
    outside [0, 1].
    """
    builder = popart.Builder()
    t0 = builder.addInputTensor("FLOAT", [2, 2])
    t1_data = np.random.rand(2, 2).astype(np.float32)
    t1 = builder.addInitializedInputTensor(t1_data)
    mm0 = builder.aiOnnx.matmul([t0, t1])
    loss = builder.aiGraphcore.identityloss([mm0])

    optimizer = popart.SGD({"lossScaling": (2, False)})

    opts = popart.SessionOptions()
    opts.automaticLossScalingSettings.enabled = True
    opts.automaticLossScalingSettings.binEdgeLocation = binEdgeLocation
    opts.automaticLossScalingSettings.thresholdUpperCountProportion = 0.2

    with ExitStack() as stack:
        e_info = None
        if not 0 <= binEdgeLocation <= 1:
            e_info = stack.enter_context(pytest.raises(
                popart.popart_exception))

        session = popart.TrainingSession(builder.getModelProto(),
                                         deviceInfo=tu.create_test_device(),
                                         dataFlow=popart.DataFlow(1, [loss]),
                                         loss=loss,
                                         optimizer=optimizer,
                                         userOptions=opts)

        if e_info:
            assert e_info.value.args[0].startswith(
                "[AutomaticLossScale transform] Out of range value for 'binEdgeLocation'."
            )
        else:
            session.prepareDevice()


def test_auto_loss_scaling_with_mixed_precision_trackable_tensors():
    """
    Create a Session with automatic loss scaling and a model that contains
    both fp32 and fp16 initializers, and see that no incompatibility error is
    thrown.
    """
    builder = popart.Builder()
    t0 = builder.addInputTensor("FLOAT", [2, 2])
    t1_data = np.random.rand(2, 2).astype(np.float32)
    t1 = builder.addInitializedInputTensor(t1_data)
    mm0 = builder.aiOnnx.matmul([t0, t1])
    t2 = builder.aiOnnx.cast([mm0], "FLOAT16")
    t3 = builder.addInputTensor("FLOAT16", [2, 2])
    mm1 = builder.aiOnnx.matmul([t2, t3])
    loss = builder.aiGraphcore.identityloss([mm1])

    optimizer = popart.SGD({"lossScaling": (2, False)})

    opts = popart.SessionOptions()
    opts.automaticLossScalingSettings.enabled = True
    opts.automaticLossScalingSettings.binEdgeLocation = 0.5
    opts.automaticLossScalingSettings.thresholdUpperCountProportion = 0.2

    session = popart.TrainingSession(builder.getModelProto(),
                                     deviceInfo=tu.create_test_device(),
                                     dataFlow=popart.DataFlow(1, [loss]),
                                     loss=loss,
                                     optimizer=optimizer,
                                     userOptions=opts)
    session.prepareDevice()


@pytest.mark.parametrize("optimizer", getOptimizers())
def test_auto_loss_scaling_identical_weight_updates(tmpdir, optimizer):
    run_automatic_loss_scaling_comparison_test(tmpdir, optimizer)


@tu.requires_ipu_model
@pytest.mark.parametrize("optimizer", getOptimizers())
def test_auto_loss_scaling_identical_weight_updates_sharded(tmpdir, optimizer):
    run_automatic_loss_scaling_comparison_test(tmpdir,
                                               shard=True,
                                               optimizer=optimizer)


@pytest.mark.parametrize("optimizer", getOptimizers())
def test_auto_loss_scaling_identical_weight_updates_grad_accumulation(
        tmpdir, optimizer):
    run_automatic_loss_scaling_comparison_test(tmpdir,
                                               grad_accumulate=True,
                                               optimizer=optimizer)


@tu.requires_ipu_model
@pytest.mark.parametrize("optimizer", getOptimizers())
def test_auto_loss_scaling_identical_weight_updates_grad_accumulation_shard(
        tmpdir, optimizer):
    run_automatic_loss_scaling_comparison_test(tmpdir,
                                               grad_accumulate=True,
                                               shard=True,
                                               optimizer=optimizer)


@tu.requires_ipu_model
@pytest.mark.parametrize("optimizer", getOptimizers())
def test_auto_loss_scaling_identical_weight_updates_grad_accumulation_pipeline(
        tmpdir, optimizer):
    run_automatic_loss_scaling_comparison_test(tmpdir,
                                               grad_accumulate=True,
                                               shard=True,
                                               pipeline=True,
                                               optimizer=optimizer)


def test_loss_scale_updates_with_grad_accumulation_correctness():
    """
    Run a session that has gradient accumulation and auto loss scaling
    enabled. Verify that:
    1. the loss scale is updated in the outer loop
    2. the gradient statistics are accumulated in the inner loop
    """
    init_ls = 1000.0
    optimizer = popart.SGD({"lossScaling": (init_ls, False)})
    accumulation_factor = 5

    opts = popart.SessionOptions()
    opts.automaticLossScalingSettings.enabled = True
    opts.automaticLossScalingSettings.binEdgeLocation = 0.5
    opts.automaticLossScalingSettings.thresholdUpperCountProportion = 0.2
    opts.enableGradientAccumulation = True
    opts.accumulationFactor = accumulation_factor

    loss, proto, t0, t_shape, label, label_shape, _ = getModelProto()
    bps = 4

    accl_stats_id = "Accum___autoLossScaleStats"
    ls_id = "finalLossScale"
    session = popart.TrainingSession(fnModel=proto,
                                     deviceInfo=tu.create_test_device(),
                                     dataFlow=popart.DataFlow(
                                         bps, [accl_stats_id, ls_id]),
                                     loss=loss,
                                     optimizer=optimizer,
                                     userOptions=opts)
    session.prepareDevice()
    session.weightsFromHost()
    anchors = session.initAnchorArrays()

    step_size = bps * accumulation_factor
    t0_data = 10000.0 * np.random.rand(step_size, *t_shape).astype(np.float16)
    label_data = np.random.randint(0, label_shape[0],
                                   step_size * label_shape[0]).astype(np.int32)
    inputs = {t0: t0_data, label: label_data}

    session.run(popart.PyStepIO(inputs, anchors))

    # 1.
    prev_ls0 = init_ls
    for i in range(len(anchors[ls_id])):
        ls0 = anchors[ls_id][i][0]
        for j in range(len(anchors[ls_id][0])):
            # loss scale is the same within inner loops
            assert anchors[ls_id][i][j] == ls0

        if i > 0:
            # loss scale has been updated between outer loops
            assert ls0 != prev_ls0

    # 2.
    for batch_stats in anchors[accl_stats_id]:
        for minibatch_idx, minibatch in enumerate(batch_stats):
            # how many elements are there in the grad tensors to be tracked?
            # np.prod(t_shape) for each of:
            #  - conv weights grad
            #  - conv data grad
            #  - matmul weights grad
            num_elms_gradstats = 3 * np.prod(t_shape)
            assert np.sum(minibatch) == num_elms_gradstats * (minibatch_idx +
                                                              1)


def test_auto_loss_scaling_update_learning_rate_without_resetting_loss_scale():
    """
    1. get TrainingSession with a.l.s
    2. anchor loss scale, updated loss scale tensors
    3. session.run, get reference loss scale, updated loss scale tensors
    4. see that loss scale stays same, but updated loss scale changes
    5. update optimizer without updating loss scale
    6. session.run, get new loss scale, updated loss scale tensors
    7. see new loss scale tensors are same as before
    """
    np.random.seed(2)
    init_ls = 1000.0
    optimizer = popart.SGD({
        "lossScaling": (init_ls, False),
        "defaultLearningRate": (20, False)
    })
    opts = popart.SessionOptions()
    opts.automaticLossScalingSettings.enabled = True

    loss, proto, t0, t_shape, label, label_shape, _ = getModelProto()
    bps = 6

    ls_id = "lossScaling_FLOAT16"
    final_ls_id = "finalLossScale"

    session = popart.TrainingSession(fnModel=proto,
                                     deviceInfo=tu.create_test_device(),
                                     dataFlow=popart.DataFlow(
                                         bps, [ls_id, final_ls_id]),
                                     loss=loss,
                                     optimizer=optimizer,
                                     userOptions=opts)
    session.prepareDevice()
    session.weightsFromHost()
    t0_data = 10000.0 * np.random.rand(bps, *t_shape).astype(np.float16)
    label_data = np.random.randint(0, label_shape[0],
                                   bps * label_shape[0]).astype(np.int32)
    inputs = {t0: t0_data, label: label_data}

    anchors = session.initAnchorArrays()
    session.run(popart.PyStepIO(inputs, anchors))

    # Loss scale optimizer tensor is unchanged over step
    for ls in anchors[ls_id]:
        assert ls == init_ls

    # Final loss scale changes over step
    for i, final_ls in enumerate(anchors[final_ls_id]):
        if i > 0:
            assert final_ls != anchors[final_ls_id][i - 1]

    # Update the learning rate
    new_optimizer = popart.SGD({
        "lossScaling": (init_ls, False),
        "defaultLearningRate": (12, False)
    })
    session.updateOptimizerFromHost(new_optimizer)

    session.run(popart.PyStepIO(inputs, anchors))

    # Loss scale optimizer tensor is unchanged (over step, and vs. last step)
    for ls in anchors[ls_id]:
        assert ls == init_ls
