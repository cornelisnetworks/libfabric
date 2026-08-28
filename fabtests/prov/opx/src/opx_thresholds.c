/*
 * Copyright (c) 2026 Cornelis Networks, Inc.  All rights reserved.
 *
 * This software is available to you under the BSD license
 * below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "opx_thresholds.h"

/*
 * Default-config floors from prov/opx/include/rdma/opx/fi_opx_hfi1.h:
 *   OPX_EAGER_SDMA_MIN_PAYLOAD_BYTES        = 512
 *   ROCr:   eager-SDMA 512   / RZV 8192    / HFISVC 16384
 *   CUDA:   eager-SDMA 4096  / RZV 4096    / HFISVC 4096
 *   system: eager-SDMA/RZV/HFISVC all 16385 (eager-SDMA window empty:
 *           the floor equals the rendezvous floor, so no size selects
 *           eager-SDMA -> OPX_UNREACHABLE).
 */
#define OPX_HARD_EAGER_SDMA_FLOOR 512

#define OPX_ROCR_EAGER_SDMA_MIN 512
#define OPX_ROCR_RZV_MIN	8192
#define OPX_ROCR_HFISVC_MIN	16384

#define OPX_CUDA_EAGER_SDMA_MIN 4096
#define OPX_CUDA_RZV_MIN	4096
#define OPX_CUDA_HFISVC_MIN	4096

#define OPX_SYSTEM_RZV_MIN    16385
#define OPX_SYSTEM_HFISVC_MIN 16385

size_t opx_hard_eager_sdma_floor(void)
{
	return OPX_HARD_EAGER_SDMA_FLOOR;
}

size_t opx_iface_eager_sdma_min(enum fi_hmem_iface iface)
{
	switch (iface) {
	case FI_HMEM_ROCR:
		return OPX_ROCR_EAGER_SDMA_MIN;
	case FI_HMEM_CUDA:
		return OPX_CUDA_EAGER_SDMA_MIN;
	default:
		return OPX_UNREACHABLE;
	}
}

size_t opx_iface_rzv_min(enum fi_hmem_iface iface)
{
	switch (iface) {
	case FI_HMEM_ROCR:
		return OPX_ROCR_RZV_MIN;
	case FI_HMEM_CUDA:
		return OPX_CUDA_RZV_MIN;
	default:
		return OPX_SYSTEM_RZV_MIN;
	}
}

size_t opx_iface_hfisvc_min(enum fi_hmem_iface iface)
{
	switch (iface) {
	case FI_HMEM_ROCR:
		return OPX_ROCR_HFISVC_MIN;
	case FI_HMEM_CUDA:
		return OPX_CUDA_HFISVC_MIN;
	default:
		return OPX_SYSTEM_HFISVC_MIN;
	}
}
