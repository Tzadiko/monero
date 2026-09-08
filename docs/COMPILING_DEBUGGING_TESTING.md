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

This section is the authoritative statement of the language standard, the
build-system floor, the compiler floors and the verified
compiler/standard-library pairings for this repository. `README.md` repeats the
floors in prose and refers here for the matrix, and the compiler-floor guard in
the root `CMakeLists.txt` points its `FATAL_ERROR` messages at this section, so
the three are kept in agreement with each other.

### Language standard

Monero is compiled as C++23. The root build sets `CMAKE_CXX_STANDARD 23`
together with `CMAKE_CXX_STANDARD_REQUIRED ON` and `CMAKE_CXX_EXTENSIONS OFF`,
so no target silently falls back to an older dialect and no compiler extension
is enabled. C sources are compiled as C11, declared the same way with
`CMAKE_C_STANDARD 11`, `CMAKE_C_STANDARD_REQUIRED ON` and
`CMAKE_C_EXTENSIONS OFF`.

CMake spells the dialect in one of two equivalent ways, and which one it picks
depends on the CMake version and the target platform as well as on the
compiler. For GCC it emits `-std=c++23`, from GCC 11.1 onwards and therefore
for every GCC this repository accepts. For Clang the mapping changed inside the
CMake range this repository supports: 3.25 and 3.26 emit `-std=c++2b` for every
Clang they accept at this dialect, whatever its version; 3.27.0 and newer emit
`-std=c++23` for Clang 17 or newer and keep `-std=c++2b` for Clang 12 through
16; and from 3.27.7 that threshold is Clang 18 when the target system is
Android, so an Android build with Clang 17 gets `-std=c++2b` as well. The Clang
16 row of the matrix below is annotated with `-std=c++2b` for that reason. Both
spellings select the same language, so a build log carrying either one is
building C++23.

### CMake

CMake 3.25 or newer is required, and `cmake_minimum_required(VERSION 3.25)` is
declared both in the root build and in the small embedded project it configures
with `try_compile`. The `CXX_STANDARD` value `23` on its own would need only
CMake 3.20; 3.25 is the floor because `cmake_minimum_required` also raises the
policy version, and the tree relies on the behaviour that comes with it. In
particular, CMP0119 makes CMake pass an explicit `-x <language>` for sources
whose `LANGUAGE` property is set, which is why
`src/crypto/CryptonightR_template.S` is declared `LANGUAGE ASM` in
`src/crypto/CMakeLists.txt`: under `LANGUAGE C` the policy would pass `-x c`
and the assembler source would fail to compile.

The versions exercised for this dialect are 3.25.3 (configure) and 3.28.3 (full
builds).

### Compiler floors and verified pairings

The floors are GCC 13, Clang 16, Apple Clang 15 (Xcode 15) and MinGW-w64 GCC
13. They are enforced at configure time by the guard in the root
`CMakeLists.txt`, and they are floors of compiler *families*: what has actually
been built and tested is the matrix below, which is the only statement of that.
A pairing the matrix does not list is untested rather than endorsed, and the
Status column names the evidence for each row, so that a verified pairing is
never read as one that is only declared, only enforced by CI, or still awaiting
its gate.

Two things about how to read the table. First, a version in the Compiler
column after a floor, and a version in the Standard library / Boost column, are
the versions the acceptance build actually used — they record what was
exercised, not a patch-level requirement, and a later patch release of the same
family is covered by the row. Second, "Verified" is reserved for a pairing that
was configured, built in full and tested in an environment we can point at;
where the pairing is exercised only by a CI image, the row says **CI-enforced**
and names the job, because a version that no acceptance build compiled must not
be read as one that was.

