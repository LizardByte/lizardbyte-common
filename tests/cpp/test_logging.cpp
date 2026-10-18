/**
 * @file tests/cpp/test_logging.cpp
 * @brief Cross-language format and native spdlog integration tests.
 */

// standard includes
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <thread>

// lib includes
#include <spdlog/async.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/sinks/ringbuffer_sink.h>

// local includes
#include <lizardbyte/common/logging.h>
#include <lizardbyte/common/testing.h>

namespace {
  namespace logging = lizardbyte::common::logging;
  using namespace std::chrono_literals;

  std::string read_file(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  }

  std::string render(logging::Formatter &formatter, spdlog::level::level_enum level, const std::string &category, const std::string &message, const char *file = "/workspace/project/src/client.cpp", int line = 42) {
    spdlog::details::log_msg record {spdlog::log_clock::time_point(123ms), spdlog::source_loc {file, line, "test"}, category, level, message};
    spdlog::memory_buf_t output;
    formatter.format(record, output);
    constexpr std::array<const char *, 6> labels {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "CRITICAL"};
    EXPECT_EQ(std::string(output.data() + record.color_range_start, record.color_range_end - record.color_range_start), labels.at(static_cast<std::size_t>(level)));
    return {output.data(), output.size()};
  }

  class LoggingTest: public testing::Test {
  protected:
    std::filesystem::path directory;

