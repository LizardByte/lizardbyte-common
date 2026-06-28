/**
 * @file tests/support/testing.cpp
 * @brief Shared GoogleTest support helper definitions.
 */

// standard includes
#include <iostream>
#include <iterator>
#include <regex>

// platform includes
#if defined(__linux__)
  #include <unistd.h>
#endif

// local includes
#include <lizardbyte/common/env.h>
#include <lizardbyte/common/testing.h>

namespace lizardbyte::common::testing {
  void BaseTest::SetUp() {
    cout_buffer_.str({});
    cout_buffer_.clear();
    cerr_buffer_.str({});
    cerr_buffer_.clear();

    if (const auto skip_reason {skipTest()}; !skip_reason.empty()) {
      test_skipped_at_setup_ = true;
      GTEST_SKIP() << skip_reason;
    }

    if (isOutputSuppressed()) {
      cout_streambuf_ = std::cout.rdbuf();
      cerr_streambuf_ = std::cerr.rdbuf();
      std::cout.rdbuf(cout_buffer_.rdbuf());
      std::cerr.rdbuf(cerr_buffer_.rdbuf());
    }
  }

  void BaseTest::TearDown() {
    if (test_skipped_at_setup_) {
      return;
    }

    if (isOutputSuppressed()) {
      if (cout_streambuf_ != nullptr) {
        std::cout.rdbuf(cout_streambuf_);
        cout_streambuf_ = nullptr;
      }

      if (cerr_streambuf_ != nullptr) {
        std::cerr.rdbuf(cerr_streambuf_);
        cerr_streambuf_ = nullptr;
      }

      const auto *test_info = ::testing::UnitTest::GetInstance()->current_test_info();
      if (test_info != nullptr && test_info->result()->Failed()) {
        std::cout << std::endl
                  << "Test failed: " << test_info->name() << std::endl
                  << std::endl
                  << "Captured cout:" << std::endl
                  << cout_buffer_.str() << std::endl
                  << "Captured cerr:" << std::endl
                  << cerr_buffer_.str() << std::endl;
      }
    }
  }

  const std::vector<std::string> &BaseTest::getArgs() const {
    static const auto args {::testing::internal::GetArgvs()};
    return args;
  }

  std::optional<std::string> BaseTest::getArgWithMatchingPattern(
    const std::string &pattern,
    const bool remove_match
  ) const {
    if (const auto &args {getArgs()}; !args.empty()) {
      const std::regex re_pattern {pattern};

      for (auto it {std::next(std::begin(args))}; it != std::end(args); ++it) {
        if (std::smatch match; std::regex_search(*it, match, re_pattern)) {
          return remove_match ? std::regex_replace(*it, re_pattern, "") : *it;
        }
      }
    }

    return std::nullopt;
  }

  bool BaseTest::isOutputSuppressed() const {
    return true;
  }

  bool BaseTest::isSystemTest() const {
    return false;
  }

  std::string BaseTest::skipTest() const {
    if (isSystemTest() && get_env("SKIP_SYSTEM_TESTS") == "1") {
      return "Skipping, this system test is disabled via SKIP_SYSTEM_TESTS=1 env.";
    }

    return {};
  }

  std::stringstream &BaseTest::coutBuffer() {
    return cout_buffer_;
  }

  std::stringstream &BaseTest::cerrBuffer() {
    return cerr_buffer_;
  }

  void LinuxTest::SetUp() {
#if !defined(__linux__)
    GTEST_SKIP() << "Skipping, this test is for Linux only.";
#else
    BaseTest::SetUp();
#endif
  }

  ::testing::AssertionResult LinuxTest::HasReadableWritableDeviceNode(const char *path) {
#if defined(__linux__)
    if (::access(path, R_OK | W_OK) == 0) {
      return ::testing::AssertionSuccess();
    }

    return ::testing::AssertionFailure() << path << " must be readable and writable";
#else
    static_cast<void>(path);
    return ::testing::AssertionSuccess();
#endif
  }

  void MacOSTest::SetUp() {
#if !defined(__APPLE__) || !defined(__MACH__)
    GTEST_SKIP() << "Skipping, this test is for macOS only.";
#else
    BaseTest::SetUp();
#endif
  }

  void WindowsTest::SetUp() {
#if !defined(_WIN32)
    GTEST_SKIP() << "Skipping, this test is for Windows only.";
#else
    BaseTest::SetUp();
#endif
  }
}  // namespace lizardbyte::common::testing
