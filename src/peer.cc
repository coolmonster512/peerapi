/*
*  Copyright 2016 The PeerConnect Project Authors. All rights reserved.
*
*  Ryan Lee
*/

#include "control.h"
#include "peer.h"
#include "webrtc/api/test/fakeconstraints.h"
#include "webrtc/api/test/mockpeerconnectionobservers.h"


namespace pc {

//
// class PeerControl
//

PeerControl::PeerControl(const std::string local_id,
                         const std::string remote_id,
                         PeerObserver* observer,
                         rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>
                             peer_connection_factory)
    : local_id_(local_id),
      remote_id_(remote_id),
      observer_(observer),
      peer_connection_factory_(peer_connection_factory){

  if (!CreatePeerConnection()) {
    LOG(LS_ERROR) << "CreatePeerConnection failed";
    DeletePeerConnection();
  }

  webrtc::DataChannelInit init;
  const std::string data_channel_name = std::string("pc_data_") + remote_id_;
  if (!CreateDataChannel(data_channel_name, init)) {
    LOG(LS_ERROR) << "CreateDataChannel failed";
    DeletePeerConnection();
  }
}

PeerControl::~PeerControl() {
  DeletePeerConnection();
  LOG(LS_INFO) << "PeerControl has been deleted";
}


bool PeerControl::Send(const char* buffer, const size_t size) {
  return local_data_channel_->Send(buffer, size);
}

bool PeerControl::SyncSend(const char* buffer, const size_t size) {
  return local_data_channel_->SyncSend(buffer, size);
}

bool PeerControl::IsWritable() {
  return local_data_channel_->IsWritable();
}

void PeerControl::Close() {
  local_data_channel_->Close();
  remote_data_channel_->Close();
}


void PeerControl::CreateOffer(const webrtc::MediaConstraintsInterface* constraints) {
  peer_connection_->CreateOffer(this, constraints);
}


void PeerControl::CreateAnswer(
  const webrtc::MediaConstraintsInterface* constraints) {
  peer_connection_->CreateAnswer(this, constraints);
}


void PeerControl::ReceiveOfferSdp(const std::string& sdp) {
  SetRemoteDescription(webrtc::SessionDescriptionInterface::kOffer, sdp);
  CreateAnswer(NULL);
}


void PeerControl::ReceiveAnswerSdp(const std::string& sdp) {
  SetRemoteDescription(webrtc::SessionDescriptionInterface::kAnswer, sdp);
}

void PeerControl::ClosePeerConnection() {
  peer_connection_->Close();
}

void PeerControl::OnDataChannel(webrtc::DataChannelInterface* data_channel) {
  PeerDataChannelObserver* Observer = new PeerDataChannelObserver(data_channel);
  remote_data_channel_ = std::unique_ptr<PeerDataChannelObserver>(Observer);
  Attach(remote_data_channel_.get());
}

void PeerControl::OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState new_state) {
  // NOTHING
}

void PeerControl::OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state) {
  switch (new_state) {
  case webrtc::PeerConnectionInterface::IceConnectionState::kIceConnectionClosed:
    //
    // Ice connection has been closed.
    // Notify it to Control so the Control will remove peer in peers_
    //
    observer_->QueueOnPeerDisconnected(remote_id_);
    break;
  case webrtc::PeerConnectionInterface::IceConnectionState::kIceConnectionDisconnected:
    //
    // Peer disconnected unexpectedly before close()
    // Queue disconnection requeue to Control.
    //  - Leave channel in signal server
    //  - Close peer data channel and ice connecition
    //
    observer_->QueuePeerDisconnect(remote_id_);
    break;
  default:
    break;
  }
}



void PeerControl::OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {
  std::string sdp;
  if (!candidate->ToString(&sdp)) return;

  Json::Value data;

  data["sdp_mid"] = candidate->sdp_mid();
  data["sdp_mline_index"] = candidate->sdp_mline_index();
  data["candidate"] = sdp;

  observer_->SendCommand(remote_id_, "ice_candidate", data);
}

