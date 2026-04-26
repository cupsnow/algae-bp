#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>

#include <gbm.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "priv.h"

#define DRIDEV_DEF "/dev/dri/card0"

static const char opt_short[] = "h";
enum {
	opt_key_null = 0x200,
	opt_key_defer_close,
	opt_key_bmp_preload,
	opt_key_bmp_height,
};
static struct option opt_long[] = {
	{"help", no_argument, NULL, 'h'},
	{NULL}
};

static void show_help(const char *prog) {

	fprintf(stdout,
"COMMAND\n"
"    %s [OPTIONS] [DRI_DEV]\n"
"\n"
"OPTIONS\n"
"    -h, --help           Show help\n"
"    DIR_DEV              default: %s\n"
"\n"
"DESCRIPTION\n"
"  DIR_DEV default: %s\n"
"\n", (prog ? prog : "APPLICATION"), 
DRIDEV_DEF, DRIDEV_DEF);
}

int main(int argc, const char **argv)
{
	int opt_op, opt_idx, r;
	const char *opt_dri_dev = DRIDEV_DEF;

	optind = 0; 
	while ((opt_op = getopt_long(argc, (char**)argv, opt_short, opt_long,
			&opt_idx)) != -1) {
		if (opt_op == 'h') {
			show_help(argc > 0 ? argv[0] : NULL);
			return 1;
		}
	}

	for (r = optind; r < argc; r++) {
		log_d("non-option[%d/%d]: %s\n", r + 1, argc, argv[r]);
	}
	if (optind < argc) {
		opt_dri_dev = argv[optind];
	}
	log_d("opt_dri_dev: %s\n", opt_dri_dev);

	int drm_fd = open(opt_dri_dev, O_RDWR);
	if (drm_fd < 0) {
		perror("open drm");
		return -1;
	}

	struct gbm_device *gbm = gbm_create_device(drm_fd);
	if (!gbm) {
		printf("gbm_create_device failed\n");
		return -1;
	}

	EGLDisplay display = eglGetDisplay((EGLNativeDisplayType)gbm);
	if (display == EGL_NO_DISPLAY) {
		printf("eglGetDisplay failed\n");
		return -1;
	}

	if (!eglInitialize(display, NULL, NULL)) {
		printf("eglInitialize failed\n");
		return -1;
	}

	printf("EGL Vendor: %s\n", eglQueryString(display, EGL_VENDOR));
	printf("EGL Version: %s\n", eglQueryString(display, EGL_VERSION));
	printf("EGL client APIs: %s\n", eglQueryString(display, EGL_CLIENT_APIS));
	printf("EGL Extensions: %s\n", eglQueryString(display, EGL_EXTENSIONS));

	EGLConfig config;
	EGLint num_config;

	EGLint attr[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_NONE
	};

	eglChooseConfig(display, attr, &config, 1, &num_config);

	EGLint ctxattr[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};

	EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctxattr);

	if (context == EGL_NO_CONTEXT) {
		printf("eglCreateContext failed\n");
		return -1;
	}

	printf("EGL context created successfully\n");

#if 1
	const char* (*get_driver_name)(void*) =
	    (void *)eglGetProcAddress("eglGetDisplayDriverName");
	const char* (*get_driver_config)(void*) =
	    (void *)eglGetProcAddress("eglGetDisplayDriverConfig");

	if (!get_driver_name) {
	    printf("EGL_MESA_query_driver not available\n");
	} else {
	    const char *name = get_driver_name(display);
	    printf("EGL driver: %s\n", name ? name : "NULL");
	}

	if (get_driver_config) {
	    char *cfg = get_driver_config(display);
	    if (cfg) {
	        printf("Driver config:\n%s\n", cfg);
	        free(cfg);
	    }
	}
#endif

	eglDestroyContext(display, context);
	eglTerminate(display);

	gbm_device_destroy(gbm);
	close(drm_fd);

	return 0;
}
