FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    ca-certificates git build-essential pkg-config liblzma-dev clang-18 libclang-18-dev \
    gcc-arm-none-eabi libnewlib-arm-none-eabi && rm -rf /var/lib/apt/lists/*
ENV LIBCLANG_PATH=/usr/lib/llvm-18/lib
ENV CC_thumbv7em_none_eabihf=arm-none-eabi-gcc
ENV AR_thumbv7em_none_eabihf=arm-none-eabi-ar
