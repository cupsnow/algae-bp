/* $Id$
 *
 * @author joelai
 *
 * @file noname
 * @brief noname
 */

#ifndef _H_ALGAE_V4L2
#define _H_ALGAE_V4L2

#include "priv.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void *v4l2_global;

void* v4l2_init(void *evctx, const char *path);
void v4l2_destroy(void *_v4l2ctx);
int v4l2_cli(void *_v4l2, int argc, const char **argv);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // _H_ALGAE_V4L2
