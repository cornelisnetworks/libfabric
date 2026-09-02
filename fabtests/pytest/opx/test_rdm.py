import pytest
from opx.opx_common import opx_run_client_server_test


@pytest.mark.parametrize("iteration_type",
                         [pytest.param("short", marks=pytest.mark.short),
                          pytest.param("standard", marks=pytest.mark.standard)])
def test_rdm_pingpong(cmdline_args, iteration_type, completion_semantic, memory_type, completion_type):
    opx_run_client_server_test(cmdline_args, "fi_rdm_pingpong", iteration_type,
                               completion_semantic, memory_type, completion_type=completion_type)


@pytest.mark.parametrize("iteration_type",
                         [pytest.param("short", marks=pytest.mark.short),
                          pytest.param("standard", marks=pytest.mark.standard)])
def test_rdm_tagged_pingpong(cmdline_args, iteration_type, completion_semantic, memory_type, completion_type):
    opx_run_client_server_test(cmdline_args, "fi_rdm_tagged_pingpong", iteration_type,
                               completion_semantic, memory_type, completion_type=completion_type)


@pytest.mark.parametrize("iteration_type",
                         [pytest.param("short", marks=pytest.mark.short),
                          pytest.param("standard", marks=pytest.mark.standard)])
def test_rdm_tagged_bw(cmdline_args, iteration_type, completion_semantic, memory_type, completion_type):
    opx_run_client_server_test(cmdline_args, "fi_rdm_tagged_bw", iteration_type,
                               completion_semantic, memory_type, completion_type=completion_type)
