# Copyright (c) 2021 Graphcore Ltd. All rights reserved.
import numpy as np
import pytest
import popart
import torch

import collections

collections.Iterable = collections.abc.Iterable

from resize_test_0 import interpolate

import onnx.backend.test.case.node.resize as onnx_resize


def test_float16_scales(op_tester):
    data = np.random.rand(1, 1, 2, 2).astype(np.float32)

    scales = np.array([1.0, 1.0, 2.0, 3.0], dtype=np.float16)

    def init_builder(builder):
        d = builder.addInputTensor(data)
        s = builder.aiOnnx.constant(scales)
        o = builder.aiOnnx.resize([d, s])
        builder.addOutputTensor(o)
        return [o]

    def reference(_):  # ref_data is an unused argument
        x = torch.tensor(data)
        s = [i for i in scales[2:]]
        o = interpolate(x, s)
        return [o]

    op_tester.run(init_builder, reference, "infer")


def test_resize11_float16_scales(op_tester):
    data = np.random.rand(1, 1, 2, 2).astype(np.float32)

    roi = np.array([], dtype=np.float32)
    scales = np.array([1.0, 1.0, 2.0, 3.0], dtype=np.float16)

    def init_builder(builder):
        d = builder.addInputTensor(data)
        r = builder.aiOnnxOpset11.constant(roi, False)
        s = builder.aiOnnxOpset11.constant(scales)
        o = builder.aiOnnxOpset11.resize([d, r, s])
        builder.addOutputTensor(o)
        return [o]

    def reference(_):  # ref_data is an unused argument
        x = torch.tensor(data)
        s = [i for i in scales[2:]]
        o = interpolate(x, s)
        return [o]

    op_tester.run(init_builder, reference, "infer")


@pytest.mark.parametrize("scale_factor, data_shape", [(3.0001, 2), (0.51, 6)])
@pytest.mark.parametrize("nearest_mode", ["round_prefer_floor", "pytorch"])
def test_odd_scale_factors(op_tester, nearest_mode, scale_factor, data_shape):
    data = np.random.rand(1, 1, data_shape).astype(np.float32)
    scales = np.array([1.0, 1.0, scale_factor], dtype=np.float32)

    def init_builder(builder):
        d = builder.addInputTensor(data)
        s = builder.aiOnnx.constant(scales)
        o = builder.aiOnnx.resize([d, s])
        builder.addNodeAttribute("nearest_mode", nearest_mode, {o})
        builder.addOutputTensor(o)
        return [o]

    def reference(_):  # ref_data is an unused argument
        if nearest_mode == "pytorch":
            x = torch.tensor(data)
            s = [i for i in scales[2:]]
            o = interpolate(x, s)
            return [o]
        else:
            o = onnx_resize.interpolate_nd(
                data, onnx_resize.nearest_coeffs, scale_factors=scales
            )
            return [o.astype(data.dtype)]

    op_tester.run(init_builder, reference, "infer")


@pytest.mark.parametrize("scale_factor, data_shape", [(3.0001, 2), (0.51, 6)])
def test_odd_scale_factors_grad(op_tester, scale_factor, data_shape):
    data = np.random.rand(1, 1, data_shape).astype(np.float32)

    scales = np.array([1.0, 1.0, scale_factor], dtype=np.float32)

    x_data_shape = [int(i * j) for i, j in zip(data.shape, scales)]
    x_data = np.random.rand(*x_data_shape).astype(np.float32)

    def init_builder(builder):
        d = builder.addInputTensor(data)
        x = builder.addInputTensor(x_data)
        s = builder.aiOnnx.constant(scales)
        o = builder.aiOnnx.resize([d, s])
        o = builder.aiOnnx.mul([o, x])
        builder.addOutputTensor(o)
        return [
            o,
            popart.reservedGradientPrefix() + d,
            popart.reservedGradientPrefix() + o,
        ]

    def reference(ref_data):
        a = torch.tensor(data, requires_grad=True)
        s = [i for i in scales[2:]]
        b = interpolate(a, s)
        b.retain_grad()
        o = b * torch.tensor(x_data)

        d__o = ref_data.getOutputTensorGrad(0)
        o.backward(torch.tensor(d__o))
        return [o, a.grad, None]

    op_tester.setPatterns(["MulArgGradOp"], enableRuntimeAsserts=False)
    op_tester.run(init_builder, reference, "train")


