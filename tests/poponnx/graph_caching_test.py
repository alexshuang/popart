import numpy as np
import pytest
import poponnx
import test_util as tu


def test_convolution_cached_by_default():
    """
    In this test we check that the default behaviour is for convolutions to be
    cached.
    """

    builder = poponnx.Builder()

    data_shape = poponnx.TensorInfo("FLOAT", [1, 2, 4, 4])
    filt_shape = poponnx.TensorInfo("FLOAT", [2, 2, 3, 3])

    i1 = builder.addInputTensor(data_shape)
    i2 = builder.addInputTensor(filt_shape)
    c1 = builder.aiOnnx.conv([i1, i2],
                             dilations=[1, 1],
                             pads=[1, 1, 1, 1],
                             strides=[1, 1])
    o = builder.aiOnnx.conv([c1, i2],
                            dilations=[1, 1],
                            pads=[1, 1, 1, 1],
                            strides=[1, 1])
    builder.addOutputTensor(o)

    proto = builder.getModelProto()

    anchor_names = ['d__' + i1, 'd__' + i2]
    dataFlow = \
        poponnx.DataFlow(1,
                         {anchor_names[0] : poponnx.AnchorReturnType("ALL"),
                          anchor_names[1] : poponnx.AnchorReturnType("ALL")})
    optimizer = poponnx.ConstSGD(0.01)
    losses = [poponnx.L1Loss(o, "l1LossVal", 0.1)]

    opts = poponnx.SessionOptionsCore()
    opts.reportOptions = {"doLayerWiseBreakdown": "true"}

    session = poponnx.Session(
        fnModel=proto,
        dataFeed=dataFlow,
        losses=losses,
        optimizer=optimizer,
        userOptions=opts)

    session.setDevice(tu.get_ipu_model(compileIPUCode=False))
    anchors = session.initAnchorArrays()

    session.prepareDevice()

    data = np.ones(data_shape.shape(), dtype=np.float32)
    filt = np.ones(filt_shape.shape(), dtype=np.float32)

    inputs = {i1: data, i2: filt}
    stepio = poponnx.PyStepIO(inputs, anchors)

    session.train(stepio)
    session.weightsFromHost()
    session.optimizerFromHost()

    # Check that there is only one convolution computation set.
    summaryReport = session.getSummaryReport()
    computeSets = tu.get_compute_sets_from_report(summaryReport)

    num_3x3_convolutions = tu.get_compute_set_regex_count(
        r'^[0-9]+/Conv_3x3/Convolve$', computeSets)
    num_4x4_convolutions = tu.get_compute_set_regex_count(
        r'^[0-9]+/Conv_4x4/Convolve$', computeSets)
    # There should be only one convolution of each type
    assert (num_3x3_convolutions == 1)
    assert (num_4x4_convolutions == 1)


def test_convolution_cached_set_to_true():
    """
    In this test we check that the convolutions are cached when they asked to be
    cached.
    """

    builder = poponnx.Builder()

    data_shape = poponnx.TensorInfo("FLOAT", [1, 2, 4, 4])
    filt_shape = poponnx.TensorInfo("FLOAT", [2, 2, 3, 3])

    i1 = builder.addInputTensor(data_shape)
    i2 = builder.addInputTensor(filt_shape)
    c1 = builder.aiOnnx.conv([i1, i2], [1, 1], 1, [], [1, 1, 1, 1], [1, 1])
    builder.addNodeAttribute("__cache_operation", True, set(c1))

    o = builder.aiOnnx.conv([c1, i2], [1, 1], 1, [], [1, 1, 1, 1], [1, 1])
    builder.addNodeAttribute("__cache_operation", True, set(o))

    builder.addOutputTensor(o)

    proto = builder.getModelProto()

    anchor_names = ['d__' + i1, 'd__' + i2]
    dataFlow = \
        poponnx.DataFlow(1,
                         {anchor_names[0] : poponnx.AnchorReturnType("ALL"),
                          anchor_names[1] : poponnx.AnchorReturnType("ALL")})
    optimizer = poponnx.ConstSGD(0.01)
    losses = [poponnx.L1Loss(o, "l1LossVal", 0.1)]

    opts = poponnx.SessionOptionsCore()
    opts.reportOptions = {"doLayerWiseBreakdown": "true"}

    session = poponnx.Session(
        fnModel=proto,
        dataFeed=dataFlow,
        losses=losses,
        optimizer=optimizer,
        userOptions=opts)

    session.setDevice(tu.get_ipu_model(compileIPUCode=False))
    anchors = session.initAnchorArrays()

    session.prepareDevice()

    data = np.ones(data_shape.shape(), dtype=np.float32)
    filt = np.ones(filt_shape.shape(), dtype=np.float32)

    inputs = {i1: data, i2: filt}
    stepio = poponnx.PyStepIO(inputs, anchors)

    session.train(stepio)
    session.weightsFromHost()
    session.optimizerFromHost()

    # Check that there is only one convolution computation set.
    summaryReport = session.getSummaryReport()
    computeSets = tu.get_compute_sets_from_report(summaryReport)

    num_3x3_convolutions = tu.get_compute_set_regex_count(
        r'^[0-9]+/Conv_3x3/Convolve$', computeSets)
    num_4x4_convolutions = tu.get_compute_set_regex_count(
        r'^[0-9]+/Conv_4x4/Convolve$', computeSets)
    # There should be only one convolution of each type
    assert (num_3x3_convolutions == 1)
    assert (num_4x4_convolutions == 1)


