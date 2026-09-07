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

<!-- markdownlint-disable MD013 (a table cell cannot be wrapped across lines) -->

| Compiler | Standard library / Boost | Status |
| --- | --- | --- |
| GCC ≥ 13 (floor) — 13.3.0 | libstdc++ 13 / Boost 1.83 | **Verified**; the Ubuntu 24.04 CI image and the reference Debian-13-class configuration |
| GCC 14.2.0 | libstdc++ 14 / Boost 1.83 | **Verified**; the Debian 13 CI image |
| Clang ≥ 16 (floor) — 16.0.6 | libstdc++ 13 / Boost 1.83 | **Verified** (`-std=c++2b`); the only verified Clang 16 pairing |
| Clang 16.0.6 | libstdc++ 14 | **Fails** at C++23 in five objects — a compiler/standard-library pairing defect, not first-party code; documented as unsupported |
| Clang 18.1.3 | libstdc++ 14 / Boost 1.91.0 | **Verified** and warning-clean |
| Clang 18.1.3 | libstdc++ 14 / Boost 1.83 | Builds and passes tests; emits one Boost-internal Beast deprecation — **conditional support requirement**: Clang ≥ 18 with libstdc++ ≥ 14 needs Boost ≥ 1.84 to be warning-clean |
| Clang 18.1.3, Debug, `STACK_TRACE` via libunwind 1.6.2 | libstdc++ 14 | **Verified** for `obj_common` only: 17 objects, including `src/common/stack_trace.cpp`, at C++23 with zero warnings; the only exercise of the libunwind path, which no CI job builds |
| Clang (any) | libc++ | **Not verified**; no CI or release path on Linux uses it |
| Apple Clang ≥ 15 (floor, Xcode 15 = LLVM 16 base) | Xcode libc++ / Homebrew Boost | **Declared, guard-enforced, not demonstrated**; the only continuous evidence is **CI-enforced**, on a newer Xcode (see below) |
| MinGW-w64 GCC ≥ 13 (floor) | libstdc++ 13 / MSYS2 or depends Boost | **CI-enforced only**: the Windows `ucrt64` job and the `x86_64-w64-mingw32` depends job |
| depends cross hosts and Guix (`gcc-15` Linux, `clang-toolchain-22` Darwin) | Boost 1.91.0, OpenSSL 3.5.7, ZeroMQ 4.3.5, protobuf 3.21.12 (all pinned) | **Not built here; pre-acceptance gate**: pinned, not verified (see below) |

<!-- markdownlint-enable MD013 -->

The Apple Clang row is the one floor in this matrix that has not been
demonstrated, and it is published as exactly that: declared, and enforced by
the configure-time guard, but not verified. Xcode 15 ships a compiler based on
LLVM 16, which makes it the Apple equivalent of the Clang 16 floor, but no
Apple compiler was available when the matrix was measured, so Xcode 15 itself
has never been configured, built or tested. The continuous evidence is the
`macOS (brew)` job, which builds and runs the reduced test tier on the newer
Xcode that `macOS-latest` ships.

The gate that would move this row to **Verified** is the `macOS (Xcode 15
floor)` job in `.github/workflows/build.yml`. It runs on a `macos-14` runner,
selects the newest Xcode 15.x the image carries with `xcode-select`, and then
performs the same configure, build and reduced test tier as the `macOS (brew)`
job; it is a `workflow_dispatch` job because no push needs it. That job has
not been run, so nothing in this repository claims Apple Clang 15 as verified,
and a configure with an Apple Clang in the 15 series says so on the spot: the
guard accepts it and prints a notice that the floor it meets is declared and
enforced rather than demonstrated, pointing here. Running the job and
recording in this row the exact Xcode and Apple Clang version it passed with
is what turns the row from declared into verified. Should Xcode 15 not pass, or
should it no longer be available on any runner image, the floor is raised to
the oldest Xcode that does pass — in this matrix, in `README.md` and in the
guard together, and the notice is removed with it.

