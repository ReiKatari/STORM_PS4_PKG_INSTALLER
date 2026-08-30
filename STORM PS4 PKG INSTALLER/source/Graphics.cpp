
#include "../include/Graphics.h"
#include <orbis/VideoOut.h>
#include <orbis/Sysmodule.h>
#include <orbis/_types/sysmodule.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h> // for malloc/free

#define STB_IMAGE_IMPLEMENTATION
#include "../include/stb_image.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "../include/stb_truetype.h" // Local include

// External Log function
#include "../include/Common.h"

// Font Implementation
Font::Font(const char* path, int pixelHeight) : ttf_buffer(NULL), pixelHeight(pixelHeight), scale(0) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        Log("Font: Failed to open %s", path);
        return;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    ttf_buffer = (uint8_t*)malloc(size);
    if (!ttf_buffer) { 
        fclose(f); 
        Log("Font: malloc failed for %s", path);
        return; 
    }
    
    fread(ttf_buffer, 1, size, f);
    fclose(f);
    
    if (!stbtt_InitFont(&info, ttf_buffer, stbtt_GetFontOffsetForIndex(ttf_buffer, 0))) {
        Log("Font: stbtt_InitFont failed for %s", path);
        free(ttf_buffer);
        ttf_buffer = NULL;
        return;
    }
    
    scale = stbtt_ScaleForPixelHeight(&info, (float)pixelHeight);
    Log("Font: Loaded %s (size=%ld)", path, size);
}

Font::~Font() {
    if (ttf_buffer) free(ttf_buffer);
}

int Font::GetTextWidth(const char* text) {
    if (!ttf_buffer || !text) return 0;
    int width = 0;
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
    
    for (int i = 0; text[i]; i++) {
        int ax, lsb;
        stbtt_GetCodepointHMetrics(&info, text[i], &ax, &lsb);
        width += (int)(ax * scale);
        
        if (text[i+1]) {
            int kern = stbtt_GetCodepointKernAdvance(&info, text[i], text[i+1]);
            width += (int)(kern * scale);
        }
    }
    return width;
}

int Font::DrawText(Scene2D* scene, int x, int y, const char* text, Color color) {
    if (!ttf_buffer || !text) return 0;
    
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
    int baseline = y + (int)(ascent * scale);
    
    int startX = x;
    
    for (int i = 0; text[i]; i++) {
        int ax, lsb;
        stbtt_GetCodepointHMetrics(&info, text[i], &ax, &lsb);
        
        int c_x1, c_y1, c_x2, c_y2;
        stbtt_GetCodepointBitmapBox(&info, text[i], scale, scale, &c_x1, &c_y1, &c_x2, &c_y2);
        
        int y_off = baseline + c_y1;
        int x_off = x + c_x1;
        
        // Render glyph
        int w = c_x2 - c_x1;
        int h = c_y2 - c_y1;
        
        if (w > 0 && h > 0 && w * h < 16384) {
            uint8_t bitmap[16384]; // Max 128x128 glyph
            stbtt_MakeCodepointBitmap(&info, bitmap, w, h, w, scale, scale, text[i]);
            
            for (int r = 0; r < h; r++) {
                for (int c = 0; c < w; c++) {
                    uint8_t alpha = bitmap[r * w + c];
                    if (alpha > 0) {
                        Color pixelCol = color;
                        // Blend alpha
                        pixelCol.a = (uint8_t)((alpha * color.a) / 255);
                        scene->DrawPixel(x_off + c, y_off + r, pixelCol);
                    }
                }
            }
        }
        
        x += (int)(ax * scale);
        if (text[i+1]) {
            x += (int)(stbtt_GetCodepointKernAdvance(&info, text[i], text[i+1]) * scale);
        }
    }
    
    return x - startX;
}

Scene2D::Scene2D(int w, int h, int pixelDepth) {
	width = w;
	height = h;
	depth = pixelDepth;
	
	flipQueue = 0;
	videoMem = NULL;
	frameBuffers = NULL;
}

Scene2D::~Scene2D() {
    // Cleanup if needed
}

// External Log function - Moved to top
// extern void Log(const char* fmt, ...);

