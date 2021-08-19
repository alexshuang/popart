# Copyright (c) 2019 Graphcore Ltd. All rights reserved.
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

import popart

import itertools
import os
import unittest
import onnx.backend.base
import onnx.backend.test

from onnx.backend.base import Device, DeviceType
from onnx.backend.test.runner import BackendIsNotSupposedToImplementIt
import onnx.shape_inference
import onnx.version_converter
from typing import Optional, Text, Any, Tuple, Sequence
from onnx import NodeProto, ModelProto, TensorProto
import numpy  # type: ignore

# The following just executes the fake backend through the backend test
# infrastructure. Since we don't have full reference implementation of all ops
# in ONNX repo, it's impossible to produce the proper results. However, we can
# run 'checker' (that's what base Backend class does) to verify that all tests
# fed are actually well-formed ONNX models.
#
# If everything is fine, all the tests would be marked as "skipped".
#
# We don't enable report in this test because the report collection logic itself
# fails when models are mal-formed.


class Context:
    def __init__(self, s, m):
        self.session = s
        self.model = m

    def run(self, inputs):

        #print(self.session)
        # Create buffers to receive results from the execution
        anchors = self.session.initAnchorArrays()

        inputmap = {}
        i = 0

        for inp in self.model.graph.input:

            isInitializer = False
            for init in self.model.graph.initializer:
                if inp.name == init.name:
                    isInitializer = True
                    break

            if isInitializer == False:
                inputmap[str(inp.name)] = inputs[i]
                i = i + 1

        stepio = popart.PyStepIO(inputmap, anchors)
        self.session.run(stepio)

        outputs = [i.name for i in self.model.graph.output]
        outputs = [anchors[i] for i in outputs]
        return outputs


class IpuBackend(onnx.backend.base.Backend):
    @classmethod
    def prepare(
            cls,
            model,  # type: ModelProto
            device='IPU',  # type: Text
            **kwargs  # type: Any
    ):  # type: (...) -> Optional[onnx.backend.base.BackendRep]
        super(IpuBackend, cls).prepare(model, device, **kwargs)

        # test shape inference
        model = onnx.shape_inference.infer_shapes(model)
        value_infos = {
            vi.name: vi
            for vi in itertools.chain(model.graph.value_info,
                                      model.graph.output)
        }

        # if do_enforce_test_coverage_whitelist(model):
        #     for node in model.graph.node:
        #         for i, output in enumerate(node.output):
        #             if node.op_type == 'Dropout' and i != 0:
        #                 continue
        #             assert output in value_infos
        #             tt = value_infos[output].type.tensor_type
        #             assert tt.elem_type != TensorProto.UNDEFINED
        #             for dim in tt.shape.dim:
        #                 assert dim.WhichOneof('value') == 'dim_value'

        popart.getLogger().setLevel("DEBUG")

        opts = popart.SessionOptions()

        anchors = {}
        for output in model.graph.output:
            anchors[output.name] = popart.AnchorReturnType("All")

        session = popart.InferenceSession(
            fnModel=model.SerializeToString(),
            dataFlow=popart.DataFlow(1, anchors),
            deviceInfo=popart.DeviceManager().createCpuDevice(),
            # deviceInfo=popart.DeviceManager().createIpuModelDevice({}),
            userOptions=opts)

        session.prepareDevice()

        context = Context(session, model)

        return context

    @classmethod
    def run_node(
            cls,
            node,  # type: NodeProto
            inputs,  # type: Any
            device='IPU',  # type: Text
            outputs_info=None,  # type: Optional[Sequence[Tuple[numpy.dtype, Tuple[int, ...]]]]
            **kwargs  # type: Any
    ):  # type: (...) -> Optional[Tuple[Any, ...]]
        super(IpuBackend, cls).run_node(node,
                                        inputs,
                                        device=device,
                                        outputs_info=outputs_info)

        raise BackendIsNotSupposedToImplementIt(
            "This is the ipu backend test that doesn't verify the results but does run the checker"
        )

    @classmethod
    def supports_device(cls, device):  # type: (Text) -> bool
        d = Device(device)
        if hasattr(DeviceType, 'IPU') and d.type == DeviceType.IPU:
            return True
        return False


backend_test = onnx.backend.test.BackendTest(IpuBackend, __name__)

