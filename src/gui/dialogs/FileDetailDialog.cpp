#include "pch.h"
/// @file FileDetailDialog.cpp
/// @brief Tabbed file-detail dialog implementation.

#include "FileDetailDialog.h"
#include "ArchivePreviewPanel.h"
#include "app/IpcClient.h"
#include "controls/AbstractListView.h"
#include "prefs/Preferences.h"
#include "utils/IpcFeedback.h"

#include <QCborArray>
#include <QCheckBox>
#include <QIcon>
#include <QClipboard>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPointer>
#include <QPushButton>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "utils/StringUtils.h"

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

/// Convert a numeric rating (0-5) to a star string.
QString ratingStars(int rating)
{
    if (rating <= 0 || rating > 5)
        return {};
    static constexpr const char* labels[] = {
        nullptr, "Poor", "Fair", "Good", "Very Good", "Excellent"
    };
    const QString stars = QString(rating, QChar(0x2605));   // ★
    return QStringLiteral("%1 (%2)").arg(stars, QLatin1StringView(labels[rating]));
}

/// Map a well-known ED2K tag nameId to a human-readable string.
QString tagNameFromId(int nameId)
{
    static const QHash<int, QString> names{
        {0x01, QStringLiteral("Filename")},
        {0x02, QStringLiteral("File Size")},
        {0x03, QStringLiteral("File Type")},
        {0x04, QStringLiteral("File Format")},
        {0x05, QStringLiteral("Last Seen Complete")},
        {0x08, QStringLiteral("Transferred")},
        {0x09, QStringLiteral("Gap Start")},
        {0x0A, QStringLiteral("Gap End")},
        {0x0B, QStringLiteral("Description")},
        {0x12, QStringLiteral("Part Filename")},
        {0x14, QStringLiteral("Status")},
        {0x15, QStringLiteral("Sources")},
        {0x16, QStringLiteral("Permissions")},
        {0x18, QStringLiteral("Download Priority")},
        {0x19, QStringLiteral("Upload Priority")},
        {0x1A, QStringLiteral("Compression")},
        {0x1B, QStringLiteral("Corrupted")},
        {0x20, QStringLiteral("Kad Last Publish Key")},
        {0x21, QStringLiteral("Kad Last Publish Src")},
        {0x22, QStringLiteral("Flags")},
        {0x23, QStringLiteral("DL Active Time")},
        {0x24, QStringLiteral("Corrupted Parts")},
        {0x25, QStringLiteral("DL Preview")},
        {0x26, QStringLiteral("Kad Last Publish Notes")},
        {0x27, QStringLiteral("AICH Hash")},
        {0x28, QStringLiteral("File Hash")},
        {0x30, QStringLiteral("Complete Sources")},
        {0x31, QStringLiteral("Collection Author")},
        {0x32, QStringLiteral("Collection Author Key")},
        {0x33, QStringLiteral("Publish Info")},
        {0x34, QStringLiteral("Last Shared")},
        {0x35, QStringLiteral("AICH Hashset")},
        {0x38, QStringLiteral("Folder Name")},
        {0x3A, QStringLiteral("File Size Hi")},
        {0x50, QStringLiteral("All-Time Transferred")},
        {0x51, QStringLiteral("All-Time Requested")},
        {0x52, QStringLiteral("All-Time Accepted")},
        {0x53, QStringLiteral("Category")},
        {0x54, QStringLiteral("All-Time Transferred Hi")},
        {0x55, QStringLiteral("Max Sources")},
        {0xD0, QStringLiteral("Media Artist")},
        {0xD1, QStringLiteral("Media Album")},
        {0xD2, QStringLiteral("Media Title")},
        {0xD3, QStringLiteral("Media Length")},
        {0xD4, QStringLiteral("Media Bitrate")},
        {0xD5, QStringLiteral("Media Codec")},
        {0xF6, QStringLiteral("File Comment")},
        {0xF7, QStringLiteral("File Rating")},
    };
    auto it = names.find(nameId);
    return it != names.end() ? *it : QStringLiteral("0x%1").arg(nameId, 2, 16, QLatin1Char('0'));
}

