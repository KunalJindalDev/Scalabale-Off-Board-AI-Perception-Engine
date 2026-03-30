#pragma once

#include <memory>
#include <string>
#include <atomic>

#include <grpcpp/grpcpp.h>
#include "inference.grpc.pb.h"
#include "thread_pool.hpp"

namespace middleware {

// gRPC service that sits between the robot fleet (Module 3) and the
// inference backend (Module 1).  It accepts incoming requests, forwards
// them to the upstream inference server via a gRPC stub, and returns
// the results.  The thread pool controls the concurrency of upstream calls.
class MiddlewareServiceImpl final
    : public ::inference::InferenceService::Service {
public:
  // upstream_address – address of Module 1, e.g. "localhost:50052"
  // pool_size        – worker threads for upstream forwarding
  MiddlewareServiceImpl(const std::string& upstream_address,
                        std::size_t        pool_size = 8);

  // Unary RPC received from robots
  grpc::Status Detect(grpc::ServerContext*                    ctx,
                      const ::inference::DetectionRequest*    req,
                      ::inference::DetectionResponse*         resp) override;

  // Bidirectional-streaming RPC received from robots
  grpc::Status DetectStream(
      grpc::ServerContext*                                         ctx,
      grpc::ServerReaderWriter<::inference::DetectionResponse,
                               ::inference::DetectionRequest>* stream) override;

  uint64_t total_forwarded() const { return total_forwarded_.load(); }

private:
  // Forward one request to Module 1 synchronously (called from pool thread)
  ::inference::DetectionResponse forward(const ::inference::DetectionRequest& req);

  std::shared_ptr<grpc::Channel>                          upstream_channel_;
  std::unique_ptr<::inference::InferenceService::Stub>    stub_;
  ThreadPool                                              pool_;
  std::atomic<uint64_t>                                   total_forwarded_{0};
};

// Owns the gRPC server that listens for robot connections.
class MiddlewareServer {
public:
  // listen_address   – what robots connect to, e.g. "0.0.0.0:50051"
  // upstream_address – Module 1 address,     e.g. "localhost:50052"
  // pool_size        – forwarding thread pool size
  MiddlewareServer(const std::string& listen_address,
                   const std::string& upstream_address,
                   std::size_t        pool_size = 8);

  // Blocks until Shutdown() is called
  void Run();
  void Shutdown();

private:
  std::string                               listen_address_;
  std::unique_ptr<MiddlewareServiceImpl>    service_;
  std::unique_ptr<grpc::Server>             server_;
};

} // namespace middleware
