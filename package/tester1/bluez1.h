#ifndef BLUEZ1_H_
#define BLUEZ1_H_
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
extern void *bluez1_global;
void *bluez1_init(void *evctx);
void bluez1_destroy(void *ctx);
/* Calls are serialized by the caller; receive callback runs on the D-Bus thread.
 * Do not call lifecycle/send functions from the receive callback. */
typedef void (*bluez1_receive_fn)(const unsigned char *, size_t, void *);
int bluez1_start(void *ctx, const char *adapter, bluez1_receive_fn receive, void *arg);
int bluez1_stop(void *ctx);
/* Store a readable TX message; notify also requires an active subscription.
 * Messages are limited to 20 bytes so they fit the default ATT MTU. */
int bluez1_send(void *ctx, const void *data, size_t size, int notify);
int bluez1_pair(void *ctx, const char *device_path);
int cli_cmd_bluez1(void *, int argc, const char **argv);
#ifdef __cplusplus
}
#endif
#endif
