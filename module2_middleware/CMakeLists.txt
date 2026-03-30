cmake_minimum_required(VERSION 3.16)
project(module2_middleware CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Dependencies — Protobuf
# ---------------------------------------------------------------------------
find_package(Protobuf QUIET)
if(NOT Protobuf_FOUND)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(PROTOBUF REQUIRED protobuf)
  add_library(protobuf::libprotobuf INTERFACE IMPORTED)
  set_target_properties(protobuf::libprotobuf PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${PROTOBUF_INCLUDE_DIRS}"
    INTERFACE_LINK_LIBRARIES      "${PROTOBUF_LIBRARIES}"
    INTERFACE_LINK_DIRECTORIES    "${PROTOBUF_LIBRARY_DIRS}"
  )
  message(STATUS "Protobuf (pkg-config): ${PROTOBUF_VERSION}")
else()
  message(STATUS "Protobuf (cmake): ${Protobuf_VERSION}")
endif()

# ---------------------------------------------------------------------------
# Dependencies — gRPC
# ---------------------------------------------------------------------------
find_package(gRPC QUIET)
if(NOT gRPC_FOUND)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GRPC   REQUIRED grpc)
  pkg_check_modules(GRPCPP REQUIRED grpc++)
  add_library(gRPC::grpc++ INTERFACE IMPORTED)
  set_target_properties(gRPC::grpc++ PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${GRPCPP_INCLUDE_DIRS}"
    INTERFACE_LINK_LIBRARIES      "${GRPCPP_LIBRARIES};${GRPC_LIBRARIES}"
    INTERFACE_LINK_DIRECTORIES    "${GRPCPP_LIBRARY_DIRS}"
  )
  message(STATUS "gRPC (pkg-config): ${GRPCPP_VERSION}")
else()
  message(STATUS "gRPC (cmake): ${gRPC_VERSION}")
endif()

# ---------------------------------------------------------------------------
# Proto / gRPC code generation (same .proto as Module 1)
# ---------------------------------------------------------------------------
set(PROTO_FILE "${CMAKE_SOURCE_DIR}/../proto/inference.proto")
set(PROTO_OUT  "${CMAKE_CURRENT_BINARY_DIR}/proto")
file(MAKE_DIRECTORY ${PROTO_OUT})

find_program(PROTOC          protoc          REQUIRED)
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)

add_custom_command(
  OUTPUT  "${PROTO_OUT}/inference.pb.cc"
          "${PROTO_OUT}/inference.pb.h"
          "${PROTO_OUT}/inference.grpc.pb.cc"
          "${PROTO_OUT}/inference.grpc.pb.h"
  COMMAND ${PROTOC}
          --proto_path="${CMAKE_SOURCE_DIR}/../proto"
          --cpp_out="${PROTO_OUT}"
          --grpc_out="${PROTO_OUT}"
          --plugin=protoc-gen-grpc="${GRPC_CPP_PLUGIN}"
          "${PROTO_FILE}"
  DEPENDS "${PROTO_FILE}"
  COMMENT "Generating protobuf/gRPC sources for Module 2"
)

add_library(proto_gen STATIC
  "${PROTO_OUT}/inference.pb.cc"
  "${PROTO_OUT}/inference.grpc.pb.cc"
)
target_include_directories(proto_gen PUBLIC ${PROTO_OUT})
target_link_libraries(proto_gen PUBLIC
  protobuf::libprotobuf
  gRPC::grpc++
)

# ---------------------------------------------------------------------------
# Middleware core library
# ---------------------------------------------------------------------------
add_library(middleware_lib STATIC
  src/thread_pool.cpp
  src/middleware_server.cpp
)

target_include_directories(middleware_lib PUBLIC
  include
  ${PROTO_OUT}
)

target_link_libraries(middleware_lib PUBLIC
  proto_gen
  gRPC::grpc++
  protobuf::libprotobuf
)

target_compile_options(middleware_lib PRIVATE -Wall -Wextra -O2)

# ---------------------------------------------------------------------------
# Middleware server binary
# ---------------------------------------------------------------------------
add_executable(middleware_server src/main.cpp)
target_link_libraries(middleware_server PRIVATE middleware_lib)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
add_subdirectory(tests)
