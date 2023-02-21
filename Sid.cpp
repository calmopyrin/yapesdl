//  Issues:
//  - Filter cutoff frequencies not 100% accurate
//  - Combined waveforms of the 6581 incorrect (SID card used 8580 anyway)
//  - filter distortion not emulated
//  - no joystick or paddle support
//  - probably many more

#include <math.h>
#include "Sid.h"

#define DIGIBLASTER_MULT 14
#define EXACT_SYNC 1

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif

// Hack to store master volume
unsigned int SIDsound::masterVolume = 0;
// ugly but necessary for SID model selection to work for now
unsigned int SIDsound::model_ = SID6581;
unsigned int SIDsound::combinedWaveFormMask;
int SIDsound::dcMixer;
int SIDsound::dcVoice;
int SIDsound::dcWave;
int SIDsound::w0;
int SIDsound::cutOffFreq[2048];
unsigned int SIDsound::filterCutoff;
rvar_t SIDsound::sidSettings[2] = {
	{ "SID model", "SidModel", SIDsound::flipSidModel, &SIDsound::model_, RVAR_STRING_FLIPLIST, SIDsound::getSidModelLabel },
	{ "", "", NULL, NULL, RVAR_NULL, NULL }
};

//
//	Random number generator for noise waveform
//

// Test a bit. Returns 1 if bit is set.
inline static long bit(long val, unsigned int bitnr)
{
	return (val >> bitnr) & 1;
}

inline void SIDsound::SIDVoice::updateShiftReg()
{
	unsigned int bit22 = bit(shiftReg,22);
	unsigned int bit17 = bit(shiftReg,17);

	// Shift 1 bit left
	shiftReg = ((shiftReg) << 1);// & 0x7fffff;

	// Feed bit 0
	shiftReg = shiftReg | (bit22 ^ bit17);
	// Store output
	waveNoiseOut = waveNoise(shiftReg);
}

inline int SIDsound::waveNoise(unsigned int shiftReg)
{
	// Pick out bits to make output value, left shift by 4
	return
		(bit(shiftReg,22) << 11) |
		(bit(shiftReg,20) << 10) |
		(bit(shiftReg,16) << 9) |
		(bit(shiftReg,13) << 8) |
		(bit(shiftReg,11) << 7) |
		(bit(shiftReg, 7) << 6) |
		(bit(shiftReg, 4) << 5) |
		(bit(shiftReg, 2) << 4);
};

void SIDsound::setModel(unsigned int model)
{
	int i;
	double *temp = NULL;

	temp = new double[2048];
	if (!temp)
        return;

	switch (model) {
		case SID8580DB:
		case SID8580:
			for ( i=0; i<2048; i++) {
				double x = i / 8.0;
				//double cf = 12500.0 * i / 2048.0; // specs and YAPE
				// approximate with a 3-degree polynomial
				//double cf = 0.0003*x*x*x + 0.0882*x*x + 44.49*x - 38.409;
				// approximate with a 2-degree polynomial
				//double cf = -0.0177*x*x + 55.261*x - 55.518; // CSG 8580R4
				double cf = -0.0156*x*x + 48.473*x - 45.074; // 8580R5
				temp[i] = cf <= 0 ? 0 : cf;
			}
			dcWave = 0x000;
			dcMixer = 0;
			dcVoice = 0;
			combinedWaveFormMask = 0xFF;
			break;

		case SID6581: // R4 actually
			for (i=0; i<1024; i++) {
				temp[i] = (tanh(((double)i/1.5 - 1024.0)/1024.0*M_PI) + tanh(M_PI))
					* (6000.0 - 220.0) + 220.0;
			}
			for (; i<1056; i++) {
				double x = ((double)i - 1024.0) / (1056.0 - 1003.);
				temp[i] = x*(1315.0 - 1003.0) + 1003.0;
			}
			for (; i<2048; i++) {
				double x = ((double)i - 1056.0) / (2048.0 - 1056.0);
				temp[i] = //(tanh (((double)i - 2048.0)/1024.0*M_PI) + tanh(M_PI))
					//* (20163.0 - 1315.0) + 1315.0;
					(20163.0 - 1315.0) * x + 1315.0;
			}
			dcWave = 0x380;
			dcMixer = -0xFFF*0xFF/18 >> 7;
			dcVoice = 0x800*0xFF;
			combinedWaveFormMask = 0x3F;
			break;

		case SID6581R1: // 6581 R1
			for (i=0; i<1024; i++) {
				temp[i] = (tanh(((double)i-1024.0)/1024.0*M_PI) + tanh(M_PI))
					* (6000.0 - 220.0) + 220.0;
			}
			for (; i<2048; i++) {
				temp[i] = (tanh (((double)i-2048.0)/1024.0*M_PI) + tanh(M_PI))
					* (18000.0 - 4600.0) + 4600.0;
			}
			dcWave = 0x380;
			dcMixer = -0xFFF*0xFF/18 >> 7;
			dcVoice = 0x800*0xFF;
			combinedWaveFormMask = 0x3F;
			break;
	}
	for (i=0; i<2048; i++) {
		const double freqDomainDivCoeff = 2 * M_PI * 1.048576;
		cutOffFreq[i] = int(temp[i] * freqDomainDivCoeff);
	}
	delete [] temp;
	setFilterCutoff();
	model_ = model;
}

