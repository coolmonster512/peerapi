/*
*  Copyright 2016 The ThroughNet Project Authors. All rights reserved.
*
*  Ryan Lee (ryan.lee at throughnet.com)
*/

#ifndef __THROUGHNET_PEER_H__
#define __THROUGHNET_PEER_H__

#include "webrtc/api/datachannelinterface.h"
#include "webrtc/api/peerconnectioninterface.h"
#include "webrtc/base/scoped_ref_ptr.h"
#include "webrtc/api/jsep.h"
#include "webrtc/base/json.h"

namespace tn {

//
// class PeerObserver
//

class PeerObserver {
public:
  virtual bool SendCommand(const std::string& command, const Json::Value& data, const std::string& peer_sid) = 0;
  virtual void OnConnected(const std::string peer_id) = 0;
  virtual void OnData(const std::string& peer_id, const char* buffer, const size_t size) = 0;
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
  explicit PeerControl(const std::string local_session_id,
                       const std::string remote_session_id,
                       PeerObserver* observer,
                       rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>
                           peer_connection_factory);

  ~PeerControl();

  const std::string& local_session_id() const { return local_session_id_; }
  const std::string& remote_session_id() const { return remote_session_id_; }

  bool Send(const std::string& message);


  //
  // PeerConnection
  //

  void CreateOffer(const webrtc::MediaConstraintsInterface* constraints);
  void CreateAnswer(const webrtc::MediaConstraintsInterface* constraints);

  void AddIceCandidate(const std::string& sdp_mid, int sdp_mline_index,
                       const std::string& candidate);
  void ReceiveOfferSdp(const std::string& sdp);
  void ReceiveAnswerSdp(const std::string& sdp);

  //
  // PeerConnectionObserver implementation.
  //

  void OnSignalingChange(
         webrtc::PeerConnectionInterface::SignalingState new_state) override {};
  void OnAddStream(webrtc::MediaStreamInterface* stream) override {};
  void OnRemoveStream(webrtc::MediaStreamInterface* stream) override {};
  void OnDataChannel(webrtc::DataChannelInterface* channel) override;
  void OnRenegotiationNeeded() override {}
  void OnIceConnectionChange(
          webrtc::PeerConnectionInterface::IceConnectionState new_state) override {};
  void OnIceGatheringChange(
          webrtc::PeerConnectionInterface::IceGatheringState new_state) override {};
  void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override;
  void OnIceConnectionReceivingChange(bool receiving) override {}

  //
  // Implements CreateSessionDescriptionObserver.
  //

  void OnSuccess(webrtc::SessionDescriptionInterface* desc);
  void OnFailure(const std::string& error) {}

  //
  // PeerDataChannelObserver
  //

  void OnPeerOpened();
  void OnPeerMessage(const webrtc::DataBuffer& buffer);


protected:

  bool CreatePeerConnection(const webrtc::MediaConstraintsInterface* constraints);
  void DeletePeerConnection();
  bool CreateDataChannel(const std::string& label,
                         const webrtc::DataChannelInit& init);
  void SetLocalDescription(const std::string& type, const std::string& sdp);
  void SetRemoteDescription(const std::string& type, const std::string& sdp);
  void SigslotConnect(PeerDataChannelObserver* datachanel);

  rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection_;
  rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> peer_connection_factory_;

  std::string local_session_id_;
  std::string remote_session_id_;
  rtc::scoped_ptr<PeerDataChannelObserver> local_data_channel_;
  rtc::scoped_ptr<PeerDataChannelObserver> remote_data_channel_;

  PeerObserver* observer_;

};


//
// class PeerDataChannelObserver
//

class PeerDataChannelObserver : public webrtc::DataChannelObserver {
public:
  explicit PeerDataChannelObserver(webrtc::DataChannelInterface* channel);
  virtual ~PeerDataChannelObserver();

  void OnBufferedAmountChange(uint64_t previous_amount) override;

  void OnStateChange() override;
  void OnMessage(const webrtc::DataBuffer& buffer) override;

  bool Send(const std::string& message);
  void Close();
  bool IsOpen() const;
  const webrtc::DataChannelInterface::DataState state() const;
  size_t received_message_count() const;

  // sigslots
  sigslot::signal0<> SignalOnOpen_;
  sigslot::signal1<const webrtc::DataBuffer&> SignalOnMessage_;

protected:

private:
  rtc::scoped_refptr<webrtc::DataChannelInterface> channel_;
  webrtc::DataChannelInterface::DataState state_;
  size_t received_message_count_;
};

} // namespace tn

#endif // __THROUGHNET_PEER_H__