# Copyright (c) 2021 Graphcore Ltd. All rights reserved.
import os
from pathlib import Path
import pytest

import sys

# In the source dir the examples_tester module lives in docs/shared/files/tests.
# We add this to the system path so this file can be ran from the source dir.
dir_docs = Path(__file__).parent.parent.parent.parent.resolve()
dir_shared_tests = dir_docs.joinpath("shared", "files", "tests")
sys.path.append(str(dir_shared_tests))

from examples_tester import ExamplesTester

working_dir = Path(os.path.dirname(__file__)).parent

# Session-level fixture, so all tests in this session share the same tmpdir for
# the MNIST dataset, so they do not each re-download it.
@pytest.fixture(scope="session")
def mnist_datasets_tmpdir(tmpdir_factory):
    fn = tmpdir_factory.mktemp("datasets")
    return fn


class TestPythonDocExamples(ExamplesTester):
    """Test simple running of the examples included in the docs"""

    def test_documentation_popxl_addition(self):
        """Test the popxl simple addition example"""
        filename = "simple_addition.py"
        self.run_python(filename, file_dir=working_dir, working_dir=working_dir)

    def test_documentation_popxl_addition_variable(self):
        """Test the popxl simple addition example"""
        filename = "tensor_addition.py"
        self.run_python(filename, file_dir=working_dir, working_dir=working_dir)

    def test_documentation_popxl_basic_subgraph(self):
        """Test the popxl basic subgraph example"""
        filename = "basic_graph.py"
        self.run_python(filename, file_dir=working_dir, working_dir=working_dir)

    def test_documentation_popxl_replication(self):
        """Test the popxl replication example"""
        filename = "replication.py"
        self.run_python(filename, file_dir=working_dir, working_dir=working_dir)

    def test_documentation_popxl_remote_var_replica_grouped(self):
        """Test the popxl remote variable in combination with replica groupings"""
        filename = "remote_variable_replica_grouped.py"
        self.run_python(filename, file_dir=working_dir, working_dir=working_dir)

    @pytest.mark.skip(
        reason=(
            "Cannot run multi-GCD tests on CI, and distributed on-chip "
            "replica-grouped variables are not yet implemented (TODO T69061)."
        )
    )
    def test_documentation_popxl_distributed_simple_example(self):
        """Test a simple distributed popxl example"""
        filename = "distributed_simple_example.py"
        self.run_python(filename, file_dir=working_dir, working_dir=working_dir)

    @pytest.mark.skip(reason="Cannot run multi-GCD tests on CI.")
    def test_documentation_popxl_distributed_rts_simple_example(self):
        """Test a simple distributed RTS popxl example"""
        filename = "distributed_rts_simple_example.py"
        self.run_python(filename, file_dir=working_dir, working_dir=working_dir)

    @pytest.mark.skip(reason="Cannot run multi-GCD tests on CI.")
    def test_documentation_popxl_distributed_rts_simple_example_manual_remote_buffer(
        self,
    ):
        """Test a simple distributed RTS popxl example using the manual remote buffer API"""
        filename = "distributed_rts_simple_example_manual_remote_buffer.py"
        self.run_python(filename, file_dir=working_dir, working_dir=working_dir)

    @pytest.mark.skip(reason="Cannot run multi-GCD tests on CI.")
    def test_documentation_popxl_distributed_rts_complex_example(self):
        """Test a complex distributed RTS popxl example"""
        filename = "distributed_rts_complex_example.py"
        self.run_python(filename, file_dir=working_dir, working_dir=working_dir)

    def test_documentation_popxl_create_multi_subgraph(self):
        """Test the popxl create multiple subgraph example"""
        filename = "create_multi_graphs_from_same_func.py"
        self.run_python(filename, file_dir=working_dir, working_dir=working_dir)

    def test_documentation_popxl_multi_callsites_graph_input(self):
        """Test the popxl create multiple callsites for a subgraph input example"""
        filename = "multi_call_graph_input.py"
        self.run_python(filename, file_dir=working_dir, working_dir=working_dir)

    def test_documentation_popxl_code_loading(self):
        """Test the code loading example"""
        filename = "code_loading.py"
        self.run_python(filename, file_dir=working_dir, working_dir=working_dir)

    def test_documentation_popxl_nested_code_loading(self):
        """Test the nested code loading example"""
        filename = "code_loading_nested.py"
        self.run_python(filename, file_dir=working_dir, working_dir=working_dir)

    def test_documentation_popxl_nested_session_contexts(self):
        """Test the nested Session contexts example"""
        filename = "nested_session_contexts.py"
        self.run_python(filename, file_dir=working_dir, working_dir=working_dir)

    def test_documentation_popxl_call_with_info(self):
        """Test the popxl call_with_info example"""
        filename = "call_with_info.py"
        self.run_python(filename, file_dir=working_dir, working_dir=working_dir)

    def test_documentation_popxl_repeat_0(self):
        """Test the popxl basic repeat example"""
        filename = "repeat_graph_0.py"
        self.run_python(filename, file_dir=working_dir, working_dir=working_dir)

    def test_documentation_popxl_repeat_1(self):
        """Test the popxl subgraph in parent in repeat example"""
        filename = "repeat_graph_1.py"
        self.run_python(filename, file_dir=working_dir, working_dir=working_dir)

    def test_documentation_popxl_repeat_2(self):
        """Test the popxl subgraph in parent in repeat example"""
        filename = "repeat_graph_2.py"
        self.run_python(filename, file_dir=working_dir, working_dir=working_dir)

    def test_documentation_popxl_get_set_tensors(self):
        """Test the popxl getting / setting tensor data example"""
        filename = "tensor_get_write.py"
        self.run_python(filename, file_dir=working_dir, working_dir=working_dir)

    def test_documentation_popxl_adv_get_write(self):
        """Test the popxl advanced getting / writing tensor data example that
        shows exactly when device-host transfers occur"""
        filename = "tensor_get_write_adv.py"
        self.run_python(filename, file_dir=working_dir, working_dir=working_dir)

    def test_documentation_popxl_autodiff(self):
        """Test the popxl autodiff op"""
        filename = "autodiff.py"
        self.run_python(filename, file_dir=working_dir, working_dir=working_dir)

    def test_documentation_popxl_in_sequence(self):
        """Test the popxl in sequence context manager"""
        filename = "in_sequence.py"
        self.run_python(filename, file_dir=working_dir, working_dir=working_dir)

    def test_documentation_popxl_remote_var(self):
        """Test the popxl remote variable"""
        filename = "remote_variable.py"
        self.run_python(filename, file_dir=working_dir, working_dir=working_dir)

    def test_documentation_popxl_remote_rts_var(self):
        """Test the popxl remote rts variable"""
        filename = "remote_rts_var.py"
        self.run_python(filename, file_dir=working_dir, working_dir=working_dir)

    def test_documentation_popxl_rts_var(self):
        """Test the popxl rts variable"""
        filename = "rts_var.py"
        self.run_python(filename, file_dir=working_dir, working_dir=working_dir)

    def test_documentation_popxl_mnist(self, mnist_datasets_tmpdir):
        """Test the popxl basic mnist example"""
        # Note, mnist.py always trains and tests, so no --test.
        filename = f"mnist.py --datasets-dir={mnist_datasets_tmpdir} --test-batch-size 8 --limit-nbatches 2"
        self.run_python(filename, file_dir=working_dir, working_dir=working_dir)

    def test_documentation_popxl_mnist_rts(self, mnist_datasets_tmpdir):
        """Test the popxl mnist with RTS example"""
        filename = f"mnist_rts.py --datasets-dir={mnist_datasets_tmpdir} --replication-factor 2 --rts --test --test-batch-size 8 --limit-nbatches 2"
        self.run_python(filename, file_dir=working_dir, working_dir=working_dir)
