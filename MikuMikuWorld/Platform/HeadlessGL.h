#pragma once

namespace MikuMikuWorld::Platform
{
	// Brings up an offscreen OpenGL 3.3 Core context on the current thread
	// and loads the GL function pointers via glad. Call once before any GL
	// calls; call shutdownHeadlessGL() before exit.
	//
	// macOS: backed by a hidden GLFW window.
	// Linux: backed by EGL (surfaceless / pbuffer); no X server required.
	bool initHeadlessGL(int width, int height);
	void shutdownHeadlessGL();
}
