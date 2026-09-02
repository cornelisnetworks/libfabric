import pytest
from opx.opx_common import opx_run_client_server_test


# fi_rma_bw covers the RMA path (read/write/writedata); device legs
# auto-skip when the peer has no HMEM device.
@pytest.mark.parametrize("operation_type", ["read", "write", "writedata"])
@pytest.mark.parametrize("iteration_type",
                         [pytest.param("short", marks=pytest.mark.short),
                          pytest.param("standard", marks=pytest.mark.standard)])
def test_rma_bw(cmdline_args, iteration_type, operation_type, completion_semantic, memory_type):
    command = "fi_rma_bw -e rdm -o " + operation_type
    opx_run_client_server_test(cmdline_args, command, iteration_type,
                               completion_semantic, memory_type)
