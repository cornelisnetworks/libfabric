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

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <shared.h>

#include "opx_regression.h"
#include "opx_thresholds.h"

/*
 * Send-protocol selection at the OPX payload-size thresholds.
 *
 * This test asserts which send path the provider selects across the
 * payload-size ranges that bound each protocol transition:
 *   - below the device SDMA floor          => PIO eager
 *   - at/above the SDMA floor, below RZV    => eager-SDMA (8-byte aligned)
 *   - at/above the RZV minimum, below HFISVC => rendezvous via SDMA/TID DPUT
 *   - at/above the HFISVC minimum           => rendezvous via HFISVC RTS
 *
 * A device transfer that is size-eligible for eager-SDMA or RZV device
 * DPUT is still forced onto PIO when the driver cannot export a usable
 * dma-buf fd (no HFI1_CAP_DMABUF).  The caller declares which outcome is
 * expected: the strict tokens (sdma, rzv-sdma, rzv-tid) require the device
 * transport and FAIL on a PIO demotion, while the *-pio-fallback tokens
 * require the PIO demotion.  Flip a leg from a fallback token to its strict
 * token once the driver gains dma-buf support.
 *
 * It does exactly ONE sized transfer per process invocation and asserts
 * the selected path.  The multi-size, multi-threshold sweep is driven by
 * the caller.  One invocation == one fabric init, one transfer, one teardown
 * counter dump. This keeps the 2-node OOB connection in lockstep (every
 * other fabtest works the same way) and lets each process's teardown
 * counter dump be captured cleanly, instead of re-initializing the
 * fabric several times inside a single process.
 *
 * The size and the expected protocol come from the command line:
 *   opx_regression sel --size=<n>
 *       --expect=<sdma|pio|pio-fallback|rzv-sdma|rzv-tid|rzv-pio-fallback|hfisvc>
 *       [-E -D rocr -i 0 dst]
 * rzv-sdma / rzv-tid assert the specific RZV device DPUT sub-transport, and
 * hfisvc asserts the HFISVC device-RDMA RTS path (only reachable with
 * FI_OPX_HFISVC=1).  Legs run without FI_OPX_HFISVC exercise the non-HFISVC
 * device SDMA/RZV path, which is not forced onto PIO.
 * If --size is omitted the test falls back to the iface eager-SDMA floor
 * and expects eager-SDMA.
 *
 * The eager-SDMA dispatch gate has two independent size conditions: the
 * payload must be >= the device SDMA floor AND 8-byte aligned.  The
 * wrapper isolates each by choosing sizes such as floor-8 (aligned,
 * below floor => PIO), floor (aligned, at floor => eager-SDMA), and
 * floor+1 (>= floor but misaligned => PIO).
 *
 * The OPX provider only records which send path was taken when libfabric
 * is built with -DOPX_DEBUG_COUNTERS, and it prints those counters to
 * stderr when the endpoint is torn down.  The counters are therefore not
 * readable as C symbols on a default build (they compile away).  To keep
 * this test buildable everywhere AND self-asserting on a counters build,
 * we capture the provider's stderr counter dump (a single, one-shot
 * redirect around this one init->transfer->teardown cycle) and scan it
 * for the named counter lines.
 *
 * Counter-line format emitted by fi_opx_debug_counters_print():
 *   (<pid>) ### <name padded to 50> <value grouped to 15> <unit>
 * The value is locale thousands-grouped, so non-digits are stripped
 * before parsing.  A counter that stayed zero is not printed at all
 * (nonzero-only), so an absent line reads as zero.
 */

/* Names emitted by the provider's counter dump (see
 * prov/opx/include/rdma/opx/fi_opx_debug_counters.h).  All of these are
 * compiled in by a single -DOPX_DEBUG_COUNTERS build, which enables the
 * SDMA, HMEM and HFISVC counter groups together. */
#define OPX_SEL_CNT_EAGER_SDMA "sdma.eager_sends"
#define OPX_SEL_CNT_PIO_EAGER  "hmem.hfi.kind[tag].send.eager"
#define OPX_SEL_CNT_FORCED_PIO "hmem.sdma_forced_pio_hfisvc"
/* Rendezvous (RZV) selection: the provider records a tagged HMEM RZV send
 * as one of the .send.rzv / .send.rzv_noncontig counters, and the DPUT
 * data-movement transport it chose as one of the dput_rzv_* counters. */
