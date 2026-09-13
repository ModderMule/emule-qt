#include "pch.h"

/// @file ExternalUnpacker.cpp

#include "archive/ExternalUnpacker.h"

#include "archive/ArchiveReader.h"
#include "utils/Log.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>

namespace eMule {

namespace {

/// Search order. `7zz` is the official 7-Zip build and reads RAR5 as well as
/// anything; `7z` is p7zip and needs its rar codec present; `unrar` is
/// RAR-only but is the reference implementation for it. `7za` is last because
/// it cannot open a RAR at all, which is the format this class exists for.
constexpr const char* kCandidates[] = { "7zz", "7z", "unrar", "7za" };

/// Places a daemon's PATH routinely does not include. A headless emulecored
/// started by launchd or systemd inherits a minimal environment, so finding the
/// binary only on PATH would make the feature work in a terminal and nowhere
/// else.
const QStringList& extraSearchDirs()
{
    static const QStringList dirs = {
#ifdef Q_OS_WIN
        QStringLiteral("C:/Program Files/7-Zip"),
        QStringLiteral("C:/Program Files (x86)/7-Zip"),
        QStringLiteral("C:/Program Files/WinRAR"),
        QStringLiteral("C:/Program Files (x86)/WinRAR"),
#else
        QStringLiteral("/opt/homebrew/bin"),
        QStringLiteral("/usr/local/bin"),
        QStringLiteral("/usr/bin"),
        QStringLiteral("/bin"),
        QStringLiteral("/snap/bin"),
#endif
    };
    return dirs;
}

[[nodiscard]] ExternalUnpacker::Kind kindOf(const QString& path)
{
    const QString base = QFileInfo(path).completeBaseName().toLower();
    if (base == QLatin1String("unrar") || base == QLatin1String("rar"))
        return ExternalUnpacker::Kind::Unrar;
    if (base.startsWith(QLatin1String("7z")))
        return ExternalUnpacker::Kind::SevenZip;
    return ExternalUnpacker::Kind::None;
}

/// Find one candidate, PATH first and then the known install locations.
[[nodiscard]] QString findOne(const QString& name)
{
    QString found = QStandardPaths::findExecutable(name);
    if (found.isEmpty())
        found = QStandardPaths::findExecutable(name, extraSearchDirs());
    return found;
}

/// The words each tool uses for a refused passphrase. There is no exit code
/// that means it portably: unrar returns 11 only in recent builds and 3 — the
/// same code as a genuine CRC error — in older ones, and 7-Zip returns a plain
/// 2. So the text is the signal, and the code is the fallback.
[[nodiscard]] bool saysWrongPassword(const QByteArray& output)
{
    static constexpr const char* kPhrases[] = {
        "wrong password", "incorrect password", "password is incorrect",
        "cannot open encrypted archive",
    };
    const QString text = QString::fromLocal8Bit(output);
    for (const char* phrase : kPhrases) {
        if (text.contains(QLatin1String(phrase), Qt::CaseInsensitive))
            return true;
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

ExternalUnpacker::ExternalUnpacker(QString preferredTool)
{
    // A configured unpacker is used or nothing. Falling back to a different
    // binary when the named one is missing would quietly do the job with
    // something the user did not choose, and the misconfiguration would then
    // only surface as behaviour they cannot account for. A typo fails loudly
    // instead, with the path in the message.
    if (!preferredTool.isEmpty()) {
        if (!QFileInfo(preferredTool).isExecutable()) {
            logWarning(QStringLiteral("ExternalUnpacker: configured unpacker '%1' is not "
                                      "executable").arg(preferredTool));
            return;
        }
        m_kind = kindOf(preferredTool);
        if (m_kind == Kind::None) {
            logWarning(QStringLiteral("ExternalUnpacker: '%1' is not a 7-Zip or unrar "
                                      "binary").arg(preferredTool));
            return;
        }
        m_toolPath = std::move(preferredTool);
        return;
    }

    for (const char* name : kCandidates) {
        const QString path = findOne(QLatin1String(name));
        if (path.isEmpty())
            continue;
        m_toolPath = path;
        m_kind = kindOf(path);
        break;
    }
}

bool ExternalUnpacker::available() const
{
    return m_kind != Kind::None && !m_toolPath.isEmpty();
}

QString ExternalUnpacker::toolPath() const
{
    return m_toolPath;
}

ExternalUnpacker::Kind ExternalUnpacker::toolKind() const
{
    return m_kind;
}

QString ExternalUnpacker::toolName() const
{
    return m_toolPath.isEmpty() ? QString() : QFileInfo(m_toolPath).fileName();
}

void ExternalUnpacker::cancel()
{
    m_cancelled.store(true);
}

void ExternalUnpacker::setTimeoutMs(qint64 ms)
{
    m_timeoutMs = ms;
}

ExternalUnpacker::Result ExternalUnpacker::extract(const QStringList& volumes,
                                                   const QString& destDir,
                                                   const QString& password)
{
    Result result;

    if (volumes.isEmpty()) {
        result.error = QObject::tr("No archive volumes to unpack");
        return result;
    }
    if (!available()) {
        result.outcome = Outcome::NoTool;
        result.error = QObject::tr("No external unpacker found — install 7-Zip or unrar "
                                   "to unpack password-protected archives");
        return result;
    }

    // A missing later volume is the difference between "extract what is there"
    // and "this release is incomplete", and only the caller's list knows.
    for (const QString& volume : volumes) {
        if (!QFileInfo::exists(volume)) {
            result.error = QObject::tr("%1 is missing")
                               .arg(QFileInfo(volume).fileName());
            return result;
        }
    }

    // Extract into a private directory and move members out through
    // safeEntryPath(). Both tools sanitise member names themselves, but that is
    // their policy rather than a promise, and this is the one place in the
    // program where an archive from a stranger is handed to a process that
    // writes wherever it likes.
    const QString staging =
        QDir(destDir).filePath(QStringLiteral(".extern-")
                               + QUuid::createUuid().toString(QUuid::Id128));
    if (!QDir().mkpath(staging)) {
        result.error = QObject::tr("Could not create %1").arg(staging);
        return result;
    }

    const QString archive = volumes.first();
    QStringList args;
    QStringList redacted;
    if (m_kind == Kind::SevenZip) {
        args = { QStringLiteral("x"), QStringLiteral("-y"), QStringLiteral("-bd"),
                 passwordArg(password), QStringLiteral("-o") + staging, archive };
    } else {
        // The trailing separator is what makes unrar treat it as a directory;
        // without it a set with one member is written to a *file* of that name.
        args = { QStringLiteral("x"), QStringLiteral("-y"), QStringLiteral("-o+"),
                 passwordArg(password), archive, staging + QLatin1Char('/') };
    }
    redacted = args;
    redacted.replaceInStrings(passwordArg(password), QStringLiteral("-p<hidden>"));

    QString error;
    const int code = run(args, redacted, {}, nullptr, error);

    harvest(staging, destDir, result);
    QDir(staging).removeRecursively();

    if (m_cancelled.load()) {
        for (const QString& path : std::as_const(result.extractedFiles))
            QFile::remove(path);
        result.extractedFiles.clear();
        result.error = QObject::tr("Cancelled");
        return result;
    }

    if (code == 0 || (m_kind == Kind::Unrar && code == 1)) {
        // unrar's 1 is "warnings", which a set with a stray comment or an
        // unsupported attribute earns while extracting perfectly.
        result.outcome = Outcome::Ok;
        return result;
    }

    // Half-written members are worse than none: post-processing would take them
    // for a finished extraction and publish a truncated release.
    for (const QString& path : std::as_const(result.extractedFiles))
        QFile::remove(path);
    result.extractedFiles.clear();

    if (error.contains(QLatin1String("wrong-password"))) {
        result.outcome = Outcome::WrongPassword;
        result.error = QObject::tr("%1 could not be unpacked: wrong password")
                           .arg(QFileInfo(archive).fileName());
        return result;
    }

    result.outcome = Outcome::Failed;
    result.error = QObject::tr("%1 could not unpack %2 (exit %3)")
                       .arg(toolName(), QFileInfo(archive).fileName())
                       .arg(code);
    return result;
}

QList<ExternalUnpacker::Member> ExternalUnpacker::listMembers(const QStringList& volumes,
                                                              const QString& password,
                                                              QString& error) const
{
    if (volumes.isEmpty() || !available())
        return {};

    QStringList args;
    if (m_kind == Kind::SevenZip) {
        args = { QStringLiteral("l"), QStringLiteral("-slt"), QStringLiteral("-y"),
                 passwordArg(password), volumes.first() };
    } else {
        args = { QStringLiteral("lb"), passwordArg(password), volumes.first() };
    }
    QStringList redacted = args;
    redacted.replaceInStrings(passwordArg(password), QStringLiteral("-p<hidden>"));

    QByteArray captured;
    // const because listing changes nothing the caller can see; the cancel flag
    // is atomic and the tool path is fixed at construction.
    auto* self = const_cast<ExternalUnpacker*>(this);
    self->run(args, redacted, {}, &captured, error);

    QList<Member> members;
    const QString text = QString::fromUtf8(captured);
    if (m_kind == Kind::Unrar) {
        // `lb` is one bare name per line and nothing else. No sizes — see the
        // header for why they are not parsed out of `l` instead.
        for (const QString& line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const QString name = line.trimmed();
            if (!name.isEmpty())
                members.append({name, 0});
        }
        return members;
    }

    // `-slt` is a blank-line-separated record per member, each starting at its
    // `Path =`. The archive's own name appears in the header block before the
    // first record, so only lines after the `----------` separator count.
    bool inRecords = false;
    for (const QString& raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (!inRecords) {
            inRecords = line.startsWith(QLatin1String("----------"));
            continue;
        }
        if (line.startsWith(QLatin1String("Path = "))) {
            members.append({line.mid(7), 0});
        } else if (line.startsWith(QLatin1String("Size = ")) && !members.isEmpty()) {
            bool ok = false;
            const qint64 size = line.mid(7).toLongLong(&ok);
            if (ok)
                members.last().size = size;
        } else if (!members.isEmpty()
                   && (line.startsWith(QLatin1String("Folder = +"))
                       || line.startsWith(QLatin1String("Attributes = D")))) {
            // A directory record. Dropping it here keeps every caller from
            // having to know that `l -slt` lists them.
            members.removeLast();
        }
    }
    return members;
}

qint64 ExternalUnpacker::streamMemberTo(const QStringList& volumes, const QString& member,
                                        const QString& outPath, const QString& password)
{
    if (volumes.isEmpty() || !available())
        return -1;

    QStringList args;
    if (m_kind == Kind::SevenZip) {
        args = { QStringLiteral("x"), QStringLiteral("-so"), QStringLiteral("-y"),
                 QStringLiteral("-bd"), passwordArg(password), volumes.first(), member };
    } else {
        // `p` prints a member; `-inul` suppresses the banner, which would
        // otherwise be the first bytes of the "file".
        args = { QStringLiteral("p"), QStringLiteral("-inul"), passwordArg(password),
                 volumes.first(), member };
    }
    QStringList redacted = args;
    redacted.replaceInStrings(passwordArg(password), QStringLiteral("-p<hidden>"));

    QString error;
    run(args, redacted, outPath, nullptr, error);

    // The exit code is deliberately ignored. A run over an incomplete volume set
    // *always* ends in an error — that is what "the download has not finished"
    // looks like from the outside — and the bytes it produced before that are
    // exactly the prefix a player wants.
    const qint64 size = QFileInfo(outPath).size();
    return size > 0 ? size : (m_cancelled.load() ? -1 : 0);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

QString ExternalUnpacker::passwordArg(const QString& password) const
{
    if (!password.isEmpty())
        return QStringLiteral("-p") + password;

    // Without this both tools *prompt*, and a daemon with no terminal then hangs
    // until the timeout rather than failing in a second.
    return m_kind == Kind::Unrar ? QStringLiteral("-p-") : QStringLiteral("-p");
}

int ExternalUnpacker::run(const QStringList& args, const QStringList& redactedArgs,
                          const QString& stdoutPath, QByteArray* captured, QString& error)
{
    QProcess process;
    process.setProgram(m_toolPath);
    process.setArguments(args);
    process.setProcessChannelMode(QProcess::MergedChannels);

    QFile out;
    const bool toFile = !stdoutPath.isEmpty();
    if (toFile) {
        // Merged channels would put the banner into the payload.
        process.setProcessChannelMode(QProcess::SeparateChannels);
        out.setFileName(stdoutPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            error = out.errorString();
            return -1;
        }
    }

    logInfo(QStringLiteral("ExternalUnpacker: %1 %2")
                .arg(toolName(), redactedArgs.join(QLatin1Char(' '))));

    process.start();
    if (!process.waitForStarted(10'000)) {
        error = QObject::tr("Could not start %1").arg(toolName());
        return -1;
    }

    QByteArray tail;
    QElapsedTimer clock;
    clock.start();

    for (;;) {
        const bool done = process.waitForFinished(200);

        if (toFile) {
            const QByteArray chunk = process.readAllStandardOutput();
            if (!chunk.isEmpty())
                out.write(chunk);
            tail += process.readAllStandardError();
        } else {
            tail += process.readAll();
        }
        // Two different jobs, and the cap has to know which. When nothing asked
        // for the output it is only scanned for a phrase, so keeping the tail is
        // enough and a tool that prints a line per member must not hold a whole
        // release's file list in memory. When a caller *is* capturing, the part
        // that matters is at the *front* — a listing's `----------` marker —
        // and trimming from the left would silently return no members at all.
        if (!captured && tail.size() > 64 * 1024)
            tail = tail.right(32 * 1024);

        if (done || process.state() == QProcess::NotRunning)
            break;

        if (m_cancelled.load()) {
            process.kill();
            process.waitForFinished(2'000);
            error = QStringLiteral("cancelled");
            break;
        }
        if (clock.elapsed() > m_timeoutMs) {
            process.kill();
            process.waitForFinished(2'000);
            error = QObject::tr("%1 timed out").arg(toolName());
            break;
        }
    }

    if (toFile) {
        out.write(process.readAllStandardOutput());
        out.flush();
        out.close();
        tail += process.readAllStandardError();
    } else {
        tail += process.readAll();
    }

    if (captured)
        *captured = tail;

    if (saysWrongPassword(tail))
        error = QStringLiteral("wrong-password");

    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
}

void ExternalUnpacker::harvest(const QString& staging, const QString& destDir, Result& result)
{
    const QDir stagingDir(staging);
    QDirIterator it(staging, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString from = it.next();
        const QString relative = stagingDir.relativeFilePath(from);

        const QString to = ArchiveReader::safeEntryPath(destDir, relative);
        if (to.isEmpty()) {
            logWarning(QStringLiteral("ExternalUnpacker: refusing unsafe member '%1'")
                           .arg(relative));
            result.rejectedEntries.append(relative);
            QFile::remove(from);
            continue;
        }

        QDir().mkpath(QFileInfo(to).absolutePath());
        QFile::remove(to);   // a re-run over an earlier attempt's output
        if (!QFile::rename(from, to)) {
            // Different filesystems, which is normal when the staging directory
            // and the destination are not the same volume.
            if (!QFile::copy(from, to)) {
                logWarning(QStringLiteral("ExternalUnpacker: could not place '%1'")
                               .arg(relative));
                continue;
            }
            QFile::remove(from);
        }
        result.extractedFiles.append(to);
    }

    result.extractedFiles.sort();
}

} // namespace eMule
