#pragma once

#include <memory>
#include <string>
#include <atomic>

#include <grpcpp/grpcpp.h>
#include "inference.grpc.pb.h"
#include "model_runner.hpp"

namespace inference {

// gRPC service implementation for Module 1.
// Owns a ModelRunner and handles both unary and bidirectional-streaming RPCs.
class InferenceServiceImpl final : public ::inference::InferenceService::Service {
public:
  explicit InferenceServiceImpl(std::shared_ptr<ModelRunner> runner);

  // Unary RPC
  grpc::Status Detect(grpc::ServerContext*          ctx,
                      const ::inference::DetectionRequest*  req,
                      ::inference::DetectionResponse*       resp) override;

  // Bidirectional streaming RPC (one goroutine per robot stream)
  grpc::Status DetectStream(
      grpc::ServerContext*                                    ctx,
      grpc::ServerReaderWriter<::inference::DetectionResponse,
                               ::inference::DetectionRequest>* stream) override;

  // Total inferences served since startup
  uint64_t total_inferences() const { return total_inferences_.load(); }

private:
  // Shared between unary + stream handlers
  ::inference::DetectionResponse process(const ::inference::DetectionRequest& req);

  std::shared_ptr<ModelRunner> runner_;
  std::atomic<uint64_t>        total_inferences_{0};
};

// Owns the gRPC server and blocks until shutdown is requested.
class InferenceServer {
public:
  // address – e.g. "0.0.0.0:50052"
  InferenceServer(const std::string& address,
                  std::shared_ptr<ModelRunner> runner);

  // Blocks until Shutdown() is called from another thread.
  void Run();

  // Signal the server to stop (safe to call from a signal handler via
  // std::atomic_flag, or from another thread).
  void Shutdown();

private:
  std::string                              address_;
  std::shared_ptr<InferenceServiceImpl>    service_;
  std::unique_ptr<grpc::Server>            server_;
};

} // namespace inference