/// Map a TAGTYPE value to a display string.
QString tagTypeName(int type)
{
    switch (type) {
    case 0x01: return QStringLiteral("Hash");
    case 0x02: return QStringLiteral("String");
    case 0x03: return QStringLiteral("UInt32");
    case 0x04: return QStringLiteral("Float");
    case 0x05: return QStringLiteral("Bool");
    case 0x06: return QStringLiteral("Bool Array");
    case 0x07: return QStringLiteral("Blob");
    case 0x08: return QStringLiteral("UInt16");
    case 0x09: return QStringLiteral("UInt8");
    case 0x0A: return QStringLiteral("Blob Unsi");
    case 0x0B: return QStringLiteral("UInt64");
    default:   return QStringLiteral("0x%1").arg(type, 2, 16, QLatin1Char('0'));
    }
}

} // anonymous namespace

// ── constructor ────────────────────────────────────────────────────────

FileDetailDialog::FileDetailDialog(const QCborMap& details, Tab initialTab,
                                   QWidget* parent)
    : QDialog(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("File Details: %1")
        .arg(str(details, QLatin1StringView("fileName"))));
    setMinimumSize(680, 420);
    resize(720, 480);

    auto* mainLayout = new QVBoxLayout(this);
    m_tabs = new QTabWidget;

    if (thePrefs.useOriginalIcons()) {
        m_tabs->addTab(createGeneralTab(details),    QIcon(QStringLiteral(":/icons/FileInfo.ico")),     tr("General"));
        m_tabs->addTab(createFileNamesTab(details),   QIcon(QStringLiteral(":/icons/FileRename.ico")),   tr("File Names"));
        m_tabs->addTab(createCommentsTab(details),    QIcon(QStringLiteral(":/icons/FileComments.ico")), tr("Comments"));
        m_tabs->addTab(createMediaInfoTab(details),   QIcon(QStringLiteral(":/icons/MediaInfo.ico")),    tr("Media Info"));
        m_tabs->addTab(createMetadataTab(details),    QIcon(QStringLiteral(":/icons/MetaData.ico")),     tr("Metadata"));
        m_tabs->addTab(createEd2kLinkTab(details),    QIcon(QStringLiteral(":/icons/eD2kLink.ico")),     tr("ED2K Link"));
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
    if (fileType.compare(QLatin1StringView("Archive"), Qt::CaseInsensitive) == 0
        || fileType.compare(QLatin1StringView("Iso"), Qt::CaseInsensitive) == 0
        || fileType.compare(QLatin1StringView("CDImage"), Qt::CaseInsensitive) == 0) {
        if (thePrefs.useOriginalIcons())
            m_tabs->addTab(createArchivePreviewTab(details), QIcon(QStringLiteral(":/icons/ArchivePreview.ico")), tr("Archive Preview"));
        else
            m_tabs->addTab(createArchivePreviewTab(details), tr("Archive Preview"));
    }

    m_tabs->setCurrentIndex(std::min(static_cast<int>(initialTab), m_tabs->count() - 1));
    mainLayout->addWidget(m_tabs);

    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::close);
    mainLayout->addWidget(btnBox);
}

// ── General tab ────────────────────────────────────────────────────────

