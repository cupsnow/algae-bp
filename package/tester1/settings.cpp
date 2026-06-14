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

#include "priv.h"

typedef struct keyval_rec {
	const char *key;
	RB_ENTRY(keyval_rec) rb_ent;
	aloe_buf_t fb;
} keyval_t;

typedef RB_HEAD(keyval_head_rec, keyval_rec) keyval_head_t;

static int keyval_cmp(keyval_t *a, keyval_t *b) {
	return strcmp(a->key, b->key);
}

RB_GENERATE_STATIC(keyval_head_rec, keyval_rec, rb_ent, keyval_cmp);

typedef struct {
	keyval_head_t keyval_root;
	pthread_mutex_t lock;
	evconn_t evconn;
} settings_t;

extern "C" {
settings_t *settings_global;
}

#define STRUCT_PACKED() __attribute__((packed))

typedef struct __attribute__((packed)) {
	uint8_t tag[6];
	uint16_t ver;
	uint8_t cksum[8];
	uint32_t payload_len;
} settings_hdr_t;

typedef struct __attribute__((packed)) {
	uint8_t key_len;
	uint16_t val_len;
	// payload layout: [key_len bytes of key][val_len bytes of value]
} keyval_hdr_t;

static void keyval_cksum_update(uint8_t cksum[8], const void *data,
		size_t len) {
	uint32_t s = *(uint32_t*)cksum;

	s = aloe_crc32(data, len, s);
	*(uint32_t*)cksum = s;
	memset(cksum + sizeof(s), 0, 8 - sizeof(s));
}

/** Save keyval.
 *
 * checksum order: keyval, settings_hdr
 */
static int keyval_save(keyval_head_t *root, uint8_t tag[6], uint16_t ver,
		int fd) {
	int ret = -1, r;
	settings_hdr_t settings_hdr = {};
	size_t cksum_pos;
	keyval_t *kv;
	off_t fpos0 = lseek(fd, 0, SEEK_CUR);

	if (tag[0]) memcpy(settings_hdr.tag, tag, sizeof(settings_hdr.tag));
	if (ver > 0) settings_hdr.ver = ver;

	if (lseek(fd, fpos0 + sizeof(settings_hdr), SEEK_SET) == (off_t)-1) {
		r = errno;
		log_e("Failed lseek; %s\n", strerror(r));
		goto finally;
	}

	RB_FOREACH(kv, keyval_head_rec, root) {
		keyval_hdr_t kv_hdr = {};
		size_t kv_hdr_sz = sizeof(keyval_hdr_t), kv_sz;
		char *kv_key;

		kv_hdr.key_len = strlen(kv->key);
		kv_hdr.val_len = kv->fb.data ? kv->fb.pos : 0;

		if ((aloe_bio_write(fd, &kv_hdr, kv_hdr_sz)) != kv_hdr_sz) {
			log_e("failed write keyval_hdr\n");
			goto finally;
		}
		keyval_cksum_update(settings_hdr.cksum, &kv_hdr, kv_hdr_sz);
		settings_hdr.payload_len += kv_hdr_sz;

		if ((aloe_bio_write(fd, kv->key, kv_hdr.key_len)) != kv_hdr.key_len) {
			log_e("failed write key\n");
			goto finally;
		}
		keyval_cksum_update(settings_hdr.cksum, kv->key, kv_hdr.key_len);
		settings_hdr.payload_len += kv_hdr.key_len;

		if (kv_hdr.val_len > 0) {
			if ((aloe_bio_write(fd, kv->fb.data,
					kv_hdr.val_len)) != kv_hdr.val_len) {
				log_e("failed write value\n");
				goto finally;
			}
			keyval_cksum_update(settings_hdr.cksum, kv->fb.data, kv_hdr.val_len);
			settings_hdr.payload_len += kv_hdr.val_len;
		}
	}

	keyval_cksum_update(settings_hdr.cksum, &settings_hdr,
			offsetof(settings_hdr_t, cksum));
	cksum_pos = offsetof(settings_hdr_t, payload_len);
	keyval_cksum_update(settings_hdr.cksum, (char*)&settings_hdr + cksum_pos,
			sizeof(settings_hdr_t) - cksum_pos);

	if (lseek(fd, fpos0, SEEK_SET) == (off_t)-1) {
		r = errno;
		log_e("Failed lseek; %s\n", strerror(r));
		goto finally;
	}

	if ((aloe_bio_write(fd, &settings_hdr,
			sizeof(settings_hdr))) != sizeof(settings_hdr)) {
		log_e("failed write key\n");
		goto finally;
	}
	ret = settings_hdr.payload_len + sizeof(settings_hdr);
finally:
	return ret;
}

