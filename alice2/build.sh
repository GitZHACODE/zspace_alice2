#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
group=base
config=Release
mode=clang
run=false
app=""
for option in "$@"; do
    case "$option" in
        --help) echo 'build.sh --<userSrc-folder> [--debug] [--cuda]'; echo 'run.sh --<folder> [--app=executable_name] [--debug] [--cuda]'; exit 0 ;;
        --debug) config=Debug ;;
        --clang) mode=clang ;;
        --cuda) mode=cuda ;;
        --run) run=true ;;
        --app=*) app="${option#--app=}" ;;
        --*) group="${option#--}" ;;
        *) echo "Unknown argument: $option" >&2; exit 2 ;;
    esac
done
[[ "$group" =~ ^[A-Za-z][A-Za-z0-9_]*$ ]] || { echo 'Invalid group name' >&2; exit 2; }
[[ -f "userSrc/$group/CMakeLists.txt" || "$group" == tests ]] || { echo "No build group: $group" >&2; exit 2; }
if [[ "$mode" == cuda ]]; then
    build_dir=build_cuda
else
    preset="clang-$(echo "$config" | tr '[:upper:]' '[:lower:]')"
    build_dir="build/$preset"
fi
runtime="$build_dir/$config/$group"
if "$run"; then
    if [[ -z "$app" ]]; then
        candidates=()
        [[ -f "$build_dir/groups/$group.txt" ]] || { echo "Build the folder first: ./build.sh --$group" >&2; exit 2; }
        while IFS= read -r name; do
            [[ -n "$name" ]] || continue
            exe="$runtime/$name"
            [[ -f "$exe" ]] || exe="$exe.exe"
            candidates+=("$exe")
        done < "$build_dir/groups/$group.txt"
        [[ ${#candidates[@]} == 1 ]] || { printf '%s\n' "${candidates[@]}"; echo 'Select an app with --app=executable_name.' >&2; exit 2; }
        exe="${candidates[0]}"
    else
        [[ "$app" =~ ^[A-Za-z][A-Za-z0-9_]*$ ]] || exit 2
        grep -Fxq -- "$app" "$build_dir/groups/$group.txt" || { echo "App is not enabled: $app" >&2; exit 2; }
        exe="$runtime/$app"
        [[ -f "$exe" ]] || exe="$exe.exe"
    fi
    exec "$exe"
fi
if [[ "$mode" == cuda ]]; then
    cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE="$config" -DALICE2_ENABLE_CUDA=ON
else
    cmake --preset "$preset"
fi
cmake --build "$build_dir" --config "$config" --target "user_$group" --parallel
echo "Runnable folder: $runtime"
