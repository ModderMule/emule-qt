#include "pch.h"
/// @file FileDetailDialog.cpp
/// @brief Tabbed file-detail dialog implementation.

#include "FileDetailDialog.h"

#include "CommentEditPanel.h"
#include "FileDetailsMerge.h"
#include "ArchivePreviewPanel.h"
#include "MetadataPage.h"
#include "app/IpcClient.h"
#include "controls/ArchiveContentsView.h"
#include "utils/ArchiveContentsIpc.h"
#include "controls/AbstractListView.h"
#include "controls/ContentScrollArea.h"
#include "prefs/Preferences.h"
#include "utils/ClientIcons.h"
#include "utils/Ed3kKeyText.h"
#include "utils/Clipboard.h"
#include "utils/DialogSizing.h"
#include "utils/IpcFeedback.h"
#include "utils/OtherFunctions.h"
#include "app/SkinIcons.h"

#include <QCborArray>
#include <QCheckBox>
#include <QIcon>
#include <QDateTime>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPalette>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "utils/StringUtils.h"

#include <memory>

namespace eMule {

// ── helpers ────────────────────────────────────────────────────────────

namespace {

/// Read a CBOR string field with fallback.
QString str(const QCborMap& m, QLatin1StringView key)
{
    return m.value(key).toString();
}

/// Read a CBOR integer field.
qint64 num(const QCborMap& m, QLatin1StringView key)
{
    return m.value(key).toInteger();
}

/// Format a unix timestamp, return "—" if zero.
QString fmtTime(qint64 epoch)
{
    if (epoch <= 0)
        return QStringLiteral("\u2014");
    return QLocale().toString(QDateTime::fromSecsSinceEpoch(epoch), QLocale::ShortFormat);
}

/// MFC's "%s   (%s ago)" date, or @p none when not set (FileDetailDialogInfo.cpp:136-182)
QString fmtTimeAgo(qint64 epoch, const QString& none)
{
    if (epoch <= 0)
        return none;
    const qint64 ago = std::max<qint64>(0, QDateTime::currentSecsSinceEpoch() - epoch);
    return FileDetailDialog::tr("%1   (%2 ago)").arg(fmtTime(epoch), formatSecondsLongHM(ago));
}

/// "1.2 MB (3.4%)" — the share is of @p whole, 0 when there is none
QString sizeAndShare(qint64 part, qint64 whole)
{
    const double pct = whole > 0 ? 100.0 * static_cast<double>(part) / static_cast<double>(whole) : 0.0;
    return QStringLiteral("%1 (%2%)").arg(formatByteSize(part), formatPercent(pct));
}

/// One of MFC's group boxes: right-aligned labels, and a second label/value
/// pair on the right where MFC puts one.
class DetailGroup {
public:
    DetailGroup(const QString& title, QBoxLayout* into)
        : m_box(new QGroupBox(title))
        , m_grid(new QGridLayout(m_box))
    {
        m_grid->setColumnStretch(1, 1);
        m_grid->setColumnStretch(3, 1);
        m_grid->setHorizontalSpacing(8);
        m_grid->setVerticalSpacing(4);   // MFC's rows sit close
        into->addWidget(m_box);
    }

    /// A row with the value over the whole width (name, path, hashes)
    QLabel* wide(const QString& label, const QString& value)
    {
        auto* v = detailValueLabel(value);
        m_grid->addWidget(caption(label), m_row, 0);
        m_grid->addWidget(v, m_row++, 1, 1, 3);
        return v;
    }

    /// A row with up to two pairs; an empty @p rightLabel leaves the right free
    QLabel* pair(const QString& label, const QString& value,
                 const QString& rightLabel = {}, const QString& rightValue = {})
    {
        auto* v = shortValue(value);
        m_grid->addWidget(caption(label), m_row, 0);
        m_grid->addWidget(v, m_row, 1);
        if (!rightLabel.isEmpty()) {
            m_grid->addWidget(caption(rightLabel), m_row, 2);
            m_grid->addWidget(shortValue(rightValue), m_row, 3);
        }
        ++m_row;
        return v;
    }

private:
    static QLabel* caption(const QString& label)
    {
        auto* l = new QLabel(label + QLatin1Char(':'));
        l->setAlignment(Qt::AlignRight | Qt::AlignTop);
        return l;
    }

    static QLabel* shortValue(const QString& text)
    {
        auto* l = new QLabel(text);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return l;
    }

    QGroupBox*   m_box;
    QGridLayout* m_grid;
    int          m_row = 0;
};

} // anonymous namespace

// ── constructor ────────────────────────────────────────────────────────

FileDetailDialog::FileDetailDialog(const QCborMap& details, Tab initialTab,
                                   QWidget* parent)
    : DetailDialog(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);

    // Floors only — the General tab grows a row for an AICH hash and its file-name
    // value wraps, so the height the tabs need is not a constant. Opens tall enough for
    // all of it, but the General page scrolls, so the user may shrink it (issue #181).
    setDesignedSize(QSize(680, 420), QSize(720, 480), DialogSizing::Fit::Scroll);

    // Explicitly qualified: a constructor must not dispatch virtually.
    m_pendingTab = initialTab;
    FileDetailDialog::setDetails(details);
}

FileDetailDialog::FileDetailDialog(const QList<QCborMap>& files, Tab initialTab, QWidget* parent)
    : DetailDialog(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setDesignedSize(QSize(680, 420), QSize(720, 480), DialogSizing::Fit::Scroll);

    m_pendingTab = initialTab;
    if (files.size() == 1) {
        FileDetailDialog::setDetails(files.first());
        return;
    }
    setSubjectKey({});   // no single file a refresh could be meant for
    setWindowTitle(tr("File Details: %n files", nullptr, static_cast<int>(files.size())));
    buildMultiTabs(mergeFileDetails(files), initialTab);
}

void FileDetailDialog::buildMultiTabs(const QCborMap& merged, Tab tabToSelect)
{
    m_tabs = new QTabWidget;
    const bool icons = thePrefs.useOriginalIcons();
    const auto add = [&](QWidget* page, const char* icon, const QString& title) {
        if (icons)
            m_tabs->addTab(page, skinIcon(QStringLiteral(":/icons/") + QLatin1StringView(icon)), title);
        else
            m_tabs->addTab(page, title);
    };
    add(createGeneralTab(merged), "FileInfo.ico", tr("General"));
    add(createCommentsTab(merged), "FileComments.ico", tr("Comments"));
    add(createMediaInfoTab(merged), "MediaInfo.ico", tr("Media Info"));
    add(createEd2kLinkTab(merged), "eD2kLink.ico", tr("ED2K Link"));

    // The pages a single file has in between are missing here
    int index = 0;
    switch (tabToSelect) {
    case Comments:  index = 1; break;
    case MediaInfo: index = 2; break;
    case Ed2kLink:  index = 3; break;
    default:        break;
    }
    m_tabs->setCurrentIndex(index);
    contentLayout()->addWidget(m_tabs);
    fitToContent();
}

// ── re-target ──────────────────────────────────────────────────────────

