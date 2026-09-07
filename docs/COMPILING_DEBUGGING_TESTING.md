# Compiling, debugging and testing efficiently

This document describes ways of compiling, debugging and testing efficiently for various use cases.
The intended audience are developers, who want to leverage newly added tricks to Monero via `CMake`. The document will lower the entry point for these developers.
Before reading this document, please consult section "Build instructions" in the main README.md. 
Some information from README.md will be repeated here, but the aim is to go beyond it.

## Basic compilation

Monero can be compiled via the main `Makefile`, using one of several targets listed there.
The targets are actually presets for `CMake` calls with various options, plus `make` commands for building or in some cases `make test` for testing.
It is possible to extract these `CMake` calls and modify them for your specific needs. For example, a minimal external cmake command to compile Monero, executed from within a newly created build directory could look like:

`cmake -S "$DIR_SRC" -DCMAKE_BUILD_TYPE=Release && make`

where the variable `DIR_SRC` is expected to store the path to the Monero source code.

## Toolchain requirements

This section is the authoritative statement of the language standard, the build-system floor, the compiler floors and the verified compiler/standard-library pairings for this repository. `README.md` repeats the floors in prose and refers here for the matrix, and the compiler-floor guard in the root `CMakeLists.txt` points its `FATAL_ERROR` messages at this section, so the three are kept in agreement with each other.

### Language standard

Monero is compiled as C++23. The root build sets `CMAKE_CXX_STANDARD 23` together with `CMAKE_CXX_STANDARD_REQUIRED ON` and `CMAKE_CXX_EXTENSIONS OFF`, so no target silently falls back to an older dialect and no compiler extension is enabled. C sources are compiled as C11, declared the same way with `CMAKE_C_STANDARD 11`, `CMAKE_C_STANDARD_REQUIRED ON` and `CMAKE_C_EXTENSIONS OFF`.

CMake spells the dialect `-std=c++23` for GCC and for Clang 17 or newer, and `-std=c++2b` for Clang 16. Both spellings select the same language, which is why the Clang 16 row of the matrix below is annotated with `-std=c++2b`.

### CMake

CMake 3.25 or newer is required, and `cmake_minimum_required(VERSION 3.25)` is declared both in the root build and in the small embedded project it configures with `try_compile`. The `CXX_STANDARD` value `23` on its own would need only CMake 3.20; 3.25 is the floor because `cmake_minimum_required` also raises the policy version, and the tree relies on the behaviour that comes with it. In particular, CMP0119 makes CMake pass an explicit `-x <language>` for sources whose `LANGUAGE` property is set, which is why `src/crypto/CryptonightR_template.S` is declared `LANGUAGE ASM` in `src/crypto/CMakeLists.txt`: under `LANGUAGE C` the policy would pass `-x c` and the assembler source would fail to compile.

The versions exercised for this dialect are 3.25.3 (configure) and 3.28.3 (full builds).

### Compiler floors and verified pairings

The floors are GCC 13, Clang 16, Apple Clang 15 (Xcode 15) and MinGW-w64 GCC 13. They are enforced at configure time by the guard in the root `CMakeLists.txt`, and they are floors of compiler *families*: what has actually been built and tested is the matrix below, which is the only statement of that. A pairing the matrix does not list is untested rather than endorsed.

| Compiler                    | Standard library / Boost              | Status |
| --------------------------- | ------------------------------------- | ------ |
| GCC ≥ 13 (floor) — 13.3.0   | libstdc++ 13 / Boost 1.83             | **Verified**; the Ubuntu 24.04 CI image and the reference Debian-13-class configuration |
| GCC 14.2.0                  | libstdc++ 14 / Boost 1.83             | **Verified**; the Debian 13 CI image |
| Clang ≥ 16 (floor) — 16.0.6 | libstdc++ 13 / Boost 1.83             | **Verified** (`-std=c++2b`); the only verified Clang 16 pairing |
| Clang 16.0.6                | libstdc++ 14                          | **Fails** at C++23 in five objects — a compiler/standard-library pairing defect, not first-party code; documented as unsupported |
| Clang 18.1.3                | libstdc++ 14 / Boost 1.91.0           | **Verified** and warning-clean |
| Clang 18.1.3                | libstdc++ 14 / Boost 1.83             | Builds and passes tests; emits one Boost-internal Beast deprecation — **conditional support requirement**: Clang ≥ 18 with libstdc++ ≥ 14 needs Boost ≥ 1.84 to be warning-clean |
| Clang (any)                 | libc++                                | **Not verified**; no CI or release path on Linux uses it |
| Apple Clang ≥ 15 (floor; Xcode 15, whose compiler is based on LLVM 16 — the Apple equivalent of the Clang 16 floor) | Xcode libc++ / Homebrew Boost | Guard-enforced; CI verifies only the compiler `macOS-latest` ships |
| MinGW-w64 GCC ≥ 13 (floor)  | libstdc++ 13 / MSYS2 or depends Boost | **CI-enforced only**: the Windows `ucrt64` job and the `x86_64-w64-mingw32` depends job |
| depends cross hosts and Guix (`gcc-15` Linux, `clang-toolchain-22` Darwin) | Boost 1.91.0, OpenSSL 3.5.7, ZeroMQ 4.3.5, protobuf 3.21.12 (all pinned) | Validated by the `depends.yml` and `guix.yml` runs |

Unknown compiler IDs and the `clang-cl` frontend are rejected at configure time, because this repository has no MSVC build path: Windows is built with MinGW-w64 GCC through MSYS2 UCRT64. The guard branches on `CMAKE_CXX_COMPILER_ID` and, for `clang-cl`, on `CMAKE_CXX_COMPILER_FRONTEND_VARIANT`.

