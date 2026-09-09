// Copyright (c) 2023-2024, The Monero Project
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

#include "gtest/gtest.h"

#include <boost/filesystem.hpp>
#include <fstream>
#include <string>
#include <sys/stat.h>

#include "common/util.h"
#include "file_io_utils.h"

TEST(LocalAddress, localhost) { ASSERT_TRUE(tools::is_local_address("localhost")); }
TEST(LocalAddress, localhost_port) { ASSERT_TRUE(tools::is_local_address("localhost:18081")); }
TEST(LocalAddress, localhost_suffix) { ASSERT_TRUE(tools::is_local_address("test.localhost")); }
TEST(LocalAddress, loopback) { ASSERT_TRUE(tools::is_local_address("127.0.0.1")); }
TEST(LocalAddress, loopback_port) { ASSERT_TRUE(tools::is_local_address("127.0.0.1:18081")); }
TEST(LocalAddress, loopback_protocol) { ASSERT_TRUE(tools::is_local_address("http://127.0.0.1")); }
TEST(LocalAddress, loopback_hi) { ASSERT_TRUE(tools::is_local_address("127.255.255.255")); }
TEST(LocalAddress, loopback_lo) { ASSERT_TRUE(tools::is_local_address("127.0.0.0")); }
TEST(LocalAddress, loopback_ipv6) { ASSERT_TRUE(tools::is_local_address("[0:0:0:0:0:0:0:1]")); }

TEST(LocalAddress, onion) { ASSERT_FALSE(tools::is_local_address("vww6ybal4bd7szmgncyruucpgfkqahzddi37ktceo3ah7ngmcopnpyyd.onion")); }
TEST(LocalAddress, i2p) { ASSERT_FALSE(tools::is_local_address("xmrto2bturnore26xmrto2bturnore26xmrto2bturnore26xmr2.b32.i2p")); }
TEST(LocalAddress, valid_ip) { ASSERT_FALSE(tools::is_local_address("1.2.3.4")); }
TEST(LocalAddress, valid_ipv6) { ASSERT_FALSE(tools::is_local_address("[0:0:0:0:0:0:0:2]")); }
TEST(LocalAddress, valid_domain) { ASSERT_FALSE(tools::is_local_address("getmonero.org")); }
TEST(LocalAddress, local_prefix) { ASSERT_FALSE(tools::is_local_address("localhost.com")); }
TEST(LocalAddress, invalid) { ASSERT_FALSE(tools::is_local_address("test")); }
TEST(LocalAddress, empty) { ASSERT_FALSE(tools::is_local_address("")); }

namespace
{
  /*! A directory that exists only for the duration of one test, so that the
      path-argument cases below can create the exact filesystem objects they are
      about (a regular file, a FIFO, a symbolic link) instead of hoping to find
      them on the host. */
  class temp_dir
  {
  public:
    temp_dir()
      : m_path(boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("monero-util-test-%%%%-%%%%"))
    {
      boost::filesystem::create_directories(m_path);
    }

    ~temp_dir()
    {
      boost::system::error_code ignored;
      boost::filesystem::remove_all(m_path, ignored);
    }

    temp_dir(const temp_dir&) = delete;
    temp_dir& operator=(const temp_dir&) = delete;

    std::string operator()(const std::string& name) const { return (m_path / name).string(); }
    const boost::filesystem::path& path() const { return m_path; }

  private:
    boost::filesystem::path m_path;
  };

  bool write_file(const std::string& path, const std::string& contents)
  {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << contents;
    f.close();
    return !f.fail();
  }
}

TEST(PathArgument, describe_short_path_verbatim)
{
  EXPECT_EQ("'/tmp/monero.log'", tools::describe_path_argument("/tmp/monero.log"));
}

TEST(PathArgument, describe_bounds_absurd_path)
{
  // The kernel accepts up to 128 KiB in a single argument; a diagnostic must not
  // echo all of it (the daemon accepted a 131,000 byte --log-file and ran).
  const std::string absurd = std::string(131000, 'A') + "/monero.log";
  const std::string described = tools::describe_path_argument(absurd);
  EXPECT_LT(described.size(), 300u);
  EXPECT_NE(std::string::npos, described.find("131011 bytes"));
  // The filename at the end of the path stays visible, because that is the part
  // that identifies the file to whoever reads the message.
  EXPECT_NE(std::string::npos, described.find("/monero.log'"));
}

TEST(PathArgument, describe_shows_ordinary_long_path_in_full)
{
  // A 200-character path is unremarkable inside a container or a temporary
  // directory, and eliding it would hide the filename the reader is looking for.
  const std::string ordinary = "/tmp/" + std::string(180, 'd') + "/monero.log";
  EXPECT_EQ("'" + ordinary + "'", tools::describe_path_argument(ordinary));
}

