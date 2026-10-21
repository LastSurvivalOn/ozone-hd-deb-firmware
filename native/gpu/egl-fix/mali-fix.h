/* Shared between egl-fix.c and gles-fix.c, which make one library */
#ifndef MALI_FIX_H
#define MALI_FIX_H

#include <EGL/egl.h>
#include <stdio.h>
#include <unistd.h>
#include <X11/Xlib.h>

/* a function of libMali.so itself */
__attribute__((visibility("hidden"))) void *mali_symbol(const char *name);
/* A libMali.so function to call from here: the bare name would bind to the first definition in
 * the process, which is glvnd's libGL.so.1 stubs when the program loaded that too (WebKit) */
#define MALI(fn) ({ static __typeof__(&fn) p_; if (!p_) p_ = (__typeof__(&fn))mali_symbol(#fn); p_; })

/* the X display behind an EGL display, or NULL */
__attribute__((visibility("hidden"))) Display *mali_x_display(EGLDisplay egl);

/* OZONE_GL_DEBUG=1: what the fixes do, on stderr */
__attribute__((visibility("hidden"))) int mali_debug(void);
#define DEBUG(...) do { if (mali_debug()) { fprintf(stderr, "[mali-fix %d] ", (int)getpid()); fprintf(stderr, __VA_ARGS__); } } while (0)

#endif