bool Scene2D::Init(size_t memSize, int numFrameBuffers) {
    // Initializing Video Output
	video = sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN, ORBIS_VIDEO_OUT_BUS_MAIN, 0, NULL);
	if(video < 0) {
        Log("Graphics: sceVideoOutOpen failed (0x%08X)", video);
        return false;
    }

	// Allocate Memory
	if (!allocateVideoMem(memSize, 2 * 1024 * 1024)) {
        Log("Graphics: allocateVideoMem failed");
        return false;
    }

	// Initializing Frame Buffers
	if (!allocateFrameBuffers(numFrameBuffers)) {
        Log("Graphics: allocateFrameBuffers failed");
        return false;
    }

	// Initializing Flip Queue
	if (!initFlipQueue()) {
        Log("Graphics: initFlipQueue failed");
        return false;
    }
	
	// Register buffers
	int ret = sceVideoOutRegisterBuffers(video, 0, (void **)frameBuffers, numFrameBuffers, &attr);
    if (ret < 0) {
        Log("Graphics: sceVideoOutRegisterBuffers failed (0x%08X)", ret);
        // Don't return false here, as it might still work partially
    }
	
	activeFrameBufferIdx = 0;
	return true;
}

bool Scene2D::initFlipQueue() {
    int rc = sceKernelCreateEqueue(&flipQueue, "flipQueue");
    if (rc < 0) return false;
    // CRITICAL: Add flip event to queue
    sceVideoOutAddFlipEvent(flipQueue, video, 0);
	return true;
}

bool Scene2D::allocateFrameBuffers(int num) {
	frameBuffers = (char **)malloc(num * sizeof(char *));
	if (!frameBuffers) return false;
    
    frameBufferSize = width * height * depth;
	frameBufferCount = num;
	
	// Attributes - Use 0x80000000 format as per OpenOrbis reference
    sceVideoOutSetBufferAttribute(&attr, 0x80000000, ORBIS_VIDEO_OUT_TILING_MODE_LINEAR, 0, width, height, width);

	for (int i = 0; i < num; i++) {
		frameBuffers[i] = (char *)videoMem + (i * frameBufferSize);
	}
	return true;
}

bool Scene2D::allocateVideoMem(size_t size, int alignment) {
	int ret = sceKernelAllocateDirectMemory(0,
										   sceKernelGetDirectMemorySize(), // Search full heap!
										   size,
										   alignment, 
										   ORBIS_KERNEL_WC_GARLIC,
										   &directMemOff);
    if (ret < 0) return false;
    
	directMemAllocationSize = size;
	
	return sceKernelMapDirectMemory(&videoMem,
								   size,
								   ORBIS_KERNEL_PROT_CPU_READ | ORBIS_KERNEL_PROT_CPU_WRITE | ORBIS_KERNEL_PROT_GPU_READ | ORBIS_KERNEL_PROT_GPU_WRITE,
								   0,
								   directMemOff,
								   alignment) >= 0;
}

void Scene2D::SubmitFlip(int frameID) {
	sceVideoOutSubmitFlip(video, activeFrameBufferIdx, ORBIS_VIDEO_OUT_FLIP_VSYNC, 0);
	
	// Prepare next buffer
	activeFrameBufferIdx = (activeFrameBufferIdx + 1) % frameBufferCount;
}

void Scene2D::FrameWait(int frameID) {
    // Simple wait
    OrbisKernelEvent ev;
    int out = 0;
    while (sceKernelWaitEqueue(flipQueue, &ev, 1, &out, 0) < 0) {
        // Wait
    }
}

void Scene2D::FrameBufferSwap() {
    // already handled by SubmitFlip and idx update
}

void Scene2D::FrameBufferClear() {
    Color blank = { 0, 0, 0, 255 }; // Black but opaque
    FrameBufferFill(blank);
}

void Scene2D::FrameBufferFill(Color color) {
    // OpenOrbis format: 0x80000000 + (R<<16) + (G<<8) + B
    uint32_t val = 0x80000000 + (color.r << 16) + (color.g << 8) + color.b;
    uint32_t *buf = (uint32_t*)frameBuffers[activeFrameBufferIdx];
    int count = width * height;
    
    for(int i=0; i<count; i++) buf[i] = val;
}

