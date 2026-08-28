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

#ifndef _OPX_THRESHOLDS_H_
#define _OPX_THRESHOLDS_H_

#include <stddef.h>

#include <rdma/fi_domain.h>

/*
 * Per-interface send-protocol threshold helpers.
 *
 * These return the OPX provider's *default-config* payload floors for a
 * given memory interface so a test can pick sizes without hard-coding a
 * magic number per body.  The values mirror the provider defaults in
 * prov/opx/include/rdma/opx/fi_opx_hfi1.h; a test that overrides a
 * threshold via the FI_OPX_*_MIN_PAYLOAD_BYTES environment variables is
 * the source of truth for that leg and must assert against the override,
 * not these defaults.
 *
 * OPX_UNREACHABLE marks an iface whose protocol window is empty in the
 * default config (e.g. system memory, where the eager-SDMA floor equals
 * the rendezvous floor).  Callers skip such legs.
 */
#define OPX_UNREACHABLE ((size_t) - 1)

size_t opx_iface_eager_sdma_min(enum fi_hmem_iface iface);
size_t opx_iface_rzv_min(enum fi_hmem_iface iface);
size_t opx_iface_hfisvc_min(enum fi_hmem_iface iface);

/* The absolute eager-SDMA floor (OPX_EAGER_SDMA_MIN_PAYLOAD_BYTES): a
 * fixed dispatch-gate clamp that no FI_OPX_*_MIN_PAYLOAD_BYTES override can
 * lower.  A lowering override only takes effect above this value. */
size_t opx_hard_eager_sdma_floor(void);

#endif /* _OPX_THRESHOLDS_H_ */
