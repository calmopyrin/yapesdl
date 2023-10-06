#pragma once

#define SCREENX 384
#define SCREENY 288

#define rgb2y(b, g, r, y) \
	y=(unsigned char)(((int)(299*r) + (int)(587*g) + (int)(114*b) + 5)/1000)

#define rgb2yuv(b, g, r, y, u, v) \
	rgb2y(b, g, r, y); \
	u = (((int)(-169*r) - (int)(331*g) + (int)(499*b)+128000 + 5)/1000); \
	v = (((int)(499*r) - (int)(418*g) - (int)(81*b)+128000 + 5)/1000)

struct Yuv {
	unsigned char y;
	int u;
	int v;
};

class TED;

struct Color {
	double hue;
	double luma;
	double saturation;
};

extern void init_palette(TED *videoChip);
extern unsigned int *palette_get_rgb();
extern void video_convert_buffer(unsigned int *pImage, unsigned int srcpitch, unsigned char *screenptr);
extern rvar_t videoSettings[];