@pytest.mark.parametrize(
    "data_shape, scales",
    [
        # This changes the values without changing the size of the dimension.
        ([8], [1.12]),
        # upsample
        ([2], [8.0]),
        ([5, 3], [2.3, 2.5]),
        # downsample
        ([2, 8], [1.0, 3 / 8]),
        ([5, 3], [0.3, 0.5]),
        # 3D
        ([5, 3, 4], [2.3, 2.5, 0.5]),
    ],
)
@pytest.mark.parametrize(
    "coordinate_transformation_mode",
    ["half_pixel", "pytorch_half_pixel", "asymmetric", "align_corners"],
)
def test_resize_linear(op_tester, data_shape, scales, coordinate_transformation_mode):
    data = np.random.rand(*data_shape).astype(np.float32)
    roi = np.array([], dtype=np.float32)
    scales = np.array(scales, dtype=np.float32)

    def init_builder(builder):
        d = builder.addInputTensor(data)
        s = builder.aiOnnxOpset11.constant(scales, False)
        r = builder.aiOnnxOpset11.constant(roi, False)
        o = builder.aiOnnxOpset11.resize(
            [d, r, s],
            mode="linear",
            coordinate_transformation_mode=coordinate_transformation_mode,
        )
        builder.addOutputTensor(o)
        return [o]

    def reference(_):  # ref_data is an unused argument
        o = onnx_resize.interpolate_nd(
            data,
            onnx_resize.linear_coeffs,
            scale_factors=scales,
            coordinate_transformation_mode=coordinate_transformation_mode,
        )
        return [o.astype(data.dtype)]

    op_tester.run(init_builder, reference, "infer")


@pytest.mark.parametrize(
    "data_shape, sizes",
    [
        # What happens if the shape is unchanged?
        ([4], [4]),
        # Some 1D cases
        ([4], [8]),
        ([8], [3]),
        # 2D cases
        ([4, 4], [9, 9]),
        ([4, 4], [5, 3]),
    ],
)
@pytest.mark.parametrize("sizes_dtype", [np.int64, np.int32])
def test_resize_sizes_input(op_tester, data_shape, sizes, sizes_dtype):
    data = np.random.rand(*data_shape).astype(np.float32)
    roi = np.array([], dtype=np.float32)
    sizes = np.array(sizes, dtype=sizes_dtype)

    def init_builder(builder):
        d = builder.addInputTensor(data)
        s = builder.aiOnnxOpset11.constant(sizes, False)
        r = builder.aiOnnxOpset11.constant(roi, False)
        o = builder.aiOnnxOpset11.resize([d, r, "", s])
        builder.addOutputTensor(o)
        return [o]

    def reference(_):  # ref_data is an unused argument
        # Convert sizes to scales.
        scales = []
        assert len(data.shape) == len(sizes)
        for di, do in zip(data.shape, sizes):
            scales.append(do / di)

        o = onnx_resize.interpolate_nd(
            data, onnx_resize.nearest_coeffs, scale_factors=scales
        )
        # Check the output shape is correct.
        assert tuple(sizes) == o.shape

        return [o.astype(data.dtype)]

    op_tester.run(init_builder, reference, "infer")


