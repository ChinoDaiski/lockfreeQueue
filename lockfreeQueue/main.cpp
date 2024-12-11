
#include <iostream>

#include "concurrent_queue.h"
#include "lockfree_queue.h"


int main(void)
{
	lockfree_queue<int> q;
	q.enqueue(10);

	int value;
	q.dequeue(value);
	std::cout << value << "\n";
	
	return 0;
}