    void SetUp() override {
      directory = std::filesystem::temp_directory_path() / ("lb-logging-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
      std::filesystem::create_directories(directory);
    }

    void TearDown() override {
      std::filesystem::remove_all(directory);
    }
  };
}  // namespace

TEST(LoggingFormat, MatchesSharedGoldenBytes) {
  logging::Formatter formatter {{"/workspace/project", {"secret", "secret-long", ""}}};
  std::string actual;
  for (const auto level : {spdlog::level::trace, spdlog::level::debug, spdlog::level::info, spdlog::level::warn, spdlog::level::err, spdlog::level::critical}) {
    actual += render(formatter, level, "network", u8"héllo");
  }
  actual += render(formatter, spdlog::level::info, "net]\n", "secret-long\r\nnext\t\033[31m");
  actual += render(formatter, spdlog::level::info, "", "metadata absent", "", 0);
  EXPECT_EQ(actual, read_file(std::filesystem::path(LOGGING_FIXTURE_DIR) / "logging-format.txt"));
  auto cloned = formatter.clone();
  EXPECT_NE(cloned, nullptr);
}

TEST(LoggingFormat, PreservesEmptyMessagesAndTrailingNewlines) {
  logging::Formatter formatter;
  const std::string prefix = "1970-01-01T00:00:00.123Z [INFO] [app] [unknown:?] ";
  EXPECT_EQ(render(formatter, spdlog::level::info, "", "", "", 0), prefix + '\n');
  EXPECT_EQ(render(formatter, spdlog::level::info, "", "x\r", "", 0), prefix + "x\n" + prefix + '\n');
}

TEST(LoggingFormat, ResolvesProjectRootWithoutWorkingDirectory) {
  EXPECT_EQ(logging::source_path("C:\\Projects\\App\\src\\main.cpp", "c:/projects/app/"), "src/main.cpp");
  EXPECT_EQ(logging::source_path("/app2/src/main.cpp", "/app"), "/app2/src/main.cpp");
  EXPECT_EQ(logging::source_path("./src/main.cpp", "/app"), "src/main.cpp");
  EXPECT_EQ(logging::source_path("/src/main.cpp", "/"), "src/main.cpp");
  EXPECT_EQ(logging::source_path("", ""), "unknown");
}

TEST_F(LoggingTest, RotatesByUtf8BytesAndRetainsBackupsAcrossRestart) {
  const auto path = directory / "application.log";
  logging::Formatter formatter;
  const auto text = render(formatter, spdlog::level::info, "test", u8"é", "", 0);
  for (int i = 0; i < 7; ++i) {
    auto logger = logging::make_logger("test", {logging::rolling_file_sink(path.string(), text.size() * 2, 2)});
    logger->log(spdlog::log_clock::time_point(123ms), {}, spdlog::level::info, u8"é");
    logger->flush();
  }
  EXPECT_EQ(read_file(path), text);
  EXPECT_EQ(read_file(directory / "application.1.log"), text + text);
  EXPECT_EQ(read_file(directory / "application.2.log"), text + text);
  EXPECT_FALSE(std::filesystem::exists(directory / "application.3.log"));
}

TEST_F(LoggingTest, OversizedRecordIsNotSplitOrRotatedFromEmptyFile) {
  auto logger = logging::make_logger("test", {logging::rolling_file_sink((directory / "large.log").string(), 1, 1)});
  logger->info("complete record");
  EXPECT_FALSE(std::filesystem::exists(directory / "large.1.log"));
  logger->info("next record");
  EXPECT_NE(read_file(directory / "large.1.log").find("complete record"), std::string::npos);
}

TEST(LoggingIntegration, MultipleNamedLoggersShareIndependentlyFilteredSinks) {
  std::ostringstream stream;
  auto output = std::make_shared<spdlog::sinks::ostream_sink_mt>(stream);
  output->set_level(spdlog::level::info);
  std::vector<std::string> received;
  auto callback = logging::callback_sink([&](const auto &text) {
    received.push_back(text);
  });
  callback->set_level(spdlog::level::warn);
  auto first = logging::make_logger("first", {output, callback}, {"", {"secret"}});
  auto second = logging::make_logger("second", {output, callback}, {"", {"secret"}});
  first->debug("ignored");
  LIZARDBYTE_LOG(first, spdlog::level::info, "hello {}", 1);
  second->warn("secret");
  first->flush();
  ASSERT_EQ(received.size(), 1);
  EXPECT_NE(received[0].find("[second]"), std::string::npos);
  EXPECT_NE(received[0].find("***"), std::string::npos);
  EXPECT_EQ(stream.str().find("ignored"), std::string::npos);
  EXPECT_NE(stream.str().find("test_logging.cpp:"), std::string::npos);
}

TEST(LoggingIntegration, FailuresUseNativeErrorHandlerAndOtherSinksStillRun) {
  auto failure = logging::callback_sink([](const auto &) {
    throw std::runtime_error("transport failed");
  });
  auto memory = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(2);
  auto logger = logging::make_logger("errors", {failure, memory});
  std::string error;
  logger->set_error_handler([&](const auto &message) {
    error = message;
  });
  logger->warn("still retained");
  EXPECT_NE(error.find("transport failed"), std::string::npos);
  ASSERT_EQ(memory->last_formatted().size(), 1);
}

TEST(LoggingIntegration, ConcurrentAndAsyncDeliveryUseNativeSinks) {
  std::atomic<int> count = 0;
  auto callback = logging::callback_sink([&](const auto &) {
    ++count;
  });
  auto logger = logging::make_logger("concurrent", {callback});
  std::vector<std::thread> workers;
  for (int i = 0; i < 4; ++i) {
    workers.emplace_back([&] {
      for (int j = 0; j < 50; ++j) {
        logger->info("record");
      }
    });
  }
  for (auto &worker : workers) {
    worker.join();
  }
  EXPECT_EQ(count, 200);
  {
    auto pool = std::make_shared<spdlog::details::thread_pool>(16, 1);
    auto async = std::make_shared<spdlog::async_logger>("async", callback, pool, spdlog::async_overflow_policy::block);
    async->info("queued");
    async->flush();
  }
  EXPECT_EQ(count, 201);
}

TEST(LoggingIntegration, RejectsInvalidConfigurationAndSupportsPlainConsole) {
  EXPECT_THROW(static_cast<void>(logging::make_logger("test", {nullptr})), std::invalid_argument);
  EXPECT_THROW(static_cast<void>(logging::callback_sink({})), std::invalid_argument);
  EXPECT_THROW(static_cast<void>(logging::rolling_file_sink("unused.log", 0, 1)), std::invalid_argument);
  EXPECT_THROW(static_cast<void>(logging::rolling_file_sink("unused.log", 1, 0)), std::invalid_argument);
  EXPECT_THROW(static_cast<void>(logging::rolling_file_sink("unused.log", 1, 200001)), std::invalid_argument);
  EXPECT_NE(logging::console_sink(spdlog::color_mode::never), nullptr);
}
