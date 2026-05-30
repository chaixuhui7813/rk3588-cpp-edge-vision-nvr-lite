#!/usr/bin/env bash
set -euo pipefail

if ! command -v apt-get >/dev/null 2>&1; then
  echo "This helper targets Debian/Ubuntu/RK3588 Debian systems with apt-get."
  exit 1
fi

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
  if command -v sudo >/dev/null 2>&1; then
    SUDO="sudo"
  else
    echo "Run as root or install sudo first."
    exit 1
  fi
fi

$SUDO apt-get update
$SUDO apt-get install -y --no-install-recommends \
  build-essential \
  cmake \
  pkg-config \
  libopencv-dev \
  ca-certificates \
  curl

echo "Base dependencies installed."
echo "RKNN Runtime is board/vendor-specific; copy rknn_api.h and librknnrt.so from your RKNN toolkit if not already installed."