void SIDsound::setPaddleReadCallback(CallBackReadMemory func)
{
	readPaddle_ = func;
}

// Static data members
const unsigned int SIDsound::RateCountPeriod[16] = {
	0x7F00,0x0006,0x003C,0x0330,0x20C0,0x6755,0x3800,0x500E,
	0x1212,0x0222,0x1848,0x59B8,0x3840,0x77E2,0x7625,0x0A93
};

const unsigned char SIDsound::envGenDRdivisors[] = {
	1,30,30,30,30,30,30,16,16,16,16,16,16,16,16,8,
	8,8,8,8,8,8,8,8,8,8,8,4,4,4,4,4,
	4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
	4,4,4,4,4,4,4,2,2,2,2,2,2,2,2,2,
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
};

void SIDsound::calcEnvelopeTable()
{
	// number of SID clocks per sample
	sidCyclesPerSampleInt = (unsigned int) ((double) sidBaseFreq / (double) sampleRate);
	clockDeltaFraction = sidBaseFreq - sidCyclesPerSampleInt * sampleRate;
	clockDeltaRemainder = 0;
}

void SIDsound::setFrequency(unsigned int sid_frequency = SOUND_FREQ_PAL_C64)
{
	if (sid_frequency) {
		sidBaseFreq = sid_frequency;
	}
	calcEnvelopeTable();
}

void SIDsound::setSampleRate(unsigned int sampleRate_)
{
	sampleRate = sampleRate_;
	calcEnvelopeTable();
}

SIDsound::SIDsound(unsigned int model, unsigned int chnlDisableMask) : enableDigiBlaster(false), lastUpdate(0)
{
	unsigned int i;

	setId("SID0");
	// Link voices together
	for (i=0; i<3; i++) {
		voice[i].index = i;
		voice[i].modulatedBy = &voice[(i+2)%3]; // previous voice
		voice[i].modulatesThis = &voice[(i+1)%3]; // next voice
		voice[i].disabled = !!((chnlDisableMask >> i) & 1);
	}
	// startup defaults
	sidBaseFreq = SOUND_FREQ_PAL_C64;
	sampleRate = SAMPLE_FREQ;
	filterCutoff = 0;
	setModel(model);
	calcEnvelopeTable();
	reset();
	setPaddleReadCallback(readPaddleEmpty);
}

