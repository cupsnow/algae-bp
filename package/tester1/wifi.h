/* $Id$
 *
 * @author joelai
 *
 * @file /algae-bp/package/tester1/wifi.h
 * @brief wifi
 */

#ifndef _H_ALGAE_TESTER1_WIFI
#define _H_ALGAE_TESTER1_WIFI


#ifdef __cplusplus
extern "C" {
#endif

extern void *wifi2_global;

void* wifi2_init(void *evctx, const char *iface);
int wifi2_cli(void*, int argc, const char **argv);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _H_ALGAE_TESTER1_WIFI */
