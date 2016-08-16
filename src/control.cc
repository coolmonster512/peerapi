/*
*  Copyright 2016 The PeerConnect Project Authors. All rights reserved.
*
*  Ryan Lee
*/

#include "control.h"
#include "peer.h"

#include "webrtc/base/json.h"
#include "webrtc/base/signalthread.h"

#include "logging.h"

#ifdef WEBRTC_POSIX
#include "webrtc/base/messagehandler.h"
#include "webrtc/base/messagequeue.h"

namespace rtc {

  MessageHandler::~MessageHandler() {
    MessageQueueManager::Clear(this);
  }

} // namespace rtc
#endif // WEBRTC_POSIX

namespace pc {

Control::Control()
       : Control(nullptr){
}

Control::Control(std::shared_ptr<Signal> signal)
       : signal_(signal) {

  signal_->SignalOnCommandReceived_.connect(this, &Control::OnSignalCommandReceived);
  signal_->SignalOnClosed_.connect(this, &Control::OnSignalConnectionClosed);
}

Control::~Control() {
  peers_.clear();
  DeleteControl();
  signal_->SignalOnCommandReceived_.disconnect(this);
  signal_->SignalOnClosed_.disconnect(this);
}


//
// Initialization and release
//

bool Control::InitializeControl() {

  ASSERT(peer_connection_factory_.get() == NULL);

  webrtc::MediaConstraintsInterface* constraints = NULL;

  if (!CreatePeerFactory(constraints)) {
    LOGP(LS_ERROR) << "CreatePeerFactory failed";
    DeleteControl();
    return false;
  }

  webrtc_thread_ = rtc::Thread::Current();

  return true;
}

void Control::DeleteControl() {
  peer_connection_factory_ = NULL;
  fake_audio_capture_module_ = NULL;
}



void Control::SignIn(const std::string& user_id, const std::string& user_password, const std::string& open_id) {
  // 1. Connect to signal server
  // 2. Send signin command to signal server
  // 3. Send createchannel command to signal server (channel name is id or alias)
  //    Other peers connect to this peer by channel name, that is id or alias
  // 4. Generate 'signedin' event to PeerConnect

  if (signal_.get() == NULL) {
    LOGP(LS_ERROR) << "SignIn failed, no signal server";
    return;
  }

  open_id_ = open_id;
  user_id_ = user_id;

  // Start by signing in
  signal_->SignIn(user_id, user_password);
  return;
}

void Control::SignOut() {

  if (webrtc_thread_ != rtc::Thread::Current()) {
    ControlMessageData *data = new ControlMessageData(0, ref_);
    webrtc_thread_->Post(this, MSG_SIGNOUT, data);
    return;
  }

  signal_->SignOut();
  DisconnectAll();
}

void Control::Connect(const std::string id) {
  // 1. Join channel on signal server
  // 2. Server(remote) peer createoffer
  // 3. Client(local) peer answeroffer
  // 4. Conect datachannel

  if (signal_.get() == NULL) {
    LOGP(LS_ERROR) << "Join failed, no signal server";
    return;
  }

  JoinChannel(id);
}

void Control::Disconnect(const std::string id) {
  // 1. Leave channel on signal server
  // 2. Close remote data channel
  // 3. Close local data channel
  // 4. Close ice connection
  // 5. Erase peer

  QueuePeerDisconnect(id);
}

void Control::DisconnectAll() {
  std::vector<std::string> peer_ids;

  for (auto peer : peers_) {
    peer_ids.push_back(peer.second->remote_id());
  }

  LOGP(LS_INFO) << "DisconnectAll(): peer count is " << peer_ids.size();

  for (auto id : peer_ids) {
    Disconnect(id);
  }
}


//
// Send data to peer
//

void Control::Send(const std::string to, const char* buffer, const size_t size) {

  typedef std::map<std::string, rtc::scoped_refptr<PeerControl>>::iterator it_type;

  it_type it = peers_.find(to);
  if (it == peers_.end()) return;

  it->second->Send(buffer, size);
  return;
}

bool Control::SyncSend(const std::string to, const char* buffer, const size_t size) {

  typedef std::map<std::string, rtc::scoped_refptr<PeerControl>>::iterator it_type;

  it_type it = peers_.find(to);
  if (it == peers_.end()) return false;

  return it->second->SyncSend(buffer, size);
}


//
// Send command to other peer by signal server
//

void Control::SendCommand(const std::string& id, const std::string& command, const Json::Value& data) {
  signal_->SendCommand(id, command, data);
}



void Control::QueuePeerDisconnect(const std::string id) {

  ControlMessageData *data = new ControlMessageData(id, ref_);

  // 1. Leave channel on signal server
  LeaveChannel(id);

  // 2. Close remote data channel
  // 3. Close local data channel
  // 4. Close ice connection
  // 5. Erase peer
  webrtc_thread_->Post(this, MSG_DISCONNECT_PEER, data);
}


//
// Both peer local and remote data channel has been opened.
// It means that ice connection had been opened already and
// now we can send and receive data from/to data channel.
//
// Implements PeerObserver::OnPeerConnected()
//

void Control::OnPeerConnected(const std::string id) {
  if (observer_ == nullptr) return;
  observer_->OnPeerConnected(id);
}

//
// Ice connection state has been changed to close.
// It means that peer data channel had been closed already.
//
// Implements PeerObserver::OnDisconnected()
//

void Control::OnPeerDisconnected(const std::string id) {

  if (observer_ == nullptr) return;

  bool erased;
  std::map<std::string, Peer>::iterator it;

  for (it = peers_.begin(); it != peers_.end(); ) {
    if (it->second->remote_id() == id) {
      erased = true;
      peers_.erase(it++);
    }
    else {
      ++it;
    }
  }

  if (erased) {
    observer_->OnPeerDisconnected(id);
    if (peers_.size() == 0) {
      OnSignedOut(open_id_);
    }
  }
}

void Control::QueueOnPeerDisconnected(const std::string id) {
  ControlMessageData *data = new ControlMessageData(id, ref_);

  // Call Control::OnPeerDisconnected()
  webrtc_thread_->Post(this, MSG_ON_PEER_DISCONNECTED, data);
}


void Control::OnPeerChannelClosed(const std::string id) {
  auto peer = peers_.find(id);
  if (peer == peers_.end()) return;

  peer->second->ClosePeerConnection();
}

void Control::QueueOnPeerChannelClosed(const std::string id, int delay) {

//  close_peerconnection
  ControlMessageData *data = new ControlMessageData(id, ref_);

  // Call Control::OnPeerDisconnected()
  if (delay==0)
    webrtc_thread_->Post(this, MSG_ON_PEER_CHANNEL_CLOSED, data);
  else 
    webrtc_thread_->PostDelayed(delay, this, MSG_ON_PEER_CHANNEL_CLOSED, data);
}


//
// Signal receiving data
//

void Control::OnPeerMessage(const std::string& id, const char* buffer, const size_t size) {
  if (observer_ == nullptr) return;
  observer_->OnPeerMessage(id, buffer, size);
}

void Control::OnPeerWritable(const std::string& id) {
  if (observer_ == nullptr) return;
  observer_->OnPeerWritable(id);
}


void Control::RegisterObserver(ControlObserver* observer, std::shared_ptr<Control> ref) {
  ref_ = ref;
  observer_ = observer;
}

void Control::UnregisterObserver() {
  observer_ = nullptr;
  ref_.reset();
}

//
// Thread message queue
//

void Control::OnMessage(rtc::Message* msg) {
  ControlMessageData* param = nullptr;
    switch (msg->message_id) {
  case MSG_COMMAND_RECEIVED:
    param = static_cast<ControlMessageData*>(msg->pdata);
    OnCommandReceived(param->data_json_);
    break;
  case MSG_DISCONNECT:
    param = static_cast<ControlMessageData*>(msg->pdata);
    Disconnect(param->data_string_);
    break;
  case MSG_DISCONNECT_PEER:
    param = static_cast<ControlMessageData*>(msg->pdata);
    DisconnectPeer(param->data_string_);
    break;
  case MSG_ON_PEER_DISCONNECTED:
    param = static_cast<ControlMessageData*>(msg->pdata);
    OnPeerDisconnected(param->data_string_);
    break;
  case MSG_ON_PEER_CHANNEL_CLOSED:
    param = static_cast<ControlMessageData*>(msg->pdata);
    OnPeerChannelClosed(param->data_string_);
    break;
  case MSG_SIGNOUT:
    param = static_cast<ControlMessageData*>(msg->pdata);
    SignOut();
    break;
  case MSG_SIGNAL_SERVER_CLOSED:
    param = static_cast<ControlMessageData*>(msg->pdata);
    OnSignedOut(param->data_string_);
    break;
  default:
    break;
  }

  if (param != nullptr) delete param;
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

    LOGP(LS_ERROR) << "Invalid message:" << message;
    return;
  }

