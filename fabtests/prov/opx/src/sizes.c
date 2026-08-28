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

#include <stdio.h>

#include <shared.h>

#include "opx_regression.h"

/*
 * "sizes": send data-integrity sweep across a
 * boundary set.  Every entry exercises a send at that size and lets the
 * shared harness fill (client) and byte-verify (server) the payload via
 * FT_OPT_VERIFY_DATA, so this proves correctness at each size regardless
 * of which internal send path the provider selects.
 *
 * INTERIM: this is a black-box data-integrity test and therefore belongs
 * in the Tier-1 pytest coverage (fabtests/pytest/opx/), not in this
 * bespoke provider harness.  It is kept here only so that the default
 * (non -DOPX_DEBUG_COUNTERS) build has a runnable test until pytest/opx
 * lands (the sel test skips without the debug-counters build).  Supersede
 * and remove this file once the pytest/opx size sweep exists.
 */
static const size_t opx_sizes_sweep[] = {
	0,     1,     7,       8,	511,	 512,	  513,	    2048,
	4095,  4096,  4097,    8191,	8192,	 8193,	  16384,    16385,
	65535, 65536, 1048575, 1048576, 1048577, 4194304, 16777216,
};

#define OPX_SIZES_COUNT (sizeof(opx_sizes_sweep) / sizeof(opx_sizes_sweep[0]))

int opx_run_sizes(void)
{
	int ret;
	size_t i;
	size_t max_size = 0;

	/*
	 * Size the single device allocation to the largest sweep entry
	 * up front (CAIL lesson: allocate once at max and reuse).  The
	 * shared harness derives its buffer size from opts.transfer_size
	 * when FT_OPT_SIZE is set, so pin it to the max before fabric
	 * init and then drive each smaller transfer without reallocating.
	 */
	for (i = 0; i < OPX_SIZES_COUNT; i++) {
		if (opx_sizes_sweep[i] > max_size)
			max_size = opx_sizes_sweep[i];
	}
	opts.transfer_size = max_size;

	ret = ft_init_fabric();
	if (ret)
		return ret;

	for (i = 0; i < OPX_SIZES_COUNT; i++) {
		size_t size = opx_sizes_sweep[i];

		ret = ft_sync();
		if (ret)
			return ret;

		if (opts.dst_addr)
			ret = ft_tx(ep, remote_fi_addr, size, &tx_ctx);
		else
			ret = ft_rx(ep, size);
		if (ret) {
			FT_PRINTERR("opx sizes transfer", ret);
			fprintf(stderr, "opx sizes: failed at size %zu\n",
				size);
			return ret;
		}
	}

	return ft_sync();
}