def test_convolution_cached_set_to_false():
    """
    In this test we check that the convolutions are not cached when they asked
    to not be cached.
    """

    builder = poponnx.Builder()

    data_shape = poponnx.TensorInfo("FLOAT", [1, 2, 4, 4])
    filt_shape = poponnx.TensorInfo("FLOAT", [2, 2, 3, 3])

    i1 = builder.addInputTensor(data_shape)
    i2 = builder.addInputTensor(filt_shape)
    c1 = builder.aiOnnx.conv([i1, i2], [1, 1], 1, [], [1, 1, 1, 1], [1, 1])
    builder.addNodeAttribute("__cache_operation", False, set(c1))

    o = builder.aiOnnx.conv([c1, i2], [1, 1], 1, [], [1, 1, 1, 1], [1, 1])
    builder.addNodeAttribute("__cache_operation", False, set(o))

    builder.addOutputTensor(o)

    proto = builder.getModelProto()

    anchor_names = ['d__' + i1, 'd__' + i2]
    dataFlow = \
        poponnx.DataFlow(1,
                         {anchor_names[0] : poponnx.AnchorReturnType("ALL"),
                          anchor_names[1] : poponnx.AnchorReturnType("ALL")})
    optimizer = poponnx.ConstSGD(0.01)
    losses = [poponnx.L1Loss(o, "l1LossVal", 0.1)]

    opts = poponnx.SessionOptionsCore()
    opts.reportOptions = {"doLayerWiseBreakdown": "true"}

    session = poponnx.Session(
        fnModel=proto,
        dataFeed=dataFlow,
        losses=losses,
        optimizer=optimizer,
        userOptions=opts)

    session.setDevice(tu.get_ipu_model(compileIPUCode=False))
    anchors = session.initAnchorArrays()

    session.prepareDevice()

    data = np.ones(data_shape.shape(), dtype=np.float32)
    filt = np.ones(filt_shape.shape(), dtype=np.float32)

    inputs = {i1: data, i2: filt}
    stepio = poponnx.PyStepIO(inputs, anchors)

    session.train(stepio)
    session.weightsFromHost()
    session.optimizerFromHost()

    # Check that there is only one convolution computation set.
    summaryReport = session.getSummaryReport()
    computeSets = tu.get_compute_sets_from_report(summaryReport)

    num_3x3_convolutions = tu.get_compute_set_regex_count(
        r'^[0-9]+/Conv_3x3/Convolve$', computeSets)
    num_4x4_convolutions = tu.get_compute_set_regex_count(
        r'^[0-9]+/Conv_4x4/Convolve$', computeSets)
    # Two 3x3 convolutions (bwd + fwd) for each convolution in the graph
    assert (num_3x3_convolutions == 4)
    # Two updates
    assert (num_4x4_convolutions == 2)


