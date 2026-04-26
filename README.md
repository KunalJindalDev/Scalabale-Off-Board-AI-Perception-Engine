# CSC591-Robotics — Distributed Obstacle Avoidance System

## System Architecture

This project uses a distributed microservices approach where robots offload AI inference to a central server.

```
Gazebo Robots (ROS 2 / Module 3)
    → RGB + depth camera frames
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

### System (WSL2 Ubuntu 22.04)
```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake pkg-config \
  libprotobuf-dev protobuf-compiler \
  libgrpc++-dev libgrpc-dev protobuf-compiler-grpc \
  libgtest-dev \
  python3-pip curl wget
```

### Docker
Install Docker Desktop for Windows: https://www.docker.com/products/docker-desktop

Pull the ROS 2 image (one-time):
```bash
docker pull osrf/ros:humble-desktop
```

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

---

## Running the System

### One-command start (recommended)

From the project root:
```bash
./start.sh
```

This script automatically:
1. Stops any existing `ros2_sim` Docker container and stale server processes
2. Starts the inference server (Module 1) and waits for port 50052
3. Starts the middleware server (Module 2) and waits for port 50051
4. Detects the WSL2 host IP dynamically
5. Launches the Docker container with the full ROS 2 simulation (Module 3)

Gazebo opens in ~30–60 seconds. To stop everything:
```bash
./stop.sh
```

Logs:
- Module 1: `/tmp/csc591/module1.log`
- Module 2: `/tmp/csc591/module2.log`
- Module 3: `docker logs ros2_sim`

---

### Manual start (3 terminals)

Modules 1 and 2 run on the WSL2 host; Module 3 runs inside Docker.

#### Terminal 1 — Inference Server (WSL2 host)
```bash
cd ~/CSC591-Robotics
./module1_inference/build/inference_server yolov8n.onnx
```
Expected output:
```
[InferenceServer] Model loaded. Input: images
[InferenceServer] Listening on 0.0.0.0:50052
```

#### Terminal 2 — Middleware (WSL2 host)
```bash
cd ~/CSC591-Robotics
./module2_middleware/build/middleware_server 0.0.0.0:50051 localhost:50052
```
Expected output:
```
[Middleware] Listening on 0.0.0.0:50051
```

#### Terminal 3 — Simulation (Docker)
```bash
WSL_IP=$(ip addr show eth0 | awk '/inet / {print $2}' | cut -d/ -f1)

docker run -it --rm \
  --name ros2_sim \
  -v ~/CSC591-Robotics:/workspace \
  -e DISPLAY=$DISPLAY \
  -v /tmp/.X11-unix:/tmp/.X11-unix \
  -v /mnt/wslg:/mnt/wslg \
  -e WAYLAND_DISPLAY=$WAYLAND_DISPLAY \
  -v /mnt/wslg/runtime-dir:/run/user/1000 \
  --add-host=wsl-host:${WSL_IP} \
  osrf/ros:humble-desktop bash
```

Inside the container:
```bash
touch /usr/lib/python3/dist-packages/COLCON_IGNORE
touch /usr/local/lib/python3.10/dist-packages/COLCON_IGNORE

apt-get update && apt-get install -y \
  python3-colcon-common-extensions \
  ros-humble-gazebo-ros-pkgs \
  ros-humble-turtlebot3-description \
  ros-humble-turtlebot3-gazebo \
  ros-humble-robot-state-publisher \
  ros-humble-cv-bridge \
  ros-humble-vision-opencv \
  python3-numpy python3-opencv

curl https://bootstrap.pypa.io/get-pip.py -o get-pip.py && python3 get-pip.py
pip3 install setuptools==58.2.0 grpcio grpcio-tools

cd /workspace
python3 -m grpc_tools.protoc \
  -I proto \
  --python_out=module3_ros2/module3_ros2 \
  --grpc_python_out=module3_ros2/module3_ros2 \
  proto/inference.proto

sed -i 's/^import inference_pb2/from . import inference_pb2/' \
  module3_ros2/module3_ros2/inference_pb2_grpc.py

