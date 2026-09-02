import pytest
from opx.opx_common import opx_run_client_server_test


# fi_msg_pingpong / fi_msg_bw exercise the connectionless MSG path for the
# draft SP functional-reliability sweeps; device legs auto-skip when the peer
# has no HMEM device.
@pytest.mark.parametrize("iteration_type",
                         [pytest.param("short", marks=pytest.mark.short),
                          pytest.param("standard", marks=pytest.mark.standard)])
def test_msg_pingpong(cmdline_args, iteration_type, completion_semantic, memory_type):
    opx_run_client_server_test(cmdline_args, "fi_msg_pingpong", iteration_type,
                               completion_semantic, memory_type)


@pytest.mark.parametrize("iteration_type",
                         [pytest.param("short", marks=pytest.mark.short),
                          pytest.param("standard", marks=pytest.mark.standard)])
def test_msg_bw(cmdline_args, iteration_type, completion_semantic, memory_type):
    opx_run_client_server_test(cmdline_args, "fi_msg_bw", iteration_type,
                               completion_semantic, memory_type)
