"""
Thin wrapper around the generated gRPC stub.

Run once before using this module:
  python3 -m grpc_tools.protoc \
    -I../proto \
    --python_out=. \
    --grpc_python_out=. \
    ../proto/inference.proto
This generates inference_pb2.py and inference_pb2_grpc.py next to this file.
"""

from __future__ import annotations

import time
from typing import Optional

import grpc

# Generated stubs (created by protoc, committed alongside the package)
try:
    from . import inference_pb2       # type: ignore
    from . import inference_pb2_grpc  # type: ignore
except ImportError as e:
    raise ImportError(
        "gRPC stubs not found. Generate them with:\n"
        "  python3 -m grpc_tools.protoc -I<repo>/proto "
        "--python_out=module3_ros2/module3_ros2 "
        "--grpc_python_out=module3_ros2/module3_ros2 "
        "<repo>/proto/inference.proto"
    ) from e


class InferenceClient:
    """Synchronous gRPC client connecting to the Module 2 middleware."""

    def __init__(self, address: str = "localhost:50051", timeout_s: float = 5.0):
        self._address = address
        self._timeout = timeout_s

        options = [
            ("grpc.max_send_message_length",    50 * 1024 * 1024),
            ("grpc.max_receive_message_length", 10 * 1024 * 1024),
        ]
        self._channel = grpc.insecure_channel(address, options=options)
        self._stub    = inference_pb2_grpc.InferenceServiceStub(self._channel)

    # ------------------------------------------------------------------
    # Unary call
    # ------------------------------------------------------------------
    def detect(
        self,
        robot_id: str,
        rgb_bytes: bytes,
        depth_bytes: bytes,
        width: int,
        height: int,
        fx: float, fy: float,
        cx: float, cy: float,
        timestamp_ns: Optional[int] = None,
    ) -> "inference_pb2.DetectionResponse":
        if timestamp_ns is None:
            timestamp_ns = time.time_ns()

        frame = inference_pb2.ImageFrame(
            rgb_data   = rgb_bytes,
            depth_data = depth_bytes,
            width      = width,
            height     = height,
        )
        req = inference_pb2.DetectionRequest(
            robot_id     = robot_id,
            timestamp_ns = timestamp_ns,
            frame        = frame,
            fx=fx, fy=fy, cx=cx, cy=cy,
        )
        return self._stub.Detect(req, timeout=self._timeout)

    # ------------------------------------------------------------------
    # Streaming call — yields DetectionResponse for each sent request
    # ------------------------------------------------------------------
    def detect_stream(self, request_iterator):
        return self._stub.DetectStream(request_iterator, timeout=None)

    def close(self):
        self._channel.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
