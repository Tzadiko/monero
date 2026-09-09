package=freebsd_base
# SECURITY / VERSION-CURRENCY NOTE (reviewed 2026-09-08) - SEC4-F20-eol-cross-sysroots
# The FreeBSD base sysroot pinned below is long end-of-life, so release artifacts for the
# x86_64-unknown-freebsd host compile against unpatched base headers and base libraries.
# Advisories: no CVE matches this coordinate specifically; the issue is the support window.
#   FreeBSD 12.3-RELEASE shipped 2021-12-07 and reached EOL 2023-03-31 (the whole stable/12
#   branch ended 2023-12-31), so nothing in this tarball has received a base security patch
#   since then. The download already comes from archive.freebsd.org/old-releases.
# Exposure here: limited to compile time. This is a sysroot supplying headers and base libraries
#   to the cross compiler, not a runtime component shipped to users; monero statically links its
#   own OpenSSL, and the stage step below already deletes usr/include/openssl so the stale base
#   headers cannot leak into the build (see the comment above $(package)_stage_cmds).
# Pin frozen deliberately: AAP 0.2.2 keeps every contrib/depends recipe version as pinned - it
#   names this sysroot explicitly - and AAP 0.6.5 states that bumping freebsd_base is not an
#   available remediation within the C++23 migration: it changes reproducible-build inputs and
#   is a maintainers' decision.
# Maintainer action when the freeze lifts: a coordinated sysroot bump re-pinning
#   $(package)_sha256_hash, then re-validating the old-libc++ cross pairs that AAP 0.10.5 gates
#   (x86_64-unknown-freebsd here, x86_64-apple-darwin and arm64-apple-darwin for darwin_sdk).
$(package)_version=12.3
$(package)_download_path=https://archive.freebsd.org/old-releases/amd64/$($(package)_version)-RELEASE/
$(package)_download_file=base.txz
$(package)_file_name=freebsd-base-$($(package)_version).txz
$(package)_sha256_hash=e85b256930a2fbc04b80334106afecba0f11e52e32ffa197a88d7319cf059840

define $(package)_extract_cmds
  echo $($(package)_sha256_hash) $($(1)_source_dir)/$($(package)_file_name) | sha256sum -c &&\
  tar xf $($(1)_source_dir)/$($(package)_file_name) ./lib/ ./usr/lib/ ./usr/include/
endef

# Prevent clang from including OpenSSL headers from the system base. We
# statically link our own version of OpenSSL.

define $(package)_stage_cmds
  mkdir -p $($(package)_staging_prefix_dir)/sysroot &&\
  rm -rf usr/include/openssl &&\
  mv lib usr $($(package)_staging_prefix_dir)/sysroot
endef
