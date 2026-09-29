#!/bin/bash

set -e

VERSION="1.0.0"

HELP_TEXT_HEADER="\
MWNet Merged Branch Build Script ($VERSION)
Custom build script for MWNet 1.0.0 based on OpenMW 0.52
Based on MWNet-deploy by Grim Kriegor
"

HELP_TEXT_BODY="\
Usage: $0 MODE [OPTIONS]

Modes of operation:
  -i, --install                  Build MWNet merged branch and dependencies
  -r, --rebuild                  Rebuild MWNet merged branch
  -c, --clean                    Clean build directory
  -h, --help                     This help text

Options:
  -s, --server-only              Only build the server
  -j, --cores N                  Use N cores for building (default: all available)
  --skip-deps                    Use installed dependencies instead of the pinned fetch fallback
  --skip-pkgs                    Skip package installation
  --cmake-local                  Tell CMake to look in /usr/local/ for libraries

This script builds the MWNet checkout containing this script.

The merged branch combines:
  - MWNet multiplayer client, dedicated server and scripting API
  - OpenMW 0.52 modernized engine codebase
"

SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"
PROJECT_DIR="$SCRIPT_DIR"

echo -e "$HELP_TEXT_HEADER"

# Parse arguments
INSTALL=false
REBUILD=false
CLEAN=false
SERVER_ONLY=false
SKIP_DEPS=false
SKIP_PACKAGE_INSTALL=false
CMAKE_LOCAL=false
ARG_CORES=""

if [ $# -eq 0 ]; then
  echo -e "$HELP_TEXT_BODY"
  exit 0
fi

while [[ $# -gt 0 ]]; do
  case $1 in
    -i|--install)
      INSTALL=true
      ;;
    -r|--rebuild)
      REBUILD=true
      ;;
    -c|--clean)
      CLEAN=true
      ;;
    -h|--help)
      echo -e "$HELP_TEXT_BODY"
      exit 0
      ;;
    -s|--server-only)
      SERVER_ONLY=true
      ;;
    -j|--cores)
      ARG_CORES="$2"
      shift
      ;;
    --skip-deps)
      SKIP_DEPS=true
      ;;
    --skip-pkgs)
      SKIP_PACKAGE_INSTALL=true
      ;;
    --cmake-local)
      CMAKE_LOCAL=true
      ;;
    *)
      echo "Unknown option: $1"
      echo -e "$HELP_TEXT_BODY"
      exit 1
      ;;
  esac
  shift
done

# Determine number of cores
if [[ "$ARG_CORES" == "" || "$ARG_CORES" == "0" ]]; then
    CORES="$(nproc)"
else
    CORES="$ARG_CORES"
fi

echo -e "\nUsing $CORES CPU cores for compilation"

# Folder hierarchy
BASE="$PROJECT_DIR"
CODE="$BASE"
BUILD_DIR="$BASE/build"

# Distro identification
if command -v lsb_release &> /dev/null; then
    DISTRO="$(lsb_release -si | awk '{print tolower($0)}')"
    DISTROCODE="$(lsb_release -sc | awk '{print tolower($0)}')"
else
    DISTRO="unknown"
    DISTROCODE="unknown"
fi

echo -e "Detected distribution: $DISTRO $DISTROCODE"

if [ "$CMAKE_LOCAL" = true ]; then
  export PATH=/usr/local/bin:$PATH
  export LD_LIBRARY_PATH=/usr/local/lib64:/usr/local/lib:"$LD_LIBRARY_PATH"
fi

# Clean build
if [ "$CLEAN" = true ]; then
  echo -e "\n>> Cleaning build directory"
  rm -rf "$BUILD_DIR"
  echo -e "Build directory cleaned. Run with --install to rebuild."
  exit 0
fi

