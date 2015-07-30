#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <math.h>
#include "sound.h"

//#define LOG_AUDIO
#define SND_BUF_MAX_READ_AHEAD 6
#define SND_LATENCY_IN_FRAGS 2

static int           MixingFreq;
static unsigned int           BufferLength;
static unsigned int			sndBufferPos;
static unsigned int		originalFrequency;

static short *sndRingBuffer;
static unsigned int sndWriteBufferIndex;
static unsigned int sndPlayBufferIndex;
static short *sndWriteBufferPtr;
static short *sndPlayBufferPtr;
static short lastSample;
static ClockCycle		lastUpdateCycle;
static unsigned int		lastSamplePos;

static SDL_AudioSpec		*audiohwspec;

static void add_new_frag()
{
	sndWriteBufferIndex++;
	sndWriteBufferPtr = sndRingBuffer
		+ (BufferLength * (sndWriteBufferIndex % SND_BUF_MAX_READ_AHEAD));
}

static void delete_frag()
{
	sndPlayBufferIndex++;
	sndPlayBufferPtr = sndRingBuffer
		+ (BufferLength * (sndPlayBufferIndex % SND_BUF_MAX_READ_AHEAD));
}

static int getLeadInFrags()
{
	return (int) (sndWriteBufferIndex - sndPlayBufferIndex);
}

static void fragmentDone()
{
	int lead_in_frags = getLeadInFrags();
#ifdef LOG_AUDIO
	fprintf(stderr, "Lead in frags: %i\n", lead_in_frags);
#endif

	while (lead_in_frags < SND_LATENCY_IN_FRAGS) {
#ifdef LOG_AUDIO
		fprintf(stderr, "   adding an extra frag.\n");
#endif
		render_audio(BufferLength, sndWriteBufferPtr);
		add_new_frag();
		lead_in_frags++;
	}
}

static void audioCallback(void *userdata, Uint8 *stream, int len)
{
	if (sndPlayBufferIndex < sndWriteBufferIndex) {
		if (len > (int)(BufferLength*2))
			len = BufferLength * 2;
   		memcpy(stream, sndPlayBufferPtr, len);
		lastSample = sndPlayBufferPtr[len/2 - 1];
   		delete_frag();
	} else {
		short *buf = (short *) stream;
		len /= 2;
		do {
			*buf++ = lastSample;
		} while(--len);
	}
#ifdef LOG_AUDIO
	fprintf(stderr, "Playing a frag (%i).\n", getLeadInFrags());
#endif
}

void updateAudio(unsigned int nrsamples)
{
	// SDL openaudio failed?
	if (!sndWriteBufferPtr)
		return;

	if (sndBufferPos + nrsamples >= BufferLength) {
		if (getLeadInFrags() > SND_LATENCY_IN_FRAGS) {
#ifdef LOG_AUDIO
			fprintf(stderr, "Skipping a frag.\n");
#endif
		} else {
			// Finish pending buffer...
			render_audio(BufferLength - sndBufferPos, sndWriteBufferPtr + sndBufferPos);
			add_new_frag();
			if ((sndBufferPos = (sndBufferPos + nrsamples) % BufferLength))
				render_audio(sndBufferPos, sndWriteBufferPtr);
			fragmentDone();
		}
	} else if (nrsamples) {
		render_audio(nrsamples, sndWriteBufferPtr + sndBufferPos);
		sndBufferPos += nrsamples;
	}
	//_ASSERT(sndBufferPos <= BufferLength);
}

static inline unsigned int getNrOfSamplesToGenerate(ClockCycle clock)
{
	// OK this should really be INT but I'm tired right now
	unsigned int samplePos = (unsigned int) ((double) clock * (double) MixingFreq / (originalFrequency * 8));
	unsigned int samplesToDo = samplePos - lastSamplePos;
	//fprintf( stderr, "Sound: %i cycles/%f samples\n", clock, (double) clock * (double) MixingFreq / (1778400.0/8.0));
	lastSamplePos = samplePos;
	return samplesToDo;
}

void flushBuffer(ClockCycle cycle)
{
	updateAudio(getNrOfSamplesToGenerate(cycle));
	lastUpdateCycle = cycle;
}

void init_audio(unsigned int soundFreq, unsigned int sampleFrq)
{
    SDL_AudioSpec *desired, *obtained = NULL;

	originalFrequency = soundFreq;
	MixingFreq = sampleFrq;//44100 22050 11025 96000 48000
	// Linux needs a buffer with a size of a factor of 2
	BufferLength = 1024; // 512 1024 2048 4096

	desired =(SDL_AudioSpec *) malloc(sizeof(SDL_AudioSpec));
	obtained=(SDL_AudioSpec *) malloc(sizeof(SDL_AudioSpec));

	desired->freq		= MixingFreq;
	desired->format		= AUDIO_S16;
	desired->channels	= 1;
	desired->samples	= BufferLength;
	desired->callback	= audioCallback;
	desired->userdata	= NULL;
	desired->size		= desired->channels * desired->samples * sizeof(Uint8);
	desired->silence	= 0x00;

	sndBufferPos = 0;
	if (SDL_OpenAudio(desired, obtained)) {
		fprintf(stderr,"SDL_OpenAudio failed!\n");
		return;
	} else {
		fprintf(stderr,"SDL_OpenAudio success!\n");
    	fprintf(stderr, "Using audio driver : %s\n", SDL_GetCurrentAudioDriver());
		if ( obtained == NULL ) {
			fprintf(stderr, "Great! We have our desired audio format!\n");
			audiohwspec = desired;
			free(obtained);
		} else {
			//fprintf(stderr, "Oops! Failed to get desired audio format!\n");
			audiohwspec = obtained;
			free(desired);
		}
	}
	MixingFreq = audiohwspec->freq;
	BufferLength = audiohwspec->samples;

	fprintf(stderr, "Obtained mixing frequency: %u\n",audiohwspec->freq);
	fprintf(stderr, "Obtained audio format: %04X\n",audiohwspec->format);
	fprintf(stderr, "Obtained channel number: %u\n",audiohwspec->channels);
	fprintf(stderr, "Obtained audio buffer size: %u\n",audiohwspec->size);
	fprintf(stderr, "Obtained sample buffer size: %u\n",audiohwspec->samples);
	fprintf(stderr, "Obtained silence value: %u\n",audiohwspec->silence);

	SDL_PauseAudio(0);

	sndRingBuffer = new short[SND_BUF_MAX_READ_AHEAD * BufferLength];
	for(unsigned int i = 0; i < SND_BUF_MAX_READ_AHEAD * BufferLength; i++)
		sndRingBuffer[i] = audiohwspec->silence;
	sndWriteBufferIndex = sndPlayBufferIndex = 0;
	sndWriteBufferPtr = sndRingBuffer;
	sndPlayBufferPtr = sndRingBuffer + BufferLength * SND_LATENCY_IN_FRAGS;

	lastSample = 0;
	lastUpdateCycle = 0;
	lastSamplePos = 0;
    //SDL_PauseAudio(1);
}

void sound_pause()
{
	SDL_PauseAudio(1);
}

void sound_resume()
{
	SDL_PauseAudio(0);
}

void close_audio()
{
    SDL_PauseAudio(1);
    SDL_Delay(15);
	SDL_CloseAudio();
	if ( audiohwspec )
		free( audiohwspec );
    delete [] sndRingBuffer;
}
