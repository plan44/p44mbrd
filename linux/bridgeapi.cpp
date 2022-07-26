//
//  bridgeapi.cpp
//  p44mbrd
//
//  Copyright (c) 2022 plan44.ch / Lukas Zeller, Zurich, Switzerland
//

#include "bridgeapi.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/poll.h>


// MARK: - CHIP error


const char *ChipError::domain()
{
  return "CHIP";
}


const char *ChipError::getErrorDomain() const
{
  return ChipError::domain();
}


ChipError::ChipError(CHIP_ERROR aChipErr, const char *aContextMessage) :
  Error(aChipErr.AsInteger(), string(nonNullCStr(aContextMessage)).append(nonNullCStr(aChipErr.AsString())))
{
}


ErrorPtr ChipError::err(CHIP_ERROR aChipErr, const char *aContextMessage)
{
  if (aChipErr==CHIP_NO_ERROR)
    return ErrorPtr(); // empty, no error
  return ErrorPtr(new ChipError(aChipErr, aContextMessage));
}


// MARK: - bridge API

BridgeApi* gSharedBridgeApiP = nullptr;

BridgeApi& BridgeApi::sharedBridgeApi()
{
  if (!gSharedBridgeApiP) {
    gSharedBridgeApiP = new BridgeApi;
  }
  return *gSharedBridgeApiP;
}


BridgeApi::BridgeApi() :
  mApiSocketFd(-1),
  mApiSocketWatchToken(0),
  mApiSocketEventMask(POLLERR),
  mStandalone(true),
  mState(disconnected),
  mCallCounter(0)
{
}


void BridgeApi::setConnectionParams(const string aApiHost, uint16_t aApiPort, MLMicroSeconds aTimeout)
{
  mApiHost = aApiHost;
  mApiPort = aApiPort;
  setTimeout(aTimeout);
}


void BridgeApi::setTimeout(MLMicroSeconds aTimeout)
{
  mTimeout = aTimeout;
}



void BridgeApi::setIncomingMessageCallback(BridgeApiCB aIncomingDataCB)
{
  mIncomingDataCB = aIncomingDataCB;
}


BridgeApi::~BridgeApi()
{
  disconnect();
}


void BridgeApi::disconnect()
{
  if (mState!=disconnected) {
    mState = disconnected;
    if (mApiSocketFd>=0) {
      close(mApiSocketFd);
    }
  }
}



void BridgeApi::connect(BridgeApiCB aConnectedCB)
{
  int res;
  ErrorPtr err;
  struct addrinfo hint;
  struct addrinfo *addressInfoList = NULL;
  string port = string_format("%d", mApiPort);

  // first disconnect previous connection, if any
  disconnect();
  mNextStepCB = aConnectedCB;
  // assume internet connection -> get list of possible addresses and try them
  if (mApiHost.empty()) {
    err = TextError::err("Missing host name or address");
    goto done;
  }
  // try to resolve host and service name (at least: service name)
  memset(&hint, 0, sizeof(addrinfo));
  hint.ai_flags = 0; // no flags
  hint.ai_family = PF_UNSPEC;
  hint.ai_socktype = SOCK_STREAM;
  hint.ai_protocol = 0;
  res = getaddrinfo(mApiHost.c_str(), port.c_str(), &hint, &addressInfoList);
  if (res!=0) {
    // error
    err = TextError::err("getaddrinfo error %d: %s", res, gai_strerror(res));
    DBGLOG(LOG_DEBUG, "SocketComm: getaddrinfo failed: %s", err->text());
    goto done;
  }
  // now try all addresses in the list
  // try to create a socket
  // as long as we have more addresses to check and not already connecting
  while (addressInfoList) {
    err.reset();
    mApiSocketFd = socket(addressInfoList->ai_family, addressInfoList->ai_socktype, addressInfoList->ai_protocol);
    if (mApiSocketFd==-1) {
      err = SysError::errNo("Cannot create client socket: ");
    }
    else {
      // usable address found, socket created
      // - make socket non-blocking
      int flags;
      if ((flags = fcntl(mApiSocketFd, F_GETFL, 0))==-1)
        flags = 0;
      fcntl(mApiSocketFd, F_SETFL, flags | O_NONBLOCK);
      // Now we have a socket
      // - need to watch for POLLOUT to detect connection
      setEventWatchingMask(POLLOUT|POLLIN);
      // - initiate connection
      LOG(LOG_DEBUG, "- Attempting connection with address family = %d, protocol = %d, addrlen=%d/sizeof=%zu", addressInfoList->ai_family, addressInfoList->ai_protocol, addressInfoList->ai_addrlen, sizeof(*(addressInfoList->ai_addr)));
      res = ::connect(mApiSocketFd, addressInfoList->ai_addr, addressInfoList->ai_addrlen);
      if (res==0 || errno==EINPROGRESS) {
        // connection initiated (or already open, but connectionMonitorHandler will take care in both cases)
        mState = connecting;
        break;
      }
      else {
        // immediate error connecting
        err = SysError::errNo("Cannot connect: ");
      }
    }
    // advance to next address
    addressInfoList = addressInfoList->ai_next;
  }
  if (mState!=connecting) {
    // exhausted addresses without starting to connect
    if (!err) err = TextError::err("No connection could be established");
    LOG(LOG_DEBUG, "Cannot initiate connection to %s:%d - %s", mApiHost.c_str(), mApiPort, err->text());
  }
  else {
    // connection in progress
    // Note: will block in standalone mode until connection succeeds or times out
    handleSocketEvents();
    return;
  }
done:
  // clean up if list processed
  if (addressInfoList) {
    freeaddrinfo(addressInfoList);
  }
  // return status
  nextStep(JsonObjectPtr(), err);
}