void FileDetailDialog::setDetails(const QCborMap& details)
{
    const int keepTab = m_tabs ? m_tabs->currentIndex() : m_pendingTab;

    // Null the retained members *before* the delete: a GetEd2kLink reply for the
    // previous file can still be queued and would land in applyEd2kLink().
    m_fileNamesTree       = nullptr;
    m_fileNamesEmptyLabel = nullptr;
    m_commentsPanel       = nullptr;
    m_linkEdit            = nullptr;
    m_chkHashset          = nullptr;
    m_chkHostname         = nullptr;
    m_chkHtml             = nullptr;
    m_chkMediaInfo        = nullptr;
    m_archive             = nullptr;
    m_ed3kMeta            = nullptr;
    delete m_tabs;
    m_tabs = nullptr;

    setSubjectKey(str(details, QLatin1StringView("hash")));
    setWindowTitle(tr("File Details: %1")
        .arg(str(details, QLatin1StringView("fileName"))));

    buildTabs(details, keepTab);
}

void FileDetailDialog::buildTabs(const QCborMap& details, int tabToSelect)
{
    m_tabs = new QTabWidget;

    // was: an ed3k download showed its General page only
    if (str(details, QLatin1StringView("network")) == QLatin1String("ed3k")) {
        buildEd3kTabs(details, tabToSelect);
        m_tabIsEnum = false;
        return;
    }
    m_tabIsEnum = false;

    if (thePrefs.useOriginalIcons()) {
        m_tabs->addTab(createGeneralTab(details),    skinIcon(QStringLiteral(":/icons/FileInfo.ico")),     tr("General"));
        m_tabs->addTab(createFileNamesTab(details),   skinIcon(QStringLiteral(":/icons/FileRename.ico")),   tr("File Names"));
        m_tabs->addTab(createCommentsTab(details),    skinIcon(QStringLiteral(":/icons/FileComments.ico")), tr("Comments"));
        m_tabs->addTab(createMediaInfoTab(details),   skinIcon(QStringLiteral(":/icons/MediaInfo.ico")),    tr("Media Info"));
        m_tabs->addTab(createMetadataTab(details),    skinIcon(QStringLiteral(":/icons/MetaData.ico")),     tr("Metadata"));
        m_tabs->addTab(createEd2kLinkTab(details),    skinIcon(QStringLiteral(":/icons/eD2kLink.ico")),     tr("ED2K Link"));
    } else {
        m_tabs->addTab(createGeneralTab(details),    tr("General"));
        m_tabs->addTab(createFileNamesTab(details),   tr("File Names"));
        m_tabs->addTab(createCommentsTab(details),    tr("Comments"));
        m_tabs->addTab(createMediaInfoTab(details),   tr("Media Info"));
        m_tabs->addTab(createMetadataTab(details),    tr("Metadata"));
        m_tabs->addTab(createEd2kLinkTab(details),    tr("ED2K Link"));
    }

    // Archive Preview tab — only for archive/ISO file types
    const QString fileType = str(details, QLatin1StringView("fileType"));
    if (fileType.compare(QLatin1StringView("Arc"), Qt::CaseInsensitive) == 0   // the eD2K token
        || fileType.compare(QLatin1StringView("Archive"), Qt::CaseInsensitive) == 0
        || fileType.compare(QLatin1StringView("Iso"), Qt::CaseInsensitive) == 0
        || fileType.compare(QLatin1StringView("CDImage"), Qt::CaseInsensitive) == 0) {
        if (thePrefs.useOriginalIcons())
            m_tabs->addTab(createArchivePreviewTab(details), skinIcon(QStringLiteral(":/icons/ArchivePreview.ico")), tr("Archive Preview"));
        else
            m_tabs->addTab(createArchivePreviewTab(details), tr("Archive Preview"));
    }

    // Clamp, not wrap: navigating from an archive (7 tabs) onto a plain file
    // (6 tabs) must not leave the tab widget on an out-of-range index.
    m_tabs->setCurrentIndex(std::clamp(tabToSelect, 0, m_tabs->count() - 1));
    contentLayout()->addWidget(m_tabs);

    fitToContent();
}

// ── General tab ────────────────────────────────────────────────────────

