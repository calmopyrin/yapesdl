#include <math.h>
#include "tedmem.h"
#include "video.h"
//
/* ---------- Inline functions ---------- */

static double myMin(double a, double b)
{
    return a>b ? b : a;
}

static double myMax(double a, double b)
{
    return a>b ? a : b;
}

inline static void RGB82YCrCb(double R, double G, double B, double &Yc, double &Cb, double &Cr)
{
	// The equations for color conversion used here, probably aren't
	// exact, but they seem to do an OK _fdc.
	Yc = 16  + 1.0 / 256.0 * (   65.738  * R +  129.057  * G +  25.064  * B);
	Cb = 0.0 + 1.0 / 256.0 * ( - 37.945  * R -   74.494  * G + 112.439  * B);
	Cr = 0.0 + 1.0 / 256.0 * (  112.439  * R -   94.154  * G -  18.285  * B);
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

void init_palette(TED *videoChip)
{
    unsigned int i;
	double	Uc, Vc, Yc,  PI = 3.14159265;

	i = videoChip->getColorCount();
	// calculate palette based on the HUE values
	for (unsigned int ix = 0; ix < i; ix++) {
		Color c = videoChip->getColor(ix);
		double bsat = c.saturation;
		Uc = bsat * ((double) cos( c.hue * PI / 180.0 ));
		Vc = bsat * ((double) sin( c.hue * PI / 180.0 ));
		Yc = c.luma ? (c.luma - 2.0) * 255.0 / (5.0 - 2.0) : 0;
		// RED, GREEN and BLUE component
		Uint8 Rc = (Uint8) myMax(myMin((Yc + Vc / 0.877283), 255.0), 0);
		Uint8 Gc = (Uint8) myMax(myMin((Yc - 0.39465 * Uc - 0.58060 * Vc ), 255.0), 0);
		Uint8 Bc = (Uint8) myMax(myMin((Yc + Uc / 0.492111), 255.0), 0);

		palette[ix + 128] = palette[ix] = Bc | (Gc << 8) | (Rc << 16);
#if 1
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

static bool doubleScan = true;
static unsigned int evenFrame = 0;

void video_convert_buffer(unsigned int *pImage, unsigned int srcpitch, unsigned char *screenptr)
{
	int i;
	int Uc[4], Vc[4], Up[4], Vp[4];

    const Yuv *yuvLookup = yuvPalette;
	const int interlace = 0;
//	const int thisFrameInterlaced = !evenFrame && doubleScan ? 1 : 0;
	unsigned char *fb = screenptr;
	unsigned char *prevLine = fb;
//	unsigned char *currLine = fb;

    i = SCREENY; /// 2;

    Up[0] = Up[1] = Up[2] = Up[3] = Vp[0] = Vp[1] = Vp[2] = Vp[3] = 0;
    do {
        int k = doubleScan & !interlace;
        do {
            int shade = doubleScan && !k ? 85 : 100;
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
                int filtX = j % 3;

                // current row
                yuv = yuvBuffer[fb[j << 1]];
                Y0 = (int)(yuv.y) * shade / 100;
                Uc[filtX] = yuv.u;
                Vc[filtX] = yuv.v ^ invertPhase;
                // previous one
                yuv = yuvBuffer[prevLine[j << 1]];
                Up[filtX] = yuv.u;
                Vp[filtX] = yuv.v ^ invertPhaseNext;

                // move one pixel
                yuv = yuvBuffer[fb[(j << 1) + 1]];
                Y1 = (int)(yuv.y) * shade / 100;
                Uc[(filtX + 1) % 3] = yuv.u;
                Vc[(filtX + 1) % 3] = yuv.v ^ invertPhase;
                // previous row
                yuv = yuvBuffer[prevLine[(j << 1) + 1]];
                Up[(filtX + 1) % 3] = yuv.u;
                Vp[(filtX + 1) % 3] = yuv.v ^ invertPhaseNext;

                // average color signal
                // approximately a 13 -> 1.3 MHz Butterworth filter
                const unsigned char U = (Uc[0] + Uc[1] + Uc[2] +
                    Up[0] + Up[1] + Up[2]) / 6; // U
                const unsigned char V = (Uc[0] + Vc[1] + Vc[2] +
                    Vp[0] + Vp[1] + Vp[2]) / 6; // V

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