  if (!rtc::GetStringFromJsonObject(message, "peer_id", &peer_id)) {
    peer_id.clear();
  }

  if (command == "signin") {
    OnSignedIn(data);
  }
  else if (command == "channelcreated") {
    OnChannelCreated(data);
  }
  else if (command == "channeljoined") {
    OnChannelJoined(data);
  }
  else if (command == "channelleaved") {
    OnChannelLeaved(data);
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
  else if (command == "close_peerconnection") {
    ClosePeerConnection(peer_id, data);
  }
}

void Control::OnSignalCommandReceived(const Json::Value& message) {
  ControlMessageData *data = new ControlMessageData(message, ref_);
  webrtc_thread_->Post(this, MSG_COMMAND_RECEIVED, data);
}

void Control::OnSignalConnectionClosed(websocketpp::close::status::value code) {
  LOGP(LS_INFO) << "Calling OnSignalConnectionClosed() with " << code;
  if (code == websocketpp::close::status::normal) {
    ControlMessageData *data = new ControlMessageData(open_id_, ref_);
    webrtc_thread_->Post(this, MSG_SIGNAL_SERVER_CLOSED, data);
  }
}

void Control::OnSignedOut(const std::string& id) {
  LOGP(LS_INFO) << "Calling OnSignedOut() with " << id;

  if (signal_==nullptr || signal_->opened()) return;
  if (peers_.size() != 0) return;

  if (observer_!=nullptr) observer_->OnSignedOut(id);
}


//
// Commands to signal server
//

void Control::CreateChannel(const std::string name) {
  Json::Value data;
  data["name"] = name;
  SendCommand(name, "createchannel", data);
}

void Control::JoinChannel(const std::string name) {
  Json::Value data;
  data["name"] = name;
  SendCommand(name, "joinchannel", data);
}

void Control::LeaveChannel(const std::string name) {
  Json::Value data;
  data["name"] = name;
  SendCommand(name, "leavechannel", data);
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
    LOGP(LS_WARNING) << "Unknown signin response";
    return;
  }

