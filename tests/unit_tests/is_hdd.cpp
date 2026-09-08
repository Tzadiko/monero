#include "common/util.h"
#include <cstdlib>
#include <string>
#include <gtest/gtest.h>
#include <boost/optional/optional_io.hpp> /* required to output boost::optional in assertions */

#if defined(__GLIBC__)
/* These two cases need storage of a known kind, which the host running the
 * tests may simply not have, so each is gated on an environment variable that
 * names a path on such storage. The variables take a filesystem path rather
 * than a device node, because tools::is_hdd() stats the path and reads the
 * rotational attribute of the block device behind it. Both are documented in
 * docs/COMPILING_DEBUGGING_TESTING.md, "Environment variables the tests read";
 * skipping is the correct outcome on a host that cannot satisfy the gate. */
TEST(is_hdd, rotational_drive) {
  const char *hdd = std::getenv("MONERO_TEST_DEVICE_HDD");
  if (hdd == nullptr)
    GTEST_SKIP() << "No rotational disk device configured: set MONERO_TEST_DEVICE_HDD "
                    "to a path on a rotational-disk-backed filesystem to run this test "
                    "(see docs/COMPILING_DEBUGGING_TESTING.md, \"Environment variables the tests read\")";
  EXPECT_EQ(tools::is_hdd(hdd), boost::optional<bool>(true));
}

TEST(is_hdd, ssd) {
  const char *ssd = std::getenv("MONERO_TEST_DEVICE_SSD");
  if (ssd == nullptr)
    GTEST_SKIP() << "No SSD device configured: set MONERO_TEST_DEVICE_SSD "
                    "to a path on a non-rotational-disk-backed filesystem to run this test "
                    "(see docs/COMPILING_DEBUGGING_TESTING.md, \"Environment variables the tests read\")";
  EXPECT_EQ(tools::is_hdd(ssd), boost::optional<bool>(false));
}

TEST(is_hdd, unknown_attrs) {
  EXPECT_EQ(tools::is_hdd("/dev/null"), boost::none);
}
#endif
TEST(is_hdd, stability)
{
  EXPECT_NO_THROW(tools::is_hdd(""));
}