def test_convolution_some_convolutions_cached():
    """
    In this test we check that the correctness of having some convolutions
    cached and some not.
    """

    builder = poponnx.Builder()

    data_shape = poponnx.TensorInfo("FLOAT", [1, 2, 4, 4])
    filt_shape = poponnx.TensorInfo("FLOAT", [2, 2, 3, 3])

    i1 = builder.addInputTensor(data_shape)
    i2 = builder.addInputTensor(filt_shape)
    c1 = builder.aiOnnx.conv([i1, i2], [1, 1], 1, [], [1, 1, 1, 1], [1, 1])
    c2 = builder.aiOnnx.conv([c1, i2], [1, 1], 1, [], [1, 1, 1, 1], [1, 1])
    builder.addNodeAttribute("__cache_operation", False, set(c2))
    o = builder.aiOnnx.conv([c2, i2],
                            dilations=[1, 1],
                            pads=[1, 1, 1, 1],
                            strides=[1, 1])
    builder.addOutputTensor(o)

    proto = builder.getModelProto()

    anchor_names = ['d__' + i1, 'd__' + i2]
    dataFlow = \
        poponnx.DataFlow(1,
                         {anchor_names[0] : poponnx.AnchorReturnType("ALL"),
                          anchor_names[1] : poponnx.AnchorReturnType("ALL")})
    optimizer = poponnx.ConstSGD(0.01)
    losses = [poponnx.L1Loss(o, "l1LossVal", 0.1)]

    opts = poponnx.SessionOptionsCore()
    opts.reportOptions = {"doLayerWiseBreakdown": "true"}

    session = poponnx.Session(
        fnModel=proto,
        dataFeed=dataFlow,
        losses=losses,
        optimizer=optimizer,
        userOptions=opts)

    session.setDevice(tu.get_ipu_model(compileIPUCode=False))
    anchors = session.initAnchorArrays()

    session.prepareDevice()

    data = np.ones(data_shape.shape(), dtype=np.float32)
    filt = np.ones(filt_shape.shape(), dtype=np.float32)

    inputs = {i1: data, i2: filt}
    stepio = poponnx.PyStepIO(inputs, anchors)

    session.train(stepio)
    session.weightsFromHost()
    session.optimizerFromHost()

    # Check that there is only one convolution computation set.
    summaryReport = session.getSummaryReport()
    computeSets = tu.get_compute_sets_from_report(summaryReport)

    num_3x3_convolutions = tu.get_compute_set_regex_count(
        r'^[0-9]+/Conv_3x3/Convolve$', computeSets)
    num_4x4_convolutions = tu.get_compute_set_regex_count(
        r'^[0-9]+/Conv_4x4/Convolve$', computeSets)
    # Two 3x3 convolutions (bwd + fwd) for the uncached convolution in the graph and then 1 for all the others
    assert (num_3x3_convolutions == 3)
    # One cached and one uncached update
    assert (num_4x4_convolutions == 2)


def test_convolution_disable_all():
    """
    In this test we check that the correctness of having some convolutions
    cached and some not.
    """

    builder = poponnx.Builder()

    data_shape = poponnx.TensorInfo("FLOAT", [1, 2, 4, 4])
    filt_shape = poponnx.TensorInfo("FLOAT", [2, 2, 3, 3])

    i1 = builder.addInputTensor(data_shape)
    i2 = builder.addInputTensor(filt_shape)
    c1 = builder.aiOnnx.conv([i1, i2],
                             dilations=[1, 1],
                             pads=[1, 1, 1, 1],
                             strides=[1, 1])
    c2 = builder.aiOnnx.conv([c1, i2],
                             dilations=[1, 1],
                             pads=[1, 1, 1, 1],
                             strides=[1, 1])
    o = builder.aiOnnx.conv([c2, i2],
                            dilations=[1, 1],
                            pads=[1, 1, 1, 1],
                            strides=[1, 1])
    builder.addOutputTensor(o)

    proto = builder.getModelProto()

    anchor_names = ['d__' + i1, 'd__' + i2]
    dataFlow = \
        poponnx.DataFlow(1,
                         {anchor_names[0] : poponnx.AnchorReturnType("ALL"),
                          anchor_names[1] : poponnx.AnchorReturnType("ALL")})
    optimizer = poponnx.ConstSGD(0.01)
    losses = [poponnx.L1Loss(o, "l1LossVal", 0.1)]

    opts = poponnx.SessionOptionsCore()
    opts.reportOptions = {"doLayerWiseBreakdown": "true"}
    opts.enableConvolutionGraphCaching = False

    session = poponnx.Session(
        fnModel=proto,
        dataFeed=dataFlow,
        losses=losses,
        optimizer=optimizer,
        userOptions=opts)

    session.setDevice(tu.get_ipu_model(compileIPUCode=False))
    anchors = session.initAnchorArrays()

    session.prepareDevice()

    data = np.ones(data_shape.shape(), dtype=np.float32)
    filt = np.ones(filt_shape.shape(), dtype=np.float32)

    inputs = {i1: data, i2: filt}
    stepio = poponnx.PyStepIO(inputs, anchors)

    session.train(stepio)
    session.weightsFromHost()
    session.optimizerFromHost()

    # Check that there is only one convolution computation set.
    summaryReport = session.getSummaryReport()
    computeSets = tu.get_compute_sets_from_report(summaryReport)

    num_3x3_convolutions = tu.get_compute_set_regex_count(
        r'^[0-9]+/Conv_3x3/Convolve$', computeSets)
    num_4x4_convolutions = tu.get_compute_set_regex_count(
        r'^[0-9]+/Conv_4x4/Convolve$', computeSets)
    # Two 3x3 convolutions (bwd + fwd) for each convolution
    assert (num_3x3_convolutions == 6)
    # Updates
    assert (num_4x4_convolutions == 3)


