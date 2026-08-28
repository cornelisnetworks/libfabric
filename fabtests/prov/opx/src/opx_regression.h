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

#ifndef _OPX_REGRESSION_H_
#define _OPX_REGRESSION_H_

/*
 * OPX provider regression harness.
 *
 * Single multi-test binary (opx_regression) with a function-pointer
 * dispatch table.  Each test body is built on the shared fabtests
 * client-server harness (common/shared.c ft_* API) rather than any
 * provider-private plumbing, so device-memory allocation and byte-exact
 * verification flow through the standard ft_init_fabric / ft_tx / ft_rx
 * path (Model B).
 *
 * A test entry returns 0 on success and non-zero on failure so the
 * binary is usable on release builds (no abort()/assert() as the pass
 * gate).
 */

/* Test entry signature.  Each body parses its own transfer sizes and
 * drives a standard fabtests client-server flow; the harness globals
 * (opts, ep, remote_fi_addr) carry the shared state.
 */
typedef int (*opx_testfn)(void);

struct opx_test {
	opx_testfn run;
	const char *name;
};

/*
 * SIZES: send data-integrity size sweep
 *
 * Drives the canonical boundary sizes (0, 1, 7, 8, 511, 512, 513, ...
 * up to a few MiB) over the selected memory interface and byte-verifies
 * every transfer device->host.  It does not assert which send path was
 * taken; it only proves data integrity at every size, so it runs on the
 * default (non-counters) build.  The device buffer is allocated once at
 * the maximum sweep size and reused.  Fails on any transfer error
 * (including the historical eager-SDMA writev -EINVAL) or byte mismatch.
 */
int opx_run_sizes(void);

/*
 * SEL: send-protocol selection at the OPX payload-size thresholds
 *
 * Does exactly one sized transfer per invocation and asserts which send
 * path the provider selected, observed via OPX debug counters parsed from
 * the endpoint teardown dump (requires a libfabric built with
 * -DOPX_DEBUG_COUNTERS).  The size and expected path come from --size and
 * --expect (set via opx_sel_set_size / opx_sel_set_expect); the multi-
 * size, multi-threshold sweep is driven by the wrapper, one invocation
 * per boundary point with a fresh 2-node OOB handshake.  --expect selects
 * the oracle: sdma, pio, rzv, hfisvc.
 */
int opx_run_sel(void);
void opx_sel_set_size(size_t size);
int opx_sel_set_expect(const char *what);

#endif /* _OPX_REGRESSION_H_ */
