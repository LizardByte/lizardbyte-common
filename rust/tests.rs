use std::io::Write;
use std::sync::Arc;
use std::time::{Duration, UNIX_EPOCH};

use spdlog::sink::{Sink, WriteSink};
use spdlog::{Level, LevelFilter};

use crate::logging::{self, Formatter, RollingWriter};

#[test]
fn shared_golden_bytes() {
    let formatter = Formatter::new(
        "/workspace/project",
        vec!["secret".into(), "secret-long".into(), "".into()],
    );
    let time = UNIX_EPOCH + Duration::from_millis(123);
    let mut actual = String::new();
    for level in [
        Level::Trace,
        Level::Debug,
        Level::Info,
        Level::Warn,
        Level::Error,
        Level::Critical,
    ] {
        let (text, range) = formatter.render(
            time,
            level,
            "network",
            Some(("/workspace/project/src/client.cpp", 42)),
            "héllo",
        );
        assert!(!text[range].contains('['));
        actual += &text;
    }
    actual += &formatter
        .render(
            time,
            Level::Info,
            "net]\n",
            Some(("/workspace/project/src/client.cpp", 42)),
            "secret-long\r\nnext\t\x1b[31m\r",
        )
        .0;
    actual += &formatter.render(time, Level::Info, "", None, "").0;
    assert_eq!(
        actual.as_bytes(),
        include_bytes!("../tests/fixtures/logging-format.txt")
    );
}

#[test]
fn source_locations_keep_consumer_root() {
    for (file, root, expected) in [
        (
            "C:\\Projects\\App\\src\\main.rs",
            "c:/projects/app/",
            "src/main.rs",
        ),
        ("/app2/src/main.rs", "/app", "/app2/src/main.rs"),
        ("./src/main.rs", "/app", "src/main.rs"),
        ("/src/main.rs", "/", "src/main.rs"),
        ("", "", "unknown"),
    ] {
        assert_eq!(logging::source_path(file, root), expected);
    }
}

#[test]
fn rotation_preserves_exact_boundary_unicode_and_restart() {
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("nested/application.log");
    let formatter = Formatter::default();
    let text = formatter
        .render(
            UNIX_EPOCH + Duration::from_millis(123),
            Level::Info,
            "test",
            None,
            "é",
        )
        .0;
    for _ in 0..5 {
        let mut writer = RollingWriter::new(&path, text.len() as u64 * 2, 2).unwrap();
        writer.write_all(text.as_bytes()).unwrap();
        writer.flush().unwrap();
    }
    assert_eq!(std::fs::read(&path).unwrap(), text.as_bytes());
    assert_eq!(
        std::fs::read(path.with_file_name("application.1.log")).unwrap(),
        (text.clone() + &text).as_bytes()
    );
    assert_eq!(
        std::fs::read(path.with_file_name("application.2.log")).unwrap(),
        (text.clone() + &text).as_bytes()
    );
    assert!(!path.with_file_name("application.3.log").exists());
}

#[test]
fn oversized_records_remain_intact_without_empty_backups() {
    let directory = tempfile::tempdir().unwrap();
    for name in ["large.log", "noextension", ".hidden"] {
        let path = directory.path().join(name);
        let mut writer = RollingWriter::new(&path, 1, 1).unwrap();
        writer.write_all(b"complete record").unwrap();
        assert_eq!(
            std::fs::read_dir(directory.path()).unwrap().count(),
            if name == "large.log" {
                1
            } else if name == "noextension" {
                3
            } else {
                5
            }
        );
        writer.write_all(b"next").unwrap();
        drop(writer);
        let backup = match name {
            "large.log" => "large.1.log",
            "noextension" => "noextension.1",
            _ => ".hidden.1",
        };
        assert_eq!(
            std::fs::read(directory.path().join(backup)).unwrap(),
            b"complete record"
        );
    }
}

#[test]
fn native_loggers_filter_share_sinks_and_capture_source() {
    let output = WriteSink::builder()
        .target(Vec::<u8>::new())
        .level_filter(LevelFilter::MoreSevereEqual(Level::Info))
        .build_arc()
        .unwrap();
    let warning = WriteSink::builder()
        .target(Vec::<u8>::new())
        .level_filter(LevelFilter::MoreSevereEqual(Level::Warn))
        .build_arc()
        .unwrap();
    let formatter = Formatter::new(env!("CARGO_MANIFEST_DIR"), vec!["secret".into()]);
    let first =
        logging::make_logger("first", vec![output.clone(), warning.clone()], &formatter).unwrap();
    let second =
        logging::make_logger("second", vec![output.clone(), warning.clone()], &formatter).unwrap();
    spdlog::debug!(logger: first, "ignored");
    spdlog::info!(logger: first, "hello");
    spdlog::warn!(logger: second, "secret");
    let text = output.with_target(|target| String::from_utf8(target.clone()).unwrap());
    assert!(!text.contains("ignored"));
    assert!(text.contains("[rust/tests.rs:"));
    let text = warning.with_target(|target| String::from_utf8(target.clone()).unwrap());
    assert_eq!(text.lines().count(), 1);
    assert!(text.contains("[second]") && text.contains("***"));
}

#[test]
fn native_writer_reports_transport_errors_and_continues_fanout() {
    struct FailingWriter;
    impl Write for FailingWriter {
        fn write(&mut self, _: &[u8]) -> std::io::Result<usize> {
            Err(std::io::Error::other("transport failed"))
        }
        fn flush(&mut self) -> std::io::Result<()> {
            Ok(())
        }
    }
    let failed = WriteSink::builder()
        .target(FailingWriter)
        .build_arc()
        .unwrap();
    let memory = WriteSink::builder()
        .target(Vec::<u8>::new())
        .build_arc()
        .unwrap();
    let logger = logging::make_logger(
        "errors",
        vec![failed, memory.clone()],
        &Formatter::default(),
    )
    .unwrap();
    let errors = Arc::new(std::sync::atomic::AtomicUsize::new(0));
    let captured = errors.clone();
    logger.set_error_handler(move |_| {
        captured.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
    });
    spdlog::warn!(logger: logger, "still delivered");
    assert_eq!(errors.load(std::sync::atomic::Ordering::Relaxed), 1);
    assert!(memory
        .with_target(|target| String::from_utf8(target.clone()).unwrap())
        .contains("still delivered"));
}

#[test]
fn native_sinks_serialize_concurrent_records() {
    let memory = WriteSink::builder()
        .target(Vec::<u8>::new())
        .build_arc()
        .unwrap();
    let logger =
        logging::make_logger("workers", vec![memory.clone()], &Formatter::default()).unwrap();
    let threads: Vec<_> = (0..4)
        .map(|_| {
            let logger = logger.clone();
            std::thread::spawn(move || {
                for _ in 0..25 {
                    spdlog::info!(logger: logger, "record");
                }
            })
        })
        .collect();
    for thread in threads {
        thread.join().unwrap();
    }
    assert_eq!(
        memory
            .with_target(|target| String::from_utf8(target.clone()).unwrap())
            .lines()
            .count(),
        100
    );
}

#[test]
fn invalid_limits_and_open_failures_are_reported() {
    for (size, count) in [(0, 1), (1, 0), (1, 200001)] {
        assert!(RollingWriter::new("unused.log", size, count).is_err());
    }
    let directory = tempfile::tempdir().unwrap();
    assert!(RollingWriter::new(directory.path(), 10, 1).is_err());
    let sink = logging::rolling_file_sink(directory.path().join("valid.log"), 10, 1).unwrap();
    sink.flush().unwrap();
    assert!(logging::console_sink().is_ok());
}
