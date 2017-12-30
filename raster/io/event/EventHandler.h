/*
 * Copyright (C) 2017, Yeolar
 */

#pragma once

#include "raster/io/event/Event.h"

namespace rdd {

class EventLoop;

class EventHandler {
 public:
  EventHandler(EventLoop* loop) : loop_(loop) {}

  void onListen(Event* event);
  void onConnect(Event* event);
  void onRead(Event* event);
  void onWrite(Event* event);
  void onComplete(Event* event);
  void onTimeout(Event* event);
  void onError(Event* event);

  void closePeer(Event* event);

 private:
  EventLoop* loop_;
};

} // namespace rdd
