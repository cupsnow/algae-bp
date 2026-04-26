#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <gbm.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "priv.h"

static const char *vs_src =
"attribute vec2 pos;"
"void main() {"
"   gl_Position = vec4(pos,0,1);"
"}";

static const char *fs_src =
"precision mediump float;"
"void main() {"
"   gl_FragColor = vec4(1,0,0,1);"
"}";

GLuint compile_shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s,1,&src,NULL);
    glCompileShader(s);
    return s;
}

int main()
{
    int drm_fd = open("/dev/dri/card0", O_RDWR);
	if (drm_fd < 0) {
		perror("open drm");
		return -1;
	}

    drmModeRes *res = drmModeGetResources(drm_fd);
    if (!res) {
    	log_e("drmModeGetResources\n");
    	return -1;
    }
    drmModeConnector *conn = drmModeGetConnector(drm_fd, res->connectors[0]);
    if (!conn) {
    	log_e("drmModeGetConnector\n");
    	return -1;
    }

    drmModeModeInfo mode = conn->modes[0];
    uint32_t conn_id = conn->connector_id;

    drmModeEncoder *enc = drmModeGetEncoder(drm_fd, conn->encoder_id);
    if (!enc) {
    	log_e("drmModeGetEncoder\n");
    	return -1;
    }

    uint32_t crtc_id = enc->crtc_id;

    struct gbm_device *gbm = gbm_create_device(drm_fd);
    if (!gbm) {
    	log_e("gbm_create_device\n");
    	return -1;
    }

    struct gbm_surface *surface = gbm_surface_create(gbm,
            mode.hdisplay,
            mode.vdisplay,
            GBM_FORMAT_XRGB8888,
            GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!surface) {
    	log_e("gbm_surface_create\n");
    	return -1;
    }

    EGLDisplay egl_dpy = eglGetDisplay((EGLNativeDisplayType)gbm);
    if (egl_dpy == EGL_NO_DISPLAY) {
    	log_e("eglGetDisplay\n");
    	return -1;
    }
    eglInitialize(egl_dpy, NULL, NULL);

    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    EGLConfig config;
    EGLint num_config;

    eglChooseConfig(egl_dpy, config_attribs, &config, 1, &num_config);

    EGLContext ctx;
    EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };

    ctx = eglCreateContext(egl_dpy, config, EGL_NO_CONTEXT, ctx_attr);
    if (!ctx) {
    	log_e("eglCreateContext\n");
    	return -1;
    }

    EGLSurface egl_surf = eglCreateWindowSurface(
            egl_dpy,
            config,
            (EGLNativeWindowType)surface,
            NULL);
    if (!egl_surf) {
    	log_e("eglCreateWindowSurface\n");
    	return -1;
    }

    eglMakeCurrent(egl_dpy, egl_surf, egl_surf, ctx);

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);

    GLuint prog = glCreateProgram();
    glAttachShader(prog,vs);
    glAttachShader(prog,fs);
    glLinkProgram(prog);

    glUseProgram(prog);

    GLfloat verts[] = {
         0.0f,  0.5f,
        -0.5f, -0.5f,
         0.5f, -0.5f
    };

    GLint pos = glGetAttribLocation(prog,"pos");

    glViewport(0,0,mode.hdisplay,mode.vdisplay);
    glClearColor(0,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT);

    glVertexAttribPointer(pos,2,GL_FLOAT,GL_FALSE,0,verts);
    glEnableVertexAttribArray(pos);

    glDrawArrays(GL_TRIANGLES,0,3);

    eglSwapBuffers(egl_dpy, egl_surf);

    struct gbm_bo *bo = gbm_surface_lock_front_buffer(surface);

    uint32_t handle = gbm_bo_get_handle(bo).u32;
    uint32_t pitch  = gbm_bo_get_stride(bo);

    uint32_t fb;
    drmModeAddFB(drm_fd,
        mode.hdisplay,
        mode.vdisplay,
        24,32,
        pitch,
        handle,
        &fb);

    drmModeSetCrtc(
        drm_fd,
        crtc_id,
        fb,
        0,0,
        &conn_id,
        1,
        &mode);

    sleep(5);

    return 0;
}
