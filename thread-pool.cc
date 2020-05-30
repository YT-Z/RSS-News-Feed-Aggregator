
/**
 * File: thread-pool.cc
 * --------------------
 * Presents the implementation of the ThreadPool class.
 */
#include "thread-pool.h"
#include <iostream>


using namespace std;
using develop::ThreadPool;

/**
 * Schedules the provided thunk (which is something that can
 * be invoked as a zero-argument function without a return value)
 * to be executed by one of the ThreadPool's threads as soon as
 * all previously scheduled thunks have been handled.
 */
ThreadPool::ThreadPool(size_t numThreads) : 
         numPendingWorks(0), allDone(false), scheduleSemaphore(0), 
         totalAvailableSemaphore(numThreads), workers(numThreads){
    // launceh a single dispatcher thread
    dt = thread([this]() {
        dispatcher();
    });
    // launch a specific number of worker threads
    for (size_t workerID = 0; workerID < numThreads; workerID++) {
        workers[workerID].t = thread([this](size_t workerID) {
            worker(workerID);
        }, workerID);
    }

}


/**
 * Schedules the provided thunk (which is something that can
 * be invoked as a zero-argument function without a return value)
 * to be executed by one of the ThreadPool's threads as soon as
 * all previously scheduled thunks have been handled.
 */
void ThreadPool::schedule(const std::function<void(void)>& thunk) {
    pendingWorksMutex.lock();
    numPendingWorks++;
    pendingWorksMutex.unlock();

    thunksQueueMutex.lock();
    thunksQueue.push(thunk);
    thunksQueueMutex.unlock();

    scheduleSemaphore.signal(); // works in schdule++
}

// dispatch work to available worker
void ThreadPool::dispatcher(){
    while (true) {
        scheduleSemaphore.wait(); // works in schedule--
        totalAvailableSemaphore.wait(); // totalAvailable--

        if (allDone) {
            return;
        }
        for (size_t workerID = 0; workerID < workers.size(); workerID++) {
            workers[workerID].m.lock();
            if (workers[workerID].occupied) {
                workers[workerID].m.unlock();
            } else {
                cout<<"dispatcher to: " << workerID << endl;
                workers[workerID].occupied = true;
                workers[workerID].m.unlock();

                thunksQueueMutex.lock();
                if (thunksQueue.empty()) {
                    thunksQueueMutex.unlock();
                    break;
                }
                workers[workerID].workerFunction = thunksQueue.front();
                thunksQueue.pop();
                thunksQueueMutex.unlock();

                workers[workerID].m.lock();
                workers[workerID].s.signal(); // inform worker there is a work to do
                workers[workerID].m.unlock();
                break;
            }
           
        }

    }
}

// excute an assigned function
void ThreadPool::worker(size_t workerID) {
    while (true) {
        workers[workerID].s.wait();
        if (allDone) {
            return;
        }

        workers[workerID].workerFunction();
        cout<< "worker:" << workerID << ",occupiec?should be T: "<< workers[workerID].occupied <<endl;
        workers[workerID].m.lock();
        workers[workerID].occupied = false;
        workers[workerID].m.unlock();

        totalAvailableSemaphore.signal(); // totalAvailable++

        pendingWorksMutex.lock();
        numPendingWorks--;
        cout<<"pending works:"<< numPendingWorks<< endl;
        pendingWorksMutex.unlock();
        cv.notify_all(); // wake up wait()
    }
}
/**
 * Blocks and waits until all previously scheduled thunks
 * have been executed in full.
 */
void ThreadPool::wait() {
    lock_guard<mutex> lg(cvMutex);
    cv.wait(cvMutex, [this] {
        return numPendingWorks == 0;
    });
    cvMutex.unlock();
}

/**
 * Destroys the ThreadPool class
 */
ThreadPool::~ThreadPool() {
    wait();
    allDone = true;
    scheduleSemaphore.signal();
    totalAvailableSemaphore.signal();
    for (size_t workerID = 0; workerID < workers.size(); workerID++) {
        workers[workerID].s.signal();
    }

    dt.join();
    for (size_t workerID = 0; workerID < workers.size(); workerID++) {
        workers[workerID].t.join();
    }
}