QWidget* FileDetailDialog::createGeneralTab(const QCborMap& d)
{
    // Downloads get MFC's grouped page; a shared file keeps this flat one
    // (MFC's shared file sheet has no General page at all)
    const bool download = d.contains(QLatin1StringView("multiCount"))
        ? d.value(QLatin1StringView("isDownload")).toBool()
        : d.contains(QLatin1StringView("downTransferred"));
    if (download)
        return createDownloadGeneralTab(d);

    // Scrolls once the user shrinks the dialog below the form (issue #181)
    auto* page = new QWidget;
    auto* form = new QFormLayout(page);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    DialogSizing::enableHeightForWidth(page);
    auto* area = new ContentScrollArea;
    area->setWidget(page);

    auto addRow = [form](const QString& label, const QString& value) {
        addDetailRow(form, label, value);
    };

    // Several files: what adds up is summed, the rest reads "-"
    // (MFC FileDetailDialogInfo.cpp:228-327)
    if (d.contains(QLatin1StringView("multiCount"))) {
        const QString na = QStringLiteral("-");
        const qint64 size = num(d, QLatin1StringView("fileSize"));
        addRow(tr("File Name"), na);
        addRow(tr("Hash (MD4)"), na);
        addRow(tr("File Size"), tr("%1 (%2 Bytes)").arg(formatByteSize(size), QLocale().toString(size)));
        addRow(tr("Completed"), QStringLiteral("%1 (%2%)")
            .arg(formatByteSize(num(d, QLatin1StringView("completedSize"))),
                 formatPercent(d.value(QLatin1StringView("percentCompleted")).toDouble())));
        addRow(tr("Transferred"), formatByteSize(num(d, QLatin1StringView("downTransferred"))));
        addRow(tr("Status"), na);
        addRow(tr("Priority"), na);
        addRow(tr("Sources"), QStringLiteral("%1 (%2 transferring, %3 A4AF)")
            .arg(num(d, QLatin1StringView("sourceCount")))
            .arg(num(d, QLatin1StringView("transferringSrcCount")))
            .arg(num(d, QLatin1StringView("a4afSourceCount"))));
        addRow(tr("Created"), na);
        addRow(tr("Last Seen Complete"), na);
        addRow(tr("Last Reception"), na);
        form->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));
        return area;
    }

    addRow(tr("File Name"), str(d, QLatin1StringView("fileName")));

    // Unfinished download: the .part (the content), not the .part.met MFC shows
    // (FileDetailDialogInfo.cpp:118-123) — issue #56. dataPath is the .part for a
    // download, the file itself otherwise.
    QString filePath = str(d, QLatin1StringView("dataPath"));
    if (filePath.isEmpty())
        filePath = str(d, QLatin1StringView("filePath"));
    if (!filePath.isEmpty())
        addRow(tr("File Path"), filePath);

    // ed3k-only: hashed while eD2K was disabled, `hash` is no MD4
    const bool ed3kOnly = d.value(QLatin1StringView("ed3kOnly")).toBool();
    addRow(tr("Hash (MD4)"), ed3kOnly ? tr("None (hashed while eD2K was disabled)") : str(d, QLatin1StringView("hash")));

    const QString aich = str(d, QLatin1StringView("aichHash"));
    if (!aich.isEmpty() && !ed3kOnly)
        addRow(tr("AICH Hash"), aich);

    // A file shared on ed3k too: its BLAKE3 root (hashed here) and what is published
    if (d.value(QLatin1StringView("ed3kEnabled")).toBool() || ed3kOnly) {
        const QString root = str(d, QLatin1StringView("ed3kRoot"));
        addRow(tr("BLAKE3 Hash"), root.isEmpty() ? tr("Not hashed yet") : root);
        QStringList published;
        if (d.value(QLatin1StringView("ed3kAliasPublished")).toBool())
            published << tr("eD2K alias");
        if (d.value(QLatin1StringView("ed3kKeywordsPublished")).toBool())
            published << tr("Keywords");
        addRow(tr("Published on ed3k"), published.isEmpty() ? tr("No") : published.join(QStringLiteral(", ")));
        if (const QString key = str(d, QLatin1StringView("ed3kKey")); !key.isEmpty())
            addRow(tr("Key"), ed3kKeyText(key));
        // every network, joined or not, and why one was left out (hops.md §1)
        if (const QString joins = ed3kJoinsText(d.value(QLatin1StringView("ed3kJoins")).toArray()); !joins.isEmpty())
            addRow(tr("Joins"), joins);
    }

    const qint64 fileSize = num(d, QLatin1StringView("fileSize"));
    // MFC FileDetailDialogInfo.cpp:280 — "734.53 MB  (770,215,936 Bytes)"
    addRow(tr("File Size"), tr("%1 (%2 Bytes)")
        .arg(formatByteSize(fileSize), QLocale().toString(fileSize)));

    const qint64 completed = num(d, QLatin1StringView("completedSize"));
    const double pct = d.value(QLatin1StringView("percentCompleted")).toDouble();
    addRow(tr("Completed"), QStringLiteral("%1 (%2%)")
        .arg(formatByteSize(completed), formatPercent(pct)));

    // Downloads only: bytes received, which repeats exceed "Completed" (MFC GetTransferred)
    if (d.contains(QLatin1StringView("downTransferred")))
        addRow(tr("Transferred"), formatByteSize(num(d, QLatin1StringView("downTransferred"))));
    addRow(tr("Status"),   str(d, QLatin1StringView("status")));
    addRow(tr("Priority"), str(d, QLatin1StringView("downPriority")));

    const qint64 src  = num(d, QLatin1StringView("sourceCount"));
    const qint64 xfer = num(d, QLatin1StringView("transferringSrcCount"));
    const qint64 a4af = num(d, QLatin1StringView("a4afSourceCount"));
    addRow(tr("Sources"),
        QStringLiteral("%1 (%2 transferring, %3 A4AF)").arg(src).arg(xfer).arg(a4af));

    addRow(tr("Created"),           fmtTime(num(d, QLatin1StringView("addedOn"))));
    addRow(tr("Last Seen Complete"), fmtTime(num(d, QLatin1StringView("lastSeenComplete"))));
    addRow(tr("Last Reception"),     fmtTime(num(d, QLatin1StringView("lastReception"))));

    form->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));
    return area;
}