QWidget* FileDetailDialog::createGeneralTab(const QCborMap& d)
{
    auto* page = new QWidget;
    auto* form = new QFormLayout(page);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    auto addRow = [form](const QString& label, const QString& value) {
        auto* lbl = new QLabel(value);
        lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
        lbl->setWordWrap(true);
        form->addRow(QStringLiteral("<b>%1:</b>").arg(label), lbl);
    };

    addRow(tr("File Name"), str(d, QLatin1StringView("fileName")));
    addRow(tr("Hash (MD4)"), str(d, QLatin1StringView("hash")));

    const QString aich = str(d, QLatin1StringView("aichHash"));
    if (!aich.isEmpty())
        addRow(tr("AICH Hash"), aich);

    const qint64 fileSize = num(d, QLatin1StringView("fileSize"));
    addRow(tr("File Size"), QStringLiteral("%1 (%2 bytes)")
        .arg(formatByteSize(static_cast<uint64>(fileSize)),
             QLocale().toString(fileSize)));

    const qint64 completed = num(d, QLatin1StringView("completedSize"));
    const double pct = d.value(QLatin1StringView("percentCompleted")).toDouble();
    addRow(tr("Completed"), QStringLiteral("%1 (%2%)")
        .arg(formatByteSize(static_cast<uint64>(completed)),
             QString::number(pct, 'f', 1)));

    addRow(tr("Status"),   str(d, QLatin1StringView("status")));
    addRow(tr("Priority"), str(d, QLatin1StringView("downPriority")));

    const qint64 src  = num(d, QLatin1StringView("sourceCount"));
    const qint64 xfer = num(d, QLatin1StringView("transferringSrcCount"));
    const qint64 a4af = num(d, QLatin1StringView("a4afSourceCount"));
    addRow(tr("Sources"),
        QStringLiteral("%1 (%2 transferring, %3 A4AF)").arg(src).arg(xfer).arg(a4af));

    const QString filePath = str(d, QLatin1StringView("filePath"));
    if (!filePath.isEmpty())
        addRow(tr("File Path"), filePath);

    addRow(tr("Created"),           fmtTime(num(d, QLatin1StringView("addedOn"))));
    addRow(tr("Last Seen Complete"), fmtTime(num(d, QLatin1StringView("lastSeenComplete"))));
    addRow(tr("Last Reception"),     fmtTime(num(d, QLatin1StringView("lastReception"))));

    form->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));
    return page;
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

    // "Search Kad" button — same Kad notes lookup as the Comments tab (a notes
    // search is the only Kad lookup that returns filenames for a file hash).
    const QString fileHash = d.value(QLatin1StringView("hash")).toString();
    const QString fileName = str(d, QLatin1StringView("fileName"));
    auto* searchKadBtn = new QPushButton(tr("Search Kad"));
    searchKadBtn->setIcon(QIcon(QStringLiteral(":/icons/KadFileSearch.ico")));
    connect(searchKadBtn, &QPushButton::clicked, this, [this, fileHash, fileName]() {
        emit searchKadNotes(fileHash, fileName);
    });
    layout->addWidget(searchKadBtn);

    return page;
}

// ── Comments tab ───────────────────────────────────────────────────────

QWidget* FileDetailDialog::createCommentsTab(const QCborMap& d)
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);

    auto* commentsTree = new ListTreeWidget;
    m_commentsTree = commentsTree;
    m_commentsTree->setHeaderLabels({tr("User Name"), tr("Rating"), tr("Comment")});
    m_commentsTree->setRootIsDecorated(false);
    m_commentsTree->setAlternatingRowColors(true);
    m_commentsTree->setSortingEnabled(true);
    m_commentsTree->header()->setStretchLastSection(true);
    commentsTree->bindColumns(QStringLiteral("fileDetailComments"), {160, 80, 320});

    m_commentsEmptyLabel =
        new QLabel(tr("No comments or ratings available for this file."));
    m_commentsEmptyLabel->setWordWrap(true);
    layout->addWidget(m_commentsEmptyLabel);
    layout->addWidget(m_commentsTree);

    populateComments(d);

    // "Search Kad" button — triggers a Kad notes lookup for this file
    const QString fileHash = d.value(QLatin1StringView("hash")).toString();
    const QString fileName = str(d, QLatin1StringView("fileName"));
    auto* searchKadBtn = new QPushButton(tr("Search Kad"));
    searchKadBtn->setIcon(QIcon(QStringLiteral(":/icons/KadFileSearch.ico")));
    connect(searchKadBtn, &QPushButton::clicked, this, [this, fileHash, fileName]() {
        emit searchKadNotes(fileHash, fileName);
    });
    layout->addWidget(searchKadBtn);

    return page;
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

void FileDetailDialog::populateComments(const QCborMap& d)
{
    if (!m_commentsTree)
        return;

    m_commentsTree->clear();
    const QCborArray commentArr = d.value(QLatin1StringView("comments")).toArray();
    for (const auto& entry : commentArr) {
        const QCborMap m = entry.toMap();
        auto* item = new QTreeWidgetItem(m_commentsTree);
        item->setText(0, m.value(QLatin1StringView("userName")).toString());
        const int rating = static_cast<int>(m.value(QLatin1StringView("rating")).toInteger());
        item->setText(1, ratingStars(rating));
        item->setData(1, Qt::UserRole, rating); // for sorting
        item->setText(2, m.value(QLatin1StringView("comment")).toString());
    }

    if (m_commentsEmptyLabel)
        m_commentsEmptyLabel->setVisible(commentArr.isEmpty());
}

