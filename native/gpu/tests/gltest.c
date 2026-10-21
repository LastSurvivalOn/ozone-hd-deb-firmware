/* GLES2 smoke test and fill-rate benchmark into a pbuffer (nothing on screen).
 * gltest [frames]  */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static GLuint shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        printf("shader: %s\n", log);
        exit(1);
    }
    return s;
}

int main(int argc, char **argv)
{
    int frames = argc > 1 ? atoi(argv[1]) : 200, W = 1280, H = 720;
    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint maj, min;
    if (!eglInitialize(d, &maj, &min)) { printf("eglInitialize failed 0x%x\n", eglGetError()); return 1; }
    printf("EGL %d.%d vendor=%s version=%s\nEGL ext: %s\n", maj, min, eglQueryString(d, EGL_VENDOR),
           eglQueryString(d, EGL_VERSION), eglQueryString(d, EGL_EXTENSIONS));
    EGLint attr[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                      EGL_RED_SIZE, 5, EGL_GREEN_SIZE, 6, EGL_BLUE_SIZE, 5, EGL_NONE };
    EGLConfig cfg;
    EGLint n;
    if (!eglChooseConfig(d, attr, &cfg, 1, &n) || n < 1) { printf("no config 0x%x\n", eglGetError()); return 1; }
    EGLint pattr[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE };
    EGLSurface s = eglCreatePbufferSurface(d, cfg, pattr);
    EGLint cattr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, cattr);
    if (s == EGL_NO_SURFACE || c == EGL_NO_CONTEXT || !eglMakeCurrent(d, s, s, c)) {
        printf("surface/context failed 0x%x\n", eglGetError());
        return 1;
    }
    printf("GL vendor=%s renderer=%s version=%s\nGL ext: %s\n", glGetString(GL_VENDOR), glGetString(GL_RENDERER),
           glGetString(GL_VERSION), glGetString(GL_EXTENSIONS));

    GLuint p = glCreateProgram();
    glAttachShader(p, shader(GL_VERTEX_SHADER, "attribute vec2 a; varying vec2 v; void main() { v = a; gl_Position = vec4(a, 0.0, 1.0); }"));
    glAttachShader(p, shader(GL_FRAGMENT_SHADER, "precision mediump float; varying vec2 v; uniform float t;"
                             "void main() { gl_FragColor = vec4(0.5 + 0.5 * sin(v.x * 10.0 + t), 0.5 + 0.5 * v.y, 0.3, 1.0); }"));
    glBindAttribLocation(p, 0, "a");
    glLinkProgram(p);
    glUseProgram(p);
    GLint tl = glGetUniformLocation(p, "t");
    static const GLfloat quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    glEnableVertexAttribArray(0);
    glViewport(0, 0, W, H);

    double t0 = now();
    for (int i = 0; i < frames; i++) {
        glUniform1f(tl, i * 0.1f);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        eglSwapBuffers(d, s);
    }
    glFinish();
    double dt = now() - t0;
    unsigned char px[4] = { 0 };
    glReadPixels(W / 2, H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    printf("%d full-screen %dx%d frames in %.2f s: %.1f fps, %.0f Mpixel/s; centre pixel %d,%d,%d err=0x%x\n",
           frames, W, H, dt, frames / dt, frames * (double)W * H / dt / 1e6, px[0], px[1], px[2], glGetError());
    /* readback: what an EGL-on-X shim would pay per frame */
    static unsigned char buf[1280 * 720 * 4];
    static unsigned short buf16[1280 * 720];
    t0 = now();
    for (int i = 0; i < 20; i++) {
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    }
    dt = now() - t0;
    printf("draw + readback RGBA: %.1f ms per frame\n", dt / 20 * 1000);
    GLint fmt, type;
    glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT, &fmt);
    glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE, &type);
    t0 = now();
    for (int i = 0; i < 20; i++) {
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glReadPixels(0, 0, W, H, fmt, type, buf16);
    }
    dt = now() - t0;
    printf("draw + readback native (fmt 0x%x type 0x%x): %.1f ms per frame, err 0x%x\n", fmt, type, dt / 20 * 1000, glGetError());
    return 0;
}