TEST(PathArgument, refuses_empty)
{
  std::string error;
  EXPECT_FALSE(tools::validate_path_argument("config-file", "", tools::path_argument_kind::existing_file, error));
  EXPECT_NE(std::string::npos, error.find("--config-file"));
}

TEST(PathArgument, refuses_over_length)
{
  std::string error;
  const std::string absurd(tools::max_path_argument_length() + 1, 'A');
  EXPECT_FALSE(tools::validate_path_argument("log-file", absurd, tools::path_argument_kind::output_file, error));
  EXPECT_NE(std::string::npos, error.find("--log-file"));
  EXPECT_LT(error.size(), 512u);
}

TEST(PathArgument, refuses_embedded_nul)
{
  std::string error;
  std::string path("/tmp/monero");
  path.push_back('\0');
  path += "ignored";
  EXPECT_FALSE(tools::validate_path_argument("log-file", path, tools::path_argument_kind::output_file, error));
}

TEST(PathArgument, accepts_regular_file_as_input)
{
  const temp_dir dir;
  const std::string file = dir("regular");
  ASSERT_TRUE(write_file(file, "data\n"));
  std::string error;
  EXPECT_TRUE(tools::validate_path_argument("config-file", file, tools::path_argument_kind::existing_file, error));
  EXPECT_TRUE(error.empty());
}

TEST(PathArgument, refuses_missing_input_file)
{
  const temp_dir dir;
  std::string error;
  EXPECT_FALSE(tools::validate_path_argument("input-file", dir("absent"), tools::path_argument_kind::existing_file, error));
}

TEST(PathArgument, refuses_directory_as_input_file)
{
  const temp_dir dir;
  std::string error;
  EXPECT_FALSE(tools::validate_path_argument("input-file", dir.path().string(), tools::path_argument_kind::existing_file, error));
  EXPECT_NE(std::string::npos, error.find("directory"));
}

TEST(PathArgument, accepts_absent_output_file)
{
  const temp_dir dir;
  std::string error;
  EXPECT_TRUE(tools::validate_path_argument("log-file", dir("not-there-yet.log"), tools::path_argument_kind::output_file, error));
}

TEST(PathArgument, accepts_absent_directory)
{
  const temp_dir dir;
  std::string error;
  EXPECT_TRUE(tools::validate_path_argument("wallet-dir", dir("to-be-created"), tools::path_argument_kind::directory, error));
}

TEST(PathArgument, refuses_regular_file_as_directory)
{
  const temp_dir dir;
  const std::string file = dir("regular");
  ASSERT_TRUE(write_file(file, "data\n"));
  std::string error;
  EXPECT_FALSE(tools::validate_path_argument("wallet-dir", file, tools::path_argument_kind::directory, error));
  EXPECT_NE(std::string::npos, error.find("not a directory"));
}

TEST(PathArgument, refuses_existing_new_file)
{
  const temp_dir dir;
  const std::string file = dir("already-here");
  ASSERT_TRUE(write_file(file, "precious\n"));
  std::string error;
  EXPECT_FALSE(tools::validate_path_argument("private-key-filename", file, tools::path_argument_kind::new_file, error));
  EXPECT_NE(std::string::npos, error.find("already exists"));
}

TEST(PathArgument, accepts_new_file)
{
  const temp_dir dir;
  std::string error;
  EXPECT_TRUE(tools::validate_path_argument("private-key-filename", dir("fresh.key"), tools::path_argument_kind::new_file, error));
}

TEST(PathArgument, refuses_new_file_in_missing_directory)
{
  const temp_dir dir;
  std::string error;
  EXPECT_FALSE(tools::validate_path_argument("certificate-filename", dir("no-such-dir/cert.crt"), tools::path_argument_kind::new_file, error));
}

#ifndef _WIN32
TEST(PathArgument, refuses_fifo_where_a_file_is_expected)
{
  const temp_dir dir;
  const std::string fifo = dir("pipe");
  ASSERT_EQ(0, mkfifo(fifo.c_str(), 0600));
  std::string error;
  // Every one of these three would otherwise open the FIFO and block forever.
  EXPECT_FALSE(tools::validate_path_argument("config-file", fifo, tools::path_argument_kind::existing_file, error));
  EXPECT_NE(std::string::npos, error.find("FIFO"));
  EXPECT_FALSE(tools::validate_path_argument("log-file", fifo, tools::path_argument_kind::output_file, error));
  EXPECT_FALSE(tools::validate_path_argument("wallet-dir", fifo, tools::path_argument_kind::directory, error));
}

