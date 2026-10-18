/**
 * @file src/include/lizardbyte/common/logging.h
 * @brief Standard formatting and spdlog configuration for LizardByte consumers.
 */
#pragma once

// standard includes
#include <functional>
#include <memory>
#include <string>
#include <vector>

// lib includes
#include <spdlog/formatter.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace lizardbyte::common::logging {
  /// Formatting policy shared by all destinations of an application.
  struct Options {
    std::string project_root;  ///< Consuming project's absolute source root, independent of the working directory.
    std::vector<std::string> secrets;  ///< Literal secrets to replace with *** in formatted output.
  };

  /// Normalize separators and make source files beneath project_root relative to it.
  /// Relative compiler paths remain relative; external absolute paths remain clickable.
  [[nodiscard]] std::string source_path(std::string file, std::string project_root);

  /// Standard UTC formatter. Every physical message line receives a complete prefix.
  /// Native console sinks color the first severity label; files always contain plain UTF-8/LF.
  class Formatter final: public spdlog::formatter {
  public:
    /// Empty secrets are ignored; longer secrets are replaced first.
    explicit Formatter(Options options = {});
    void format(const spdlog::details::log_msg &record, spdlog::memory_buf_t &destination) override;
    [[nodiscard]] std::unique_ptr<spdlog::formatter> clone() const override;

  private:
    Options options_;
    [[nodiscard]] std::string redact(std::string text) const;
  };

  /// Create an unregistered native spdlog logger with independently filtered sinks.
  /// The logger accepts trace and above; set each sink's threshold separately.
  /// Sinks may be shared by loggers using the same Options. Native async, callback,
  /// ringbuffer, backtrace, and error-handler APIs remain available; see docs/logging.md.
  [[nodiscard]] std::shared_ptr<spdlog::logger> make_logger(std::string name, std::vector<spdlog::sink_ptr> sinks, const Options &options = {});

  /// Standard stdout colors; automatic mode detects terminal capabilities.
  [[nodiscard]] std::shared_ptr<spdlog::sinks::stdout_color_sink_mt> console_sink(spdlog::color_mode mode = spdlog::color_mode::automatic);

  /// Size rotation: append at startup, app.log -> app.1.log, newest backup first.
  /// One shared sink must own each path; defaults are 10 MiB and five backups.
  /// Both limits must be positive, with at most 200000 backups (spdlog's limit).
  [[nodiscard]] spdlog::sink_ptr rolling_file_sink(const spdlog::filename_t &path, std::size_t max_bytes = 10 * 1024 * 1024, std::size_t backup_count = 5);

  /// Deliver complete standard, redacted text to a transport or bounded queue.
  /// Native spdlog locking/error handling applies; do not recursively log to this sink.
  [[nodiscard]] spdlog::sink_ptr callback_sink(std::function<void(const std::string &)> callback);
}  // namespace lizardbyte::common::logging

/// Capture a C++17 source location while preserving native spdlog format arguments.
#define LIZARDBYTE_LOG(logger, level, ...) (logger)->log(spdlog::source_loc {__FILE__, __LINE__, SPDLOG_FUNCTION}, (level), __VA_ARGS__)
