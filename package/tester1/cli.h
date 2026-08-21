/* $Id$
 *
 * @author joelai
 *
 * @file noname
 * @brief noname
 */

#ifndef _H_ALGAE_CLI
#define _H_ALGAE_CLI

#include "priv.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void *cli_global;

void* cli1_init(void *evctx);
void cli1_destroy(void *_clictx);
int cli1_cmd_add(void *_clictx, const char *str,
		int (*run)(void*, int, const char**), void *cbarg, const char *detail);

void cli1_test1(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _H_ALGAE_CLI */