// MFC FileDetailDialogInfo: General / Transfer / Dates. Several files: per-file
// rows read "-", the rest add up (ibid. :228-327). Rows only for the advanced
// mode are behind "Show more controls".
QWidget* FileDetailDialog::createDownloadGeneralTab(const QCborMap& d)
{
    // Scrolls only where the screen is too short for all three boxes (issue #36)
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    DialogSizing::enableHeightForWidth(page);
    auto* area = new ContentScrollArea;
    area->setFrameShape(QFrame::NoFrame);
    area->setWidget(page);

    const bool multi = d.contains(QLatin1StringView("multiCount"));
    const bool ext = thePrefs.showExtControls();
    const QString na = QStringLiteral("-");
    const auto value = [&](const QString& single) { return multi ? na : single; };
    const auto flag = [&](const char* key) { return d.value(QLatin1StringView(key)).toBool(); };
    const auto count = [&](const char* key) { return num(d, QLatin1StringView(key)); };

    const qint64 fileSize   = count("fileSize");
    const qint64 completed  = count("completedSize");
    const qint64 xferred    = count("downTransferred");
    const QString fileName  = str(d, QLatin1StringView("fileName"));

    // ── General
    DetailGroup general(tr("General"), layout);
    general.wide(tr("Name"), value(fileName));
    // The .part, not MFC's .part.met — issue #56
    QString filePath = str(d, QLatin1StringView("dataPath"));
    if (filePath.isEmpty())
        filePath = str(d, QLatin1StringView("filePath"));
    general.wide(tr("File Path"), value(filePath));
    // an ed3k download (network "ed3k"): hash = its BLAKE3 root, no AICH or eD2K hashset
    const bool ed3kRow = str(d, QLatin1StringView("network")) == QLatin1String("ed3k");
    general.wide(tr("Hash"), value(str(d, QLatin1StringView("hash"))));
    const QString aich = str(d, QLatin1StringView("aichHash"));
    if (!ed3kRow)
        general.wide(tr("AICH Hash"), value(aich.isEmpty() ? tr("Unknown") : aich));

    general.pair(tr("Size"), tr("%1 (%2 Bytes); Size on disk: %3")
        .arg(formatByteSize(fileSize), QLocale().toString(fileSize),
             formatByteSize(count("sizeOnDisk"))));

    // Type: what the first bytes are, against what the extension says (ibid. :196-227)
    QString type = na;
    bool typeWarning = false;
    if (!multi) {
        const QString suffix = QFileInfo(fileName).suffix().toUpper();
        const QString actual = str(d, QLatin1StringView("containerActual"));
        switch (count("containerVerdict")) {
        case 1:   // Matches
            type = tr("%1 (Verified)").arg(actual.isEmpty() ? suffix : actual);
            break;
        case 2:   // WrongContainer
            type = tr("%1 (Verified)").arg(actual) + QStringLiteral(" - ")
                 + tr("Invalid file extension: %1").arg(suffix);
            typeWarning = true;
            break;
        case 3:   // NoKnownContainer
            type = tr("%1 (Unverified)").arg(suffix) + QStringLiteral(" - ")
                 + tr("Invalid file extension: %1").arg(suffix);
            typeWarning = true;
            break;
        default:
            type = suffix.isEmpty() ? tr("Unknown") : tr("%1 (Unverified)").arg(suffix);
            break;
        }
    }
    QLabel* typeLabel = general.pair(tr("Type"), type);
    if (typeWarning) {
        QPalette pal = typeLabel->palette();
        pal.setColor(QPalette::WindowText, QColor(Qt::red));
        typeLabel->setPalette(pal);
    }

    if (ext) {
        const qint64 parts = count("partCount");
        const qint64 avail = count("availablePartCount");
        general.pair(tr("Parts"), value(tr("%1; available: %2 (%3%)")
            .arg(parts).arg(avail)
            .arg(QString::number(parts > 0 ? 100.0 * static_cast<double>(avail) / static_cast<double>(parts) : 0.0, 'f', 1))));
    }

    const qint64 transferring = count("transferringSrcCount");
    const QString status = transferring > 0
        ? tr("Downloading (from %n source(s))", nullptr, static_cast<int>(transferring))
        : str(d, QLatin1StringView("status"));
    if (ext && !ed3kRow) {
        // ibid. :283-298 — several files: "Yes" only when all have both
        QString hashset;
        if (multi) {
            const qint64 md4 = count("md4HashsetAvailableCount");
            const qint64 aichSets = count("aichHashsetAvailableCount");
            const qint64 files = count("multiCount");
            if (md4 == 0 && aichSets == 0)
                hashset = tr("No");
            else if (md4 == files && aichSets == files)
                hashset = tr("Yes") + QStringLiteral(" (eD2K + AICH)");
        } else {
            const bool md4 = flag("md4HashsetAvailable");
            const bool aichSet = flag("aichHashsetAvailable");
            if (md4 && aichSet)
                hashset = tr("Yes") + QStringLiteral(" (eD2K + AICH)");
            else if (aichSet)
                hashset = tr("Yes") + QStringLiteral(" (AICH)");
            else if (md4)
                hashset = tr("Yes") + QStringLiteral(" (eD2K)");
            else
                hashset = tr("No");
        }
        general.pair(tr("Status"), value(status), tr("Hashset available"), hashset);
    } else {
        general.pair(tr("Status"), value(status));
    }

    // ── Transfer
    DetailGroup transfer(tr("Transfer"), layout);
    const qint64 sources = count("sourceCount");
    transfer.pair(tr("Sources"), ext
        ? tr("%1 (Useful: %2, NNP: %3); A4AF: %4")
              .arg(sources).arg(count("validSourceCount"))
              .arg(count("nnpSourceCount")).arg(count("a4afSourceCount"))
        : QString::number(sources));
    // Priority is not on MFC's page; kept beside the rate
    transfer.pair(tr("Datarate"), formatByteRate(count("datarate")),
                  tr("Priority"), value(str(d, QLatin1StringView("downPriority"))));
    const qint64 remaining = std::max<qint64>(0, fileSize - completed);
    if (ext) {
        transfer.pair(tr("Transferred"), formatByteSize(xferred),
                      tr("Compression"), sizeAndShare(count("compressionGain"), xferred));
        transfer.pair(tr("Completed"), sizeAndShare(completed, fileSize),
                      tr("Corrupted"), sizeAndShare(count("corruptionLoss"), xferred));
        transfer.pair(tr("Remaining"), sizeAndShare(remaining, fileSize),
                      tr("Recovered"), tr("%n Part(s)", nullptr, static_cast<int>(count("recoveredParts"))));
    } else {
        transfer.pair(tr("Transferred"), formatByteSize(xferred));
        transfer.pair(tr("Completed"), sizeAndShare(completed, fileSize));
        transfer.pair(tr("Remaining"), sizeAndShare(remaining, fileSize));
    }

    // ── Dates
    DetailGroup dates(tr("Dates"), layout);
    dates.pair(tr("Download added"), value(fmtTimeAgo(count("addedOn"), tr("Unknown"))));
    const qint64 active = count("dlActiveTime");
    dates.pair(tr("Download time"), value(active > 0 ? formatSecondsLongHM(active) : tr("Unknown")));
    dates.pair(tr("Seen complete"), value(fmtTimeAgo(count("lastSeenComplete"), tr("Never"))));
    dates.pair(tr("Last reception"), value(fmtTimeAgo(count("lastReception"), tr("Never"))));

    // other names of the root known here (alias.md): its torrent/usenet/hoster/eD2K jobs
    QStringList aliases;
    for (const auto& a : d.value(QLatin1StringView("ed3kAliases")).toArray())
        if (const QString text = ed3kAliasText(a.toMap()); !text.isEmpty())
            aliases << text;

    // ── ed3k: a plain ed3k download, listed in Transfers only (Ed3kDownloads.h)
    if (ed3kRow && !multi) {
        DetailGroup ed3k(tr("ed3k"), layout);
        ed3k.pair(tr("Key"), ed3kKeyText(str(d, QLatin1StringView("ed3kKey"))));
        // hops.md §1: no HTTP source under the anonymous key; no row when none was dropped
        if (const qint64 dropped = count("ed3kWebDropped"); dropped > 0)
            ed3k.wide(tr("HTTP sources"), ed3kWebDroppedText(int(dropped)));
        if (!aliases.isEmpty())
            ed3k.wide(tr("Also known as"), aliases.join(u'\n'));
        if (const QString joins = ed3kJoinsText(d.value(QLatin1StringView("ed3kJoins")).toArray()); !joins.isEmpty())
            ed3k.wide(tr("Joins"), joins);
    }

    // ── ed3k: the eD2K download joined from ed3k (§14.2), not on MFC's page
    if (!ed3kRow && (multi ? count("ed3kEnabledCount") > 0 : flag("ed3kEnabled"))) {
        DetailGroup ed3k(tr("ed3k"), layout);
        QString root;
        const QString hex = str(d, QLatin1StringView("ed3kRoot"));
        if (!hex.isEmpty())
            root = count("ed3kPair") == 1 ? tr("%1 (Trusted)").arg(hex) : tr("%1 (Unverified)").arg(hex);
        else if (flag("ed3kSearching"))
            root = tr("Searching...");
        else if (flag("ed3kTracked"))
            root = tr("Not found");
        else
            root = tr("Unknown");
        ed3k.wide(tr("BLAKE3 Hash"), value(root));
        ed3k.pair(tr("Sources"), tr("%1 (transferring %2)")
            .arg(count("ed3kSources")).arg(count("ed3kTransferring")));
        ed3k.pair(tr("Transferred"), sizeAndShare(count("ed3kBytes"), fileSize));
        if (!multi && !aliases.isEmpty())
            ed3k.wide(tr("Also known as"), aliases.join(u'\n'));
        if (!multi)
            if (const QString joins = ed3kJoinsText(d.value(QLatin1StringView("ed3kJoins")).toArray()); !joins.isEmpty())
                ed3k.wide(tr("Joins"), joins);
    }

    layout->addStretch(1);
    return area;
}

// ── File Names tab ─────────────────────────────────────────────────────