source /opt/ros/humble/setup.bash
colcon build --packages-select module3_ros2
source install/setup.bash
export TURTLEBOT3_MODEL=burger
ros2 launch module3_ros2 simulation.launch.py middleware_addr:=wsl-host:50051
```

---

## Running Tests

### Module 1 (Google Test)
```bash
cd module1_inference/build
ctest --output-on-failure
# With a real model:
YOLO_MODEL_PATH=~/CSC591-Robotics/yolov8n.onnx ctest --output-on-failure
```

### Module 2 (Google Test)
```bash
cd module2_middleware/build
ctest --output-on-failure
```

---

## Robot Behaviour

3 TurtleBot3 Burger robots spawn in a 12×12 m arena containing 5 `person_standing` obstacles (COCO class 0, reliably detected by YOLOv8) and 2 cylindrical pillars.

Each robot independently:
1. **Drives straight** at cruise speed when no obstacle is within `DANGER_ZONE_M` (1.5 m)
2. **Turns away** from the nearest detected person — right if the person is left of centre, left if right of centre
3. **Stops and rotates** if an obstacle is directly ahead (centre ±25% of frame)
4. **Recovery-spins** if stuck for more than 3 seconds

Inference happens at up to 5 fps per robot. The fleet manager prints latency and detection counts every 5 seconds.

### Tunable parameters (`obstacle_avoider.py`)

| Parameter | Default | Description |
|---|---|---|
| `LINEAR_SPEED` | 40.0 m/s | Cruise speed |
| `ANGULAR_SPEED` | 1.0 rad/s | Turning speed |
| `DANGER_ZONE_M` | 1.5 m | Distance at which avoidance triggers |
| `RECOVERY_TIMEOUT_S` | 3.0 s | Seconds before recovery spin |

---

## Launch Arguments

```bash
ros2 launch module3_ros2 simulation.launch.py \
  middleware_addr:=wsl-host:50051 \
  max_fps:=5.0
```

| Argument | Default | Description |
|---|---|---|
| `middleware_addr` | `localhost:50051` | Address of Module 2 middleware |
| `max_fps` | `5.0` | Max inference frames per second per robot |

---

## Simulation Notes

- **Physics**: ODE at 100 Hz, `max_step_size=0.01 s`, `real_time_factor=0` (uncapped — runs as fast as hardware allows). Step sizes above ~0.02 s cause ODE instability for differential-drive robots (wobbling in place).
- **Depth camera**: injected into the TurtleBot3 URDF at launch time using `libgazebo_ros_camera.so` (the ROS 2 Humble equivalent of the legacy `openni_kinect` plugin). Publishes `/robot_N/camera/image_raw` and `/robot_N/camera/depth/image_raw`.
- **Obstacles**: `person_standing` Gazebo models downloaded from the model server on first run (internet access required inside the container).
- **Real-time factor**: visible in the Gazebo status bar. Values >1.0 mean the simulation runs faster than wall-clock time.

---

## Project Structure

```
CSC591-Robotics/
├── start.sh                         # One-command system start
├── stop.sh                          # One-command system stop
├── proto/
│   └── inference.proto              # Shared gRPC schema
├── module1_inference/
│   ├── include/                     # ModelRunner, InferenceServer headers
│   ├── src/                         # ONNX inference + gRPC service impl
│   ├── tests/                       # Google Test unit tests
│   └── CMakeLists.txt
├── module2_middleware/
│   ├── include/                     # ThreadPool, MiddlewareServer headers
│   ├── src/                         # Proxy server + thread pool impl
│   ├── tests/                       # Google Test unit tests
│   └── CMakeLists.txt
└── module3_ros2/
    ├── module3_ros2/
    │   ├── robot_node.py            # Camera capture + gRPC client
    │   ├── obstacle_avoider.py      # Velocity command generation
    │   ├── fleet_manager.py         # Diagnostic aggregator
    │   └── grpc_client.py           # gRPC stub wrapper
    ├── launch/
    │   └── simulation.launch.py     # Full system launch (injects depth camera)
    └── worlds/
        └── obstacle_world.world     # 12×12 m Gazebo arena
```
