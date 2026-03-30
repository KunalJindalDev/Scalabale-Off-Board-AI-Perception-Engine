cmake_minimum_required(VERSION 3.16)
project(module1_inference CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Dependencies — Protobuf
# ---------------------------------------------------------------------------
find_package(Protobuf QUIET)
if(NOT Protobuf_FOUND)
  # Fallback: use pkg-config (works with apt-installed libprotobuf-dev)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(PROTOBUF REQUIRED protobuf)
  # Create the imported target that the rest of the file expects
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
  pkg_check_modules(GRPC  REQUIRED grpc)
  pkg_check_modules(GRPCPP REQUIRED grpc++)
  # Create the imported target that the rest of the file expects
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

# ONNX Runtime does not ship a standard CMake config file.
# Search in common install prefixes; override with -DONNXRUNTIME_ROOT=...
set(ONNXRUNTIME_ROOT "" CACHE PATH "Root of ONNX Runtime install (contains include/ and lib/)")

find_path(ONNXRUNTIME_INCLUDE_DIR
  NAMES onnxruntime/core/session/onnxruntime_cxx_api.h
        onnxruntime_cxx_api.h
  HINTS
    "${ONNXRUNTIME_ROOT}/include"
    /usr/local/include
    /usr/include
  DOC "ONNX Runtime include directory"
)

find_library(ONNXRUNTIME_LIB
  NAMES onnxruntime
  HINTS
    "${ONNXRUNTIME_ROOT}/lib"
    /usr/local/lib
    /usr/lib
    /usr/lib/x86_64-linux-gnu
  DOC "ONNX Runtime shared library"
)

if(NOT ONNXRUNTIME_INCLUDE_DIR OR NOT ONNXRUNTIME_LIB)
  message(FATAL_ERROR
    "Could not find ONNX Runtime.\n"
    "Install it with:\n"
    "  wget https://github.com/microsoft/onnxruntime/releases/download/v1.18.1/onnxruntime-linux-x64-1.18.1.tgz\n"
    "  tar -xzf onnxruntime-linux-x64-1.18.1.tgz\n"
    "  sudo cp -r onnxruntime-linux-x64-1.18.1/include/* /usr/local/include/\n"
    "  sudo cp -r onnxruntime-linux-x64-1.18.1/lib/*     /usr/local/lib/\n"
    "  sudo ldconfig\n"
    "Or re-run cmake with: -DONNXRUNTIME_ROOT=/path/to/onnxruntime-linux-x64-1.18.1"
  )
endif()

message(STATUS "ONNX Runtime include: ${ONNXRUNTIME_INCLUDE_DIR}")
message(STATUS "ONNX Runtime lib    : ${ONNXRUNTIME_LIB}")

# Create an imported target so link syntax stays clean
add_library(onnxruntime SHARED IMPORTED)
set_target_properties(onnxruntime PROPERTIES
  IMPORTED_LOCATION             "${ONNXRUNTIME_LIB}"
  INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIR}"
)

# ---------------------------------------------------------------------------
# Proto / gRPC code generation
# ---------------------------------------------------------------------------
set(PROTO_FILE "${CMAKE_SOURCE_DIR}/../proto/inference.proto")
set(PROTO_OUT  "${CMAKE_CURRENT_BINARY_DIR}/proto")
file(MAKE_DIRECTORY ${PROTO_OUT})

# Locate protoc and the gRPC plugin
find_program(PROTOC       protoc                          REQUIRED)
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin             REQUIRED)

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
  COMMENT "Generating protobuf/gRPC sources for Module 1"
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
# Core inference library (used by both the server binary and tests)
# ---------------------------------------------------------------------------
add_library(inference_lib STATIC
  src/model_runner.cpp
  src/inference_server.cpp
)

target_include_directories(inference_lib PUBLIC
  include
  ${PROTO_OUT}
)

target_link_libraries(inference_lib PUBLIC
  proto_gen
  onnxruntime
  gRPC::grpc++
  protobuf::libprotobuf
)

target_compile_options(inference_lib PRIVATE -Wall -Wextra -O2)

# ---------------------------------------------------------------------------
# Inference server binary
# ---------------------------------------------------------------------------
add_executable(inference_server src/main.cpp)
target_link_libraries(inference_server PRIVATE inference_lib)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
add_subdirectory(tests)
