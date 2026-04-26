#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="/tmp/csc591"
mkdir -p "$LOG_DIR"

GREEN='\033[0;32m'; YELLOW='\033[1;33m'; RED='\033[0;31m'; NC='\033[0m'
info()  { echo -e "${GREEN}[+]${NC} $*"; }
warn()  { echo -e "${YELLOW}[!]${NC} $*"; }
die()   { echo -e "${RED}[x]${NC} $*" >&2; exit 1; }

# ─── 1. Stop existing services ───────────────────────────────────────────────

info "Stopping any running ros2_sim container..."
docker stop ros2_sim 2>/dev/null && warn "  stopped ros2_sim" || true

info "Killing any stale inference/middleware processes..."
pkill -f "inference_server" 2>/dev/null && warn "  killed inference_server" || true
pkill -f "middleware_server" 2>/dev/null && warn "  killed middleware_server" || true

# Give ports a moment to free
sleep 1

# ─── 2. Verify binaries and model exist ──────────────────────────────────────

[[ -x "$SCRIPT_DIR/module1_inference/build/inference_server" ]] \
    || die "inference_server binary not found. Run: cd module1_inference/build && cmake .. && make"

[[ -x "$SCRIPT_DIR/module2_middleware/build/middleware_server" ]] \
    || die "middleware_server binary not found. Run: cd module2_middleware/build && cmake .. && make"

[[ -f "$SCRIPT_DIR/yolov8n.onnx" ]] \
    || die "yolov8n.onnx not found. Run: yolo export model=yolov8n.pt format=onnx"

# ─── 3. Start Module 1 — Inference Server (port 50052) ───────────────────────

info "Starting Module 1 — Inference Server..."
"$SCRIPT_DIR/module1_inference/build/inference_server" \
    "$SCRIPT_DIR/yolov8n.onnx" \
    > "$LOG_DIR/module1.log" 2>&1 &
M1_PID=$!
echo $M1_PID > "$LOG_DIR/module1.pid"

info "  Waiting for port 50052..."
for i in $(seq 1 30); do
    ss -tlnp 2>/dev/null | grep -q ':50052' && break
    sleep 1
    [[ $i -eq 30 ]] && die "Inference server failed to start. Check $LOG_DIR/module1.log"
done
info "  Module 1 up (pid $M1_PID)"

# ─── 4. Start Module 2 — Middleware Server (port 50051) ──────────────────────

info "Starting Module 2 — Middleware Server..."
"$SCRIPT_DIR/module2_middleware/build/middleware_server" \
    "0.0.0.0:50051" "localhost:50052" \
    > "$LOG_DIR/module2.log" 2>&1 &
M2_PID=$!
echo $M2_PID > "$LOG_DIR/module2.pid"

info "  Waiting for port 50051..."
for i in $(seq 1 15); do
    ss -tlnp 2>/dev/null | grep -q ':50051' && break
    sleep 1
    [[ $i -eq 15 ]] && die "Middleware server failed to start. Check $LOG_DIR/module2.log"
done
info "  Module 2 up (pid $M2_PID)"

# ─── 5. Detect WSL2 host IP ──────────────────────────────────────────────────

WSL_IP=$(ip addr show eth0 2>/dev/null | awk '/inet / {print $2}' | cut -d/ -f1 | head -1)
[[ -n "$WSL_IP" ]] || WSL_IP=$(hostname -I | awk '{print $1}')
[[ -n "$WSL_IP" ]] || die "Could not determine WSL2 host IP"
info "WSL2 host IP: $WSL_IP"

# ─── 6. Start Module 3 — ROS2 Simulation (Docker) ────────────────────────────

info "Starting Module 3 — ROS2 Simulation in Docker..."
docker run -d --rm \
    --name ros2_sim \
    -v "$SCRIPT_DIR":/workspace \
    -e DISPLAY="$DISPLAY" \
    -v /tmp/.X11-unix:/tmp/.X11-unix \
    -v /mnt/wslg:/mnt/wslg \
    -e WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-}" \
    -v /mnt/wslg/runtime-dir:/run/user/1000 \
    --add-host=wsl-host:"$WSL_IP" \
    osrf/ros:humble-desktop \
    bash -c "
        touch /usr/lib/python3/dist-packages/COLCON_IGNORE 2>/dev/null || true
        touch /usr/local/lib/python3.10/dist-packages/COLCON_IGNORE 2>/dev/null || true
        apt-get update -qq && apt-get install -y -qq \
            python3-colcon-common-extensions \
            ros-humble-gazebo-ros-pkgs \
            ros-humble-turtlebot3-description \
            ros-humble-turtlebot3-gazebo \
            ros-humble-robot-state-publisher \
            ros-humble-cv-bridge \
            ros-humble-vision-opencv \
            python3-numpy \
            python3-opencv 2>&1 | tail -3
        pip3 install -q setuptools==58.2.0 grpcio grpcio-tools
        cd /workspace
        python3 -m grpc_tools.protoc \
            -I proto \
            --python_out=module3_ros2/module3_ros2 \
            --grpc_python_out=module3_ros2/module3_ros2 \
            proto/inference.proto
        sed -i 's/^import inference_pb2/from . import inference_pb2/' \
            module3_ros2/module3_ros2/inference_pb2_grpc.py
        source /opt/ros/humble/setup.bash
        colcon build --packages-select module3_ros2 2>&1
        source install/setup.bash
        export TURTLEBOT3_MODEL=burger
        ros2 launch module3_ros2 simulation.launch.py middleware_addr:=wsl-host:50051
    " 2>&1

info "  Docker container started (ros2_sim)"
info ""
info "System is starting up. Gazebo will open in ~30-60s."
info ""
info "Logs:"
info "  Module 1 : $LOG_DIR/module1.log"
info "  Module 2 : $LOG_DIR/module2.log"
info "  Module 3 : docker logs ros2_sim"
info ""
info "To stop everything: ./stop.sh"
