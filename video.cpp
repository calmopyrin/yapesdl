#include <math.h>
#include "tedmem.h"
#include "video.h"
//
/* ---------- Inline functions ---------- */

template<typename T> T myMin(T a, T b)
{
	return a>b ? b : a;
}

template<typename T> T myMax(T a, T b)
{
	return a>b ? a : b;
}

inline static void RGB2YUV(double R, double G, double B, Yuv &yuv)
{
	// RGB -> YUV
	yuv.y = (int)(0.299*R + 0.587*G + 0.114*B + 0.5);
	yuv.u = (int)(- 0.14713*R - 0.28886*G + 0.436*B + 0.5); // U = 0.492111 (B'-Y')
	yuv.v = (int)(0.615*R - 0.51499*G - 0.10001*B + 0.5); // V = 0.877283 (R'-Y')
}

static unsigned int	palette[256];
static Yuv          yuvPalette[256];
static unsigned int doubleScan = 1;
static unsigned int evenFrame = 0;
static unsigned int interlacedShade = 85;
static unsigned int videoSaturation = 100;
static unsigned int videoBrightness = 100;
static int videoHueOffset = 0;
static unsigned int videoGammaCorrection = 0;

static double gammaCorr(double in)
{
	const double palCrtGamma = 2.8;
	const double targetGamma = 2.2;
	double out = pow(255.0, 1 - palCrtGamma) * pow(in, palCrtGamma);
	out = pow(255.0, 1 - 1 / targetGamma) * pow(out, 1 / targetGamma);
	return out;
}

void init_palette(TED *videoChip)
{
	unsigned int i;
	double	Uc, Vc, Yc,  PI = 4.0 * atan(1.0);

	i = videoChip->getColorCount();
	// calculate palette based on the HUE values
	for (unsigned int ix = 0; ix < i; ix++) {
		Color c = videoChip->getColor(ix);
		double bsat = c.saturation * videoSaturation / 100;
		double brf = (videoBrightness - 100.0) / 100.0;
		double hue = fmod(c.hue + double(videoHueOffset), 360.0);
		Uc = bsat * ((double) cos( hue * PI / 180.0 ));
		Vc = bsat * ((double) sin( hue * PI / 180.0 ));
		Yc = c.luma ? (myMin<double>(c.luma + brf, 5.0) - 2.0) * 255.0 / (5.0 - 2.0) : 0;
		// RED, GREEN and BLUE component
		double rf = (Yc + Vc / 0.877283);
		double gf = (Yc - 0.39465 * Uc - 0.58060 * Vc);
		double bf = (Yc + Uc / 0.492111);
		if (videoGammaCorrection) {
			rf = gammaCorr(rf);
			gf = gammaCorr(gf);
			bf = gammaCorr(bf);
		}
		Uint8 Rc = (Uint8) myMax<double>(myMin<double>(rf, 255.0), 0);
		Uint8 Gc = (Uint8) myMax<double>(myMin<double>(gf, 255.0), 0);
		Uint8 Bc = (Uint8) myMax<double>(myMin<double>(bf, 255.0), 0);

		palette[ix + 128] = palette[ix] = Bc | (Gc << 8) | (Rc << 16);
#if 0
		yuvPalette[ix].y = (unsigned char)(Yc);
		yuvPalette[ix].u = (int)(Uc + 128);
		yuvPalette[ix].v = (int)(Vc + 128);
#else
		rgb2yuv(Bc, Gc, Rc, yuvPalette[ix].y, yuvPalette[ix].u, yuvPalette[ix].v);
#endif
		yuvPalette[ix + 128] = yuvPalette[ix];
	}
}

unsigned int *palette_get_rgb()
{
	return palette;
}

static void flipInterlacedShade(void *none)
{
	interlacedShade = interlacedShade + 15;
	if (interlacedShade > 100) interlacedShade = 10;
}

static void flipVideoSaturation(void *none)
{
	videoSaturation = videoSaturation + 10;
	if (videoSaturation > 200) videoSaturation = 0;
	init_palette(theTed);
}

static void flipVideoBrightness(void *none)
{
	videoBrightness = videoBrightness + 15;
	if (videoBrightness > 200) videoBrightness = 25;
	init_palette(theTed);
}

static void flipVideoHueOffset(void *none)
{
	videoHueOffset = videoHueOffset + 5;
	if (videoHueOffset > 30) videoHueOffset = -30;
	init_palette(theTed);
}

