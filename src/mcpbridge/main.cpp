/// @file main.cpp
/// @brief emuleqt-mcp — MCP stdio bridge to a running eMuleQt daemon.
///
/// stdout carries the protocol and nothing else; everything a human should
/// read goes to stderr.

#include "McpStdioBridge.h"

#include "app/AppConfig.h"

#include <QCommandLineParser>
#include <QCoreApplication>

#include <cstdio>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("emuleqt-mcp"));
    QCoreApplication::setApplicationVersion(QString(eMule::kAppVersion));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "MCP stdio bridge for eMuleQt. Forwards JSON-RPC from stdin to the daemon's /mcp "
        "endpoint.\nWithout options it reads port and API key from the local configuration."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption urlOption(QStringLiteral("url"),
        QStringLiteral("MCP endpoint, e.g. http://192.168.1.10:4711/mcp."), QStringLiteral("url"));
    const QCommandLineOption keyOption(QStringLiteral("api-key"),
        QStringLiteral("API key. Default: $EMULEQT_API_KEY, then the local configuration."),
        QStringLiteral("key"));
    const QCommandLineOption configOption(QStringLiteral("config"),
        QStringLiteral("Configuration directory of the daemon."), QStringLiteral("dir"));
    const QCommandLineOption insecureOption(QStringLiteral("insecure"),
        QStringLiteral("Accept a self-signed HTTPS certificate."));
    parser.addOptions({urlOption, keyOption, configOption, insecureOption});
    parser.process(app);

    using eMule::McpStdioBridge;
    McpStdioBridge::Target target;
    QString problem;
    if (parser.isSet(urlOption)) {
        target.url = QUrl(parser.value(urlOption));
    } else {
        if (parser.isSet(configOption))
            eMule::AppConfig::setConfigDirOverride(parser.value(configOption));
        target = McpStdioBridge::targetFromConfig(eMule::AppConfig::configDir(), problem);
    }
    if (parser.isSet(keyOption))
        target.apiKey = parser.value(keyOption).toUtf8();
    else if (const QByteArray env = qgetenv("EMULEQT_API_KEY"); !env.isEmpty())
        target.apiKey = env;
    if (parser.isSet(insecureOption))
        target.insecureTls = true;

    if (!problem.isEmpty())
        std::fprintf(stderr, "emuleqt-mcp: %s\n", qPrintable(problem));
    if (!target.url.isValid() || target.url.host().isEmpty()) {
        std::fprintf(stderr, "emuleqt-mcp: no endpoint — pass --url\n");
        return 2;
    }
    std::fprintf(stderr, "emuleqt-mcp: forwarding to %s\n",
                 qPrintable(target.url.toString(QUrl::RemoveUserInfo)));

    McpStdioBridge bridge(target, [](const QByteArray& line) {
        std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
    });
    QObject::connect(&bridge, &McpStdioBridge::finished, &app, &QCoreApplication::quit);

    // Blocking reads on their own thread; the lines are handled on the main one.
    std::thread reader([&bridge] {
        std::string line;
        while (std::getline(std::cin, line)) {
            QMetaObject::invokeMethod(&bridge, [&bridge, bytes = QByteArray::fromStdString(line)] {
                bridge.handleLine(bytes);
            }, Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(&bridge, &McpStdioBridge::inputClosed, Qt::QueuedConnection);
    });
    reader.detach();

    return QCoreApplication::exec();
}