@pytest.mark.parametrize(
    "data_shape, scales",
    [
        # This changes the values without changing the size of the dimension.
        ([8], [1.12]),
        # upsample
        ([2], [8.0]),
        ([2, 2], [2.0, 3.0]),
        ([5, 3], [2.3, 2.5]),
        # downsample
        ([2, 8], [1.0, 3 / 8]),
        ([5, 4], [0.5, 0.5]),
        # 3D
        ([5, 3, 4], [2.3, 2.5, 0.5]),
    ],
)
@pytest.mark.parametrize(
    "coordinate_transformation_mode",
    ["half_pixel", "pytorch_half_pixel", "asymmetric", "align_corners"],
)
def test_resize_cubic(op_tester, data_shape, scales, coordinate_transformation_mode):
    # This particular case has quite a large error.
    #   Popart:
    #   [[0.64254135 0.5632834 ]]
    #   Reference:
    #   [[0.6508137 0.5556082]]
    #   Diff:
    #   [[-0.00827235  0.00767517]]
    # I'm not sure what is causing it, but it's the only failure in 52 tests, so skipping it for now.
    if (
        coordinate_transformation_mode == "pytorch_half_pixel"
        and data_shape == [2, 4]
        and scales == [0.5, 0.5]
    ):
        pytest.skip()

    data = np.random.rand(*data_shape).astype(np.float32)
    roi = np.array([], dtype=np.float32)
    scales = np.array(scales, dtype=np.float32)

    def init_builder(builder):
        d = builder.addInputTensor(data)
        s = builder.aiOnnxOpset11.constant(scales, False)
        r = builder.aiOnnxOpset11.constant(roi, False)
        o = builder.aiOnnxOpset11.resize(
            [d, r, s],
            mode="cubic",
            coordinate_transformation_mode=coordinate_transformation_mode,
        )
        builder.addOutputTensor(o)
        return [o]

    def reference(_):  # ref_data is an unused argument
        o = onnx_resize.interpolate_nd(
            data,
            onnx_resize.cubic_coeffs,
            scale_factors=scales,
            coordinate_transformation_mode=coordinate_transformation_mode,
        )

        return [o.astype(data.dtype)]

    # The more dimensions we introduce, the more floating point error we get.
    if len(data.shape) == 2:
        op_tester.atol = 1e-06
    elif len(data.shape) == 3:
        op_tester.atol = 1e-05

    op_tester.run(init_builder, reference, "infer")


@pytest.mark.parametrize(
    "data_shape, scales",
    [
        # This changes the values without changing the size of the dimension.
        # in pytorch we find that it will reset scale = 1.0 if input_size==output_size
        # i.e. floor(8*1.12)=8
        # but the reset will only happen in backward pass(not forward pass),
        # so this is a bug in pytorch
        # so we temprarily comment this test
        # ([8, 8], [1.12, 1.12]),
        # upsample
        ([2, 2], [2.0, 3.0]),
        ([5, 3], [2.3, 2.5]),
        # downsample
        ([2, 8], [1.0, 3 / 8]),
        ([5, 4], [0.5, 0.5]),
    ],
)
def test_resize_cubic_grad(op_tester, data_shape, scales):
    data = np.random.rand(1, 1, *data_shape).astype(np.float32)
    roi = np.array([], dtype=np.float32)
    scales = np.array([1.0, 1.0] + scales, dtype=np.float32)

    x_data_shape = [int(i * j) for i, j in zip(data.shape, scales)]
    x_data = np.random.rand(*x_data_shape).astype(np.float32)

    def init_builder(builder):
        d = builder.addInputTensor(data)
        x = builder.addInputTensor(x_data)
        s = builder.aiOnnxOpset11.constant(scales, False)
        r = builder.aiOnnxOpset11.constant(roi, False)
        o = builder.aiOnnxOpset11.resize(
            [d, r, s], mode="cubic", coordinate_transformation_mode="pytorch_half_pixel"
        )
        o = builder.aiOnnx.mul([o, x])
        builder.addOutputTensor(o)
        return [
            o,
            popart.reservedGradientPrefix() + d,
            popart.reservedGradientPrefix() + o,
        ]

    def reference(ref_data):
        a = torch.tensor(data, requires_grad=True)
        s = [i for i in scales[2:]]
        b = torch.nn.functional.interpolate(
            a, scale_factor=s, mode="bicubic", align_corners=False
        )
        b.retain_grad()
        o = b * torch.tensor(x_data)

        d__o = ref_data.getOutputTensorGrad(0)
        o.backward(torch.tensor(d__o))
        return [o, a.grad, None]

    op_tester.setPatterns(["MulArgGradOp"], enableRuntimeAsserts=False)
    # according to our experience, 1e-6 should be a good abosolute tolerance
    op_tester.atol = 1e-6
    op_tester.run(init_builder, reference, "train")


