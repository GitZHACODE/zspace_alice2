#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

mode="${1:-release}"

usage() {
    cat <<'EOF'
Usage:
  ./build.sh           Build release with Clang/Ninja
  ./build.sh release   Build release with Clang/Ninja
  ./build.sh debug     Build debug with Clang/Ninja
  ./build.sh test      Build test sketches with Clang/Ninja
  ./build.sh cuda      Build CUDA configuration with default CMake generator
EOF
}

case "$mode" in
    release|clang)
        preset="clang-release"
        echo "[alice2] Configuring preset: ${preset}"
        cmake --preset "${preset}"
        echo "[alice2] Building preset: ${preset}"
        cmake --build --preset "${preset}"
        ;;
    debug)
        preset="clang-debug"
        echo "[alice2] Configuring preset: ${preset}"
        cmake --preset "${preset}"
        echo "[alice2] Building preset: ${preset}"
        cmake --build --preset "${preset}"
        ;;
    test)
        preset="clang-test-debug"
        echo "[alice2] Configuring preset: ${preset}"
        cmake --preset "${preset}"
        echo "[alice2] Building preset: ${preset}"
        cmake --build --preset "${preset}"
        ;;
    cuda)
        build_dir="build_cuda"
        echo "[alice2] Configuring CUDA build: ${build_dir}"
        cmake -S . -B "${build_dir}" -DCMAKE_BUILD_TYPE=Release -DALICE2_ENABLE_CUDA=ON
        echo "[alice2] Building CUDA configuration"
        cmake --build "${build_dir}" --parallel
        ;;
    -h|--help|help)
        usage
        exit 0
        ;;
    *)
        echo "[alice2] Unknown build mode: ${mode}" >&2
        usage >&2
        exit 2
        ;;
esac

echo "[alice2] Build finished successfully."
