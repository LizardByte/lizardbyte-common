/**
 * @file src/common/logging.cpp
 * @brief Common policy layered over spdlog's formatters and sinks.
 */

// standard includes
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

// lib includes
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>

// local includes
#include <lizardbyte/common/logging.h>

namespace lizardbyte::common::logging {
  namespace {
    constexpr std::array<std::string_view, 7> names {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "CRITICAL", "OFF"};

    class FormattedCallbackSink final: public spdlog::sinks::base_sink<std::mutex> {
    public:
      explicit FormattedCallbackSink(std::function<void(const std::string &)> callback):
          callback_(std::move(callback)) {
        if (!callback_) {
          throw std::invalid_argument("A logging callback is required");
        }
      }

    protected:
      void sink_it_(const spdlog::details::log_msg &record) override {
        spdlog::memory_buf_t text;
        formatter_->format(record, text);
        callback_(std::string(text.data(), text.size()));
      }

      void flush_() override {}

    private:
      std::function<void(const std::string &)> callback_;
    };

    std::string escaped(std::string_view value, bool field) {
      constexpr std::string_view hex = "0123456789abcdef";
      std::string result;
      for (const unsigned char c : value) {
        if (c < 32 || c == 127 || (field && (c == '[' || c == ']'))) {
          result += "\\x";
          result += hex[c >> 4];
          result += hex[c & 15];
        } else {
          result += static_cast<char>(c);
        }
      }
      return result;
    }

    std::string normalized(std::string path) {
      std::replace(path.begin(), path.end(), '\\', '/');
      while (path.size() > 1 && path.back() == '/') {
        path.pop_back();
      }
      while (path.compare(0, 2, "./") == 0) {
        path.erase(0, 2);
      }
      return path;
    }

    std::string timestamp_text(spdlog::log_clock::time_point timestamp) {
      const auto millis = std::chrono::floor<std::chrono::milliseconds>(timestamp.time_since_epoch());
      const auto seconds = std::chrono::floor<std::chrono::seconds>(millis);
      const auto time = spdlog::log_clock::to_time_t(spdlog::log_clock::time_point(seconds));
      std::tm utc {};
#ifdef _WIN32
      const bool valid = gmtime_s(&utc, &time) == 0;
#else
      const bool valid = gmtime_r(&time, &utc) != nullptr;
#endif
      if (!valid) {
        throw std::out_of_range("Logging timestamp is outside the platform range");
      }
      std::ostringstream output;
      output.imbue(std::locale::classic());
      output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.' << std::setfill('0') << std::setw(3) << (millis - seconds).count() << 'Z';
      return output.str();
    }
  }  // namespace

  std::string source_path(std::string file, std::string project_root) {
    file = normalized(std::move(file));
    project_root = normalized(std::move(project_root));
    auto comparable_file = file;
    auto comparable_root = project_root;
    if (project_root.size() > 1 && project_root[1] == ':') {
      const auto lower = [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      };
      std::transform(comparable_file.begin(), comparable_file.end(), comparable_file.begin(), lower);
      std::transform(comparable_root.begin(), comparable_root.end(), comparable_root.begin(), lower);
    }
    const auto prefix = comparable_root == "/" ? "/" : comparable_root + '/';
    if (!project_root.empty() && comparable_file.compare(0, prefix.size(), prefix) == 0) {
      file.erase(0, prefix.size());
    }
    return file.empty() ? "unknown" : file;
  }

  Formatter::Formatter(Options options):
      options_(std::move(options)) {
    auto &secrets = options_.secrets;
    secrets.erase(std::remove(secrets.begin(), secrets.end(), ""), secrets.end());
    std::stable_sort(secrets.begin(), secrets.end(), [](const auto &left, const auto &right) {
      return left.size() > right.size();
    });
  }

  std::string Formatter::redact(std::string text) const {
    for (const auto &secret : options_.secrets) {
      std::size_t offset = 0;
      while ((offset = text.find(secret, offset)) != std::string::npos) {
        text.replace(offset, secret.size(), "***");
        offset += 3;
      }
    }
    return text;
  }