void SIDsound::reset(void)
{
	volume = masterVolume;

	lastByteWritten = 0;

	for (int v=0; v<3; v++) {
		voice[v].wave = WAVE_NONE;
		voice[v].egState = EG_FROZEN;
		voice[v].accu = 0;
		voice[v].freq = voice[v].pw = 0;
		voice[v].envCurrLevel = voice[v].envSustainLevel = 0;
		voice[v].gate = voice[v].ring = voice[v].test = 0;
		voice[v].filter = voice[v].sync = false;
		voice[v].muted = 0;
		// Initial value of internal shift register
		voice[v].shiftReg = 0x7FFFFC;
		voice[v].envExpCounter = 0;
		// don't forget to zero the cached wave noise
		voice[v].waveNoiseOut = 0;
		voice[v].envAttackAdd = voice[v].envDecaySub = voice[v].envReleaseSub = 0;
		voice[v].envCounterCompare = 0;
		voice[v].envCounter = 0x7fff;
		voice[v].lastWaveFormOutput = 0;
	}
	for (unsigned int i = 0; i < 32; i++)
		reg[i] = 0;

	filterType = FILTER_NONE;
	filterCutoff = filterResonance = 0;

	Vhp = Vbp = Vlp = 0;
	setFilterCutoff();
	setResonance();

	dcDigiBlaster = 0;
	clockDeltaRemainder = 0;
}

void SIDsound::dumpState()
{
	saveVar(&reg, sizeof(reg) / sizeof(reg[0]));
	for (unsigned int i = 0; i < 3; i++) {
		SIDVoice &v = voice[i];
		saveVar(&v.accu, sizeof(v.accu));
		saveVar(&v.accPrev, sizeof(v.accPrev));
		saveVar(&v.egState, sizeof(v.egState));
		saveVar(&v.envCounter, sizeof(v.envCounter));
		saveVar(&v.envExpCounter, sizeof(v.envExpCounter));
		saveVar(&v.shiftReg, sizeof(v.shiftReg));
	}
	//saveVar(&clockDeltaRemainder, sizeof(clockDeltaRemainder));
}

void SIDsound::readState()
{
	readVar(&reg, sizeof(reg) / sizeof(reg[0]));
	//
	for (unsigned int i = 0; i < 32; i++) {
		write(i, reg[i]);
	}
	for (unsigned int i = 0; i < 3; i++) {
		SIDVoice &v = voice[i];
		readVar(&v.accu, sizeof(v.accu));
		readVar(&v.accPrev, sizeof(v.accPrev));
		readVar(&v.egState, sizeof(v.egState));
		readVar(&v.envCounter, sizeof(v.envCounter));
		readVar(&v.envExpCounter, sizeof(v.envExpCounter));
		readVar(&v.shiftReg, sizeof(v.shiftReg));
	}
	//readVar(&clockDeltaRemainder, sizeof(clockDeltaRemainder));
	setFilterCutoff();
}

inline int SIDsound::getWaveSample(SIDVoice &v)
{
	switch (v.wave) {
		case WAVE_TRI:
			return waveTriangle(v);
		case WAVE_SAW:
			return waveSaw(v);
		case WAVE_PULSE:
			return wavePulse(v);
		case WAVE_TRISAW:
			return waveTriSaw(v);
		case WAVE_TRIPULSE:
			return waveTriPulse(v);
		case WAVE_SAWPULSE:
			return waveSawPulse(v);
		case WAVE_TRISAWPULSE:
			return waveTriSawPulse(v);
		case WAVE_NOISE:
			return v.waveNoiseOut;
		case WAVE_NONE:
			return 0x000;
		default:
			return 0x000;
	}
}

unsigned char SIDsound::read(unsigned int adr)
{
	switch (adr) {
	case 0x19:
	case 0x1A:
		// POTX/POTY paddle potentiometers
		lastByteWritten = 0;
		return readPaddle_(adr - 0x19);

		// Voice 3 (only) oscillator readout
		// 8 most significant bits
		case 0x1B:
			lastByteWritten = 0;
			{
				unsigned int bits = voice[2].wave & 0xF;
				// check if power of 2, i.e. no combined waveform
				unsigned char mask = !(bits & (bits - 1)) ? 0xFF : combinedWaveFormMask;
				return (unsigned char)(voice[2].lastWaveFormOutput >> 4) & (mask);
			}

		// Voice 3 EG readout
		case 0x1C:
			/*fprintf(stderr, "cycle: %010llu envcount : %04X state: %02X level: %02X\n",
				lastUpdate, voice[2].envCounter, voice[2].egState, voice[2].envCurrLevel);*/
 			return (unsigned char)(voice[2].envCurrLevel);

		case 0x1E: // Digiblaster DAC readout
			if (enableDigiBlaster && model_ == SID8580)
			{
				return (unsigned char) (dcDigiBlaster >> DIGIBLASTER_MULT);
			}
			return lastByteWritten;

		default:
			// Write-only registers return the last value written
			return lastByteWritten;
	}
}