The last row is a gate rather than a result. No depends cross host and no Guix
triple has been built at this dialect, so the Boost, OpenSSL, ZeroMQ and
protobuf versions in that row are the versions `contrib/depends` pins and not
versions this dialect has been compiled against. The gates are a `depends.yml`
run across all ten cross hosts — among them the three whose pinned libc++
predates C++23, the two Apple Darwin targets and FreeBSD — and a `guix.yml` run
on the candidate, repeated to confirm that the release binaries still
reproduce. Until both pass, this row
claims nothing beyond the pins; the remediation path if one of them fails is
"Cross-build dialect exceptions" below.

Unknown compiler IDs and the `clang-cl` frontend are rejected at configure
time, because this repository has no MSVC build path: Windows is built with
MinGW-w64 GCC through MSYS2 UCRT64. The guard branches on
`CMAKE_CXX_COMPILER_ID` and, for `clang-cl`, on
`CMAKE_CXX_COMPILER_FRONTEND_VARIANT`.

Those two are CMake's detection results, and they only describe the compiler
when they come from CMake's own detection, so the root build takes three
further steps.

Before `project()` it rejects a cache entry for any C or C++ detection output —
compiler id, compiler version, frontend variant, simulated id, and the computed
standard and extensions defaults — because such an entry, whether passed as
`-D` or left behind in a build directory, is what the guard would read instead
of the real value, and an under-floor compiler would then configure while being
reported as a supported one.

Before the guard it rejects any flag variable that defines or undefines a
compiler-identification macro (`__GNUC__`, `__clang__`, `__clang_major__`,
`__apple_build_version__`, `_MSC_VER` and their companions), in
`CMAKE_C_FLAGS`, `CMAKE_CXX_FLAGS` and their per-configuration variants,
which is also where `CFLAGS` and `CXXFLAGS` from the environment arrive.
Overriding one of those macros changes what CMake's detection reports and what
every compile of the tree sees at the same time, so it would otherwise defeat
both the guard and the probe below; no build needs to override them, and the
standard library keys its own feature tests off them.

After the guard it compiles a small probe with the selected compiler and
rejects it on the compiler's own predefined macros. The probe holds the floors
even where the detection results do not describe the compiler at all — a
toolchain file that declares an identity and suppresses detection, or a driver
that reports one version while preprocessing as another. It keeps the project's
flags, so a cross build still gets its `--target` and `--sysroot`, with any
identification-macro definition stripped, and it runs `NO_CACHE` so that its
result lands in the normal variable the check reads: under CMP0126 a normal
variable shadows a cache entry of the same name, so a cached result could
otherwise be stood in for by a pre-seeded normal one.

The guard checks compiler family, frontend variant and version, but it does
not inspect the standard library. It therefore cannot detect the Clang 16 with
libstdc++ 14 pairing defect, which is a header-library mismatch rather than a
compiler version, and that is exactly why the matrix carries that row:
such a failure surfaces only when the affected translation units are compiled,
so this table is the only place it is documented.

### Libraries

Boost 1.69 is the declared floor, unchanged by the move to C++23; 1.83 and
1.91.0 are the versions verified under C++23. Clang 18 or newer with libstdc++
14 or newer must use Boost 1.84 or newer for a warning-clean build. With Boost
1.83 that combination builds and passes its tests, but Beast's use of the
deprecated `std::aligned_storage` produces one Boost-internal warning origin
(eight diagnostics in the measured full build), reached
through the tree's single Beast consumer,
`tests/unit_tests/epee_http_server.cpp`; Boost 1.84.0 replaced that use
upstream, so releases from 1.84 onwards do not carry it. Boost 1.69 through
1.82 were not built or tested at this dialect in any pairing, so nothing is
claimed for them here beyond the declared floor. Nothing is suppressed for any
of this — no `-Wno-*` flag and no diagnostic pragma is added anywhere — the
resolution is the newer Boost.

