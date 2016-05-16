/*
*  Copyright 2016 The ThroughNet Project Authors. All rights reserved.
*
*  Ryan Lee (ryan.lee at throughnet.com)
*/

#include "control.h"
#include "peer.h"

#include "webrtc/base/json.h"
#include "webrtc/base/signalthread.h"



namespace tn {

Control::Control(ControlObserver* observer, const std::string id)
       : Control(observer, id, nullptr){
}

Control::Control(ControlObserver* observer, const std::string id, std::shared_ptr<Signal> signal)
       : observer_(observer), id_(id),
         signal_(signal) {

  signal_->SignalOnCommandReceived_.connect(this, &Control::OnSignalCommandReceived);
}

Control::~Control() {
  peers_.clear();
}


//
// Initialization and release
//

bool Control::InitializeControl() {

  ASSERT(peer_connection_factory_.get() == NULL);

  webrtc::MediaConstraintsInterface* constraints = NULL;

  if (!CreatePeerFactory(constraints)) {
    LOG(LS_ERROR) << "CreatePeerFactory failed";
    DeleteControl();
    return false;
  }

  signaling_thread_ = rtc::Thread::Current();

  return true;
}

void Control::DeleteControl() {
  peer_connection_factory_ = NULL;
  fake_audio_capture_module_ = NULL;
}



//
// Send data to peer or emit data to channel
//

void Control::Send(const std::string to, const char* buffer, const size_t size) {

  typedef std::map<std::string, rtc::scoped_refptr<PeerControl>>::iterator it_type;

  it_type it = peers_.find(to);
  if (it == peers_.end()) return;

  it->second->Send(buffer, size);
  return;
}


//
// Send command to other peer by signal server
//

bool Control::SendCommand(const std::string& id, const std::string& command, const Json::Value& data) {
  signal_->SendCommand(id, command, data);
  return true;
}


//
// Signal connected
//

void Control::OnConnected(const std::string id) {
  if (observer_ == nullptr) return;
  observer_->OnPeerConnected(id);
}

//
// Signal disconnected
//

void Control::OnDisconnected(const std::string id) {
  if (observer_ == nullptr) return;
  observer_->OnPeerDisconnected(id);
}

//
// Signal receiving data
//

void Control::OnPeerMessage(const std::string& id, const char* buffer, const size_t size) {
  if (observer_ == nullptr) return;
  observer_->OnPeerMessage(id, buffer, size);
}

//
// Thread message queue
//

void Control::OnMessage(rtc::Message* msg) {
  switch (msg->message_id) {
  case MSG_COMMAND_RECEIVED:
    ControlMessageData* param =
      static_cast<ControlMessageData*>(msg->pdata);
    OnCommandReceived(param->data_);
    delete param;
    break;
  }

  return;
}

//
// Signin to signal server
//

void Control::SignIn() {
  if (signal_.get() == NULL) {
    LOG(LS_ERROR) << "SignIn failed, no signal server";
    return;
  }

  signal_->SignIn();
  return;
}

void Control::Join(const std::string id) {
  if (signal_.get() == NULL) {
    LOG(LS_ERROR) << "Join failed, no signal server";
    return;
  }

  signal_->JoinChannel(id);
  return;
}

//
// Dispatch command from signal server
//

void Control::OnCommandReceived(const Json::Value& message) {

  Json::Value data;
  std::string command;
  std::string peer_id;

  if (!rtc::GetStringFromJsonObject(message, "command", &command) ||
      !rtc::GetValueFromJsonObject(message, "data", &data)) {

    LOG(LS_ERROR) << "Invalid message:" << message;
    return;
  }

  if (!rtc::GetStringFromJsonObject(message, "peer_id", &peer_id)) {
    peer_id.clear();
  }

  if (command == "signedin") {
    OnSignedIn(data);
  }
  else if (command == "created") {
    OnCreated(data);
  }
  else if (command == "joined") {
    OnJoined(data);
  }
  else if (command == "leaved") {
    OnLeaved(data);
  }
  else if (command == "createoffer") {
    CreateOffer(data);
  }
  else if (command == "offersdp") {
    ReceiveOfferSdp(peer_id, data);
  }
  else if (command == "answersdp") {
    ReceiveAnswerSdp(peer_id, data);
  }
  else if (command == "ice_candidate") {
    AddIceCandidate(peer_id, data);
  }
}

void Control::OnSignalCommandReceived(const Json::Value& message) {
  ControlMessageData *data = new ControlMessageData(message);
  signaling_thread_->Post(this, MSG_COMMAND_RECEIVED, data);
}


//
// Create peer creation factory
//

bool Control::CreatePeerFactory(
  const webrtc::MediaConstraintsInterface* constraints) {

  fake_audio_capture_module_ = FakeAudioCaptureModule::Create();
  if (fake_audio_capture_module_ == NULL) {
    return false;
  }

  peer_connection_factory_ = webrtc::CreatePeerConnectionFactory(
    rtc::Thread::Current(), rtc::Thread::Current(),
    fake_audio_capture_module_, NULL, NULL);

  if (!peer_connection_factory_.get()) {
    return false;
  }

  return true;
}


//
// Add ice candidate to local peer from remote peer
//

void Control::AddIceCandidate(const std::string& peer_id, const Json::Value& data) {

  std::string sdp_mid;
  int sdp_mline_index;
  std::string candidate;

  if (!rtc::GetStringFromJsonObject(data, "sdp_mid", &sdp_mid)) return;
  if (!rtc::GetIntFromJsonObject(data, "sdp_mline_index", &sdp_mline_index)) return;
  if (!rtc::GetStringFromJsonObject(data, "candidate", &candidate)) return;

  if (peers_.find(peer_id) == peers_.end()) return;
  peers_[peer_id]->AddIceCandidate(sdp_mid, sdp_mline_index, candidate);
}



//
// 'signin' command
//

void Control::OnSignedIn(const Json::Value& data) {
  bool result;
  if (!rtc::GetBoolFromJsonObject(data, "result", &result)) {
    LOG(LS_WARNING) << "Unknown signin response";
    return;
  }

  if (!result) {
    LOG(LS_WARNING) << "Signin failed";
    return;
  }

  std::string session_id;
  if (!rtc::GetStringFromJsonObject(data, "session_id", &session_id)) {
    LOG(LS_WARNING) << "Signin failed - no session_id";
    return;
  }

  session_id_ = session_id;
  signal_->CreateChannel(id_);
}

//
// 'create' command
//

void Control::OnCreated(const Json::Value& data) {
  bool result;
  if (!rtc::GetBoolFromJsonObject(data, "result", &result)) {
    LOG(LS_WARNING) << "Unknown signin response";
    return;
  }

  if (!result) {
    LOG(LS_WARNING) << "Create channel failed";
    return;
  }

  std::string channel;
  if (!rtc::GetStringFromJsonObject(data, "name", &channel)) {
    LOG(LS_WARNING) << "Create channel failed - no channel name";
    return;
  }

  observer_->OnReady(channel);
}

//
// 'join' command
//

void Control::OnJoined(const Json::Value& data) {

}


//
// 'leave' command
//

void Control::OnLeaved(const Json::Value& data) {

  std::string channel;
  if (!rtc::GetStringFromJsonObject(data, "name", &channel)) {
    LOG(LS_WARNING) << "OnLeave failed - no name";
    return;
  }

  for (auto const &peer : peers_) {
    if (peer.second->remote_id() == channel) {
      peer.second->Close();
    }

    observer_->OnPeerDisconnected(channel);
  }
}


//
// 'createoffer' command
//

void Control::CreateOffer(const Json::Value& data) {

  Json::Value peers;
  if (!rtc::GetValueFromJsonObject(data, "peers", &peers)) {
    LOG(LS_WARNING) << "createoffer failed - no peers value";
    return;
  }

  for (size_t i = 0; i < peers.size(); ++i) {
    std::string remote_id;
    if (!rtc::GetStringFromJsonArray(peers, i, &remote_id)) {
      LOG(LS_WARNING) << "Peer handshake failed - invalid peer id";
      return;
    }

    Peer peer = new rtc::RefCountedObject<PeerControl>(id_, remote_id, this, peer_connection_factory_);
    peers_.insert(std::pair<std::string, Peer>(remote_id, peer));

    peer->CreateOffer(NULL);
  }
}

//
// 'offersdp' command
//

void Control::ReceiveOfferSdp(const std::string& peer_id, const Json::Value& data) {
  std::string sdp;

  if (!rtc::GetStringFromJsonObject(data, "sdp", &sdp)) return;

  Peer peer = new rtc::RefCountedObject<PeerControl>(id_, peer_id, this, peer_connection_factory_);
  peers_.insert(std::pair<std::string, Peer>(peer_id, peer));

  peer->ReceiveOfferSdp(sdp);
}


//
// 'answersdp' command
//

void Control::ReceiveAnswerSdp(const std::string& peer_id, const Json::Value& data) {
  std::string sdp;

  if (!rtc::GetStringFromJsonObject(data, "sdp", &sdp)) return;
  if (peers_.find(peer_id) == peers_.end()) return;

  peers_[peer_id]->ReceiveAnswerSdp(sdp);
}


} // namespace tn
