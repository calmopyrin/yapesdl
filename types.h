#ifndef _TYPES_H
#define _TYPES_H

#if defined(__EMSCRIPTEN__)
#include <SDL2/SDL.h>
#elif defined(_MSC_VER)
#include <SDL/SDL.h>
#else
#include <SDL2/SDL.h>
#endif

#ifdef __GNUC__ /*__GNUC__*/
typedef unsigned long long ClockCycle;
#define TSTATE_T_MID (((long long) -1LL)/2ULL)
#define TSTATE_T_LEN "Lu"
#else
typedef unsigned __int64 ClockCycle;
#define TSTATE_T_MID (((__int64) -1L)/2UL)
#define TSTATE_T_LEN "lu"
#endif

#ifndef MAX
#define MAX(A,B) ((A) > (B) ? (A) : (B))
#endif
#ifndef MIN
#define MIN(A,B) ((A) < (B) ? (A) : (B))
#endif

typedef struct _MEM_PATCH {
	unsigned int addr;
	unsigned char byte;
} mem_patch_t;

enum RvarTypes {
	RVAR_NULL,
	RVAR_TOGGLE,
	RVAR_INT,
	RVAR_HEX,
	RVAR_STRING,
	RVAR_STRING_FLIPLIST,
	RVAR_VARIANT
};

typedef struct _RVAR {
	char menuName[32];
	char resourceName[32];
	void (*callback)(void *);
	void *value;
	RvarTypes type;
	const char *(*label)();
} rvar_t;
extern rvar_t *settings[];

/* WAV file header structure */
/* should be 1-byte aligned */
#pragma pack(1)
typedef struct _WAV_HEADER {
	char riff[4];
	unsigned int rLen;
	char WAVEfmt[8];
	unsigned int fLen; /* 0x1020 */
	unsigned short wFormatTag; /* 0x0001 */
	unsigned short nChannels; /* 0x0001 */
	unsigned int nSamplesPerSec;
	unsigned int nAvgBytesPerSec; // nSamplesPerSec*nChannels*(nBitsPerSample%8)
	unsigned short nBlockAlign; /* 0x0001 */
	unsigned short nBitsPerSample; /* 0x0008 */
	char datastr[4];
	unsigned int data_size;
} wav_header_t;

#pragma pack()

typedef void(*CallBackFunctor)(void *);
typedef void(*CallBackFunctorInt8)(void* , unsigned char);
typedef unsigned char(*CallBackReadMemory)(unsigned short);

// Simple linked list class
template<typename T>
class LinkedList {
public:
	LinkedList() {
		next = nullptr;
		prev = nullptr;
		// Do NOT auto-add
	}
	static T* getRoot() { return root; }
	static T* getLast() { return last; }
	static unsigned int getCount() { return count; }

	T* getNext() const { return next; }
	T* getPrev() const { return prev; }

	// Add node to end of list
	static void add(T* node) {
		if (!node) return;

		node->next = nullptr;
		node->prev = last;

		if (last)
			last->next = node;
		else
			root = node;

		last = node;
		count++;
	}

	// Remove node from list
	static void remove(T* node) {
		if (!node) return;

		if (node->prev)
			node->prev->next = node->next;
		else
			root = node->next;

		if (node->next)
			node->next->prev = node->prev;
		else
			last = node->prev;

		node->next = nullptr;
		node->prev = nullptr;

		if (count > 0)
			count--;
	}

protected:
	T* next;
	T* prev;

private:
	static T* root;
	static T* last;
	static unsigned int count;
};

// static definitions
template<typename T> T* LinkedList<T>::root = nullptr;
template<typename T> T* LinkedList<T>::last = nullptr;
template<typename T> unsigned int LinkedList<T>::count = 0;

class Resettable : public LinkedList<Resettable> {
public:
	Resettable() {
		add(this);
	}
	~Resettable() {
		remove(this);
	}
	void resetAll(bool hard = false) {
		Resettable *rs = getRoot();
		while (rs) {
			rs->reset(hard);
			rs->getNext();
		}
	}
	virtual void reset(bool hard) = 0;
};

class MemoryHandler;

class Debuggable : public LinkedList<Debuggable> {
public:
	Debuggable() {
		add(this);
	}
	~Debuggable() {
		remove(this);
	}
	Debuggable *cycleToNext(Debuggable *d) {
		if (d) {
			Debuggable *n = d->getNext();
			if (!n)
				n = getRoot();
			return n;
		}
		else
			return getRoot();
	}
	virtual int disassemble(int pc, char *line) = 0;
	virtual void step() = 0;
	virtual unsigned int getProgramCounter() = 0;
	virtual MemoryHandler &getMem() = 0;
	virtual unsigned int getcycle() = 0;
	virtual void regDump(char *line, int rowcount) = 0;
	virtual const char *getName() = 0;
};

//template<> unsigned int LinkedList<Resettable>::count = 0;
//template<> Resettable* LinkedList<Resettable>::root = 0;
//template<> Resettable* LinkedList<Resettable>::last = 0;

#endif // _TYPES_H