void PeerControl::OnSuccess(webrtc::SessionDescriptionInterface* desc) {

  // This callback should take the ownership of |desc|.
  std::unique_ptr<webrtc::SessionDescriptionInterface> owned_desc(desc);
  std::string sdp;

  if (!desc->ToString(&sdp)) return;

  // Set local description
  SetLocalDescription(desc->type(), sdp);

  //
  // Send message to other peer
  Json::Value data;

  if (desc->type() == webrtc::SessionDescriptionInterface::kOffer) {
    data["sdp"] = sdp;
    observer_->SendCommand(remote_id_, "offersdp", data);
  }
  else if (desc->type() == webrtc::SessionDescriptionInterface::kAnswer) {
    data["sdp"] = sdp;
    observer_->SendCommand(remote_id_, "answersdp", data);
  }
}

void PeerControl::OnPeerOpened() {

  // Both local_data_channel_ and remote_data_channel_ has been opened
  if (local_data_channel_.get() != nullptr && remote_data_channel_.get() != nullptr &&
      local_data_channel_->state() == webrtc::DataChannelInterface::DataState::kOpen &&
      remote_data_channel_->state() == webrtc::DataChannelInterface::DataState::kOpen
    ) {
    observer_->OnPeerConnected(remote_id_);
    observer_->OnPeerWritable(local_id_);
  }
}

void PeerControl::OnPeerClosed() {

  if (local_data_channel_.get() == nullptr && remote_data_channel_.get() == nullptr) {
    return;
  }

  // Both local_data_channel_ and remote_data_channel_ has been closed
  if (local_data_channel_->state() == webrtc::DataChannelInterface::DataState::kClosed &&
      remote_data_channel_->state() == webrtc::DataChannelInterface::DataState::kClosed) {

    // Close local peerconnection
    observer_->QueueOnPeerChannelClosed(remote_id_, 1000);

  }
}


void PeerControl::OnPeerMessage(const webrtc::DataBuffer& buffer) {
  std::string data;
  observer_->OnPeerMessage(remote_id_, buffer.data.data<char>(), buffer.data.size());
}

void PeerControl::OnBufferedAmountChange(const uint64_t previous_amount) {
  if (!local_data_channel_->IsWritable()) return;
  observer_->OnPeerWritable(remote_id_);
}


bool PeerControl::CreateDataChannel(
                    const std::string& label,
                    const webrtc::DataChannelInit& init) {

  rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel;

  data_channel = peer_connection_->CreateDataChannel(label, &init);
  if (data_channel.get() == NULL) {
    return false;
  }

  local_data_channel_.reset(new PeerDataChannelObserver(data_channel));
  if (local_data_channel_.get() == NULL) {
    return false;
  }

  Attach(local_data_channel_.get());
  return true;
}

void PeerControl::AddIceCandidate(const std::string& sdp_mid, int sdp_mline_index,
                                  const std::string& candidate) {

  std::unique_ptr<webrtc::IceCandidateInterface> owned_candidate(
    webrtc::CreateIceCandidate(sdp_mid, sdp_mline_index, candidate, NULL));

  peer_connection_->AddIceCandidate(owned_candidate.get());
}


bool PeerControl::CreatePeerConnection() {
  ASSERT(peer_connection_factory_.get() != NULL);
  ASSERT(peer_connection_.get() == NULL);

  // Enable DTLS
  webrtc::FakeConstraints constraints;
  constraints.AddOptional(webrtc::MediaConstraintsInterface::kEnableDtlsSrtp, "true");

  // CreatePeerConnection with RTCConfiguration.
  webrtc::PeerConnectionInterface::RTCConfiguration config;
  webrtc::PeerConnectionInterface::IceServer ice_server;
  ice_server.uri = "stun:stun.l.google.com:19302";
  config.servers.push_back(ice_server);

  peer_connection_ = peer_connection_factory_->CreatePeerConnection(
    config, &constraints, NULL, NULL, this);

  return peer_connection_.get() != NULL;
}

void PeerControl::DeletePeerConnection() {
  Detach(remote_data_channel_.get());
  Detach(local_data_channel_.get());

  remote_data_channel_ = NULL;
  local_data_channel_ = NULL;
  peer_connection_ = NULL;
  peer_connection_factory_ = NULL;
}

void PeerControl::SetLocalDescription(const std::string& type,
                                              const std::string& sdp) {

  rtc::scoped_refptr<webrtc::MockSetSessionDescriptionObserver>
    observer(new rtc::RefCountedObject<
      webrtc::MockSetSessionDescriptionObserver>());
  peer_connection_->SetLocalDescription(
    observer, webrtc::CreateSessionDescription(type, sdp, NULL));
}