| Compiler | Standard library / Boost | Status |
| --- | --- | --- |
| GCC ≥ 13 (floor) — 13.4.0 | libstdc++ 13 / Boost 1.88.0 | **Verified**: configured, built in full and tested, against the Boost the build host provided |
| GCC 14 — 14.3.0 | libstdc++ 14 / Boost 1.88.0 | **Verified**: configured, built in full and tested |
| GCC 13 and GCC 14 | libstdc++ 13 or 14 / Boost 1.83 | **CI-enforced, not built here**: Boost 1.83 is what the Ubuntu 24.04 and Debian 13 images ship, so the `build-linux` jobs exercise that pairing on every push. No acceptance build compiled 1.83 locally, so this row claims CI coverage and nothing more |
| Clang ≥ 16 (floor) — 16.0.4 | libstdc++ 13 / Boost 1.88.0 | **Verified** (`-std=c++2b`): the only verified Clang 16 pairing, and Clang must be pointed at the libstdc++ 13 headers on a host that also carries 14 (`--gcc-install-dir=/usr/lib/gcc/<triple>/13`) |
| Clang 16 | libstdc++ 14 | **Fails** at C++23: `std::pair`'s C++23 pair-like converting constructor (P2165R4) cannot be resolved inside Boost.Variant/Boost.Spirit, so translation units reaching that instantiation stop with errors in `bits/stl_pair.h`. A compiler/standard-library pairing defect, not first-party code; documented as unsupported |
| Clang 18 — 18.1.8 | libstdc++ 14 / Boost 1.88.0 | **Verified** and warning-clean; Boost 1.88 sits on the Boost ≥ 1.84 side of the condition in the next row |
| Clang ≥ 18 | libstdc++ ≥ 14 / Boost ≤ 1.83 | **Conditional support requirement**: the tree builds and passes its tests, but Beast's use of the deprecated `std::aligned_storage` yields one Boost-internal warning origin, so Boost ≥ 1.84 is required for a warning-clean build. Measured in the reference environment, which carried Boost 1.83; it cannot be reproduced on a host whose only Boost is ≥ 1.84 |
| Clang 18, Debug, `STACK_TRACE` via libunwind | libstdc++ 14 | **Verified** for `obj_common` only: 17 objects, including `src/common/stack_trace.cpp`, at C++23 with zero warnings; the only exercise of the libunwind path, which no CI job builds |
| Clang (any) | libc++ | **Not verified**; no CI or release path on Linux uses it |
| Apple Clang ≥ 15 (floor, Xcode 15 = LLVM 16 base) | Xcode libc++ / Homebrew Boost | **Declared, guard-enforced, not demonstrated**; the only continuous evidence is **CI-enforced**, on a newer Xcode (see below) |
| MinGW-w64 GCC ≥ 13 (floor) | libstdc++ 13 / MSYS2 or depends Boost | **CI-enforced only**: the Windows `ucrt64` job and the `x86_64-w64-mingw32` depends job |
| depends cross hosts and Guix (`gcc-15` Linux, `clang-toolchain-22` Darwin) | Boost 1.91.0, OpenSSL 3.5.7, ZeroMQ 4.3.5, protobuf 3.21.12 (all pinned) | **Not built here; pre-acceptance gate**: pinned, not verified (see below). At this dialect the pinned protobuf recipe emits a diagnostic of its own on every host, recorded under "Documented third-party diagnostics" |

The Apple Clang row is the one floor in this matrix that has not been
demonstrated, and it is published as exactly that: declared, and enforced by
the configure-time guard, but not verified. Xcode 15 ships a compiler based on
LLVM 16, which makes it the Apple equivalent of the Clang 16 floor, but no
Apple compiler was available when the matrix was measured, so Xcode 15 itself
has never been configured, built or tested. The only continuous evidence is
the `macOS (brew)` job, which builds and runs the reduced test tier on the
newer Xcode that `macOS-latest` ships. One pinned Xcode 15 configure, build
and test run is required before this floor is published, and recording in this
row the exact Xcode and Apple Clang version it passed with is what turns the
row from declared into verified. Should that run not pass, the floor is raised
to the oldest Xcode that does pass — in this matrix, in `README.md` and in the
guard together.

The last row is a gate rather than a result. No depends cross host and no Guix
triple has been built at this dialect, so the Boost, OpenSSL, ZeroMQ and
protobuf versions in that row are the versions `contrib/depends` pins and not
versions this dialect has been compiled against. The gates are a `depends.yml`
run across all ten cross hosts — among them the three whose pinned libc++
predates C++23, the two Apple Darwin targets and FreeBSD — and a `guix.yml` run
on the candidate, repeated to confirm that the release binaries still
reproduce. Those runs are the pre-acceptance gates themselves, and no
in-repository remediation for them is recorded: until both pass, this row
claims nothing beyond the pins.