The guard checks compiler versions only. It therefore cannot detect the Clang 16 with libstdc++ 14 pairing defect, which is a header-library mismatch rather than a compiler version, and that is exactly why the matrix carries that row: such a failure surfaces only when the affected translation units are compiled, so this table is the only place it is documented.

### Libraries

Boost 1.69 is the declared floor, unchanged by the move to C++23; 1.83 and 1.91.0 are the versions verified under C++23. Clang 18 or newer with libstdc++ 14 or newer must use Boost 1.84 or newer for a warning-clean build. With Boost 1.83 or older that combination builds and passes its tests, but Beast's use of the deprecated `std::aligned_storage` emits one deprecation diagnostic, reached through the tree's single Beast consumer, `tests/unit_tests/epee_http_server.cpp`; Boost 1.84.0 replaced that use upstream. Nothing is suppressed for it — no `-Wno-*` flag and no diagnostic pragma is added anywhere — the resolution is the newer Boost.

OpenSSL 1.1.1 is the declared floor and is likewise unchanged: it is a C API consumed through `extern "C"`, so the C++ dialect cannot move it. 3.0.13 is the version verified against, and 3.5.7 is the version pinned in `contrib/depends`.

Raising either declared floor is a separate maintainers' decision, not a consequence of the move to C++23.

### Rust

Rust and `cargo` are mandatory on `master` — `src/fcmp_pp/fcmp_pp_rust` is built unconditionally — so both must be on `PATH` before CMake is configured. `src/fcmp_pp/fcmp_pp_rust/Cargo.toml` declares no `rust-version`, so the repository states no minimum supported Rust version; 1.93, installed through rustup, is the toolchain CI tests with.

### Cross-build dialect exceptions

The Darwin (Xcode 12.2) and FreeBSD 12.3 sysroots pinned by `contrib/depends` ship a libc++ that predates the C++23 library additions, which is why no C++23 library facility is used in the tree; any future use has to be gated on the matching `__cpp_lib_*` feature-test macro from `<version>` with the existing implementation kept as the fallback. Should a depends recipe fail to compile against one of those sysroots, the remediation is a per-recipe, per-host exception through that recipe's `$(package)_cxxflags_$(host_os)` hook, adding `-std=c++17` for that one recipe on that one host while the first-party dialect stays at 23, and it is recorded in this section. Bumping either sysroot is deliberately not an available remediation, because it changes reproducible-build inputs and is a maintainers' decision. No such exception is currently in force.

## Use cases

### Test Driven Development (TDD) - shared libraries for release builds

Building shared libraries spares a lot of disk space and linkage time. By default only the debug builds produce shared libraries. If you'd like to produce dynamic libraries for the release build for the same reasons as it's being done for the debug version, then you need to add the `BUILD_SHARED_LIBS=ON` flag to the `CMake` call, like the following:

`cmake -S "$DIR_SRC" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON && make`

A perfect use case for the above call is following the Test Driven Development (TDD) principles. In a nutshell, you'd first write a couple of tests, which describe the (new) requirements of the class/method that you're about to write or modify. The tests will typically compile for quite a long time, so ideally write them once. After you're done with the tests, the only thing left to do is to keep modifying the implementation for as long as the tests are failing. If the implementation is contained properly within a .cpp file, then the only time cost to be paid will be compiling the single source file and generating the implementation's shared library. The test itself will not have to be touched and will pick up the new version of the implementation (via the shared library) upon the next execution of the test.

### Project generation for IDEs

CMake allows to generate project files for many IDEs. The list of supported project files can be obtained by writing in the console:

`cmake -G`

For instance, in order to generate Makefiles and project files for the Code::Blocks IDE, this part of the call would look like the following:

`cmake -G "CodeBlocks - Unix Makefiles" (...)`

The additional artifact of the above call is the `monero.cbp` Code::Blocks project file in the build directory.

### Debugging in Code::Blocks (CB)

First prepare the build directory for debugging using the following example command, assuming, that the path to the source dir is being held in the DIR_SRC variable, and using 2 cores:

`cmake -S "$DIR_SRC" -G "CodeBlocks - Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON && make -j 2`

After a successful build, open the `monero.cbp` with CB. From the CB's menu bar select the target, that you want debug. Assuming these are unit tests:

`Build -> Select target -> Select target -> unit_tests`

In order to lower the turnaround times, we will run a specific portion of code of interest, without having to go through all the time costly initialization and execution of unrelated parts. For this we'll use GTest's capabilities of test filtering. From the build directory run the following command to learn all the registered tests:

`tests/unit_tests/unit_tests --gtest_list_tests`

For example, if you're only interested in logging, you'd find in the list the label `logging.` and its subtests. To execute all the logging tests, you'd write in the console:

`tests/unit_tests/unit_tests --gtest_filter="logging.*"`

This parameter is what we need to transfer to CB, in order to reflect the same behaviour in the CB's debugger. From the main menu select:

`Project -> Set program's arguments...`

Then in the `Program's arguments` textbox you'd write in this case: 

`--gtest_filter="logging.*"`

Verify if the expected UTs are being properly executed with `F9` or select:

`Build -> Build and run`

If everything looks fine, then after setting some breakpoints of your choice, the target is ready for debugging in CB via:

`Debug -> Start/Continue`

## To be done (and merged):
### Multihost parallel compilation
https://github.com/monero-project/monero/pull/7160

### Unity builds
https://github.com/monero-project/monero/pull/7217

