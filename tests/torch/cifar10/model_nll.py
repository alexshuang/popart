# Copyright (c) 2018 Graphcore Ltd. All rights reserved.
# see model0.py for a more detailed
# description of what's going on.
#
# test note (10/01/2019)
# to check that the test is doing what it is supposed to,
# I scale "oneHot" in nllx.cpp by 1.01, and the test then fails

import sys
import os

import c10driver
import popart
import cmdline
from popart.torch import torchwriter
#we require torch in this file to create the torch Module
import torch

args = cmdline.parse()

nInChans = 3
# the number of classes
nOutChans = 10
batchSize = 2
batchesPerStep = 3
anchors = {}
dataFlow = popart.DataFlow(batchesPerStep, anchors)
inputShapeInfo = popart.InputShapeInfo()
inputShapeInfo.add("image0",
                   popart.TensorInfo("FLOAT", [batchSize, nInChans, 32, 32]))
inputShapeInfo.add("label", popart.TensorInfo("INT32", [batchSize]))

inNames = ["image0", "label"]
cifarInIndices = {"image0": 0, "label": 1}
outNames = ["loss"]

willowOptPasses = popart.Patterns(popart.PatternsLevel.All)


def nllloss(logprobs, targets):
    targets = targets.unsqueeze(1)
    loss = torch.gather(logprobs, 1, targets)
    return -loss.sum()


class Module0(torch.nn.Module):
    def __init__(self):
        torch.nn.Module.__init__(self)
        self.conv1 = torchwriter.conv3x3(nInChans, nOutChans)
        self.conv3 = torchwriter.conv3x3(nOutChans, nOutChans)
        self.relu = torch.nn.functional.relu
        # for softmax dim -1 is correct for [sample][class],
        # gives class probabilities for each sample.
        self.softmax = torch.nn.Softmax(dim=-1)

    def forward(self, inputs):
        image0 = inputs[0]
        x = self.conv1(image0)
        x = self.relu(x)
        x = self.conv3(x)
        preProbSquared = x + x

        window_size = (int(x.size()[2]), int(x.size()[3]))
        x = torch.nn.functional.avg_pool2d(x,
                                           kernel_size=window_size,
                                           stride=window_size)
        pre_probs = torch.squeeze(x)
        probs = self.softmax(pre_probs)
        logprobs = torch.log(probs)
        loss = nllloss(logprobs, inputs[1])
        return loss


# Set arbitrary seed so model weights are initialized to the
# same values each time the test is run
torch.manual_seed(1)

torchWriter = torchwriter.PytorchNetWriter(
    inNames=inNames,
    outNames=outNames,
    optimizer=popart.ConstSGD(0.001),
    inputShapeInfo=inputShapeInfo,
    dataFlow=dataFlow,
    ### Torch specific:
    module=Module0(),
    samplesPerBatch=batchSize)

c10driver.run(torchWriter, willowOptPasses, args.outputdir, cifarInIndices,
              args.device, args.hw_id)
