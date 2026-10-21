/* GLES2 animation in an ordinary X11 window through the Mali X11 EGL (DRI2 + UMP).
 * x11win [W H seconds x y] */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
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
    return s;
}

int main(int argc, char **argv)
{
    int W = argc > 1 ? atoi(argv[1]) : 640, H = argc > 2 ? atoi(argv[2]) : 360;
    double secs = argc > 3 ? atof(argv[3]) : 5;
    int x = argc > 4 ? atoi(argv[4]) : 100, y = argc > 5 ? atoi(argv[5]) : 100;
    Display *xd = XOpenDisplay(NULL);
    if (!xd) { printf("no X display\n"); return 1; }
    Window win = XCreateSimpleWindow(xd, DefaultRootWindow(xd), x, y, W, H, 0, 0, 0);
    XStoreName(xd, win, "x11win");
    XMapWindow(xd, win);
    XSync(xd, False);

    EGLDisplay d = eglGetDisplay((EGLNativeDisplayType)xd);
    EGLint maj, min;
    if (!eglInitialize(d, &maj, &min)) { printf("eglInitialize failed 0x%x\n", eglGetError()); return 1; }
    printf("EGL %s %s\n", eglQueryString(d, EGL_VENDOR), eglQueryString(d, EGL_VERSION));
    EGLint attr[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                      EGL_RED_SIZE, 5, EGL_GREEN_SIZE, 6, EGL_BLUE_SIZE, 5, EGL_NONE };
    EGLConfig cfg;
    EGLint n;
    if (!eglChooseConfig(d, attr, &cfg, 1, &n) || !n) { printf("no config 0x%x\n", eglGetError()); return 1; }
    EGLSurface s = eglCreateWindowSurface(d, cfg, (EGLNativeWindowType)win, NULL);
    if (s == EGL_NO_SURFACE) { printf("window surface failed 0x%x\n", eglGetError()); return 1; }
    EGLint cattr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, cattr);
    if (c == EGL_NO_CONTEXT || !eglMakeCurrent(d, s, s, c)) { printf("context failed 0x%x\n", eglGetError()); return 1; }
    printf("GL %s / %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    eglSwapInterval(d, 0);

    GLuint p = glCreateProgram();
    glAttachShader(p, shader(GL_VERTEX_SHADER, "attribute vec2 a; varying vec2 v; void main() { v = a; gl_Position = vec4(a, 0.0, 1.0); }"));
    glAttachShader(p, shader(GL_FRAGMENT_SHADER, "precision mediump float; varying vec2 v; uniform float t;"
                             "void main() { gl_FragColor = vec4(0.5 + 0.5 * sin(v.x * 6.0 + t), 0.5 + 0.5 * sin(v.y * 6.0 - t), 0.5 + 0.5 * sin(t * 0.7), 1.0); }"));
    glBindAttribLocation(p, 0, "a");
    glLinkProgram(p);
    glUseProgram(p);
    GLint tl = glGetUniformLocation(p, "t");
    static const GLfloat quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    glEnableVertexAttribArray(0);
    glViewport(0, 0, W, H);

    int frames = 0;
    double t0 = now();
    while (now() - t0 < secs) {
        glUniform1f(tl, frames * 0.05f);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        eglSwapBuffers(d, s);
        frames++;
    }
    double dt = now() - t0;
    printf("%d frames of %dx%d in %.2f s: %.1f fps, err 0x%x\n", frames, W, H, dt, frames / dt, glGetError());
    eglTerminate(d);
    XCloseDisplay(xd);
    return 0;
}
