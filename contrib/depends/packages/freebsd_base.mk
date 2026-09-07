# 12.3-RELEASE has been end of life since 2023. This base archive is immutable,
# receives no security updates, and predates the libc fix in
# FreeBSD-SA-23:15.stdio (CVE-2023-5941), so code inlined from its headers and
# any statically linked base component carries the unpatched version.
# x86_64-unknown-freebsd is therefore security-blocked for published artifacts
# until a supported, patched, reproducible sysroot is authorized: see
# docs/COMPILING_DEBUGGING_TESTING.md, "Cross-build dialect exceptions".
# Replacing this pin is a release-engineering decision, not a build fix.
package=freebsd_base
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
