#!/usr/bin/env bash
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive

# Keep debian:bullseye base so output binaries link against glibc <= 2.31 (runs on 2020+ Linux distros).
# Debian 11 LTS ended 2026-08-31 and is migrating to archive.debian.org: prefer the official archive
# (with bullseye-security), and fall back to deb.debian.org bullseye main without security if needed.
cat > /etc/apt/sources.list <<'EOF'
deb http://archive.debian.org/debian bullseye main
deb http://archive.debian.org/debian bullseye-updates main
deb http://archive.debian.org/debian-security bullseye-security main
EOF
rm -f /etc/apt/sources.list.d/*

if ! apt-get -o Acquire::Check-Valid-Until=false update </dev/null; then
  echo "archive.debian.org update failed; falling back to deb.debian.org (without bullseye-security)..."
  cat > /etc/apt/sources.list <<'EOF'
deb http://deb.debian.org/debian bullseye main
deb http://deb.debian.org/debian bullseye-updates main
EOF
  apt-get -o Acquire::Check-Valid-Until=false update </dev/null
fi

apt-get -o Acquire::Check-Valid-Until=false install -y --no-install-recommends \
  ca-certificates curl git ninja-build g++ g++-multilib gcc-multilib zip binutils tar gzip </dev/null

curl -fsSL https://github.com/Kitware/CMake/releases/download/v3.28.3/cmake-3.28.3-linux-x86_64.tar.gz | \
  tar -xz -C /usr/local --strip-components=1

cmake -B build-linux -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS='-m32' \
  -DCMAKE_CXX_FLAGS='-m32' \
  -DCMAKE_SHARED_LINKER_FLAGS='-m32' \
  -DSVPLUS_BUILD_SERVER=ON \
  -DSVPLUS_BUILD_CLIENT=OFF \
  -DSVPLUS_WARNINGS_AS_ERRORS=ON
cmake --build build-linux --parallel
