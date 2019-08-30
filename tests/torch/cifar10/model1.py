# see model0.py for a more detailed
# description of what's going on.

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
nOutChans = 10
batchSize = 2
batchesPerStep = 3
anchors = {
    "l1LossVal": popart.AnchorReturnType("FINAL"),
    "nllLossVal": popart.AnchorReturnType("FINAL"),
    "probs": popart.AnchorReturnType("FINAL")
}
dataFeed = popart.DataFlow(batchesPerStep, anchors)
inputShapeInfo = popart.InputShapeInfo()
inputShapeInfo.add("image0",
                   popart.TensorInfo("FLOAT", [batchSize, nInChans, 32, 32]))
inputShapeInfo.add("image1",
                   popart.TensorInfo("FLOAT", [batchSize, nInChans, 32, 32]))
inputShapeInfo.add("label", popart.TensorInfo("INT32", [batchSize]))

inNames = ["image0", "image1"]
cifarInIndices = {"image0": 0, "image1": 0, "label": 1}
outNames = ["preProbSquared", "probs"]
losses = [
    popart.NllLoss("probs", "label", "nllLossVal"),
    popart.L1Loss("preProbSquared", "l1LossVal", 0.01)
]

willowOptPasses = popart.Patterns(popart.PatternsLevel.ALL)


class Module0(torch.nn.Module):
    def __init__(self):
        torch.nn.Module.__init__(self)
        self.conv1 = torchwriter.conv3x3(nInChans, nOutChans)
        self.conv2 = torchwriter.conv3x3(nOutChans, nOutChans)
        self.sin = torch.sin
        self.pad = torch.nn.functional.pad
        # for softmax dim -1 is correct for [sample][class],
        # gives class probabilities for each sample.
        self.softmax = torch.nn.Softmax(dim=-1)

    def forward(self, inputs):
        image0 = inputs[0]
        image1 = inputs[1]
        x = image0 + image1

        x = self.conv1(x)
        x = self.sin(x)
        x = self.pad(x, pad=(1, 0, 1, 0), mode='constant', value=0)
        x = self.conv2(x)
        preProbSquared = x + x

        window_size = (int(x.size()[2]), int(x.size()[3]))
        x = torch.nn.functional.avg_pool2d(x, kernel_size=window_size)
        x = torch.squeeze(x)
        # if batchSize == 1, the above sqeeze removes too many dimensions
        if batchSize == 1:
            # mapping x.shape to int prevents pytorch tracking it
            # and trying to insert ops we don't support into the graph
            x_shape = map(int, x.shape)
            x = x.view(batchSize, *x_shape)
        # probabilities:
        # Note that for Nll, Pytorch requires logsoftmax input.
        # We do this separately the framework dependant section,
        # torchwriter.py
        probs = self.softmax(x)
        # -> currently no support from pytorch
        # -> for gather or log (pytorch 0.4.1)
        # x = torch.gather(input = x, dim = 1, index= labels)
        # loss = torch.log(x)
        return preProbSquared, probs


# Set arbitrary seed so model weights are initialized to the
# same values each time the test is run
torch.manual_seed(1)

torchWriter = torchwriter.PytorchNetWriter(
    inNames=inNames,
    outNames=outNames,
    losses=losses,
    optimizer=popart.SGDBuilder().learningRate(0.001).build(),
    inputShapeInfo=inputShapeInfo,
    dataFeed=dataFeed,
    ### Torch specific:
    module=Module0(),
    samplesPerBatch=batchSize)

c10driver.run(torchWriter, willowOptPasses, args.outputdir, cifarInIndices,
              args.device, args.hw_id)
