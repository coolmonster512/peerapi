/*
*  Copyright 2016 The PeerConnect Project Authors. All rights reserved.
*
*  Ryan Lee
*/

#ifndef __PEERCONNECT_PEER_H__
#define __PEERCONNECT_PEER_H__

#include <condition_variable>
#include <mutex>
#include <memory>
#include "webrtc/api/datachannelinterface.h"
#include "webrtc/api/peerconnectioninterface.h"
#include "webrtc/base/scoped_ref_ptr.h"
#include "webrtc/api/jsep.h"
#include "webrtc/base/json.h"

namespace pc {

//
// class PeerObserver
//

class PeerObserver {
public:
  virtual void SendCommand(const std::string& id, const std::string& command, const Json::Value& data) = 0;
  virtual void Close(const std::string id) = 0;
  virtual void OnConnected(const std::string id) = 0;
  virtual void OnClosed(const std::string id) = 0;
  virtual void OnMessage(const std::string& id, const char* buffer, const size_t size) = 0;
  virtual void OnWritable(const std::string& id) = 0;
  virtual void OnError( const std::string id, const std::string& reason ) = 0;

};

class PeerDataChannelObserver;


//
// class PeerControl
//

class PeerControl
      : public webrtc::CreateSessionDescriptionObserver,
        public webrtc::PeerConnectionObserver,
        public sigslot::has_slots<> {

public:

  using string = std::string;

  explicit PeerControl(const string local_session_id,
                       const string remote_session_id,
                       PeerObserver* observer,
                       rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>
                           peer_connection_factory);

  ~PeerControl();

  enum PeerState {
    pConnecting,
    pOpen,
    pClosing,
    pClosed
  };

  const string& local_id() const { return local_id_; }
  const string& remote_id() const { return remote_id_; }
  const PeerState state() const { return state_ ; }

  //
  // APIs
  //

  bool Initialize();
  bool Send(const char* buffer, const size_t size);
  bool SyncSend(const char* buffer, const size_t size);
  bool IsWritable();
  void Close();

  //
  // PeerConnection
  //

  void CreateOffer(const webrtc::MediaConstraintsInterface* constraints);
  void CreateAnswer(const webrtc::MediaConstraintsInterface* constraints);
  void AddIceCandidate(const string& sdp_mid, int sdp_mline_index,
                       const string& candidate);
  void ReceiveOfferSdp(const string& sdp);
  void ReceiveAnswerSdp(const string& sdp);

  //
  // PeerConnectionObserver implementation.
  //

  void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState new_state) override;
  void OnAddStream(webrtc::MediaStreamInterface* stream) override {};
  void OnRemoveStream(webrtc::MediaStreamInterface* stream) override {}
  void OnDataChannel(webrtc::DataChannelInterface* channel) override;
  void OnRenegotiationNeeded() override {}
  void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state) override; 
  void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState new_state) override {}
  void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override;
  void OnIceConnectionReceivingChange(bool receiving) override {}

  //
  // Implements CreateSessionDescriptionObserver.
  //

  void OnSuccess(webrtc::SessionDescriptionInterface* desc) override;
  void OnFailure(const string& error) override  {}

  //
  // PeerDataChannelObserver
  //

  void OnPeerOpened();
  void OnPeerClosed();
  void OnPeerDisconnected();
  void OnPeerMessage(const webrtc::DataBuffer& buffer);
  void OnBufferedAmountChange(const uint64_t previous_amount);

protected:

  bool CreatePeerConnection();
  void DeletePeerConnection();
  bool CreateDataChannel(const string& label,
                         const webrtc::DataChannelInit& init);
  void SetLocalDescription(const string& type, const string& sdp);
  void SetRemoteDescription(const string& type, const string& sdp);
  void Attach(PeerDataChannelObserver* datachannel);
  void Detach(PeerDataChannelObserver* datachannel);

  rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection_;
  rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> peer_connection_factory_;

  string local_id_;
  string remote_id_;
  std::unique_ptr<PeerDataChannelObserver> local_data_channel_;
  std::unique_ptr<PeerDataChannelObserver> remote_data_channel_;

  PeerState state_;

  PeerObserver* control_;

};


//
// class PeerDataChannelObserver
//

class PeerDataChannelObserver : public webrtc::DataChannelObserver {
public:
  explicit PeerDataChannelObserver(webrtc::DataChannelInterface* channel);
  virtual ~PeerDataChannelObserver();

  void OnStateChange() override;
  void OnMessage(const webrtc::DataBuffer& buffer) override;
  void OnBufferedAmountChange(uint64_t previous_amount) override;

  bool Send(const char* buffer, const size_t size);
  bool SyncSend(const char* buffer, const size_t size);
  void Close();
  bool IsOpen() const;
  uint64_t BufferedAmount();
  bool IsWritable();
  const webrtc::DataChannelInterface::DataState state() const;

  // sigslots
  sigslot::signal0<> SignalOnOpen_;
  sigslot::signal0<> SignalOnDisconnected_;
  sigslot::signal1<const webrtc::DataBuffer&> SignalOnMessage_;
  sigslot::signal1<const uint64_t> SignalOnBufferedAmountChange_;

protected:

private:

  const size_t max_buffer_size_ = 16 * 1024 * 1024;

  rtc::scoped_refptr<webrtc::DataChannelInterface> channel_;
  webrtc::DataChannelInterface::DataState state_;
  std::condition_variable send_cv_;
  std::mutex send_lock_;
};

} // namespace pc

#endif // __PEERCONNECT_PEER_H__