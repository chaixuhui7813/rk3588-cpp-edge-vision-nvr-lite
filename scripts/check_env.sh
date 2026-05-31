#!/usr/bin/env bash
set -u

echo "== RK3588 edge vision environment check =="
echo "arch: $(uname -m)"
echo "kernel: $(uname -sr)"

echo
echo "-- compiler --"
if command -v g++ >/dev/null 2>&1; then g++ --version | head -n 1; else echo "g++: missing"; fi
if command -v cmake >/dev/null 2>&1; then cmake --version | head -n 1; else echo "cmake: missing"; fi

echo
echo "-- OpenCV --"
if pkg-config --exists opencv4; then
  echo "opencv4: $(pkg-config --modversion opencv4)"
elif pkg-config --exists opencv; then
  echo "opencv: $(pkg-config --modversion opencv)"
else
  echo "OpenCV pkg-config: missing"
  echo "Install hint: bash scripts/install_deps_debian.sh"
fi

echo
echo "-- devices --"
for dev in /dev/rknpu /dev/dri; do
  if [ -e "$dev" ]; then ls -ld "$dev"; else echo "$dev: missing"; fi
done

echo
echo "-- RKNN Runtime --"
if ldconfig -p 2>/dev/null | grep -q librknnrt; then
  ldconfig -p | grep librknnrt
else
  find /usr /usr/local /opt -name 'librknnrt.so*' 2>/dev/null | head -n 10 || true
fi
find /usr /usr/local /opt -name 'rknn_api.h' 2>/dev/null | head -n 10 || true

echo
echo "-- resources --"
df -h .
free -h || true

echo
echo "Done. Missing RKNN files are OK for --mock builds; real NPU mode needs rknn_api.h and librknnrt.so."