void PeerControl::SetRemoteDescription(const std::string& type,
                                               const std::string& sdp) {

  rtc::scoped_refptr<webrtc::MockSetSessionDescriptionObserver>
    observer(new rtc::RefCountedObject<
      webrtc::MockSetSessionDescriptionObserver>());
  peer_connection_->SetRemoteDescription(
    observer, webrtc::CreateSessionDescription(type, sdp, NULL));
}

void PeerControl::Attach(PeerDataChannelObserver* datachannel) {
  datachannel->SignalOnOpen_.connect(this, &PeerControl::OnPeerOpened);
  datachannel->SignalOnClosed_.connect(this, &PeerControl::OnPeerClosed);
  datachannel->SignalOnMessage_.connect(this, &PeerControl::OnPeerMessage);
  datachannel->SignalOnBufferedAmountChange_.connect(this, &PeerControl::OnBufferedAmountChange);
}

void PeerControl::Detach(PeerDataChannelObserver* datachannel) {
  datachannel->SignalOnOpen_.disconnect(this);
  datachannel->SignalOnClosed_.disconnect(this);
  datachannel->SignalOnMessage_.disconnect(this);
  datachannel->SignalOnBufferedAmountChange_.disconnect(this);
}



//
// class PeerDataChannelObserver
//

PeerDataChannelObserver::PeerDataChannelObserver(webrtc::DataChannelInterface* channel)
  : channel_(channel) {
  channel_->RegisterObserver(this);
  state_ = channel_->state();
}

PeerDataChannelObserver::~PeerDataChannelObserver() {
  channel_->Close();
  state_ = channel_->state();
  channel_->UnregisterObserver();
}

void PeerDataChannelObserver::OnBufferedAmountChange(uint64_t previous_amount) {
  SignalOnBufferedAmountChange_(previous_amount);

  if (channel_->buffered_amount() == 0) {
    std::lock_guard<std::mutex> lk(send_lock_);
    send_cv_.notify_all();
  }

  return;
}

void PeerDataChannelObserver::OnStateChange() {
  state_ = channel_->state();
  if (state_ == webrtc::DataChannelInterface::DataState::kOpen) {
    SignalOnOpen_();
  }
  else if (state_ == webrtc::DataChannelInterface::DataState::kClosed) {
    SignalOnClosed_();
  }
}

void PeerDataChannelObserver::OnMessage(const webrtc::DataBuffer& buffer) {
  SignalOnMessage_(buffer);
}

bool PeerDataChannelObserver::Send(const char* buffer, const size_t size) {
  rtc::CopyOnWriteBuffer rtcbuffer(buffer, size);
  webrtc::DataBuffer databuffer(rtcbuffer, true);

  if (channel_->buffered_amount() >= max_buffer_size_) return false;
  return channel_->Send(databuffer);
}

bool PeerDataChannelObserver::SyncSend(const char* buffer, const size_t size) {
  rtc::CopyOnWriteBuffer rtcbuffer(buffer, size);
  webrtc::DataBuffer databuffer(rtcbuffer, true);

  std::unique_lock<std::mutex> lock(send_lock_);
  if (!channel_->Send(databuffer)) return false;

  if (!send_cv_.wait_for(lock, std::chrono::milliseconds(60*1000),
                         [this] () { return channel_->buffered_amount() == 0; })) {
    return false;
  }

  return true;
}

void PeerDataChannelObserver::Close() {
  LOG(LS_WARNING) << "Close data channel";
  if (channel_->state() != webrtc::DataChannelInterface::kClosing) {
    channel_->Close();
  }
}


bool PeerDataChannelObserver::IsOpen() const {
  return state_ == webrtc::DataChannelInterface::kOpen;
}

uint64_t PeerDataChannelObserver::BufferedAmount() {
  return channel_->buffered_amount();
}

bool PeerDataChannelObserver::IsWritable() {
  if (channel_ == nullptr) return false;
  if (!IsOpen() || channel_->buffered_amount() > 0) return false;
  return true;
}


const webrtc::DataChannelInterface::DataState
PeerDataChannelObserver::state() const {
  return channel_->state();
}


} // namespace pc