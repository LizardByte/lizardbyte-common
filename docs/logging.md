# Shared logging

This is the logging contract for C++, Python, and Rust consumers of lizardbyte-common.
It implements the reusable foundation for [roadmap issue 186](https://github.com/LizardByte/roadmap/issues/186).
Consumer migrations happen separately; this change does not close the organization-wide issue.

## Backends and format

Use native logging APIs rather than a second logging framework:

- C++ uses [spdlog](https://github.com/gabime/spdlog), pinned as a submodule. An existing
  `spdlog::spdlog` target can be supplied by the consumer. The production target remains `lizardbyte::common`.
- Python uses the standard `logging` package and `logging.handlers`, with no runtime dependencies.
- Rust uses [spdlog-rs](https://docs.rs/spdlog-rs), including its `log` facade bridge and async sinks.
  The crate can be consumed as a Cargo path dependency from the same submodule.

Every physical line has this format, with one space separating fields:

```text
2026-10-18T12:34:56.789Z [INFO] [network] [src/network/client.cpp:42] Connected
```

Timestamps are UTC, with exactly three fractional digits. Category is the native logger name
(or the `log` target when the Rust bridge uses an unnamed logger), defaulting to `app`.
Severity names are `TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR`, and `CRITICAL`.
Sunshine's verbose maps to trace and fatal maps to critical. Python trace is level `5` (`TRACE`);
use `logger.log(TRACE, ...)`. The Rust `log` facade has no critical level; use the native critical macro.
Unknown source metadata is represented as `unknown:?`.

Source locations keep the **entire path relative to the consuming project's root**, never just the basename.
Pass that root explicitly at startup; it is not inferred from the current working directory or this dependency's
source directory. C++ callers should pass their own `PROJECT_SOURCE_DIR` through a target compile definition;
Rust workspace applications should pass their workspace root, which may be above `CARGO_MANIFEST_DIR`.
This keeps locations clickable when running from an IDE or a build directory. Already-relative compiler paths
stay relative. Files outside the configured root stay absolute so IDEs can still open external dependency code.
Separators become `/`; Windows drive paths are matched without ASCII case sensitivity and with a directory boundary.
Pass normalized absolute roots and compiler paths; path resolution is lexical and does not access the filesystem.
If a packaged build moves source files, supply the original build source root to remove that prefix.

Text is UTF-8 and files use LF on every OS. CRLF and bare CR in messages become line breaks;
every resulting line repeats the original prefix. A trailing message newline produces an empty prefixed line.
ASCII control bytes, including embedded ANSI escapes, become visible `\xNN` sequences.
Brackets and controls in category and location fields are escaped to preserve the field boundaries.
Terminal styling is added only by the output destination, never stored in files or callback text.

The common palette is magenta trace, blue debug, green info, yellow warning, red error, and bold red critical.
Only the **first severity label** of a record is styled, including for multiline records. Automatic mode disables
color when redirected. C++ uses native Windows console attributes; Python and Rust use ANSI, so Windows hosts
must enable virtual-terminal processing when necessary. Exact shades and bold rendering depend on the terminal.

## Rolling files

Defaults are **10 MiB** (`10 * 1024 * 1024` bytes) per active file and **five backups**:

```text
application.log       # active, appended to on restart
application.1.log     # newest backup
application.2.log
...
application.5.log     # oldest retained backup
```

The index goes before the last extension. Names without extensions and dotfiles append the index.
Parent directories are created. Both limits must be positive; backup count is at most 200000.
Before writing a complete formatted record, rotate if the nonempty active file plus the incoming UTF-8 bytes
would **exceed** the size limit. Equality does not rotate. An oversized record is written intact to an empty
file and rotated before the next record. There is no startup truncation, compression, or time-based rotation.
Writes flush before returning in the configured synchronous loggers; this is not an fsync/durability guarantee.

The C++ implementation uses spdlog's native rotating sink. Python subclasses the standard rotating handler only
to align byte accounting, filename placement, and Windows newlines. Rust's native rotator uses underscore names
and counts the active file in retention, so a small `Write` adapter provides this exact contract while native
spdlog-rs handles locking, formatting, dispatch, async delivery, and error handling.

**One sink or handler owns a given file path.** Share it between named loggers in a process. For multiprocessing,
send worker records through a queue to one writer/listener, as Themerr already does. Independent processes,
different language runtimes, or distinct sink instances must not rotate the same path concurrently.
Do not externally truncate, rename, or delete active files; rotation ownership belongs to the selected backend.

## C++ usage

```cmake
add_subdirectory(third-party/lizardbyte-common)
target_link_libraries(my_app PRIVATE lizardbyte::common)
target_compile_definitions(my_app PRIVATE APP_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
```

```cpp
#include <lizardbyte/common/logging.h>

namespace logging = lizardbyte::common::logging;
auto console = logging::console_sink();
console->set_level(spdlog::level::info);
auto file = logging::rolling_file_sink("logs/application.log");
file->set_level(spdlog::level::debug);
auto logger = logging::make_logger("network", {console, file}, {APP_SOURCE_ROOT, {"a-secret-token"}});
LIZARDBYTE_LOG(logger, spdlog::level::info, "Connected to {}", "server");
logger->flush();
```

The macro captures the call site without changing spdlog's formatting API. Native `logger->info(...)` calls
remain available but have no source location. The factory does not register a global logger. It accepts trace
and above; sink thresholds determine destinations. Set `logger->set_level` for an additional logger-wide filter.
Factories configure immediate flushing. Keep shared sinks on the same formatting policy, since formatters belong
to sinks. Installed package consumers use `find_package(lizardbyte-common CONFIG REQUIRED)`; spdlog is an exported
dependency and is installed alongside the library when the bundled dependency is used.

Use `callback_sink` for formatted, redacted webhook text. Use native `ringbuffer_sink_mt` for bounded UI retention,
backtrace buffering for diagnostics, or `async_logger` with a bounded thread pool for slow destinations. Set the
common `Formatter` and sink filters before starting workers. Shutdown the pool to drain queued records; an async
flush call queues a flush and does not itself wait for completion. Set overflow policy deliberately.

## Python usage

```python
import logging
from pathlib import Path
from lizardbyte_common.logging import CallbackHandler, RollingFileHandler, TRACE, console_handler

root = str(Path(__file__).resolve().parent)  # choose the actual application root
logger = logging.getLogger('network')
logger.setLevel(TRACE)
logger.propagate = False
logger.addHandler(console_handler(root, level=logging.INFO))
logger.addHandler(RollingFileHandler('logs/application.log', root, level=logging.DEBUG))
# Add CallbackHandler(enqueue_webhook_text, root, level=logging.WARNING) when configured.
logger.info('Connected to %s', 'server')
```

Install the same `lizardbyte-common` Python path dependency used for repository tooling; runtime imports live
under `lizardbyte_common`. Named loggers, filters, `stacklevel`, traceback/stack formatting, library loggers,
`QueueHandler`/`QueueListener`, `MemoryHandler`, and third-party handlers remain native Python facilities.
Attach `Formatter(root, secrets=[...])` to custom handlers too. Use `respect_handler_level=True` on QueueListener.
Only one listener owns rolling files. Stop listeners before closing handlers or calling `logging.shutdown()`.
This module never clears or modifies existing root logger handlers or installs global exception hooks.

## Rust usage

```toml
[dependencies]
lizardbyte-common = { path = "third-party/lizardbyte-common" }
```

```rust
use lizardbyte_common::logging::{self, spdlog, Formatter, BACKUP_COUNT, MAX_BYTES};
use spdlog::{Level, LevelFilter};
use spdlog::sink::Sink;

let format = Formatter::new(env!("CARGO_MANIFEST_DIR"), vec![]); // workspace apps pass workspace root
let console = logging::console_sink()?;
console.set_level_filter(LevelFilter::MoreSevereEqual(Level::Info));
let file = logging::rolling_file_sink("logs/application.log", MAX_BYTES, BACKUP_COUNT)?;
file.set_level_filter(LevelFilter::MoreSevereEqual(Level::Debug));
let logger = logging::make_logger("network", vec![console, file], &format)?;
spdlog::info!(logger: logger, "Connected to {}", "server");
logger.flush();
# Ok::<(), Box<dyn std::error::Error>>(())
```

Native macros capture call-site locations. `spdlog::init_log_crate_proxy()` optionally bridges existing `log`
callers (including Koko's dependencies); install it once in the application, choose its logger/filter, and set
the facade's maximum level. Use an unnamed native logger to retain each `log` record's target/category.
Native `AsyncPoolSink` provides bounded asynchronous delivery; configure overflow and explicitly drain/join the
pool on shutdown. `WriteSink` accepts any `Write + Send` transport, memory buffer, or bounded queue; the shared
formatter supplies standard redacted text. Sinks shared between loggers must use the same formatter policy.

## Delivery, redaction, and migration needs

The standard covers presentation and storage; it does not choose a network client, application data directory,
authentication scheme, retry schedule, event loop, webhook embed schema, or global exception policy.
Transport callbacks must not recursively log through themselves. Use bounded queues and expose dropped-record
counts when choosing a dropping overflow policy. The native frameworks report sink failures through their error
handlers; one failing destination does not prevent other native destinations from receiving the record. Keep
error reporting separate from the failing transport to prevent recursion.

Supply literal secret values through formatter configuration; longest values are replaced first, and empty
values are ignored. Redaction covers rendered category, source path, message and Python tracebacks, without
changing records used by other handlers. It does **not** automatically discover credentials or sanitize arbitrary
structured fields. A custom sink forwarding raw records must apply the same redaction policy. Queue records
and native backtrace/raw ring buffers may retain unredacted data; restrict access and prefer formatted buffers.
Application-specific regex redaction and filters can be layered on native facilities before dispatch.

The adjacent projects were reviewed for these migration needs:

| Project | Existing behavior | Reusable integration path |
| --- | --- | --- |
| Koko | fern/log, colored categories/source, ANSI cleanup, password/token patterns, web log reader | `log` bridge, shared formatter/rolling writer, preserve application regex policy and update the reader |
| Moonlight-XboxOG | bounded structured ring, independent file/debugger thresholds, startup console, optional locations | native retained/formatted sink plus callbacks for startup/xemu/overlay; keep NXDK/SDL adapters in consumer |
| support-bot | stdout, 10 MiB/five backups, warning-only Discord embeds, 50-message startup queue, async tasks, library thresholds | standard handlers plus bounded queue and formatted callback; Discord/event-loop/embed policy remains in bot |
| Themerr | named channels, config/regex redaction, traceback hooks, multiprocessing queues, session log viewer, yt-dlp adapter | native named handlers/filters/listener; preserve exception, redaction and session-viewer policy in consumer |
| Sunshine | Boost async dispatch, startup rotation, verbose/fatal, Android callback, flush/deinit, periodic min/max/average diagnostics | spdlog levels/async/error hooks and callback sinks; periodic statistics and Android adaptation remain in consumer |
| One | Qt warnings and protocol log callbacks | Qt message handler and protocol callback feeding named native logger |
| libvirtualhid | optional consumer callback, disable-on-exception, bounded Windows driver trace files | consumer callback into common logger; preserve ABI, exception containment and driver-platform ownership |

No production common target depends on these projects, Discord, Qt, SDL, Boost.Log, or a network library.
Platform console and driver integration still needs native consumer testing when those migrations happen.

## Validation

`tests/fixtures/logging-format.txt` is a shared byte-for-byte golden fixture for all three implementations.
Suites also cover project-root paths, Unicode size boundaries, append/restart, backup retention, oversized records,
independent filters, named loggers, redacted callbacks, native failure handling, queues, and concurrency.
Run `pytest`, the CMake GoogleTest executable, and `cargo test --locked --all-targets`.
CI builds C++ and runs Python/Rust on Linux, macOS, and Windows; Rust also runs formatting, Clippy and rustdoc.