QWidget* FileDetailDialog::createFileNamesTab(const QCborMap& d)
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);

    auto* fileNamesTree = new ListTreeWidget;
    m_fileNamesTree = fileNamesTree;
    m_fileNamesTree->setHeaderLabels({tr("File Name"), tr("Sources")});
    m_fileNamesTree->setRootIsDecorated(false);
    m_fileNamesTree->setAlternatingRowColors(true);
    m_fileNamesTree->setSortingEnabled(true);
    // Interactive, not Stretch/ResizeToContents: Qt-owned widths can't be
    // resized by the user, so there would be nothing to remember.
    m_fileNamesTree->header()->setStretchLastSection(true);
    fileNamesTree->bindColumns(QStringLiteral("fileDetailNames"), {380, 80});

    m_fileNamesEmptyLabel = new QLabel(
        tr("No alternative file names reported by sources. "
           "Use “Search Kad” to look them up."));
    m_fileNamesEmptyLabel->setWordWrap(true);
    layout->addWidget(m_fileNamesEmptyLabel);
    layout->addWidget(m_fileNamesTree);

    populateFileNames(d);

    // MFC IDD_FILEDETAILS_NAME (emule.rc:42-52): take a source's name over into the
    // edit, tidy it, rename. Only while the file can still be renamed
    // (CFileDetailDialogName::CanRenameFile).
    const QString subjectHash = d.value(QLatin1StringView("hash")).toString();
    const QString currentName = str(d, QLatin1StringView("fileName"));
    const bool canRename = d.value(QLatin1StringView("canRename")).toBool();

    auto* takeOverBtn = new QPushButton(tr("&Take over"));
    takeOverBtn->setObjectName(QStringLiteral("takeOverName"));
    m_fileNameEdit = new QLineEdit(currentName);
    m_fileNameEdit->setObjectName(QStringLiteral("renameEdit"));
    auto* cleanupBtn = new QPushButton(tr("&Cleanup"));
    cleanupBtn->setObjectName(QStringLiteral("cleanupName"));
    auto* renameBtn = new QPushButton(tr("Rename"));
    renameBtn->setObjectName(QStringLiteral("renameFile"));
    renameBtn->setEnabled(false);

    const auto takeOver = [this] {
        if (const auto* item = m_fileNamesTree->currentItem(); item && m_fileNameEdit->isEnabled())
            m_fileNameEdit->setText(item->text(0));
    };
    const auto copyName = [this] {
        if (const auto* item = m_fileNamesTree->currentItem())
            eMule::Clipboard::setText(item->text(0));
    };
    connect(takeOverBtn, &QPushButton::clicked, this, takeOver);
    connect(m_fileNamesTree, &QTreeWidget::itemDoubleClicked, this, takeOver);
    connect(cleanupBtn, &QPushButton::clicked, this, [this] {
        m_fileNameEdit->setText(cleanupFilename(m_fileNameEdit->text(), thePrefs.filenameCleanups()));
    });
    connect(m_fileNameEdit, &QLineEdit::textChanged, renameBtn, [renameBtn, currentName](const QString& text) {
        const QString name = text.trimmed();
        renameBtn->setEnabled(!name.isEmpty() && !name.contains(u'|') && name != currentName);
    });
    const auto rename = [this, renameBtn, subjectHash] {
        if (renameBtn->isEnabled())
            emit renameFileRequested(subjectHash, m_fileNameEdit->text().trimmed());
    };
    connect(renameBtn, &QPushButton::clicked, this, rename);
    connect(m_fileNameEdit, &QLineEdit::returnPressed, this, rename);

    m_fileNamesTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_fileNamesTree, &QTreeWidget::customContextMenuRequested, this,
            [this, takeOver, copyName, canRename](const QPoint& pos) {
        const bool have = m_fileNamesTree->currentItem() != nullptr;
        QMenu menu(this);
        QAction* take = menu.addAction(tr("Take over"), this, takeOver);
        take->setEnabled(have && canRename);
        menu.setDefaultAction(take);
        menu.addAction(tr("Copy"), this, copyName)->setEnabled(have);
        menu.exec(m_fileNamesTree->viewport()->mapToGlobal(pos));
    });

    for (QWidget* w : {static_cast<QWidget*>(takeOverBtn), static_cast<QWidget*>(m_fileNameEdit),
                       static_cast<QWidget*>(cleanupBtn)})
        w->setEnabled(canRename);

    auto* takeRow = new QHBoxLayout;
    takeRow->addWidget(takeOverBtn);
    takeRow->addStretch();
    layout->addLayout(takeRow);
    auto* editRow = new QHBoxLayout;
    editRow->addWidget(m_fileNameEdit, 1);
    editRow->addWidget(cleanupBtn);
    editRow->addWidget(renameBtn);
    layout->addLayout(editRow);

    // "Search Kad" button — same Kad notes lookup as the Comments tab (a notes
    // search is the only Kad lookup that returns filenames for a file hash).
    const QString fileHash = d.value(QLatin1StringView("hash")).toString();
    const QString fileName = str(d, QLatin1StringView("fileName"));
    auto* searchKadBtn = new QPushButton(tr("Search Kad"));
    searchKadBtn->setIcon(skinIcon(QStringLiteral(":/icons/KadFileSearch.ico")));
    connect(searchKadBtn, &QPushButton::clicked, this, [this, fileHash, fileName]() {
        emit searchKadNotes(fileHash, fileName);
    });
    layout->addWidget(searchKadBtn);

    return page;
}

// ── Comments tab ───────────────────────────────────────────────────────

QWidget* FileDetailDialog::createCommentsTab(const QCborMap& d)
{
    // MFC has two comment pages: the editable IDD_COMMENT on the shared-file sheet,
    // and the read-only IDD_COMMENTLST on the download and search sheets. This port
    // has one dialog class serving all three, so the daemon's `canComment` — its own
    // sharedfiles->GetFileByID gate, the one CCommentDialog uses — picks the page.
    // A shared part file therefore gets the editor from the Downloads list too; the
    // original only offers it from Shared Files, but it publishes notes for exactly
    // the same set of files.
    if (d.value(QLatin1StringView("canComment")).toBool()) {
        auto* editor = new CommentEditPanel(QStringLiteral("fileDetailComments"));
        connect(editor, &CommentEditPanel::postComment,
                this, &DetailDialog::postFileComment);
        m_commentsPanel = editor;
    } else {
        m_commentsPanel = new CommentsPanel(QStringLiteral("fileDetailComments"));
    }

    connect(m_commentsPanel, &CommentsPanel::searchKadNotes,
            this, &DetailDialog::searchKadNotes);
    connect(m_commentsPanel, &CommentsPanel::commentFilterChanged,
            this, &DetailDialog::commentFilterChanged);
    m_commentsPanel->setDetails(d);
    return m_commentsPanel;
}

void FileDetailDialog::commentApplied(bool ok)
{
    if (auto* editor = qobject_cast<CommentEditPanel*>(m_commentsPanel))
        editor->commentApplied(ok);
}

// ── Dynamic-tab population / refresh ───────────────────────────────────

void FileDetailDialog::populateFileNames(const QCborMap& d)
{
    if (!m_fileNamesTree)
        return;

    m_fileNamesTree->clear();
    const QCborArray names = d.value(QLatin1StringView("sourceNames")).toArray();
    for (const auto& entry : names) {
        const QCborMap m = entry.toMap();
        auto* item = new QTreeWidgetItem(m_fileNamesTree);
        item->setText(0, m.value(QLatin1StringView("name")).toString());
        const int count = static_cast<int>(m.value(QLatin1StringView("count")).toInteger());
        item->setData(1, Qt::DisplayRole, count);
    }
    m_fileNamesTree->sortByColumn(1, Qt::DescendingOrder);

    if (m_fileNamesEmptyLabel)
        m_fileNamesEmptyLabel->setVisible(names.isEmpty());
}

