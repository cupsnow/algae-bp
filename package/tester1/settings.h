/* $Id$
 *
 * Copyright (c) 2026-2026, joelai
 * All Rights Reserved
 *
 * SPDX-License-Identifier: MIT
 *
 * @file settings.cpp
 * @brief noname
 *
 */

 #ifndef _H_ALGAE_TESTER1_SETTINGS
 #define _H_ALGAE_TESTER1_SETTINGS
 
int settings_set(void *ctx, const char *key, const void *data, size_t len);
int settings_get(void *ctx, const char *key, void *data, size_t *len);
int settings_peek(void *ctx, const char *key, const void **data, size_t *len);
void settings_destroy(void *ctx);
void* settings_init(void *evctx, const char *path);
int settings_persist(void *ctx, unsigned long ms);

 #endif /* _H_ALGAE_TESTER1_SETTINGS */
