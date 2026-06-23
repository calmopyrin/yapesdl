#include "types.h"
#include <SDL/SDL_opengl.h>
#include <cstdio>

#ifdef _MSC_VER
#pragma comment(lib, "opengl32.lib")
#endif

static SDL_GLContext glctx;
static GLuint glTex;
static unsigned int winWidth, winHeight;
static int vsync;

static void updateViewPort(unsigned int w, unsigned int h)
{
	winWidth = w;
	winHeight = h;
	glViewport(0, 0, w, h);
}

bool openGLInit(SDL_Window* wnd, unsigned int windowWidth, unsigned int windowHeight, 
	unsigned int width, unsigned int height, unsigned int sync)
{
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
	
	glctx = SDL_GL_CreateContext(wnd);
	if (!glctx)
		return false;
	// creating a context does NOT automatically make it current
	SDL_GL_MakeCurrent(wnd, glctx);
	SDL_GL_SetSwapInterval(vsync = (sync != 0));
	
	fprintf(stderr, "OpenGL version found: %s\n", glGetString(GL_VERSION));
	
	// Enable 2D texturing
	// doesn't have any effect when using a shader
	glEnable(GL_TEXTURE_2D);
	// may be unnecessary
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	// Texture setup
	glGenTextures(1, &glTex);
	glBindTexture(GL_TEXTURE_2D, glTex);

	// set texture environment
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	// Allocate storage
	glTexImage2D(
		GL_TEXTURE_2D,
		0,
		GL_RGBA, // use GL_BGRA for Windows? GL_RGBA
		width,
		height,
		0,
		GL_BGRA,
		GL_UNSIGNED_BYTE,
		NULL
	);
	// set up projection
	updateViewPort(windowWidth, windowHeight);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, 1, 1, 0, -1, 1);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	// many drivers Clear color is black with alpha = 0, which can cause nothing to appear
	glClearColor(0.f, 0.f, 0.f, 0.f);
	// Clear the buffer with predefined colour
	glClear(GL_COLOR_BUFFER_BIT);
	//
	return true;
}

void openGLFrameUpdate(SDL_Window* wnd, void* texture, unsigned int width, unsigned int height, unsigned int sync)
{
	int wndW, wndH;
	SDL_GL_GetDrawableSize(wnd, &wndW, &wndH);
	if (wndW != winWidth || wndH != winHeight) {
		updateViewPort(wndW, wndH);
	}
	if (vsync != sync) {
		vsync = sync;
		SDL_GL_SetSwapInterval(sync != 0);
	}
	// Upload texture
	glBindTexture(GL_TEXTURE_2D, glTex); //printf("glBindTexture Error: %u\n", glGetError());
	glTexSubImage2D(
		GL_TEXTURE_2D,
		0,
		0, 0,
		width,
		height,
		GL_BGRA,          // or GL_BGRA
		GL_UNSIGNED_BYTE,
		texture
	);
	// Draw
	glBegin(GL_QUADS);
	glTexCoord2f(0, 0); glVertex2f(0, 0);
	glTexCoord2f(1, 0); glVertex2f(1, 0);
	glTexCoord2f(1, 1); glVertex2f(1, 1);
	glTexCoord2f(0, 1); glVertex2f(0, 1);
	glEnd();
	// Present
	SDL_GL_SwapWindow(wnd); //printf("SDL_GL_SwapWindow Error: %u\n", glGetError());
}

void openGLClose()
{
	glDeleteTextures(1, &glTex);
	glTex = 0;
	SDL_GL_DeleteContext(glctx);
	glctx = NULL;
}