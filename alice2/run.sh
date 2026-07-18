#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

mode="${1:-release}"

usage() {
    cat <<'EOF'
Usage:
  ./run.sh           Run release Clang/Ninja build
  ./run.sh release   Run release Clang/Ninja build
  ./run.sh debug     Run debug Clang/Ninja build
  ./run.sh test      Run test Clang/Ninja build
  ./run.sh cuda      Run CUDA build
EOF
}

case "$mode" in
    release|clang)
        exe="build/clang-release/bin/alice2"
        ;;
    debug)
        exe="build/clang-debug/bin/alice2"
        ;;
    test)
        exe="build/clang-test-debug/bin/alice2"
        ;;
    cuda)
        exe="build_cuda/bin/alice2"
        ;;
    -h|--help|help)
        usage
        exit 0
        ;;
    *)
        echo "[alice2] Unknown run mode: ${mode}" >&2
        usage >&2
        exit 2
        ;;
esac

if [[ ! -x "${exe}" ]]; then
    echo "[alice2] Executable not found: ${exe}" >&2
    echo "[alice2] Build it first, for example: ./build.sh ${mode}" >&2
    exit 1
fi

echo "[alice2] Launching ${exe}"
exec "${exe}"
