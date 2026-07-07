---
type: Build Guide
title: Build Guide
description: Build, flash, monitor, test, and lint commands for the ecotiter C++23 firmware
tags: [build, esp-idf, cmake, c++23]
timestamp: 2026-07-07
---

# Build Guide

## Prerequisites

- ESP-IDF v6.0.1 installed at `$IDF_PATH`
- C++23-capable toolchain (ESP-IDF provides clang-based toolchain)
- cmake >= 3.16
- Python 3.10+ for scripts

## Quick Start

```bash
# Configure target (run once)
idf.py set-target esp32s3

# Build firmware
idf.py build

# Flash to device
idf.py -p /dev/ttyACM0 flash

# Monitor serial output (30s timeout)
timeout 30 idf.py -p /dev/ttyACM0 monitor
```

## Build Commands (via scripts/build.sh)

```bash
# Build firmware
./scripts/build.sh build

# Flash to default port (/dev/ttyACM0)
./scripts/build.sh flash

# Flash to specific port
./scripts/build.sh flash /dev/ttyUSB0

# Serial monitor with 30s timeout
./scripts/build.sh monitor

# Host unit tests (Catch2)
./scripts/build.sh test

# clang-tidy static analysis
./scripts/build.sh tidy

# Clean build artifacts
./scripts/build.sh clean
```

## Manual Build Steps

```bash
# Standard build
idf.py build

# Clean rebuild
idf.py fullclean && idf.py build

# Regenerate sdkconfig after defaults change
idf.py reconfigure

# Generate compile_commands.json for clang-tidy
idf.py build  # generates automatically with CMAKE_EXPORT_COMPILE_COMMANDS=ON
```

## Host Unit Tests

Unit tests compile domain components natively (x86_64) using Catch2:

```bash
mkdir -p build-tests && cd build-tests
cmake ../tests
cmake --build .
ctest --output-on-failure
```

## Linting

```bash
# clang-tidy (requires compile_commands.json from build)
find components main -name '*.cpp' | xargs -P4 clang-tidy -p build/

# clang-format check
find components main -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i -n
```

## SDK Config Policy

- Edit only `sdkconfig.defaults` -- never `sdkconfig` (auto-generated)
- Never run `idf.py menuconfig` (not reproducible)
- After changing defaults: `idf.py reconfigure`
