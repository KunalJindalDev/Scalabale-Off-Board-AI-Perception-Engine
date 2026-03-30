"""
RobotNode — one instance per simulated robot.

Subscribes to:
  /robot_<id>/camera/image_raw          (sensor_msgs/Image, BGR8)
  /robot_<id>/camera/depth/image_raw    (sensor_msgs/Image, 32FC1, metres)

Publishes detection results to:
  /robot_<id>/detections                (std_msgs/String, JSON)

Sends frames to the Module 2 middleware over gRPC and publishes the
bounding-box results so ObstacleAvoider can react.
"""

from __future__ import annotations

import json
import time
import threading
from typing import Optional

import numpy as np
import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy, HistoryPolicy
from sensor_msgs.msg import Image, CameraInfo
from std_msgs.msg import String

from .grpc_client import InferenceClient


# Lazy import: cv_bridge is only needed at runtime, not at import time.
try:
    from cv_bridge import CvBridge
    _BRIDGE = CvBridge()
except ImportError:
    _BRIDGE = None


SENSOR_QOS = QoSProfile(
    reliability=ReliabilityPolicy.BEST_EFFORT,
    history=HistoryPolicy.KEEP_LAST,
    depth=1,
)


class RobotNode(Node):
    """Captures camera frames, sends them for inference, publishes results."""

    def __init__(self):
        super().__init__("robot_node")

        # ----------------------------------------------------------------
        # Parameters (set via launch arguments or ros2 param)
        # ----------------------------------------------------------------
        self.declare_parameter("robot_id",         "robot_0")
        self.declare_parameter("middleware_addr",  "localhost:50051")
        self.declare_parameter("max_fps",          5.0)   # cap frame rate sent

        self._robot_id = self.get_parameter("robot_id").value
        addr           = self.get_parameter("middleware_addr").value
        self._min_dt   = 1.0 / self.get_parameter("max_fps").value

        # ----------------------------------------------------------------
        # Camera intrinsics (updated from /camera_info)
        # ----------------------------------------------------------------
        self._fx = self._fy = 500.0
        self._cx = self._cy = 320.0

        # ----------------------------------------------------------------
        # Synchronised frame buffers
        # ----------------------------------------------------------------
        self._lock        = threading.Lock()
        self._latest_rgb:   Optional[np.ndarray] = None
        self._latest_depth: Optional[np.ndarray] = None
        self._last_sent     = 0.0

        # ----------------------------------------------------------------
        # gRPC client
        # ----------------------------------------------------------------
        self._client = InferenceClient(address=addr)
        self.get_logger().info(
            f"[{self._robot_id}] gRPC client → {addr}"
        )

        # ----------------------------------------------------------------
        # ROS subscriptions
        # ----------------------------------------------------------------
        ns = f"/{self._robot_id}"

        self.create_subscription(
            Image, f"{ns}/camera/image_raw",
            self._rgb_callback, SENSOR_QOS,
        )
        self.create_subscription(
            Image, f"{ns}/camera/depth/image_raw",
            self._depth_callback, SENSOR_QOS,
        )
        self.create_subscription(
            CameraInfo, f"{ns}/camera/camera_info",
            self._camera_info_callback, SENSOR_QOS,
        )

        # ----------------------------------------------------------------
        # Publisher for downstream obstacle avoider
        # ----------------------------------------------------------------
        self._det_pub = self.create_publisher(
            String, f"{ns}/detections", 10
        )

        # ----------------------------------------------------------------
        # Timer to drive inference at capped rate
        # ----------------------------------------------------------------
        self.create_timer(self._min_dt, self._inference_tick)

    # ------------------------------------------------------------------
    # Callbacks
    # ------------------------------------------------------------------

    def _rgb_callback(self, msg: Image):
        if _BRIDGE is None:
            return
        try:
            img = _BRIDGE.imgmsg_to_cv2(msg, desired_encoding="bgr8")
            with self._lock:
                self._latest_rgb = img
        except Exception as e:
            self.get_logger().warning(f"RGB conversion error: {e}")

    def _depth_callback(self, msg: Image):
        if _BRIDGE is None:
            return
        try:
            depth = _BRIDGE.imgmsg_to_cv2(msg, desired_encoding="passthrough")
            if depth.dtype != np.float32:
                depth = depth.astype(np.float32)
            with self._lock:
                self._latest_depth = depth
        except Exception as e:
            self.get_logger().warning(f"Depth conversion error: {e}")

    def _camera_info_callback(self, msg: CameraInfo):
        # Camera intrinsic matrix K: [fx, 0, cx, 0, fy, cy, 0, 0, 1]
        if len(msg.k) >= 6:
            self._fx = msg.k[0]
            self._fy = msg.k[4]
            self._cx = msg.k[2]
            self._cy = msg.k[5]

    # ------------------------------------------------------------------
    # Inference tick (called by timer)
    # ------------------------------------------------------------------

    def _inference_tick(self):
        now = time.monotonic()
        if now - self._last_sent < self._min_dt:
            return

        with self._lock:
            rgb   = self._latest_rgb
            depth = self._latest_depth

        if rgb is None:
            return  # no frame yet

        h, w = rgb.shape[:2]
        rgb_bytes   = rgb.tobytes()
        depth_bytes = depth.tobytes() if depth is not None else b""

        try:
            resp = self._client.detect(
                robot_id    = self._robot_id,
                rgb_bytes   = rgb_bytes,
                depth_bytes = depth_bytes,
                width       = w,
                height      = h,
                fx=self._fx, fy=self._fy,
                cx=self._cx, cy=self._cy,
                timestamp_ns=time.time_ns(),
            )
            self._last_sent = now
            self._publish_detections(resp)

        except Exception as e:
            self.get_logger().warning(f"[{self._robot_id}] gRPC error: {e}")

    def _publish_detections(self, resp):
        dets = []
        for bb in resp.detections:
            dets.append({
                "class":      bb.class_name,
                "confidence": round(bb.confidence, 3),
                "bbox":       [bb.x_min, bb.y_min, bb.x_max, bb.y_max],
                "world":      [round(bb.world_x, 3),
                               round(bb.world_y, 3),
                               round(bb.world_z, 3)],
            })
        payload = {
            "robot_id":          resp.robot_id,
            "timestamp_ns":      resp.timestamp_ns,
            "inference_time_ms": round(resp.inference_time_ms, 2),
            "detections":        dets,
        }
        msg = String()
        msg.data = json.dumps(payload)
        self._det_pub.publish(msg)

    def destroy_node(self):
        self._client.close()
        super().destroy_node()


def main(args=None):
    rclpy.init(args=args)
    node = RobotNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