static void apiSocketEventCallback(chip::System::SocketEvents aEvents, intptr_t aData)
{
  BridgeApi* api = static_cast<BridgeApi *>((void *)aData);
  int pollev =
    (aEvents.Has(chip::System::SocketEventFlags::kRead) ? POLLIN : 0) |
    (aEvents.Has(chip::System::SocketEventFlags::kWrite) ? POLLOUT : 0) |
    (aEvents.Has(chip::System::SocketEventFlags::kError) ? POLLERR : 0);
  api->handleSocketEvent(pollev);
}


bool BridgeApi::handleSocketEvents()
{
//  // calculate needed event mask
//  int neededevents = POLLIN;
//  if (!mTransmitBuffer.empty()) neededevents |= POLLOUT;
//  setEventWatchingMask(neededevents);
  bool done = false;
  if (mStandalone) {
    while (!done) {
      // poll
      struct pollfd polledFd;
      polledFd.fd = mApiSocketFd;
      polledFd.events = mApiSocketEventMask;
      polledFd.revents = 0; // no event returned so far
      // actual FDs to test. Note: while in Linux timeout<0 means block forever, ONLY exactly -1 means block in macOS!
      int numReadyFDs = poll(&polledFd, (int)1, mTimeout==Infinite ? -1 : (int)(mTimeout/MilliSecond));
      if (numReadyFDs>0) {
        done = handleSocketEvent(polledFd.revents);
      }
      else {
        // timeout
        if (mState==connecting) mState = disconnected;
        else if (mState==waiting) mState = connected;
        nextStep(JsonObjectPtr(), TextError::err("handleSocketEvents: timeout"));
        return true;
      }
    }
  }
  else {
    // CHIP
    // socket watcher should catch it
    // set up timeout
    return true; // no need to call again
  }
  // return done status (caller should call again when not done)
  return done;
}


void BridgeApi::setEventWatchingMask(short aEventMask)
{
  if (!mStandalone) {
    // may be we need to setup socket watching
    if (mApiSocketWatchToken==0) {
      ErrorPtr err = ChipError::err(chip::DeviceLayer::SystemLayerSockets().StartWatchingSocket(mApiSocketFd, &mApiSocketWatchToken));
      if (Error::isOK(err)) {
        err = ChipError::err(chip::DeviceLayer::SystemLayerSockets().SetCallback(mApiSocketWatchToken, apiSocketEventCallback, (intptr_t)this));
        if (Error::isOK(err)) {
          mApiSocketEventMask = 0; // nothing watching yet in chip mode, force re-apply
        }
      }
      if (Error::notOK(err)) {
        LOG(LOG_ERR, "Cannot leave standalone mode: %s", err->text());
        mStandalone = true;
      }
    }
    // watching something
    if ((aEventMask ^ mApiSocketEventMask) & POLLIN) {
      if (aEventMask & POLLIN) chip::DeviceLayer::SystemLayerSockets().RequestCallbackOnPendingRead(mApiSocketWatchToken);
      else chip::DeviceLayer::SystemLayerSockets().ClearCallbackOnPendingRead(mApiSocketWatchToken);
    }
    if ((aEventMask ^ mApiSocketEventMask) & POLLOUT) {
      if (aEventMask & POLLOUT) chip::DeviceLayer::SystemLayerSockets().RequestCallbackOnPendingWrite(mApiSocketWatchToken);
      else chip::DeviceLayer::SystemLayerSockets().ClearCallbackOnPendingWrite(mApiSocketWatchToken);
    }
  }
  mApiSocketEventMask = aEventMask;
}


void BridgeApi::endStandalone()
{
  mStandalone = false;
}



bool BridgeApi::handleSocketEvent(short aEvent)
{
  // socket event found
  if (aEvent & POLLOUT) {
    if (mState==connecting) {
      // we were waiting for connection (signalled by POLLOUT)
      mState = connected;
      setEventWatchingMask(POLLIN);
      nextStep(JsonObjectPtr(), ErrorPtr());
      return true;
    }
    return handleOutgoingData();
  }
  if (aEvent & POLLIN) {
    // data received
    return handleIncomingData();
  }
  if (aEvent & POLLHUP) {
    // connection broken
    disconnect();
    nextStep(JsonObjectPtr(), TextError::err("connection reported HUP"));
    return true;
  }
  return !mNextStepCB; // done when no next step is pending
}


void BridgeApi::nextStep(JsonObjectPtr aJsonMsg, ErrorPtr aError)
{
  if (mNextStepCB) {
    BridgeApiCB cb = mNextStepCB;
    mNextStepCB = NoOP;
    cb(aJsonMsg, aError);
  }
}


