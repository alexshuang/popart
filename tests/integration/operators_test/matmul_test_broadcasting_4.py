# Copyright (c) 2021 Graphcore Ltd. All rights reserved.
import matmul_test_broadcasting_base as mtb

# generated test cases
# these are all known to be valid input shapes to np.matmul
shapes_ = (
    ([3, 1, 2, 4], [4, 2]),
    ([2, 4], [4, 2]),
    ([2, 1, 3, 4], [3, 4, 2]),
    ([2, 1, 4, 3], [1, 3, 2]),
    ([1], [1]),
    ([1, 3, 4], [4]),
    ([2, 1], [1, 4]),
    ([2, 1, 3], [4, 2, 3, 1]),
    ([3], [2, 3, 1]),
    ([3, 4, 2, 1], [1, 4]),
    ([3, 4], [4, 2]),
    ([4, 1, 3, 2], [3, 2, 1]),
    ([2], [3, 2, 4]),
    ([4], [1, 4, 3]),
    ([4, 2], [1, 4, 2, 3]),
)


def test_matmul_broadcasting_4(op_tester):
    mtb._test_matmul_broadcasting_base(op_tester, shapes_)