void SIDsound::write(unsigned int adr, unsigned char value)
{
	lastByteWritten = value;

	SIDVoice &v = voice[adr/7];
	switch (adr) {
		case 0:
		case 7:
		case 14:
			v.freq = (unsigned short)((v.freq & 0xff00) | value);
			break;

		case 1:
		case 8:
		case 15:
			v.freq = (unsigned short)((v.freq & 0xff) | (value << 8));
			break;

		case 2:
		case 9:
		case 16:
			v.pw = (v.pw & 0xf00000) | (value << 12);
			break;

		case 3:
		case 10:
		case 17:
			v.pw = (v.pw & 0x0ff000) | ((value & 0xf) << 20);
			break;

		case 4:
		case 11:
		case 18:
			if ((value ^ v.gate) & 1) {
				if (value & 1) {
					// gate on
					v.egState = EG_ATTACK;
					v.envCounterCompare = v.envAttackAdd;
				} else {
					// gate off
					v.egState = EG_RELEASE;
					v.envCounterCompare = v.envReleaseSub;
				}
				v.gate = value & 1;
			}
			v.sync = value & 2;
			v.ring = value & 4;
			// Latch wave output if waveform 0 is selected
			// Waveform 0 outputs the previous oscillator value (with analogue fade in the real chip)
			// Note: we would not need this if SID emulation was cycle based
			if (v.wave && !(value >> 4)) {
				v.lastWaveFormOutput = getWaveSample(voice[2]);
			}
			if ((value & 8) && !v.test) {
				v.accu = 0;
				unsigned int bit19 = (v.shiftReg >> 19) & 1;
				v.shiftReg = (v.shiftReg & 0x7ffffd) | ((bit19^1) << 1);
				v.test = 0xFFF;
			} else if (v.test && !(value & 8)) {
				unsigned int bit0 = ((v.shiftReg >> 22) ^ (v.shiftReg >> 17)) & 0x1;
				v.shiftReg <<= 1;
				v.shiftReg &= 0x7fffff;
				v.shiftReg |= bit0;
				v.test = 0x000;
			}
			v.wave = (value >> 4) & 0x0F;
			if (v.wave > 8) {
				v.shiftReg &= 0x7fffff^(1<<22)^(1<<20)^(1<<16)^(1<<13)^(1<<11)^(1<<7)^(1<<4)^(1<<2);
				v.waveNoiseOut = 0;
			}
			break;

		case 5:
		case 12:
		case 19:
			v.envAttackAdd = value >> 4;
			v.envDecaySub = value & 0x0F;
			if (v.egState == EG_ATTACK)
				v.envCounterCompare = v.envAttackAdd;
			else if (v.egState == EG_DECAY)
				v.envCounterCompare = v.envDecaySub;
			break;

		case 6:
		case 13:
		case 20:
			v.envSustainLevel = (value >> 4) * 0x11;
			v.envReleaseSub = value & 0x0F;
			if (v.egState == EG_RELEASE)
				v.envCounterCompare = v.envReleaseSub;
			break;

		case 21:
			if ((value ^ filterCutoff) & 7) {
				filterCutoff = (value & 7) | (filterCutoff & 0x7F8);
				setFilterCutoff();
			}
			break;

		case 22:
			filterCutoff = (value << 3) | (filterCutoff & 7);
			setFilterCutoff();
			break;

		case 23:
			voice[0].filter = value & 1;
			voice[1].filter = value & 2;
			voice[2].filter = value & 4;
			filterResonance = (unsigned char)(value >> 4);
			setResonance();
			break;

		case 24:
			volume = value & 0x0F;
			voice[2].muted = value & 0x80;
			filterType = (unsigned char)((value >> 4) & 7);
			break;

		case 30: // Digiblaster DAC
			if (enableDigiBlaster && model_ == SID8580)
			{
				dcDigiBlaster = (value ^ 0x00) << DIGIBLASTER_MULT;
			}
			break;

		case 31: // Digiblaster ADC
			break;
	}
	reg[adr] = value;
}

