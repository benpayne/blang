#!/bin/bash
# Install dependencies for BLang compiler
set -e

echo "Installing BLang build dependencies..."

if [ "$(uname)" = "Linux" ]; then
    sudo apt-get update
    sudo apt-get install -y cmake build-essential

    if [ "$1" = "--with-llvm" ]; then
        echo "Installing LLVM 18 development headers..."
        sudo apt-get install -y llvm-18-dev
    fi
elif [ "$(uname)" = "Darwin" ]; then
    brew install cmake

    if [ "$1" = "--with-llvm" ]; then
        brew install llvm@18
    fi
else
    echo "Unsupported platform: $(uname)"
    exit 1
fi

echo "Dependencies installed successfully."
