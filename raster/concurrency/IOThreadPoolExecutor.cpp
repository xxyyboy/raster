/*
 * Copyright 2017 Facebook, Inc.
 * Copyright (C) 2017, Yeolar
 */

#include "raster/concurrency/IOThreadPoolExecutor.h"

namespace rdd {

IOThreadPoolExecutor::IOThreadPoolExecutor(
    size_t numThreads,
    std::shared_ptr<ThreadFactory> threadFactory,
    EventLoopManager* ebm,
    bool waitForAll)
    : ThreadPoolExecutor(numThreads, std::move(threadFactory), waitForAll),
      nextThread_(0),
      eventLoopManager_(ebm) {
  setNumThreads(numThreads);
}

IOThreadPoolExecutor::~IOThreadPoolExecutor() {
  stop();
}

void IOThreadPoolExecutor::add(VoidFunc func) {
  add(std::move(func), 0);
}

void IOThreadPoolExecutor::add(
    VoidFunc func,
    uint64_t expiration,
    VoidFunc expireCallback) {
  RWSpinLock::ReadHolder r{&threadListLock_};
  if (threadList_.get().empty()) {
    throw std::runtime_error("No threads available");
  }
  auto ioThread = pickThread();

  auto task = Task(std::move(func), expiration, std::move(expireCallback));
  auto wrappedVoidFunc = [ ioThread, task ]() mutable {
    runTask(ioThread, std::move(task));
    ioThread->pendingTasks--;
  };

  ioThread->pendingTasks++;
  ioThread->eventLoop->addCallback(std::move(wrappedVoidFunc));
}

std::shared_ptr<IOThreadPoolExecutor::IOThread>
IOThreadPoolExecutor::pickThread() {
  auto& me = *thisThread_;
  auto& ths = threadList_.get();
  // When new task is added to IOThreadPoolExecutor, a thread is chosen for it
  // to be executed on, thisThread_ is by default chosen, however, if the new
  // task is added by the clean up operations on thread destruction, thisThread_
  // is not an available thread anymore, thus, always check whether or not
  // thisThread_ is an available thread before choosing it.
  if (me && std::find(ths.cbegin(), ths.cend(), me) != ths.cend()) {
    return me;
  }
  auto n = ths.size();
  if (n == 0) {
    return me;
  }
  auto thread = ths[nextThread_.fetch_add(1, std::memory_order_relaxed) % n];
  return std::static_pointer_cast<IOThread>(thread);
}

EventLoop* IOThreadPoolExecutor::getEventLoop() {
  RWSpinLock::ReadHolder r{&threadListLock_};
  return pickThread()->eventLoop;
}

EventLoop* IOThreadPoolExecutor::getEventLoop(
    ThreadPoolExecutor::ThreadHandle* h) {
  auto thread = dynamic_cast<IOThread*>(h);
  if (thread) {
    return thread->eventLoop;
  }
  return nullptr;
}

EventLoopManager* IOThreadPoolExecutor::getEventLoopManager() {
  return eventLoopManager_;
}

std::shared_ptr<ThreadPoolExecutor::Thread> IOThreadPoolExecutor::makeThread() {
  return std::make_shared<IOThread>(this);
}

void IOThreadPoolExecutor::threadRun(ThreadPtr thread) {
  const auto ioThread = std::static_pointer_cast<IOThread>(thread);
  ioThread->eventLoop = eventLoopManager_->getEventLoop();
  thisThread_.reset(new std::shared_ptr<IOThread>(ioThread));

  ioThread->eventLoop->addCallback([thread] { thread->startupBaton.post(); });
  while (ioThread->shouldRun) {
    ioThread->eventLoop->loopForever();
  }
  if (isJoin_) {
    while (ioThread->pendingTasks > 0) {
      ioThread->eventLoop->loopOnce();
    }
  }
  if (isWaitForAll_) {
    // some tasks, like thrift asynchronous calls, create additional
    // event base hookups, let's wait till all of them complete.
    ioThread->eventLoop->loop();
  }

  std::lock_guard<std::mutex> guard(ioThread->eventLoopShutdownMutex_);
  ioThread->eventLoop = nullptr;
  eventLoopManager_->clearEventLoop();
}

// threadListLock_ is writelocked
void IOThreadPoolExecutor::stopThreads(size_t n) {
  std::vector<ThreadPtr> stoppedThreads;
  stoppedThreads.reserve(n);
  for (size_t i = 0; i < n; i++) {
    const auto ioThread =
        std::static_pointer_cast<IOThread>(threadList_.get()[i]);
    for (auto& o : observers_) {
      o->threadStopped(ioThread.get());
    }
    ioThread->shouldRun = false;
    stoppedThreads.push_back(ioThread);
    std::lock_guard<std::mutex> guard(ioThread->eventLoopShutdownMutex_);
    if (ioThread->eventLoop) {
      ioThread->eventLoop->stop();
    }
  }
  for (auto thread : stoppedThreads) {
    stoppedThreads_.add(thread);
    threadList_.remove(thread);
  }
}

uint64_t IOThreadPoolExecutor::getPendingTaskCount() {
  uint64_t count = 0;
  for (const auto& thread : threadList_.get()) {
    auto ioThread = std::static_pointer_cast<IOThread>(thread);
    size_t pendingTasks = ioThread->pendingTasks;
    if (pendingTasks > 0 && !ioThread->idle) {
      pendingTasks--;
    }
    count += pendingTasks;
  }
  return count;
}

} // namespace rdd
