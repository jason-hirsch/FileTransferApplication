#ifndef BoundedBuffer_h
#define BoundedBuffer_h

#include <stdio.h>
#include <queue>
#include <string>
#include <mutex>
#include "Semaphore.h"

using namespace std;

class BoundedBuffer
{
private:
	int cap; // max number of items in the buffer
	queue<vector<char>> q;	/* the queue of items in the buffer. Note
	that each item a sequence of characters that is best represented by a vector<char> because: 
	1. An STL std::string cannot keep binary/non-printables
	2. The other alternative is keeping a char* for the sequence and an integer length (i.e., the items can be of variable length), which is more complicated.*/

	// add necessary synchronization variables (e.g., sempahores, mutexes) and variables
    Semaphore sema;
    Semaphore empty;
    Semaphore full;

public:
	BoundedBuffer(int _cap) : cap(_cap), sema(1), empty(_cap), full(0) {
	}

	~BoundedBuffer(){
	}

	void push(vector<char> data){
		// follow the class lecture pseudocode
		//1. Perform necessary waiting (by calling wait on the right semaphores and mutexes),
        empty.P();
        sema.P();
		//2. Push the data onto the queue
        q.push(data);
		//3. Do necessary unlocking and notification
        sema.V();
        full.V();

	}

	vector<char> pop(){
		//1. Wait using the correct sync variables
        full.P();
        sema.P();
		//2. Pop the front item of the queue.
        vector<char> front = q.front();
        q.pop();
		//3. Unlock and notify using the right sync variables
        sema.V();
        empty.V();
		//4. Return the popped vector
        return front;
	}
};

#endif /* BoundedBuffer_h */