static void toggleVideoGammaCorrection(void *none)
{
	videoGammaCorrection = 1 - videoGammaCorrection;
	init_palette(theTed);
}

rvar_t videoSettings[] = {
	{ "Interlaced line shade", "InterlacedShade", flipInterlacedShade, &interlacedShade, RVAR_INT, NULL },
	{ "Video saturation in percent", "VideoSaturation", flipVideoSaturation, &videoSaturation, RVAR_INT, NULL },
	{ "Video brightness in percent", "VideoBrightness", flipVideoBrightness, &videoBrightness, RVAR_INT, NULL },
	{ "Video hue offset in degrees", "VideoHueOffset", flipVideoHueOffset, &videoHueOffset, RVAR_INT, NULL },
	{ "Video CRT gamma correction", "VideoCrtGammaCorrection", toggleVideoGammaCorrection, &videoGammaCorrection, RVAR_TOGGLE, NULL },
	{ "", "", NULL, NULL, RVAR_NULL, NULL }
};

void video_convert_buffer(unsigned int *pImage, unsigned int srcpitch, unsigned char *screenptr)
{
	int i;
	int Uc[4], Vc[4], Up[4], Vp[4];

	const Yuv *yuvLookup = yuvPalette;
	const int interlace = 0;
//	const int thisFrameInterlaced = !evenFrame && doubleScan ? 1 : 0;
	unsigned char *fb = screenptr;
	unsigned char *prevLine = fb;

	i = SCREENY; /// 2;

	Up[0] = Up[1] = Up[2] = Up[3] = Vp[0] = Vp[1] = Vp[2] = Vp[3] = 0;
	do {
		int k = doubleScan & !interlace;
		do {
			int shade = doubleScan && !k ? interlacedShade : 100;
			int j = 0;
			const Yuv *yuvBuffer = yuvLookup;
//            const unsigned int lineVphase = (i & 1) ^ thisFrameInterlaced;
			const unsigned int invertPhase = 0; //(vPhase[0] ^ (lineVphase) && isPalMode) ? -1 : 0;
			const unsigned int invertPhaseNext = 0; //((vPhase[1] ^ (lineVphase ^ 1))  && isPalMode) ? -1 : 0;

			Yuv yuv = yuvBuffer[*fb];
			Uc[0] = Uc[1] = Uc[2] = Uc[3] = yuv.u;
			Vc[0] = Vc[1] = Vc[2] = Vc[3] = yuv.v ^ invertPhase;

			do {
				int Y0, Y1;
				const unsigned int dj = j << 1;
				const unsigned int filtX = dj & 3;
				const unsigned int filtNextX = (filtX + 1) & 3;

				// current row
				yuv = yuvBuffer[fb[dj]];
				Y0 = (int)(yuv.y) * shade / 100;
				Uc[filtX] = yuv.u;
				Vc[filtX] = yuv.v ^ invertPhase;
				// previous one
				yuv = yuvBuffer[prevLine[dj]];
				Up[filtX] = yuv.u;
				Vp[filtX] = yuv.v ^ invertPhaseNext;

				// move one pixel
				yuv = yuvBuffer[fb[dj + 1]];
				Y1 = (int)(yuv.y) * shade / 100;
				Uc[filtNextX] = yuv.u;
				Vc[filtNextX] = yuv.v ^ invertPhase;
				// previous row
				yuv = yuvBuffer[prevLine[dj + 1]];
				Up[filtNextX] = yuv.u;
				Vp[filtNextX] = yuv.v ^ invertPhaseNext;

				// average color signal
				// approximately a 13 -> 1.3 MHz Butterworth filter
				const unsigned char U = (Uc[0] + Uc[1] + Uc[2] + Uc[3] +
					Up[0] + Up[1] + Up[2] + Up[3]) / 8; // U
				const unsigned char V = (Vc[0] + Vc[1] + Vc[2] + Vc[3] +
					Vp[0] + Vp[1] + Vp[2] + Vp[3]) / 8; // V

				// store result
				*(pImage + j) =
					(U << 0)
					| (Y0 << 8)
					| (V << 16)
					| (Y1 << 24);

			} while (j++ < SCREENX / 2);
			// take care of double scan
			if (k == 0) {
				prevLine = fb;
			}
			pImage += srcpitch << interlace;// << doubleScan;
		} while (k--);
		fb += srcpitch;
	} while (--i);
	evenFrame = !evenFrame;
}
