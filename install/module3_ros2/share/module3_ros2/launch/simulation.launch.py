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
import xacro
from ament_index_python.packages import get_package_share_directory

from launch import LaunchDescription
from launch.actions import (
    DeclareLaunchArgument,
    IncludeLaunchDescription,
    TimerAction,
    SetEnvironmentVariable,
)
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


# Spawn poses for 3 robots (x, y, yaw_deg)
ROBOT_POSES = [
    (0.0,  0.0,  0.0),
    (2.0, -2.0, 90.0),
    (-2.0, 2.0, 180.0),
]

ROBOT_MODEL = 'burger'

# Injected into every burger URDF:
#   1. A large bright-orange box body extension — makes each robot clearly visible
#      in Gazebo regardless of mesh-resolution quirks in Docker/WSL2.
#   2. Camera link + depth sensor plugin.
_DEPTH_CAM_XML = """\

  <!-- Tall orange box mounted on the burger — makes robots easy to see in Gazebo -->
  <link name="body_marker">
    <visual>
      <origin xyz="0 0 0" rpy="0 0 0"/>
      <geometry><box size="0.20 0.20 0.40"/></geometry>
      <material name="orange"><color rgba="1.0 0.4 0.0 1.0"/></material>
    </visual>
    <inertial>
      <mass value="0.001"/>
      <inertia ixx="1e-9" ixy="0" ixz="0" iyy="1e-9" iyz="0" izz="1e-9"/>
    </inertial>
  </link>
  <joint name="body_marker_joint" type="fixed">
    <parent link="base_link"/>
    <child link="body_marker"/>
    <origin xyz="0 0 0.22" rpy="0 0 0"/>
  </joint>

  <link name="camera_link">
    <visual>
      <geometry><box size="0.015 0.04 0.04"/></geometry>
    </visual>
    <inertial>
      <mass value="0.035"/>
      <inertia ixx="1e-6" ixy="0" ixz="0" iyy="1e-6" iyz="0" izz="1e-6"/>
    </inertial>
  </link>
  <joint name="camera_joint" type="fixed">
    <parent link="base_link"/>
    <child link="camera_link"/>
    <origin xyz="0.073 0 0.084" rpy="0 0 0"/>
  </joint>

  <link name="camera_optical_link"/>
  <joint name="camera_optical_joint" type="fixed">
    <parent link="camera_link"/>
    <child link="camera_optical_link"/>
    <origin xyz="0 0 0" rpy="-1.5707963 0 -1.5707963"/>
  </joint>

  <gazebo reference="camera_link">
    <sensor type="depth" name="depth_camera">
      <always_on>true</always_on>
      <update_rate>5</update_rate>
      <camera name="depth_camera">
        <horizontal_fov>1.0472</horizontal_fov>
        <image>
          <width>640</width>
          <height>480</height>
          <format>B8G8R8</format>
        </image>
        <clip><near>0.1</near><far>8.0</far></clip>
        <depth_camera/>
      </camera>
      <plugin name="depth_camera_controller" filename="libgazebo_ros_camera.so">
        <camera_name>camera</camera_name>
        <frame_name>camera_optical_link</frame_name>
        <min_depth>0.1</min_depth>
        <max_depth>8.0</max_depth>
      </plugin>
    </sensor>
  </gazebo>
"""


def _with_depth_camera(urdf: str) -> str:
    """Insert depth camera sensor plugin into a URDF string just before </robot>."""
    head, _, tail = urdf.rpartition('</robot>')
    return head + _DEPTH_CAM_XML + '</robot>' + tail


def generate_launch_description():
    pkg_share = FindPackageShare("module3_ros2")

    tb3_desc = get_package_share_directory('turtlebot3_description')
    urdf_path = os.path.join(tb3_desc, 'urdf', f'turtlebot3_{ROBOT_MODEL}.urdf')
    robot_doc = xacro.process_file(urdf_path, mappings={'namespace': ''})
    robot_description = _with_depth_camera(robot_doc.toxml())

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

        # robot_state_publisher publishes /{robot_id}/robot_description (latched),
        # which spawn_entity.py reads to get the URDF with the depth camera.
        rsp = Node(
            package='robot_state_publisher',
            executable='robot_state_publisher',
            name='robot_state_publisher',
            namespace=robot_id,
            parameters=[{
                'robot_description': robot_description,
                'use_sim_time': True,
            }],
            output='screen',
        )

        spawn = Node(
            package="gazebo_ros",
            executable="spawn_entity.py",
            name=f"spawn_{robot_id}",
            arguments=[
                "-entity",          robot_id,
                "-topic",           f"/{robot_id}/robot_description",
                "-robot_namespace", robot_id,
                "-x", str(rx), "-y", str(ry), "-z", "0.01",
                "-Y", str(ryaw_deg * 3.14159 / 180.0),
            ],
            output="screen",
        )

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

        avoider_node = Node(
            package="module3_ros2",
            executable="obstacle_avoider",
            name=f"obstacle_avoider_{i}",
            namespace=robot_id,
            parameters=[{"robot_id": robot_id}],
            output="screen",
        )

        # RSP starts immediately so the topic is ready before spawn_entity.py runs.
        robot_nodes.append(rsp)
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
        SetEnvironmentVariable("TURTLEBOT3_MODEL", ROBOT_MODEL),
        middleware_addr_arg,
        max_fps_arg,
        gazebo,
        *robot_nodes,
        fleet_manager,
    ])
