package=sodium
# SECURITY / VERSION-CURRENCY NOTE (reviewed 2026-09-08) - SEC4-F17-depends-libsodium-cve
# The pinned 1.0.18 release tarball (2019-05-30) predates the fix for CVE-2025-69277.
# Advisories: CVE-2025-69277 (Medium, CVSS 3.1 base 4.5, AV:L/AC:H/PR:N/UI:N/S:C/C:L/I:L/A:N) -
#   crypto_core_ed25519_is_valid_point accepted points outside the main cryptographic group,
#   because the post-multiply-by-L identity test checked X==0 but not Y==Z. Only atypical
#   low-level custom-crypto callers reach it; the high-level APIs are unaffected. Fixed by
#   upstream commit ad3004e, first released in 1.0.21-RELEASE (2026-01-06) and backported by
#   Debian into 1.0.18-1+deb12u1 / 1.0.18-1+deb13u1 (DSA-6094-1, 2026-01-05).
# Exposure here: not reachable, proven at symbol level. The 13 built binaries import three
#   libsodium symbols in total - crypto_shorthash_siphash24, crypto_verify_32 and, in the wallet
#   binaries only, crypto_aead_chacha20poly1305_ietf_decrypt - and zero crypto_core_ed25519*
#   symbols, so the vulnerable entry point is never linked. Monero's own ed25519 arithmetic
#   lives in src/crypto and external/supercop, not in libsodium.
# Pin frozen deliberately: AAP 0.2.2 keeps every contrib/depends recipe version as pinned,
#   because changing a recipe alters reproducible-build inputs.
# Maintainer action when the freeze lifts: move to 1.0.21 or newer, the first upstream release
#   carrying ad3004e, re-pinning $(package)_sha256_hash - or carry ad3004e as a
#   $(package)_patches entry against 1.0.18.
$(package)_version=1.0.18
$(package)_download_path=https://github.com/jedisct1/libsodium/releases/download/$($(package)_version)-RELEASE
$(package)_file_name=libsodium-$($(package)_version).tar.gz
$(package)_sha256_hash=6f504490b342a4f8a4c4a02fc9b866cbef8622d5df4e5452b46be121e46636c1

define $(package)_set_vars
$(package)_config_opts=--enable-static --disable-shared
$(package)_config_opts+=--prefix=$(host_prefix)
endef

define $(package)_preprocess_cmds
  cp -f $(BASEDIR)/config.guess $(BASEDIR)/config.sub build-aux
endef

define $(package)_config_cmds
  $($(package)_autoconf) AR_FLAGS=$($(package)_arflags)
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef

define $(package)_postprocess_cmds
  rm lib/*.la
endef