Unknown compiler IDs and the `clang-cl` frontend are rejected at configure
time, because this repository has no MSVC build path: Windows is built with
MinGW-w64 GCC through MSYS2 UCRT64. The guard branches on
`CMAKE_CXX_COMPILER_ID` and, for `clang-cl`, on
`CMAKE_CXX_COMPILER_FRONTEND_VARIANT`.

The guard checks compiler family, frontend variant and version, but it does
not inspect the standard library. It therefore cannot detect the Clang 16 with
libstdc++ 14 pairing defect, which is a header-library mismatch rather than a
compiler version, and that is exactly why the matrix carries that row:
such a failure surfaces only when the affected translation units are compiled,
so this table is the only place it is documented.

### Libraries

Boost 1.69 is the declared floor, unchanged by the move to C++23. Which Boost
versions have actually been through a C++23 build, and on what evidence, is
worth stating precisely, because the floor and the exercised versions are
different things:

- **1.88.0 — verified.** Every acceptance build of this tree was configured and
  linked against 1.88.0, the version its build host provided, on all four
  compiler rows of the matrix above.
- **1.83 — CI-enforced, not built locally.** 1.83 is the version the Ubuntu
  24.04 and Debian 13 images ship, so the `build-linux` jobs compile the tree
  against it on every push. No local acceptance build compiled 1.83, so it is
  claimed here as CI coverage rather than as a locally verified pairing.
- **1.91.0 — pinned in `contrib/depends`, covered by the depends gate.** The
  cross-build recipe pins it and rebuilds it at this dialect; that is the
  `depends.yml`/`guix.yml` gate described with the last row of the matrix, not
  a native build.
- **1.69 through 1.82 — nothing claimed.** They were not built or tested at
  this dialect in any pairing; the declared floor is all that stands behind
  them.

Clang 18 or newer with libstdc++ 14 or newer must use Boost 1.84 or newer for a
warning-clean build. With Boost 1.83 that combination builds and passes its
tests, but Beast's use of the deprecated `std::aligned_storage` produces one
Boost-internal warning origin (eight diagnostics in the full build that
measured it), reached through the tree's single Beast consumer,
`tests/unit_tests/epee_http_server.cpp`; Boost 1.84.0 replaced that use
upstream, so releases from 1.84 onwards do not carry it, and a host whose only
Boost is 1.84 or newer — 1.88.0 in the verified rows above — cannot reproduce
it. Nothing is suppressed for any of this — no `-Wno-*` flag and no diagnostic
pragma is added anywhere — the resolution is the newer Boost.

OpenSSL 1.1.1 is the declared floor and is likewise unchanged: it is a C API
consumed through `extern "C"`, so the C++ dialect cannot move it. 3.0.13 was
verified in the reference environment and 3.5.3 in the environment that built
the delivered tree; 3.5.7 is the version pinned in `contrib/depends` and, like
every other pin, is covered by the depends gate rather than by a native build.

Raising either declared floor is a separate maintainers' decision, not a
consequence of the move to C++23.

### Rust

Rust and `cargo` are mandatory on `master` — `src/fcmp_pp/fcmp_pp_rust` is
built unconditionally — so both must be on `PATH` before CMake is configured.
`src/fcmp_pp/fcmp_pp_rust/Cargo.toml` declares no `rust-version`, so the
repository states no minimum supported Rust version; 1.93, installed through
rustup, is the toolchain CI tests with.

### Documented third-party diagnostics

The move to C++23 is held to adding no new warning origin, and the first-party
tree meets that: no `-Wno-*` flag, no diagnostic pragma and no
`[[maybe_unused]]` is added anywhere, and every diagnostic the dialect
surfaced in this repository's own code was removed by rewriting the code. Two
dependencies do emit a dialect-induced diagnostic from inside their own
headers, where the fix belongs upstream rather than here. Both are recorded
below rather than silenced, and they are the only third-party origins observed
at this dialect: when a build's warning census is compared against a C++17
baseline, these are the allowed difference and any other new origin is a
regression to be fixed at source.