void FileDetailDialog::applyDetails(const QCborMap& details)
{
    populateFileNames(details);
    // ed3k: the tags came with a Kad answer; the page is built whole
    if (m_ed3kMeta && details.contains(QLatin1StringView("tags"))) {
        if (auto* layout = m_ed3kMeta->layout()) {
            while (QLayoutItem* item = layout->takeAt(0)) {
                delete item->widget();
                delete item;
            }
            layout->addWidget(createMetadataTab(details));
        }
    }
    if (m_commentsPanel)
        m_commentsPanel->setDetails(details);
}

void FileDetailDialog::refreshLive(const QCborMap& details)
{
    // MFC's timer is the info page's: nothing to do while another page is on top
    if (!m_tabs || m_tabs->currentIndex() != General)
        return;
    QWidget* shown = m_tabs->widget(General);
    std::unique_ptr<QWidget> fresh(createGeneralTab(details));
    if (copyValueTexts(shown, fresh.get()))
        return;

    // Rows came or went (ext controls, ed3k): swap the page, keep the scroll position
    const auto* oldArea = qobject_cast<QAbstractScrollArea*>(shown);
    const int scroll = oldArea ? oldArea->verticalScrollBar()->value() : 0;
    const QIcon icon = m_tabs->tabIcon(General);
    const QString text = m_tabs->tabText(General);
    QWidget* page = fresh.release();
    m_tabs->removeTab(General);
    m_tabs->insertTab(General, page, icon, text);
    m_tabs->setCurrentIndex(General);
    delete shown;
    if (auto* area = qobject_cast<QAbstractScrollArea*>(page))
        area->verticalScrollBar()->setValue(scroll);
}

// ── Media Info tab ─────────────────────────────────────────────────────

QWidget* FileDetailDialog::createMediaInfoTab(const QCborMap& d)
{
    auto* page = new QWidget;
    auto* form = new QFormLayout(page);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    const QString artist  = str(d, QLatin1StringView("mediaArtist"));
    const QString album   = str(d, QLatin1StringView("mediaAlbum"));
    const QString title   = str(d, QLatin1StringView("mediaTitle"));
    const QString codec   = str(d, QLatin1StringView("mediaCodec"));
    const QString format  = str(d, QLatin1StringView("mediaFormat"));
    const QString audio   = str(d, QLatin1StringView("mediaAudioCodec"));
    const qint64  length  = num(d, QLatin1StringView("mediaLength"));
    const qint64  bitrate = num(d, QLatin1StringView("mediaBitrate"));
    const qint64  width   = num(d, QLatin1StringView("mediaWidth"));
    const qint64  height  = num(d, QLatin1StringView("mediaHeight"));
    const qint64  channels = num(d, QLatin1StringView("mediaChannels"));
    const qint64  sampleRate = num(d, QLatin1StringView("mediaSampleRate"));
    const double  fps     = d.value(QLatin1StringView("mediaFps")).toDouble();

    const bool hasAny = !artist.isEmpty() || !album.isEmpty() || !title.isEmpty()
                     || !codec.isEmpty() || !format.isEmpty() || !audio.isEmpty()
                     || length > 0 || bitrate > 0 || width > 0;

    if (!hasAny) {
        form->addRow(new QLabel(tr("No media information available.")));
        form->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));
        return page;
    }

    auto addRow = [form](const QString& label, const QString& value) {
        auto* lbl = new QLabel(value);
        lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(QStringLiteral("<b>%1:</b>").arg(label), lbl);
    };

    if (!format.isEmpty())  addRow(tr("Format"), format);
    if (!title.isEmpty())   addRow(tr("Title"), title);
    if (!artist.isEmpty())  addRow(tr("Artist"), artist);
    if (!album.isEmpty())   addRow(tr("Album"), album);

    if (length > 0) {
        const qint64 h = length / 3600;
        const int m = static_cast<int>((length % 3600) / 60);
        const int s = static_cast<int>(length % 60);
        QString text = h > 0 ? QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0'))
                                   .arg(s, 2, 10, QLatin1Char('0'))
                             : QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
        if (d.value(QLatin1StringView("mediaLengthEstimated")).toBool())
            text += QStringLiteral(" (%1)").arg(tr("estimated"));
        addRow(tr("Length"), text);
    }
    if (bitrate > 0)
        addRow(tr("Bitrate"), QStringLiteral("%1 Kbit/s").arg(bitrate));

    if (!codec.isEmpty() && codec != audio)
        addRow(tr("Codec"), codec);
    if (width > 0 && height > 0)
        addRow(tr("Resolution"), QStringLiteral("%1 x %2").arg(width).arg(height));
    if (fps > 0.0)
        addRow(tr("Frame Rate"), QStringLiteral("%1 fps").arg(fps, 0, 'f', 2));

    if (!audio.isEmpty() || channels > 0 || sampleRate > 0) {
        QStringList parts;
        if (!audio.isEmpty())
            parts << audio;
        if (channels == 1)
            parts << tr("Mono");
        else if (channels == 2)
            parts << tr("Stereo");
        else if (channels > 0)
            parts << tr("%n channels", nullptr, static_cast<int>(channels));
        if (sampleRate > 0)
            parts << QStringLiteral("%1 Hz").arg(sampleRate);
        addRow(tr("Audio"), parts.join(QStringLiteral(", ")));
    }

    form->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));
    return page;
}

// ── Metadata (ED2K Tags) tab ───────────────────────────────────────────

QWidget* FileDetailDialog::createMetadataTab(const QCborMap& d)
{
    return createMetadataPage(d, QStringLiteral("fileDetailTags"));
}

// ── ED2K Link tab ──────────────────────────────────────────────────────

QWidget* FileDetailDialog::createEd2kLinkTab(const QCborMap& d)
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);

    m_ed2kLink = str(d, QLatin1StringView("ed2kLink"));
    m_fileHash = str(d, QLatin1StringView("hash"));

    // Link display
    m_linkEdit = new QTextEdit;
    m_linkEdit->setReadOnly(true);
    m_linkEdit->setFont(QFont(QStringLiteral("monospace")));
    m_linkEdit->setPlainText(m_ed2kLink);
    layout->addWidget(m_linkEdit, 1);

    // Options group
    auto* group = new QGroupBox(tr("Link Options"));
    auto* groupLayout = new QVBoxLayout(group);

    m_chkHashset = new QCheckBox(tr("Include Hashset"));
    m_chkHostname = new QCheckBox(tr("Include Hostname"));
    m_chkHostname->setToolTip(tr("Add your hostname or public IPv6 as a source"));
    m_chkHtml = new QCheckBox(tr("HTML Format"));
    // Advanced mode only; read by the core from the file, so complete files only
    m_chkMediaInfo = new QCheckBox(tr("Include Media Info"));
    m_chkMediaInfo->setToolTip(tr("Add a line with resolution, codecs, length and bitrate under "
                                  "the HTML link of a complete file"));
    m_chkMediaInfo->setVisible(thePrefs.showExtControls());
    m_chkMediaInfo->setEnabled(false);

    groupLayout->addWidget(m_chkHashset);
    groupLayout->addWidget(m_chkHostname);
    groupLayout->addWidget(m_chkHtml);
    groupLayout->addWidget(m_chkMediaInfo);
    layout->addWidget(group);
    // One link per file is listed as it came; the options ask for a single file's link
    group->setEnabled(!m_fileHash.isEmpty());

    // Connect checkboxes
    auto updateLink = [this](Qt::CheckState) { updateEd2kLinkDisplay(); };
    connect(m_chkHashset,  &QCheckBox::checkStateChanged, this, updateLink);
    connect(m_chkHostname, &QCheckBox::checkStateChanged, this, updateLink);
    connect(m_chkHtml,     &QCheckBox::checkStateChanged, this, updateLink);
    connect(m_chkMediaInfo, &QCheckBox::checkStateChanged, this, updateLink);
    connect(m_chkHtml, &QCheckBox::toggled, m_chkMediaInfo, &QWidget::setEnabled);

    // Ask for the link once the owning panel has wired requestEd2kLink() up (it connects
    // immediately after construction, so a zero-timer lands after that but before any
    // user interaction). This also settles the Hostname checkbox's enabled state.
    QTimer::singleShot(0, this, [this] { updateEd2kLinkDisplay(); });

    // Copy button
    auto* btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    auto* copyBtn = new QPushButton(tr("Copy to Clipboard"));
    connect(copyBtn, &QPushButton::clicked, this, [this] {
        eMule::Clipboard::setText(m_linkEdit->toPlainText());
    });
    btnLayout->addWidget(copyBtn);
    layout->addLayout(btnLayout);

    return page;
}