void FileDetailDialog::applyDetails(const QCborMap& details)
{
    populateFileNames(details);
    populateComments(details);
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
    const qint64  length  = num(d, QLatin1StringView("mediaLength"));
    const qint64  bitrate = num(d, QLatin1StringView("mediaBitrate"));

    const bool hasAny = !artist.isEmpty() || !album.isEmpty() || !title.isEmpty()
                     || !codec.isEmpty() || length > 0 || bitrate > 0;

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

    if (!title.isEmpty())   addRow(tr("Title"), title);
    if (!artist.isEmpty())  addRow(tr("Artist"), artist);
    if (!album.isEmpty())   addRow(tr("Album"), album);
    if (!codec.isEmpty())   addRow(tr("Codec"), codec);

    if (bitrate > 0)
        addRow(tr("Bitrate"), QStringLiteral("%1 kbps").arg(bitrate));

    if (length > 0) {
        const int mins = static_cast<int>(length / 60);
        const int secs = static_cast<int>(length % 60);
        addRow(tr("Length"), QStringLiteral("%1:%2")
            .arg(mins).arg(secs, 2, 10, QLatin1Char('0')));
    }

    form->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));
    return page;
}

// ── Metadata (ED2K Tags) tab ───────────────────────────────────────────

QWidget* FileDetailDialog::createMetadataTab(const QCborMap& d)
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);

    const QCborArray tagArr = d.value(QLatin1StringView("tags")).toArray();

    if (tagArr.isEmpty()) {
        layout->addWidget(new QLabel(tr("No metadata tags available.")));
        layout->addStretch();
        return page;
    }

    auto* tree = new ListTreeWidget;
    tree->setHeaderLabels({tr("Tag Name"), tr("Type"), tr("Value")});
    tree->setRootIsDecorated(false);
    tree->setAlternatingRowColors(true);
    tree->setSortingEnabled(true);
    tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree->header()->setStretchLastSection(true);
    tree->bindColumns(QStringLiteral("fileDetailTags"), {200, 100, 260});

    for (const auto& entry : tagArr) {
        const QCborMap t = entry.toMap();
        auto* item = new QTreeWidgetItem(tree);

        // Tag name: prefer string name, fall back to numeric nameId
        const QString tagName = t.value(QLatin1StringView("name")).toString();
        const qint64 nameId = t.value(QLatin1StringView("nameId")).toInteger();
        if (!tagName.isEmpty())
            item->setText(0, tagName);
        else if (nameId > 0)
            item->setText(0, tagNameFromId(static_cast<int>(nameId)));
        else
            item->setText(0, tr("Unknown"));

        // Tag type
        const int tagType = static_cast<int>(t.value(QLatin1StringView("type")).toInteger());
        item->setText(1, tagTypeName(tagType));

        // Tag value
        if (t.contains(QLatin1StringView("strValue")))
            item->setText(2, t.value(QLatin1StringView("strValue")).toString());
        else if (t.contains(QLatin1StringView("intValue")))
            item->setText(2, QString::number(t.value(QLatin1StringView("intValue")).toInteger()));
        else if (t.contains(QLatin1StringView("floatValue")))
            item->setText(2, QString::number(t.value(QLatin1StringView("floatValue")).toDouble(), 'g', 6));
        else if (t.contains(QLatin1StringView("hashValue")))
            item->setText(2, t.value(QLatin1StringView("hashValue")).toString());
    }

    tree->sortByColumn(0, Qt::AscendingOrder);
    layout->addWidget(tree);
    return page;
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

    groupLayout->addWidget(m_chkHashset);
    groupLayout->addWidget(m_chkHostname);
    groupLayout->addWidget(m_chkHtml);
    layout->addWidget(group);

    // Connect checkboxes
    auto updateLink = [this](Qt::CheckState) { updateEd2kLinkDisplay(); };
    connect(m_chkHashset,  &QCheckBox::checkStateChanged, this, updateLink);
    connect(m_chkHostname, &QCheckBox::checkStateChanged, this, updateLink);
    connect(m_chkHtml,     &QCheckBox::checkStateChanged, this, updateLink);

    // Ask for the link once the owning panel has wired requestEd2kLink() up (it connects
    // immediately after construction, so a zero-timer lands after that but before any
    // user interaction). This also settles the Hostname checkbox's enabled state.
    QTimer::singleShot(0, this, [this] { updateEd2kLinkDisplay(); });

    // Copy button
    auto* btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    auto* copyBtn = new QPushButton(tr("Copy to Clipboard"));
    connect(copyBtn, &QPushButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(m_linkEdit->toPlainText());
    });
    btnLayout->addWidget(copyBtn);
    layout->addLayout(btnLayout);

    return page;
}

