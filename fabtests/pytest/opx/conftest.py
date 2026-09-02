import pytest

# OPX provider memory-type fixtures.
#
# The opx provider supports both ROCr (AMD) and CUDA (NVIDIA) device memory;
# both are covered here. host_to_host always runs. The device
# legs are marked so they can be selected/deselected (-m rocr_memory) and
# ClientServerTest.prepare_base_command auto-skips a leg when the peer has no
# matching HMEM device (num_hmem_devices == 0), so these run cleanly on a
# host-only node without a manual skip.
@pytest.fixture(scope="module", params=["host_to_host",
                                        pytest.param("host_to_rocr", marks=pytest.mark.rocr_memory),
                                        pytest.param("rocr_to_host", marks=pytest.mark.rocr_memory),
                                        pytest.param("rocr_to_rocr", marks=pytest.mark.rocr_memory),
                                        pytest.param("host_to_cuda", marks=pytest.mark.cuda_memory),
                                        pytest.param("cuda_to_host", marks=pytest.mark.cuda_memory),
                                        pytest.param("cuda_to_cuda", marks=pytest.mark.cuda_memory)])
def memory_type(request):
    return request.param
