"""Cross-language conformance and standard-library integration tests."""

from concurrent.futures import ThreadPoolExecutor
import io
import logging
from logging.handlers import MemoryHandler, QueueHandler, QueueListener
from pathlib import Path
from queue import Queue
import sys

import pytest

from lizardbyte_common import logging as common


def record(level=logging.INFO, name='network', message='héllo', file='/workspace/project/src/client.cpp', line=42):
    value = logging.LogRecord(name, level, file, line, message, (), None)
    value.created = 0.123
    return value


def test_shared_golden_bytes():
    formatter = common.Formatter('/workspace/project', secrets=['secret', 'secret-long', ''])
    actual = ''.join(formatter.format(record(level)) + '\n' for level in [5, 10, 20, 30, 40, 50])
    actual += formatter.format(record(name='net]\n', message='secret-long\r\nnext\t\x1b[31m')) + '\n'
    actual += formatter.format(record(name='', message='metadata absent', file='', line=0)) + '\n'
    fixture = Path(__file__).parents[1] / 'fixtures/logging-format.txt'
    assert actual.encode() == fixture.read_bytes()


def test_empty_messages_and_trailing_newlines():
    prefix = '1970-01-01T00:00:00.123Z [INFO] [app] [unknown:?] '
    formatter = common.Formatter()
    assert formatter.format(record(name='', message='', file='', line=0)) == prefix
    assert formatter.format(record(name='', message='x\r', file='', line=0)) == prefix + 'x\n' + prefix


@pytest.mark.parametrize(('file', 'root', 'expected'), [
    ('C:\\Projects\\App\\src\\main.py', 'c:/projects/app/', 'src/main.py'),
    ('/app2/src/main.py', '/app', '/app2/src/main.py'),
    ('./src/main.py', '/app', 'src/main.py'),
    ('/src/main.py', '/', 'src/main.py'),
    ('', '', 'unknown'),
])
def test_source_paths(file, root, expected):
    assert common.source_path(file, root) == expected


def test_color_only_marks_first_severity():
    text = common.Formatter(color=True).format(record(message='first\nsecond'))
    assert text.count('\x1b[') == 2
    assert '[\x1b[32mINFO\x1b[0m]' in text
    assert '\n1970-01-01T00:00:00.123Z [INFO]' in text


def test_console_detects_terminal_and_keeps_stream_owned(monkeypatch):
    stream = io.StringIO()
    stream.isatty = lambda: True
    handler = common.console_handler(stream=stream)
    handler.handle(record())
    assert '\x1b[32m' in stream.getvalue()
    handler.close()
    assert not stream.closed
    plain = common.console_handler(stream=io.StringIO(), color=False)
    assert '\x1b' not in plain.format(record())
    monkeypatch.setattr(sys, 'stdout', stream)
    assert common.console_handler().stream is stream


def test_traceback_stack_and_cached_exception_are_redacted():
    value = record(message='token %s')
    value.args = ('secret',)
    try:
        raise ValueError('secret')
    except ValueError:
        value.exc_info = sys.exc_info()
    value.stack_info = 'stack secret'
    formatter = common.Formatter(secrets=['secret'])
    output = formatter.format(value)
    assert 'ValueError: ***' in output
    assert 'stack ***' in output
    assert 'secret' not in output
    assert value.args == ('secret',)
    value.exc_info = None
    value.exc_text = 'cached secret'
    assert 'cached ***' in formatter.format(value)


def test_rollover_boundary_unicode_retention_and_restart(tmp_path):
    path = tmp_path / 'nested/application.log'
    value = record(name='test', message='é', file='', line=0)
    text = (common.Formatter().format(value) + '\n').encode()
    for _ in range(7):
        handler = common.RollingFileHandler(path, max_bytes=len(text) * 2, backup_count=2)
        handler.handle(value)
        handler.close()
    assert path.read_bytes() == text
    assert path.with_name('application.1.log').read_bytes() == text * 2
    assert path.with_name('application.2.log').read_bytes() == text * 2
    assert not path.with_name('application.3.log').exists()
    assert b'\r' not in text


def test_oversized_record_is_preserved_and_no_empty_backup(tmp_path):
    path = tmp_path / 'large.log'
    handler = common.RollingFileHandler(path, max_bytes=1, backup_count=1)
    handler.handle(record(message='complete record'))
    assert not path.with_name('large.1.log').exists()
    handler.handle(record(message='next record'))
    handler.close()
    assert b'complete record' in path.with_name('large.1.log').read_bytes()


@pytest.mark.parametrize(('max_bytes', 'backups'), [(0, 1), (1, 0), (1, 200001)])
def test_invalid_limits(tmp_path, max_bytes, backups):
    with pytest.raises(ValueError, match='Positive'):
        common.RollingFileHandler(tmp_path / 'unused.log', max_bytes=max_bytes, backup_count=backups)


@pytest.mark.parametrize(('base', 'expected'), [
    ('app.log.1', 'app.1.log'), ('app.1', 'app.1'), ('.hidden.1', '.hidden.1'),
])
def test_backup_names(base, expected):
    assert common.RollingFileHandler._backup_name(base) == expected


def test_multiple_named_loggers_independent_sinks_and_callbacks():
    stream = io.StringIO()
    console = common.console_handler(stream=stream, level=logging.INFO)
    received = []
    callback = common.CallbackHandler(received.append, secrets=['secret'])
    loggers = [logging.Logger('first', common.TRACE), logging.Logger('second', common.TRACE)]
    for logger in loggers:
        logger.addHandler(console)
        logger.addHandler(callback)
    loggers[0].debug('ignored')
    loggers[0].info('first message')
    loggers[1].warning('secret')
    assert len(received) == 1
    assert '[second]' in received[0]
    assert '***' in received[0]
    assert received[0].endswith('\n')
    assert 'ignored' not in stream.getvalue()


def test_callback_failure_uses_native_error_handler(monkeypatch):
    def fail(_):
        raise OSError('transport failed')
    handler = common.CallbackHandler(fail)
    errors = []
    monkeypatch.setattr(handler, 'handleError', errors.append)
    value = record(logging.WARNING)
    handler.handle(value)
    assert errors == [value]


def test_bounded_startup_buffer_and_async_queue():
    received = []
    target = common.CallbackHandler(received.append, level=common.TRACE)
    memory = MemoryHandler(capacity=2, flushLevel=logging.ERROR, target=target)
    memory.handle(record(message='startup'))
    assert not received
    memory.flush()
    assert 'startup' in received[0]
    queue = Queue(maxsize=64)
    listener = QueueListener(queue, target, respect_handler_level=True)
    logger = logging.Logger('worker', common.TRACE)
    logger.addHandler(QueueHandler(queue))
    listener.start()
    logger.info('asynchronous')
    listener.stop()
    assert 'asynchronous' in received[-1]
    memory.close()


def test_threaded_records_remain_complete():
    received = []
    handler = common.CallbackHandler(received.append, level=common.TRACE)
    with ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(lambda index: handler.handle(record(message=str(index))), range(100)))
    assert len(received) == 100
    assert all(text.count('\n') == 1 for text in received)
