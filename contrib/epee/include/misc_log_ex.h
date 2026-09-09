// Copyright (c) 2006-2013, Andrey N. Sabelnikov, www.sabelnikov.net
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
// * Neither the name of the Andrey N. Sabelnikov nor the
// names of its contributors may be used to endorse or promote products
// derived from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER  BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//


#ifndef _MISC_LOG_EX_H_
#define _MISC_LOG_EX_H_

#ifdef __cplusplus

#include <sstream>
#include <string>
#include <string_view>

#include "easylogging++.h"

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "default"

#define MAX_LOG_FILE_SIZE 104850000 // 100 MB - 7600 bytes
#define MAX_LOG_FILES 10

#define LOG_TO_STRING(x) \
    std::stringstream ss; \
    ss << x; \
    const std::string str = ss.str();

#define MCLOG_TYPE(level, cat, color, type, x) do { \
    if (el::Loggers::allowed(level, cat)) { \
      LOG_TO_STRING(x); \
      el::base::Writer(level, color, __FILE__, __LINE__, ELPP_FUNC, type).construct(cat) << str; \
    } \
  } while (0)

#define MCLOG(level, cat, color, x) MCLOG_TYPE(level, cat, color, el::base::DispatchAction::NormalLog, x)
#define MCLOG_FILE(level, cat, x) MCLOG_TYPE(level, cat, el::Color::Default, el::base::DispatchAction::FileOnlyLog, x)

#define MCFATAL(cat,x) MCLOG(el::Level::Fatal,cat, el::Color::Default, x)
#define MCERROR(cat,x) MCLOG(el::Level::Error,cat, el::Color::Default, x)
#define MCWARNING(cat,x) MCLOG(el::Level::Warning,cat, el::Color::Default, x)
#define MCINFO(cat,x) MCLOG(el::Level::Info,cat, el::Color::Default, x)
#define MCDEBUG(cat,x) MCLOG(el::Level::Debug,cat, el::Color::Default, x)
#define MCTRACE(cat,x) MCLOG(el::Level::Trace,cat, el::Color::Default, x)

#define MCLOG_COLOR(level,cat,color,x) MCLOG(level,cat,color,x)
#define MCLOG_RED(level,cat,x) MCLOG_COLOR(level,cat,el::Color::Red,x)
#define MCLOG_GREEN(level,cat,x) MCLOG_COLOR(level,cat,el::Color::Green,x)
#define MCLOG_YELLOW(level,cat,x) MCLOG_COLOR(level,cat,el::Color::Yellow,x)
#define MCLOG_BLUE(level,cat,x) MCLOG_COLOR(level,cat,el::Color::Blue,x)
#define MCLOG_MAGENTA(level,cat,x) MCLOG_COLOR(level,cat,el::Color::Magenta,x)
#define MCLOG_CYAN(level,cat,x) MCLOG_COLOR(level,cat,el::Color::Cyan,x)

#define MLOG_RED(level,x) MCLOG_RED(level,MONERO_DEFAULT_LOG_CATEGORY,x)
#define MLOG_GREEN(level,x) MCLOG_GREEN(level,MONERO_DEFAULT_LOG_CATEGORY,x)
#define MLOG_YELLOW(level,x) MCLOG_YELLOW(level,MONERO_DEFAULT_LOG_CATEGORY,x)
#define MLOG_BLUE(level,x) MCLOG_BLUE(level,MONERO_DEFAULT_LOG_CATEGORY,x)
#define MLOG_MAGENTA(level,x) MCLOG_MAGENTA(level,MONERO_DEFAULT_LOG_CATEGORY,x)
#define MLOG_CYAN(level,x) MCLOG_CYAN(level,MONERO_DEFAULT_LOG_CATEGORY,x)

#define MFATAL(x) MCFATAL(MONERO_DEFAULT_LOG_CATEGORY,x)
#define MERROR(x) MCERROR(MONERO_DEFAULT_LOG_CATEGORY,x)
#define MWARNING(x) MCWARNING(MONERO_DEFAULT_LOG_CATEGORY,x)
#define MINFO(x) MCINFO(MONERO_DEFAULT_LOG_CATEGORY,x)
#define MDEBUG(x) MCDEBUG(MONERO_DEFAULT_LOG_CATEGORY,x)
#define MTRACE(x) MCTRACE(MONERO_DEFAULT_LOG_CATEGORY,x)
#define MLOG(level,x) MCLOG(level,MONERO_DEFAULT_LOG_CATEGORY,el::Color::Default,x)

#define MGINFO(x) MCINFO("global",x)
#define MGINFO_RED(x) MCLOG_RED(el::Level::Info, "global",x)
#define MGINFO_GREEN(x) MCLOG_GREEN(el::Level::Info, "global",x)
#define MGINFO_YELLOW(x) MCLOG_YELLOW(el::Level::Info, "global",x)
#define MGINFO_BLUE(x) MCLOG_BLUE(el::Level::Info, "global",x)
#define MGINFO_MAGENTA(x) MCLOG_MAGENTA(el::Level::Info, "global",x)
#define MGINFO_CYAN(x) MCLOG_CYAN(el::Level::Info, "global",x)