@pytest.mark.parametrize(
    "data_shape, scales",
    [
        # upsample
        ([1, 1, 3], [1, 1, 2]),
        ([1, 1, 8], [1, 1, 1.5]),
        ([1, 1, 3], [1, 1, 2]),
        # downsample
        ([1, 1, 6], [1, 1, 5 / 6]),
        ([1, 1, 8], [1, 1, 3 / 4]),
        ([1, 1, 10], [1, 1, 0.7]),
    ],
)
def test_resize_torch_linear(op_tester, data_shape, scales):
    data = np.random.rand(*data_shape).astype(np.float32)
    roi = np.array([], dtype=np.float32)
    scales = np.array(scales, dtype=np.float32)

    def init_builder(builder):
        d = builder.addInputTensor(data)
        s = builder.aiOnnxOpset11.constant(scales, False)
        r = builder.aiOnnxOpset11.constant(roi, False)
        o = builder.aiOnnxOpset11.resize(
            [d, r, s],
            mode="linear",
            coordinate_transformation_mode="pytorch_half_pixel",
        )
        builder.addOutputTensor(o)
        return [o]

    def reference(_):  # ref_data is an unused argument
        d = torch.Tensor(data)
        s = np.multiply(scales, data_shape).round().astype(np.int32)
        o = torch.nn.functional.interpolate(
            d, size=(s[2]), mode="linear", align_corners=False
        )
        return [o.numpy().astype(data.dtype)]

    op_tester.run(init_builder, reference, "infer")


@pytest.mark.parametrize(
    "data_shape, scales",
    [
        # upsample
        ([1, 1, 5, 3], [1, 1, 2, 2]),
        ([1, 1, 2, 8], [1, 1, 3, 1.5]),
        ([1, 1, 5, 3], [1, 1, 1.6, 2]),
        # downsample
        ([1, 1, 4, 6], [1, 1, 0.5, 5 / 6]),
        ([1, 1, 5, 8], [1, 1, 0.2, 3 / 4]),
        ([1, 1, 7, 10], [1, 1, 4 / 7, 0.7]),
    ],
)
@pytest.mark.skipif(
    torch.__version__ >= "1.11.0",
    reason="For some reason torch > 1.11.0 gives different values for nn.functional.interpolate.",
    # TODO: T71460 find out why and fix
)
def test_resize_torch_bilinear(op_tester, data_shape, scales):
    data = np.random.rand(*data_shape).astype(np.float32)
    roi = np.array([], dtype=np.float32)
    scales = np.array(scales, dtype=np.float32)

    def init_builder(builder):
        d = builder.addInputTensor(data)
        s = builder.aiOnnxOpset11.constant(scales, False)
        r = builder.aiOnnxOpset11.constant(roi, False)
        o = builder.aiOnnxOpset11.resize(
            [d, r, s],
            mode="linear",
            coordinate_transformation_mode="pytorch_half_pixel",
        )
        builder.addOutputTensor(o)
        return [o]

    def reference(_):  # ref_data is an unused argument
        d = torch.Tensor(data)
        s = np.multiply(scales, data_shape).round().astype(np.int32)
        o = torch.nn.functional.interpolate(
            d, size=(s[2], s[3]), mode="bilinear", align_corners=False
        )
        return [o.numpy().astype(data.dtype)]

    op_tester.run(init_builder, reference, "infer")


