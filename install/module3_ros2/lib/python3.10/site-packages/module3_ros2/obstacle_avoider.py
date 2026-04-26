"""
ObstacleAvoider — one instance per robot.

Subscribes to:
  /robot_<id>/detections     (std_msgs/String, JSON from RobotNode)
  /robot_<id>/odom           (nav_msgs/Odometry, optional – used for recovery)

Publishes:
  /robot_<id>/cmd_vel        (geometry_msgs/Twist)

Avoidance strategy:
  1. If no obstacle is within DANGER_ZONE_M metres → drive forward.
  2. If an obstacle is on the left side of the FOV → turn right.
  3. If on the right side → turn left.
  4. If directly ahead (centre ±25 %) → stop and rotate in place.
  5. Recovery: if the robot has been stopped for >RECOVERY_TIMEOUT_S → spin.
"""

from __future__ import annotations

import json
import time
from typing import List, Optional

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy, HistoryPolicy
from geometry_msgs.msg import Twist
from std_msgs.msg import String
from nav_msgs.msg import Odometry

SENSOR_QOS = QoSProfile(
    reliability=ReliabilityPolicy.BEST_EFFORT,
    history=HistoryPolicy.KEEP_LAST,
    depth=1,
)


class Detection:
    """Lightweight container parsed from the JSON payload."""
    __slots__ = ("class_name", "confidence", "bbox", "world_x", "world_y", "world_z")

    def __init__(self, d: dict):
        self.class_name  = d.get("class", "unknown")
        self.confidence  = float(d.get("confidence", 0.0))
        bbox             = d.get("bbox", [0, 0, 0, 0])
        self.bbox        = bbox
        world            = d.get("world", [0, 0, 0])
        self.world_x     = world[0]
        self.world_y     = world[1]
        self.world_z     = world[2]  # forward distance (metres)

    @property
    def centre_x_norm(self) -> float:
        """Normalised x-centre of the bounding box in [0, 1]."""
        return (self.bbox[0] + self.bbox[2]) / 2.0 / 640.0  # assumes w=640


class ObstacleAvoider(Node):
    """Reactive obstacle avoidance using detection bounding boxes."""

    # Tunable parameters
    DANGER_ZONE_M      = 1.5    # m — stop / steer if obstacle is closer
    LINEAR_SPEED       = 0.5    # m/s — cruise speed
    ANGULAR_SPEED      = 1.2    # rad/s — turning speed
    RECOVERY_TIMEOUT_S = 3.0    # s — spin if stuck this long
    CONTROL_HZ         = 10.0   # Hz — cmd_vel publish rate

    def __init__(self):
        super().__init__("obstacle_avoider")

        self.declare_parameter("robot_id", "robot_0")
        self._robot_id = self.get_parameter("robot_id").value

        ns = f"/{self._robot_id}"

        # ----------------------------------------------------------------
        # State
        # ----------------------------------------------------------------
        self._latest_dets: List[Detection] = []
        self._stopped_since: Optional[float] = None
        self._in_recovery = False

        # ----------------------------------------------------------------
        # Subscriptions
        # ----------------------------------------------------------------
        self.create_subscription(
            String, f"{ns}/detections",
            self._detections_callback, 10,
        )
        self.create_subscription(
            Odometry, f"{ns}/odom",
            self._odom_callback, SENSOR_QOS,
        )

        # ----------------------------------------------------------------
        # Publisher
        # ----------------------------------------------------------------
        self._cmd_pub = self.create_publisher(Twist, f"{ns}/cmd_vel", 10)

        # ----------------------------------------------------------------
        # Control loop timer
        # ----------------------------------------------------------------
        self.create_timer(1.0 / self.CONTROL_HZ, self._control_loop)

        self.get_logger().info(
            f"[{self._robot_id}] ObstacleAvoider ready  "
            f"(danger_zone={self.DANGER_ZONE_M} m)"
        )

    # ------------------------------------------------------------------
    # Callbacks
    # ------------------------------------------------------------------

    def _detections_callback(self, msg: String):
        try:
            payload = json.loads(msg.data)
        except json.JSONDecodeError:
            return
        self._latest_dets = [Detection(d) for d in payload.get("detections", [])]

    def _odom_callback(self, _msg: Odometry):
        pass  # reserved for future dead-reckoning / recovery behaviour

    # ------------------------------------------------------------------
    # Control loop
    # ------------------------------------------------------------------

    def _control_loop(self):
        cmd = Twist()

        # Filter to only obstacles within the danger zone with depth info
        close = [
            d for d in self._latest_dets
            if 0 < d.world_z < self.DANGER_ZONE_M
        ]

        if not close:
            # Clear path — drive forward
            cmd.linear.x  = self.LINEAR_SPEED
            cmd.angular.z = 0.0
            self._stopped_since = None
            self._in_recovery   = False
        else:
            # Find the nearest obstacle
            nearest = min(close, key=lambda d: d.world_z)
            cx      = nearest.centre_x_norm  # 0 = left, 1 = right

            if self._stopped_since is None:
                self._stopped_since = time.monotonic()

            stuck_s = time.monotonic() - self._stopped_since

            if stuck_s > self.RECOVERY_TIMEOUT_S:
                # Spin in place to escape a dead end
                self._in_recovery = True
                cmd.linear.x  = 0.0
                cmd.angular.z = self.ANGULAR_SPEED  # always CCW in recovery
                self.get_logger().warning(
                    f"[{self._robot_id}] Recovery spin (stuck {stuck_s:.1f}s)"
                )
            elif 0.375 <= cx <= 0.625:
                # Obstacle directly ahead → stop and rotate
                cmd.linear.x  = 0.0
                cmd.angular.z = self.ANGULAR_SPEED  # turn left
            elif cx < 0.375:
                # Obstacle on the left → turn right
                cmd.linear.x  = self.LINEAR_SPEED * 0.3
                cmd.angular.z = -self.ANGULAR_SPEED
            else:
                # Obstacle on the right → turn left
                cmd.linear.x  = self.LINEAR_SPEED * 0.3
                cmd.angular.z = self.ANGULAR_SPEED

        self._cmd_pub.publish(cmd)


def main(args=None):
    rclpy.init(args=args)
    node = ObstacleAvoider()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