inline void SIDsound::setFilterCutoff()
{
	w0 = cutOffFreq[filterCutoff];
	// Limit cutoff to Nyquist frq to keep the sample based filter stable
	const int NyquistFrq = sampleRate / 2;
	const int maxCutOff = NyquistFrq > 16000 ? 16000 : NyquistFrq;
	const double freqDomainDivCoeff = 2 * M_PI * 1.048576;
	const int w0MaxDt = int(maxCutOff * freqDomainDivCoeff); // 16000
	if (w0 > w0MaxDt) w0 = w0MaxDt;
}

inline void SIDsound::setResonance()
{
	resonanceCoeffDiv1024 = (int) (1024.0/(0.707 + 1.9 * (double) filterResonance / 15.0) + 0.5); // 2.3
}

inline unsigned int SIDsound::clock()
{
	unsigned int count = sidCyclesPerSampleInt;
	unsigned int newCount = clockDeltaFraction + clockDeltaRemainder;

	if (newCount >= sampleRate) {
		clockDeltaRemainder = newCount - sampleRate;
		count++;
	} else
		clockDeltaRemainder = newCount;
	return count;
}

// simplified version of http://bel.fi/~alankila/c64-sw/index-cpp.html
inline int SIDsound::filterOutput(unsigned int cycles, int Vi)
{
	const int w0deltaTime = w0 >> 6;
	Vi >>= 7;
	unsigned int count = cycles;

	do {
		int dVlp = (w0deltaTime * Vbp >> 14);
		Vlp -= dVlp;
		int dVbp = (w0deltaTime * Vhp >> 14);
		Vbp -= dVbp;
		Vhp = (Vbp * resonanceCoeffDiv1024 >> 10) - Vlp - Vi;
	} while (--count);

	int Vf;

	switch (filterType) {
		default:
		case FILTER_NONE:
			Vf = 0;
			break;
		case FILTER_LP:
			Vf = Vlp;
			break;
		case FILTER_BP:
			Vf = Vbp;
			break;
		case FILTER_LPBP:
			Vf = Vlp + Vbp;
			break;
		case FILTER_HP:
			Vf = Vhp;
			break;
		case FILTER_NOTCH:
			Vf = Vlp + Vhp;
			break;
		case FILTER_HPBP:
			Vf = Vbp + Vhp;
			break;
		case FILTER_ALL:
			Vf = Vlp + Vbp + Vhp;
			break;
	}
	return Vf << 7;
}

// Envelope based on:
// http://blog.kevtris.org/?p=13
inline void SIDsound::SIDVoice::doEnvelopeGenerator(const unsigned int cycles)
{
	unsigned int count = cycles;

	do {
		unsigned int LFSR = envCounter;
		if (LFSR != RateCountPeriod[envCounterCompare & 0x0f]) {
			const unsigned int feedback = ((LFSR >> 14) ^ (LFSR >> 13)) & 1;
			LFSR = ((LFSR << 1) | feedback) & 0x7FFF;
			envCounter = LFSR;
		} else {
			// LFSR = 0x7fff reset LFSR
			envCounter = 0x7fff;

			if (egState == EG_ATTACK || ((egState != EG_FROZEN) && ++envExpCounter == envGenDRdivisors[envCurrLevel & 0xff])) {

				envExpCounter = 0;

				switch (egState) {

				case EG_ATTACK:
					// According to Bob Yannes, Attack is linear...
					if ( ((++envCurrLevel) & 0xFF) == 0xFF) {
						egState = EG_DECAY;
						envCounterCompare = envDecaySub;
					}
					break;

				case EG_DECAY:
					if (envCurrLevel != envSustainLevel) {
						--envCurrLevel &= 0xFF;
						if (!envCurrLevel)
							egState = EG_FROZEN;
					}
					break;

				case EG_RELEASE:
					envCurrLevel = (envCurrLevel - 1) & 0xFF;
					if (!envCurrLevel)
						egState = EG_FROZEN;
					break;

				case EG_FROZEN:
					envCurrLevel = 0;
					break;
				}
			}
		}
	} while (--count);

//	return v.envCurrLevel & 0xFF; // envelope is 8 bits
}