// ── ed3k download ──────────────────────────────────────────────────────

void FileDetailDialog::buildEd3kTabs(const QCborMap& details, int tabToSelect)
{
    // was: no File Names (sources tell no names) and no Metadata (no eD2K tags); now both from
    // the KEYWORD records others publish for the root, looked up when either tab is opened
    const bool icons = thePrefs.useOriginalIcons();
    const auto add = [&](QWidget* page, const char* icon, const QString& title) {
        return icons ? m_tabs->addTab(page, skinIcon(QStringLiteral(":/icons/") + QLatin1StringView(icon)), title)
                     : m_tabs->addTab(page, title);
    };
    add(createGeneralTab(details), "FileInfo.ico", tr("General"));
    const int names = add(createFileNamesTab(details), "FileRename.ico", tr("File Names"));
    if (m_fileNamesEmptyLabel)
        m_fileNamesEmptyLabel->setText(tr("No other names found in ed3k Kad yet. Opening this tab looks them up."));
    const int comments = add(createCommentsTab(details), "FileComments.ico", tr("Comments"));
    const int media = add(createMediaInfoTab(details), "MediaInfo.ico", tr("Media Info"));
    m_ed3kMeta = new QWidget;
    auto* metaLayout = new QVBoxLayout(m_ed3kMeta);
    metaLayout->setContentsMargins(0, 0, 0, 0);
    metaLayout->addWidget(createMetadataTab(details));
    const int meta = add(m_ed3kMeta, "MetaData.ico", tr("Metadata"));
    const int link = add(createEd3kLinkTab(details), "eD2kLink.ico", tr("ed3k Link"));
    m_ed3kKadAsked = false;

    // what an archive (its first volume, or a lone one) holds, as recorded in ed3k
    const QString name = str(details, QLatin1StringView("fileName"));
    int archive = -1;
    if (ArchiveContentsView::opensArchive(name)) {
        m_archive = new ArchiveContentsView(m_tabs, false);
        m_archivePayload = QCborMap{{QStringLiteral("volumes"),
                                     QCborArray{QCborMap{{QStringLiteral("name"), name},
                                                         {QStringLiteral("root"), str(details, QLatin1StringView("hash"))}}}}};
        m_archiveFolder = QFileInfo(name).completeBaseName();
        archive = add(m_archive, "FileInfo.ico", tr("Archive"));
        attachArchive();
    }
    int preview = -1;
    const QString type = getFileTypeByName(name);
    if (type.compare(QLatin1StringView("Arc"), Qt::CaseInsensitive) == 0
        || type.compare(QLatin1StringView("Iso"), Qt::CaseInsensitive) == 0)
        preview = add(createArchivePreviewTab(details), "ArchivePreview.ico", tr("Archive Preview"));
    // no table of contents in the data here: the archive record in Kad tells (the Archive tab)
    m_archiveFound = -2;
    if (auto* panel = preview >= 0 && m_archive ? qobject_cast<ArchivePreviewPanel*>(m_tabs->widget(preview)) : nullptr) {
        const auto kadText = [](int found) {
            return found > 0 ? tr("Not in the data downloaded yet. Listed in Kad: see the Archive tab.")
                 : found == 0 ? tr("Nothing found in Kad.") : tr("Nothing found in Kad: the lookup failed.");
        };
        auto missing = std::make_shared<bool>(false);
        connect(panel, &ArchivePreviewPanel::scanFinished, this, [missing] { *missing = false; });
        connect(panel, &ArchivePreviewPanel::contentsNotFound, this, [this, panel, missing, kadText] {
            *missing = true;
            if (m_archiveFound > -2) {
                panel->setStatusText(kadText(m_archiveFound));
                return;
            }
            panel->setStatusText(tr("Looking up in Kad..."));
            if (m_archive)
                m_archive->ensureLookup();
        });
        connect(m_archive, &ArchiveContentsView::lookupFinished, panel, [this, panel, missing, kadText](int found) {
            m_archiveFound = found;
            if (*missing)
                panel->setStatusText(kadText(found));
        });
    }

    int index = tabToSelect;
    if (m_tabIsEnum) {
        switch (tabToSelect) {
        case FileNames:      index = names; break;
        case Comments:       index = comments; break;
        case MediaInfo:      index = media; break;
        case Metadata:       index = meta; break;
        case Ed2kLink:       index = link; break;
        case ArchivePreview: index = preview >= 0 ? preview : 0; break;
        default:             index = 0; break;
        }
    }
    // names and tags: one Kad lookup per file, when either tab is shown
    const QString hash = str(details, QLatin1StringView("hash"));
    const bool kad = details.value(QLatin1StringView("kadConnected")).toBool();
    const auto askKad = [this, names, meta, hash, name, kad](int tab) {
        if ((tab == names || tab == meta) && !m_ed3kKadAsked && kad) {
            m_ed3kKadAsked = true;
            emit searchKadNotes(hash, name);
        }
    };
    connect(m_tabs, &QTabWidget::currentChanged, this, [this, archive, askKad](int tab) {
        if (tab == archive && m_archive)
            m_archive->ensureLookup();
        askKad(tab);
    });
    m_tabs->setCurrentIndex(std::clamp(index, 0, m_tabs->count() - 1));
    if (m_archive && m_tabs->currentIndex() == archive)
        QTimer::singleShot(0, m_archive, &ArchiveContentsView::ensureLookup);
    QTimer::singleShot(0, this, [this, askKad] { askKad(m_tabs->currentIndex()); });
    contentLayout()->addWidget(m_tabs);
    fitToContent();
}