The first is Boost.Beast's deprecated `std::aligned_storage` with Clang ≥ 18
and libstdc++ ≥ 14, described under "Libraries" above and resolved by using
Boost ≥ 1.84.

The second appears only in the `contrib/depends` cross-builds, in the pinned
protobuf recipe:

| Property | Value |
| --- | --- |
| Flag | `-Wdeprecated-enum-enum-conversion` |
| Emitted by | protobuf 21.12 (protobuf-cpp 3.21.12), the version `contrib/depends/packages/protobuf.mk` pins, from its own header `google/protobuf/generated_message_tctable_impl.h` |
| Extent | 35 distinct origins — lines 186, 188–196, 198–203, 205, 207–215, 217–222, 225–227 — reached while compiling three of protobuf's own translation units (`generated_message_tctable_full.cc`, `generated_message_tctable_lite.cc`, `unknown_field_set.cc`), so 105 diagnostics per depends host |
| Cause | P1120R0 deprecated arithmetic between two different enumeration types in C++20. The header composes its field-layout constants that way, e.g. `kBool = kFkVarint \| kRep8Bits`, mixing `field_layout::FieldKind` with `field_layout::FieldRep`. C++23 is the first dialect this repository compiles the recipe under |
| Scope | Every depends host, and independent of the host compiler and standard library: it reproduces identically with Clang and with GCC, and on the two Apple Darwin hosts and the FreeBSD host whose sysroots carry an older libc++ |
| Effect on the build | None. No `-Werror` reaches the recipe — `contrib/depends` adds none — so the diagnostics cannot become errors, and no first-party warning origin is added on any host |
| Why a native build never shows it | Natively, protobuf is found in a system include directory and diagnostics from system headers are suppressed. In depends the recipe compiles its own headers through `-I.`/`-I..`, where that suppression does not apply |

To see it without a cross-build, compile the header as a non-system include —
`printf '#include "google/protobuf/generated_message_tctable_impl.h"\nint main(){return 0;}\n' > tu.cpp` then
`c++ -std=c++23 -I<dir containing google/> -c tu.cpp` — which yields the 35
diagnostics; the same command at `-std=c++17`, or with `-isystem` in place of
`-I`, yields none.

The disposition is the same as for the Beast condition: it is documented, and
it is not suppressed. Removing it would mean either adding
`-Wno-deprecated-enum-enum-conversion` to that one recipe through the
`$(package)_cxxflags_$(host_os)` hook that `contrib/depends/packages/zeromq.mk`
already uses for its own flags — a flag on third-party code rather than a
first-party suppression — or moving the recipe to a protobuf release that fixed
the enum arithmetic upstream. Both change the inputs of a reproducible build,
which makes them maintainers' decisions rather than part of a language-standard
migration, so neither is done here.

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

## Running tests and binaries locally

`tests/README.md` describes what each test suite covers and how it is invoked.
This section covers the two things that are easy to get wrong when running
them, or the binaries they exercise, on a development machine: the environment
variables some tests are gated on, and the arguments the daemon and the
blockchain utilities need in order to stay away from your real, mainnet data.

### Environment variables the tests read

| Variable | Read by | Effect |
| --- | --- | --- |
| `DNS_PUBLIC` | the DNS resolution path used by several suites | Set it to `tcp` before running the tests, as CI does, so DNS lookups go over TCP to a public resolver instead of depending on the local resolver |
| `MONERO_TEST_DEVICE_HDD` | `is_hdd.rotational_drive` in `tests/unit_tests/is_hdd.cpp` | A path on a filesystem backed by a **rotational** disk. Unset, the case skips |
| `MONERO_TEST_DEVICE_SSD` | `is_hdd.ssd` in `tests/unit_tests/is_hdd.cpp` | A path on a filesystem backed by a **non-rotational** disk. Unset, the case skips |

Both device variables take a **filesystem path**, not a device node:
`tools::is_hdd()` in `src/common/util.cpp` calls `stat()` on the path and reads
`/sys/dev/block/<major>:<minor>/queue/rotational` (falling back to
`.../../queue/rotational` for a partition), so a path that is not on a block
device, or a device node with no mounted filesystem, makes the probe
indeterminate rather than true or false. The probe logs which of those cases
it hit at debug level, so run the binary under test at `--log-level 2` —
`unit_tests` accepts that flag, as the daemon and the utilities do — to see
lines such as `is_hdd: no rotational attribute for <path> ... - device kind
unknown`. Level 1 is not enough: it selects `*:INFO`, and only level 2 turns
on `*:DEBUG`.