#define IFLOG(level, cat, color, type, init, x) \
  do { \
    if (el::Loggers::allowed(level, cat)) { \
      init; \
      LOG_TO_STRING(x); \
      el::base::Writer(level, color, __FILE__, __LINE__, ELPP_FUNC, type).construct(cat) << str; \
    } \
  } while(0)
#define MIDEBUG(init, x) IFLOG(el::Level::Debug, MONERO_DEFAULT_LOG_CATEGORY, el::Color::Default, el::base::DispatchAction::NormalLog, init, x)


#define LOG_ERROR(x) MERROR(x)
#define LOG_PRINT_L0(x) MWARNING(x)
#define LOG_PRINT_L1(x) MINFO(x)
#define LOG_PRINT_L2(x) MDEBUG(x)
#define LOG_PRINT_L3(x) MTRACE(x)
#define LOG_PRINT_L4(x) MTRACE(x)

#define _dbg3(x) MTRACE(x)
#define _dbg2(x) MDEBUG(x)
#define _dbg1(x) MDEBUG(x)
#define _info(x) MINFO(x)
#define _note(x) MDEBUG(x)
#define _fact(x) MDEBUG(x)
#define _mark(x) MDEBUG(x)
#define _warn(x) MWARNING(x)
#define _erro(x) MERROR(x)

#define MLOG_SET_THREAD_NAME(x) el::Helpers::setThreadName(x)

std::string mlog_get_default_log_path(const char *default_filename);
void mlog_configure(const std::string &filename_base, bool console, const std::size_t max_log_file_size = MAX_LOG_FILE_SIZE, const std::size_t max_log_files = MAX_LOG_FILES);
void mlog_set_categories(const char *categories);
std::string mlog_get_categories();
void mlog_set_log_level(int level);
void mlog_set_log(const char *log);

