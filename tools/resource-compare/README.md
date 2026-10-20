# resource-compare

The milestone 1 check: every resource decodes byte-identically to ScummVM's
resource loader. It needs the game files, so it runs locally, not in CI.

How it works:

1. The reference ScummVM, built with
   `tools/scummvm-patches/0001-dump-resources.patch`, loads every resource
   through its own resource manager and writes the data and a manifest
   (`SIERRA_HD_DUMP_RESOURCES=<dir>`).
2. `sci-resource-dump` (`tools/resource-dump`) writes the same layout from
   sci-resource.
3. `compare.py` requires identical manifests (ids, sizes, loaded ids, source
   files) and identical data files.

Run it inside the toolchain container, with the game folder mounted
read-only:

    SIERRA_HD_MOUNTS=/scummvm/checkout:/scummvm \
        docker/run.sh tools/reference-scummvm/build.sh /scummvm build/reference-scummvm
    docker/run.sh cmake --preset release
    SIERRA_HD_MOUNTS=/path/to/game:/game docker/run.sh tools/resource-compare/run.sh /game

The dumps go to `build/dumps/` (git-ignored, game-derived data).

Other builds of the dump tool can be compared the same way:

    build/asan/tools/resource-dump/sci-resource-dump /game build/dumps/asan
    qemu-aarch64 build/cross-aarch64/tools/resource-dump/sci-resource-dump /game build/dumps/arm64
    python3 tools/resource-compare/compare.py build/dumps/scummvm build/dumps/arm64
