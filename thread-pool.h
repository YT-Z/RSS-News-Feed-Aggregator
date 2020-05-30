#ifndef _thread_pool_
#define _thread_pool_

#include <cstddef>     // for size_t
#include <functional>  // for the function template used in the schedule signature
#include <thread>      // for thread
#include <vector>      // for vector
#include <queue>
#include <mutex>
#include <condition_variable>
#include <semaphore.h>

namespace develop {
class ThreadPool {
 public:

/**
 * Constructs a ThreadPool configured to spawn up to the specified
 * number of threads.
 */
  ThreadPool(size_t numThreads);

/**
 * Schedules the provided thunk (which is something that can
 * be invoked as a zero-argument function without a return value)
 * to be executed by one of the ThreadPool's threads as soon as
 * all previously scheduled thunks have been handled.
 */
  void schedule(const std::function<void(void)>& thunk);

/**
 * Blocks and waits until all previously scheduled thunks
 * have been executed in full.
 */
  void wait();

/**
 * Waits for all previously scheduled thunks to execute, and then
 * properly brings down the ThreadPool and any resources tapped
 * over the course of its lifetime.
 */
  ~ThreadPool();
  
 private:
  std::thread dt;                                   // dispatcher thread handle
  std::vector<std::thread> wts;                     // worker thread handles
  bool exit;                                        // exit flag
  std::vector<bool> wBusy;                          // worker busy status
  std::vector<std::function<void(void)>> wThunks;   // thunks for each worker
  std::queue<std::function<void(void)>> thunkQueue; // thunk queue
  std::mutex mThunkQueue;                           // thunk queue mutex
  std::condition_variable_any cvThunkQueue;         // thunk queue cv
  std::mutex mWorkerStatus;                         // wBusy mutex
  std::condition_variable_any cvWorkerStatus;       // wBusy cv
  std::vector<semaphore> sWorkers;                  // worker semaphore vector

  void dispatcher();                                // dispatcher thread routine
  void worker(size_t workerID);                     // worker thread routine

/**
 * ThreadPools are the type of thing that shouldn't be cloneable, since it's
 * not clear what it means to clone a ThreadPool (should copies of all outstanding
 * functions to be executed be copied?).
 *
 * In order to prevent cloning, we remove the copy constructor and the
 * assignment operator.  By doing so, the compiler will ensure we never clone
 * a ThreadPool.
 */
  ThreadPool(const ThreadPool& original) = delete;
  ThreadPool& operator=(const ThreadPool& rhs) = delete;
};

#endif
}





/**
 * File: thread-pool.h
 * -------------------
 * Exports a ThreadPool abstraction, which manages a finite pool
 * of worker threads that collaboratively work through a sequence of tasks.
 * As each task is scheduled, the ThreadPool waits for at least
 * one worker thread to be free and then assigns that task to that worker.  
 * Threads are scheduled and served in a FIFO manner, and tasks need to
 * take the form of thunks, which are zero-argument thread routines.
 */

// #ifndef _thread_pool_
// #define _thread_pool_

// #include <cstdlib>
// #include <functional>
// #include <queue>
// #include <mutex>
// #include "semaphore.h"
// #include <thread>
// #include <condition_variable>
// // place additional #include statements here

// namespace develop {

// class ThreadPool {
//  public:

// /**
//  * Constructs a ThreadPool configured to spawn up to the specified
//  * number of threads.
//  */
//   ThreadPool(size_t numThreads);

// /**
//  * Destroys the ThreadPool class
//  */
//   ~ThreadPool();

// /**
//  * Schedules the provided thunk (which is something that can
//  * be invoked as a zero-argument function without a return value)
//  * to be executed by one of the ThreadPool's threads as soon as
//  * all previously scheduled thunks have been handled.
//  */
//   void schedule(const std::function<void(void)>& thunk);

// /**
//  * Blocks and waits until all previously scheduled thunks
//  * have been executed in full.
//  */
//   void wait();

//  private:
//   std::thread dt;

//   int numPendingWorks;
//   std::mutex pendingWorksMutex;

//   std::mutex cvMutex;
//   std::condition_variable_any cv;
  
//   std::queue<std::function<void(void)>> thunksQueue;
//   std::mutex thunksQueueMutex;

//   semaphore scheduleSemaphore; // communication between schedule and dispatcher
//   semaphore totalAvailableSemaphore; // the total available number of worker


//   struct workerInfo {
//     semaphore s;
//     std::thread t;
//     std::mutex m;
//     bool occupied;
//     std::function<void(void)> workerFunction;
//   };
//   std::vector<workerInfo> workers;

//   void dispatcher();
//   void worker(size_t workerID);
  
//   ThreadPool(const ThreadPool& original) = delete;
//   ThreadPool& operator=(const ThreadPool& rhs) = delete;
// };

// #endif

// }
