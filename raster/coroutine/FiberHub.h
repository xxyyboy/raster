/*
 * Copyright (C) 2018, Yeolar
 */

#pragma once

#include "accelerator/concurrency/CPUThreadPoolExecutor.h"
#include "raster/coroutine/Fiber.h"

namespace rdd {

class FiberHub {
 public:
  virtual acc::CPUThreadPoolExecutor* getCPUThreadPoolExecutor(int poolId) = 0;

  void execute(Fiber* fiber, int poolId);
  void execute(std::unique_ptr<Fiber::Task> task, int poolId);
};

} // namespace rdd
