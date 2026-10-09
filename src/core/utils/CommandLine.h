#pragma once

/// @file CommandLine.h
/// @brief QCommandLineParser::process() without the message box that never closes.

class QCommandLineParser;
class QCoreApplication;

namespace eMule {

/// Like parser.process(app). On Windows without a console (CI, a launcher) Qt
/// shows --version, --help and option errors in a message box and waits for a
/// click; here that box counts down on its OK button and closes by itself.
void processCommandLine(QCommandLineParser& parser, const QCoreApplication& app);

} // namespace eMule
