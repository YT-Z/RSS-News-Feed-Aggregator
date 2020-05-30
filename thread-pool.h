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

#ifndef _thread_pool_
#define _thread_pool_

#include <cstdlib>
#include <functional>
#include <queue>
#include <mutex>
#include "semaphore.h"
#include <thread>
#include <condition_variable>
// place additional #include statements here

namespace develop {

class ThreadPool {
 public:

/**
 * Constructs a ThreadPool configured to spawn up to the specified
 * number of threads.
 */
  ThreadPool(size_t numThreads);

/**
 * Destroys the ThreadPool class
 */
  ~ThreadPool();

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

 private:
  std::thread dt;

  int numPendingWorks;
  std::mutex pendingWorksMutex;

  std::mutex cvMutex;
  std::condition_variable_any cv;
  
  std::queue<std::function<void(void)>> thunksQueue;
  std::mutex thunksQueueMutex;

  semaphore scheduleSemaphore; // communication between schedule and dispatcher
  semaphore totalAvailableSemaphore; // the total available number of worker


  struct workerInfo {
    semaphore s;
    std::thread t;
    std::mutex m;
    bool occupied;
    std::function<void(void)> workerFunction;
  };
  std::vector<workerInfo> workers;

  void dispatcher();
  void worker(size_t workerID);
  
  ThreadPool(const ThreadPool& original) = delete;
  ThreadPool& operator=(const ThreadPool& rhs) = delete;
};

#endif

}
