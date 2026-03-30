"""
simulation.launch.py

Launches:
  - Gazebo with obstacle_world.world
  - 3 differential-drive robots (TurtleBot3-style) via spawn_entity
  - robot_node  (×3) – frame capture + gRPC inference client
  - obstacle_avoider (×3) – velocity command generation
  - fleet_manager   (×1) – diagnostic aggregator

Arguments (override with `ros2 launch module3_ros2 simulation.launch.py arg:=value`):
  middleware_addr  – address of the Module 2 middleware  [default: localhost:50051]
  max_fps          – max inference frames per second per robot [default: 5.0]
  num_robots       – number of robots to spawn [default: 3]
"""

import os
from launch import LaunchDescription
from launch.actions import (
    DeclareLaunchArgument,
    IncludeLaunchDescription,
    GroupAction,
    TimerAction,
    SetEnvironmentVariable,
)
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch_ros.actions import Node, PushRosNamespace
from launch_ros.substitutions import FindPackageShare
import os


# Spawn poses for 3 robots (x, y, yaw_deg)
ROBOT_POSES = [
    (0.0,  0.0,  0.0),
    (2.0, -2.0, 90.0),
    (-2.0, 2.0, 180.0),
]


def generate_launch_description():
    pkg_share = FindPackageShare("module3_ros2")

    # ----------------------------------------------------------------
    # Declared arguments
    # ----------------------------------------------------------------
    middleware_addr_arg = DeclareLaunchArgument(
        "middleware_addr", default_value="localhost:50051",
        description="Address of the Module 2 middleware gRPC server",
    )
    max_fps_arg = DeclareLaunchArgument(
        "max_fps", default_value="5.0",
        description="Maximum inference frames per second per robot",
    )

    middleware_addr = LaunchConfiguration("middleware_addr")
    max_fps         = LaunchConfiguration("max_fps")

    # ----------------------------------------------------------------
    # Gazebo
    # ----------------------------------------------------------------
    world_file = PathJoinSubstitution([pkg_share, "worlds", "obstacle_world.world"])

    gazebo = IncludeLaunchDescription(
        PythonLaunchDescriptionSource([
            PathJoinSubstitution([
                FindPackageShare("gazebo_ros"), "launch", "gazebo.launch.py"
            ])
        ]),
        launch_arguments={"world": world_file, "verbose": "false"}.items(),
    )

    # ----------------------------------------------------------------
    # Per-robot nodes
    # ----------------------------------------------------------------
    robot_nodes = []

    for i, (rx, ry, ryaw_deg) in enumerate(ROBOT_POSES):
        robot_id = f"robot_{i}"

        # Spawn entity into Gazebo using TurtleBot3 burger model
        tb3_urdf = PathJoinSubstitution([
            FindPackageShare("turtlebot3_description"), "urdf", "turtlebot3_burger.urdf"
        ])
        spawn = Node(
            package="gazebo_ros",
            executable="spawn_entity.py",
            name=f"spawn_{robot_id}",
            arguments=[
                "-entity",  robot_id,
                "-file",    tb3_urdf,
                "-robot_namespace", robot_id,
                "-x", str(rx), "-y", str(ry), "-z", "0.01",
                "-Y", str(ryaw_deg * 3.14159 / 180.0),
            ],
            output="screen",
        )

        # Inference client node
        robot_node = Node(
            package="module3_ros2",
            executable="robot_node",
            name=f"robot_node_{i}",
            namespace=robot_id,
            parameters=[{
                "robot_id":        robot_id,
                "middleware_addr": middleware_addr,
                "max_fps":         max_fps,
            }],
            output="screen",
        )

        # Obstacle avoidance node
        avoider_node = Node(
            package="module3_ros2",
            executable="obstacle_avoider",
            name=f"obstacle_avoider_{i}",
            namespace=robot_id,
            parameters=[{"robot_id": robot_id}],
            output="screen",
        )

        # Delay spawn slightly so Gazebo is ready
        robot_nodes.append(TimerAction(period=3.0 + i * 1.5, actions=[spawn]))
        robot_nodes.append(TimerAction(period=5.0 + i * 1.5, actions=[robot_node]))
        robot_nodes.append(TimerAction(period=5.0 + i * 1.5, actions=[avoider_node]))

    # ----------------------------------------------------------------
    # Fleet manager
    # ----------------------------------------------------------------
    fleet_manager = TimerAction(
        period=7.0,
        actions=[Node(
            package="module3_ros2",
            executable="fleet_manager",
            name="fleet_manager",
            parameters=[{"robot_ids": [f"robot_{i}" for i in range(len(ROBOT_POSES))]}],
            output="screen",
        )],
    )

    return LaunchDescription([
        SetEnvironmentVariable("TURTLEBOT3_MODEL", "burger"),
        middleware_addr_arg,
        max_fps_arg,
        gazebo,
        *robot_nodes,
        fleet_manager,
    ])