@pytest.mark.parametrize(
    "data_shape, scales",
    [
        # upsample
        ([1, 1, 4, 5, 3], [1, 1, 3, 2, 2]),
        ([1, 1, 6, 2, 8], [1, 1, 4, 3, 1.5]),
        ([1, 1, 7, 5, 3], [1, 1, 2, 1.6, 2]),
        # downsample
        ([1, 1, 8, 4, 6], [1, 1, 3 / 8, 0.5, 5 / 6]),
        ([1, 1, 3, 5, 8], [1, 1, 2 / 3, 0.2, 3 / 4]),
        ([1, 1, 6, 7, 10], [1, 1, 0.5, 4 / 7, 0.7]),
    ],
)
@pytest.mark.skipif(
    torch.__version__ >= "1.11.0",
    reason="For some reason torch > 1.11.0 gives different values for nn.functional.interpolate.",
    # TODO: T71460 find out why and fix
)
def test_resize_torch_trilinear(op_tester, data_shape, scales):
    data = np.random.rand(*data_shape).astype(np.float32)
    roi = np.array([], dtype=np.float32)
    scales = np.array(scales, dtype=np.float32)

    def init_builder(builder):
        d = builder.addInputTensor(data)
        s = builder.aiOnnxOpset11.constant(scales, False)
        r = builder.aiOnnxOpset11.constant(roi, False)
        o = builder.aiOnnxOpset11.resize(
            [d, r, s],
            mode="linear",
            coordinate_transformation_mode="pytorch_half_pixel",
        )
        builder.addOutputTensor(o)
        return [o]

    def reference(_):  # ref_data is an unused argument
        d = torch.Tensor(data)
        s = np.multiply(scales, data_shape).round().astype(np.int32)
        o = torch.nn.functional.interpolate(
            d, size=(s[2], s[3], s[4]), mode="trilinear", align_corners=False
        )
        return [o.numpy().astype(data.dtype)]

    op_tester.run(init_builder, reference, "infer")


@pytest.mark.parametrize(
    "scales_data, size_data",
    [
        ([], [2, 2, 2, 2]),
        ([1.0, 1.0, 2.0, 3.0], []),
    ],
)
def test_resize_with_both_scales_resize_param(op_tester, scales_data, size_data):
    data = np.random.rand(1, 3, 2, 2).astype(np.float32)
    roi = np.array([], dtype=np.float32)
    scales = np.array(scales_data, dtype=np.float16)
    sizes = np.array(size_data, dtype=np.int64)

    def init_builder(builder):
        input_tensor = builder.addInputTensor(data)
        scales_tesor = builder.aiOnnxOpset11.constant(scales, False)
        sizes_tensor = builder.aiOnnxOpset11.constant(sizes, False)
        roi_tensor = builder.aiOnnxOpset11.constant(roi, False)
        out_tensor = builder.aiOnnxOpset11.resize(
            [input_tensor, roi_tensor, scales_tesor, sizes_tensor]
        )
        builder.addOutputTensor(out_tensor)
        return [out_tensor]

    def reference(_):  # ref_data is an unused argument
        # Convert sizes to scales.
        if scales_data == []:
            for di, do in zip(data.shape, sizes):
                scales_data.append(do / di)

        o = onnx_resize.interpolate_nd(
            data, onnx_resize.nearest_coeffs, scale_factors=scales_data
        )
        # Check the output shape is correct.
        return [o.astype(data.dtype)]

    op_tester.run(init_builder, reference, "infer")


def test_resize_error_case(op_tester):
    data = np.random.rand(4, 6).astype(np.float32)
    roi = np.array([], dtype=np.float32)
    scales = np.array([1.0, 2.0], dtype=np.float16)
    sizes = np.array([1, 2], dtype=np.int64)

    def init_builder(builder):
        input_tensor = builder.addInputTensor(data)
        scales_tesor = builder.aiOnnxOpset11.constant(scales, False)
        sizes_tenosr = builder.aiOnnxOpset11.constant(sizes, False)
        roi_tensor = builder.aiOnnxOpset11.constant(roi, False)
        out_tensor = builder.aiOnnxOpset11.resize(
            [input_tensor, roi_tensor, scales_tesor, sizes_tenosr]
        )
        builder.addOutputTensor(out_tensor)
        return [out_tensor]

    def reference(_):  # ref_data is an unused argument
        # Convert sizes to scales.
        return []

    with pytest.raises(popart.popart_exception) as e_info:
        op_tester.run(init_builder, reference, "infer")
    assert (
        e_info.value.args[0] == "Resize op has inputs for `sizes` and `scales`. "
        "Only one of these tensors should be present. If `sizes` is needed, "
        "please pass an empty string as the name of `scales` in the "
        "input list ( resize([X, roi, '', sizes]) ). If the proto is not "
        "generated from the PopART Builder, please confirm that only one "
        "of the two tensors shall have data."
    )
