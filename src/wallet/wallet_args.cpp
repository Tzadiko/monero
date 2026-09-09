// Copyright (c) 2014-2026, The Monero Project
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#include "wallet/wallet_args.h"

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/format.hpp>
#include "common/i18n.h"
#include "common/util.h"
#include "misc_log_ex.h"
#include "string_tools.h"
#include "version.h"

#if defined(WIN32)
#include <crtdbg.h>
#endif

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "wallet.wallet2"

// workaround for a suspected bug in pthread/kernel on MacOS X
#ifdef __APPLE__
#define DEFAULT_MAX_CONCURRENCY 1
#else
#define DEFAULT_MAX_CONCURRENCY 0
#endif

namespace
{
  class Print
  {
  public:
    Print(const std::function<void(const std::string&, bool)> &p, bool em = false): print(p), emphasis(em) {}
    ~Print() { print(ss.str(), emphasis); }
    template<typename T> std::ostream &operator<<(const T &t) { ss << t; return ss; }
  private:
    const std::function<void(const std::string&, bool)> &print;
    std::stringstream ss;
    bool emphasis;
  };

  /*!
   * \brief Checks a --log-file path, returning why it is unusable.
   *
   * The logging layer refuses to write through a symbolic link or to anything that is not a
   * regular file, and continues with file logging disabled when it has to (mlog_configure() in
   * contrib/epee/src/mlog.cpp): appending log data through a link writes it outside the
   * directory the operator chose, and a FIFO or a device in place of the log file is never what
   * was meant. A wallet that was explicitly told where to log should not start with its log
   * silently going nowhere - the wallet log carries addresses, transaction ids and, at verbose
   * levels, whole RPC payloads - so the same rules are applied here, where the option name can
   * still be named in the message, exactly as the daemon does in src/daemon/main.cpp. The path
   * is only inspected, never created: creating it belongs to mlog_configure(), which does it
   * with the restrictive mode and the O_NOFOLLOW this check relies on.
   *
   * \param log_path The log file path as it will be passed to mlog_configure().
   * \return An empty string when the path is usable, otherwise the reason it is not.
   */
  std::string log_file_rejection_reason(const std::string &log_path)
  {
    namespace bf = boost::filesystem;

    boost::system::error_code ec;
    // symlink_status() does not follow the final component - the component under test.
    const bf::file_status status = bf::symlink_status(log_path, ec);
    if (status.type() == bf::file_not_found)
    {
      // The usual first run: the file is created by mlog_configure(), along with any missing
      // parent directory. Boost reports a missing path as file_not_found *and* sets ec, so only
      // an error other than "does not exist" matters here - a parent component that is not a
      // directory, or one that cannot be searched, also comes back as file_not_found.
      if (ec && ec != boost::system::errc::no_such_file_or_directory)
        return "it cannot be examined: " + ec.message();
      return std::string();
    }
    if (ec)
      return "it cannot be examined: " + ec.message();
    if (status.type() == bf::symlink_file)
      return "it is a symbolic link; specify the real file path";
    if (status.type() != bf::regular_file)
      return "it is not a regular file; specify a plain file path";
    return std::string();
  }
}

namespace wallet_args
{
  // Create on-demand to prevent static initialization order fiasco issues.
  command_line::arg_descriptor<std::string> arg_generate_from_json()
  {
    return {"generate-from-json", wallet_args::tr("Generate wallet from JSON format file"), ""};
  }
  command_line::arg_descriptor<std::string> arg_wallet_file()
  {
    return {"wallet-file", wallet_args::tr("Use wallet <arg>"), ""};
  }
  command_line::arg_descriptor<std::string> arg_password_file()
  {
    return {"password-file", wallet_args::tr("Wallet password file"), ""};
  }

  const char* tr(const char* str)
  {
    return i18n_translate(str, "wallet_args");
  }