  if (!result) {
    LOGP(LS_ERROR) << "Signin failed";
    return;
  }

  std::string session_id;
  if (!rtc::GetStringFromJsonObject(data, "session_id", &session_id)) {
    LOGP(LS_ERROR) << "Signin failed - no session_id";
    return;
  }

  session_id_ = session_id;

  //
  // Create channel
  //

  CreateChannel(open_id_);
}


void Control::OnChannelCreated(const Json::Value& data) {
  bool result;
  if (!rtc::GetBoolFromJsonObject(data, "result", &result)) {
    LOGP(LS_WARNING) << "Unknown signin response";
    return;
  }

  std::string channel;
  if (!rtc::GetStringFromJsonObject(data, "name", &channel)) {
    LOGP(LS_ERROR) << "Create channel failed - no channel name";
    return;
  }

  if (!result) {
    LOGP(LS_ERROR) << "Create channel failed";
    std::string reason;
    if (!rtc::GetStringFromJsonObject(data, "reason", &reason)) {
      reason = "Unknown reason";
    }
    observer_->OnError(channel, reason);
    return;
  }

  observer_->OnSignedIn(channel);
}

void Control::OnChannelJoined(const Json::Value& data) {
  bool result;

  LOGP(LS_INFO) << "OnChannelJoined(" << data.toStyledString() << ")";

  if (!rtc::GetBoolFromJsonObject(data, "result", &result)) {
    LOGP(LS_ERROR) << "Unknown channel join response";
    return;
  }

  std::string channel;
  if (!rtc::GetStringFromJsonObject(data, "name", &channel)) {
    LOGP(LS_ERROR) << "Join channel failed - no channel name";
    return;
  }

  if (!result) {
    LOGP(LS_ERROR) << "Join channel failed";
    std::string reason;
    if (!rtc::GetStringFromJsonObject(data, "reason", &reason)) {
      reason = "Unknown reason";
    }
    observer_->OnError(channel, reason);
    return;
  }
}


//
// 'leave' command
//

void Control::OnChannelLeaved(const Json::Value& data) {
  // Nothing
}


//
// 'createoffer' command
//

void Control::CreateOffer(const Json::Value& data) {

  Json::Value peers;
  if (!rtc::GetValueFromJsonObject(data, "peers", &peers)) {
    LOGP(LS_ERROR) << "createoffer failed - no peers value";
    return;
  }

  for (size_t i = 0; i < peers.size(); ++i) {
    std::string remote_id;
    if (!rtc::GetStringFromJsonArray(peers, i, &remote_id)) {
      LOGP(LS_ERROR) << "Peer handshake failed - invalid peer id";
      return;
    }

    Peer peer = new rtc::RefCountedObject<PeerControl>(open_id_, remote_id, this, peer_connection_factory_);
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

  Peer peer = new rtc::RefCountedObject<PeerControl>(open_id_, peer_id, this, peer_connection_factory_);
  peers_.insert(std::pair<std::string, Peer>(peer_id, peer));

  peer->ReceiveOfferSdp(sdp);
}


//
// 'answersdp' command
//

void Control::ReceiveAnswerSdp(const std::string& peer_id, const Json::Value& data) {
  std::string sdp;

  if (!rtc::GetStringFromJsonObject(data, "sdp", &sdp)) return;

  auto peer = peers_.find(peer_id);
  if (peer == peers_.end()) return;

  peer->second->ReceiveAnswerSdp(sdp);
}

void Control::ClosePeerConnection(const std::string& peer_id, const Json::Value& data) {
  auto peer = peers_.find(peer_id);
  if (peer == peers_.end()) return;

  peers_[peer_id]->ClosePeerConnection();
}



void Control::DisconnectPeer(const std::string id) {
  // 1. Close remote data channel (remote_data_channel_)
  // 2. Close local data channel (local_data_channel_)
  // 3. Close ice connection (peer_connection_)
  // 4. Erase peer

  auto peer = peers_.find(id);
  if (peer == peers_.end()) return;

  peer->second->Close();
}


} // namespace pc
