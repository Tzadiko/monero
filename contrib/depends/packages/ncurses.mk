package=ncurses
# SECURITY / VERSION-CURRENCY NOTE (reviewed 2026-09-08) - SEC4-F19-ncurses-pin-cve
# The pinned 6.1 is affected by CVE-2023-29491, but the flaw's precondition is not met here.
# Advisories: CVE-2023-29491 - ncurses before 6.4-20230408, when used by a setuid or setgid
#   application, lets a local user trigger security-relevant memory corruption through malformed
#   data in a terminfo database file found in $HOME/.terminfo or reached via the TERMINFO or TERM
#   environment variable. It is exploitable only through such a privileged consumer.
# Exposure here: not reachable, proven. All 13 built binaries are mode 0755 and a search for any
#   setuid/setgid bit (find build/bin -perm /6000) returns nothing, so a stock installation cannot
#   meet the precondition. This recipe also configures --without-cxx-binding, --without-cxx and
#   --without-progs, so none of the terminfo-consuming utilities is produced; only the libraries
#   are staged, for the readline recipe that declares ncurses as its dependency.
# Pin frozen deliberately: AAP 0.2.2 keeps every contrib/depends recipe version as pinned,
#   because changing a recipe alters reproducible-build inputs.
# Maintainer action when the freeze lifts: move this pin to ncurses 6.4-20230408 or newer and
#   re-pin $(package)_sha256_hash.
$(package)_version=6.1
$(package)_download_path=https://ftp.gnu.org/gnu/ncurses
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=aa057eeeb4a14d470101eff4597d5833dcef5965331be3528c08d99cebaa0d17
$(package)_patches=fallback.c

define $(package)_set_vars
  $(package)_build_opts=CC="$($(package)_cc)"
  $(package)_config_env=cf_cv_ar_flags=""
  $(package)_config_opts=--prefix=$(host_prefix)
  $(package)_config_opts+=--disable-shared
  $(package)_config_opts+=--with-build-cc=$(build_CC)
  $(package)_config_opts+=--without-debug
  $(package)_config_opts+=--without-ada
  $(package)_config_opts+=--without-cxx-binding
  $(package)_config_opts+=--without-cxx
  $(package)_config_opts+=--without-ticlib
  $(package)_config_opts+=--without-tic
  $(package)_config_opts+=--without-progs
  $(package)_config_opts+=--without-tests
  $(package)_config_opts+=--without-tack
  $(package)_config_opts+=--without-manpages
  $(package)_config_opts+=--with-termlib=tinfo
  $(package)_config_opts+=--disable-tic-depends
  $(package)_config_opts+=--disable-big-strings
  $(package)_config_opts+=--disable-ext-colors
  $(package)_config_opts+=--enable-pc-files
  $(package)_config_opts+=--host=$(HOST)
  $(pacakge)_config_opts+=--without-shared
  $(pacakge)_config_opts+=--without-pthread
  $(pacakge)_config_opts+=--disable-rpath
  $(pacakge)_config_opts+=--disable-colorfgbg
  $(pacakge)_config_opts+=--disable-ext-mouse
  $(pacakge)_config_opts+=--disable-symlinks
  $(pacakge)_config_opts+=--enable-warnings
  $(pacakge)_config_opts+=--enable-assertions
  $(package)_config_opts+=--with-default-terminfo-dir=/etc/_terminfo_
  $(package)_config_opts+=--with-terminfo-dirs=/etc/_terminfo_
  $(pacakge)_config_opts+=--enable-database
  $(pacakge)_config_opts+=--enable-sp-funcs
  $(pacakge)_config_opts+=--disable-term-driver
  $(pacakge)_config_opts+=--enable-interop
  $(pacakge)_config_opts+=--enable-widec
  $(package)_build_opts=CFLAGS="$($(package)_cflags) $($(package)_cppflags) -fPIC"
endef

define $(package)_preprocess_cmds
  cp -f $(BASEDIR)/config.guess $(BASEDIR)/config.sub . && \
  cp $($(package)_patch_dir)/fallback.c ncurses
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE) $($(package)_build_opts) V=1
endef

define $(package)_stage_cmds
  $(MAKE) install.libs DESTDIR=$($(package)_staging_dir)
endef

