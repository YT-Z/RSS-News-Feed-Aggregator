#include "thread-pool.h"
#include <algorithm>
using namespace std;
using develop::ThreadPool;

void ThreadPool::dispatcher() {
  while (true) {
    lock_guard<mutex> lgThunkQueue(mThunkQueue);
    cvThunkQueue.wait(mThunkQueue, [this]{
      return !thunkQueue.empty() || exit;
    });
    if (exit) return;
    mThunkQueue.unlock();
    lock_guard<mutex> lgWorkerStatus(mWorkerStatus);
    cvWorkerStatus.wait(mWorkerStatus, [this]{
      return find(wBusy.begin(), wBusy.end(), false) != wBusy.end();
    });
    for (size_t workerID = 0; workerID < wBusy.size(); workerID++) {
      if (!wBusy[workerID]) {
        wBusy[workerID] = true;
        mWorkerStatus.unlock();
        cvWorkerStatus.notify_all();
        mThunkQueue.lock();
        wThunks[workerID] = thunkQueue.front();
        thunkQueue.pop();
        mThunkQueue.unlock();
        cvThunkQueue.notify_all();
        sWorkers[workerID].signal();
        break;
      }
    }
  }
}

void ThreadPool::worker(size_t workerID) {
  while (true) {
    sWorkers[workerID].wait();
    if (exit) return;
    wThunks[workerID]();
    lock_guard<mutex> lgWorkerStatus(mWorkerStatus);
    wBusy[workerID] = false;
    mWorkerStatus.unlock();
    cvWorkerStatus.notify_all();
  }
}

ThreadPool::ThreadPool(size_t numThreads) : wts(numThreads), exit(false),
  wBusy(numThreads, false), wThunks(numThreads), sWorkers(numThreads) {
  dt = thread([this]() { dispatcher(); });
  for (size_t workerID = 0; workerID < numThreads; workerID++) {
    wts[workerID] = thread([this](size_t workerID) {
      worker(workerID);
    }, workerID);
  }
}

void ThreadPool::schedule(const function<void(void)>& thunk) {
  lock_guard<mutex> lgThunkQueue(mThunkQueue);
  thunkQueue.push(thunk);
  mThunkQueue.unlock();
  cvThunkQueue.notify_all();
}

void ThreadPool::wait() {
  lock_guard<mutex> lgThunkQueue(mThunkQueue);
  cvThunkQueue.wait(mThunkQueue, [this]{ return thunkQueue.empty(); });
  mThunkQueue.unlock();
  lock_guard<mutex> lgWorkerStatus(mWorkerStatus);
  cvWorkerStatus.wait(mWorkerStatus, [this]{
    return find(wBusy.begin(), wBusy.end(), true) == wBusy.end();
  });
  mWorkerStatus.unlock();
}

ThreadPool::~ThreadPool() {
  wait();
  exit = true;
  cvThunkQueue.notify_all();
  cvWorkerStatus.notify_all();
  for (semaphore& s : sWorkers) s.signal();
  dt.join();
  for (thread& t : wts) t.join();
}



/**
 * File: thread-pool.cc
 * --------------------
 * Presents the implementation of the ThreadPool class.
 */


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
//     thunksQueueMutex.lock();
//     thunksQueue.push(thunk);

//     pendingWorksMutex.lock();
//     numPendingWorks++;
//     pendingWorksMutex.unlock();

//     thunksQueueMutex.unlock();
//     scheduleSemaphore.signal(); // works in schdule++
// }

// // dispatch work to available worker
// void ThreadPool::dispatcher(){
//     while (true) {
//         scheduleSemaphore.wait(); // works in schedule--
//         if (numPendingWorks == 0) return;
//         totalAvailableSemaphore.wait(); // totalAvailable--

//         for (size_t workerID = 0; workerID < workers.size(); workerID++) {
//             workers[workerID].m.lock();
//             if (workers[workerID].occupied) {
//                 workers[workerID].m.unlock();
//             } else {
//                 workers[workerID].occupied = true;

//                 thunksQueueMutex.lock();
//                 workers[workerID].workerFunction = thunksQueue.front();
//                 thunksQueue.pop();
//                 thunksQueueMutex.unlock();

//                 workers[workerID].s.signal();
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
//         if (numPendingWorks == 0) return;

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
//     scheduleSemaphore.signal();
//     for (size_t workerID = 0; workerID < workers.size(); workerID++) {
//         workers[workerID].s.signal();
//     }

//     dt.join();
//     for (size_t workerID = 0; workerID < workers.size(); workerID++) {
//         workers[workerID].t.join();
//     }
// }

