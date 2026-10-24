/**
 * @file tests/consumer/main.cpp
 * @brief Installed-package consumer smoke test.
 */

// standard includes
#include <sstream>

// lib includes
#include <spdlog/sinks/ostream_sink.h>

// local includes
#include <lizardbyte/common/logging.h>

int main() {
  std::ostringstream text;
  auto output = std::make_shared<spdlog::sinks::ostream_sink_mt>(text);
  auto logger = lizardbyte::common::logging::make_logger("consumer", {output});
  LIZARDBYTE_LOG(logger, spdlog::level::info, "installed package");
  return text.str().find("[consumer]") == std::string::npos ? 1 : 0;
}
