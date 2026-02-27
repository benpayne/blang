#!/bin/bash
# Install dependencies for BLang compiler
set -e

echo "Installing BLang build dependencies..."

if [ "$(uname)" = "Linux" ]; then
    sudo apt-get update
    sudo apt-get install -y cmake build-essential

    if [ "$1" = "--with-llvm" ]; then
        echo "Installing LLVM 18 development headers..."
        # LLVM 18 is in default repos on Ubuntu 24.04+, but older releases
        # (e.g. 22.04 Jammy) need the official LLVM APT repository.
        if ! apt-cache show llvm-18-dev >/dev/null 2>&1; then
            echo "Adding LLVM APT repository for $(lsb_release -cs)..."
            sudo apt-get install -y wget gnupg lsb-release
            wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key | sudo gpg --dearmor -o /usr/share/keyrings/llvm-archive-keyring.gpg
            echo "deb [signed-by=/usr/share/keyrings/llvm-archive-keyring.gpg] http://apt.llvm.org/$(lsb_release -cs)/ llvm-toolchain-$(lsb_release -cs)-18 main" | sudo tee /etc/apt/sources.list.d/llvm-18.list
            sudo apt-get update
        fi
        sudo apt-get install -y llvm-18-dev libzstd-dev
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
