# Multi-Robot Obstacle Avoidance System

## Overview

This project implements a centralized inference pipeline for real-time obstacle avoidance in multi-robot systems. By offloading YOLOv8 object detection to a high-performance centralized server, a fleet of simulated robots can navigate using high-fidelity vision without overwhelming their local processors.

The system operates across three distinct modules: a ROS 2 simulation and control layer, a concurrent gRPC middleware proxy, and a C++ inference backend.

## My Core Contributions (Module 3: ROS 2 Simulation & Control)

I designed and developed the entire robotic simulation, sensor management, and control architecture for this project. My specific engineering contributions include:

* **ROS 2 Node Architecture:** Built the core robot nodes that capture synchronized RGB and depth frames and interface with the centralized inference pipeline.
* **Reactive Control Logic:** Developed an obstacle avoidance controller that subscribes to detection topics and publishes `geometry_msgs/Twist` velocity commands at 10 Hz. The logic dictates that robots reactively scale velocity and steer away from obstacles detected within a 1.5 m 3D danger zone.
* **Gazebo Simulation & Sensor Injection:** Engineered the simulation environment by processing the TurtleBot3 XACRO templates via Python API to dynamically inject a simulated depth camera (`libgazebo_ros_camera.so`) at launch time.
* **Physics Tuning:** Systematically tested ODE physics constraints, establishing that a maximum step size of 0.01 s is strictly required to prevent wheel joint instability during locomotion.
* **Fleet Orchestration & Diagnostics:** Implemented a lightweight `fleet_manager` ROS 2 node that subscribes to all agent topics, continuously aggregating real-time diagnostics like rolling mean inference latency, cumulative frame counts, and total detections.

## Team Members & System Architecture

This was a collaborative effort, decoupled into three distinct microservices.

* **Module 1: AI Inference Server (Shreyas Raviprasad):** A C++ server utilizing ONNX Runtime and YOLOv8n to process frames without Python overhead. It performs depth backprojection using camera intrinsics to convert 2D bounding boxes into 3D world coordinates.
* **Module 2: Middleware Proxy (Suyesh Jadhav):** A C++ proxy using gRPC that maintains a thread pool to dispatch concurrent `Detect` RPCs. This allows all three robots to have frames in flight simultaneously without blocking asynchronous I/O.
* **Module 3: Robot Simulation & Control (Kunal Jindal):** Detailed above.

## Performance Results

The distributed architecture successfully scaled to three concurrent robots operating without shared state. The system maintained a stable throughput of 4 FPS per robot, keeping the mean end-to-end inference latency tightly bounded between 198 ms and 262 ms.