  void Formatter::format(const spdlog::details::log_msg &record, spdlog::memory_buf_t &destination) {
    const auto label = names.at(static_cast<std::size_t>(record.level));
    const auto file = source_path(record.source.filename ? record.source.filename : "", options_.project_root);
    const auto source = file == "unknown" || record.source.line <= 0 ? "unknown:?" : escaped(redact(file), true) + ":" + std::to_string(record.source.line);
    const auto category = record.logger_name.size() == 0 ? std::string("app") : std::string(record.logger_name.data(), record.logger_name.size());
    const auto time = timestamp_text(record.time);
    const auto prefix = time + " [" + std::string(label) + "] [" + escaped(redact(category), true) + "] [" + source + "] ";
    const auto message = redact(std::string(record.payload.data(), record.payload.size()));
    record.color_range_start = destination.size() + time.size() + 2;
    record.color_range_end = record.color_range_start + label.size();
    std::size_t start = 0;
    const auto append_line = [&](std::size_t end) {
      const auto text = prefix + escaped(std::string_view(message).substr(start, end - start), false) + '\n';
      destination.append(text.data(), text.data() + text.size());
    };
    for (std::size_t i = 0; i < message.size(); ++i) {
      if (message[i] == '\r' || message[i] == '\n') {
        append_line(i);
        if (message[i] == '\r' && i + 1 < message.size() && message[i + 1] == '\n') {
          ++i;
        }
        start = i + 1;
      }
    }
    append_line(message.size());
  }

  std::unique_ptr<spdlog::formatter> Formatter::clone() const {
    return std::make_unique<Formatter>(options_);
  }

  std::shared_ptr<spdlog::logger> make_logger(std::string name, std::vector<spdlog::sink_ptr> sinks, const Options &options) {
    for (const auto &sink : sinks) {
      if (!sink) {
        throw std::invalid_argument("Logging sinks must not be null");
      }
    }
    auto logger = std::make_shared<spdlog::logger>(std::move(name), sinks.begin(), sinks.end());
    logger->set_level(spdlog::level::trace);
    logger->set_formatter(std::make_unique<Formatter>(options));
    logger->flush_on(spdlog::level::trace);
    return logger;
  }

  std::shared_ptr<spdlog::sinks::stdout_color_sink_mt> console_sink(spdlog::color_mode mode) {
    auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>(mode);
#ifdef _WIN32
    // Win32 foreground bits: blue=1, green=2, red=4, intensity=8.
    sink->set_color(spdlog::level::trace, 5);
    sink->set_color(spdlog::level::debug, 1);
    sink->set_color(spdlog::level::info, 2);
    sink->set_color(spdlog::level::warn, 6);
    sink->set_color(spdlog::level::err, 4);
    sink->set_color(spdlog::level::critical, 12);
#else
    sink->set_color(spdlog::level::trace, sink->magenta);
    sink->set_color(spdlog::level::debug, sink->blue);
    sink->set_color(spdlog::level::info, sink->green);
    sink->set_color(spdlog::level::warn, sink->yellow);
    sink->set_color(spdlog::level::err, sink->red);
    sink->set_color(spdlog::level::critical, sink->red_bold);
#endif
    return sink;
  }

  spdlog::sink_ptr rolling_file_sink(const spdlog::filename_t &path, std::size_t max_bytes, std::size_t backup_count) {
    if (max_bytes == 0 || backup_count == 0 || backup_count > 200000) {
      throw std::invalid_argument("Positive logging limits and at most 200000 backups are required");
    }
    return std::make_shared<spdlog::sinks::rotating_file_sink_mt>(path, max_bytes, backup_count, false);
  }

  spdlog::sink_ptr callback_sink(std::function<void(const std::string &)> callback) {
    return std::make_shared<FormattedCallbackSink>(std::move(callback));
  }
}  // namespace lizardbyte::common::logging
