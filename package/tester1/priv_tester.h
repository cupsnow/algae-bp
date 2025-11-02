/* $Id$
 *
 * Copyright (c) 2025, joelai
 * All Rights Reserved
 *
 * SPDX-License-Identifier: MIT
 *
 * @file noname
 * @brief noname
 */

#ifndef PRIV_TESTER_H_
#define PRIV_TESTER_H_

#include "priv.h"

#ifdef __cplusplus
extern "C" {
#endif

//   if <SECTION NAME> is a valid c variable name
// symbol auto defined by linker when referenced
//   __start_<SECTION NAME>,
//   __stop_<SECTION NAME> (the address after the section)
//   here declared as array to reference the symbol
// and at least 1 trigger linker to define these symbol

#define TESTER_SECTION_ALIGN 64
#define TESTER_SECTION(_name) extern char __start_ ## _name[1]; \
	extern char __stop_ ## _name[1];
#define TESTER_SECTION_ATTR(_name) __attribute__(( \
		used, \
		section(aloe_stringify(_name)), \
		aligned(TESTER_SECTION_ALIGN) \
))

TESTER_SECTION(_tester_section)

typedef const struct tester_test_rec {
	const char *name;
	int (*run)(int level, int argc, const char **argv);
} tester_test_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PRIV_TESTER_H_ */