QWidget* FileDetailDialog::createEd3kLinkTab(const QCborMap& d)
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    auto* edit = new QTextEdit;
    edit->setReadOnly(true);
    edit->setFont(QFont(QStringLiteral("monospace")));
    edit->setPlainText(str(d, QLatin1StringView("ed3kLink")));
    layout->addWidget(edit, 1);
    auto* btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    auto* copyBtn = new QPushButton(tr("Copy to Clipboard"));
    connect(copyBtn, &QPushButton::clicked, this, [edit] { eMule::Clipboard::setText(edit->toPlainText()); });
    copyBtn->setEnabled(!edit->toPlainText().isEmpty());
    btnLayout->addWidget(copyBtn);
    layout->addLayout(btnLayout);
    return page;
}

void FileDetailDialog::setIpc(IpcClient* ipc)
{
    m_ipc = ipc;
    attachArchive();
}

void FileDetailDialog::attachArchive()
{
    if (!m_ipc || !m_archive)
        return;
    const QCborMap payload = m_archivePayload;
    ArchiveContentsIpc::attach(m_archive, m_ipc, [payload] { return std::pair{QStringLiteral("roots"), payload}; },
                               m_archiveFolder);
}

// ── Archive Preview tab ────────────────────────────────────────────────

QWidget* FileDetailDialog::createArchivePreviewTab(const QCborMap& d)
{
    auto* panel = new ArchivePreviewPanel;
    // dataPath, not fullName: that is the .part.met of a download, and absent for a shared file
    const QString dataPath = str(d, QLatin1StringView("dataPath"));
    const auto fileSize = static_cast<uint64_t>(num(d, QLatin1StringView("fileSize")));
    panel->setFile(dataPath, fileSize);
    // A download in progress: what is still missing, and the preview file
    QList<qint64> gaps;
    for (const auto& value : d.value(QLatin1StringView("archiveGaps")).toArray())
        gaps << value.toInteger();
    panel->setPartFile(gaps, d.value(QLatin1StringView("canRecoverArchive")).toBool());
    const QString fileHash = d.value(QLatin1StringView("hash")).toString();
    connect(panel, &ArchivePreviewPanel::previewFileRequested, this,
            [this, fileHash] { emit archivePreviewFileRequested(fileHash); });
    panel->setAutoScan(thePrefs.autoArchivePreviewStart());   // else: the Update button
    return panel;
}

// ── private helpers ────────────────────────────────────────────────────

void FileDetailDialog::updateEd2kLinkDisplay()
{
    // A queued signal can outlive the tab it came from — setDetails() tears the
    // whole tab set down and rebuilds it when the walker moves to another file.
    if (!m_chkHtml || !m_chkHashset || !m_chkHostname || !m_linkEdit)
        return;

    // Ask the daemon for this exact flag combination. Picking between pre-generated
    // variants used to drop the hostname whenever the hashset box was ticked, and the
    // GUI cannot build the link itself: only the core knows what may be advertised.
    if (!m_fileHash.isEmpty()) {
        emit requestEd2kLink(m_fileHash, m_chkHashset->isChecked(),
                             m_chkHostname->isChecked(), m_chkHtml->isChecked(),
                             m_chkMediaInfo && m_chkMediaInfo->isChecked());
        return;
    }

    // No hash in the details map (older core): fall back to the plain variant.
    m_linkEdit->setPlainText(m_ed2kLink);
}

void FileDetailDialog::applyEd2kLink(const QString& link, bool sourceHintAvailable)
{
    if (!m_chkHtml || !m_chkHostname || !m_linkEdit)
        return;   // the ED2K tab was torn down by a navigation

    // Plain text for the HTML variant too: MFC shows the markup, and copying it is the
    // point -- a rendered anchor showed and copied only the file name (issue #36)
    m_linkEdit->setPlainText(link);

    m_chkHostname->setEnabled(sourceHintAvailable);
    if (!sourceHintAvailable && m_chkHostname->isChecked())
        m_chkHostname->setChecked(false);   // re-triggers the request
}

// ── shared GetEd2kLink wiring ──────────────────────────────────────────

void connectEd2kLinkRequests(FileDetailDialog* dialog, IpcClient* ipc)
{
    if (!dialog || !ipc)
        return;

    // One counter per dialog: a reply that was overtaken by a later request must not
    // overwrite the newer link text.
    auto generation = std::make_shared<int>(0);
    QPointer<FileDetailDialog> dlgPtr(dialog);

    QObject::connect(dialog, &FileDetailDialog::requestEd2kLink, dialog,
        [ipc, generation, dlgPtr](const QString& hash, bool hashset,
                                  bool sourceHint, bool html, bool mediaInfo) {
            if (!ipc->isConnected())
                return;
            const int myGeneration = ++(*generation);

            Ipc::IpcMessage req(Ipc::IpcMsgType::GetEd2kLink);
            req.append(QCborArray{hash});   // the request is batched; this dialog wants one
            req.append(hashset);
            req.append(sourceHint);
            req.append(html);
            req.append(mediaInfo);
            ipc->sendRequest(std::move(req),
                [generation, myGeneration, dlgPtr](const Ipc::IpcMessage& resp) {
                    if (!dlgPtr || myGeneration != *generation)
                        return;
                    if (resp.type() != Ipc::IpcMsgType::Result || !resp.fieldBool(0))
                        return;
                    const QCborArray result = resp.fieldArray(1);
                    // Empty means the daemon no longer knows the hash — leave whatever
                    // text the dialog already shows rather than blanking it.
                    const QString link = result.at(0).toArray().at(0).toString();
                    if (!link.isEmpty())
                        dlgPtr->applyEd2kLink(link, result.at(1).toBool());
                });
        });
}

void connectDownloadDetailsRefresh(FileDetailDialog* dialog, IpcClient* ipc, const QStringList& hashes)
{
    constexpr int kRefreshMs = 5000;   // MFC FileDetailDialogInfo.cpp:88
    const auto request = [](const QString& hash) {
        Ipc::IpcMessage req(Ipc::IpcMsgType::GetDownloadDetails);
        req.append(hash);
        req.append(true);   // generalOnly
        return req;
    };
    if (!dialog || !ipc || hashes.isEmpty())
        return;
    if (hashes.size() == 1) {
        connectLiveRefresh(dialog, ipc, request, kRefreshMs);
        return;
    }

    // Several files: all of them again, merged once the last answer is in
    QPointer<FileDetailDialog> dlgPtr(dialog);
    auto* timer = new QTimer(dialog);
    auto pending = std::make_shared<bool>(false);
    QObject::connect(timer, &QTimer::timeout, dialog, [ipc, dlgPtr, pending, hashes, request] {
        if (!dlgPtr || *pending || !ipc->isConnected() || !dlgPtr->isVisible() || dlgPtr->isMinimized())
            return;
        *pending = true;
        struct Round {
            QList<QCborMap> files;
            qsizetype outstanding = 0;
        };
        auto round = std::make_shared<Round>();
        round->outstanding = hashes.size();
        for (const QString& hash : hashes) {
            ipc->sendRequest(request(hash), [dlgPtr, pending, round](const Ipc::IpcMessage& r) {
                if (r.isValid() && r.fieldBool(0))
                    round->files.append(r.field(1).toMap());
                if (--round->outstanding > 0)
                    return;
                *pending = false;
                if (dlgPtr && round->files.size() > 1)
                    dlgPtr->refreshLive(mergeFileDetails(round->files));
            });
        }
    });
    timer->start(kRefreshMs);
}

} // namespace eMule
