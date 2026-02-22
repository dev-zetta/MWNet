#!/bin/bash

set -e

VERSION="1.0.0-merged"

HELP_TEXT_HEADER="\
TES3MP Merged Branch Build Script ($VERSION)
Custom build script for TES3MP 0.8.1 merged with openmw-50
Based on TES3MP-deploy by Grim Kriegor
"

HELP_TEXT_BODY="\
Usage: $0 MODE [OPTIONS]

Modes of operation:
  -i, --install                  Build TES3MP merged branch and dependencies
  -r, --rebuild                  Rebuild TES3MP merged branch
  -c, --clean                    Clean build directory
  -h, --help                     This help text

Options:
  -s, --server-only              Only build the server
  -j, --cores N                  Use N cores for building (default: all available)
  --skip-deps                    Skip building external dependencies (no-op: CrabNet is now vendored)
  --skip-pkgs                    Skip package installation
  --cmake-local                  Tell CMake to look in /usr/local/ for libraries

This script builds the TES3MP merged branch located at:
  /home/gmax/dev/TES3MP

The merged branch combines:
  - TES3MP 0.8.1 multiplayer features
  - openmw-50 modernized codebase with ESM::RefId API
"

SCRIPT_DIR="$(dirname $(readlink -f $0))"
PROJECT_DIR="/home/gmax/dev/TES3MP"

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
DEPENDENCIES="$BASE/dependencies"

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
        libqt5svg5-dev
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
        tinyxml
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
        tinyxml-devel
      ;;
    
    *)
      echo -e "\nWarning: Unknown distribution. Please install dependencies manually."
      echo -e "See BUILD_INSTRUCTIONS.md for the list of required packages."
      ;;
  esac
fi

# CrabNet is now vendored in extern/crabnet and built automatically by CMake

# Build TES3MP
if [ $INSTALL == true ] || [ $REBUILD == true ]; then
  echo -e "\n>> Building TES3MP merged branch"
  
  mkdir -p "$BUILD_DIR"
  cd "$BUILD_DIR"
  
  # CMake parameters for merged branch (openmw-50 + TES3MP)
  CMAKE_PARAMS="-Wno-dev \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_CXX_STANDARD=20 \
      -DBUILD_OPENCS=OFF"
  
  if [ "$SERVER_ONLY" = true ]; then
    echo -e "Building server-only configuration"
    CMAKE_PARAMS="$CMAKE_PARAMS \
      -DBUILD_OPENMW_MP=ON \
      -DBUILD_OPENCS=OFF \
      -DBUILD_BROWSER=OFF \
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
      -DBUILD_BROWSER=ON \
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
  [ -f "$BUILD_DIR/tes3mp" ] && echo -e "  - tes3mp (client)"
  [ -f "$BUILD_DIR/tes3mp-server" ] && echo -e "  - tes3mp-server"
  [ -f "$BUILD_DIR/tes3mp-browser" ] && echo -e "  - tes3mp-browser"
  [ -f "$BUILD_DIR/openmw-launcher" ] && echo -e "  - openmw-launcher"
  [ -f "$BUILD_DIR/openmw-cs" ] && echo -e "  - openmw-cs (Construction Set)"
  
  echo -e "\nBuild log saved to: $BASE/build.log"
  echo -e "\nTo run the client:"
  echo -e "  cd $BUILD_DIR && ./tes3mp"
  echo -e "\nTo run the server:"
  echo -e "  cd $BUILD_DIR && ./tes3mp-server"
  
  cd "$BASE"
fi

echo -e "\nDone!"
