package=openssl

# SECURITY / VERSION-CURRENCY NOTE (reviewed 2026-09-08) - SEC4-F16-depends-openssl-357-cves
# The 3.5.7 pin below is affected by four CVEs, all fixed in OpenSSL 3.5.8.
# Advisories (affected from 3.5.0 before 3.5.8; OpenSSL rates the most severe of them
#   Moderate): CVE-2026-14456 unbounded memory growth in the QUIC server incoming channel
#   queue; CVE-2026-14457 RFC7250 raw-public-key server signature-algorithm selection can
#   dereference a missing certificate; CVE-2026-18798 QUIC server double free while
#   processing an INITIAL packet; CVE-2026-54874 excessive memory use buffering DTLS
#   records for a future epoch.
# Exposure here: NOT REACHABLE - monero uses OpenSSL only for TLS through
#   boost::asio::ssl plus hashing. A dynamic-symbol audit of the built binaries found 252
#   genuine OpenSSL imports (SSL_CTX_*, EVP_*, X509_*) and zero QUIC, DTLS, OCSP, CMP,
#   CMS, PKCS#7, PKCS#12 or raw-public-key entry points; this recipe additionally
#   configures no-dtls1, no-ssl3 and no-sctp below, compiling part of that surface out.
# Pin frozen deliberately: AAP 0.2.2 freezes every contrib/depends recipe version to
#   preserve reproducible-build inputs.
# Maintainer action when the freeze lifts: move the version to 3.5.8 with its new
#   sha256_hash - 3.5 is the supported LTS branch (to 2030-04-08), so that is a
#   patch-level move inside the same branch.
$(package)_version=3.5.7
$(package)_download_path=https://github.com/openssl/openssl/releases/download/openssl-$($(package)_version)
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=a8c0d28a529ca480f9f36cf5792e2cd21984552a3c8e4aa11a24aa31aeac98e8
$(package)_patches=fix-android.patch

define $(package)_set_vars
$(package)_config_env=AR="$($(package)_ar)" RANLIB="$($(package)_ranlib)" CC="$($(package)_cc)"
$(package)_config_env_android=ANDROID_NDK_ROOT="$(host_prefix)/native" PATH="$(host_prefix)/native/bin"
$(package)_build_env_android=ANDROID_NDK_ROOT="$(host_prefix)/native"
$(package)_config_opts=--prefix=$(host_prefix) --openssldir=$(host_prefix)/etc/openssl --libdir=$(host_prefix)/lib
$(package)_config_opts+=no-apps
$(package)_config_opts+=no-capieng
$(package)_config_opts+=no-dso
$(package)_config_opts+=no-dtls1
$(package)_config_opts+=no-ec_nistp_64_gcc_128
$(package)_config_opts+=no-gost
$(package)_config_opts+=no-md2
$(package)_config_opts+=no-rc5
$(package)_config_opts+=no-rdrand
$(package)_config_opts+=no-rfc3779
$(package)_config_opts+=no-sctp
$(package)_config_opts+=no-shared
$(package)_config_opts+=no-ssl-trace
$(package)_config_opts+=no-ssl3
$(package)_config_opts+=no-tests
$(package)_config_opts+=no-unit-test
$(package)_config_opts+=no-weak-ssl-ciphers
$(package)_config_opts+=no-winstore
$(package)_config_opts+=no-zlib
$(package)_config_opts+=no-zlib-dynamic
$(package)_config_opts_linux=-fPIC -Wa,--noexecstack
$(package)_config_opts_freebsd=-fPIC -Wa,--noexecstack
$(package)_config_opts_x86_64_linux=linux-x86_64
$(package)_config_opts_i686_linux=linux-generic32
$(package)_config_opts_arm_linux=linux-generic32
$(package)_config_opts_aarch64_linux=linux-generic64
$(package)_config_opts_arm_android=--static android-arm
$(package)_config_opts_aarch64_android=--static android-arm64
$(package)_config_opts_aarch64_darwin=darwin64-arm64-cc
$(package)_config_opts_riscv64_linux=linux64-riscv64
$(package)_config_opts_loongarch64_linux=linux-generic64
$(package)_config_opts_mipsel_linux=linux-generic32
$(package)_config_opts_mips_linux=linux-generic32
$(package)_config_opts_powerpc_linux=linux-generic32
$(package)_config_opts_x86_64_darwin=darwin64-x86_64-cc
$(package)_config_opts_x86_64_mingw32=mingw64
$(package)_config_opts_i686_mingw32=mingw
$(package)_config_opts_x86_64_freebsd=BSD-x86_64
endef

define $(package)_preprocess_cmds
  sed -i.old 's|crypto ssl apps util tools fuzz providers doc|crypto ssl util tools providers|' build.info && \
  patch -p1 < $($(package)_patch_dir)/fix-android.patch && \
  rm -rf doc demos apps test
endef

define $(package)_config_cmds
  ./Configure $($(package)_config_opts) ARFLAGS=$($(package)_arflags)
endef

define $(package)_build_cmds
  $(MAKE) build_libs
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install_sw
endef

define $(package)_postprocess_cmds
  rm -rf share bin etc
endef
