/*
 *  Copyright 2016 The ThroughNet Project Authors. All rights reserved.
 *
 *  Ryan Lee (ryan.lee at throughnet.com)
 */

#ifndef __THROUGHNET_DUMMY_SIGNAL_H__
#define __THROUGHNET_DUMMY_SIGNAL_H__

#include <map>
#include <vector>
#include "webrtc/base/sigslot.h"
#include "signalconnection.h"

namespace tn {

class DummySignal
  : public Signal {
public:
  typedef std::vector<DummySignal*> PeerSignal;
  virtual void SignIn();
  virtual bool SendCommand(const Json::Value& jmessage);

private:
  static std::map<std::string, PeerSignal> connections_;

}; // class DummySignal

} // namespace tn


#endif // __THROUGHNET_DUMMY_SIGNAL_H__

