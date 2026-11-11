"""Standard logging policy built on Python's logging handlers.

Use ordinary named loggers, filters, QueueHandler/QueueListener, MemoryHandler,
and third-party handlers. This module never changes the process-wide root logger.
See docs/logging.md for the cross-language format and rotation contract.
"""

from datetime import datetime, UTC
import logging
from logging.handlers import RotatingFileHandler
from pathlib import Path
import sys

TRACE = 5
MAX_BYTES = 10 * 1024 * 1024
BACKUP_COUNT = 5
_LABELS = ((50, 'CRITICAL'), (40, 'ERROR'), (30, 'WARN'), (20, 'INFO'), (10, 'DEBUG'), (0, 'TRACE'))
_COLORS = {'TRACE': '35', 'DEBUG': '34', 'INFO': '32', 'WARN': '33', 'ERROR': '31', 'CRITICAL': '1;31'}


def source_path(file: str, project_root: str) -> str:
    """Keep source paths relative to the consumer root, with portable separators.

    Paths outside the supplied root stay absolute. Relative compiler paths stay
    relative. Windows drive paths are compared case-insensitively on every OS.
    """
    file = file.replace('\\', '/').rstrip('/') or '/'
    root = str(project_root).replace('\\', '/').rstrip('/')
    while file.startswith('./'):
        file = file[2:]
    prefix = root + '/'
    comparable_file, comparable_prefix = file, prefix
    if len(root) > 1 and root[1] == ':':
        comparable_file, comparable_prefix = file.lower(), prefix.lower()
    if project_root and comparable_file.startswith(comparable_prefix):
        file = file[len(prefix):]
    return file if file != '/' else 'unknown'


def _escape(text: str, field: bool = False) -> str:
    return ''.join(f'\\x{ord(c):02x}' if ord(c) < 32 or ord(c) == 127 or (field and c in '[]') else c for c in text)


class Formatter(logging.Formatter):
    """UTC milliseconds, severity, logger name, project-relative file:line, message.

    Literal secrets are removed from formatted text, including tracebacks and
    metadata. This does not mutate LogRecords used by other handlers.
    """

    def __init__(self, project_root: str = '', *, color: bool = False, secrets=()):
        super().__init__()
        self.project_root = str(project_root)
        self.color = color
        self.secrets = sorted((secret for secret in secrets if secret), key=len, reverse=True)

    def _redact(self, text: str) -> str:
        for secret in self.secrets:
            text = text.replace(secret, '***')
        return text

    def format(self, record: logging.LogRecord) -> str:
        """Format without a terminal newline, as expected by logging.Handler."""
        timestamp = datetime.fromtimestamp(record.created, UTC).isoformat(timespec='milliseconds')
        timestamp = timestamp.replace('+00:00', 'Z')
        label = next(label for minimum, label in _LABELS if record.levelno >= minimum)
        path = source_path(record.pathname, self.project_root)
        location = 'unknown:?'
        if path != 'unknown' and record.lineno > 0:
            location = f'{_escape(self._redact(path), True)}:{record.lineno}'
        prefix = f'{timestamp} [{label}] [{_escape(self._redact(record.name or "app"), True)}] [{location}] '
        message = record.getMessage()
        if record.exc_info:
            message += '\n' + self.formatException(record.exc_info)
        elif record.exc_text:
            message += '\n' + record.exc_text
        if record.stack_info:
            message += '\n' + self.formatStack(record.stack_info)
        message = self._redact(message).replace('\r\n', '\n').replace('\r', '\n')
        result = '\n'.join(prefix + _escape(line) for line in message.split('\n'))
        if self.color:
            start = len(timestamp) + 2
            result = result[:start] + f'\x1b[{_COLORS[label]}m' + label + '\x1b[0m' + result[start + len(label):]
        return result


def console_handler(project_root: str = '', *, level: int = logging.INFO, stream=None, color: bool | None = None,
                    secrets=()) -> logging.StreamHandler:
    """Create a stdout handler; automatic color uses the stream's isatty().

    Windows callers must enable ANSI terminal processing before forcing color.
    Streams remain owned by the caller. No global logger is reconfigured.
    """
    stream = sys.stdout if stream is None else stream
    handler = logging.StreamHandler(stream)
    handler.setLevel(level)
    use_color = bool(getattr(stream, 'isatty', lambda: False)()) if color is None else color
    handler.setFormatter(Formatter(project_root, color=use_color, secrets=secrets))
    return handler


class RollingFileHandler(RotatingFileHandler):
    """Native rotation with UTF-8 byte accounting and spdlog-compatible names.

    app.log becomes app.1.log through app.5.log. One handler/listener must own a
    path; share that handler between named loggers. Complete oversized records
    stay intact. Existing files are appended to after restart.
    """

    def __init__(self, filename, project_root: str = '', *, level: int = logging.INFO,
                 max_bytes: int = MAX_BYTES, backup_count: int = BACKUP_COUNT, secrets=()):
        if max_bytes <= 0 or not 1 <= backup_count <= 200000:
            raise ValueError('Positive logging limits and at most 200000 backups are required')
        path = Path(filename)
        path.parent.mkdir(parents=True, exist_ok=True)
        super().__init__(path, maxBytes=max_bytes, backupCount=backup_count, encoding='utf-8')
        self.setLevel(level)
        self.setFormatter(Formatter(project_root, secrets=secrets))
        self.namer = self._backup_name

    @staticmethod
    def _backup_name(default_name: str) -> str:
        base, index = default_name.rsplit('.', 1)
        path = Path(base)
        return str(path.with_name(f'{path.stem}.{index}{path.suffix}'))

    def _open(self):
        # Native FileHandler otherwise translates LF to CRLF on Windows.
        return open(self.baseFilename, self.mode, encoding=self.encoding, errors=self.errors, newline='\n')

    def shouldRollover(self, record: logging.LogRecord) -> bool:
        """Rotate before an incoming record would exceed the byte limit."""
        self.stream.flush()
        size = Path(self.baseFilename).stat().st_size
        incoming = len((self.format(record) + self.terminator).encode('utf-8'))
        return size > 0 and size + incoming > self.maxBytes


class CallbackHandler(logging.Handler):
    """Send standard, redacted text to a caller-provided transport or queue.

    The callback gets one complete record including LF. Handler locking
    serializes callbacks. Use QueueHandler/QueueListener for slow network I/O.
    Delivery, retries, authentication, and destination limits belong to the app.
    """

    def __init__(self, callback, project_root: str = '', *, level: int = logging.WARNING, secrets=()):
        super().__init__(level)
        self.callback = callback
        self.setFormatter(Formatter(project_root, secrets=secrets))

    def emit(self, record: logging.LogRecord):
        """Report transport errors through the standard logging error handler."""
        try:
            self.callback(self.format(record) + '\n')
        except Exception:
            self.handleError(record)
