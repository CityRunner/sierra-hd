# Cross-compilation to Linux ARM64 inside the toolchain container, with the
# Ubuntu arm64 cross packages as the target libraries. Tests run under
# qemu-user (QEMU_LD_PREFIX is set by the container).
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(triple aarch64-linux-gnu)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_CXX_COMPILER_TARGET ${triple})

set(CMAKE_FIND_ROOT_PATH /usr/${triple})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(CMAKE_CROSSCOMPILING_EMULATOR qemu-aarch64)

# The host binutils have no aarch64 linker; lld handles every target.
set(CMAKE_EXE_LINKER_FLAGS_INIT -fuse-ld=lld)
set(CMAKE_SHARED_LINKER_FLAGS_INIT -fuse-ld=lld)
