#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
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

#if 1
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
#endif
	log_d("opt_dri_dev: %s\n", opt_dri_dev);

    EGLDisplay dpy = EGL_NO_DISPLAY;

#if 1
    {
        int fd = -1;

        if ((fd = open(opt_dri_dev, O_RDWR)) == -1) {
            log_e("Failed open %s\n", opt_dri_dev);
            goto finally;
        }

        struct gbm_device *gbm = NULL;

        if ((gbm = gbm_create_device(fd)) == NULL) {
            log_e("Failed create gbm device\n");
            goto finally;
        }
        EGLDisplay disp = EGL_NO_DISPLAY;

        if ((disp = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL)) == EGL_NO_DISPLAY) {
            log_e("Failed get platform display\n");
            goto finally;
        }

        log_d("got platform display\n");

        dpy = disp;
    }
#endif

    if (dpy == EGL_NO_DISPLAY) {
        dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    }
    if (dpy == EGL_NO_DISPLAY) {
        printf("EGL: no display\n");
        return -1;
    }

    if (!eglInitialize(dpy, NULL, NULL)) {
        printf("EGL: init failed\n");
        return -1;
    }

    EGLint attr[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    EGLConfig cfg;
    EGLint num;

    eglChooseConfig(dpy, attr, &cfg, 1, &num);

    eglBindAPI(EGL_OPENGL_ES_API);

    EGLint ctxattr[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxattr);

    if (ctx == EGL_NO_CONTEXT) {
        printf("EGL: context failed\n");
        return -1;
    }

    if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
        printf("EGL: make current failed\n");
        return -1;
    }

    printf("GL_VENDOR  : %s\n", glGetString(GL_VENDOR));
    printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    printf("GL_VERSION : %s\n", glGetString(GL_VERSION));

    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);
finally:
    return 0;
}
