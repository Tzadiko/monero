# Trezor hardware wallet support

This module adds [Trezor] hardware support to Monero.


## Basic information

Trezor integration is based on the following original proposal: https://github.com/ph4r05/monero-trezor-doc

A custom high-level transaction signing protocol uses Trezor in a similar way a cold wallet is used. 
Transaction is build incrementally on the device. 

Trezor implements the signing protocol in [trezor-firmware] repository, in the [monero](https://github.com/trezor/trezor-firmware/tree/master/core/src/apps/monero) application.
Please, refer to [monero readme](https://github.com/trezor/trezor-firmware/blob/master/core/src/apps/monero/README.md) for more information.

## Dependencies

Trezor uses [Protobuf](https://protobuf.dev/) library.

Monero is now compiled with C++23 by default. If you are getting Trezor compilation errors, it may be caused by abseil (protobuf dependency) not being compiled with C++23.
To fix this, build protobuf from source. Use the same release the deterministic `contrib/depends` build and CI already use — protobuf 21.12, C++ distribution 3.21.12 — and verify the downloaded archive against the SHA-256 hash pinned in `contrib/depends/packages/native_protobuf.mk` before you unpack or build anything:

```shell
# Fetch the pinned release archive over HTTPS (a release, never a moving branch).
curl -fLO https://github.com/protocolbuffers/protobuf/releases/download/v21.12/protobuf-cpp-3.21.12.tar.gz

# Verify it first; nothing is unpacked unless the hash matches. On macOS use `shasum -a 256 -c`.
echo "4eab9b524aa5913c6fffb20b2a8abf5ef7f95a80bc0701f3a6dbb4c607f73460  protobuf-cpp-3.21.12.tar.gz" \
  | sha256sum --check && tar -xzf protobuf-cpp-3.21.12.tar.gz

# Build out of source and install into a prefix you own: no sudo, nothing in the system prefix.
cmake -S protobuf-3.21.12 -B protobuf-build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local/protobuf-3.21.12" \
  -Dprotobuf_BUILD_SHARED_LIBS=ON -Dprotobuf_BUILD_TESTS=OFF
cmake --build protobuf-build --parallel 4   # adjust to your core count
cmake --install protobuf-build
```

Then configure Monero with `-DCMAKE_PREFIX_PATH="$HOME/.local/protobuf-3.21.12"` (or whichever prefix you chose) so that `protoc` and the protobuf CMake package are picked up from it — the same hand-off the *Other systems* section below describes.

Note that 3.21.12 predates protobuf's abseil dependency, so it leaves no abseil standard to mismatch: Monero's Trezor probe defines `PROTOBUF_HAS_ABSEIL` only from protobuf 22.0 upwards (`cmake/CheckTrezor.cmake`). If you must build protobuf 22.0 or newer instead, add `-DABSL_PROPAGATE_CXX_STD=TRUE -DCMAKE_CXX_STANDARD=23` so that the bundled abseil is compiled at Monero's dialect, and verify that release's own published checksum first — never a moving branch.

### macOS

```bash
brew update && brew bundle --file=contrib/brew/Brewfile
```

### MSYS2 (UCRT64)

```bash
pacman -S mingw-w64-ucrt-x86_64-protobuf
```

### Other systems

- install Protobuf
- point `CMAKE_PREFIX_PATH` environment variable to Protobuf installation.

## Troubleshooting

To disable Trezor support, set `USE_DEVICE_TREZOR=OFF`, e.g.:

```shell
USE_DEVICE_TREZOR=OFF make release
```

## Resources:

- First pull request https://github.com/monero-project/monero/pull/4241
- Integration proposal https://github.com/ph4r05/monero-trezor-doc
- Integration readme in trezor-firmware https://github.com/trezor/trezor-firmware/blob/master/core/src/apps/monero/README.md

[Trezor]: https://trezor.io/
[trezor-firmware]: https://github.com/trezor/trezor-firmware/