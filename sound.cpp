#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "sound.h"

//#define LOG_AUDIO
#ifndef __EMSCRIPTEN__
#define AUDIO_CALLBACK
#endif

#define SND_BUF_MAX_READ_AHEAD bufferMaxLeadInFrags
#define SND_LATENCY_IN_FRAGS bufferLatencyInFrags

// SDL specific
static SDL_AudioDeviceID dev;
static SDL_AudioSpec obtained, *audiohwspec;
//
static const unsigned int bufLenInMsec[] = { 20, 50, 100, 200, 10 };
static unsigned int bufLenIndex = 0;
static int bufferLatencyInFrags = 2;
static int bufferMaxLeadInFrags = 5;
static unsigned int BufferLengthInMsec;
//
static unsigned int	MixingFreq;
static unsigned int BufferLength;
static unsigned int	sndBufferPos;

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
static unsigned int soundEnabled = 1;
static unsigned int soundPaused = 0;

void SoundSource::bufferFill(unsigned int nrsamples, short *buffer)
{
	SoundSource *cb = SoundSource::getRoot();
	if (cb && !soundPaused) {
		cb->calcSamples(buffer, nrsamples);
		cb = cb->getNext();
	} else {
		memset(buffer, 0, nrsamples * 2);
		return;
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
#ifndef AUDIO_CALLBACK
	SDL_QueueAudio(dev, sndWriteBufferPtr, BufferLength * 2);
#endif
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
#ifdef AUDIO_CALLBACK
	return (int) (sndWriteBufferIndex - sndPlayBufferIndex);
#else
	unsigned int b = SDL_GetQueuedAudioSize(dev);
	return (int) (b / (BufferLength << 1));
#endif
}

static void fragmentDone()
{
	int lead_in_frags = getLeadInFrags();
#ifdef LOG_AUDIO
	printf("Lead in frags: %i\n", lead_in_frags);
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
	const unsigned int samplePos = (unsigned int) ((double) clock * (double) MixingFreq / deviceFrq);
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

static unsigned int calibrateAudioBufferSize(unsigned int msec, unsigned int sampleRate)
{
#ifdef __EMSCRIPTEN__
	// Emscripten needs a buffer with a size of a power of 2
	double x = msec * sampleRate / 1000.0;
	return (unsigned int)pow(2, ceil(log(x) / log(2)));
#elif defined(_WIN32)
	return (sampleRate / (1000 / msec));
#else
	// Linux needs a buffer with a size of a factor of 512?
	// 512 1024 2048 4096
	double x = msec * sampleRate / 1000.0;
	return (unsigned int)(x / 1024.0 + 0.5) * 1024;
#endif
}

// Emscripten requires audio buffers to be cleaned when stopped
void sound_reset()
{
	for (unsigned int i = 0; i < SND_BUF_MAX_READ_AHEAD * BufferLength; i++)
		sndRingBuffer[i] = audiohwspec->silence;
}

void init_audio(unsigned int sampleFrq)
{
    SDL_AudioSpec desired;

	if (sampleFrq < 11025 || sampleFrq > 192000) sampleFrq = SAMPLE_FREQ;
	MixingFreq = sampleFrq;

	BufferLengthInMsec = bufLenInMsec[bufLenIndex];
	BufferLength = calibrateAudioBufferSize(BufferLengthInMsec, MixingFreq);
	if (BufferLength < 512) BufferLength = 512;

	desired.freq		= MixingFreq;
	desired.format		= AUDIO_S16;
	desired.channels	= 1;
	desired.samples		= BufferLength;
	desired.callback	= 
#ifdef AUDIO_CALLBACK
		audioCallback;
#else
		NULL;
#endif
	desired.userdata	= NULL;
	desired.size		= desired.channels * desired.samples * sizeof(Uint8);
	desired.silence		= 0x00;

	mixingBuffer = new short[BufferLength];
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
    sound_resume();
}

void sound_pause()
{
#ifdef __EMSCRIPTEN__
	soundPaused = 1;
#else
	SDL_PauseAudioDevice(dev, 1);
#endif
}

void sound_resume()
{
	soundPaused = 0;
	SDL_PauseAudioDevice(dev, !soundEnabled);
}

void sound_change_freq(unsigned int &newFreq)
{
	close_audio();
	SoundSource *cb = SoundSource::getRoot();
	while (cb) {
		cb->setSampleRate(newFreq);
		cb = cb->getNext();
	}
	init_audio(newFreq);
	newFreq = audiohwspec->freq;
	SoundSource::setSamplingRate(newFreq);
}

void close_audio()
{
	SDL_PauseAudioDevice(dev, 1);
	SDL_CloseAudioDevice(dev);
	delete[] sndRingBuffer;
	delete[] mixingBuffer;
}

//-- sound options management

static void flipAudioBufferSize(void *none)
{
	const unsigned int arraySize = sizeof(bufLenInMsec) / sizeof(bufLenInMsec[0]);
	close_audio();
	bufLenIndex = (bufLenIndex + 1) % arraySize;
	init_audio(MixingFreq);
}

static void flipAudioFrequency(void *none)
{
	const unsigned int frq[] = { 48000, 96000, 192000, 22050, 44100 };
	const unsigned int current = MixingFreq;
	const unsigned int entries = sizeof(frq) / sizeof(frq[0]);
	int i = entries;
	bool found;

	do {
		i -= 1;
		found = frq[i] == current;
	} while (i && !found);
	i = (i + 1) % entries;
	unsigned int rate = frq[i];
	sound_change_freq(rate);
}

rvar_t soundSettings[] = {
	{ "Sound enabled", "SoundOn", NULL, &soundEnabled, RVAR_TOGGLE, NULL },
#ifndef __EMSCRIPTEN__
	{ "Audio frequency", "SoundFrequency", flipAudioFrequency, &MixingFreq, RVAR_INT, NULL },
	{ "Audio buffer length in msec", "AudioBufferLength", flipAudioBufferSize, &BufferLengthInMsec, RVAR_INT, NULL },
#endif
	{ "", "", NULL, NULL, RVAR_NULL, NULL }
};
