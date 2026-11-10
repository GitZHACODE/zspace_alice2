#!/usr/bin/env python3
"""Prepare pinned sources/SDKs. Python is build tooling, never an app dependency."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request

PROJECT = Path(__file__).resolve().parents[1]
DEPENDS = PROJECT / "depends"
SOURCES = {
    "OpenUSD": ("PixarAnimationStudios/OpenUSD", "363a7c8da8d1937072a5f0989e91faf72eb1fa76", "094727ef5a57e326364f5ed792ffa8c62e53ddc4b9a256e28fc131eb1374bc0d"),
}


def download(url, target, checksum=None):
    print(f"Downloading {url}", flush=True)
    urllib.request.urlretrieve(url, target)
    digest = hashlib.sha256(target.read_bytes()).hexdigest()
    if checksum and digest != checksum:
        raise RuntimeError(f"Checksum mismatch for {url}: {digest}")


def source(name, destination):
    repo, revision, checksum = SOURCES[name]
    marker = destination / ".alice2-revision"
    if marker.exists() and marker.read_text() == revision:
        return destination
    if destination.exists() and any(destination.iterdir()):
        raise RuntimeError(f"Unmanaged or different revision at {destination}; choose another source directory")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=destination.parent) as directory:
        temporary = Path(directory)
        archive = temporary / "source.tar.gz"
        download(f"https://codeload.github.com/{repo}/tar.gz/{revision}", archive, checksum)
        with tarfile.open(archive) as bundle:
            # All pinned archives have a single root. Reject links and traversal.
            for member in bundle.getmembers():
                member_path = (temporary / member.name).resolve()
                if not member_path.is_relative_to(temporary.resolve()) or member.issym() or member.islnk():
                    raise RuntimeError(f"Unsafe archive member: {member.name}")
            bundle.extractall(temporary)
            root = temporary / bundle.getnames()[0].split('/')[0]
        if destination.exists():
            destination.rmdir()
        shutil.move(root, destination)
    marker.write_text(revision)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("component", choices=["usd"])
    parser.add_argument("--source-dir", type=Path)
    parser.add_argument("--prefix", type=Path, default=DEPENDS / "USD_Lite")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--generator", default="Ninja")
    parser.add_argument("--tbb-root", type=Path)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--config", choices=["Release", "Debug", "RelWithDebInfo"], default="Release")
    args = parser.parse_args()
    root = args.source_dir or DEPENDS / "OpenUSD-src"
    # An explicitly supplied checkout is useful for offline SDK builds.
    if not (args.source_dir and (root / "CMakeLists.txt").exists()):
        source("OpenUSD", root)
    # The pinned USD CMake entry has no per-schema switches. This one-line
    # allowlist excludes unused schema families from the monolithic library.
    schema_cmake = root / "pxr/usd/CMakeLists.txt"
    schema_source = schema_cmake.read_text()
    allowlist = 'list(FILTER DIRS INCLUDE REGEX "^(ar|kind|sdf|sdr|pcp|usd|usdGeom|usdShade)$")\n\n'
    if allowlist not in schema_source:
        anchor = "foreach(d ${DIRS})"
        if anchor not in schema_source:
            raise RuntimeError("OpenUSD schema allowlist no longer matches the pinned source")
        schema_cmake.write_text(schema_source.replace(anchor, allowlist + anchor))
    build = args.build_dir or args.prefix.resolve().parent / "OpenUSD-build"
    options = ["PXR_ENABLE_PYTHON_SUPPORT", "PXR_BUILD_IMAGING", "PXR_BUILD_USD_IMAGING",
               "PXR_BUILD_USDVIEW", "PXR_BUILD_EXEC", "PXR_BUILD_USD_VALIDATION",
               "PXR_BUILD_USD_TOOLS", "PXR_BUILD_TESTS", "PXR_BUILD_EXAMPLES",
               "PXR_BUILD_TUTORIALS", "PXR_BUILD_DOCUMENTATION", "PXR_ENABLE_GL_SUPPORT",
               "PXR_ENABLE_METAL_SUPPORT", "PXR_ENABLE_VULKAN_SUPPORT",
               "PXR_BUILD_ALEMBIC_PLUGIN", "PXR_BUILD_DRACO_PLUGIN", "PXR_BUILD_EMBREE_PLUGIN",
               "PXR_BUILD_OPENIMAGEIO_PLUGIN", "PXR_BUILD_OPENCOLORIO_PLUGIN",
               "PXR_BUILD_PRMAN_PLUGIN", "PXR_ENABLE_MATERIALX_SUPPORT",
               "PXR_ENABLE_PTEX_SUPPORT", "PXR_ENABLE_OPENVDB_SUPPORT", "PXR_BUILD_IMAGEIO_PLUGIN"]
    command = ["cmake", "-S", str(root.resolve()), "-B", str(build), "-G", args.generator,
               f"-DCMAKE_BUILD_TYPE={args.config}", f"-DCMAKE_INSTALL_PREFIX={args.prefix.resolve()}",
               "-DPXR_BUILD_MONOLITHIC=ON"] + [f"-D{option}=OFF" for option in options]
    # USD 25.11 assumes a transitive TBB version include. New oneTBB releases
    # removed that include; make it explicit before USD chooses its TBB API.
    force_include = "/FItbb/version.h" if os.name == "nt" else "-include tbb/version.h"
    command += [f"-DCMAKE_CXX_FLAGS={force_include}"]
    if args.tbb_root:
        command += [f"-DCMAKE_PREFIX_PATH={args.tbb_root.resolve()}"]
    subprocess.run(command, check=True)
    subprocess.run(["cmake", "--build", str(build), "--config", args.config,
                    "--parallel", str(args.jobs)], check=True)
    install = ["cmake", "--install", str(build), "--config", args.config]
    if args.config == "Release":
        install.append("--strip")
    subprocess.run(install, check=True)


if __name__ == "__main__":
    main()
