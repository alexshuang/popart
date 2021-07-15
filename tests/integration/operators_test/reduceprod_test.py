# Copyright (c) 2019 Graphcore Ltd. All rights reserved.
import itertools
import numpy as np
import popart
import torch
import pytest
import torch.nn.functional as F
from op_tester import op_tester

USE_DEFAULT_AXES = None


def test_reduceprod(op_tester):
    data = np.random.rand(5, 3, 7).astype(np.float32)
    axes_list = [[], [0], [1], [2], [0, 1], [0, 2], [1, 2], [0, 1, 2],
                 USE_DEFAULT_AXES]
    keepdims_list = [False, True]

    def init_builder(builder):
        tensor = builder.addInputTensor(data)
        result = []
        for axes, keepdims in itertools.product(axes_list, keepdims_list):
            if axes is USE_DEFAULT_AXES:
                out = builder.aiOnnx.reduceprod(
                    [tensor],
                    keepdims=keepdims,
                    debugContext="test_reduceprod_default_{0}".format(
                        keepdims))
            else:
                out = builder.aiOnnx.reduceprod(
                    [tensor],
                    axes=axes,
                    keepdims=keepdims,
                    debugContext="test_reduceprod_{0}_{1}".format(
                        axes, keepdims))
            builder.addOutputTensor(out)
            result.append(out)
        return result

    def reference(ref_data):
        result = []
        for axes, keepdims in itertools.product(axes_list, keepdims_list):
            result.append(
                np.prod(
                    data,
                    axis=tuple(axes) if axes is not USE_DEFAULT_AXES else None,
                    keepdims=keepdims))
        return result

    op_tester.setPatterns(['PreUniRepl'], enableRuntimeAsserts=False)
    op_tester.run(init_builder, reference, 'infer')


def test_reduceprod_training(op_tester):
    data = np.random.rand(2, 5, 3).astype(np.float32)
    # To check if implementation can handle 0.0 in the input with gradients
    data[1, 1, 1] = 0.0
    axes_list = [[0], [1], [2], [0, 1], [0, 2], [1, 2], [0, 1, 2],
                 USE_DEFAULT_AXES]
    keepdims_list = [False, True]

    def init_builder(builder):
        result = []
        axes_reduce = []
        for axes, keepdims in itertools.product(axes_list, keepdims_list):
            tensor = builder.addInputTensor(data)
            if axes is USE_DEFAULT_AXES:
                out = builder.aiOnnx.reduceprod(
                    [tensor],
                    keepdims=keepdims,
                    debugContext="test_reduceprod_default_{0}".format(
                        keepdims))
            else:
                out = builder.aiOnnx.reduceprod(
                    [tensor],
                    axes=axes,
                    keepdims=keepdims,
                    debugContext="test_reduceprod_{0}_{1}".format(
                        axes, keepdims))
            result.append(out)
            result.append(popart.reservedGradientPrefix() + tensor)
            axes_len = len(axes) if axes is not USE_DEFAULT_AXES else 3
            axes_reduce.append(range(3 - (0 if keepdims else axes_len)))
        sum = builder.aiOnnx.sum([
            builder.aiOnnx.reducesum([r],
                                     axes=axes,
                                     keepdims=False,
                                     debugContext="test_reducesum_all")
            for r, axes in zip(result[0::2], axes_reduce)
        ],
                                 debugContext="test_sum")
        reshaped_sum = builder.aiOnnx.unsqueeze([sum],
                                                axes=[0],
                                                debugContext="test_reshape")
        builder.addOutputTensor(reshaped_sum)
        result = [
            reshaped_sum,
            popart.reservedGradientPrefix() + reshaped_sum
        ] + result
        return result

    def reference(ref_data):
        result = []
        for axes, keepdims in itertools.product(axes_list, keepdims_list):
            tensor = torch.tensor(data, requires_grad=True)
            out = tensor
            sorted_axes = axes if axes is not USE_DEFAULT_AXES else [0, 1, 2]
            sorted_axes.sort(reverse=True)
            for dim in sorted_axes:
                out = torch.prod(out, dim=dim, keepdim=keepdims)
            result.append(out)
            result.append(tensor)

        sum = torch.unsqueeze(torch.sum(
            torch.stack([torch.sum(r) for r in result[0::2]])),
                              dim=0)

        d__o = ref_data.getOutputTensorGrad(0)
        sum.backward(torch.tensor(d__o))
        result[1::2] = [r.grad for r in result[1::2]]

        result = [sum, sum.grad] + result
        return result

    op_tester.setPatterns(['OpToIdentity'], enableRuntimeAsserts=False)
    op_tester.run(init_builder, reference, 'train')