def test_matmul_infer_cached_by_default():
    """
    In this test we check that the default behaviour is for matmul to be
    cached.
    """

    builder = poponnx.Builder()

    matmul_lhs_shape = poponnx.TensorInfo("FLOAT", [2, 3])
    matmul_rhs_shape = poponnx.TensorInfo("FLOAT", [3, 4])

    i1 = builder.addInputTensor(matmul_lhs_shape)
    i2 = builder.addInputTensor(matmul_rhs_shape)
    i3 = builder.addInputTensor(matmul_lhs_shape)
    i4 = builder.addInputTensor(matmul_rhs_shape)

    c1 = builder.aiOnnx.matmul([i1, i2])
    c2 = builder.aiOnnx.matmul([i3, i4])

    o = builder.aiOnnx.add([c1, c2])

    builder.addOutputTensor(o)

    proto = builder.getModelProto()

    anchor_names = [o]
    dataFlow = poponnx.DataFlow(1, {o: poponnx.AnchorReturnType("ALL")})

    opts = poponnx.SessionOptionsCore()
    opts.logging = {'all': 'TRACE'}
    opts.reportOptions = {"doLayerWiseBreakdown": "true"}

    session = poponnx.Session(
        fnModel=proto, dataFeed=dataFlow, userOptions=opts)

    session.setDevice(tu.get_ipu_model(compileIPUCode=False))
    anchors = session.initAnchorArrays()

    session.prepareDevice()

    matmul1_lhs = np.ones(matmul_lhs_shape.shape(), dtype=np.float32)
    matmul1_rhs = np.ones(matmul_rhs_shape.shape(), dtype=np.float32)
    matmul2_lhs = np.ones(matmul_lhs_shape.shape(), dtype=np.float32)
    matmul2_rhs = np.ones(matmul_rhs_shape.shape(), dtype=np.float32)

    inputs = {
        i1: matmul1_lhs,
        i2: matmul1_rhs,
        i3: matmul2_lhs,
        i4: matmul2_rhs
    }
    stepio = poponnx.PyStepIO(inputs, anchors)

    session.infer(stepio)

    # Check that there is only one convolution computation set.
    summaryReport = session.getSummaryReport()

    computeSets = tu.get_compute_sets_from_report(summaryReport)

    num_matmuls = tu.get_compute_set_regex_count(r'^[0-9]+/Conv_1/Convolve$',
                                                 computeSets)
    # There should be only one matmul
    assert (num_matmuls == 1)


