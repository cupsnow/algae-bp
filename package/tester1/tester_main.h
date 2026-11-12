/* $Id$
 *
 * This file is part of the project esh-ws
 *
 * Nov 10, 2026
 *
 * @author joelai
 *
 * @file tester_main.h
 * @brief tester_util
 */

#ifndef TESTER_MAIN_H_
#define TESTER_MAIN_H_

/**
 *
 * Assume linker (GCC) automatic create symbol for the section name "tester_section"
 *
 *     __start_tester_section - start of the section
 *     __stop_tester_section - the address after the section
 *
 * For using nm to check the "test_section" size, strip with retain the symbol, ie. command hint
 *
 *     strip -K __start_tester_section -K __stop_tester_section
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

#define TESTER_SECTION_STRINGIFY(_s) # _s
#define TESTER_SECTION_STRINGIFY2(_s) TESTER_SECTION_STRINGIFY(_s)

#define TESTER_SECTION_ALIGN 64
#define TESTER_SECTION(_name) \
	extern char __start_ ## _name[1]; \
	extern char __stop_ ## _name[1];
#define TESTER_SECTION_ATTR(_name) \
	__attribute__(( \
		used, \
		section(TESTER_SECTION_STRINGIFY2(_name)), \
		aligned(TESTER_SECTION_ALIGN) \
	))

TESTER_SECTION(tester_section)

typedef const struct {
	const char name[48];
	int (*run)(void*, int, const char**);
} tester_section_entry_t;

#define TESTER_SECTION_ENTRY(_name, _run) \
	static tester_section_entry_t TESTER_SECTION_ATTR(tester_section) \
	_name ## test_section_entry = { \
		.name = TESTER_SECTION_STRINGIFY2(_name), \
		.run = _run, \
	}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TESTER_MAIN_H_ */
