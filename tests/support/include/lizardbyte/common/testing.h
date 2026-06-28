/**
 * @file tests/support/include/lizardbyte/common/testing.h
 * @brief Shared GoogleTest support helpers.
 */
#pragma once

// standard includes
#include <optional>
#include <sstream>
#include <string>
#include <vector>

// lib includes
#include <gtest/gtest.h>

namespace lizardbyte::common::testing {
  /**
   * @brief Base class used by default for shared tests.
   *
   * ``cout`` and ``cerr`` are redirected during tests and printed when a test fails.
   */
  class BaseTest: public ::testing::Test {
  protected:
    /**
     * @brief Tear down the test base.
     */
    ~BaseTest() override = default;

    /**
     * @brief Set up the test.
     */
    void SetUp() override;

    /**
     * @brief Tear down the test.
     */
    void TearDown() override;

    /**
     * @brief Get available command line arguments.
     * @return Command line args from GoogleTest.
     */
    [[nodiscard]] virtual const std::vector<std::string> &getArgs() const;

    /**
     * @brief Get the command line argument that matches the pattern.
     * @param pattern Pattern to look for.
     * @param remove_match Specify if the matched pattern should be removed before returning the argument.
     * @return Matching command line argument, or null optional if nothing matched.
     */
    [[nodiscard]] virtual std::optional<std::string> getArgWithMatchingPattern(
      const std::string &pattern,
      bool remove_match
    ) const;

    /**
     * @brief Check if test output should be printed only when the test fails.
     * @return True if output is suppressed, false otherwise.
     */
    [[nodiscard]] virtual bool isOutputSuppressed() const;

    /**
     * @brief Check if the test interacts with system settings.
     * @return True if it does, false otherwise.
     *
     * Set ``SKIP_SYSTEM_TESTS=1`` to skip tests that return true here.
     */
    [[nodiscard]] virtual bool isSystemTest() const;

    /**
     * @brief Skip the test by specifying the reason.
     * @return A non-empty string if the test needs to be skipped, empty string otherwise.
     */
    [[nodiscard]] virtual std::string skipTest() const;

    /**
     * @brief Get captured cout.
     * @return Captured cout stream.
     */
    [[nodiscard]] std::stringstream &coutBuffer();

    /**
     * @brief Get captured cerr.
     * @return Captured cerr stream.
     */
    [[nodiscard]] std::stringstream &cerrBuffer();

  private:
    std::stringstream cout_buffer_; /**< Stores cout while output is suppressed. */
    std::stringstream cerr_buffer_; /**< Stores cerr while output is suppressed. */
    std::streambuf *cout_streambuf_ {nullptr}; /**< Original cout stream buffer. */
    std::streambuf *cerr_streambuf_ {nullptr}; /**< Original cerr stream buffer. */
    bool test_skipped_at_setup_ {false}; /**< True when SetUp skipped before redirection. */
  };

  /**
   * @brief Base class for Linux-only tests.
   */
  class LinuxTest: public BaseTest {
  protected:
    /**
     * @brief Set up the test.
     */
    void SetUp() override;

    /**
     * @brief Check that a Linux device node is readable and writable.
     * @param path Device node path.
     * @return GoogleTest assertion result.
     */
    static ::testing::AssertionResult HasReadableWritableDeviceNode(const char *path);
  };

  /**
   * @brief Base class for macOS-only tests.
   */
  class MacOSTest: public BaseTest {
  protected:
    /**
     * @brief Set up the test.
     */
    void SetUp() override;
  };

  /**
   * @brief Base class for Windows-only tests.
   */
  class WindowsTest: public BaseTest {
  protected:
    /**
     * @brief Set up the test.
     */
    void SetUp() override;
  };
}  // namespace lizardbyte::common::testing

#if !defined(LIZARDBYTE_COMMON_TESTING_NO_GLOBAL_ALIASES)
/**
 * @brief Global compatibility alias for the shared base test fixture.
 */
using BaseTest = ::lizardbyte::common::testing::BaseTest;

/**
 * @brief Global compatibility alias for the shared Linux-only test fixture.
 */
using LinuxTest = ::lizardbyte::common::testing::LinuxTest;

