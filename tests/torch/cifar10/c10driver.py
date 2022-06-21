# Copyright (c) 2018 Graphcore Ltd. All rights reserved.
import os
import sys
import tempfile
import popart
import torch
import numpy as np
import math
from tempfile import TemporaryDirectory
from torchvision import transforms, datasets

# `import test_util` requires adding to sys.path
from pathlib import Path
sys.path.append(
    str(Path(__file__).resolve().parent.parent.parent / 'integration'))
import test_util as tu


class TestFailureError(Exception):
    def __init__(self, message):
        super().__init__(message)


def run(torchWriter,
        patterns,
        outputdir,
        cifarInIndices,
        device,
        device_hw_id,
        mode="train",
        syntheticData=False,
        transformations=[],
        epochs=4,
        printAnchorArrays=False):

    popart.getLogger().setLevel("TRACE")
    popart.getLogger("session").setLevel("WARN")

    if outputdir is None:
        with TemporaryDirectory() as outputdir:
            return _run_impl(torchWriter,
                             patterns,
                             outputdir,
                             cifarInIndices,
                             device,
                             device_hw_id,
                             mode,
                             syntheticData,
                             transformations,
                             epochs,
                             printAnchorArrays=printAnchorArrays)
    else:
        if not os.path.exists(outputdir):
            os.mkdir(outputdir)

        return _run_impl(torchWriter,
                         patterns,
                         outputdir,
                         cifarInIndices,
                         device,
                         device_hw_id,
                         mode,
                         syntheticData,
                         transformations,
                         epochs,
                         printAnchorArrays=printAnchorArrays)


