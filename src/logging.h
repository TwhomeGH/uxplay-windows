#pragma once

#include <QString>

namespace applog {

// Installs a Qt message handler that mirrors output to a rolling log file.
// Call after QApplication has its organization/application name set.
void install();

// Closes the log file and restores the default Qt message handler.
void shutdown();

// Absolute path of the current log file (empty before install()).
QString filePath();

// Opens the log file, or its folder if the file does not exist yet.
void openLogFile();

} // namespace applog