  std::pair<boost::optional<boost::program_options::variables_map>, bool> main(
    int argc,
    const char* const argv[],
    const char* const usage,
    const char* const notice,
    boost::program_options::options_description desc_params,
    const boost::program_options::positional_options_description& positional_options,
    const std::function<void(const std::string&, bool)> &print,
    const char *default_log_name,
    bool log_to_console)
  
  {
    namespace bf = boost::filesystem;
    namespace po = boost::program_options;
#ifdef WIN32
    _CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif

    const command_line::arg_descriptor<std::string> arg_log_level = {"log-level", "0-4 or categories", ""};
    const command_line::arg_descriptor<std::size_t> arg_max_log_file_size = {"max-log-file-size", "Specify maximum log file size [B]", MAX_LOG_FILE_SIZE};
    const command_line::arg_descriptor<std::size_t> arg_max_log_files = {"max-log-files", "Specify maximum number of rotated log files to be saved (no limit by setting to 0)", MAX_LOG_FILES};
    const command_line::arg_descriptor<uint32_t> arg_max_concurrency = {"max-concurrency", wallet_args::tr("Max number of threads to use for a parallel job"), DEFAULT_MAX_CONCURRENCY};
    const command_line::arg_descriptor<std::string> arg_log_file = {"log-file", wallet_args::tr("Specify log file"), ""};
    const command_line::arg_descriptor<std::string> arg_config_file = {"config-file", wallet_args::tr("Config file"), "", true};


    tools::on_startup();
#ifdef NDEBUG
    tools::disable_core_dumps();
#endif
    tools::set_strict_default_file_permissions(true);

    epee::string_tools::set_module_name_and_folder(argv[0]);

    po::options_description desc_general(wallet_args::tr("General options"));
    command_line::add_arg(desc_general, command_line::arg_help);
    command_line::add_arg(desc_general, command_line::arg_version);

    command_line::add_arg(desc_params, arg_log_file);
    command_line::add_arg(desc_params, arg_log_level);
    command_line::add_arg(desc_params, arg_max_log_file_size);
    command_line::add_arg(desc_params, arg_max_log_files);
    command_line::add_arg(desc_params, arg_max_concurrency);
    command_line::add_arg(desc_params, arg_config_file);

    po::options_description desc_all;
    desc_all.add(desc_general).add(desc_params);
    po::variables_map vm;
    bool should_terminate = false;
    bool r = command_line::handle_error_helper(desc_all, [&]()
    {
      auto parser = po::command_line_parser(argc, argv).options(desc_all).positional(positional_options);
      po::store(parser.run(), vm);

      if (command_line::get_arg(vm, command_line::arg_help))
      {
        Print(print) << "Monero '" << MONERO_RELEASE_NAME << "' (v" << MONERO_VERSION_FULL << ")" << ENDL;
        Print(print) << wallet_args::tr("This is the command line monero wallet. It needs to connect to a monero\n"
												  "daemon to work correctly.") << ENDL;
        Print(print) << wallet_args::tr("Usage:") << ENDL << "  " << usage;
        Print(print) << desc_all;
        should_terminate = true;
        return true;
      }
      else if (command_line::get_arg(vm, command_line::arg_version))
      {
        Print(print) << "Monero '" << MONERO_RELEASE_NAME << "' (v" << MONERO_VERSION_FULL << ")";
        should_terminate = true;
        return true;
      }

      if(command_line::has_arg(vm, arg_config_file))
      {
        std::string config = command_line::get_arg(vm, arg_config_file);
        bf::path config_path(config);
        boost::system::error_code ec;
        if (bf::exists(config_path, ec))
        {
          // The value is about to be opened and parsed as a config file, so refuse
          // an existing name that cannot be one. A named pipe with no writer is
          // the case that matters: the open blocks forever, so the wallet never
          // starts and never explains why, not even when killed by a timeout.
          // This validation covers monero-wallet-cli, monero-wallet-rpc and
          // monero-gen-trusted-multisig, which all take --config-file through here.
          std::string config_error;
          if (!tools::validate_path_argument(arg_config_file.name, config, tools::path_argument_kind::existing_file, config_error))
          {
            MERROR(config_error);
            return false;
          }
          po::store(po::parse_config_file<char>(config_path.string<std::string>().c_str(), desc_params), vm);
        }
        else
        {
          // Echo the value through describe_path_argument(): the kernel accepts
          // 128 KiB in a single argument, and an unbounded echo of a wrong path
          // buries the message it belongs to.
          MERROR(wallet_args::tr("Can't find config file ") << tools::describe_path_argument(config));
          return false;
        }
      }

      po::notify(vm);
      return true;
    });
    if (!r)
      return {boost::none, true};

    if (should_terminate)
      return {std::move(vm), should_terminate};

    std::string log_path;
    if (!command_line::is_arg_defaulted(vm, arg_log_file))
    {
      log_path = command_line::get_arg(vm, arg_log_file);
      // mlog_configure() has no way to report a bad log path, so an unusable
      // value would leave the wallet running with no log file and no complaint,
      // and a named pipe with no writer would block the open before any logging
      // exists to report it. Validate here, while the option can still be named.
      std::string log_file_error;
      if (!tools::validate_path_argument(arg_log_file.name, log_path, tools::path_argument_kind::output_file, log_file_error))
      {
        Print(print, true) << log_file_error;
        return {boost::none, true};
      }
    }
    else
      log_path = mlog_get_default_log_path(default_log_name);

    // Reject an unusable log file before mlog_configure() is given it. mlog_configure()
    // enforces the same rules at the epee layer - it will not write through a symbolic link,
    // which would append log data outside the intended directory, nor to a FIFO or a device -
    // but all it can do then is carry on with file logging disabled, which for a wallet that
    // was told where to log means a running process and no log at all. Refusing here names the
    // option in the diagnostic; MERROR is the channel this function already uses for the
    // config-file failure above, which is likewise raised before logging is configured.
    const std::string log_file_rejection = log_file_rejection_reason(log_path);
    if (!log_file_rejection.empty())
    {
      MERROR(wallet_args::tr("Invalid --log-file ") << log_path << ": " << log_file_rejection);
      return {boost::none, true};
    }

    mlog_configure(log_path, log_to_console, command_line::get_arg(vm, arg_max_log_file_size), command_line::get_arg(vm, arg_max_log_files));
    if (!command_line::is_arg_defaulted(vm, arg_log_level))
    {
      mlog_set_log(command_line::get_arg(vm, arg_log_level).c_str());
    }
    else if (!log_to_console)
    {
      mlog_set_categories("");
    }

    if (notice)
      Print(print) << notice << ENDL;

    if (!command_line::is_arg_defaulted(vm, arg_max_concurrency))
      tools::set_max_concurrency(command_line::get_arg(vm, arg_max_concurrency));

    Print(print) << "Monero '" << MONERO_RELEASE_NAME << "' (v" << MONERO_VERSION_FULL << ")";

    if (!command_line::is_arg_defaulted(vm, arg_log_level))
      MINFO("Setting log level = " << command_line::get_arg(vm, arg_log_level));
    else
    {
      const char *logs = getenv("MONERO_LOGS");
      MINFO("Setting log levels = " << (logs ? logs : "<default>"));
    }
    MINFO(wallet_args::tr("Logging to: ") << log_path);

    Print(print) << boost::format(wallet_args::tr("Logging to %s")) % log_path;

    const ssize_t lockable_memory = tools::get_lockable_memory();
    if (lockable_memory >= 0 && lockable_memory < 256 * 4096) // 256 pages -> at least 256 secret keys and other such small/medium objects
      Print(print) << tr("WARNING: You may not have a high enough lockable memory limit")
#ifdef ELPP_OS_UNIX
        << ", " << tr("see ulimit -l")
#endif
        ;

    return {std::move(vm), should_terminate};
  }
}