To find suitable values, list the mounted filesystems and the rotational flag
of the device behind each one:

```bash
lsblk -o NAME,ROTA,MOUNTPOINT      # ROTA 1 is rotational, 0 is not
findmnt -no SOURCE,TARGET          # which device is behind which path
```

Then export a mount point (or any directory under one) from the matching line,
for example `MONERO_TEST_DEVICE_SSD=/tmp` on a host whose `/tmp` is on an
SSD or in memory, and `MONERO_TEST_DEVICE_HDD=/mnt/spinning-disk` on a host
that has one. On a machine or container with no rotational storage —
which is the normal case for CI runners — leave `MONERO_TEST_DEVICE_HDD`
unset: the skip is the correct outcome, and pointing the variable at a
non-rotational or nonexistent path makes the case fail rather than skip.

When running the `unit_tests` binary directly, pass the copy of the test data
that the build produced, `--data-dir <build dir>/tests/data`, rather than the
one in the source tree; some wallet suites write into that directory, and
CTest itself passes the build copy. Run test binaries one at a time: several
suites bind fixed loopback ports, so CI never passes `-j` to `ctest`.

### Keeping local runs away from mainnet data

Without `--data-dir`, every binary that opens a blockchain database uses the
default data directory, which is the **mainnet** one — `~/.bitmonero` on Unix,
as each utility's own `--help` shows. That applies to any invocation the
argument parser accepts, including one whose only argument is an empty or
nonsensical positional value: unknown *options* are rejected before anything
happens, but a valid invocation with a junk positional argument falls through
to the default data directory and performs its real operation there, creating
or opening a mainnet LMDB. A smoke or fuzzing harness that runs these binaries
with generated arguments will do this on every call unless it passes the flags
below.

Always pass the network, offline and data-directory flags — and note that the
accepted set differs per binary, so a single blanket flag list does not work:

| Binary | Flags to pass |
| --- | --- |
| `monerod` | `--testnet --offline --no-igd --data-dir <throwaway> [--log-file <path>]` |
| `monero-wallet-rpc` | `--testnet --offline --wallet-dir <throwaway> --disable-rpc-login --log-file <path>` |
| `monero-wallet-cli` | `--testnet --offline --wallet-file <throwaway>`; with no wallet argument it waits at an interactive prompt |
| `monero-blockchain-import` | `--testnet --offline --data-dir <throwaway>` |
| `monero-blockchain-{export,prune,prune-known-spent-data,stats,depth,ancestry}` | `--testnet --data-dir <throwaway>`; these do **not** accept `--offline` or `--no-igd` and reject them outright |
| `monero-blockchain-usage` | `--testnet --input <throwaway>`; it has no `--data-dir` |

`--offline` on the daemon prevents every peer-to-peer connection. For a
deterministic local chain the functional tests use `--regtest` with
`--fixed-difficulty` and add `--offline` to the instances that need no peers,
which is the pattern to copy for any ad-hoc chain of your own.

Logs need the same care as data directories. The blockchain utilities accept
`--log-level` but not `--log-file`, and every one of these binaries writes
`<binary name>.log` **next to the executable it was invoked as** — not into the
current directory and not into the data directory. Running them out of
`build/bin` therefore leaves log files there to clean up; invoking them through
a symlink in a scratch directory puts the log beside the symlink instead.
`monerod`, `monero-wallet-cli` and `monero-wallet-rpc` do accept `--log-file`,
and it is worth passing: `monero-wallet-rpc` in particular grows its log
quickly over a session.

## Continuous integration

`.github/workflows/build.yml` (the `ci/gh-actions/cli` workflow) runs on every
push and every pull request, except when the change touches only `docs/**` or
`**/README.md`: both triggers list those two patterns under `paths-ignore`, so
a documentation-only change — including a change to this file — triggers none
of the jobs below.

### The jobs