inline void SIDsound::SIDVoice::doAccuCycles(const unsigned int cyclesToDo)
{
	if (!test && freq) {
		// Update accumulator
		const unsigned int add = freq * cyclesToDo;
		accPrev = accu;
		accu += add;

		// noise shift register is updating even when waveform is not selected
		unsigned int accNext = accPrev;
		do {
			accNext += freq;
			// Update noise shift register when bit 19 is rising high (FIXME: this is delayed by 2 cycles)
			if (!(accPrev & 0x080000) && (accNext & 0x080000))
				updateShiftReg();
		} while (accNext < accu);
		// accu is 24 bit
		accu &= 0xFFFFFF;
	}
}

inline void SIDsound::SIDVoice::applySync()
{
#if EXACT_SYNC
	if (modulatesThis->sync && !(accPrev & 0x800000) && (accu & 0x800000)
		&& !(sync && !(modulatedBy->accPrev & 0x800000) && (modulatedBy->accu & 0x800000))
		)
		modulatesThis->accu = 0;
#else
	if (sync && !(modulatedBy->accPrev & 0x800000) && (modulatedBy->accu & 0x800000)
		/*&& !(modulatedBy->sync && !(modulatedBy->accPrev & 0x80000) &&
			((modulatedBy->accu + add) & 0x80000))*/
		)
	{
		accu = (modulatedBy->accu - 0x800000) & 0xFFFFFF;
	}
#endif
}

inline void SIDsound::updateState(unsigned int cyclesToDo)
{
	// Unrolled loop for the three voices
	// Waveform generators
	voice[0].doAccuCycles(cyclesToDo);
	voice[1].doAccuCycles(cyclesToDo);
	voice[2].doAccuCycles(cyclesToDo);
	// Apply syncing when all oscillators are updated
	voice[0].applySync();
	voice[1].applySync();
	voice[2].applySync();
	// Remember last value for OSC3 readout
	voice[2].lastWaveFormOutput = getWaveSample(voice[2]);
	// Envelope generators
	voice[0].doEnvelopeGenerator(cyclesToDo);
	voice[1].doEnvelopeGenerator(cyclesToDo);
	voice[2].doEnvelopeGenerator(cyclesToDo);
}

inline int SIDsound::getOutput(int cyclesToDo)
{
	// Outputs for normal and filtered sounds
	int sumFilteredOutput = 0;
	int sumOutput = 0;

	// Filtered and unfiltered output
	unsigned int j = 2;
	do {
		SIDVoice& v = voice[j];
		// test bit sets pulse high
		int output = v.test ? 0xFFF : getWaveSample(v);
		int envelope = v.envCurrLevel & 0xFF; // envelope is 8 bits

		if (v.filter)
			sumFilteredOutput += (output - dcWave) * envelope + dcVoice;
		else {
			if (v.muted)
				sumOutput += (0x0000 - dcWave) * envelope + dcVoice;
			else
				sumOutput += (output - dcWave) * envelope + dcVoice;
		}
	} while (j--);

	return (sumOutput + filterOutput(cyclesToDo, sumFilteredOutput)
		+ dcMixer + dcDigiBlaster) * volume;
}

