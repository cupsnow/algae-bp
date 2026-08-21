/* $Id$
 *
 * @author joelai
 *
 * @file noname
 * @brief noname
 */

#ifndef _H_ALGAE_MGMT
#define _H_ALGAE_MGMT

#include "priv.h"

#ifdef __cplusplus
extern "C" {
#endif

void* mgmt1_init(void *evctx, const char *path);
void mgmt1_destroy(void *_mgmtctx);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // _H_ALGAE_MGMT