def test_matmul_infer_not_cached():
    """
    In this test we check that the we don't cache when the option is set
    """

    builder = poponnx.Builder()

    matmul_lhs_shape = poponnx.TensorInfo("FLOAT", [2, 3])
    matmul_rhs_shape = poponnx.TensorInfo("FLOAT", [3, 4])

    i1 = builder.addInputTensor(matmul_lhs_shape)
    i2 = builder.addInputTensor(matmul_rhs_shape)
    i3 = builder.addInputTensor(matmul_lhs_shape)
    i4 = builder.addInputTensor(matmul_rhs_shape)

    c1 = builder.aiOnnx.matmul([i1, i2])
    builder.addNodeAttribute("__cache_operation", False, set(c1))
    c2 = builder.aiOnnx.matmul([i3, i4])
    builder.addNodeAttribute("__cache_operation", False, set(c2))

    o = builder.aiOnnx.add([c1, c2])

    builder.addOutputTensor(o)

    proto = builder.getModelProto()

    anchor_names = [o]
    dataFlow = poponnx.DataFlow(1, {o: poponnx.AnchorReturnType("ALL")})

    opts = poponnx.SessionOptionsCore()
    opts.logging = {'all': 'TRACE'}
    opts.reportOptions = {"doLayerWiseBreakdown": "true"}

    session = poponnx.Session(
        fnModel=proto, dataFeed=dataFlow, userOptions=opts)

    session.setDevice(tu.get_ipu_model(compileIPUCode=False))
    anchors = session.initAnchorArrays()

    session.prepareDevice()

    matmul1_lhs = np.ones(matmul_lhs_shape.shape(), dtype=np.float32)
    matmul1_rhs = np.ones(matmul_rhs_shape.shape(), dtype=np.float32)
    matmul2_lhs = np.ones(matmul_lhs_shape.shape(), dtype=np.float32)
    matmul2_rhs = np.ones(matmul_rhs_shape.shape(), dtype=np.float32)

    inputs = {
        i1: matmul1_lhs,
        i2: matmul1_rhs,
        i3: matmul2_lhs,
        i4: matmul2_rhs
    }
    stepio = poponnx.PyStepIO(inputs, anchors)

    session.infer(stepio)

    summaryReport = session.getSummaryReport()

    computeSets = tu.get_compute_sets_from_report(summaryReport)

    num_matmuls = tu.get_compute_set_regex_count(r'^[0-9]+/Conv_1/Convolve$',
                                                 computeSets)
    # There should be only two matmul
    assert (num_matmuls == 2)


def test_matmul_train_cached_by_default():
    """
    In this test we check that the default behaviour is for matmul to be
    cached.
    """

    builder = poponnx.Builder()

    matmul1_lhs_shape = poponnx.TensorInfo("FLOAT", [2, 3])
    matmul1_rhs_shape = poponnx.TensorInfo("FLOAT", [3, 4])

    matmul2_lhs_shape = poponnx.TensorInfo("FLOAT", [2, 3])
    matmul2_rhs_shape = poponnx.TensorInfo("FLOAT", [3, 4])

    i1 = builder.addInputTensor(matmul1_lhs_shape)
    i2 = builder.addInputTensor(matmul1_rhs_shape)
    i3 = builder.addInputTensor(matmul2_lhs_shape)
    i4 = builder.addInputTensor(matmul2_rhs_shape)

    c1 = builder.aiOnnx.matmul([i1, i2])
    c2 = builder.aiOnnx.matmul([i3, i4])

    o = builder.aiOnnx.add([c1, c2])

    builder.addOutputTensor(o)

    proto = builder.getModelProto()

    anchor_names = ['d__' + i1, 'd__' + i2, 'd__' + i3, 'd__' + i4]
    dataFlow = poponnx.DataFlow(
        1, {
            anchor_names[0]: poponnx.AnchorReturnType("ALL"),
            anchor_names[1]: poponnx.AnchorReturnType("ALL"),
            anchor_names[2]: poponnx.AnchorReturnType("ALL"),
            anchor_names[3]: poponnx.AnchorReturnType("ALL")
        })
    optimizer = poponnx.ConstSGD(0.01)
    losses = [poponnx.L1Loss(o, "l1LossVal", 0.1)]

    opts = poponnx.SessionOptionsCore()
    opts.logging = {'all': 'TRACE'}
    opts.reportOptions = {"doLayerWiseBreakdown": "true"}

    session = poponnx.Session(
        fnModel=proto,
        dataFeed=dataFlow,
        losses=losses,
        optimizer=optimizer,
        userOptions=opts)

    session.setDevice(tu.get_ipu_model(compileIPUCode=False))
    anchors = session.initAnchorArrays()

    session.prepareDevice()

    matmul1_lhs = np.ones(matmul1_lhs_shape.shape(), dtype=np.float32)
    matmul1_rhs = np.ones(matmul1_rhs_shape.shape(), dtype=np.float32)
    matmul2_lhs = np.ones(matmul2_lhs_shape.shape(), dtype=np.float32)
    matmul2_rhs = np.ones(matmul2_rhs_shape.shape(), dtype=np.float32)

    inputs = {
        i1: matmul1_lhs,
        i2: matmul1_rhs,
        i3: matmul2_lhs,
        i4: matmul2_rhs
    }
    stepio = poponnx.PyStepIO(inputs, anchors)

    session.infer(stepio)
    session.weightsFromHost()
    session.optimizerFromHost()

    # Check that there is only 3 matmul convs in computation set.
    summaryReport = session.getSummaryReport()

    computeSets = tu.get_compute_sets_from_report(summaryReport)

    num_matmuls = tu.get_compute_set_regex_count(r'^[0-9]+/Conv_1/Convolve$',
                                                 computeSets)
    # There should be only three matmul
    assert (num_matmuls == 3)


