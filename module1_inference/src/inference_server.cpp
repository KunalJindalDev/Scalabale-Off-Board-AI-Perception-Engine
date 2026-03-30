#include "inference_server.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>

#include <grpcpp/server_builder.h>

namespace inference {

// ---------------------------------------------------------------------------
// InferenceServiceImpl
// ---------------------------------------------------------------------------
InferenceServiceImpl::InferenceServiceImpl(std::shared_ptr<ModelRunner> runner)
  : runner_(std::move(runner)) {}

::inference::DetectionResponse
InferenceServiceImpl::process(const ::inference::DetectionRequest& req) {
  const auto& frame = req.frame();

  if (frame.rgb_data().empty()) {
    throw std::invalid_argument("Empty rgb_data in DetectionRequest");
  }

  const auto* rgb   = reinterpret_cast<const uint8_t*>(frame.rgb_data().data());
  const float* depth = frame.depth_data().empty()
                         ? nullptr
                         : reinterpret_cast<const float*>(frame.depth_data().data());

  auto t0 = std::chrono::steady_clock::now();

  auto dets = runner_->run(rgb, depth,
                           static_cast<int>(frame.width()),
                           static_cast<int>(frame.height()),
                           req.fx(), req.fy(), req.cx(), req.cy());

  auto t1 = std::chrono::steady_clock::now();
  float ms = std::chrono::duration<float, std::milli>(t1 - t0).count();

  ::inference::DetectionResponse resp;
  resp.set_robot_id(req.robot_id());
  resp.set_timestamp_ns(req.timestamp_ns());
  resp.set_inference_time_ms(ms);

  for (const auto& d : dets) {
    auto* bb = resp.add_detections();
    bb->set_x_min(d.x_min);
    bb->set_y_min(d.y_min);
    bb->set_x_max(d.x_max);
    bb->set_y_max(d.y_max);
    bb->set_confidence(d.confidence);
    bb->set_class_id(d.class_id);
    bb->set_class_name(d.class_name);
    bb->set_world_x(d.world_x);
    bb->set_world_y(d.world_y);
    bb->set_world_z(d.world_z);
  }

  ++total_inferences_;
  return resp;
}

grpc::Status InferenceServiceImpl::Detect(
    grpc::ServerContext*                  /*ctx*/,
    const ::inference::DetectionRequest*  req,
    ::inference::DetectionResponse*       resp) {
  try {
    *resp = process(*req);
    return grpc::Status::OK;
  } catch (const std::exception& e) {
    return grpc::Status(grpc::StatusCode::INTERNAL, e.what());
  }
}

grpc::Status InferenceServiceImpl::DetectStream(
    grpc::ServerContext* /*ctx*/,
    grpc::ServerReaderWriter<::inference::DetectionResponse,
                             ::inference::DetectionRequest>* stream) {
  ::inference::DetectionRequest req;
  while (stream->Read(&req)) {
    try {
      auto resp = process(req);
      if (!stream->Write(resp)) break;  // client disconnected
    } catch (const std::exception& e) {
      std::cerr << "[InferenceServer] stream error: " << e.what() << '\n';
    }
  }
  return grpc::Status::OK;
}

// ---------------------------------------------------------------------------
// InferenceServer
// ---------------------------------------------------------------------------
InferenceServer::InferenceServer(const std::string& address,
                                 std::shared_ptr<ModelRunner> runner)
  : address_(address),
    service_(std::make_shared<InferenceServiceImpl>(std::move(runner))) {}

void InferenceServer::Run() {
  grpc::ServerBuilder builder;
  builder.AddListeningPort(address_, grpc::InsecureServerCredentials());
  builder.RegisterService(service_.get());

  // Increase default message limits to accommodate image frames
  builder.SetMaxReceiveMessageSize(50 * 1024 * 1024);  // 50 MB
  builder.SetMaxSendMessageSize(4  * 1024 * 1024);     //  4 MB

  server_ = builder.BuildAndStart();
  if (!server_) {
    throw std::runtime_error("Failed to start gRPC server on " + address_);
  }

  std::cout << "[InferenceServer] Listening on " << address_ << '\n';
  server_->Wait();
}

void InferenceServer::Shutdown() {
  if (server_) {
    server_->Shutdown();
    std::cout << "[InferenceServer] Shutdown complete.\n";
  }
}

} // namespace inference
