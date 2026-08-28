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

/*
 * opx_regression: multi-test binary for OPX provider regression tests.
 *
 * How to add a test (e.g. "spNN"):
 *   1. Add prov/opx/src/spNN.c implementing "int opx_run_spNN(void);".
 *   2. Declare its prototype in prov/opx/src/opx_regression.h.
 *   3. Add a "{opx_run_spNN, "spNN"}" entry to opx_testlist[] below.
 *   4. Add spNN.c to OPX_REGRESSION_SRCS in prov/opx/Makefile.include.
 *
 * Steps 3 and 4 are what wire the test into the binary and the build;
 * configure.ac and Makefile.am need no further changes.
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <shared.h>

#include "opx_regression.h"

static const struct opx_test opx_testlist[] = {
	{opx_run_sizes, "sizes"},
	{opx_run_sel, "sel"},
};

#define OPX_NUM_TESTS (sizeof(opx_testlist) / sizeof(opx_testlist[0]))

static const char *test_filter;

/* A test is selected when no filter is set, or when the filter is a
 * substring of the test name.  A leading '!' inverts the match so a
 * single test can be excluded.
 */
static int opx_test_selected(const char *name)
{
	if (!test_filter)
		return 1;

	if (test_filter[0] == '!')
		return strstr(name, test_filter + 1) == NULL;

	return strstr(name, test_filter) != NULL;
}

/* Long-only options consumed before the shared getopt string.  --size and
 * --expect drive the single-transfer sel test (see prov/opx/src/sel.c);
 * they use values outside the ASCII range so they never collide with the
 * short options in ADDR_OPTS / INFO_OPTS. */
enum {
	OPX_OPT_SIZE = 256,
	OPX_OPT_EXPECT,
};

static const struct option opx_long_opts[] = {
	{"size", required_argument, NULL, OPX_OPT_SIZE},
	{"expect", required_argument, NULL, OPX_OPT_EXPECT},
	{NULL, 0, NULL, 0},
};

static void opx_usage(char *name)
{
	ft_usage(name, "OPX provider regression harness");
	FT_PRINT_OPTS_USAGE("-f <filter>",
			    "run only tests whose name matches <filter> "
			    "(prefix '!' to invert)");
	FT_PRINT_OPTS_USAGE("-l", "list available tests and exit");
	FT_PRINT_OPTS_USAGE("--size <n>", "sel: transfer size in bytes");
	FT_PRINT_OPTS_USAGE(
		"--expect <sdma|pio|pio-fallback|rzv-sdma|rzv-tid|"
		"rzv-pio-fallback|hfisvc>",
		"sel: expected send path at --size");
}

int main(int argc, char **argv)
{
	int op, ret, cleanup_ret;
	int failures = 0;
	size_t run_count = 0;
	size_t i;

	opts = INIT_OPTS;
	/* FT_OPT_SIZE lets tests drive explicit transfer sizes;
	 * FT_OPT_VERIFY_DATA makes ft_tx/ft_rx fill and byte-check the
	 * device buffers through the shared ft_hmem_* path.
	 */
	opts.options |= FT_OPT_SIZE | FT_OPT_VERIFY_DATA;

	hints = fi_allocinfo();
	if (!hints)
		return EXIT_FAILURE;

	while ((op = getopt_long(argc, argv, "f:lh" ADDR_OPTS INFO_OPTS,
				 opx_long_opts, NULL)) != -1) {
		switch (op) {
		default:
			ft_parse_addr_opts(op, optarg, &opts);
			ft_parseinfo(op, optarg, hints, &opts);
			break;
		case 'f':
			test_filter = optarg;
			break;
		case 'l':
			for (i = 0; i < OPX_NUM_TESTS; i++)
				printf("%s\n", opx_testlist[i].name);
			fi_freeinfo(hints);
			return EXIT_SUCCESS;
		case OPX_OPT_SIZE:
			opx_sel_set_size((size_t) strtoull(optarg, NULL, 0));
			break;
		case OPX_OPT_EXPECT:
			if (opx_sel_set_expect(optarg)) {
				fprintf(stderr,
					"invalid --expect '%s' (use sdma|pio|"
					"pio-fallback|rzv-sdma|rzv-tid|"
					"rzv-pio-fallback|hfisvc)\n",
					optarg);
				fi_freeinfo(hints);
				return EXIT_FAILURE;
			}
			break;
		case '?':
		case 'h':
			opx_usage(argv[0]);
			fi_freeinfo(hints);
			return EXIT_FAILURE;
		}
	}

	/* A leading positional matching a test name is consumed as an implicit
	 * filter so it is not mistaken for dst_addr; the next positional is the
	 * server address.
	 */
	if (optind < argc && !test_filter) {
		for (i = 0; i < OPX_NUM_TESTS; i++) {
			if (!strcmp(argv[optind], opx_testlist[i].name)) {
				test_filter = argv[optind];
				optind++;
				break;
			}
		}
	}

	if (optind < argc)
		opts.dst_addr = argv[optind];

	hints->ep_attr->type = FI_EP_RDM;
	hints->caps = FI_MSG;
	hints->mode = FI_CONTEXT | FI_CONTEXT2;
	hints->domain_attr->mr_mode = opts.mr_mode;
	hints->addr_format = opts.address_format;

	for (i = 0; i < OPX_NUM_TESTS; i++) {
		if (!opx_test_selected(opx_testlist[i].name))
			continue;

		run_count++;
		printf("Running %s...\n", opx_testlist[i].name);
		ret = opx_testlist[i].run();
		if (ret) {
			failures++;
			/* FT_PRINTERR requires a string-literal first arg
			 * (it is token-pasted into the format string), so the
			 * runtime test name is reported via a plain fprintf.
			 */
			fprintf(stderr, "%s(): ret=%d (%s)\n",
				opx_testlist[i].name, ret, fi_strerror(-ret));
		}
	}

	cleanup_ret = ft_free_res();
	if (cleanup_ret && !failures)
		failures++;

	if (!run_count) {
		fprintf(stderr, "No tests matched filter\n");
		return EXIT_FAILURE;
	}

	printf("%zu/%zu tests done, %d failed\n", run_count, OPX_NUM_TESTS,
	       failures);

	if (failures) {
		fprintf(stderr, "FAILURE\n");
		return EXIT_FAILURE;
	}

	printf("SUCCESS\n");
	return EXIT_SUCCESS;
}