#define OPX_SEL_CNT_RZV	       "hmem.hfi.kind[tag].send.rzv"
#define OPX_SEL_CNT_RZV_NONCONTIG "hmem.hfi.kind[tag].send.rzv_noncontig"
#define OPX_SEL_CNT_DPUT_SDMA  "hmem.dput_rzv_sdma"
#define OPX_SEL_CNT_DPUT_TID   "hmem.dput_rzv_tid"
#define OPX_SEL_CNT_DPUT_PIO   "hmem.dput_rzv_pio"
/* HFISVC rendezvous RTS: a nonzero attempt count means the RZV send took
 * the HFISVC device-RDMA RTS path rather than the SDMA/TID DPUT path. */
#define OPX_SEL_CNT_HFISVC_RTS "hfisvc.rzv_send_rts.attempt"

/* Which send path this invocation is expected to select. */
enum opx_sel_proto {
	OPX_SEL_PROTO_PIO_EAGER,
	OPX_SEL_PROTO_EAGER_SDMA,
	OPX_SEL_PROTO_PIO_FALLBACK, /* SDMA-eligible size demoted to PIO: no dma-buf */
	OPX_SEL_PROTO_RZV_SDMA,	  /* rendezvous via non-TID SDMA DPUT */
	OPX_SEL_PROTO_RZV_TID,	  /* rendezvous via expected-TID SDMA DPUT */
	OPX_SEL_PROTO_RZV_PIO_FALLBACK, /* RZV DPUT demoted to PIO: no dma-buf */
	OPX_SEL_PROTO_HFISVC_RTS, /* rendezvous via HFISVC device-RDMA RTS */
};

/* Per-invocation request, set from the command line by main.c before the
 * test runs.  opx_run_sel() takes no arguments (the harness dispatch
 * signature), so the request is carried in file scope. */
static size_t opx_sel_req_size;
static int opx_sel_req_size_set;
static enum opx_sel_proto opx_sel_req_expect = OPX_SEL_PROTO_EAGER_SDMA;

void opx_sel_set_size(size_t size)
{
	opx_sel_req_size = size;
	opx_sel_req_size_set = 1;
}

/* Returns 0 on a recognised value, -1 otherwise. */
int opx_sel_set_expect(const char *what)
{
	if (!strcmp(what, "sdma") || !strcmp(what, "eager-sdma")) {
		opx_sel_req_expect = OPX_SEL_PROTO_EAGER_SDMA;
		return 0;
	}
	if (!strcmp(what, "pio") || !strcmp(what, "pio-eager")) {
		opx_sel_req_expect = OPX_SEL_PROTO_PIO_EAGER;
		return 0;
	}
	if (!strcmp(what, "pio-fallback") || !strcmp(what, "sdma-pio-fallback")) {
		opx_sel_req_expect = OPX_SEL_PROTO_PIO_FALLBACK;
		return 0;
	}
	if (!strcmp(what, "rzv-sdma")) {
		opx_sel_req_expect = OPX_SEL_PROTO_RZV_SDMA;
		return 0;
	}
	if (!strcmp(what, "rzv-tid")) {
		opx_sel_req_expect = OPX_SEL_PROTO_RZV_TID;
		return 0;
	}
	if (!strcmp(what, "rzv-pio-fallback") || !strcmp(what, "rzv-pio")) {
		opx_sel_req_expect = OPX_SEL_PROTO_RZV_PIO_FALLBACK;
		return 0;
	}
	if (!strcmp(what, "hfisvc") || !strcmp(what, "hfisvc-rts")) {
		opx_sel_req_expect = OPX_SEL_PROTO_HFISVC_RTS;
		return 0;
	}
	return -1;
}

struct opx_sel_counters {
	unsigned long eager_sdma;
	unsigned long pio_eager;
	unsigned long forced_pio;
	unsigned long rzv;
	unsigned long rzv_noncontig;
	unsigned long dput_sdma;
	unsigned long dput_tid;
	unsigned long dput_pio;
	unsigned long hfisvc_rts;
	int have_any; /* at least one OPX counter line was seen */
};

/* Parse "(pid) ### <name> <value> <unit>" for the given counter name.
 * Returns the value with thousands separators stripped, or 0 when the
 * counter line is absent (absent == stayed zero). */