TEST(PathArgument, refuses_symlink_as_new_file)
{
  const temp_dir dir;
  const std::string target = dir("target");
  const std::string link = dir("link");
  ASSERT_TRUE(write_file(target, "precious\n"));
  boost::system::error_code ec;
  boost::filesystem::create_symlink(target, link, ec);
  ASSERT_FALSE(ec) << ec.message();
  std::string error;
  EXPECT_FALSE(tools::validate_path_argument("private-key-filename", link, tools::path_argument_kind::new_file, error));
  EXPECT_NE(std::string::npos, error.find("already exists"));
  // The link's target must be untouched by the check itself.
  std::string contents;
  ASSERT_TRUE(epee::file_io_utils::load_file_to_string(target, contents));
  EXPECT_EQ("precious\n", contents);
}

TEST(PathArgument, refuses_dangling_symlink_as_new_file)
{
  const temp_dir dir;
  const std::string link = dir("dangling");
  boost::system::error_code ec;
  boost::filesystem::create_symlink(dir("nowhere"), link, ec);
  ASSERT_FALSE(ec) << ec.message();
  std::string error;
  EXPECT_FALSE(tools::validate_path_argument("private-key-filename", link, tools::path_argument_kind::new_file, error));
}

TEST(PathArgument, follows_symlink_to_regular_file_for_input)
{
  const temp_dir dir;
  const std::string target = dir("target");
  const std::string link = dir("link");
  ASSERT_TRUE(write_file(target, "data\n"));
  boost::system::error_code ec;
  boost::filesystem::create_symlink(target, link, ec);
  ASSERT_FALSE(ec) << ec.message();
  std::string error;
  // Reading through a link an operator set up is legitimate: only the type of the
  // object that would be opened matters here.
  EXPECT_TRUE(tools::validate_path_argument("passphrase-file", link, tools::path_argument_kind::existing_file, error));
}
#endif

TEST(SaveStringToNewFile, creates_file_with_exact_contents)
{
  const temp_dir dir;
  const std::string file = dir("fresh.key");
  const std::string data = "-----BEGIN PRIVATE KEY-----\nnot-a-real-key\n";
  std::string error;
  ASSERT_TRUE(tools::save_string_to_new_file(file, data, error)) << error;
  std::string readback;
  ASSERT_TRUE(epee::file_io_utils::load_file_to_string(file, readback));
  EXPECT_EQ(data, readback);
}

TEST(SaveStringToNewFile, refuses_existing_file)
{
  const temp_dir dir;
  const std::string file = dir("already-here");
  ASSERT_TRUE(write_file(file, "precious\n"));
  std::string error;
  EXPECT_FALSE(tools::save_string_to_new_file(file, "key material", error));
  EXPECT_NE(std::string::npos, error.find("Refusing to overwrite"));
  std::string readback;
  ASSERT_TRUE(epee::file_io_utils::load_file_to_string(file, readback));
  EXPECT_EQ("precious\n", readback);
}

TEST(SaveStringToNewFile, refuses_empty_filename)
{
  std::string error;
  EXPECT_FALSE(tools::save_string_to_new_file("", "key material", error));
}

#ifndef _WIN32
TEST(SaveStringToNewFile, does_not_write_through_a_symlink)
{
  // This is the regression guard for the arbitrary-file-overwrite defect: writing
  // a private key to a path that is a symbolic link truncated and replaced the
  // link's target, with an exit status of success.
  const temp_dir dir;
  const std::string target = dir("decoy");
  const std::string link = dir("link");
  ASSERT_TRUE(write_file(target, "important host data\n"));
  boost::system::error_code ec;
  boost::filesystem::create_symlink(target, link, ec);
  ASSERT_FALSE(ec) << ec.message();

  std::string error;
  EXPECT_FALSE(tools::save_string_to_new_file(link, "-----BEGIN PRIVATE KEY-----\n", error));
  EXPECT_FALSE(error.empty());

  std::string readback;
  ASSERT_TRUE(epee::file_io_utils::load_file_to_string(target, readback));
  EXPECT_EQ("important host data\n", readback);
  EXPECT_TRUE(boost::filesystem::is_symlink(boost::filesystem::symlink_status(link)));
}

TEST(SaveStringToNewFile, creates_owner_only_file)
{
  const temp_dir dir;
  const std::string file = dir("fresh.key");
  std::string error;
  ASSERT_TRUE(tools::save_string_to_new_file(file, "key material", error)) << error;
  struct stat st = {};
  ASSERT_EQ(0, ::stat(file.c_str(), &st));
  EXPECT_EQ(0, st.st_mode & (S_IRWXG | S_IRWXO));
}
#endif