// OPTIMIZED: Fast blit pre-converted buffer (for background image)
void Scene2D::BlitBuffer(uint32_t* src, int srcW, int srcH) {
    if (!src) return;
    
    uint32_t* dst = (uint32_t*)frameBuffers[activeFrameBufferIdx];
    
    // Fast path: if source matches screen size, memcpy entire buffer
    if (srcW == width && srcH == height) {
        memcpy(dst, src, width * height * sizeof(uint32_t));
        return;
    }
    
    // Otherwise, copy line by line with clipping
    int copyW = (srcW < width) ? srcW : width;
    int copyH = (srcH < height) ? srcH : height;
    
    for (int y = 0; y < copyH; y++) {
        memcpy(&dst[y * width], &src[y * srcW], copyW * sizeof(uint32_t));
    }
}

void Scene2D::DrawPixel(int x, int y, Color color) {
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    
    uint32_t *buf = (uint32_t*)frameBuffers[activeFrameBufferIdx];
    uint32_t offset = y * width + x;
    
    // OpenOrbis format: 0x80000000 + (R<<16) + (G<<8) + B
    // Alpha blending
    if (color.a < 255) {
        uint32_t bg = buf[offset];
        uint8_t bg_r = (bg >> 16) & 0xFF;
        uint8_t bg_g = (bg >> 8) & 0xFF;
        uint8_t bg_b = (bg) & 0xFF;
        
        float alpha = color.a / 255.0f;
        float invAlpha = 1.0f - alpha;
        
        uint8_t out_r = (uint8_t)(color.r * alpha + bg_r * invAlpha);
        uint8_t out_g = (uint8_t)(color.g * alpha + bg_g * invAlpha);
        uint8_t out_b = (uint8_t)(color.b * alpha + bg_b * invAlpha);
        
        buf[offset] = 0x80000000 + (out_r << 16) + (out_g << 8) + out_b;
    } else {
        buf[offset] = 0x80000000 + (color.r << 16) + (color.g << 8) + color.b;
    }
}

void Scene2D::DrawRectangle(int x, int y, int w, int h, Color color) {
    // Check bounds
    if (x >= width || y >= height) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > width) w = width - x;
    if (y + h > height) h = height - y;
    if (w <= 0 || h <= 0) return;

    // Supports alpha blending? Yes, if alpha < 255
    if (color.a < 255) {
        // Slow path: utilize DrawPixel for alpha blending
        // OR implement manual blending here for speed
        for (int row = 0; row < h; row++) {
            for (int col = 0; col < w; col++) {
                DrawPixel(x + col, y + row, color);
            }
        }
        return;
    }

    // Fast path: Opaque color - OpenOrbis format
    uint32_t encodedColor = 0x80000000 + (color.r << 16) + (color.g << 8) + color.b;
    uint32_t* buffer = (uint32_t*)this->frameBuffers[this->activeFrameBufferIdx];

    // Optimize: calculate pointer once per row
    for (int row = 0; row < h; row++) {
        uint32_t* rowPtr = buffer + (y + row) * width + x;
        // Fill row
        for (int col = 0; col < w; col++) {
            rowPtr[col] = encodedColor;
        }
    }
}