void SIDsound::calcSamples(short *buf, unsigned int count)
{
	do {
		const int cyclesToDo = clock();
		updateState(cyclesToDo);
		*buf++ = (short) (getOutput(cyclesToDo) >> 11);
	} while (--count);
}

SIDsound::~SIDsound()
{
	masterVolume = volume;
}

static short interimBuffer[312 * 63 * 2];
static unsigned int sampleUpdatePointer = 0;
static int lastCount = 0;

/*
void SIDsound::catchUpOnState(ClockCycle currCycle)
{
	int todo = (int)(currCycle - lastUpdate);
	while (todo > lastCount) {
		if (lastCount) {
			updateState(lastCount);
			interimBuffer[sampleUpdatePointer++] = (short)(getOutput(lastCount) >> 11);
			todo -= lastCount;
		}
		lastCount = clock();
	}
	//fprintf(stderr, "UPDATE cycle: %012llu todo: %012llu sample: %u\n", currCycle, lastUpdate, sampleUpdatePointer);
	if (todo > 0) {
		updateState(todo);
		lastCount -= todo;
	}
	lastUpdate = currCycle;
}

void SIDsound::calcSamples(short* buf, unsigned int count)
{
	//if (count > sizeof(interimBuffer) / 2)
	//	count = sizeof(interimBuffer) / 2;
	while (count > sampleUpdatePointer) {
		if (lastCount) {
			updateState(lastCount);
			interimBuffer[sampleUpdatePointer++] = (short)(getOutput(lastCount) >> 11);
			lastUpdate += lastCount;
		}
		if (count > sampleUpdatePointer)
			lastCount = clock();
		else
			lastCount = 0;
	}
	//fprintf(stderr, "FLUSH  cycle: %012llu todo: %08i sample: %08u\n", lastUpdate, lastCount, sampleUpdatePointer);
	//if (count < sampleUpdatePointer) {
	//	for (register unsigned int i = 0; i < count; i++)
	//		buf[i] = interimBuffer[i];
	//	for (register unsigned int i = 0; i < sampleUpdatePointer - count; i++)
	//		buf[count + i] = interimBuffer[count + i];
	//	sampleUpdatePointer = sampleUpdatePointer - count;
	//}
	for (register unsigned int i = 0; i < count; i++)
		buf[i] = interimBuffer[i];
	sampleUpdatePointer = 0;
}
*/

int cycleCountForSample = 0;

void SIDsound::updateLastCycleCount(ClockCycle currCycle)
{
	lastUpdate = currCycle;
}

void SIDsound::catchUpOnState(ClockCycle currCycle)
{
	int todo = (int)(currCycle - lastUpdate);

	if (todo > 0) {
		int cyclesLeft = todo;
		//cycleCountForSample += todo;
		//while (cycleCountForSample > lastCount) {
		//	lastCount = clock();
		//	if (sampleUpdatePointer >= sizeof(interimBuffer) / 2)
		//		return;
		//	updateState(lastCount);
		//	interimBuffer[sampleUpdatePointer++] = (short)(getOutput(lastCount) >> 11);
		//	cycleCountForSample -= lastCount;
		//	cyclesLeft -= lastCount;
		//}
		if (cyclesLeft <= 0)
			return;
		updateState(cyclesLeft);
		updateLastCycleCount(currCycle);
	}
}

void SIDsound::calcSamplesCycleBased(short* buf, unsigned int count)
{
	//if (count > sizeof(interimBuffer) / 2)
	//	count = sizeof(interimBuffer) / 2;
	while (count > sampleUpdatePointer) {
		catchUpOnState(lastUpdate + 1);
	}
	for (unsigned int i = 0; i < count; i++)
		buf[i] = interimBuffer[i];
	sampleUpdatePointer -= count;
	//fprintf(stderr, "FLUSH  cycle: %012llu todo: %08i sample: %08u\n", lastUpdate, lastCount, sampleUpdatePointer);
	if (sampleUpdatePointer) {
		for (unsigned int i = 0; i < sampleUpdatePointer; i++)
			interimBuffer[i] = interimBuffer[count + i];
		sampleUpdatePointer = 0;
	}
}
