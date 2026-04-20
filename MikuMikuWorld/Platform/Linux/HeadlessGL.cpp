#include "../HeadlessGL.h"

#include <glad/glad.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <cstdio>

namespace MikuMikuWorld::Platform
{
	namespace
	{
		EGLDisplay g_display = EGL_NO_DISPLAY;
		EGLContext g_context = EGL_NO_CONTEXT;
		EGLSurface g_surface = EGL_NO_SURFACE;

		EGLDisplay openDisplay()
		{
			// Prefer the surfaceless Mesa platform so Xorg/Wayland are not
			// required at runtime. Fall back to the legacy default display
			// for drivers that don't advertise the extension.
			auto eglGetPlatformDisplayEXT = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
				eglGetProcAddress("eglGetPlatformDisplayEXT"));
			if (eglGetPlatformDisplayEXT)
			{
				EGLDisplay d = eglGetPlatformDisplayEXT(
					EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
				if (d != EGL_NO_DISPLAY)
					return d;
			}
			return eglGetDisplay(EGL_DEFAULT_DISPLAY);
		}
	}

	bool initHeadlessGL(int width, int height)
	{
		g_display = openDisplay();
		if (g_display == EGL_NO_DISPLAY)
		{
			std::fprintf(stderr, "eglGetDisplay failed\n");
			return false;
		}

		EGLint eglMajor = 0;
		EGLint eglMinor = 0;
		if (!eglInitialize(g_display, &eglMajor, &eglMinor))
		{
			std::fprintf(stderr, "eglInitialize failed\n");
			return false;
		}

		if (!eglBindAPI(EGL_OPENGL_API))
		{
			std::fprintf(stderr, "eglBindAPI(EGL_OPENGL_API) failed\n");
			return false;
		}

		const EGLint configAttribs[] = {
			EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
			EGL_BLUE_SIZE, 8,
			EGL_GREEN_SIZE, 8,
			EGL_RED_SIZE, 8,
			EGL_ALPHA_SIZE, 8,
			EGL_DEPTH_SIZE, 24,
			EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
			EGL_NONE,
		};
		EGLConfig config = nullptr;
		EGLint numConfigs = 0;
		if (!eglChooseConfig(g_display, configAttribs, &config, 1, &numConfigs) || numConfigs == 0)
		{
			std::fprintf(stderr, "eglChooseConfig failed\n");
			return false;
		}

		// ScorePreview renders into its own FBO, so the pbuffer only exists
		// to satisfy eglMakeCurrent; keep it 1x1.
		const EGLint pbufferAttribs[] = {
			EGL_WIDTH, 1,
			EGL_HEIGHT, 1,
			EGL_NONE,
		};
		g_surface = eglCreatePbufferSurface(g_display, config, pbufferAttribs);
		if (g_surface == EGL_NO_SURFACE)
		{
			std::fprintf(stderr, "eglCreatePbufferSurface failed (0x%x)\n", eglGetError());
			return false;
		}

		const EGLint contextAttribs[] = {
			EGL_CONTEXT_MAJOR_VERSION, 3,
			EGL_CONTEXT_MINOR_VERSION, 3,
			EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
			EGL_NONE,
		};
		g_context = eglCreateContext(g_display, config, EGL_NO_CONTEXT, contextAttribs);
		if (g_context == EGL_NO_CONTEXT)
		{
			std::fprintf(stderr, "eglCreateContext failed (0x%x)\n", eglGetError());
			return false;
		}

		if (!eglMakeCurrent(g_display, g_surface, g_surface, g_context))
		{
			std::fprintf(stderr, "eglMakeCurrent failed (0x%x)\n", eglGetError());
			return false;
		}

		if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(eglGetProcAddress)))
		{
			std::fprintf(stderr, "gladLoadGLLoader failed\n");
			return false;
		}

		(void)width;
		(void)height;
		return true;
	}

	void shutdownHeadlessGL()
	{
		if (g_display != EGL_NO_DISPLAY)
		{
			eglMakeCurrent(g_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
			if (g_context != EGL_NO_CONTEXT)
				eglDestroyContext(g_display, g_context);
			if (g_surface != EGL_NO_SURFACE)
				eglDestroySurface(g_display, g_surface);
			eglTerminate(g_display);
		}
		g_display = EGL_NO_DISPLAY;
		g_context = EGL_NO_CONTEXT;
		g_surface = EGL_NO_SURFACE;
	}
}
