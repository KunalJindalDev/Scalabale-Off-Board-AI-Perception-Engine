#!/usr/bin/env bash
GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info() { echo -e "${GREEN}[+]${NC} $*"; }
warn() { echo -e "${YELLOW}[!]${NC} $*"; }

info "Stopping ros2_sim container..."
docker stop ros2_sim 2>/dev/null && warn "  stopped ros2_sim" || true

info "Stopping inference server..."
pkill -f "inference_server" 2>/dev/null && warn "  killed inference_server" || true

info "Stopping middleware server..."
pkill -f "middleware_server" 2>/dev/null && warn "  killed middleware_server" || true

info "Done."