/**
 * @brief Global compatibility alias for the shared macOS-only test fixture.
 */
using MacOSTest = ::lizardbyte::common::testing::MacOSTest;

/**
 * @brief Global compatibility alias for the shared Windows-only test fixture.
 */
using WindowsTest = ::lizardbyte::common::testing::WindowsTest;
#endif

/** @cond */
#define LB_DETAIL_CAT_IMPL(part_1, part_2) part_1##part_2
#define LB_DETAIL_CAT(part_1, part_2) LB_DETAIL_CAT_IMPL(part_1, part_2)
#define LB_DETAIL_JOIN_1(part_1) part_1
#define LB_DETAIL_JOIN_2(part_1, part_2) LB_DETAIL_CAT(LB_DETAIL_CAT(part_1, _), part_2)
#define LB_DETAIL_JOIN_3(part_1, part_2, part_3) LB_DETAIL_JOIN_2(LB_DETAIL_JOIN_2(part_1, part_2), part_3)
#define LB_DETAIL_JOIN_4(part_1, part_2, part_3, part_4) \
  LB_DETAIL_JOIN_2(LB_DETAIL_JOIN_3(part_1, part_2, part_3), part_4)
#define LB_DETAIL_JOIN_5(part_1, part_2, part_3, part_4, part_5) \
  LB_DETAIL_JOIN_2(LB_DETAIL_JOIN_4(part_1, part_2, part_3, part_4), part_5)
#define LB_DETAIL_JOIN_6(part_1, part_2, part_3, part_4, part_5, part_6) \
  LB_DETAIL_JOIN_2(LB_DETAIL_JOIN_5(part_1, part_2, part_3, part_4, part_5), part_6)
#define LB_DETAIL_JOIN_7(part_1, part_2, part_3, part_4, part_5, part_6, part_7) \
  LB_DETAIL_JOIN_2(LB_DETAIL_JOIN_6(part_1, part_2, part_3, part_4, part_5, part_6), part_7)
#define LB_DETAIL_JOIN_8(part_1, part_2, part_3, part_4, part_5, part_6, part_7, part_8) \
  LB_DETAIL_JOIN_2(LB_DETAIL_JOIN_7(part_1, part_2, part_3, part_4, part_5, part_6, part_7), part_8)

#define LB_DETAIL_GET_9TH_ARG(_1, _2, _3, _4, _5, _6, _7, _8, _9, ...) _9
#define LB_DETAIL_COUNT_ARGS(...) LB_DETAIL_GET_9TH_ARG(__VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1)
#define LB_DETAIL_JOIN_DISPATCH(count, ...) LB_DETAIL_JOIN_DISPATCH_IMPL(count, __VA_ARGS__)
#define LB_DETAIL_JOIN_DISPATCH_IMPL(count, ...) LB_DETAIL_JOIN_##count(__VA_ARGS__)
/** @endcond */

/**
 * @brief Concatenate variadic macro arguments with underscore separators.
 */
#define LB_CAT_VA_ARGS(...) LB_DETAIL_JOIN_DISPATCH(LB_DETAIL_COUNT_ARGS(__VA_ARGS__), __VA_ARGS__)

/**
 * @brief Create a test name from multiple macro arguments.
 *
 * Usage:
 * @code
 * #define TEST_S(...) LB_MAKE_TEST(TEST, MySuite, __VA_ARGS__)
 * TEST_S(Does, The, Thing) {}
 * @endcode
 */
#define LB_MAKE_TEST(test_macro, test_suite_name, ...) test_macro(test_suite_name, LB_CAT_VA_ARGS(__VA_ARGS__))

#if !defined(LIZARDBYTE_COMMON_TESTING_KEEP_GTEST_TEST)
  #undef TEST  // NOSONAR(cpp:S959): Tests intentionally wrap TEST to use BaseTest.

  /**
   * @brief Redefine TEST to automatically use the shared BaseTest fixture.
   */
  #define TEST(test_case_name, test_name) \
    GTEST_TEST_( \
      test_case_name, \
      test_name, \
      ::lizardbyte::common::testing::BaseTest, \
      ::testing::internal::GetTypeId<::lizardbyte::common::testing::BaseTest>() \
    )
#endif
