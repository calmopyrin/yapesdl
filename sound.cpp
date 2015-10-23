#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <math.h>
#include "sound.h"

#define SOUND_BUFSIZE_MSEC 20
// Linux needs a buffer with a size of a factor of 2
// 512 1024 2048 4096
#define FRAGMENT_SIZE (int(double(sampleFrq * SOUND_BUFSIZE_MSEC) / 1000.0 / 1024.0 + 0.5 ) * 1024)

//#define LOG_AUDIO
#define SND_BUF_MAX_READ_AHEAD 6
#define SND_LATENCY_IN_FRAGS 2

static SDL_AudioDeviceID dev;
static SDL_AudioSpec obtained, *audiohwspec;

static int           MixingFreq;
static unsigned int  BufferLength;
static unsigned int	 sndBufferPos;

static short *mixingBuffer;
static short *sndRingBuffer;
static unsigned int sndWriteBufferIndex;
static unsigned int sndPlayBufferIndex;
static short *sndWriteBufferPtr;
static short *sndPlayBufferPtr;
static short lastSample;
static ClockCycle		lastUpdateCycle;
static unsigned int		lastSamplePos;

template<> unsigned int LinkedList<SoundSource>::count = 0;
template<> SoundSource* LinkedList<SoundSource>::root = 0;
template<> SoundSource* LinkedList<SoundSource>::last = 0;
unsigned int SoundSource::sampleRate = SAMPLE_FREQ;

void SoundSource::bufferFill(unsigned int nrsamples, short *buffer)
{
	SoundSource *cb = SoundSource::getRoot();
	if (cb) {
		cb->calcSamples(buffer, nrsamples);
		cb = cb->getNext();
	}
	// multiple sources
	while (cb) {
		int i = nrsamples - 1;
		cb->calcSamples(mixingBuffer, nrsamples);
		do {
			buffer[i] += mixingBuffer[i];
		} while(i--);
		cb = cb->getNext();
	}
}

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
		SoundSource::bufferFill(BufferLength, sndWriteBufferPtr);
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
			SoundSource::bufferFill(BufferLength - sndBufferPos, sndWriteBufferPtr + sndBufferPos);
			add_new_frag();
			if ((sndBufferPos = (sndBufferPos + nrsamples) % BufferLength))
				SoundSource::bufferFill(sndBufferPos, sndWriteBufferPtr);
			fragmentDone();
		}
	} else if (nrsamples) {
		SoundSource::bufferFill(nrsamples, sndWriteBufferPtr + sndBufferPos);
		sndBufferPos += nrsamples;
	}
	//_ASSERT(sndBufferPos <= BufferLength);
}

static inline unsigned int getNrOfSamplesToGenerate(ClockCycle clock, unsigned int deviceFrq)
{
	// OK this should really be INT but I'm tired right now
	unsigned int samplePos = (unsigned int) ((double) clock * (double) MixingFreq / deviceFrq);
	unsigned int samplesToDo = samplePos - lastSamplePos;
	//fprintf( stderr, "Sound: %i cycles/%f samples\n", clock, (double) clock * (double) MixingFreq / deviceFrq);
	// 'clock' might have been reset but 'lastSamplePos' not!
	if (lastSamplePos > samplePos)
		samplesToDo = 0;
	lastSamplePos = samplePos;
	return samplesToDo;
}

void flushBuffer(ClockCycle cycle, unsigned int frq)
{
	updateAudio(getNrOfSamplesToGenerate(cycle, frq));
	lastUpdateCycle = cycle;
}

void init_audio(unsigned int sampleFrq)
{
    SDL_AudioSpec desired;

	MixingFreq = sampleFrq;

	BufferLength = FRAGMENT_SIZE;

	desired.freq		= MixingFreq;
	desired.format		= AUDIO_S16;
	desired.channels	= 1;
	desired.samples		= BufferLength;
	desired.callback	= audioCallback;
	desired.userdata	= NULL;
	desired.size		= desired.channels * desired.samples * sizeof(Uint8);
	desired.silence		= 0x00;

	mixingBuffer = new short[FRAGMENT_SIZE];
	if (!mixingBuffer)
		return;
	dev = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
	if (!dev) {
		fprintf(stderr, "SDL_OpenAudioDevice failed!\n");
		return;
	} else {
		fprintf(stderr, "SDL_OpenAudioDevice success!\n");
    	fprintf(stderr, "Using audio driver : %s\n", SDL_GetCurrentAudioDriver());
		audiohwspec = &obtained;
	}
	MixingFreq = audiohwspec->freq;
	BufferLength = audiohwspec->samples;

	fprintf(stderr, "Obtained mixing frequency: %u\n", audiohwspec->freq);
	fprintf(stderr, "Obtained audio format: %04X\n", audiohwspec->format);
	fprintf(stderr, "Obtained channel number: %u\n", audiohwspec->channels);
	fprintf(stderr, "Obtained audio buffer size: %u\n", audiohwspec->size);
	fprintf(stderr, "Obtained sample buffer size: %u\n", audiohwspec->samples);
	fprintf(stderr, "Obtained silence value: %u\n", audiohwspec->silence);

	sndRingBuffer = new short[SND_BUF_MAX_READ_AHEAD * BufferLength];
	for(unsigned int i = 0; i < SND_BUF_MAX_READ_AHEAD * BufferLength; i++)
		sndRingBuffer[i] = audiohwspec->silence;
	sndWriteBufferIndex = SND_LATENCY_IN_FRAGS;
	sndPlayBufferIndex = 0;
	sndWriteBufferPtr = sndRingBuffer + BufferLength * SND_LATENCY_IN_FRAGS;
	sndPlayBufferPtr = sndRingBuffer;

	sndBufferPos = 0;
	lastSample = 0;
	lastUpdateCycle = 0;
	lastSamplePos = 0;
    SDL_PauseAudioDevice(dev, 0);
}

void sound_pause()
{
	SDL_PauseAudioDevice(dev, 1);
}

void sound_resume()
{
	SDL_PauseAudioDevice(dev, 0);
}

void sound_change_freq(unsigned int &newFreq)
{
	close_audio();
	SoundSource *cb = SoundSource::getRoot();
	if (cb) {
		cb->setSampleRate(newFreq);
		cb = cb->getNext();
	}
	init_audio(newFreq);
	newFreq = audiohwspec->freq;
}

void close_audio()
{
	SDL_CloseAudioDevice(dev);
    delete[] sndRingBuffer;
	delete[] mixingBuffer;
}
