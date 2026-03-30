"""
FleetManager — optional diagnostic node.

Aggregates detection statistics from all robots and prints a live summary
table. Useful for benchmarking latency and throughput during integration
testing (run with `ros2 run module3_ros2 fleet_manager`).
"""

from __future__ import annotations

import json
import time
from collections import defaultdict
from typing import DefaultDict, Dict, List

import rclpy
from rclpy.node import Node
from std_msgs.msg import String


class FleetManager(Node):
    def __init__(self):
        super().__init__("fleet_manager")

        self.declare_parameter("robot_ids", ["robot_0", "robot_1", "robot_2"])
        self._robot_ids: List[str] = (
            self.get_parameter("robot_ids").value
        )

        # Per-robot stats
        self._latency_ms:  DefaultDict[str, List[float]] = defaultdict(list)
        self._det_counts:  DefaultDict[str, int]         = defaultdict(int)
        self._frame_count: DefaultDict[str, int]         = defaultdict(int)

        for rid in self._robot_ids:
            self.create_subscription(
                String, f"/{rid}/detections",
                lambda msg, r=rid: self._callback(msg, r), 10,
            )

        self.create_timer(5.0, self._print_summary)

    def _callback(self, msg: String, robot_id: str):
        try:
            payload = json.loads(msg.data)
        except json.JSONDecodeError:
            return

        lat_ms = payload.get("inference_time_ms", 0.0)
        n_dets = len(payload.get("detections", []))

        self._latency_ms[robot_id].append(lat_ms)
        self._det_counts[robot_id] += n_dets
        self._frame_count[robot_id] += 1

    def _print_summary(self):
        now = time.strftime("%H:%M:%S")
        self.get_logger().info(f"\n{'='*60}")
        self.get_logger().info(f"Fleet summary @ {now}")
        self.get_logger().info(f"{'Robot':<12}{'Frames':>8}{'Avg lat(ms)':>14}{'Total dets':>12}")
        self.get_logger().info("-" * 46)

        for rid in self._robot_ids:
            frames = self._frame_count[rid]
            lats   = self._latency_ms[rid]
            avg_lat = sum(lats) / len(lats) if lats else 0.0
            dets    = self._det_counts[rid]
            self.get_logger().info(
                f"{rid:<12}{frames:>8}{avg_lat:>14.1f}{dets:>12}"
            )


def main(args=None):
    rclpy.init(args=args)
    node = FleetManager()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
