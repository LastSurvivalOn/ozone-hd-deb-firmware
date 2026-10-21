/* GLES2 animation in an fbdev window (the framebuffer the FRAMEBUFFER variable names), N seconds */
#include <EGL/egl.h>
#include <EGL/fbdev_window.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
int main(int argc, char **argv)
{
    int W = atoi(argv[1]), H = atoi(argv[2]); double secs = atof(argv[3]);
    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint ma, mi;
    if (!eglInitialize(d, &ma, &mi)) { printf("init failed 0x%x\n", eglGetError()); return 1; }
    EGLint attr[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                      EGL_RED_SIZE, 5, EGL_GREEN_SIZE, 6, EGL_BLUE_SIZE, 5, EGL_NONE };
    EGLConfig cfg; EGLint n;
    eglChooseConfig(d, attr, &cfg, 1, &n);
    static fbdev_window win; win.width = W; win.height = H;
    EGLSurface s = eglCreateWindowSurface(d, cfg, (EGLNativeWindowType)&win, NULL);
    EGLint cattr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, cattr);
    if (s == EGL_NO_SURFACE || !eglMakeCurrent(d, s, s, c)) { printf("window surface failed 0x%x\n", eglGetError()); return 1; }
    const char *vs = "attribute vec2 a; varying vec2 v; void main() { v = a; gl_Position = vec4(a, 0.0, 1.0); }";
    const char *fs = "precision mediump float; varying vec2 v; uniform float t;"
                     "void main() { gl_FragColor = vec4(0.5 + 0.5 * sin(v.x * 6.0 + t), 0.5 + 0.5 * sin(v.y * 6.0 - t), 0.5 + 0.5 * sin(t), 1.0); }";
    GLuint p = glCreateProgram(), a = glCreateShader(GL_VERTEX_SHADER), b = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(a, 1, &vs, 0); glCompileShader(a); glShaderSource(b, 1, &fs, 0); glCompileShader(b);
    glAttachShader(p, a); glAttachShader(p, b); glBindAttribLocation(p, 0, "a"); glLinkProgram(p); glUseProgram(p);
    GLint tl = glGetUniformLocation(p, "t");
    static const GLfloat q[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, q); glEnableVertexAttribArray(0);
    glViewport(0, 0, W, H);
    double t0 = now(); int f = 0;
    while (now() - t0 < secs) {
        glUniform1f(tl, (float)(now() - t0) * 3.0f);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        eglSwapBuffers(d, s);
        f++;
    }
    printf("%d frames in %.1f s: %.1f fps at %dx%d\n", f, now() - t0, f / (now() - t0), W, H);
    return 0;
}