// Minimal 5x7 font glyphs (Space .. Underscore)
static const uint8_t font5x7[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, // Space
    0x00, 0x00, 0x5F, 0x00, 0x00, // !
    0x00, 0x07, 0x00, 0x07, 0x00, // "
    0x14, 0x7F, 0x14, 0x7F, 0x14, // #
    0x24, 0x2A, 0x7F, 0x2A, 0x12, // $
    0x23, 0x13, 0x08, 0x64, 0x62, // %
    0x36, 0x49, 0x55, 0x22, 0x50, // &
    0x00, 0x05, 0x03, 0x00, 0x00, // '
    0x00, 0x1C, 0x22, 0x41, 0x00, // (
    0x00, 0x41, 0x22, 0x1C, 0x00, // )
    0x14, 0x08, 0x3E, 0x08, 0x14, // *
    0x08, 0x08, 0x3E, 0x08, 0x08, // +
    0x00, 0x50, 0x30, 0x00, 0x00, // ,
    0x08, 0x08, 0x08, 0x08, 0x08, // -
    0x00, 0x60, 0x60, 0x00, 0x00, // .
    0x20, 0x10, 0x08, 0x04, 0x02, // /
    0x3E, 0x51, 0x49, 0x45, 0x3E, // 0
    0x00, 0x42, 0x7F, 0x40, 0x00, // 1
    0x42, 0x61, 0x51, 0x49, 0x46, // 2
    0x21, 0x41, 0x45, 0x4B, 0x31, // 3
    0x18, 0x14, 0x12, 0x7F, 0x10, // 4
    0x27, 0x45, 0x45, 0x45, 0x39, // 5
    0x3C, 0x4A, 0x49, 0x49, 0x30, // 6
    0x01, 0x71, 0x09, 0x05, 0x03, // 7
    0x36, 0x49, 0x49, 0x49, 0x36, // 8
    0x06, 0x49, 0x49, 0x29, 0x1E, // 9
    0x00, 0x36, 0x36, 0x00, 0x00, // :
    0x00, 0x56, 0x36, 0x00, 0x00, // ;
    0x08, 0x14, 0x22, 0x41, 0x00, // <
    0x14, 0x14, 0x14, 0x14, 0x14, // =
    0x00, 0x41, 0x22, 0x14, 0x08, // >
    0x02, 0x01, 0x51, 0x09, 0x06, // ?
    0x32, 0x49, 0x79, 0x41, 0x3E, // @
    0x7E, 0x11, 0x11, 0x11, 0x7E, // A
    0x7F, 0x49, 0x49, 0x49, 0x36, // B
    0x3E, 0x41, 0x41, 0x41, 0x22, // C
    0x7F, 0x41, 0x41, 0x22, 0x1C, // D
    0x7F, 0x49, 0x49, 0x49, 0x41, // E
    0x7F, 0x09, 0x09, 0x09, 0x01, // F
    0x3E, 0x41, 0x49, 0x49, 0x7A, // G
    0x7F, 0x08, 0x08, 0x08, 0x7F, // H
    0x00, 0x41, 0x7F, 0x41, 0x00, // I
    0x20, 0x40, 0x41, 0x3F, 0x01, // J
    0x7F, 0x08, 0x14, 0x22, 0x41, // K
    0x7F, 0x40, 0x40, 0x40, 0x40, // L
    0x7F, 0x02, 0x0C, 0x02, 0x7F, // M
    0x7F, 0x04, 0x08, 0x10, 0x7F, // N
    0x3E, 0x41, 0x41, 0x41, 0x3E, // O
    0x7F, 0x09, 0x09, 0x09, 0x06, // P
    0x3E, 0x41, 0x51, 0x21, 0x5E, // Q
    0x7F, 0x09, 0x19, 0x29, 0x46, // R
    0x46, 0x49, 0x49, 0x49, 0x31, // S
    0x01, 0x01, 0x7F, 0x01, 0x01, // T
    0x3F, 0x40, 0x40, 0x40, 0x3F, // U
    0x1F, 0x20, 0x40, 0x20, 0x1F, // V
    0x3F, 0x40, 0x38, 0x40, 0x3F, // W
    0x63, 0x14, 0x08, 0x14, 0x63, // X
    0x07, 0x08, 0x70, 0x08, 0x07, // Y
    0x61, 0x51, 0x49, 0x45, 0x43, // Z
    0x00, 0x7F, 0x41, 0x41, 0x00, // [
    0x02, 0x04, 0x08, 0x10, 0x20, // (Backslash)
    0x00, 0x41, 0x41, 0x7F, 0x00, // ]
    0x04, 0x02, 0x01, 0x02, 0x04, // ^
    0x40, 0x40, 0x40, 0x40, 0x40, // _
};