# Operations we do not support
backend_test.exclude('test_compress')
backend_test.exclude('test_convtranspose')
backend_test.exclude('test_expand')
backend_test.exclude('test_eyelike')
backend_test.exclude('test_gru')
backend_test.exclude('test_hardmax')
backend_test.exclude('test_isnan')
backend_test.exclude('test_maxunpool')
backend_test.exclude('test_mvn')
backend_test.exclude('test_onehot')
backend_test.exclude('test_PReLU')
backend_test.exclude('test_reduce_log')
backend_test.exclude('test_rnn')
backend_test.exclude('test_scan')
backend_test.exclude('test_simple_rnn')
backend_test.exclude('test_size')
backend_test.exclude('test_top_k')
backend_test.exclude('test_upsample')
backend_test.exclude('test_xor')
backend_test.exclude('test_ConvTranspose2d')
backend_test.exclude('test_ConvTranspose2d_no_bias')
backend_test.exclude('test_Embedding')
backend_test.exclude('test_Embedding_sparse')
backend_test.exclude('test_GLU_dim')
backend_test.exclude('test_GLU')
backend_test.exclude('test_nonzero')
backend_test.exclude('test_tfidfvectorizer')
backend_test.exclude('test_where')
backend_test.exclude('test_bitshift')
backend_test.exclude('test_matmulinteger')

backend_test.exclude('test_convinteger')
backend_test.exclude('test_basic_convinteger')
backend_test.exclude('test_dequantizelinear')
backend_test.exclude('test_isinf')
backend_test.exclude('test_mod')
backend_test.exclude('test_resize')
backend_test.exclude('test_nonmaxsuppression')
backend_test.exclude('test_qlinearconv')
backend_test.exclude('test_qlinearmatmul')
backend_test.exclude('test_quantizelinear')
backend_test.exclude('test_reversesequence')
backend_test.exclude('test_roialign')
backend_test.exclude('test_split')

backend_test.exclude('test_cumsum')
backend_test.exclude('test_round')
backend_test.exclude('test_gather_elements')
backend_test.exclude('test_scatter_elements')
backend_test.exclude('test_gathernd')
backend_test.exclude('test_scatternd')
backend_test.exclude('test_det')
backend_test.exclude('test_dynamicquantizelinear')
backend_test.exclude('test_range')
backend_test.exclude('test_unique')
backend_test.exclude('test_sequence')

# high level models
backend_test.exclude('bvlc_alexnet')
backend_test.exclude('densenet121')
backend_test.exclude('inception_v1')
backend_test.exclude('inception_v2')
backend_test.exclude('resnet50')
backend_test.exclude('shufflenet')
backend_test.exclude('squeezenet')
backend_test.exclude('vgg19')
backend_test.exclude('zfnet512')
backend_test.exclude('strnorm')

# Test that do not work for ops we have implemented

# poplar lstm does not support peepholes
backend_test.exclude('test_lstm_with_peepholes_ipu')

# no double type on ipu
backend_test.exclude('cast_DOUBLE_to_FLOAT16')
backend_test.exclude('cast_DOUBLE_to_FLOAT_ipu')
backend_test.exclude('cast_FLOAT16_to_DOUBLE_ipu')
backend_test.exclude('cast_FLOAT_to_DOUBLE_ipu')

# no int64 on ipu
backend_test.exclude('test_argmax')
backend_test.exclude('test_argmin')

# no string type
backend_test.exclude('cast_FLOAT_to_STRING_ipu')
backend_test.exclude('cast_STRING_to_FLOAT_ipu')

# T6601
backend_test.exclude('averagepool_1d_default')
backend_test.exclude('averagepool_3d_default')
backend_test.exclude('AvgPool3d')
backend_test.exclude('AvgPool3d_stride1_pad0_gpu_input')
backend_test.exclude('AvgPool3d_stride')
backend_test.exclude('MaxPool1d')
backend_test.exclude('MaxPool1d_stride')
backend_test.exclude('MaxPool3d')
backend_test.exclude('MaxPool3d_stride')
backend_test.exclude('MaxPool3d_stride_padding')

# T????
backend_test.exclude('maxpool_2d_ceil')
backend_test.exclude('maxpool_2d_dilations')

# T6602
backend_test.exclude('averagepool_2d_pads_count_include_pad')
backend_test.exclude('averagepool_2d_precomputed_pads_count_include_pad')

# T???? Add new ceil param
backend_test.exclude('averagepool_2d_ceil')

# T6604
backend_test.exclude('constant')

# T6605
backend_test.exclude('gather_0')
backend_test.exclude('gather_1')
backend_test.exclude('scatter_with_axis')
backend_test.exclude('scatter_without_axis')