// ── Archive Preview tab ────────────────────────────────────────────────

QWidget* FileDetailDialog::createArchivePreviewTab(const QCborMap& d)
{
    auto* panel = new ArchivePreviewPanel;
    const QString fullName = str(d, QLatin1StringView("fullName"));
    const auto fileSize = static_cast<uint64_t>(num(d, QLatin1StringView("fileSize")));
    panel->setFile(fullName, fileSize);
    panel->setAutoScan(true);
    return panel;
}

// ── private helpers ────────────────────────────────────────────────────

void FileDetailDialog::updateEd2kLinkDisplay()
{
    // Ask the daemon for this exact flag combination. Picking between pre-generated
    // variants used to drop the hostname whenever the hashset box was ticked, and the
    // GUI cannot build the link itself: only the core knows what may be advertised.
    if (!m_fileHash.isEmpty()) {
        emit requestEd2kLink(m_fileHash, m_chkHashset->isChecked(),
                             m_chkHostname->isChecked(), m_chkHtml->isChecked());
        return;
    }

    // No hash in the details map (older core): fall back to the plain variant.
    if (m_chkHtml->isChecked())
        m_linkEdit->setHtml(m_ed2kLink);
    else
        m_linkEdit->setPlainText(m_ed2kLink);
}

void FileDetailDialog::applyEd2kLink(const QString& link, bool sourceHintAvailable)
{
    if (m_chkHtml->isChecked())
        m_linkEdit->setHtml(link);
    else
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
                                  bool sourceHint, bool html) {
            if (!ipc->isConnected())
                return;
            const int myGeneration = ++(*generation);

            Ipc::IpcMessage req(Ipc::IpcMsgType::GetEd2kLink);
            req.append(QCborArray{hash});   // the request is batched; this dialog wants one
            req.append(hashset);
            req.append(sourceHint);
            req.append(html);
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

// ── shared SearchKadNotes wiring ───────────────────────────────────────

void connectKadNotesSearch(FileDetailDialog* dialog, IpcClient* ipc,
                           Ipc::IpcMsgType detailsRequest)
{
    if (!dialog || !ipc)
        return;

    QPointer<FileDetailDialog> dlgPtr(dialog);

    QObject::connect(dialog, &FileDetailDialog::searchKadNotes, dialog,
        [ipc, dlgPtr, detailsRequest](const QString& fileHash, const QString& fileName) {
            if (!ipc->isConnected())
                return;

            Ipc::IpcMessage kadMsg(Ipc::IpcMsgType::SearchKadNotes);
            kadMsg.append(fileHash);
            kadMsg.append(fileName);
            ipc->sendRequest(std::move(kadMsg),
                [ipc, dlgPtr, detailsRequest, fileHash](const Ipc::IpcMessage& resp) {
                    if (!dlgPtr)
                        return;
                    // Kad down, or a notes lookup for this file is already running.
                    if (!IpcFeedback::checkOrWarn(resp, dlgPtr,
                                                  FileDetailDialog::tr("Search Kad")))
                        return;

                    // Kad notes arrive asynchronously over UDP; re-fetch details a couple
                    // of times so the open dialog picks up the new File Names / Comments.
                    auto refresh = [ipc, dlgPtr, detailsRequest, fileHash]() {
                        if (!dlgPtr || !ipc->isConnected())
                            return;
                        Ipc::IpcMessage req(detailsRequest);
                        req.append(fileHash);
                        ipc->sendRequest(std::move(req), [dlgPtr](const Ipc::IpcMessage& r) {
                            if (dlgPtr && r.fieldBool(0))
                                dlgPtr->applyDetails(r.field(1).toMap());
                        });
                    };
                    QTimer::singleShot(8000, dlgPtr, refresh);
                    QTimer::singleShot(20000, dlgPtr, refresh);
                });
        });
}

} // namespace eMule