| Job | Where it runs | What it does |
| --- | --- | --- |
| `build-macos` (`macOS (brew)`) | `macOS-latest` | Installs dependencies with `brew install --quiet cmake boost hidapi openssl zmq unbound protobuf ccache`, builds, and runs the reduced test tier |
| `build-windows` (`Windows (MSYS2)`) | `windows-latest`, every step in the `msys2 {0}` shell | Sets up `msys2/setup-msys2@v2` with `msystem: ucrt64` and installs the toolchain and libraries through `pacboy`, builds, and runs the reduced test tier |
| `build-arch` (`Arch Linux`) | `archlinux:latest` container | Installs the rolling Arch toolchain and dependencies with `pacman -Syyu`, then configures and builds |
| `build-linux` | a two-entry matrix of `debian:13` (`Debian 13`) and `ubuntu:24.04` (`Ubuntu 24.04`) containers | Installs the shared `APT_INSTALL_LINUX` list and Rust 1.93 through a checksum-pinned `rustup-init`, then configures and builds |
| `test-ubuntu` (`Ubuntu 24.04 (tests)`) | `ubuntu:24.04` container, `--privileged` | The same dependencies plus the test harness' `pip` modules, then builds and runs the tests; the container is privileged because `tests/create_test_disks.sh` sets up loop devices |
| `build-docker` (`Docker`) | `ubuntu-latest` | `docker build .`, which is what keeps the repository `Dockerfile` building |
| `source-archive` (`source archive`) | `ubuntu:22.04` container | Produces the release source tarball with `git-archive-all --force-submodules` and uploads it as an artifact; it compiles nothing, which is why it stays on 22.04 while the compiling jobs moved forward |

The five jobs that build the tree directly all run the same `BUILD_DEFAULT`
command: a `cmake -S . -B build` configure with `-D ARCH="default"`,
`-D BUILD_TESTS=ON`, `-D BUILD_GUI_DEPS=ON`, `-D ENABLE_FUZZ_TEST=ON` and
`-D CMAKE_BUILD_TYPE=Release`, followed by `cmake --build build --target all`,
with `USE_DEVICE_TREZOR_MANDATORY=ON` in the workflow environment. Each of
them also restores a `ccache` cache keyed on its own operating system or
container image, caps it at 150 MB, and saves it again only on pushes whose
restore missed.

### Which jobs run tests, and why serially

`build-linux` and `build-arch` stop at `BUILD_DEFAULT`: they compile the test
binaries but execute none of them, so a regression that only shows up when a
test runs is not caught there. `test-ubuntu` is the only Linux job that runs
tests — `ctest --test-dir build --output-on-failure -E core_tests` with
`DNS_PUBLIC=tcp`, then the same command with `-R core_tests` in place of the
exclusion; on a pull request that second run is preceded by a `--fresh`
reconfigure and a rebuild of `core_tests` with
`CFLAGS=-DMONERO_CRYPTO_SLOW_HASH_ITER=20`, which is what fits the consensus
scenarios into a runner's time budget. `build-macos` and `build-windows` run
the reduced tier instead, which excludes `functional_tests_rpc`, `core_tests`,
`cnv4-jit`, `hash-variant2-int-sqrt` and `wide_difficulty` and filters out the
DNS- and output-selection-dependent unit tests.

No `ctest` invocation in the workflow is given `-j`, and none should be:
several tests bind fixed loopback ports — the `epee_boosted_tcp_server` unit
tests use 5626, `net_load_tests` uses 36230 and 36231, and the
`functional_tests_rpc` harness starts daemons and wallets on fixed ports from
18090 upwards — so suites running concurrently would collide on them. Test
parallelism is not the same knob as the build parallelism below.

### What the dependency lists enable

Two groups of packages in the shared dependency lists are there for what they
add to the build and test graph rather than for the daemon itself, and both are
easy to leave out of a local install and then wonder what is missing:

- `libreadline-dev` in `APT_INSTALL_LINUX`, and `readline` on the Arch
  `pacman` line, are what let `find_package(Readline)` succeed in the root
  build. Without them it reports "Could not find GNU readline library so
  building without readline support" and `contrib/epee/src/CMakeLists.txt`
  gates out the `epee_readline` and `obj_epee_readline` targets, so the
  readline-backed CLI input path in `contrib/epee/src/readline_buffer.cpp` is
  compiled by nothing.
