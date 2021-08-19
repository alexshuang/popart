# Copyright (c) 2018 Graphcore Ltd. All rights reserved.
# see model0.py for a more detailed
# description of what's going on.

import sys
import os
import torch
import c10driver
import cmdline
import popart
import popart_core
from popart.torch import torchwriter

args = cmdline.parse()

nInChans = 3
nOutChans = 10
batchSize = 2
batchesPerStep = 3
anchors = {
    "loss": popart_core.AnchorReturnType("Final"),
}
dataFlow = popart_core.DataFlow(batchesPerStep, anchors)
inputShapeInfo = popart_core.InputShapeInfo()
inputShapeInfo.add("image0",
                   popart.TensorInfo("FLOAT", [batchSize, nInChans, 32, 32]))
inputShapeInfo.add("image1",
                   popart.TensorInfo("FLOAT", [batchSize, nInChans, 32, 32]))
inputShapeInfo.add("label", popart.TensorInfo("INT32", [batchSize]))
inNames = ["image0", "image1", "label"]
cifarInIndices = {"image0": 0, "image1": 0, "label": 1}
outNames = ["loss"]

willowOptPatterns = popart.Patterns()
willowOptPatterns.OpToIdentity = True


def nllloss(logprobs, targets):
    targets = targets.unsqueeze(1)
    loss = torch.gather(logprobs, 1, targets)
    return -torch.sum(loss)


class Module0(torch.nn.Module):
    def __init__(self):
        torch.nn.Module.__init__(self)
        self.conv1 = torchwriter.conv3x3(nInChans, nOutChans)
        self.conv2 = torchwriter.conv3x3(nOutChans, nOutChans)
        self.relu = torch.nn.functional.relu
        # for softmax dim -1 is correct for [sample][class],
        # gives class probabilities for each sample.
        self.softmax = torch.nn.Softmax(dim=-1)

    def forward(self, inputs):
        image0 = inputs[0]
        image1 = inputs[1]
        labels = inputs[2]
        x = image0 - image1

        x = self.conv1(x)
        x = self.relu(x)
        x = self.conv2(x)
        preProbSquared = x + x
        l1loss = torch.sum(0.01 * torch.abs(preProbSquared))

        window_size = (int(x.size()[2]), int(x.size()[3]))
        x = torch.nn.functional.avg_pool2d(x,
                                           kernel_size=window_size,
                                           stride=window_size)
        x = torch.squeeze(x)
        probs = self.softmax(x)
        logprobs = torch.log(probs)
        nll = nllloss(logprobs, labels)

        loss = l1loss + nll
        return loss


# Set arbitrary seed so model weights are initialized to the
# same values each time the test is run
torch.manual_seed(1)

torchWriter = torchwriter.PytorchNetWriter(
    inNames=inNames,
    outNames=outNames,
    optimizer=popart_core.ConstSGD(0.001),
    inputShapeInfo=inputShapeInfo,
    dataFlow=dataFlow,
    ### Torch specific:
    module=Module0(),
    samplesPerBatch=batchSize)

c10driver.run(torchWriter, willowOptPatterns, args.outputdir, cifarInIndices,
              args.device, args.hw_id)