OpenSSL 1.1.1 is the declared floor and is likewise unchanged: it is a C API
consumed through `extern "C"`, so the C++ dialect cannot move it. 3.0.13 is the
version verified against, and 3.5.7 is the version pinned in `contrib/depends`.
All three are compatibility and evidence coordinates rather than versions to
deploy; "Dependency security posture" below says which OpenSSL to actually
build against.

Raising either declared floor is a separate maintainers' decision, not a
consequence of the move to C++23.

### Dependency security posture

A version in this file or in the README dependency table answers one of three
questions — what the build accepts, what this dialect was verified against, or
what `contrib/depends` pins for a deterministic build — and none of them is
"what is safe to run". This section answers that last question for the
libraries where the two answers differ, so that no number published elsewhere
in the repository is read as security advice.

<!-- markdownlint-disable MD013 (a table cell cannot be wrapped across lines) -->

| Dependency | Coordinate in this repository | Security status | Build and run against |
| --- | --- | --- | --- |
| OpenSSL | declared floor 1.1.1 (`find_package(OpenSSL 1.1.1 REQUIRED)`), 3.0.13 verified, 3.5.7 pinned in `contrib/depends/packages/openssl.mk` | 1.1.1 is out of public support, 3.0.13 is behind its own branch's current 3.0.22, and 3.5.7 predates the fixes in 3.5.8 | 3.5.8 or newer on the 3.5 branch and 3.6.4 or newer on 3.6; on any other branch, that branch's current release |
| libzmq | declared floor 4.2.0, 4.3.5 pinned in `contrib/depends/packages/zeromq.mk`, no version enforced by the build (`pkg_check_modules(libzmq REQUIRED IMPORTED_TARGET libzmq)`) | 4.2.0 is inside the affected range of CVE-2019-6250 (remote code execution) and of CVE-2020-15166 | 4.3.5 or newer |
| libunbound | declared floor 1.4.16, 1.25.2 pinned in `contrib/depends/packages/unbound.mk`, no version enforced by the build | the 2012 floor predates CVE-2014-8602 and every later fix; separately, 1.19.1 through 1.25.0 are affected by CVE-2026-33278 (CVSS 9.8, a DNSSEC-validator use-after-free reachable through any name the daemon resolves), fixed in 1.25.1 | 1.25.2 or newer |
| libsodium | no floor declared, 1.0.18 pinned in `contrib/depends/packages/sodium.mk`, no version enforced by the build | 1.0.18 predates the later security releases and the hardening added around AEAD verification | 1.0.21 or newer |
| libreadline | declared floor 6.3.0 (optional dependency), 8.0 pinned in `contrib/depends/packages/readline.mk`, no version enforced by the build | a bare 6.3.0 permits CVE-2014-2524, which 6.3 patch 3 fixes | 8.0 or newer |
| MSYS2 UCRT64 packages | the `pacman -S mingw-w64-ucrt-x86_64-…` line in the README Windows section installs whatever the MSYS2 repository currently offers | the unbound package has been 1.24.2, inside the CVE-2026-33278 range, and the OpenSSL package 3.6.1, below 3.6.4 | unbound 1.25.2 or newer and OpenSSL 3.6.4 or newer, checked with `pacman -Qi` before any binary from that environment is published |
| FreeBSD cross sysroot | `freebsd_base` 12.3 pinned in `contrib/depends/packages/freebsd_base.mk` and consumed by `contrib/depends/hosts/freebsd.mk` | the 12.3-RELEASE base archive has been end of life since 2023, receives no updates, and predates the libc fix in FreeBSD-SA-23:15.stdio (CVE-2023-5941) | nothing available: the target is security-blocked for published artifacts, see "Cross-build dialect exceptions" |

<!-- markdownlint-enable MD013 -->

