#ifndef _TYPES_H
#define _TYPES_H

#ifdef _WIN32
#include <SDL/SDL.h>
#else
#include <SDL2/SDL.h>
#endif

typedef struct _MEM_PATCH {
	unsigned int addr;
	unsigned char byte;
} mem_patch;

#ifdef __GNUC__ /*__GNUC__*/
typedef unsigned long long ClockCycle;
#define TSTATE_T_MID (((long long) -1LL)/2ULL)
#define TSTATE_T_LEN "Lu"
#else
typedef unsigned __int64 ClockCycle;
#define TSTATE_T_MID (((__int64) -1L)/2UL)
#define TSTATE_T_LEN "lu"
#endif

// Simple linked list class
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

#endif // _TYPES_H