namespace epee
{

#define ENDL std::endl

#define TRY_ENTRY()   try {
#define CATCH_ENTRY(location, return_val) } \
  catch(const std::exception& ex) \
{ \
  (void)(ex); \
  LOG_ERROR("Exception at [" << location << "], what=" << ex.what()); \
  return return_val; \
}\
  catch(...)\
{\
  LOG_ERROR("Exception at [" << location << "], generic exception \"...\"");\
  return return_val; \
}

#define CATCH_ENTRY_SWALLOW_EX(location) } \
  catch(const std::exception& ex) \
{ \
  (void)(ex); \
  LOG_ERROR("Exception at [" << location << "], what=" << ex.what()); \
}\
  catch(...)\
{\
  LOG_ERROR("Exception at [" << location << "], generic exception \"...\"");\
}

#define CATCH_ENTRY_L0(lacation, return_val) CATCH_ENTRY(lacation, return_val)
#define CATCH_ENTRY_L1(lacation, return_val) CATCH_ENTRY(lacation, return_val)
#define CATCH_ENTRY_L2(lacation, return_val) CATCH_ENTRY(lacation, return_val)
#define CATCH_ENTRY_L3(lacation, return_val) CATCH_ENTRY(lacation, return_val)
#define CATCH_ENTRY_L4(lacation, return_val) CATCH_ENTRY(lacation, return_val)


#define ASSERT_MES_AND_THROW(message) {LOG_ERROR(message); std::stringstream ss; ss << message; throw std::runtime_error(ss.str());}
#define CHECK_AND_ASSERT_THROW_MES(expr, message) do {if(!(expr)) ASSERT_MES_AND_THROW(message);} while(0)


#ifndef CHECK_AND_ASSERT
#define CHECK_AND_ASSERT(expr, fail_ret_val)   do{if(!(expr)){return fail_ret_val;};}while(0)
#endif

#ifndef CHECK_AND_ASSERT_MES
#define CHECK_AND_ASSERT_MES(expr, fail_ret_val, message)   do{if(!(expr)) {LOG_ERROR(message); return fail_ret_val;};}while(0)
#endif

#ifndef CHECK_AND_NO_ASSERT_MES_L
#define CHECK_AND_NO_ASSERT_MES_L(expr, fail_ret_val, l, message)   do{if(!(expr)) {LOG_PRINT_L##l(message); return fail_ret_val;};}while(0)
#endif

#ifndef CHECK_AND_NO_ASSERT_MES
#define CHECK_AND_NO_ASSERT_MES(expr, fail_ret_val, message) CHECK_AND_NO_ASSERT_MES_L(expr, fail_ret_val, 0, message)
#endif

#ifndef CHECK_AND_NO_ASSERT_MES_L1
#define CHECK_AND_NO_ASSERT_MES_L1(expr, fail_ret_val, message) CHECK_AND_NO_ASSERT_MES_L(expr, fail_ret_val, 1, message)
#endif


#ifndef CHECK_AND_ASSERT_MES_NO_RET
#define CHECK_AND_ASSERT_MES_NO_RET(expr, message)   do{if(!(expr)) {LOG_ERROR(message); return;};}while(0)
#endif


#ifndef CHECK_AND_ASSERT_MES2
#define CHECK_AND_ASSERT_MES2(expr, message)   do{if(!(expr)) {LOG_ERROR(message); };}while(0)
#endif

enum console_colors
{
  console_color_default,
  console_color_white,
  console_color_red,
  console_color_green,
  console_color_blue,
  console_color_cyan,
  console_color_magenta,
  console_color_yellow
};

bool is_stdout_a_tty();
void set_console_color(int color, bool bright);
void reset_console_color();

//! Default cap, in input bytes, applied by \ref sanitize_for_log to a single logged value.
constexpr std::size_t MAX_LOG_VALUE_LENGTH = 1024;

/*! \brief Neutralise a caller-supplied value so it can be logged as a single log entry.

  Log entries are written one per physical line and the file layout is tab separated
  (`%datetime\t%thread\t%level\t%logger\t%loc\t%msg`), so a value that still holds a
  newline, a carriage return or a tab when it reaches a log sink lets whoever supplied
  that value split one logical entry into several physical lines, or forge extra fields
  inside one line - CWE-117, improper output neutralization for logs. Raw bytes at or
  above 0x80 additionally make the log file non-text, which defeats ordinary tooling
  (`grep` needs `-a`) and can render as unrelated glyphs on an operator's terminal.

  This function converts any byte sequence into printable 7-bit ASCII, so that the
  result cannot terminate a line, cannot introduce a field separator and cannot carry a
  terminal escape sequence:
    - `\` becomes `\\`, which makes the transformation reversible and unambiguous;
    - LF, CR and TAB become the two-character mnemonics `\n`, `\r` and `\t`;
    - every other byte below 0x20, plus 0x7F and every byte at or above 0x80, becomes
      `\xNN` with uppercase hexadecimal digits;
    - printable ASCII (0x20 to 0x7E, `\` excepted) is passed through unchanged.

  At most \p max_length input bytes are transformed; a longer value is truncated and the
  marker `...[truncated, N bytes total]` - naming the true input length - is appended, so
  that an unbounded caller-supplied value cannot be used to flood the log.

  It must be applied at the log call site, to the caller-supplied value only, and never
  to the surrounding message text: wrapping the message as a whole would escape the
  characters the log format itself relies on. Typical use:
  \code
    MERROR("[SendRawTxHex]: Failed to parse tx from hexbuff: " << epee::sanitize_for_log(req.tx_as_hex));
  \endcode

  \param value The untrusted value, treated as an opaque byte sequence; it may hold
    embedded NULs and need not be valid UTF-8.
  \param max_length Maximum number of input bytes transformed before truncation.
  \return The escaped value, guaranteed to contain only printable ASCII (0x20 to 0x7E)
    and therefore no line break, no tab and no non-ASCII byte.
  \note Throws only if the output allocation fails; the transformation itself cannot fail.
*/
inline std::string sanitize_for_log(const std::string_view value, const std::size_t max_length = MAX_LOG_VALUE_LENGTH)
{
  static constexpr const char hex_digits[] = "0123456789ABCDEF";
  static constexpr const char truncation_prefix[] = "...[truncated, ";
  static constexpr const char truncation_suffix[] = " bytes total]";

  const std::size_t kept_size = value.size() < max_length ? value.size() : max_length;
  const std::string_view kept = value.substr(0, kept_size);
  const bool truncated = kept_size < value.size();

  std::string out;
  // Worst case is four output characters (`\xNN`) per input byte; reserving that up
  // front means exactly one allocation, whatever the input looks like. kept_size is
  // bounded by max_length, so the reservation is bounded too.
  out.reserve((kept_size * 4) + (truncated ? sizeof(truncation_prefix) + sizeof(truncation_suffix) + 20 : 0));

  for (const char c : kept)
  {
    const unsigned char byte = static_cast<unsigned char>(c);
    switch (byte)
    {
      case '\\': out += "\\\\"; continue;
      case '\n': out += "\\n"; continue;
      case '\r': out += "\\r"; continue;
      case '\t': out += "\\t"; continue;
      default: break;
    }

    if (byte < 0x20 || byte >= 0x7F)
    {
      out += "\\x";
      out += hex_digits[byte >> 4];
      out += hex_digits[byte & 0x0F];
      continue;
    }

    out += c;
  }

  if (truncated)
  {
    out += truncation_prefix;
    out += std::to_string(value.size());
    out += truncation_suffix;
  }

  return out;
}

}

extern "C"
{

#endif

#ifdef __GNUC__
#define ATTRIBUTE_PRINTF __attribute__((format(printf, 2, 3)))
#else
#define ATTRIBUTE_PRINTF
#endif

bool merror(const char *category, const char *format, ...) ATTRIBUTE_PRINTF;
bool mwarning(const char *category, const char *format, ...) ATTRIBUTE_PRINTF;
bool minfo(const char *category, const char *format, ...) ATTRIBUTE_PRINTF;
bool mdebug(const char *category, const char *format, ...) ATTRIBUTE_PRINTF;
bool mtrace(const char *category, const char *format, ...) ATTRIBUTE_PRINTF;

#ifdef __cplusplus

}

#endif

#endif //_MISC_LOG_EX_H_