static unsigned long opx_sel_scan_value(const char *text, const char *name)
{
	const char *p = text;
	size_t namelen = strlen(name);

	while ((p = strstr(p, name)) != NULL) {
		const char *q = p + namelen;
		unsigned long val = 0;
		int saw_digit = 0;

		/* Advance to the value field; it is the first run of digits
		 * (optionally comma-grouped) after the counter name. */
		while (*q && *q != '\n') {
			if (isdigit((unsigned char) *q)) {
				val = val * 10 + (unsigned long) (*q - '0');
				saw_digit = 1;
			} else if (*q == ',') {
				/* thousands separator - skip */
			} else if (saw_digit) {
				break;
			}
			q++;
		}
		if (saw_digit)
			return val;
		p += namelen;
	}
	return 0;
}

/* Read the whole of an already-rewound stream into a heap buffer.  Returns
 * a NUL-terminated buffer (caller frees) or NULL on error. */
static char *opx_sel_slurp(FILE *fp)
{
	char *text = NULL;
	size_t len = 0;
	size_t cap = 0;
	char buf[4096];
	size_t n;

	while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
		if (len + n + 1 > cap) {
			size_t newcap = cap ? cap * 2 : 8192;
			char *grown;

			while (newcap < len + n + 1)
				newcap *= 2;
			grown = realloc(text, newcap);
			if (!grown) {
				free(text);
				return NULL;
			}
			text = grown;
			cap = newcap;
		}
		memcpy(text + len, buf, n);
		len += n;
	}
	if (!text) {
		text = malloc(1);
		if (!text)
			return NULL;
	}
	text[len] = '\0';
	return text;
}

/* Run one init->transfer->teardown cycle with this process's stderr
 * redirected to a temporary file, so the provider's teardown counter
 * dump is captured.  On return *out holds the parsed counters and *xret
 * holds the transfer status.  Returns 0 if the capture machinery worked
 * (regardless of the transfer status), non-zero on a capture/setup error.
 *
 * This is a single, one-shot redirect around a single fabric lifecycle in
 * this process - there is no fork and no repeated init/teardown, so it
 * does not disturb the 2-node OOB handshake or the shared-harness globals
 * the way a per-measurement loop would. */
static int opx_sel_measure(size_t size, struct opx_sel_counters *out, int *xret)
{
	char tmpl[] = "/tmp/opx_sel_ctrs.XXXXXX";
	int cap_fd;
	int saved_stderr;
	long flen;
	FILE *fp;
	char *text;
	int ret;

	memset(out, 0, sizeof(*out));
	*xret = 0;

	cap_fd = mkstemp(tmpl);
	if (cap_fd < 0) {
		FT_PRINTERR("opx sel mkstemp", -errno);
		return -errno;
	}
	unlink(tmpl);

	fflush(stderr);
	saved_stderr = dup(STDERR_FILENO);
	if (saved_stderr < 0) {
		ret = -errno;
		close(cap_fd);
		return ret;
	}
	if (dup2(cap_fd, STDERR_FILENO) < 0) {
		ret = -errno;
		close(cap_fd);
		close(saved_stderr);
		return ret;
	}

	/* Pin the single transfer size before init so the shared harness
	 * allocates the device buffer at this size (allocate-once, reuse). */
	opts.transfer_size = size ? size : 1;

	ret = ft_init_fabric();
	if (!ret) {
		ret = ft_sync();
		if (!ret) {
			if (opts.dst_addr)
				ret = ft_tx(ep, remote_fi_addr, size, &tx_ctx);
			else
				ret = ft_rx(ep, size);
		}
		if (!ret)
			ret = ft_sync();
	}

	/* Tear down the endpoint so the provider dumps its counters into the
	 * captured stderr before we restore the real stderr.  ft_free_res is
	 * idempotent, so main.c's later call is a harmless no-op. */
	(void) ft_free_res();

	fflush(stderr);
	dup2(saved_stderr, STDERR_FILENO);
	close(saved_stderr);

	*xret = ret;

	/* Read back and re-emit the captured output to the real stderr so it
	 * is not lost from the job log, then parse it. */
	flen = lseek(cap_fd, 0, SEEK_SET);
	fp = fdopen(cap_fd, "r");
	if (!fp) {
		ret = -errno;
		close(cap_fd);
		return ret;
	}
	(void) flen;
	text = opx_sel_slurp(fp);
	fclose(fp); /* also closes cap_fd */
	if (!text)
		return -FI_ENOMEM;

	fputs(text, stderr);
	fflush(stderr);
	out->have_any = (strstr(text, ") ### ") != NULL);
	out->eager_sdma = opx_sel_scan_value(text, OPX_SEL_CNT_EAGER_SDMA);
	out->pio_eager = opx_sel_scan_value(text, OPX_SEL_CNT_PIO_EAGER);
	out->forced_pio = opx_sel_scan_value(text, OPX_SEL_CNT_FORCED_PIO);
	out->rzv = opx_sel_scan_value(text, OPX_SEL_CNT_RZV);
	out->rzv_noncontig = opx_sel_scan_value(text, OPX_SEL_CNT_RZV_NONCONTIG);
	out->dput_sdma = opx_sel_scan_value(text, OPX_SEL_CNT_DPUT_SDMA);
	out->dput_tid = opx_sel_scan_value(text, OPX_SEL_CNT_DPUT_TID);
	out->dput_pio = opx_sel_scan_value(text, OPX_SEL_CNT_DPUT_PIO);
	out->hfisvc_rts = opx_sel_scan_value(text, OPX_SEL_CNT_HFISVC_RTS);
	free(text);

	return 0;
}