void Scene2D::DrawText(const char* text, int x, int y, Color color, int scale) {
    if (!text) return;
    
    int cursorX = x;
    // scale is passed as argument
    
    for (int i = 0; text[i]; i++) {
        char c = text[i];
        if (c >= 'a' && c <= 'z') c -= 32; // Uppercase only for this basic font
        if (c < 32 || c > 95) c = ' ';
        
        int idx = (c - 32) * 5;
        
        for (int col = 0; col < 5; col++) {
            uint8_t line = font5x7[idx + col];
            for (int row = 0; row < 7; row++) {
                if ((line >> row) & 1) {
                    DrawRectangle(cursorX + (col * scale), y + (row * scale), scale, scale, color);
                }
            }
        }
        cursorX += 6 * scale;
    }
}

PNG::PNG(const char *path) {
    int n;
    this->data = (uint32_t*)stbi_load(path, &this->width, &this->height, &n, 4);
    if (!this->data) {
        Log("PNG: Failed to load %s - Reason: %s", path, stbi_failure_reason());
    } else {
        Log("PNG: Loaded %s (%dx%d)", path, this->width, this->height);
    }
}

PNG::PNG(const uint8_t *buffer, int size) {
    int n;
    this->data = (uint32_t*)stbi_load_from_memory(buffer, size, &this->width, &this->height, &n, 4);
    if (!this->data) {
        Log("PNG: Failed to load from memory - Reason: %s", stbi_failure_reason());
    } else {
        Log("PNG: Loaded from memory (%dx%d)", this->width, this->height);
    }
}

PNG::~PNG() {
    if (this->data) stbi_image_free(this->data);
}

void PNG::Draw(Scene2D *scene, int x, int y) {
    if (!this->data) return;
    
    // OPTIMIZED: Direct framebuffer access with memcpy per scanline
    // Get framebuffer pointer (accessing internal state)
    extern char** GetActiveFrameBuffer(Scene2D* scene);
    extern int GetFrameBufferWidth(Scene2D* scene);
    
    // Fallback to slow method if we can't access internals
    // For now, use an optimized loop that minimizes function calls
    
    // Pre-calculate constants outside inner loop
    int imgWidth = this->width;
    int imgHeight = this->height;
    uint32_t* imgData = this->data;
    
    // Clip to screen bounds
    int startX = (x < 0) ? -x : 0;
    int startY = (y < 0) ? -y : 0;
    int endX = (x + imgWidth > 1920) ? (1920 - x) : imgWidth;
    int endY = (y + imgHeight > 1080) ? (1080 - y) : imgHeight;
    
    if (startX >= endX || startY >= endY) return;
    
    // Draw in batches - process 4 pixels at once where possible
    for (int j = startY; j < endY; j++) {
        int screenY = y + j;
        int rowStart = j * imgWidth;
        
        for (int i = startX; i < endX; i++) {
            uint32_t pixel = imgData[rowStart + i];
            
            // Fast alpha check - skip fully transparent
            uint8_t a = (pixel >> 24) & 0xFF;
            if (a == 0) continue;
            
            // Extract RGB (stb_image loads as RGBA)
            uint8_t r = (pixel) & 0xFF;
            uint8_t g = (pixel >> 8) & 0xFF;
            uint8_t b = (pixel >> 16) & 0xFF;
            
            // Only call DrawPixel for visible pixels
            Color c = { r, g, b, a };
            scene->DrawPixel(x + i, screenY, c);
        }
    }
}

void PNG::Draw(Scene2D *scene, int x, int y, int w, int h) {
    if (!this->data) return;
    for (int dy = 0; dy < h; dy++) {
        int sy = (dy * this->height) / h;
        if (sy >= this->height) sy = this->height - 1;
        for (int dx = 0; dx < w; dx++) {
            int sx = (dx * this->width) / w;
            if (sx >= this->width) sx = this->width - 1;
            uint32_t pixel = this->data[sy * this->width + sx];
            uint8_t r = (pixel) & 0xFF;
            uint8_t g = (pixel >> 8) & 0xFF;
            uint8_t b = (pixel >> 16) & 0xFF;
            uint8_t a = (pixel >> 24) & 0xFF;
            if (a == 0) continue;
            scene->DrawPixel(x + dx, y + dy, {r, g, b, a});
        }
    }
}
