#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- macros ---------- */
#define DIE(fmt, ...) do { \
    fprintf(stderr, "FATAL: " fmt "\n", ##__VA_ARGS__); \
    exit(EXIT_FAILURE); \
} while (0)

#define EGL_CHECK(msg) \
    do { \
        EGLint err = eglGetError(); \
        if (err != EGL_SUCCESS) \
            DIE("%s: EGL error 0x%x", msg, err); \
    } while (0)

#define GL_CHECK(msg) \
    do { \
        GLenum err = glGetError(); \
        if (err != GL_NO_ERROR) \
            DIE("%s: GL error 0x%x", msg, err); \
    } while (0)

/* ---------- shader ---------- */
static const char *vs_src =
"attribute vec2 pos;\n"
"void main() {\n"
"    gl_Position = vec4(pos, 0.0, 1.0);\n"
"}\n";

static const char *fs_src =
"precision mediump float;\n"
"void main() {\n"
"    gl_FragColor = vec4(1.0, 0.3, 0.2, 1.0);\n"
"}\n";

/* ---------- shader helpers ---------- */
static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    if (!s) DIE("glCreateShader failed");

    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        DIE("shader compile failed: %s", log);
    }
    return s;
}

static GLuint make_program(void)
{
    GLuint vs = compile(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fs_src);

    GLuint p = glCreateProgram();
    if (!p) DIE("glCreateProgram failed");

    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "pos");
    glLinkProgram(p);

    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        DIE("program link failed: %s", log);
    }
    return p;
}

/* ---------- main ---------- */
int main(void)
{
    /* --- get surfaceless display --- */
    EGLDisplay dpy =
        eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL);
    if (dpy == EGL_NO_DISPLAY)
        DIE("eglGetPlatformDisplay failed");

    if (!eglInitialize(dpy, NULL, NULL))
        EGL_CHECK("eglInitialize");

    printf("EGL vendor: %s\n", eglQueryString(dpy, EGL_VENDOR));
    printf("EGL version: %s\n", eglQueryString(dpy, EGL_VERSION));
    printf("EGL client APIs: %s\n",
           eglQueryString(dpy, EGL_CLIENT_APIS));

    /* --- query Mesa driver --- */
    const char *(*get_driver_name)(EGLDisplay);
    get_driver_name =
        (void*)eglGetProcAddress("eglGetDisplayDriverName");

    if (get_driver_name) {
        const char *drv = get_driver_name(dpy);
        printf("EGL driver: %s\n", drv ? drv : "NULL");
    }

    /* --- choose config (pbuffer) --- */
    EGLint cfg_attr[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_NONE
    };

    EGLConfig cfg;
    EGLint n;
    if (!eglChooseConfig(dpy, cfg_attr, &cfg, 1, &n) || n < 1)
        EGL_CHECK("eglChooseConfig");

    if (!eglBindAPI(EGL_OPENGL_ES_API))
        EGL_CHECK("eglBindAPI");

    EGLint ctx_attr[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    EGLContext ctx =
        eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (ctx == EGL_NO_CONTEXT)
        EGL_CHECK("eglCreateContext");

    /* --- pbuffer surface --- */
    EGLint pbuf_attr[] = {
        EGL_WIDTH, 256,
        EGL_HEIGHT, 256,
        EGL_NONE
    };

    EGLSurface surf =
        eglCreatePbufferSurface(dpy, cfg, pbuf_attr);
    if (surf == EGL_NO_SURFACE)
        EGL_CHECK("eglCreatePbufferSurface");

    if (!eglMakeCurrent(dpy, surf, surf, ctx))
        EGL_CHECK("eglMakeCurrent");

    /* --- GL render --- */
    GLuint prog = make_program();
    glUseProgram(prog);

    GLfloat verts[] = {
         0.0f,  0.7f,
        -0.7f, -0.7f,
         0.7f, -0.7f
    };

    glViewport(0, 0, 256, 256);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);
    glEnableVertexAttribArray(0);

    glDrawArrays(GL_TRIANGLES, 0, 3);
    GL_CHECK("draw");

    glFinish();

    /* --- readback --- */
    unsigned char pixels[256 * 256 * 4];
    glReadPixels(0, 0, 256, 256,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    GL_CHECK("glReadPixels");

    int nonzero = 0;
    for (size_t i = 0; i < sizeof(pixels); i++) {
        if (pixels[i]) { nonzero = 1; break; }
    }

    printf("Render result: %s\n",
           nonzero ? "non-empty (OK)" : "empty (FAIL)");

    return 0;
}
