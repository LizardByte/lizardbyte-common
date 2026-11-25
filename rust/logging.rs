//! Common format, colors, and rotation policy over `spdlog-rs`.
//!
//! Native loggers support independent sink filters, asynchronous pools, error
//! handlers, custom writers, and the `log` facade. No global logger is installed.

use std::fs::{self, File, OpenOptions};
use std::io::{self, Write};
use std::ops::Range;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::time::SystemTime;

use chrono::{DateTime, SecondsFormat, Utc};
use spdlog::formatter::{Formatter as NativeFormatter, FormatterContext};
use spdlog::sink::{Sink, StdStreamSink, WriteSink};
use spdlog::terminal_style::{Color, Style, StyleMode};
use spdlog::{Level, LevelFilter, Logger, Record, StringBuf};

/// Native logging types and macros are available without a second dependency.
pub use spdlog;

/// Default maximum active log size: 10 MiB.
pub const MAX_BYTES: u64 = 10 * 1024 * 1024;
/// Default number of retained backups, excluding the active file.
pub const BACKUP_COUNT: usize = 5;

/// Normalize separators and remove the consuming project's absolute source root.
/// External absolute paths and relative compiler paths keep their full location.
#[must_use]
pub fn source_path(file: &str, project_root: &str) -> String {
    let file = file.replace('\\', "/");
    let file = file.trim_end_matches('/').trim_start_matches("./");
    let root = project_root.replace('\\', "/");
    let prefix = format!("{}/", root.trim_end_matches('/'));
    let windows = root.as_bytes().get(1) == Some(&b':');
    let matches = if windows {
        file.to_ascii_lowercase()
            .starts_with(&prefix.to_ascii_lowercase())
    } else {
        file.starts_with(&prefix)
    };
    let file = if !root.is_empty() && matches {
        &file[prefix.len()..]
    } else {
        file
    };
    if file.is_empty() {
        "unknown".into()
    } else {
        file.into()
    }
}

fn escape(text: &str, field: bool) -> String {
    let mut output = String::new();
    for c in text.chars() {
        if c < ' ' || c == '\u{7f}' || (field && matches!(c, '[' | ']')) {
            use std::fmt::Write as _;
            write!(output, "\\x{:02x}", u32::from(c)).expect("writing to a String cannot fail");
        } else {
            output.push(c);
        }
    }
    output
}

/// Clonable native formatter; secrets are literal, longest-first replacements.
#[derive(Clone, Default)]
pub struct Formatter {
    project_root: String,
    secrets: Vec<String>,
}

impl Formatter {
    /// Supply the consuming project root, rather than this crate's manifest path.
    #[must_use]
    pub fn new(project_root: impl Into<String>, mut secrets: Vec<String>) -> Self {
        secrets.retain(|secret| !secret.is_empty());
        secrets.sort_by_key(|secret| std::cmp::Reverse(secret.len()));
        Self {
            project_root: project_root.into(),
            secrets,
        }
    }

    fn redact(&self, text: &str) -> String {
        self.secrets
            .iter()
            .fold(text.to_owned(), |text, secret| text.replace(secret, "***"))
    }

    /// Format external diagnostic fields with the same policy as native records.
    /// The returned byte range identifies the first severity label for coloring.
    #[must_use]
    pub fn render(
        &self,
        time: SystemTime,
        level: Level,
        category: &str,
        location: Option<(&str, u32)>,
        message: &str,
    ) -> (String, Range<usize>) {
        let time: DateTime<Utc> = time.into();
        let timestamp = time.to_rfc3339_opts(SecondsFormat::Millis, true);
        let label = match level {
            Level::Trace => "TRACE",
            Level::Debug => "DEBUG",
            Level::Info => "INFO",
            Level::Warn => "WARN",
            Level::Error => "ERROR",
            Level::Critical => "CRITICAL",
        };
        let source = match location {
            Some((file, line)) if !file.is_empty() && line > 0 => format!(
                "{}:{line}",
                escape(&self.redact(&source_path(file, &self.project_root)), true)
            ),
            _ => "unknown:?".into(),
        };
        let category = if category.is_empty() { "app" } else { category };
        let prefix = format!(
            "{timestamp} [{label}] [{}] [{source}] ",
            escape(&self.redact(category), true)
        );
        let message = self
            .redact(message)
            .replace("\r\n", "\n")
            .replace('\r', "\n");
        let text = message
            .split('\n')
            .map(|line| format!("{prefix}{}\n", escape(line, false)))
            .collect();
        let start = timestamp.len() + 2;
        (text, start..start + label.len())
    }
}

impl NativeFormatter for Formatter {
    fn format(
        &self,
        record: &Record,
        destination: &mut StringBuf,
        context: &mut FormatterContext,
    ) -> spdlog::Result<()> {
        let location = record
            .source_location()
            .map(|source| (source.file(), source.line()));
        let (text, range) = self.render(
            record.time(),
            record.level(),
            record.logger_name().unwrap_or("app"),
            location,
            record.payload(),
        );
        let offset = destination.len();
        context.set_style_range(Some(range.start + offset..range.end + offset));
        destination.push_str(&text);
        Ok(())
    }
}

