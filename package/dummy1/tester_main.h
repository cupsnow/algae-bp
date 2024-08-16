/* $Id$
 *
 * Copyright 2024, Dexatek Technology Ltd.
 * This is proprietary information of Dexatek Technology Ltd.
 * All Rights Reserved. Reproduction of this documentation or the
 * accompanying programs in any manner whatsoever without the written
 * permission of Dexatek Technology Ltd. is strictly forbidden.
 *
 * @author joelai
 */

#ifndef TESTER_MAIN_H_
#define TESTER_MAIN_H_

#include <sys/queue.h>

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
		section(stringify2(_name)), \
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

#endif /* TESTER_MAIN_H_ */
