#pragma once

#include <QString>

namespace applog {

/// Installs a bounded asynchronous console/file logger after app names are set.
/// Normal messages may be dropped on overload; a summary records their count.
/// logPath optionally selects an explicit destination, e.g. an isolated test log.
void install(const QString &logPath = {});

/// Drains queued messages, joins the writer and restores the prior Qt handler.
void shutdown();

/// Waits up to timeoutMs milliseconds for already accepted messages to be written.
/// Returns false on timeout; success means the worker attempted the output,
/// not that bytes reached physical storage. Use before exporting a log snapshot.
bool flush(int timeoutMs = 2000);

struct LogStats {
    quint64 queuedLines;
    quint64 queuedBytes;
    quint64 droppedLines;
    quint64 writtenBatches;
};

/// Returns queue/drop/batch counters without performing any file operations.
LogStats stats();

// Absolute path of the current log file (empty before install()).
QString filePath();

// Opens the log file, or its folder if the file does not exist yet.
void openLogFile();

// Opens the Windows "Open with" picker for the log file.
void openLogFileWith();

// Opens the folder containing the current log file.
void openLogFolder();

// Opens (and creates if needed) the diagnostics bundle folder.
void openDiagnosticsFolder();

} // namespace applog
