# CSC591-Robotics — Distributed Obstacle Avoidance System

## System Architecture

This project uses a distributed microservices approach where robots offload AI inference to a central server.

```
Gazebo Robots (ROS 2 / Module 3)
    → camera frames
    → Module 2: gRPC Middleware (C++, port 50051)
    → Module 1: ONNX Inference Server (C++, port 50052)
    → bounding boxes + 3D coords back through middleware
    → Twist velocity commands → robots steer away from obstacles
```

**Module 1** — AI inference backend (ONNX Runtime + YOLOv8, C++)
**Module 2** — gRPC middleware proxy with thread pool (C++)
**Module 3** — ROS 2 robot simulation, fleet orchestration, obstacle avoidance (Python + Gazebo)

---

## Prerequisites

### System (WSL2 Ubuntu 20.04)
```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake pkg-config \
  libprotobuf-dev protobuf-compiler \
  libgrpc++-dev libgrpc-dev protobuf-compiler-grpc \
  libgtest-dev \
  python3-pip curl wget
```

### Docker (for Module 3 / ROS 2)
Install Docker Desktop for Windows: https://www.docker.com/products/docker-desktop

### GUI (WSLg — for Gazebo)
In Windows PowerShell (as Administrator):
```powershell
wsl --update
wsl --shutdown
```
Reopen WSL2. Verify WSLg is working:
```bash
echo $DISPLAY   # should print :0
ls /mnt/wslg    # should list files
```

---

## One-Time Setup

### 1. Clone the repo
```bash
git clone <repo-url>
cd CSC591-Robotics
```

### 2. Install ONNX Runtime
```bash
cd /tmp
wget https://github.com/microsoft/onnxruntime/releases/download/v1.18.1/onnxruntime-linux-x64-1.18.1.tgz
tar -xzf onnxruntime-linux-x64-1.18.1.tgz
sudo cp -r onnxruntime-linux-x64-1.18.1/include/* /usr/local/include/
sudo cp -r onnxruntime-linux-x64-1.18.1/lib/*     /usr/local/lib/
sudo ldconfig
cd -
```

### 3. Install Google Test
```bash
cd /usr/src/gtest && sudo cmake . && sudo make
sudo cp lib/*.a /usr/lib
cd -
```

### 4. Download YOLOv8 ONNX model
```bash
cd ~/CSC591-Robotics
pip3 install ultralytics onnx
yolo export model=yolov8n.pt format=onnx
```

### 5. Build Module 1 (Inference Server)
```bash
cd module1_inference
mkdir build && cd build
cmake ..
make -j$(nproc)
cd ../..
```

### 6. Build Module 2 (Middleware)
```bash
cd module2_middleware
mkdir build && cd build
cmake ..
make -j$(nproc)
cd ../..
```

### 7. Set up Docker container (Module 3)
```bash
docker run -it --rm \
  -v ~/CSC591-Robotics:/workspace \
  -e DISPLAY=$DISPLAY \
  -v /tmp/.X11-unix:/tmp/.X11-unix \
  -v /mnt/wslg:/mnt/wslg \
  -e WAYLAND_DISPLAY=$WAYLAND_DISPLAY \
  -v /mnt/wslg/runtime-dir:/run/user/1000 \
  osrf/ros:humble-desktop \
  bash
```

Inside the container (run these once):
```bash
# Prevent colcon from scanning system packages
touch /usr/lib/python3/dist-packages/COLCON_IGNORE
touch /usr/local/lib/python3.10/dist-packages/COLCON_IGNORE

# Install dependencies
apt-get update && apt-get install -y \
  python3-colcon-common-extensions \
  ros-humble-gazebo-ros-pkgs \
  ros-humble-turtlebot3-description \
  ros-humble-turtlebot3-gazebo

curl https://bootstrap.pypa.io/get-pip.py -o get-pip.py && python3 get-pip.py
pip3 install setuptools==58.2.0 grpcio grpcio-tools

# Generate gRPC Python stubs
cd /workspace
python3 -m grpc_tools.protoc \
  -I proto \
  --python_out=module3_ros2/module3_ros2 \
  --grpc_python_out=module3_ros2/module3_ros2 \
  proto/inference.proto

# Fix generated stub import (must use relative import inside package)
sed -i 's/^import inference_pb2/from . import inference_pb2/' \
  module3_ros2/module3_ros2/inference_pb2_grpc.py

# Build ROS 2 package
source /opt/ros/humble/setup.bash
colcon build --packages-select module3_ros2
source install/setup.bash
```

---

## Running the System

You need **3 terminals**. Modules 1 and 2 run on the WSL2 host; Module 3 runs inside Docker.

### Terminal 1 — Inference Server (WSL2 host)
```bash
cd ~/CSC591-Robotics
./module1_inference/build/inference_server yolov8n.onnx
```
Expected output:
```
[InferenceServer] Model loaded. Input: images
[InferenceServer] Listening on 0.0.0.0:50052
```

### Terminal 2 — Middleware (WSL2 host)
```bash
cd ~/CSC591-Robotics
./module2_middleware/build/middleware_server 0.0.0.0:50051 localhost:50052
```
Expected output:
```
[Middleware] Listening on 0.0.0.0:50051
```

### Terminal 3 — Simulation (Docker container)
Get a shell in the running container:
```bash
docker exec -it $(docker ps -q) bash
```
Then:
```bash
source /opt/ros/humble/setup.bash
source /workspace/install/setup.bash
export TURTLEBOT3_MODEL=burger
ros2 launch module3_ros2 simulation.launch.py
```

Gazebo will open with 3 TurtleBot3 robots in an obstacle arena. The fleet manager prints inference latency and detection counts every 5 seconds.

---

## Running Tests

### Module 1 (Google Test)
```bash
cd module1_inference/build
ctest --output-on-failure
# Or with a real model:
YOLO_MODEL_PATH=~/CSC591-Robotics/yolov8n.onnx ctest --output-on-failure
```

### Module 2 (Google Test)
```bash
cd module2_middleware/build
ctest --output-on-failure
```

---

## Launch Arguments

The simulation launch file accepts optional arguments:

```bash
ros2 launch module3_ros2 simulation.launch.py \
  middleware_addr:=localhost:50051 \
  max_fps:=5.0
```

| Argument | Default | Description |
|---|---|---|
| `middleware_addr` | `localhost:50051` | Address of Module 2 middleware |
| `max_fps` | `5.0` | Max inference frames per second per robot |

---

## Project Structure

```
CSC591-Robotics/
├── proto/
│   └── inference.proto          # Shared gRPC schema
├── module1_inference/
│   ├── include/                 # ModelRunner, InferenceServer headers
│   ├── src/                     # ONNX inference + gRPC service impl
│   ├── tests/                   # Google Test unit tests
│   └── CMakeLists.txt
├── module2_middleware/
│   ├── include/                 # ThreadPool, MiddlewareServer headers
│   ├── src/                     # Proxy server + thread pool impl
│   ├── tests/                   # Google Test unit tests
│   └── CMakeLists.txt
└── module3_ros2/
    ├── module3_ros2/
    │   ├── robot_node.py        # Camera capture + gRPC client
    │   ├── obstacle_avoider.py  # Velocity command generation
    │   ├── fleet_manager.py     # Diagnostic aggregator
    │   └── grpc_client.py       # gRPC stub wrapper
    ├── launch/
    │   └── simulation.launch.py # Full system launch
    └── worlds/
        └── obstacle_world.world # Gazebo arena with obstacles
```