static int keyval_load(keyval_head_t *root, uint8_t tag[6], uint16_t ver,
		int fd) {
	int ret = -1, r;
	settings_hdr_t settings_hdr;
	size_t payload_len = 0, cksum_pos;
	uint8_t cksum[8] = {};
	keyval_t *kv = NULL;
	aloe_buf_t fb = {};

	if ((aloe_bio_read(fd, &settings_hdr, sizeof(settings_hdr)))
			!= sizeof(settings_hdr)) {
		log_e("failed read settings_hdr\n");
		goto finally;
	}

	if (tag[0] && memcmp(settings_hdr.tag, tag, sizeof(settings_hdr.tag)) != 0) {
		log_e("mismatch tag\n");
		goto finally;
	}

	if (ver > 0 && ver != settings_hdr.ver) {
		log_e("mismatch ver\n");
		goto finally;
	}

	while (payload_len < settings_hdr.payload_len) {
		keyval_hdr_t kv_hdr;
		size_t kv_hdr_sz = sizeof(keyval_hdr_t), kv_sz;
		char *kv_key;

		if ((aloe_bio_read(fd, &kv_hdr, kv_hdr_sz)) != kv_hdr_sz) {
			log_e("failed read keyval_hdr\n");
			goto finally;
		}
		payload_len += kv_hdr_sz;
		keyval_cksum_update(cksum, &kv_hdr, kv_hdr_sz);

		kv_sz = (size_t)kv_hdr.key_len + kv_hdr.val_len;
		if (kv_sz > fb.cap && aloe_buf_expand(&fb, kv_sz + 1024, 0) != 0) {
			log_e("failed expand buffer\n");
			goto finally;
		}

		if (aloe_bio_read(fd, fb.data, kv_sz) != kv_sz) {
			log_e("failed read kv data\n");
			goto finally;
		}
		payload_len += kv_sz;
		keyval_cksum_update(cksum, fb.data, kv_sz);

		if ((kv = (keyval_t*)aloe_calloc(1,
				sizeof(*kv) + kv_hdr.key_len + 1)) == NULL) {
			log_e("failed alloc kv\n");
			goto finally;
		}
		kv_key = (char*)(kv + 1);
		memcpy(kv_key, fb.data, kv_hdr.key_len);
		kv_key[kv_hdr.key_len] = '\0';
		kv->key = kv_key;

		if (kv_hdr.val_len > 0) {
			if (aloe_buf_expand(&kv->fb, kv_hdr.val_len + 1024, 0) != 0) {
				log_e("failed alloc val\n");
				goto finally;
			}
			aloe_buf_clear(&kv->fb);
			memcpy(kv->fb.data, (char*)fb.data + kv_hdr.key_len,
					(kv->fb.pos = kv_hdr.val_len));
			((char*)kv->fb.data)[kv->fb.pos] = '\0';
		}
		RB_INSERT(keyval_head_rec, root, kv);
		kv = NULL;
	}

	keyval_cksum_update(cksum, &settings_hdr, offsetof(settings_hdr_t, cksum));
	cksum_pos = offsetof(settings_hdr_t, payload_len);
	keyval_cksum_update(cksum, (char*)&settings_hdr + cksum_pos,
			sizeof(settings_hdr_t) - cksum_pos);

	if (memcmp(cksum, settings_hdr.cksum, sizeof(cksum)) != 0) {
		log_e("checksum mismatch\n");
		goto finally;
	}
	ret = payload_len + sizeof(settings_hdr);
finally:
	if (fb.data) aloe_free(fb.data);
	if (kv) {
		if (kv->fb.data) aloe_free(kv->fb.data);
		aloe_free(kv);
	}
	return ret;
}

static void settings_save_cb(int fd, unsigned ev, void *cbarg) {
	settings_t *settings = (settings_t*)cbarg;

	settings->evconn.ev = NULL;

finally:

}

int settings_set(void *ctx, const char *key, const void *data, size_t len) {
	settings_t *settings = (settings_t*)ctx;

}

int settings_get(void *ctx, const char *key, void *data, size_t *len) {
	settings_t *settings = (settings_t*)ctx;

}

int settings_peek(void *ctx, const char *key, const void **data,
		size_t *len) {
	settings_t *settings = (settings_t*)ctx;

}

void settings_destroy(void *ctx) {
	settings_t *settings = (settings_t*)ctx;
}

void* settings_init(void *evctx, const char *path) {
	int ret = -1, fd = -1;
	settings_t *settings = NULL;
	uint8_t cksum[4];

	if ((settings = (settings_t*)aloe_malloc(sizeof(*settings))) == NULL) {
		log_e("malloc\n");
		goto finally;
	}
	memset(settings, 0, sizeof(*settings));
	RB_INIT(&settings->keyval_root);
	pthread_mutex_init(&settings->lock, NULL);
	settings->evconn.ev_ctx = evctx;

	if ((fd = open(path, O_RDONLY, 0666)) == -1) {
		log_e("failed open %s\n", path);
		goto finally;
	}

finally:
	return settings;
}

int settings_persist(void *ctx, unsigned long ms) {
	settings_t *settings = (settings_t*)ctx;

}