bool BridgeApi::handleOutgoingData()
{
  if (mState>=connected && mTransmitBuffer.size()>0) {
    // write as much as possible, nonblocking
    ssize_t res = write(mApiSocketFd, mTransmitBuffer.c_str(), mTransmitBuffer.size());
    if (res>0) {
      // erase sent part
      mTransmitBuffer.erase(0, res);
    }
  }
  return mTransmitBuffer.empty();
}


bool BridgeApi::handleIncomingData()
{
  ErrorPtr err;
  JsonObjectPtr msg;

  // get number of bytes ready for reading
  int numBytes = 0; // must be int!! FIONREAD defines parameter as *int
  ssize_t res = ioctl(mApiSocketFd, FIONREAD, &numBytes);
  if (res<0) {
    err = SysError::errNo("handleIncomingData: FIONREAD: ");
    nextStep(msg, err);
    return true;
  }
  if (numBytes==0) {
    err = TextError::err("connection reported POLLIN+0 bytes");
    disconnect();
    nextStep(msg, err);
    return true;
  }
  else {
    // got some data
    uint8_t *buffer = new uint8_t[numBytes];
    res = read(mApiSocketFd, buffer, numBytes); // read
    if (res<0) {
      if (errno==EWOULDBLOCK)
        return false; // nothing read
      else {
        err = SysError::errNo("handleIncomingData: ");
        nextStep(msg, err);
      }
    }
    else if (res>0) {
      // process incoming data (could be multiple messages!)
      uint8_t *buf = buffer;
      //LOG(LOG_DEBUG, "data received = %.*s", (int)res, (const char*)buffer);
      while (res>0) {
        void* lineend = memchr(buf, '\n', res);
        size_t linebytes = lineend ? (uint8_t *)lineend-buf : res;
        mReceiveBuffer.append((const char *)buf, linebytes);
        //LOG(LOG_DEBUG, "receive buffer = %s", mReceiveBuffer.c_str());
        if (!lineend) break; // not yet at least one full line in the buffer
        // complete message, decode
        buf += linebytes+1; res -= linebytes+1; // reduce to unprocessed rest (+1 for line end)
        // convert to json
        msg = JsonObject::objFromText(mReceiveBuffer.c_str(), mReceiveBuffer.size(), &err, false);
        // receive buffer now parsed, clear
        mReceiveBuffer.clear();
        // process
        //LOG(LOG_DEBUG, "msg = %s", msg->json_c_str());
        JsonObjectPtr o;
        if (msg && msg->get("id", o)) {
          // this IS a method answer...
          if (mState==waiting) {
            mState = connected;
            // ..and we are waiting for one: check ID, but only if we have a callback, otherwise just ignore the msg
            if (!mNextStepCB || o->stringValue()==mNextStepId) {
              // correct answer id, report to callback
              nextStep(msg, err);
            }
            else {
              // wrong answer id, report error callback (if any)
              nextStep(JsonObjectPtr(), TextError::err("method response has wrong id '%s', expected '%s'", o->c_strValue(), mNextStepId.c_str()));
            }
          }
        }
        else if (mIncomingDataCB) {
          // notification, report
          mIncomingDataCB(msg, err);
        }
      } // while lines in the buffer
      // put rest (can be zero) into receive buffer for next message
      mReceiveBuffer.assign((const char *)buf, res);
      return true; // message processed, data handled (or ignored)
    }
  }
  return false;
}


ErrorPtr BridgeApi::sendmsg(JsonObjectPtr aMessage)
{
  bool startSend = mTransmitBuffer.empty();
  mTransmitBuffer.append(aMessage->json_c_str());
  mTransmitBuffer.append("\n");
  if (startSend) {
    if (!handleOutgoingData()) {
      // not sent everything, wait for more events
      setEventWatchingMask(POLLIN|POLLOUT);
      handleSocketEvents();
    }
    else {
      setEventWatchingMask(POLLIN);
    }
  }
  return ErrorPtr();
}


ErrorPtr BridgeApi::notify(const string aNotification, JsonObjectPtr aParams)
{
  if (!aParams) aParams = JsonObject::newObj();
  aParams->add("notification", JsonObject::newString(aNotification));
  return sendmsg(aParams);
}


void BridgeApi::call(const string aMethod, JsonObjectPtr aParams, BridgeApiCB aResponseCB)
{
  mNextStepCB = aResponseCB;
  if (mState!=connected) {
    // can only send when connected but not waiting for response
    nextStep(JsonObjectPtr(), TextError::err("busy: cannot call method now"));
  }
  if (!aParams) aParams = JsonObject::newObj();
  aParams->add("method", JsonObject::newString(aMethod));
  mNextStepId = string_format("%ld", ++mCallCounter);
  aParams->add("id", JsonObject::newString(mNextStepId));
  mState = waiting;
  ErrorPtr err = sendmsg(aParams);
  if (Error::notOK(err)) {
    nextStep(JsonObjectPtr(), err);
    return;
  }
  // need more events
  handleSocketEvents();
}
