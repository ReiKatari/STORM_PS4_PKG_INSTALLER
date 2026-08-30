#pragma once

#include "Common.h"

#include <stdint.h>
#include <vector>
#include <string>
#include <orbis/libkernel.h>
#include <orbis/VideoOut.h>
#include <orbis/Sysmodule.h>
#include "stb_truetype.h"

// Dimensions
#define FRAME_WIDTH     1920
#define FRAME_HEIGHT    1080
#define FRAME_DEPTH        4

struct Color {
    uint8_t r, g, b, a;
    
    Color() : r(0), g(0), b(0), a(255) {}
    Color(uint8_t _r, uint8_t _g, uint8_t _b) : r(_r), g(_g), b(_b), a(255) {}
    Color(uint8_t _r, uint8_t _g, uint8_t _b, uint8_t _a) : r(_r), g(_g), b(_b), a(_a) {}
};

class Scene2D {
private:
	int width;
	int height;
	int depth;
	int video;
	
    off_t directMemOff;
	size_t directMemAllocationSize;
	
	uintptr_t videoMemSP;
	void *videoMem;
	
	char **frameBuffers;
	OrbisKernelEqueue flipQueue;
	OrbisVideoOutBufferAttribute attr;
	
	int frameBufferSize;
	int frameBufferCount;
	int activeFrameBufferIdx;

	bool initFlipQueue();
	bool allocateFrameBuffers(int num);
	char *allocateDisplayMem(size_t size);
	bool allocateVideoMem(size_t size, int alignment);
	void deallocateVideoMem();

public:
	Scene2D(int w, int h, int pixelDepth);
	~Scene2D();
	
	bool Init(size_t memSize, int numFrameBuffers);
	
	void SubmitFlip(int frameID);
	void FrameWait(int frameID);
	void FrameBufferSwap();
	void FrameBufferClear();
	void FrameBufferFill(Color color);
	void BlitBuffer(uint32_t* src, int srcW, int srcH); // Fast memcpy blit
	
	void DrawPixel(int x, int y, Color color);
    void DrawRectangle(int x, int y, int w, int h, Color color);
    
    // Draw Bitmap Text
    // Draw Bitmap Text
    void DrawText(const char* text, int x, int y, Color color, int scale = 3);
};

// TrueType Font Wrapper
class Font {
public:
    stbtt_fontinfo info;
    uint8_t* ttf_buffer;
    float scale;
    int pixelHeight;

    Font(const char* path, int pixelHeight);
    ~Font();
    
    // Draw string using this font. Returns width of drawn string.
    int DrawText(Scene2D* scene, int x, int y, const char* text, Color color);
    int GetTextWidth(const char* text);
};

// Simplified PNG wrapper based on samples
class PNG {
public:
    int width, height;
    uint32_t *data;

    PNG(const char *path);
    PNG(const uint8_t *buffer, int size); // Load from memory
    ~PNG();
    void Draw(Scene2D *scene, int x, int y);
    void Draw(Scene2D *scene, int x, int y, int w, int h); // Scaled
};
