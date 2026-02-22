#!/bin/bash
# Install development dependencies for building BLang with LLVM code generation
set -e

echo "Installing LLVM 18 development headers and dependencies..."
sudo apt-get update -qq
sudo apt-get install -y llvm-18-dev libzstd-dev

echo ""
echo "Dependencies installed. To build with LLVM code generation:"
echo "  mkdir -p build && cd build"
echo "  cmake .. -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm"
echo "  make"
