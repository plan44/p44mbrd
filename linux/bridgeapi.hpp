//
//  bridgeapi.hpp
//  p44mbrd
//
//  Copyright (c) 2022 plan44.ch / Lukas Zeller, Zurich, Switzerland
//

#pragma once

#include <string>

#include "jsonobject.hpp"

#include <platform/CHIPDeviceLayer.h>

using namespace p44;


/// callback for API results
using BridgeApiCB = std::function<void(JsonObjectPtr aJsonMsg, ErrorPtr aError)>;


/// CHIP_ERROR based error
class ChipError : public Error
{
public:
  static const char *domain();
  virtual const char *getErrorDomain() const P44_OVERRIDE;

  /// create system error from passed errno and set message to strerror() text
  /// @param aChipErr a CHIP error number
  ChipError(CHIP_ERROR aChipErr, const char *aContextMessage = NULL);

  /// factory function to create a ErrorPtr either containing NULL (if aChipErr indicates OK)
  /// or a ChipError (if aChipErr indicates error)
  static ErrorPtr err(CHIP_ERROR aChipErr, const char *aContextMessage = NULL);
};



class BridgeApi
{
  int mApiSocketFd; ///< the socket connection to the bridge API
  chip::System::SocketWatchToken mApiSocketWatchToken; ///< the CHIP system sockets layer's token for observing a file descriptor
  int mApiSocketEventMask; ///< events we need to get from the socket
  bool mStandalone; ///< if set, requests are processed in an internal mainloop
  string mApiHost; ///< the hostname to connect to
  uint16_t mApiPort; ///< the port to connect to
  typedef enum {
    disconnected, ///< disconnected
    connecting, ///< connecting but not yet connected
    connected, ///< connected, ready
    waiting, ///< waiting for specific answer
  } ConnectionState;
  ConnectionState mState;
  BridgeApiCB mIncomingDataCB; ///< called when a notification arrives
  BridgeApiCB mNextStepCB; ///< called and cleared when next pending step (connection, response) happens
  string mNextStepId; ///< expected Id of next incoming message
  string mTransmitBuffer; ///< transmit buffer
  string mReceiveBuffer; ///< receive buffer
  MLMicroSeconds mTimeout; ///< timeout for connect and API calls
  long mCallCounter;

public:

  BridgeApi();
  virtual ~BridgeApi();

  /// set connection params
  void setConnectionParams(const string aApiHost, uint16_t aApiPort, MLMicroSeconds aTimeout);

  /// set timeout
  void setTimeout(MLMicroSeconds aTimeout);

  /// connect the API
  void connect(BridgeApiCB aConnectedCB);

  /// disconnect the API
  void disconnect();

  /// API method call
  /// @param aMethod the method name
  /// @param aParams parameters of the call
  /// @param aResponseCB will be called with the result or error
  void call(const string aMethod, JsonObjectPtr aParams, BridgeApiCB aResponseCB);

  /// API notification sending
  /// @param aNotification the notification to send
  ErrorPtr notify(const string aNotification, JsonObjectPtr aParams);

  /// set callback for incoming messages that are not answers to previous calls
  void setIncomingMessageCallback(BridgeApiCB aIncomingDataCB);

  /// send data
  ErrorPtr sendmsg(JsonObjectPtr aMessage);

  /// handle socket events. In standalone mode, socket will be polled and function
  /// does not return until expected event or timeout has happened.
  /// In chip mode, a layer socket event callback is registered to asynchronously handle events
  /// and this function is mostly NOP and returns immediately.
  bool handleSocketEvents();

  /// switch to using CHIP layer socket events
  /// @note should be called when CHIP is up and running
  void endStandalone();

  /// handle socket event
  /// @param aEvent the socket event to process (POLLxxx constants)
  /// @return true when all needed events for the current operation have been received
  bool handleSocketEvent(short aEvent);

private:

  /// @param aEventMask the events to watch for
  void setEventWatchingMask(short aEventMask);

  /// handle incoming data
  /// @return true when all needed events for the current operation have been received
  bool handleIncomingData();

  /// handle outgoing data
  /// @return true when all buffered data has been sent
  bool handleOutgoingData();

  void nextStep(JsonObjectPtr aJsonMsg, ErrorPtr aError);

};
