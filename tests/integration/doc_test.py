# Copyright (c) 2020 Graphcore Ltd. All rights reserved.
from pathlib import Path
import sys

PYTHON_EXAMPLE_PATH = (
    Path(__file__).parent.parent.parent / "docs/popart/files"
).resolve()
assert PYTHON_EXAMPLE_PATH.exists()

sys.path.insert(0, str(PYTHON_EXAMPLE_PATH))


def test_importing_graphs():
    import importing_graphs  # pylint: disable=unused-import

    print("importing_graphs.py example succeeded")


def test_importing_session():
    import importing_session  # pylint: disable=unused-import

    print("importing_session.py example succeeded")
