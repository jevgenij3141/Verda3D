#pragma once

#include <windows.h>
#include <GL/gl.h>

#include <cstring>
#include <filesystem>

enum class GraphicsBackend { OpenGL, Vulkan };
enum class GraphicsTarget { Window, Bitmap };

#define EDITOR_GL_FUNCTIONS(X) \
    X(glBegin) \
    X(glBindTexture) \
    X(glBlendFunc) \
    X(glCallList) \
    X(glCallLists) \
    X(glClear) \
    X(glClearColor) \
    X(glColor3f) \
    X(glColor4f) \
    X(glDeleteLists) \
    X(glDeleteTextures) \
    X(glDepthFunc) \
    X(glDepthMask) \
    X(glDisable) \
    X(glEnable) \
    X(glEnd) \
    X(glFinish) \
    X(glFrustum) \
    X(glGenLists) \
    X(glGenTextures) \
    X(glGetError) \
    X(glGetIntegerv) \
    X(glGetString) \
    X(glLineWidth) \
    X(glListBase) \
    X(glLoadIdentity) \
    X(glLoadMatrixd) \
    X(glMatrixMode) \
    X(glOrtho) \
    X(glPointSize) \
    X(glPolygonOffset) \
    X(glRasterPos2i) \
    X(glReadPixels) \
    X(glRectd) \
    X(glRecti) \
    X(glScissor) \
    X(glTexCoord2d) \
    X(glTexImage2D) \
    X(glTexParameteri) \
    X(glVertex2d) \
    X(glVertex3d) \
    X(glViewport) \
    X(wglCreateContext) \
    X(wglDeleteContext) \
    X(wglGetCurrentContext) \
    X(wglGetCurrentDC) \
    X(wglGetProcAddress) \
    X(wglMakeCurrent) \
    X(wglUseFontBitmapsW)

struct GraphicsApi {
#define GRAPHICS_DECLARE(name) decltype(&::name) name = nullptr;
    EDITOR_GL_FUNCTIONS(GRAPHICS_DECLARE)
#undef GRAPHICS_DECLARE
    decltype(&::ChoosePixelFormat) choosePixelFormat = nullptr;
    decltype(&::SetPixelFormat) setPixelFormat = nullptr;
    decltype(&::SwapBuffers) swapBuffers = nullptr;
};

template<class Function>
Function graphicsFunction(FARPROC address) {
    static_assert(sizeof(Function) == sizeof(address));
    Function function = nullptr;
    if (address) std::memcpy(&function, &address, sizeof(function));
    return function;
}

const GraphicsApi& graphics();
GraphicsBackend activeGraphicsBackend();
GraphicsBackend preferredGraphicsBackend();
bool saveGraphicsBackend(GraphicsBackend backend);
bool initializeGraphics(GraphicsBackend backend);
bool findWorkingVulkanDriver();
bool setGraphicsPixelFormat(HDC dc, GraphicsTarget target);
HGLRC createGraphicsContext(HDC dc, GraphicsTarget target);
std::filesystem::path executableDirectory();

#ifndef GRAPHICS_IMPLEMENTATION
#define glBegin graphics().glBegin
#define glBindTexture graphics().glBindTexture
#define glBlendFunc graphics().glBlendFunc
#define glCallList graphics().glCallList
#define glCallLists graphics().glCallLists
#define glClear graphics().glClear
#define glClearColor graphics().glClearColor
#define glColor3f graphics().glColor3f
#define glColor4f graphics().glColor4f
#define glDeleteLists graphics().glDeleteLists
#define glDeleteTextures graphics().glDeleteTextures
#define glDepthFunc graphics().glDepthFunc
#define glDepthMask graphics().glDepthMask
#define glDisable graphics().glDisable
#define glEnable graphics().glEnable
#define glEnd graphics().glEnd
#define glFinish graphics().glFinish
#define glFrustum graphics().glFrustum
#define glGenLists graphics().glGenLists
#define glGenTextures graphics().glGenTextures
#define glGetError graphics().glGetError
#define glGetIntegerv graphics().glGetIntegerv
#define glGetString graphics().glGetString
#define glLineWidth graphics().glLineWidth
#define glListBase graphics().glListBase
#define glLoadIdentity graphics().glLoadIdentity
#define glLoadMatrixd graphics().glLoadMatrixd
#define glMatrixMode graphics().glMatrixMode
#define glOrtho graphics().glOrtho
#define glPolygonOffset graphics().glPolygonOffset
#define glPointSize graphics().glPointSize
#define glRasterPos2i graphics().glRasterPos2i
#define glReadPixels graphics().glReadPixels
#define glRectd graphics().glRectd
#define glRecti graphics().glRecti
#define glScissor graphics().glScissor
#define glTexCoord2d graphics().glTexCoord2d
#define glTexImage2D graphics().glTexImage2D
#define glTexParameteri graphics().glTexParameteri
#define glVertex2d graphics().glVertex2d
#define glVertex3d graphics().glVertex3d
#define glViewport graphics().glViewport
#define wglCreateContext graphics().wglCreateContext
#define wglDeleteContext graphics().wglDeleteContext
#define wglGetCurrentContext graphics().wglGetCurrentContext
#define wglGetCurrentDC graphics().wglGetCurrentDC
#define wglGetProcAddress graphics().wglGetProcAddress
#define wglMakeCurrent graphics().wglMakeCurrent
#define wglUseFontBitmapsW graphics().wglUseFontBitmapsW
#define ChoosePixelFormat graphics().choosePixelFormat
#define SetPixelFormat graphics().setPixelFormat
#define SwapBuffers graphics().swapBuffers
#endif