Two things follow from that table, and both are deliberate. First, none of
these numbers is raised by the move to C++23. A declared floor is a
compatibility statement and a `contrib/depends` version is a
reproducible-build input; neither is a dialect question, and moving one is a
maintainers' decision taken on its own evidence, which is why the floors and
every pin are unchanged here. What this documentation owes instead is that the
numbers it does publish cannot be mistaken for a safe configuration — the
purpose of this section and of the note under the README dependency table.
Second, apart from OpenSSL's 1.1.1 and Boost's 1.69 the build enforces none of
these versions, so the operator or packager chooses them. Current distribution
packages satisfy every row above, which leaves the pinned deterministic
builds, and therefore the OpenSSL, libsodium and FreeBSD rows, as the ones
where only a change in this repository can move the version.

Acting on those rows means raising `openssl.mk` to 3.5.8 or newer and
`sodium.mk` to 1.0.21 or newer with their new hashes, raising the README
floors for libzmq, libunbound and libreadline to the last column (and, to have
them enforced rather than documented, adding the version to the
`pkg_check_modules` and `find_package` calls that locate them, at the cost of
refusing to configure on hosts that ship an older one), and authorizing a
supported, patched, reproducible FreeBSD sysroot before FreeBSD artifacts are
published. Each of those alters a package build id and invalidates the
matching `contrib/depends` cache, so they belong to a deliberate dependency
update with its own testing rather than to a language-standard migration.

### Rust

Rust and `cargo` are mandatory on `master` — `src/fcmp_pp/fcmp_pp_rust` is
built unconditionally — so both must be on `PATH` before CMake is configured.
`src/fcmp_pp/fcmp_pp_rust/Cargo.toml` declares no `rust-version`, so the
repository states no minimum supported Rust version; 1.93, installed through
rustup, is the toolchain CI tests with.

### Cross-build dialect exceptions

The Darwin (Xcode 12.2) and FreeBSD 12.3 sysroots pinned by `contrib/depends`
ship a libc++ that predates the C++23 library additions, which is why no C++23
library facility is used in the tree; any future use has to be gated on the
matching `__cpp_lib_*` feature-test macro from `<version>` with the existing
implementation kept as the fallback.

The FreeBSD sysroot carries a second problem, which is not about the dialect.
`freebsd_base` 12.3 is the 12.3-RELEASE base archive from
`archive.freebsd.org/old-releases`, and 12.3 has been end of life since 2023:
the archive is immutable, receives no security updates, and predates the libc
fix in FreeBSD-SA-23:15.stdio (CVE-2023-5941). The dynamic base libraries of a
FreeBSD binary are resolved on the machine that runs it, so a patched host
supplies patched libraries, but everything the compiler inlines from that
sysroot — the libc++ templates and the inline parts of libc — is compiled into
the artifact from an unpatched base, and any part of the base linked
statically is that base's version. `x86_64-unknown-freebsd` is therefore
security-blocked for published artifacts: build and test it as much as is
useful, but a binary from this sysroot must not be published as a supported
FreeBSD release until a supported, patched and reproducible sysroot has been
authorized. Doing that is a release-engineering decision — it changes a
reproducible-build input, exactly like the third remediation below — and it is
the only thing that lifts the block; nothing in the build can compensate for
it. The status of the pin is recorded in
`contrib/depends/packages/freebsd_base.mk` and in "Dependency security
posture" above.

Should anything fail to compile against one of those sysroots, the remediation
is fixed in advance and is applied in this order. First, a failing depends
recipe — Boost, ZeroMQ or protobuf — gets a per-recipe, per-host exception
through that recipe's `$(package)_cxxflags_$(host_os)` hook, adding
`-std=c++17` for that one recipe on that one host while the first-party dialect
stays at 23, and the exception is recorded in this section. Second, a failure
in first-party code against the old libc++ is by construction a
missing-library-facility failure, since no C++23 library facility is used
anywhere in the tree, so it is treated as a defect in the migration and fixed
at the source site, with first-party code staying at C++23; it is never worked
around by lowering the first-party dialect. Third, bumping either sysroot is
deliberately not an available remediation, because it changes
reproducible-build inputs and is a maintainers' decision. No such exception is
currently in force.

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