/* Assert that this invocation selected the expected protocol.  Returns 0
 * on a match (or an expected skip), -FI_EIO on a mismatch, or the transfer
 * error. */
static int opx_sel_check(size_t size, enum opx_sel_proto expect)
{
	struct opx_sel_counters c;
	int xret;
	int ret;

	ret = opx_sel_measure(size, &c, &xret);
	if (ret) {
		fprintf(stderr, "opx sel: capture setup failed at size %zu\n",
			size);
		return ret;
	}
	if (xret) {
		fprintf(stderr, "opx sel: transfer failed at size %zu\n", size);
		return xret;
	}

	if (!c.have_any) {
		printf("opx sel: SKIP size %zu - no OPX debug counters "
		       "(build libfabric with -DOPX_DEBUG_COUNTERS)\n",
		       size);
		return 0;
	}

	if (expect == OPX_SEL_PROTO_EAGER_SDMA) {
		if (c.eager_sdma > 0 && c.forced_pio == 0) {
			printf("opx sel: size %zu selected eager-SDMA (ok)\n",
			       size);
			return 0;
		}
		fprintf(stderr,
			"opx sel: size %zu expected eager-SDMA but counters "
			"show eager_sdma=%lu pio_eager=%lu forced_pio=%lu\n",
			size, c.eager_sdma, c.pio_eager, c.forced_pio);
		return -FI_EIO;
	}

	if (expect == OPX_SEL_PROTO_PIO_FALLBACK) {
		if (c.forced_pio > 0 && c.eager_sdma == 0) {
			printf("opx sel: size %zu SDMA-eligible, demoted to PIO "
			       "(forced_pio=%lu) (ok)\n",
			       size, c.forced_pio);
			return 0;
		}
		fprintf(stderr,
			"opx sel: size %zu expected forced-PIO fallback but "
			"counters show eager_sdma=%lu pio_eager=%lu "
			"forced_pio=%lu\n",
			size, c.eager_sdma, c.pio_eager, c.forced_pio);
		return -FI_EIO;
	}

	if (expect == OPX_SEL_PROTO_RZV_SDMA) {
		unsigned long rzv = c.rzv + c.rzv_noncontig;

		if (rzv == 0 || c.hfisvc_rts > 0) {
			fprintf(stderr,
				"opx sel: size %zu expected RZV (non-TID SDMA "
				"DPUT) but counters show rzv=%lu "
				"rzv_noncontig=%lu hfisvc_rts=%lu\n",
				size, c.rzv, c.rzv_noncontig, c.hfisvc_rts);
			return -FI_EIO;
		}
		if (c.dput_sdma > 0 && c.dput_tid == 0 && c.dput_pio == 0) {
			printf("opx sel: size %zu selected RZV via non-TID SDMA "
			       "DPUT (sdma=%lu) (ok)\n",
			       size, c.dput_sdma);
			return 0;
		}
		fprintf(stderr,
			"opx sel: size %zu expected RZV non-TID SDMA DPUT but "
			"counters show dput_sdma=%lu dput_tid=%lu dput_pio=%lu\n",
			size, c.dput_sdma, c.dput_tid, c.dput_pio);
		return -FI_EIO;
	}

	if (expect == OPX_SEL_PROTO_RZV_TID) {
		unsigned long rzv = c.rzv + c.rzv_noncontig;

		if (rzv == 0 || c.hfisvc_rts > 0) {
			fprintf(stderr,
				"opx sel: size %zu expected RZV (expected-TID "
				"DPUT) but counters show rzv=%lu "
				"rzv_noncontig=%lu hfisvc_rts=%lu\n",
				size, c.rzv, c.rzv_noncontig, c.hfisvc_rts);
			return -FI_EIO;
		}
		if (c.dput_tid > 0 && c.dput_pio == 0) {
			printf("opx sel: size %zu selected RZV via expected-TID "
			       "DPUT (tid=%lu) (ok)\n",
			       size, c.dput_tid);
			return 0;
		}
		fprintf(stderr,
			"opx sel: size %zu expected RZV expected-TID DPUT but "
			"counters show dput_sdma=%lu dput_tid=%lu dput_pio=%lu\n",
			size, c.dput_sdma, c.dput_tid, c.dput_pio);
		return -FI_EIO;
	}

	if (expect == OPX_SEL_PROTO_RZV_PIO_FALLBACK) {
		unsigned long rzv = c.rzv + c.rzv_noncontig;

		if (rzv == 0 || c.hfisvc_rts > 0) {
			fprintf(stderr,
				"opx sel: size %zu expected RZV (PIO DPUT "
				"fallback) but counters show rzv=%lu "
				"rzv_noncontig=%lu hfisvc_rts=%lu\n",
				size, c.rzv, c.rzv_noncontig, c.hfisvc_rts);
			return -FI_EIO;
		}
		if (c.dput_pio > 0 && (c.dput_sdma + c.dput_tid) == 0) {
			printf("opx sel: size %zu RZV demoted to PIO DPUT "
			       "(dput_pio=%lu) (ok)\n",
			       size, c.dput_pio);
			return 0;
		}
		fprintf(stderr,
			"opx sel: size %zu expected RZV PIO DPUT fallback but "
			"counters show dput_sdma=%lu dput_tid=%lu dput_pio=%lu\n",
			size, c.dput_sdma, c.dput_tid, c.dput_pio);
		return -FI_EIO;
	}

	if (expect == OPX_SEL_PROTO_HFISVC_RTS) {
		unsigned long rzv = c.rzv + c.rzv_noncontig;

		if (c.hfisvc_rts > 0 && rzv > 0) {
			printf("opx sel: size %zu selected RZV via HFISVC RTS "
			       "(attempt=%lu) (ok)\n",
			       size, c.hfisvc_rts);
			return 0;
		}
		fprintf(stderr,
			"opx sel: size %zu expected HFISVC RTS but counters "
			"show rzv=%lu rzv_noncontig=%lu hfisvc_rts=%lu\n",
			size, c.rzv, c.rzv_noncontig, c.hfisvc_rts);
		return -FI_EIO;
	}

	/* OPX_SEL_PROTO_PIO_EAGER */
	if (c.pio_eager > 0 && c.eager_sdma == 0) {
		printf("opx sel: size %zu selected PIO-eager (ok)\n", size);
		return 0;
	}
	fprintf(stderr,
		"opx sel: size %zu expected PIO-eager but counters show "
		"eager_sdma=%lu pio_eager=%lu forced_pio=%lu\n",
		size, c.eager_sdma, c.pio_eager, c.forced_pio);
	return -FI_EIO;
}

int opx_run_sel(void)
{
	size_t size;
	enum opx_sel_proto expect;

	if (opx_sel_req_size_set) {
		size = opx_sel_req_size;
		expect = opx_sel_req_expect;
	} else {
		/* No explicit size: fall back to the iface eager-SDMA floor
		 * and expect eager-SDMA.  A floor of OPX_UNREACHABLE means the
		 * iface has no reachable eager-SDMA window (e.g. system), so
		 * there is nothing to assert (R10 expected, not a failure). */
		size = opx_iface_eager_sdma_min(opts.iface);
		if (size == OPX_UNREACHABLE) {
			printf("opx sel: SKIP iface %d - no reachable "
			       "eager-SDMA window in the default config\n",
			       (int) opts.iface);
			return 0;
		}
		expect = OPX_SEL_PROTO_EAGER_SDMA;
	}

	return opx_sel_check(size, expect);
}
