/**
 * @file tests/cpp/test_env.cpp
 * @brief Unit tests for environment variable helpers.
 */

// standard includes
#include <cerrno>
#include <iostream>
#include <string>
#include <utility>

// local includes
#include <lizardbyte/common/env.h>

namespace {
  constexpr auto test_env_name {"LIZARDBYTE_COMMON_TEST_ENV"};

  int failures {};

  class EnvGuard {
  public:
    explicit EnvGuard(std::string name):
        name_ {std::move(name)},
        had_value_ {lizardbyte::common::get_env(name_, old_value_)} {
      static_cast<void>(lizardbyte::common::unset_env(name_));
    }

    ~EnvGuard() {
      if (had_value_) {
        static_cast<void>(lizardbyte::common::set_env(name_, old_value_));
      } else {
        static_cast<void>(lizardbyte::common::unset_env(name_));
      }
    }

  private:
    std::string name_;
    std::string old_value_;
    bool had_value_;
  };

  void expect_true(const bool condition, const char *expression, const char *file, const int line) {
    if (condition) {
      return;
    }

    std::cerr << file << ':' << line << ": expected true: " << expression << '\n';
    ++failures;
  }

  void expect_false(const bool condition, const char *expression, const char *file, const int line) {
    if (!condition) {
      return;
    }

    std::cerr << file << ':' << line << ": expected false: " << expression << '\n';
    ++failures;
  }

  template<typename L, typename R>
  void expect_eq(const L &left, const R &right, const char *left_expression, const char *right_expression, const char *file, const int line) {
    if (left == right) {
      return;
    }

    std::cerr << file << ':' << line << ": expected equality: " << left_expression << " == " << right_expression << " (actual: " << left << " != " << right << ")\n";
    ++failures;
  }

#define EXPECT_TRUE(expression) expect_true((expression), #expression, __FILE__, __LINE__)
#define EXPECT_FALSE(expression) expect_false((expression), #expression, __FILE__, __LINE__)
#define EXPECT_EQ(left, right) expect_eq((left), (right), #left, #right, __FILE__, __LINE__)

  void get_env_returns_false_for_missing_variable() {
    EnvGuard guard {test_env_name};

    std::string value {"unchanged"};

    EXPECT_FALSE(lizardbyte::common::get_env(test_env_name, value));
    EXPECT_EQ(value, "unchanged");
  }

  void set_env_updates_environment_variable() {
    EnvGuard guard {test_env_name};

    std::string value;

    EXPECT_EQ(lizardbyte::common::set_env(test_env_name, "alpha"), 0);
    EXPECT_TRUE(lizardbyte::common::get_env(test_env_name, value));
    EXPECT_EQ(value, "alpha");
  }

  void append_env_sets_missing_environment_variable() {
    EnvGuard guard {test_env_name};

    std::string value;

    EXPECT_EQ(lizardbyte::common::append_env(test_env_name, "alpha", ";"), 0);
    EXPECT_TRUE(lizardbyte::common::get_env(test_env_name, value));
    EXPECT_EQ(value, "alpha");
  }

  void append_env_appends_with_separator() {
    EnvGuard guard {test_env_name};

    std::string value;

    EXPECT_EQ(lizardbyte::common::set_env(test_env_name, "alpha"), 0);
    EXPECT_EQ(lizardbyte::common::append_env(test_env_name, "beta", ";"), 0);
    EXPECT_TRUE(lizardbyte::common::get_env(test_env_name, value));
    EXPECT_EQ(value, "alpha;beta");
  }

  void append_env_skips_existing_value() {
    EnvGuard guard {test_env_name};

    std::string value;

    EXPECT_EQ(lizardbyte::common::set_env(test_env_name, "alpha;beta"), 0);
    EXPECT_EQ(lizardbyte::common::append_env(test_env_name, "beta", ";"), 0);
    EXPECT_TRUE(lizardbyte::common::get_env(test_env_name, value));
    EXPECT_EQ(value, "alpha;beta");
  }

  void unset_env_removes_environment_variable() {
    EnvGuard guard {test_env_name};

    std::string value;

    EXPECT_EQ(lizardbyte::common::set_env(test_env_name, "alpha"), 0);
    EXPECT_TRUE(lizardbyte::common::get_env(test_env_name, value));
    EXPECT_EQ(lizardbyte::common::unset_env(test_env_name), 0);
    EXPECT_FALSE(lizardbyte::common::get_env(test_env_name, value));
  }

  void invalid_env_names_are_rejected() {
    std::string value {"unchanged"};

    EXPECT_FALSE(lizardbyte::common::get_env("", value));
    EXPECT_FALSE(lizardbyte::common::get_env("INVALID=NAME", value));
    EXPECT_EQ(lizardbyte::common::set_env("", "value"), EINVAL);
    EXPECT_EQ(lizardbyte::common::set_env("INVALID=NAME", "value"), EINVAL);
    EXPECT_EQ(lizardbyte::common::append_env("", ""), EINVAL);
    EXPECT_EQ(lizardbyte::common::append_env("INVALID=NAME", ""), EINVAL);
    EXPECT_EQ(lizardbyte::common::unset_env(""), EINVAL);
    EXPECT_EQ(lizardbyte::common::unset_env("INVALID=NAME"), EINVAL);
    EXPECT_EQ(value, "unchanged");
  }
}  // namespace

int main() {
  get_env_returns_false_for_missing_variable();
  set_env_updates_environment_variable();
  append_env_sets_missing_environment_variable();
  append_env_appends_with_separator();
  append_env_skips_existing_value();
  unset_env_removes_environment_variable();
  invalid_env_names_are_rejected();

  return failures == 0 ? 0 : 1;
}
