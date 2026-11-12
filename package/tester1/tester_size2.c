/* $Id$
 *
 * This file is part of the project esh-ws
 *
 * Nov 10, 2026
 *
 * @author joelai
 *
 * @file /esh-ws/agt-app/esghub/app-dk/dexatek/main_application/main_size_test.c
 * @brief main_size_test
 */

#include "tester_main.h"
#include "priv.h"

static int tester_size2_main(void *ctx, int argc, const char **argv) {
	dump_argv(argc, argv);
	return 0;
}

TESTER_SECTION_ENTRY(tester_size2, tester_size2_main);
