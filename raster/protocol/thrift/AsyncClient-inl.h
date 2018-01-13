/*
 * Copyright (C) 2018, Yeolar
 */

#include <arpa/inet.h>

#include "raster/framework/HubAdaptor.h"
#include "raster/protocol/binary/Transport.h"
#include "raster/protocol/thrift/Util.h"
#include "raster/util/Logging.h"

namespace rdd {

template <class C>
TAsyncClient<C>::TAsyncClient(const ClientOption& option)
  : AsyncClient(Singleton<HubAdaptor>::try_get(), option) {
  init();
}

template <class C>
TAsyncClient<C>::TAsyncClient(const Peer& peer,
                           const TimeoutOption& timeout)
  : AsyncClient(Singleton<HubAdaptor>::try_get(), peer, timeout) {
  init();
}

template <class C>
TAsyncClient<C>::TAsyncClient(const Peer& peer,
                           uint64_t ctimeout,
                           uint64_t rtimeout,
                           uint64_t wtimeout)
  : AsyncClient(Singleton<HubAdaptor>::try_get(),
                peer, ctimeout, rtimeout, wtimeout) {
  init();
}

template <class C>
template <class Res>
bool TAsyncClient<C>::recv(void (C::*recvFunc)(Res&), Res& response) {
  if (!event_ || event_->state() == Event::kFail) {
    return false;
  }
  auto transport = event_->transport<BinaryTransport>();
  auto range = transport->body->coalesce();
  pibuf_->resetBuffer((uint8_t*)range.data(), range.size());

  if (keepalive_) {
    int32_t seqid = thrift::getSeqId(pibuf_.get());
    if (seqid != event_->seqid()) {
      RDDLOG(ERROR) << "peer[" << peer_ << "]"
        << " recv unmatched seqid: " << seqid << "!=" << event_->seqid();
      event_->setState(Event::kFail);
    }
  }
  (client_.get()->*recvFunc)(response);
  return true;
}

template <class C>
template <class Res>
bool TAsyncClient<C>::recv(Res (C::*recvFunc)(void), Res& response) {
  if (!event_ || event_->state() == Event::kFail) {
    return false;
  }
  auto transport = event_->transport<BinaryTransport>();
  auto range = transport->body->coalesce();
  pibuf_->resetBuffer((uint8_t*)range.data(), range.size());

  if (keepalive_) {
    int32_t seqid = thrift::getSeqId(pibuf_.get());
    if (seqid != event_->seqid()) {
      RDDLOG(ERROR) << "peer[" << peer_ << "]"
        << " recv unmatched seqid: " << seqid << "!=" << event_->seqid();
      event_->setState(Event::kFail);
    }
  }
  response = (client_.get()->*recvFunc)();
  return true;
}

template <class C>
template <class... Req>
bool TAsyncClient<C>::send(
    void (C::*sendFunc)(const Req&...), const Req&... requests) {
  if (!event_) {
    return false;
  }
  (client_.get()->*sendFunc)(requests...);

  if (keepalive_) {
    thrift::setSeqId(pobuf_.get(), event_->seqid());
  }
  uint8_t* p;
  uint32_t n;
  pobuf_->getBuffer(&p, &n);
  auto transport = event_->transport<BinaryTransport>();
  transport->sendHeader(n);
  transport->sendBody(IOBuf::copyBuffer(p, n));;
  return true;
}

template <class C>
template <class Res, class... Req>
bool TAsyncClient<C>::fetch(
    void (C::*recvFunc)(Res&), Res& response,
    void (C::*sendFunc)(const Req&...), const Req&... requests) {
  return (send(sendFunc, requests...) &&
          FiberManager::yield() &&
          recv(recvFunc, response));
}

template <class C>
template <class Res, class... Req>
bool TAsyncClient<C>::fetch(
    Res (C::*recvFunc)(void), Res& response,
    void (C::*sendFunc)(const Req&...), const Req&... requests) {
  return (send(sendFunc, requests...) &&
          FiberManager::yield() &&
          recv(recvFunc, response));
}

template <class C>
std::shared_ptr<Channel> TAsyncClient<C>::makeChannel() {
  return std::make_shared<Channel>(
      peer_, timeout_, make_unique<BinaryTransportFactory>());
}

template <class C>
void TAsyncClient<C>::init() {
  pibuf_.reset(new apache::thrift::transport::TMemoryBuffer());
  pobuf_.reset(new apache::thrift::transport::TMemoryBuffer());
  piprot_.reset(new apache::thrift::protocol::TBinaryProtocol(pibuf_));
  poprot_.reset(new apache::thrift::protocol::TBinaryProtocol(pobuf_));

  client_ = make_unique<C>(piprot_, poprot_);
  channel_ = makeChannel();
}

} // namespace rdd