/*
*  Copyright 2016 The ThroughNet Project Authors. All rights reserved.
*
*  Ryan Lee (ryan.lee at throughnet.com)
*/

#ifndef __THROUGHNET_CONTROLOBSERVER_H__
#define __THROUGHNET_CONTROLOBSERVER_H__

namespace tn {

class ControlObserver {
public:
  virtual void OnPeerConnected(const std::string& id) = 0;
  virtual void OnPeerDisconnected(const std::string& id) = 0;
  virtual void OnReady(const std::string& id) = 0;
  virtual void OnPeerMessage(const std::string& id, const char* buffer, const size_t size) = 0;
};

} // namespace tn

#endif // __THROUGHNET_CONTROLOBSERVER_H__