# Install dependencies
if [[ $INSTALL == true && $SKIP_PACKAGE_INSTALL == false ]]; then
  echo -e "\n>> Installing system packages"
  
  case $DISTRO in
    ubuntu|debian|linuxmint|pop)
      echo -e "Installing packages for Debian/Ubuntu-based system"
      sudo apt update
      sudo apt install -y \
        build-essential \
        cmake \
        git \
        libboost-all-dev \
        libsdl2-dev \
        qtbase5-dev \
        qttools5-dev \
        libqt5opengl5-dev \
        libopenscenegraph-dev \
        libavcodec-dev \
        libavformat-dev \
        libavutil-dev \
        libswscale-dev \
        libswresample-dev \
        libmygui-dev \
        libbullet-dev \
        libopenal-dev \
        libunshield-dev \
        liblz4-dev \
        libluajit-5.1-dev \
        libncurses-dev \
        libtinyxml-dev \
        librecast-dev \
        libyaml-cpp-dev \
        libcollada-dom-dev \
        libsqlite3-dev \
        libqt5svg5-dev \
        libsodium-dev \
        libssl-dev \
        libprotobuf-dev \
        protobuf-compiler
      ;;
    
    arch|manjaro)
      echo -e "Installing packages for Arch-based system"
      sudo pacman -Sy --needed \
        base-devel \
        cmake \
        git \
        boost \
        sdl2 \
        qt5-base \
        openscenegraph \
        ffmpeg \
        mygui \
        bullet \
        openal \
        libunshield \
        lz4 \
        luajit \
        ncurses \
        tinyxml \
        libsodium \
        openssl \
        protobuf
      ;;
    
    fedora|rhel|centos)
      echo -e "Installing packages for Fedora/RHEL-based system"
      sudo dnf install -y \
        gcc-c++ \
        cmake \
        git \
        boost-devel \
        SDL2-devel \
        qt5-qtbase-devel \
        OpenSceneGraph-devel \
        ffmpeg-devel \
        mygui-devel \
        bullet-devel \
        openal-soft-devel \
        libunshield-devel \
        lz4-devel \
        luajit-devel \
        ncurses-devel \
        tinyxml-devel \
        libsodium-devel \
        openssl-devel \
        protobuf-devel \
        protobuf-compiler
      ;;
    
    *)
      echo -e "\nWarning: Unknown distribution. Please install dependencies manually."
      echo -e "See BUILD_INSTRUCTIONS.md for the list of required packages."
      ;;
  esac
fi

# Build MWNet
if [ $INSTALL == true ] || [ $REBUILD == true ]; then
  echo -e "\n>> Building MWNet merged branch"
  
  mkdir -p "$BUILD_DIR"
  cd "$BUILD_DIR"
  
  # CMake parameters for MWNet 1.0.0 (OpenMW 0.52 base)
  CMAKE_PARAMS="-Wno-dev \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_CXX_STANDARD=20 \
      -DBUILD_OPENCS=OFF"

  if [ "$SKIP_DEPS" = false ]; then
    CMAKE_PARAMS="$CMAKE_PARAMS -DMWNET_FETCH_DEPS=ON"
  fi
  
  if [ "$SERVER_ONLY" = true ]; then
    echo -e "Building server-only configuration"
    CMAKE_PARAMS="$CMAKE_PARAMS \
      -DBUILD_OPENMW_MP=ON \
      -DBUILD_OPENCS=OFF \
      -DBUILD_BSATOOL=OFF \
      -DBUILD_ESMTOOL=OFF \
      -DBUILD_ESSIMPORTER=OFF \
      -DBUILD_LAUNCHER=OFF \
      -DBUILD_MWINIIMPORTER=OFF \
      -DBUILD_OPENMW=OFF \
      -DBUILD_NIFTEST=OFF \
      -DBUILD_WIZARD=OFF"
  else
    echo -e "Building full client and server"
    CMAKE_PARAMS="$CMAKE_PARAMS \
      -DBUILD_OPENMW=ON \
      -DBUILD_OPENMW_MP=ON \
      -DBUILD_LAUNCHER=ON"
  fi
  
  if [ "$CMAKE_LOCAL" = true ]; then
    CMAKE_PARAMS="$CMAKE_PARAMS \
      -DCMAKE_LIBRARY_PATH=/usr/local/lib64"
  fi
  
  echo -e "\n>> CMake configuration:"
  echo -e "$CMAKE_PARAMS\n"
  
  # Run CMake
  cmake "$CODE" $CMAKE_PARAMS
  
  # Build
  echo -e "\n>> Compiling with $CORES cores..."
  set -o pipefail
  make -j$CORES 2>&1 | tee "$BASE/build.log"
  
  echo -e "\n=========================================="
  echo -e "Build completed successfully!"
  echo -e "=========================================="
  echo -e "\nExecutables are located in: $BUILD_DIR"
  echo -e "\nAvailable executables:"
  [ -f "$BUILD_DIR/mwnet" ] && echo -e "  - mwnet (client)"
  [ -f "$BUILD_DIR/mwnet-server" ] && echo -e "  - mwnet-server"
  [ -f "$BUILD_DIR/openmw-launcher" ] && echo -e "  - openmw-launcher"
  [ -f "$BUILD_DIR/openmw-cs" ] && echo -e "  - openmw-cs (Construction Set)"
  
  echo -e "\nBuild log saved to: $BASE/build.log"
  echo -e "\nTo run the client:"
  echo -e "  cd $BUILD_DIR && ./mwnet"
  echo -e "\nTo run the server:"
  echo -e "  cd $BUILD_DIR && ./mwnet-server"
  
  cd "$BASE"
fi

echo -e "\nDone!"
