#include "middleware_server.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>

#include <grpcpp/server_builder.h>
#include <grpcpp/create_channel.h>

namespace middleware {

// ---------------------------------------------------------------------------
// MiddlewareServiceImpl
// ---------------------------------------------------------------------------
MiddlewareServiceImpl::MiddlewareServiceImpl(const std::string& upstream_address,
                                             std::size_t        pool_size)
  : pool_(pool_size) {
  grpc::ChannelArguments args;
  args.SetInt(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, 50 * 1024 * 1024);
  args.SetInt(GRPC_ARG_MAX_SEND_MESSAGE_LENGTH,    50 * 1024 * 1024);

  upstream_channel_ = grpc::CreateCustomChannel(
      upstream_address, grpc::InsecureChannelCredentials(), args);

  stub_ = ::inference::InferenceService::NewStub(upstream_channel_);

  std::cout << "[Middleware] Upstream = " << upstream_address
            << "  pool_size = " << pool_size << '\n';
}

::inference::DetectionResponse
MiddlewareServiceImpl::forward(const ::inference::DetectionRequest& req) {
  grpc::ClientContext ctx;
  ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));

  ::inference::DetectionResponse resp;
  grpc::Status status = stub_->Detect(&ctx, req, &resp);

  if (!status.ok()) {
    throw std::runtime_error("[Middleware] Upstream gRPC error: " +
                             status.error_message());
  }
  ++total_forwarded_;
  return resp;
}

grpc::Status MiddlewareServiceImpl::Detect(
    grpc::ServerContext*                 /*ctx*/,
    const ::inference::DetectionRequest* req,
    ::inference::DetectionResponse*      resp) {
  try {
    // Dispatch to thread pool so this handler thread is not blocked
    auto future = pool_.enqueue([this, req] { return forward(*req); });
    *resp = future.get();
    return grpc::Status::OK;
  } catch (const std::exception& e) {
    return grpc::Status(grpc::StatusCode::INTERNAL, e.what());
  }
}

grpc::Status MiddlewareServiceImpl::DetectStream(
    grpc::ServerContext* /*ctx*/,
    grpc::ServerReaderWriter<::inference::DetectionResponse,
                             ::inference::DetectionRequest>* stream) {
  ::inference::DetectionRequest req;
  while (stream->Read(&req)) {
    try {
      // Each frame is forwarded asynchronously; we await before writing back
      // to preserve the per-robot ordering guarantee.
      auto future = pool_.enqueue([this, req] { return forward(req); });
      auto resp   = future.get();
      if (!stream->Write(resp)) break;
    } catch (const std::exception& e) {
      std::cerr << "[Middleware] stream error for robot "
                << req.robot_id() << ": " << e.what() << '\n';
    }
  }
  return grpc::Status::OK;
}

// ---------------------------------------------------------------------------
// MiddlewareServer
// ---------------------------------------------------------------------------
MiddlewareServer::MiddlewareServer(const std::string& listen_address,
                                   const std::string& upstream_address,
                                   std::size_t        pool_size)
  : listen_address_(listen_address),
    service_(std::make_unique<MiddlewareServiceImpl>(upstream_address, pool_size)) {}

void MiddlewareServer::Run() {
  grpc::ServerBuilder builder;
  builder.AddListeningPort(listen_address_, grpc::InsecureServerCredentials());
  builder.RegisterService(service_.get());
  builder.SetMaxReceiveMessageSize(50 * 1024 * 1024);
  builder.SetMaxSendMessageSize(50 * 1024 * 1024);

  server_ = builder.BuildAndStart();
  if (!server_) {
    throw std::runtime_error("[Middleware] Failed to bind on " + listen_address_);
  }

  std::cout << "[Middleware] Listening on " << listen_address_ << '\n';
  server_->Wait();
}

void MiddlewareServer::Shutdown() {
  if (server_) {
    server_->Shutdown();
    std::cout << "[Middleware] Shutdown complete. Total forwarded: "
              << service_->total_forwarded() << '\n';
  }
}

} // namespace middleware
