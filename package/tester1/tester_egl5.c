#include <stdio.h>
#include <fcntl.h>
#include <stdarg.h>
#include <unistd.h>
#include <getopt.h>

#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
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
		log_d("argv[%d/%d]: %s\n", r + 1, argc, argv[r]);
	}
	if (optind < argc) {
		opt_dri_dev = argv[optind];
	}
	log_d("opt_dri_dev: %s\n", opt_dri_dev);

    int fd = open(opt_dri_dev, O_RDWR);
    if (fd < 0) {
        perror("open drm");
        return -1;
    }

    struct gbm_device *gbm = gbm_create_device(fd);
    if (!gbm) {
        printf("gbm_create_device failed\n");
        return -1;
    }

    EGLDisplay dpy = eglGetDisplay((EGLNativeDisplayType)gbm);
    if (dpy == EGL_NO_DISPLAY) {
        printf("eglGetDisplay failed\n");
        return -1;
    }

    if (!eglInitialize(dpy, NULL, NULL)) {
        printf("eglInitialize failed\n");
        return -1;
    }

    printf("EGL vendor: %s\n", eglQueryString(dpy, EGL_VENDOR));
    printf("EGL version: %s\n", eglQueryString(dpy, EGL_VERSION));

    EGLConfig config;
    EGLint num;

    EGLint config_attr[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_NONE
    };

    eglChooseConfig(dpy, config_attr, &config, 1, &num);

    EGLint ctx_attr[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    EGLContext ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, ctx_attr);
    if (ctx == EGL_NO_CONTEXT) {
        printf("eglCreateContext failed\n");
        return -1;
    }

    EGLint surf_attr[] = {
        EGL_WIDTH, 256,
        EGL_HEIGHT, 256,
        EGL_NONE
    };

    EGLSurface surf = eglCreatePbufferSurface(dpy, config, surf_attr);

    eglMakeCurrent(dpy, surf, surf, ctx);

    glClearColor(1.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);

    printf("EGL rendering OK\n");

    eglDestroySurface(dpy, surf);
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);

    gbm_device_destroy(gbm);
    close(fd);

    return 0;
}