#pragma once

/// @file UsenetPostingHarness.h
/// @brief Posts files to a FakeNntpServer as yEnc articles and writes the NZB
///        that indexes them.
///
/// Shared by every test that drives a real UsenetQueue against a fake provider:
/// streaming, direct unpack, and the post-processing pipeline all need the same
/// "put these bytes on a news server" step and differ only in what they do next.

#include "FakeNntpServer.h"
#include "TestHelpers.h"

#include "decode/YencDecoder.h"
#include "nntp/NewsServer.h"
#include "prefs/Preferences.h"
#include "queue/UsenetQueue.h"

#include <QByteArray>
#include <QFile>
#include <QList>
#include <QString>
#include <QStringList>

namespace eMule::testing::usenet {

using eMule::usenet::NewsServer;
using eMule::NntpTlsMode;
using eMule::usenet::yencCrc32;

inline NewsServer serverConfig(quint16 port, int maxConnections)
{
    NewsServer s;
    s.name = QStringLiteral("fake");
    s.host = QStringLiteral("127.0.0.1");
    s.port = port;
    s.tlsMode = NntpTlsMode::None;
    s.user = QStringLiteral("testuser");
    s.pass = QStringLiteral("testpass");
    s.maxConnections = maxConnections;
    return s;
}

/// The message-ids of every BODY the server was asked for, in order.
inline QStringList bodyOrder(const FakeNntpServer& server)
{
    QStringList out;
    for (const QString& line : server.receivedCommands()) {
        if (!line.startsWith(QLatin1String("BODY "), Qt::CaseInsensitive))
            continue;
        out << line.mid(5).trimmed();
    }
    return out;
}

inline QByteArray makeArticleFor(const QByteArray& whole, int part, int partSize,
                          const QString& name)
{
    const int offset = (part - 1) * partSize;
    const QByteArray chunk = whole.mid(offset, partSize);

    QByteArray out;
    const int total = int((whole.size() + partSize - 1) / partSize);
    out += QStringLiteral("=ybegin part=%1 total=%2 line=128 size=%3 name=%4")
               .arg(part).arg(total).arg(whole.size()).arg(name).toLatin1();
    out += '\n';
    out += QStringLiteral("=ypart begin=%1 end=%2")
               .arg(offset + 1).arg(offset + chunk.size()).toLatin1();
    out += '\n';

    QByteArray line;
    for (const char raw : chunk) {
        const auto enc = quint8(quint8(raw) + 42);
        if (enc == 0x00 || enc == 0x0A || enc == 0x0D || enc == '=') {
            line.append('=');
            line.append(char(quint8(enc + 64)));
        } else {
            line.append(char(enc));
        }
        if (line.size() >= 128) {
            out += line;
            out += '\n';
            line.clear();
        }
    }
    if (!line.isEmpty()) {
        out += line;
        out += '\n';
    }

    out += QStringLiteral("=yend size=%1 part=%2 pcrc32=%3")
               .arg(chunk.size()).arg(part)
               .arg(yencCrc32(0, chunk), 8, 16, QLatin1Char('0')).toLatin1();
    return out;
}

inline QString multiId(int fileIndex, int part)
{
    return QStringLiteral("f%1p%2@example.com").arg(fileIndex).arg(part);
}

/// One posted file: its name and its bytes.
struct PostedFile {
    QString name;
    QByteArray data;
};

/// Post @p files to @p server as yEnc articles of @p partSize, and return the
/// NZB that indexes them.
inline QByteArray postFiles(FakeNntpServer& server, const QList<PostedFile>& files, int partSize)
{
    QByteArray xml;
    xml += R"(<?xml version="1.0" encoding="iso-8859-1" ?>)"
           "\n<nzb xmlns=\"http://www.newzbin.com/DTD/2003/nzb\">\n";

    for (int f = 0; f < files.size(); ++f) {
        const PostedFile& pf = files.at(f);
        const int parts = int((pf.data.size() + partSize - 1) / partSize);

        xml += QStringLiteral(
                   "  <file poster=\"tester\" date=\"1700000000\" "
                   "subject=\"&quot;%1&quot; yEnc (1/%2)\">\n")
                   .arg(pf.name).arg(parts).toUtf8();
        xml += "    <groups><group>alt.binaries.test</group></groups>\n";
        xml += "    <segments>\n";
        for (int p = 1; p <= parts; ++p) {
            const QByteArray article = makeArticleFor(pf.data, p, partSize, pf.name);
            server.addArticle(multiId(f, p), article);
            xml += QStringLiteral(
                       "      <segment bytes=\"%1\" number=\"%2\">%3</segment>\n")
                       .arg(article.size()).arg(p).arg(multiId(f, p)).toUtf8();
        }
        xml += "    </segments>\n  </file>\n";
    }

    xml += "</nzb>\n";
    return xml;
}

/// Read @p length bytes of the logical file at @p offset by walking the pieces,
/// which is what WebServer::serveRange does for real.
///
/// The general form: a set holds several inner files, each with its own length,
/// so a case cannot just read one file at one part size. Shared by the offline
/// stream cases and by the live preview test — one copy, so a change to the
/// piece model cannot leave the two disagreeing.
inline QByteArray readThroughPieces(const QList<eMule::usenet::UsenetQueue::StreamPiece>& pieces,
                                    qint64 offset, qint64 length)
{
    QByteArray out;
    for (const auto& p : pieces) {
        const qint64 pos = offset + out.size();
        if (out.size() >= length)
            break;
        if (p.virtualOffset + p.length <= pos)
            continue;
        if (p.virtualOffset > pos)
            break;

        QFile f(p.path);
        if (!f.open(QIODevice::ReadOnly))
            break;
        const qint64 into = pos - p.virtualOffset;
        if (!f.seek(p.fileOffset + into))
            break;
        out += f.read(qMin(length - out.size(), p.length - into));
    }
    return out;
}

inline void useTempPrefs(const eMule::testing::TempDir& tmp)
{
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});
}

} // namespace eMule::testing::usenet
