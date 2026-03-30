#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

#include "middleware_server.hpp"

static middleware::MiddlewareServer* g_server = nullptr;

static void on_signal(int /*sig*/) {
  if (g_server) g_server->Shutdown();
}

int main(int argc, char** argv) {
  // Usage: middleware_server [listen_addr] [upstream_addr] [pool_size]
  const std::string listen_addr   = (argc > 1) ? argv[1] : "0.0.0.0:50051";
  const std::string upstream_addr = (argc > 2) ? argv[2] : "localhost:50052";
  const std::size_t pool_size     = (argc > 3) ? static_cast<std::size_t>(std::stoul(argv[3])) : 8;

  std::cout << "[Middleware] Starting\n"
            << "  listen   : " << listen_addr   << '\n'
            << "  upstream : " << upstream_addr << '\n'
            << "  threads  : " << pool_size     << '\n';

  middleware::MiddlewareServer server(listen_addr, upstream_addr, pool_size);
  g_server = &server;

  std::signal(SIGINT,  on_signal);
  std::signal(SIGTERM, on_signal);

  server.Run();
  return EXIT_SUCCESS;
}