def _run_impl(torchWriter, patterns, outputdir, cifarInIndices, device,
              device_hw_id, mode, syntheticData, transformations, epochs,
              printAnchorArrays):

    runIds = [-1] + [
        int(x.split("runId")[1].split("_")[0])
        for x in os.listdir(outputdir) if "runId" in x
    ]
    baseId = 1 + max(runIds)

    def getFnModel(framework, epoch):
        return os.path.join(
            outputdir,
            "runId%d_%sModel_epoch%s.onnx" % (baseId, framework, epoch))

    def getFnPopArt(epoch):
        return getFnModel("PopArt", epoch)

    def getFnTorch(epoch):
        return getFnModel("Torch", epoch)

    def getFnModel0():
        return os.path.join(outputdir, "runId%d_model0.onnx" % (baseId, ))

    dataFlow = torchWriter.dataFlow
    inputShapeInfo = torchWriter.inputShapeInfo
    validModes = ["infer", "train"]
    if mode not in validModes:
        raise Exception("mode must be one of " + str(validModes))

    transform = transforms.Compose([
        transforms.ToTensor(),
        transforms.Normalize((0.5, 0.5, 0.5), (0.5, 0.5, 0.5))
    ])

    # determine what the data directory is
    datadir = "unset"

    dir_path = os.path.dirname(os.path.realpath(__file__))
    path_c10datadir = os.path.join(dir_path, "c10datadir.py")
    if os.path.exists(path_c10datadir):
        import c10datadir
        datadir = c10datadir.c10datadir
    else:
        tmpdir = tempfile.gettempdir()
        datadir = os.path.abspath(os.path.join(tmpdir, 'cifar10data'))
    print("Using datadir=%s" % (datadir))

    if (not os.path.exists(datadir)):
        print(
            "Specified datadir %s does not exist. Consider making it here with os.mkdir(datadir)"
            % (datadir, ))

    print("c10driver: getting data from", datadir)
    trainset = datasets.CIFAR10(root=datadir,
                                train=True,
                                download=False,
                                transform=transform)

    fnModel0 = getFnModel0()

    # write ONNX Model to file
    torchWriter.saveModel(fnModel=fnModel0)

    stepLoader = torch.utils.data.DataLoader(
        trainset,
        # the amount of data loaded for each step.
        # note this is not the batch size, it's the "step" size
        # (samples per step)
        batch_size=torchWriter.samplesPerBatch * dataFlow.batchesPerStep(),
        #non-random data loading
        shuffle=False,
        num_workers=0)

    deviceManager = popart.DeviceManager()

    # Create a CPU device
    if device == "cpu":
        device = deviceManager.createCpuDevice()

    # Create an IPU Model device
    elif device == "ipu_model":

        options = {"compileIPUCode": True, 'numIPUs': 1, 'tilesPerIPU': 4}
        device = deviceManager.createIpuModelDevice(options)

    # Create an Simulator
    elif device == "sim":
        options = {"numIpus": 1, "tilesPerIPU": 4}
        device = deviceManager.createSimDevice(options)

    # Get a Hardware Device
    elif device == "hw":
        # Get a hardware device that meets the reqirements,
        # may throw if none are available.
        # Will attach to the device
        if device_hw_id:
            device = deviceManager.acquireDeviceById(device_hw_id)
        else:
            device = tu.acquire_ipu()

    # Enumerate available devices
    print("Enumerating devices")
    print("-------------------------------------")
    for idx, d in enumerate(deviceManager.enumerateDevices()):
        print('{0}. {1}'.format(idx, d))
    print("")

    opts = popart.SessionOptions()
    opts.logDir = outputdir
    if syntheticData == True:
        opts.syntheticDataMode = popart.SyntheticDataMode.RandomNormal

    modelProtoX = fnModel0
    if transformations:
        gc = popart.GraphTransformer(fnModel0)
        for transformation in transformations:
            print("Running %s transformation pass" % (transformation, ))
            if transformation == "removeUnusedInputs":
                gc.removeUnusedInputs()

            elif transformation == "prepareNodesForTraining":
                gc.prepareNodesForTraining()

            else:
                raise RuntimeError(
                    "Unrecognised transformation %s" % (transformation, ))

        modelProtoX = gc.getModelProto()

    # Reads ONNX model from file and creates backwards graph,
    # performs Ir optimisations

    if mode == 'infer':
        session = popart.InferenceSession(fnModel=modelProtoX,
                                          inputShapeInfo=inputShapeInfo,
                                          dataFlow=dataFlow,
                                          patterns=patterns,
                                          userOptions=opts,
                                          deviceInfo=device)
    else:
        if len(torchWriter.outNames) != 1:
            raise RuntimeError("Expecting single scalar loss tensor")

        # Append output with an identity loss, to reduce to scalar if
        # necessary
        bder = popart.Builder(modelProtoX)
        loss = bder.aiGraphcore.identityloss(
            [torchWriter.outNames[0]], reduction=popart.ReductionType.Sum)
        session = popart.TrainingSession(fnModel=bder.getModelProto(),
                                         inputShapeInfo=inputShapeInfo,
                                         dataFlow=dataFlow,
                                         loss=loss,
                                         optimizer=torchWriter.optimizer,
                                         patterns=patterns,
                                         userOptions=opts,
                                         deviceInfo=device)

    # get the tensor info for the anchors
    anchorArrays = session.initAnchorArrays()

    allDotPrefixes = [x[0:-4] for x in os.listdir(outputdir) if ".dot" in x]
    print("Will generate graph pdfs for all of:")
    print(allDotPrefixes)
    import subprocess
    # set generateFromDots to True to
    # generate pdf figures of the Ir. It
    # requires the 'dot' program
    generateFromDots = False
    if generateFromDots:
        for name in allDotPrefixes:
            dotfile = os.path.join(outputdir, "%s.dot" % (name, ))
            outputfile = os.path.join(outputdir, "%s.pdf" % (name, ))
            log = subprocess.call(
                ["dot", "-T", "pdf", "-o", outputfile, dotfile])
            print("Exit status on `%s' was: %s" % (name, log))

    print("Setting device to IPU, and preparing it")
    session.prepareDevice()

    if mode == "train":
        print("Writing weights to device")
        session.weightsFromHost()

        print("Writing Optimizer tensors to device, if there are any")

    def addStepDimension(data, batchesPerStep):
        if batchesPerStep == 1:
            return data
        else:
            dataShape = np.array(np.shape(data))
            dataShape[0] //= batchesPerStep
            dataShape = np.insert(dataShape, 0, batchesPerStep)
            return np.reshape(data, dataShape)

    def reportTensorError(tensorInd, result):
        reportStr = str(tensorInd) + " :\n"
        reportStr += "  |pA - tA|^2 / (|pA||tA| + 1e-8)  = " + str(
            result) + "\n"
        return reportStr

    def getAnchorTensor(tId, anchorArrays):
        assertStr = "Tensor" + tId + " must be specified as an anchor"
        assert (tId in anchorArrays.keys()), assertStr
        return anchorArrays[tId]

    def subsampleBatches(array, refShape):
        arrayShape = np.shape(array)

        # Every Nth batch
        if len(arrayShape) == len(refShape):
            n = arrayShape[0] // refShape[0]
            return array[n - 1::n]

        # Last batch only
        else:
            return array[-1]

    def getTensorError(tA, pA):
        # pA, tA are corresponding tensors from two models
        pA_shape = np.shape(pA)
        tA_shape = np.shape(tA)
        assert (pA_shape == tA_shape), "Arrays must be same shape"

        ss_err = np.sum((np.array(pA) - np.array(tA))**2)
        ss_pA = np.sum(np.array(pA)**2)
        ss_tA = np.sum(np.array(tA)**2)
        return ss_err / (math.sqrt(ss_pA * ss_tA) + 1.0e-8)

    def checkResult(result, margin):
        if np.isnan(result):
            raise TestFailureError(str(result) + " is NaN")
        elif (result > margin):
            raise TestFailureError(
                str(result) + " is greater than " + str(margin))

    margin = 5.0e-7
    numReports = []

    for epoch in range(epochs):  # loop over the dataset multiple times
        print("Epoch is %d" % (epoch, ))
        stepData = next(iter(stepLoader))

        # Form the input map for one step's worth of data.
        # Note: data from the torch DataLoader has shape:
        #   [stepSize * batchSize, sampleShape]
        # whereas Popart expects input data of the shape:
        #   [stepSize, batchSize, sampleShape]
        # so we reshape the input array before passing to the stepio
        inputs = {}
        for tenId in cifarInIndices.keys():
            inputs[tenId] = \
                addStepDimension(stepData[cifarInIndices[tenId]].numpy(),
                                 session.dataFlow.batchesPerStep())

        if mode == "train":
            # take batchesPerStep passes (1 step), Torch
            torchWriter.train(inputs)

            # take batchesPerStep passes (1 step), PopArt
            pystepio = popart.PyStepIO(inputs, anchorArrays)
            session.run(pystepio)

            if printAnchorArrays:
                print(
                    "\nAnchor arrays (being printed as printAnchorArrays==True):"
                )
                for name in anchorArrays.keys():
                    arr = anchorArrays[name]
                    print("\nAnchored Array Name=", name, " and Size=",
                          arr.size)

                    if (arr.size < 10):
                        print("\nArray (of size < 10) values are")
                        print(arr)

                    if len(arr.shape) > 1:
                        for i, slice0 in enumerate(arr):
                            print("Sum along axis %d is Sum=%.15f" %
                                  (i, slice0.sum()))

                    print("Total Sum is %.15f" % (arr.sum()))

            # write models to file
            fnTorchModel = getFnTorch(epoch)
            fnPopArtModel = getFnPopArt(epoch)
            torchWriter.saveModel(fnTorchModel)
            session.modelToHost(fnPopArtModel)
            print("Writing models to " + fnTorchModel + " and " +
                  fnPopArtModel)

            # Compare parameters from updated Onnx models
            print("Obtaining popart NumericsReport, A: Torch, B: Popart.")
            if epoch is 0:
                nr = popart.NumericsReport(fnModel0, fnTorchModel, fnModel0,
                                           fnPopArtModel)
            else:
                nr = popart.NumericsReport(getFnTorch(epoch - 1), fnTorchModel,
                                           getFnPopArt(epoch - 1),
                                           fnPopArtModel)

            print(nr.fullReport())
            # One relative error calculated per weight tensor
            for tId, relerror in nr.getRelativeErrors().items():
                checkResult(relerror, margin)

        elif mode == "infer":
            # take batchesPerStep passes (1 step), Torch
            # returns map of outputs for each sample
            # Note: already are of dimension matching the
            # anchors
            torchOutputs = torchWriter.infer(inputs)

            # take batchesPerStep passes (1 step), PopArt
            pystepio = popart.PyStepIO(inputs, anchorArrays)
            session.run(pystepio)

            # Compare torch outputs tensors with popart output from
            # anchor tensor maps
            for nInd, outName in enumerate(torchWriter.outNames):
                # Torch outputs returned for all samples, whereas
                # anchors are returned as specified by the user.
                # Subsample torch outputs to match dimensions
                torchOuput = subsampleBatches(torchOutputs[outName],
                                              np.shape(anchorArrays[outName]))
                result = getTensorError(torchOuput, anchorArrays[outName])
                print(reportTensorError(nInd, result))
                checkResult(result, margin)

    return anchorArrays
