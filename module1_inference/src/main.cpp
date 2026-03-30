#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "inference_server.hpp"
#include "model_runner.hpp"

// Pointer held so the signal handler can trigger a clean shutdown.
static inference::InferenceServer* g_server = nullptr;

static void on_signal(int /*sig*/) {
  if (g_server) g_server->Shutdown();
}

int main(int argc, char** argv) {
  // Usage: inference_server <model.onnx> [address] [conf] [iou]
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0]
              << " <model.onnx> [address=0.0.0.0:50052]"
                 " [conf_thresh=0.45] [iou_thresh=0.50]\n";
    return EXIT_FAILURE;
  }

  const std::string model_path  = argv[1];
  const std::string address     = (argc > 2) ? argv[2] : "0.0.0.0:50052";
  const float       conf_thresh = (argc > 3) ? std::stof(argv[3]) : 0.45f;
  const float       iou_thresh  = (argc > 4) ? std::stof(argv[4]) : 0.50f;

  std::cout << "[InferenceServer] Loading model: " << model_path << '\n';

  std::shared_ptr<inference::ModelRunner> runner;
  try {
    runner = std::make_shared<inference::ModelRunner>(
        model_path, conf_thresh, iou_thresh);
  } catch (const std::exception& e) {
    std::cerr << "[InferenceServer] Failed to load model: " << e.what() << '\n';
    return EXIT_FAILURE;
  }

  std::cout << "[InferenceServer] Model loaded. Input: "
            << runner->input_name() << '\n';

  inference::InferenceServer server(address, std::move(runner));
  g_server = &server;

  std::signal(SIGINT,  on_signal);
  std::signal(SIGTERM, on_signal);

  server.Run();  // blocks until Shutdown()
  return EXIT_SUCCESS;
}