- `python3 python3-requests python3-zmq python3-deepdiff` in
  `APT_INSTALL_LINUX`, and the corresponding
  `python3 python-requests python-pyzmq python-deepdiff` on the Arch `pacman`
  line, satisfy the `import requests`, `import zmq` and `import deepdiff`
  probe in `tests/functional_tests/CMakeLists.txt`. Without them that file
  emits a `CMake Warning`, adds `functional_tests_rpc` and
  `check_missing_rpc_methods` to `CTEST_CUSTOM_TESTS_IGNORE`, and registers
  neither test. `test-ubuntu` additionally installs `requests`, `psutil`,
  `monotonic`, `zmq` and `deepdiff` with `pip`; with the apt packages in
  place `pip` reports `requests`, `pyzmq` and `deepdiff` already satisfied
  and installs only `psutil` and `monotonic`, neither of which this tree
  imports — the test harness uses `time.monotonic()` from the standard
  library.

With both groups present, a configure of the CI configuration emits no
`CMake Warning`, `ctest --test-dir build -N` lists 24 tests including those
two, and the build reaches 124 `Built target` lines with a compile database of
453 entries of which 321 are C++23 — the counts to compare a local build
against. They hold only where these packages are installed: without readline
two of those targets and one of those entries are simply absent, and without
the python modules two of those tests are never registered.

### Build parallelism

Each of the five building jobs calls `./.github/actions/set-make-job-count`
first. The action budgets one logical core and 2.25 GiB of memory per compile
job and sets `MAKE_JOB_COUNT` to `max(1, min(nproc, MemTotal / 2.25 GiB))`,
reading `/proc/meminfo` and `nproc` on Linux and in the MSYS2 shell and
`sysctl hw.memsize` and `hw.logicalcpu` on macOS. The jobs then pass that
value to the build as `CMAKE_BUILD_PARALLEL_LEVEL`, so a runner with less
memory than it has cores compiles with fewer jobs instead of being OOM-killed.
The same rule is a reasonable starting point for choosing `-j` or
`--parallel` locally.

### Which floors continuous integration exercises

The "Toolchain requirements" section above states the floors and the status of
each; this is where that status comes from, because which floor a job actually
compiles with is not obvious from the job names.

- **GCC** is the only family exercised continuously. The two `build-linux`
  containers and `test-ubuntu` build with the distribution GCC of Debian 13
  and Ubuntu 24.04 — the two GCC rows of the matrix above — and `build-arch`
  builds with Arch's rolling GCC, far newer than the floor.
- **Apple Clang** is exercised only as whatever compiler `macOS-latest`
  currently ships, which is newer than the published Apple Clang 15 floor. The
  floor version itself is enforced by the configure-time compiler guard in the
  root `CMakeLists.txt`, and no job compiles the tree with it. Demonstrating
  it needs a pinned Xcode 15.x, which exists only on the `macos-14`
  runner-image family or on developer hardware, and that image family is being
  retired — so the opportunity to demonstrate it on a hosted runner is
  time-limited.
- **MinGW-w64 GCC** is exercised only by `build-windows`, in the MSYS2
  `ucrt64` environment, and its `pacboy` packages are a rolling toolchain
  rather than a pinned one: the UCRT64 repository currently ships GCC 16.2.0,
  CMake 4.4.3 and Boost 1.92.0, comfortably above the published MinGW-w64
  GCC 13 and CMake 3.25 floors and newer than any Boost release the matrix
  above discusses. What that job establishes is therefore that the current
  UCRT64 toolchain builds the tree, not that the floor version does.
- **Clang on Linux has no job at all.** Nothing in the workflow compiles the
  tree with Clang: that floor is enforced by the configure-time guard, and the
  Clang rows of the matrix above were measured by local builds rather than by
  CI, so the matrix and not CI is the statement of which Clang pairings work.

## To be done (and merged):
### Multihost parallel compilation
https://github.com/monero-project/monero/pull/7160

### Unity builds
https://github.com/monero-project/monero/pull/7217

