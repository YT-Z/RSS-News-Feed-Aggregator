
/**
 * File: thread-pool.cc
 * --------------------
 * Presents the implementation of the ThreadPool class.
 */
#include "thread-pool.h"
#include <iostream>
#include "ostreamlock.h"
#include "thread-utils.h"
using namespace std;
using develop::ThreadPool;

// constructor
ThreadPool::ThreadPool(size_t numThreads) : wts(numThreads), workerNum(numThreads), execNum(0), sd(0), dw_wr(numThreads), dw_rd(0) {
	dt = thread([this]() { dispatcher(); }); 
	for (size_t workerID = 0; workerID < numThreads; workerID++) {
		wts[workerID] = thread([this](size_t workerID) { worker(workerID); }, workerID); 
	}
}

// schedule
void ThreadPool::schedule(const function<void(void)>& thunk) {
	v_lock.lock();
	execNum++;
	v_lock.unlock();
	q_lock.lock();
	q1.push(thunk);
	q_lock.unlock();
	sd.signal();
}

// wait
void ThreadPool::wait() {
	unique_lock<mutex> ul(v_lock);
	cv.wait(ul, [this]{ return execNum == 0; });
}

// dispatcher
void ThreadPool::dispatcher() {
	while(true) {
		sd.wait();
		dw_wr.wait();
		q_lock.lock();
		if(q1.empty()) {
			q_lock.unlock();
			break;
		} else {
			q2.push(q1.front());
			q1.pop();
			q_lock.unlock();
			dw_rd.signal();
		}
	}
}

// worker
void ThreadPool::worker(size_t workerID) {
	while(true) {
		dw_rd.wait();
		q_lock.lock();
		if(q2.empty()) {
			q_lock.unlock();
			break;
		} else {
			const function<void(void)> thunk = q2.front();
			q2.pop();
			q_lock.unlock();
			thunk();	// execute the function
			dw_wr.signal();
			v_lock.lock();
			execNum--;	// decrement total function to be excuted
			v_lock.unlock();
			cv.notify_all();	// try to awake wait
		}
	}
}

// destructor
ThreadPool::~ThreadPool() {
	while(true) { 
		if(q1.empty()) {
			sd.signal(); 
			break;
		}
	}
	while(true) {
		if(q2.empty()) {
			for (size_t workerID = 0; workerID < workerNum; workerID++) {
				dw_rd.signal(); 
			}
			break;
		}
	}
	dt.join();	// reclaim dispatcher
	for(size_t workerID = 0; workerID < workerNum; workerID++) { wts[workerID].join(); }	// reclaim workers
}

// #include "thread-pool.h"

// using namespace std;
// using develop::ThreadPool;

// /**
//  * Schedules the provided thunk (which is something that can
//  * be invoked as a zero-argument function without a return value)
//  * to be executed by one of the ThreadPool's threads as soon as
//  * all previously scheduled thunks have been handled.
//  */
// ThreadPool::ThreadPool(size_t numThreads) : totalAvailableSemaphore(numThreads), workers(numThreads){
//     // launceh a single dispatcher thread
//     dt = thread([this]() {
//         dispatcher();
//     });
//     // launch a specific number of worker threads
//     for (size_t workerID = 0; workerID < numThreads; workerID++) {
//         workers[workerID].t = thread([this](size_t workerID) {
//             worker(workerID);
//         }, workerID);
//     }

// }


// /**
//  * Schedules the provided thunk (which is something that can
//  * be invoked as a zero-argument function without a return value)
//  * to be executed by one of the ThreadPool's threads as soon as
//  * all previously scheduled thunks have been handled.
//  */
// void ThreadPool::schedule(const std::function<void(void)>& thunk) {
//     pendingWorksMutex.lock();
//     numPendingWorks++;
//     pendingWorksMutex.unlock();

//     thunksQueueMutex.lock();
//     thunksQueue.push(thunk);
//     thunksQueueMutex.unlock();

//     scheduleSemaphore.signal(); // works in schdule++
// }

// // dispatch work to available worker
// void ThreadPool::dispatcher(){
//     while (true) {
//         scheduleSemaphore.wait(); // works in schedule--
//         totalAvailableSemaphore.wait(); // totalAvailable--
//         if (allDone) {
//             cvMutex.unlock();
//             return;
//         }
//         for (size_t workerID = 0; workerID < workers.size(); workerID++) {
//             workers[workerID].m.lock();
//             if (workers[workerID].occupied) {
//                 workers[workerID].m.unlock();
//             } else {
//                 workers[workerID].occupied = true;
//                 workers[workerID].m.unlock();

//                 thunksQueueMutex.lock();
//                 workers[workerID].workerFunction = thunksQueue.front();
//                 thunksQueue.pop();
//                 thunksQueueMutex.unlock();

//                 workers[workerID].m.lock();
//                 workers[workerID].s.signal(); // inform worker there is a work to do
//                 workers[workerID].m.unlock();
//                 break;
//             }
           
//         }

//     }
// }

// // excute an assigned function
// void ThreadPool::worker(size_t workerID) {
//     while (true) {
//         workers[workerID].s.wait();
//         if (allDone) {
//             cvMutex.unlock();
//             return;
//         }

//         workers[workerID].workerFunction();

//         workers[workerID].m.lock();
//         workers[workerID].occupied = false;
//         workers[workerID].m.unlock();

//         totalAvailableSemaphore.signal(); // totalAvailable++

//         pendingWorksMutex.lock();
//         numPendingWorks--;
//         pendingWorksMutex.unlock();
//         cv.notify_all(); // wake up wait()
//     }
// }
// /**
//  * Blocks and waits until all previously scheduled thunks
//  * have been executed in full.
//  */
// void ThreadPool::wait() {
//     lock_guard<mutex> lg(cvMutex);
//     cv.wait(cvMutex, [this] {
//         return numPendingWorks == 0;
//     });
// }

// /**
//  * Destroys the ThreadPool class
//  */
// ThreadPool::~ThreadPool() {
//     wait();
//     allDone = true;
//     scheduleSemaphore.signal();
//     totalAvailableSemaphore.signal();
//     for (size_t workerID = 0; workerID < workers.size(); workerID++) {
//         workers[workerID].s.signal();
//     }

//     dt.join();
//     for (size_t workerID = 0; workerID < workers.size(); workerID++) {
//         workers[workerID].t.join();
//     }
// }

