package=darwin_sdk
# SECURITY / VERSION-CURRENCY NOTE (reviewed 2026-09-08) - SEC4-F20-eol-cross-sysroots
# The macOS SDK pinned below is long end-of-life: it is the Xcode 12.2 (build 12B45b) SDK from
# November 2020, so release artifacts for the apple-darwin hosts compile against unpatched SDK
# headers and against libc++ headers of that vintage.
# Advisories: no CVE matches this coordinate specifically; the issue is the support window - the
#   SDK is nearly six years old and Apple ships security fixes only in current Xcode releases.
# Exposure here: limited to compile time. This is a sysroot supplying headers and stub libraries
#   to the cross compiler, not a runtime component shipped to users; monero statically links its
#   own readline, and the stage step below already deletes usr/include/readline so the stale SDK
#   headers cannot leak into the build (see the comment above $(package)_stage_cmds).
# Pin frozen deliberately: AAP 0.2.2 keeps every contrib/depends recipe version as pinned - it
#   names this SDK explicitly - and AAP 0.6.5 states that bumping darwin_sdk is not an available
#   remediation within the C++23 migration: it changes reproducible-build inputs and is a
#   maintainers' decision.
# Maintainer action when the freeze lifts: a coordinated sysroot bump re-pinning
#   $(package)_file_name and $(package)_sha256_hash, then re-validating the old-libc++ cross pairs
#   that AAP 0.10.5 gates (x86_64-apple-darwin and arm64-apple-darwin here, plus
#   x86_64-unknown-freebsd for freebsd_base).
$(package)_version=12.2
$(package)_download_path=https://bitcoincore.org/depends-sources/sdks
$(package)_file_name=Xcode-12.2-12B45b-extracted-SDK-with-libcxx-headers.tar.gz
$(package)_sha256_hash=df75d30ecafc429e905134333aeae56ac65fac67cb4182622398fd717df77619

# Prevent clang from including readline headers from the SDK. We statically link
# our own version of readline.

define $(package)_stage_cmds
  mkdir -p $($(package)_staging_prefix_dir)/SDK &&\
  rm -rf usr/include/readline && \
  mv * $($(package)_staging_prefix_dir)/SDK
endef