# T6607
backend_test.exclude('Conv1d_dilated')
backend_test.exclude('Conv1d_groups')
backend_test.exclude('Conv1d')
backend_test.exclude('Conv1d_pad1')
backend_test.exclude('Conv1d_pad2')
backend_test.exclude('Conv1d_stride')
backend_test.exclude('Conv2d_depthwise')
backend_test.exclude('Conv3d_dilated')
backend_test.exclude('Conv3d_dilated_strided')
backend_test.exclude('Conv3d_groups')
backend_test.exclude('Conv3d_no_bias')
backend_test.exclude('Conv3d_stride')
backend_test.exclude('Conv3d_stride_padding')
backend_test.exclude('Conv3d')

# T6608
backend_test.exclude('Conv2d_groups')
backend_test.exclude('Conv2d_groups_thnn')

# T9150
backend_test.exclude('test_shape_ipu')
backend_test.exclude('test_shape_example_ipu')

# Failures due to static size of poplar graph
backend_test.exclude('test_tile')

# T9215
backend_test.exclude('test_slice')

# Failures that have not been triaged
backend_test.exclude('test_reshape_extended_dims')
backend_test.exclude('test_reshape_negative_dim')
backend_test.exclude('test_reshape_one_dim')
backend_test.exclude('test_reshape_reduced_dim')
backend_test.exclude('test_reshape_reordered_dims')

backend_test.exclude('test_operator_add_broadcast')
backend_test.exclude('test_operator_add_size1_broadcast')
backend_test.exclude('test_operator_add_size1_right_broadcast')
backend_test.exclude('test_operator_add_size1_singleton_broadcast')
backend_test.exclude('test_operator_chunk')
backend_test.exclude('test_operator_convtranspose')
backend_test.exclude('test_operator_maxpool')
backend_test.exclude('test_operator_non_float_params')

backend_test.exclude('test_operator_reduced_mean')
backend_test.exclude('test_operator_repeat')
backend_test.exclude('test_operator_symbolic')

backend_test.exclude('test_maxpool_with_argmax_2d_precomputed_pads')
backend_test.exclude('test_maxpool_1d_default')
backend_test.exclude('test_maxpool_2d_precomputed_same_upper')
backend_test.exclude('test_maxpool_2d_same_lower')
backend_test.exclude('test_maxpool_2d_same_upper')
backend_test.exclude('test_maxpool_3d_default')
backend_test.exclude('test_maxpool_with_argmax_2d_precomputed_strides')

#T12065
backend_test.exclude('test_concat_1d_axis_negative_1')
backend_test.exclude('test_concat_2d_axis_negative_1')
backend_test.exclude('test_concat_2d_axis_negative_2')
backend_test.exclude('test_concat_3d_axis_negative_1')
backend_test.exclude('test_concat_3d_axis_negative_2')
backend_test.exclude('test_concat_3d_axis_negative_3')
backend_test.exclude('test_flatten_negative_axis1')
backend_test.exclude('test_flatten_negative_axis2')
backend_test.exclude('test_flatten_negative_axis3')
backend_test.exclude('test_flatten_negative_axis4')
backend_test.exclude('test_reduce_l1_negative_axes')
backend_test.exclude('test_reduce_l2_negative_axes')
backend_test.exclude('test_reduce_max_negative_axes')
backend_test.exclude('test_reduce_mean_negative_axes')
backend_test.exclude('test_reduce_min_negative_axes')
backend_test.exclude('test_reduce_prod_negative_axes')
backend_test.exclude('test_reduce_sum_negative_axes')
backend_test.exclude('test_reduce_sum_square_negative_axes')
backend_test.exclude('test_squeeze_negative_axes')
backend_test.exclude('test_unsqueeze_negative_axes')
backend_test.exclude('test_gather_negative_indices')

#T12066
backend_test.exclude('test_reshape_negative_extended_dims')
backend_test.exclude('test_reshape_reordered_all_dims')
backend_test.exclude('test_reshape_reordered_last_dims')
backend_test.exclude('test_reshape_zero_and_negative_dim')
backend_test.exclude('test_reshape_zero_dim')

backend_test.exclude('test_clip')
#backend_test.include('test_clip_default_inbounds')
#backend_test.include('test_clip_inbounds')

#T12067
backend_test.exclude('test_edge_pad')
backend_test.exclude('test_reflect_pad')

backend_test.exclude('test_gemm_default_no_bias')
backend_test.exclude('test_unsqueeze_unsorted_axes')

# import all test cases at global scope to make them visible to python.unittest
globals().update(backend_test.test_cases)

if __name__ == '__main__':
    unittest.main()