def test_gemm_train_cached_by_default():
    """
    In this test we check that the default behaviour is for matmul to be
    cached.
    """

    builder = poponnx.Builder()

    gemmA_shape = poponnx.TensorInfo("FLOAT", [2, 4])
    gemmB_shape = poponnx.TensorInfo("FLOAT", [4, 6])
    gemmC_shape = poponnx.TensorInfo("FLOAT", [2, 6])

    i1 = builder.addInputTensor(gemmA_shape)
    i2 = builder.addInputTensor(gemmB_shape)
    i3 = builder.addInputTensor(gemmC_shape)

    i4 = builder.addInputTensor(gemmA_shape)
    i5 = builder.addInputTensor(gemmB_shape)
    i6 = builder.addInputTensor(gemmC_shape)

    c1 = builder.aiOnnx.gemm([i1, i2, i3])
    c2 = builder.aiOnnx.gemm([i4, i5, i6])

    o = builder.aiOnnx.add([c1, c2])

    builder.addOutputTensor(o)

    proto = builder.getModelProto()

    anchor_names = [
        'd__' + i1, 'd__' + i2, 'd__' + i3, 'd__' + i4, 'd__' + i5, 'd__' + i6
    ]
    dataFlow = poponnx.DataFlow(
        1, {
            anchor_names[0]: poponnx.AnchorReturnType("ALL"),
            anchor_names[1]: poponnx.AnchorReturnType("ALL"),
            anchor_names[2]: poponnx.AnchorReturnType("ALL"),
            anchor_names[3]: poponnx.AnchorReturnType("ALL"),
            anchor_names[4]: poponnx.AnchorReturnType("ALL"),
            anchor_names[5]: poponnx.AnchorReturnType("ALL")
        })
    optimizer = poponnx.ConstSGD(0.01)
    losses = [poponnx.L1Loss(o, "l1LossVal", 0.1)]

    opts = poponnx.SessionOptionsCore()
    opts.logging = {'all': 'TRACE'}
    opts.reportOptions = {"doLayerWiseBreakdown": "true"}

    session = poponnx.Session(
        fnModel=proto,
        dataFeed=dataFlow,
        losses=losses,
        optimizer=optimizer,
        userOptions=opts)

    session.setDevice(tu.get_ipu_model(compileIPUCode=False))
    anchors = session.initAnchorArrays()

    session.prepareDevice()

    gemm1_A = np.ones(gemmA_shape.shape(), dtype=np.float32)
    gemm1_B = np.ones(gemmB_shape.shape(), dtype=np.float32)
    gemm1_C = np.ones(gemmC_shape.shape(), dtype=np.float32)

    gemm2_A = np.ones(gemmA_shape.shape(), dtype=np.float32)
    gemm2_B = np.ones(gemmB_shape.shape(), dtype=np.float32)
    gemm2_C = np.ones(gemmC_shape.shape(), dtype=np.float32)

    inputs = {
        i1: gemm1_A,
        i2: gemm1_B,
        i3: gemm1_C,
        i4: gemm2_A,
        i5: gemm2_B,
        i6: gemm2_C
    }
    stepio = poponnx.PyStepIO(inputs, anchors)

    session.infer(stepio)
    session.weightsFromHost()
    session.optimizerFromHost()

    # Check that there is only 2 matmul conv's computation set.
    summaryReport = session.getSummaryReport()

    computeSets = tu.get_compute_sets_from_report(summaryReport)

    num_matmuls = tu.get_compute_set_regex_count(r'^[0-9]+/Conv_1/Convolve$',
                                                 computeSets)
    # There should be only three matmul
    assert (num_matmuls == 3)
