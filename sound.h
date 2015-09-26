#ifndef _SOUND_H
#define _SOUND_H

#include "types.h"

#define SAMPLE_FREQ 48000

template<typename T>
class LinkedList {
public:
    LinkedList() {
        count++;
    }
    ~LinkedList() {
        count--;
    }
    static T *getRoot() { return root; }
    T *getNext() { return next; }
    void add(T *ss) {
        if (root) {
            last = last->next = ss;
        } else {
            last = root = ss;
        }
        next = 0;
    }
    void remove(T *ss) {
        T *prevNode;
        T *nextNode;
        T *node = root;
        if (!node)
            return;

       prevNode = root;
        do {
            nextNode = node->next;
            if (node == ss) {
                if (node == root) {
                    root = nextNode;
                    if (!root)
                        last = 0;
                }
                if (node == last)
                    last = prevNode;
                if (prevNode) {
                    prevNode->next = nextNode;
                    if (nextNode == last)
                        last = nextNode;
                }
            }
            node = nextNode;
        } while (node);
    }
   // void cascadeCall()
private:
    T *next;
    static T *root;
    static T *last;
    static unsigned int count;
};

// derive from this class for sound sources
class SoundSource : public LinkedList<SoundSource> {
public:
    SoundSource() {
        add(this);
    }
    ~SoundSource() {
        remove(this);
    }
    static void bufferFill(unsigned int nrsamples, short *buffer);
    static void resetCallback();
    virtual void calcSamples(short *buffer, unsigned int nrsamples) = 0;
private:
    char name[16];
};

extern void init_audio(unsigned int sampleFrq = SAMPLE_FREQ);
extern void close_audio();
extern void sound_pause();
extern void sound_resume();
extern void updateAudio(unsigned int nrsamples);

extern void flushBuffer(ClockCycle cycle, unsigned int frq);
extern void writeSoundReg(ClockCycle cycle, unsigned int reg, unsigned char value);
extern void ted_sound_init(unsigned int mixingFreq);

#endif