/// Configure an unregistered native logger. Each sink keeps its own filter.
pub fn make_logger(
    name: &str,
    sinks: Vec<Arc<dyn Sink>>,
    formatter: &Formatter,
) -> spdlog::Result<Arc<Logger>> {
    for sink in &sinks {
        sink.set_formatter(Box::new(formatter.clone()));
    }
    Logger::builder()
        .name(name)
        .sinks(sinks)
        .level_filter(LevelFilter::All)
        .flush_level_filter(LevelFilter::All)
        .build_arc()
}

/// Native stdout sink with automatic terminal detection and the common palette.
pub fn console_sink() -> spdlog::Result<Arc<StdStreamSink>> {
    let mut sink = StdStreamSink::builder()
        .stdout()
        .style_mode(StyleMode::Auto)
        .build()?;
    for (level, color) in [
        (Level::Trace, Color::Magenta),
        (Level::Debug, Color::Blue),
        (Level::Info, Color::Green),
        (Level::Warn, Color::Yellow),
        (Level::Error, Color::Red),
        (Level::Critical, Color::Red),
    ] {
        let mut style = Style::builder();
        style.color(color);
        if level == Level::Critical {
            style.bold();
        }
        sink.set_style(level, style.build());
    }
    Ok(Arc::new(sink))
}

fn backup_path(path: &Path, index: usize) -> PathBuf {
    let extension = path.extension().filter(|extension| !extension.is_empty());
    let mut name = if extension.is_some() {
        path.file_stem()
    } else {
        path.file_name()
    }
    .unwrap_or_default()
    .to_os_string();
    name.push(format!(".{index}"));
    if let Some(extension) = extension {
        name.push(".");
        name.push(extension);
    }
    path.with_file_name(name)
}

/// Small writer adapter for common rotation naming and boundary semantics.
/// `spdlog-rs`'s built-in rotator uses different names and counts the active file.
/// Use through `rolling_file_sink`, which serializes access with a native WriteSink.
pub struct RollingWriter {
    path: PathBuf,
    max_bytes: u64,
    backups: usize,
    file: Option<File>,
}

impl RollingWriter {
    /// Open in append mode. Parent directories are created, as with native spdlog.
    pub fn new(path: impl Into<PathBuf>, max_bytes: u64, backups: usize) -> io::Result<Self> {
        if max_bytes == 0 || !(1..=200000).contains(&backups) {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "positive limits and at most 200000 backups are required",
            ));
        }
        let path = path.into();
        if let Some(parent) = path
            .parent()
            .filter(|parent| !parent.as_os_str().is_empty())
        {
            fs::create_dir_all(parent)?;
        }
        let mut writer = Self {
            path,
            max_bytes,
            backups,
            file: None,
        };
        writer.reopen()?;
        Ok(writer)
    }

    fn reopen(&mut self) -> io::Result<()> {
        self.file = Some(
            OpenOptions::new()
                .create(true)
                .append(true)
                .open(&self.path)?,
        );
        Ok(())
    }

    fn rotate(&mut self) -> io::Result<()> {
        self.file.take(); // Windows requires the active handle to close before rename.
        for index in (1..=self.backups).rev() {
            let previous = if index == 1 {
                self.path.clone()
            } else {
                backup_path(&self.path, index - 1)
            };
            if previous.exists() {
                let destination = backup_path(&self.path, index);
                if destination.exists() {
                    fs::remove_file(&destination)?;
                }
                fs::rename(previous, destination)?;
            }
        }
        self.reopen()
    }
}

impl Write for RollingWriter {
    fn write(&mut self, buffer: &[u8]) -> io::Result<usize> {
        if self.file.is_none() {
            self.reopen()?;
        }
        let size = self
            .file
            .as_ref()
            .expect("file was opened")
            .metadata()?
            .len();
        if size > 0 && (size >= self.max_bytes || buffer.len() as u64 > self.max_bytes - size) {
            self.rotate()?;
        }
        self.file
            .as_mut()
            .expect("file was opened")
            .write_all(buffer)?;
        Ok(buffer.len())
    }

    fn flush(&mut self) -> io::Result<()> {
        if let Some(file) = &mut self.file {
            file.flush()?;
        }
        Ok(())
    }
}

/// Standard rolling destination. WriteSink provides locking, formatting and errors.
pub fn rolling_file_sink(
    path: impl Into<PathBuf>,
    max_bytes: u64,
    backups: usize,
) -> io::Result<Arc<WriteSink<RollingWriter>>> {
    let writer = RollingWriter::new(path, max_bytes, backups)?;
    WriteSink::builder()
        .target(writer)
        .build_arc()
        .map_err(io::Error::other)
}
