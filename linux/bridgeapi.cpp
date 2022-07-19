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


BridgeApi::BridgeApi() :
  mApiSocketFd(-1),
  mStandalone(true),
  state(disconnected)
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
  if (state!=disconnected) {
    state = disconnected;
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
      // - initiate connection
      LOG(LOG_DEBUG, "- Attempting connection with address family = %d, protocol = %d, addrlen=%d/sizeof=%zu", addressInfoList->ai_family, addressInfoList->ai_protocol, addressInfoList->ai_addrlen, sizeof(*(addressInfoList->ai_addr)));
      res = ::connect(mApiSocketFd, addressInfoList->ai_addr, addressInfoList->ai_addrlen);
      if (res==0 || errno==EINPROGRESS) {
        // connection initiated (or already open, but connectionMonitorHandler will take care in both cases)
        state = connecting;
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
  if (state!=connecting) {
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


bool BridgeApi::handleSocketEvents()
{
  bool done = false;
  if (mStandalone) {
    while (!done) {
      // poll
      struct pollfd polledFd;
      polledFd.fd = mApiSocketFd;
      polledFd.events = POLLIN|POLLHUP;
      if (state==connecting || !mTransmitBuffer.empty()) polledFd.events |= POLLOUT;
      polledFd.revents = 0; // no event returned so far
      // actual FDs to test. Note: while in Linux timeout<0 means block forever, ONLY exactly -1 means block in macOS!
      int numReadyFDs = poll(&polledFd, (int)1, mTimeout==Infinite ? -1 : (int)(mTimeout/MilliSecond));
      if (numReadyFDs>0) {
        done = handleSocketEvent(polledFd.revents);
      }
      else {
        // timeout
        if (state==connecting) state = disconnected;
        else if (state==waiting) state = connected;
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


bool BridgeApi::handleSocketEvent(short aEvent)
{
  // socket event found
  if (aEvent & POLLOUT) {
    if (state==connecting) {
      // we were waiting for connection (signalled by POLLOUT)
      state = connected;
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
  if (state>=connected && mTransmitBuffer.size()>0) {
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
    nextStep(msg, err);
    return true;
  }
  if (numBytes>0) {
    uint8_t *buf = new uint8_t[numBytes];
    res = read(mApiSocketFd, buf, numBytes); // read
    if (res<0) {
      if (errno==EWOULDBLOCK)
        return false; // nothing read
      else {
        err = SysError::errNo("handleIncomingData: ");
        nextStep(msg, err);
      }
    }
    else if (res>0) {
      // process incoming data
      void* lineend = memchr(buf, '\n', res);
      size_t linebytes = lineend ? (uint8_t *)lineend-buf : res;
      mReceiveBuffer.append((const char *)buf, linebytes);
      if (lineend) {
        // complete message, decode
        msg = JsonObject::objFromText(mReceiveBuffer.c_str(), mReceiveBuffer.size(), &err, false);
        // put rest into receive buffer for next message
        if (lineend) {
          mReceiveBuffer.assign((const char *)buf+linebytes+1, res-linebytes-1);
        }
        else {
          mReceiveBuffer.clear();
        }
        // report it
        if (state==waiting) {
          state = connected;
          nextStep(msg, err);
          return true;
        }
        else if (mIncomingDataCB) {
          mIncomingDataCB(msg, err);
          return true;
        }
      }
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
      handleSocketEvents();
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
  if (state!=connected) {
    // can only send when connected but not waiting for response
    nextStep(JsonObjectPtr(), TextError::err("busy: cannot call method now"));
  }
  if (!aParams) aParams = JsonObject::newObj();
  aParams->add("method", JsonObject::newString(aMethod));
  state = waiting;
  ErrorPtr err = sendmsg(aParams);
  if (Error::notOK(err)) {
    nextStep(JsonObjectPtr(), err);
    return;
  }
  // need more events
  handleSocketEvents();
}
