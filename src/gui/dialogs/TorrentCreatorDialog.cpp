#include "pch.h"

/// @file TorrentCreatorDialog.cpp
/// @brief Torrent Creator (#191) — see TorrentCreatorDialog.h.

#include "dialogs/TorrentCreatorDialog.h"

#include "app/IpcClient.h"
#include "app/UiState.h"
#include "controls/CheckComboBox.h"
#include "dialogs/FolderPickerDialog.h"
#include "prefs/Preferences.h"
#include "utils/AsyncMessageBox.h"
#include "utils/DialogSizing.h"
#include "utils/IpcFeedback.h"
#include "utils/MenuUtils.h"

#include "IpcMessage.h"
#include "IpcProtocol.h"

#include <QCborArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QMessageBox>
#include <QUrl>
#include <QVBoxLayout>

namespace eMule {

namespace {

// uistate.yml torrentCreator keys
const QString kFormat = QStringLiteral("format");
const QString kPieceSize = QStringLiteral("pieceSize");
const QString kPrivate = QStringLiteral("private");
const QString kDotfiles = QStringLiteral("ignoreDotfiles");
const QString kSeed = QStringLiteral("startSeeding");
const QString kTrackers = QStringLiteral("trackers");
const QString kPublicTrackers = QStringLiteral("publicTrackers");   // checked URLs, \n-joined
const QString kWebSeeds = QStringLiteral("webSeeds");
const QString kComment = QStringLiteral("comment");
const QString kSource = QStringLiteral("source");
const QString kSaveDir = QStringLiteral("saveDir");

QString pieceLabel(int bytes)
{
    return bytes >= 1024 * 1024 ? QStringLiteral("%1 %2").arg(bytes / (1024 * 1024)).arg(QCoreApplication::translate("Units", "MB"))
                                : QStringLiteral("%1 %2").arg(bytes / 1024).arg(QCoreApplication::translate("Units", "KB"));
}

QCborArray linesOf(const QString& text, bool keepEmpty)
{
    QCborArray out;
    bool lastEmpty = true;
    for (const QString& raw : text.split(u'\n')) {
        const QString line = raw.trimmed();
        // one empty line between tiers is enough
        if (line.isEmpty() && (!keepEmpty || lastEmpty))
            continue;
        out.append(line);
        lastEmpty = line.isEmpty();
    }
    return out;
}

/// @p grow: takes the dialog's spare height; else @p lines high
QPlainTextEdit* textBox(QWidget* parent, int lines, bool grow)
{
    auto* e = new QPlainTextEdit(parent);
    e->setTabChangesFocus(true);
    e->setLineWrapMode(QPlainTextEdit::NoWrap);
    const int h = e->fontMetrics().lineSpacing() * lines + 2 * e->frameWidth() + 8;
    e->setMinimumHeight(h);
    if (!grow)
        e->setMaximumHeight(h);
    // QPlainTextEdit's size hint is 256 px high: the content fit would make a tall dialog
    e->setSizePolicy(QSizePolicy::Expanding, grow ? QSizePolicy::Expanding : QSizePolicy::Fixed);
    return e;
}

struct DropSource {
    QString path;
    QStringList onlyFiles;
};

/// One file or folder, or several files of one folder; else nothing.
std::optional<DropSource> dropSource(const QMimeData* mime)
{
    if (!mime || !mime->hasUrls() || mime->urls().isEmpty())
        return std::nullopt;
    QStringList paths;
    for (const QUrl& url : mime->urls()) {
        if (!url.isLocalFile())
            return std::nullopt;
        paths.append(QDir::cleanPath(url.toLocalFile()));   // a folder URL ends in /
    }
    if (paths.size() == 1)
        return QFileInfo::exists(paths.first()) ? std::optional(DropSource{paths.first(), {}}) : std::nullopt;
    const QString folder = QFileInfo(paths.first()).absolutePath();
    DropSource out{folder, {}};
    for (const QString& p : paths) {
        const QFileInfo fi(p);
        if (!fi.isFile() || fi.absolutePath() != folder)
            return std::nullopt;
        out.onlyFiles.append(fi.fileName());
    }
    return out;
}

} // namespace

TorrentCreatorDialog::TorrentCreatorDialog(IpcClient* ipc, const QString& path, const QStringList& onlyFiles,
                                           QWidget* parent)
    : RememberedSizeDialog(parent)
    , m_ipc(ipc)
{
    setWindowTitle(tr("Torrent Creator"));
    setWindowIcon(menuIcon("Torrent.ico"));
    auto* layout = new QVBoxLayout(this);

    // -- what to share -------------------------------------------------------
    auto* sourceBox = new QGroupBox(tr("Select file/folder to share"), this);
    auto* sourceLayout = new QVBoxLayout(sourceBox);
    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(new QLabel(tr("Path:"), sourceBox));
    m_pathEdit = new QLineEdit(sourceBox);
    m_pathEdit->setToolTip(tr("File or folder to make the torrent of, as a path on the core's disk. "
                              "A folder becomes one torrent with all its files."));
    m_pathEdit->setPlaceholderText(tr("Drop a file or folder here"));
    connect(m_pathEdit, &QLineEdit::textEdited, this, [this] { m_onlyFiles.clear(); m_selectionLabel->hide(); });
    pathRow->addWidget(m_pathEdit, 1);
    sourceLayout->addLayout(pathRow);
    m_selectionLabel = new QLabel(sourceBox);
    m_selectionLabel->setWordWrap(true);
    sourceLayout->addWidget(m_selectionLabel);
    auto* buttonRow = new QHBoxLayout;
    buttonRow->addStretch(1);
    m_fileButton = new QPushButton(tr("Select file"), sourceBox);
    // a file dialog shows this machine's disk; the core's is only browsable by folder
    m_fileButton->setEnabled(m_ipc && m_ipc->isLocalConnection());
    m_fileButton->setToolTip(m_fileButton->isEnabled()
                                 ? tr("Pick a single file.")
                                 : tr("Only with a local core. Use Select folder or type the path."));
    connect(m_fileButton, &QPushButton::clicked, this, &TorrentCreatorDialog::selectFile);
    m_folderButton = new QPushButton(tr("Select folder"), sourceBox);
    m_folderButton->setToolTip(tr("Pick a folder; every file below it goes into the torrent."));
    connect(m_folderButton, &QPushButton::clicked, this, &TorrentCreatorDialog::selectFolder);
    buttonRow->addWidget(m_fileButton);
    buttonRow->addWidget(m_folderButton);
    sourceLayout->addLayout(buttonRow);
    layout->addWidget(sourceBox);

    // -- settings ------------------------------------------------------------
    auto* settingsBox = new QGroupBox(tr("Settings"), this);
    auto* settingsLayout = new QVBoxLayout(settingsBox);
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    m_format = new QComboBox(settingsBox);
    // BitTorrent has these hashes and no other: v1 SHA-1 pieces, v2 SHA-256 merkle trees
    m_format->addItem(tr("Hybrid (v1 + v2)"), 2);
    m_format->addItem(tr("v2 (SHA-256)"), 1);
    m_format->addItem(tr("v1 (SHA-1)"), 0);
    m_format->setToolTip(tr("Hybrid torrents work with every client. v2 is not understood by older clients."));
    form->addRow(tr("Torrent format:"), m_format);
    m_pieceSize = new QComboBox(settingsBox);
    m_pieceSize->addItem(tr("Auto"), 0);
    for (int i = 4; i <= 17; ++i)   // 16 KiB .. 128 MiB, as qBittorrent
        m_pieceSize->addItem(pieceLabel(1024 << i), 1024 << i);
    m_pieceSize->setToolTip(tr("Size of the blocks the content is hashed and traded in. Auto picks one "
                               "by total size; larger pieces make a smaller .torrent file."));
    form->addRow(tr("Piece size:"), m_pieceSize);
    settingsLayout->addLayout(form);
    m_ignoreDotfiles = new QCheckBox(tr("Ignore dotfiles"), settingsBox);
    m_ignoreDotfiles->setToolTip(tr("Files and folders whose name starts with a period are left out."));
    m_private = new QCheckBox(tr("Private torrent (won't distribute on DHT network)"), settingsBox);
    m_private->setToolTip(tr("Sets the private flag: peers come from the trackers only, no DHT, "
                             "peer exchange or local discovery. Needed by private trackers."));
    m_startSeeding = new QCheckBox(tr("Start seeding immediately"), settingsBox);
    m_startSeeding->setToolTip(tr("Add the new torrent to the Torrents list and seed it from the "
                                  "selected files, without checking them again."));
    settingsLayout->addWidget(m_ignoreDotfiles);
    settingsLayout->addWidget(m_private);
    settingsLayout->addWidget(m_startSeeding);
    m_ed3kNote = new QLabel(settingsBox);
    m_ed3kNote->setWordWrap(true);
    settingsLayout->addWidget(m_ed3kNote);
    connect(m_private, &QCheckBox::toggled, this, &TorrentCreatorDialog::updateEd3kNote);
    connect(m_startSeeding, &QCheckBox::toggled, this, &TorrentCreatorDialog::updateEd3kNote);
    layout->addWidget(settingsBox);

    // -- fields --------------------------------------------------------------
    auto* fieldsBox = new QGroupBox(tr("Fields"), this);
    // a grid, not a form: the tracker row takes the spare height
    auto* fields = new QGridLayout(fieldsBox);
    fields->setColumnStretch(1, 1);
    auto addRow = [fields, fieldsBox](const QString& label, QWidget* field) {
        const int row = fields->rowCount();
        fields->addWidget(new QLabel(label, fieldsBox), row, 0, Qt::AlignRight | Qt::AlignTop);
        fields->addWidget(field, row, 1);
        return row;
    };
    m_publicTrackers = new CheckComboBox(fieldsBox);
    m_publicTrackers->setEmptyText(tr("None"));
    m_publicTrackers->setRemovable(true);
    for (const QString& url : thePrefs.btPublicTrackers())
        m_publicTrackers->addCheckItem(url);
    m_publicTrackers->setToolTip(
        tr("Open public trackers, most reliable first. Each checked one is announced to as its own tier.\n"
           "3 to 5 reliable trackers are plenty for a public torrent: more add announce traffic with "
           "little gain, as DHT and peer exchange find the rest of the swarm.\n"
           "Leave all unchecked for a private torrent; use the private tracker's URL below.\n"
           "Custom trackers of a public torrent join this list. Del or Backspace removes the "
           "highlighted entry."));
    connect(m_publicTrackers, &CheckComboBox::itemRemoved, this, [this] { storeTrackerList(); });
    m_publicTrackers->setActionItem(tr("Reset to defaults"));
    connect(m_publicTrackers, &CheckComboBox::actionTriggered, this, [this] { resetTrackerList(); });
    m_publicTrackers->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    addRow(tr("Public trackers:"), m_publicTrackers);
    m_trackers = textBox(fieldsBox, 5, true);
    m_trackers->setToolTip(tr("Your own trackers, added after the checked public ones. "
                              "One URL per line. Separate tracker tiers with an empty line."));
    fields->setRowStretch(addRow(tr("Custom trackers:"), m_trackers), 1);
    m_webSeeds = textBox(fieldsBox, 2, false);
    m_webSeeds->setToolTip(tr("HTTP(S) servers that host the same files. One URL per line."));
    addRow(tr("Web seed URLs:"), m_webSeeds);
    m_comment = textBox(fieldsBox, 2, false);
    m_comment->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_comment->setToolTip(tr("Free text stored in the .torrent; clients show it in the torrent's details."));
    addRow(tr("Comments:"), m_comment);
    m_source = new QLineEdit(fieldsBox);
    // "source" key sits inside info, so it changes the info hash (cross-seed per tracker)
    m_source->setToolTip(tr("Optional tag such as a tracker's name, stored inside the torrent's info part. "
                            "It changes the info hash, so the same files get a separate torrent per tracker. "
                            "Some private trackers require their own tag here. Leave empty if unsure."));
    addRow(tr("Source:"), m_source);
    layout->addWidget(fieldsBox, 1);

    // -- progress, buttons ---------------------------------------------------
    auto* progressRow = new QHBoxLayout;
    progressRow->addWidget(new QLabel(tr("Progress:"), this));
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    progressRow->addWidget(m_progress, 1);
    layout->addLayout(progressRow);
    m_buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_createButton = m_buttons->addButton(tr("Create Torrent"), QDialogButtonBox::AcceptRole);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &TorrentCreatorDialog::onCreate);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &TorrentCreatorDialog::reject);
    layout->addWidget(m_buttons);

    if (m_ipc) {
        connect(m_ipc, &IpcClient::torrentCreation, this, &TorrentCreatorDialog::onProgress);
        connect(m_ipc, &IpcClient::torrentCreated, this, &TorrentCreatorDialog::onCreated);
    }

    connect(m_private, &QCheckBox::toggled, this, &TorrentCreatorDialog::updatePublicTrackers);

    // text boxes would paste a dropped file's URL: the dialog takes it first
    setAcceptDrops(true);
    installEventFilter(this);
    for (QWidget* w : std::initializer_list<QWidget*>{m_pathEdit, m_source, m_trackers->viewport(), m_webSeeds->viewport(),
                                                      m_comment->viewport()})
        w->installEventFilter(this);

    loadSettings();
    setPath(path, onlyFiles);
    updateEd3kNote();
    updatePublicTrackers();
    DialogSizing::applySize(this, QSize(480, 0), QSize(560, 600));
}

TorrentCreatorDialog::~TorrentCreatorDialog()
{
    saveSettings();
}

void TorrentCreatorDialog::run(IpcClient* ipc, QWidget* parent, const QString& path, const QStringList& onlyFiles)
{
    if (!IpcFeedback::requireConnection(ipc, tr("Create Torrent")))
        return;
    auto* dlg = new TorrentCreatorDialog(ipc, path, onlyFiles, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

bool TorrentCreatorDialog::acceptsDrop(IpcClient* ipc, const QMimeData* mime)
{
    return ipc && ipc->isLocalConnection() && dropSource(mime).has_value();
}

bool TorrentCreatorDialog::runFromDrop(IpcClient* ipc, QWidget* parent, const QMimeData* mime)
{
    const auto source = acceptsDrop(ipc, mime) ? dropSource(mime) : std::nullopt;
    if (!source)
        return false;
    // not modal inside the drop: the drag source waits for it
    QTimer::singleShot(0, parent, [ipc, parent, s = *source] { run(ipc, parent, s.path, s.onlyFiles); });
    return true;
}

void TorrentCreatorDialog::reject()
{
    // Cancel stops the hashing first; a second Cancel closes
    if (m_jobId != 0 && m_ipc) {
        Ipc::IpcMessage msg(Ipc::IpcMsgType::CancelTorrentCreation);
        msg.append(qint64(m_jobId));
        m_ipc->sendRequest(std::move(msg));
        m_jobId = 0;
        setBusy(false);
        m_progress->setValue(0);
        return;
    }
    RememberedSizeDialog::reject();
}

bool TorrentCreatorDialog::eventFilter(QObject* watched, QEvent* event)
{
    const QEvent::Type type = event->type();
    if (type == QEvent::DragEnter || type == QEvent::DragMove || type == QEvent::Drop) {
        auto* drop = static_cast<QDropEvent*>(event);
        if (const auto source = takesDrops() ? dropSource(drop->mimeData()) : std::nullopt) {
            drop->setDropAction(Qt::CopyAction);
            drop->accept();
            if (type == QEvent::Drop)
                setPath(source->path, source->onlyFiles);
            return true;
        }
    }
    return RememberedSizeDialog::eventFilter(watched, event);
}

void TorrentCreatorDialog::onCreate()
{
    const QString path = QDir::fromNativeSeparators(m_pathEdit->text().trimmed());
    if (path.isEmpty()) {
        AsyncMessageBox::warning(this, tr("Torrent Creator"), tr("Select a file or folder to share first."));
        return;
    }
    if (!IpcFeedback::requireConnection(m_ipc, tr("Create Torrent")))
        return;

    // where the .torrent goes on this machine: asked before the hashing, as qBittorrent
    QString name = QFileInfo(path).fileName();
    if (name.isEmpty())
        name = QStringLiteral("new");
    QString dir = theUiState.torrentCreatorValue(kSaveDir);
    if (dir.isEmpty() || !QDir(dir).exists())
        dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    QString target = QFileDialog::getSaveFileName(this, tr("Select where to save the new torrent"),
                                                  QDir(dir).filePath(name + QStringLiteral(".torrent")),
                                                  tr("Torrent Files (*.torrent)"));
    if (target.isEmpty())
        return;
    if (!target.endsWith(QStringLiteral(".torrent"), Qt::CaseInsensitive))
        target += QStringLiteral(".torrent");
    m_savePath = target;
    theUiState.setTorrentCreatorValue(kSaveDir, QFileInfo(target).absolutePath());
    saveSettings();

    setBusy(true);
    m_progress->setValue(0);
    Ipc::IpcMessage msg(Ipc::IpcMsgType::CreateTorrent);
    msg.append(params());
    rememberCustomTrackers();
    QPointer<TorrentCreatorDialog> self(this);
    m_ipc->sendRequest(std::move(msg), [self](const Ipc::IpcMessage& resp) {
        if (!self)
            return;
        if (!IpcFeedback::checkOrWarn(resp, self, tr("Torrent Creator"))) {
            self->setBusy(false);
            return;
        }
        self->m_jobId = int(resp.fieldInt(1));
    });
}

void TorrentCreatorDialog::onProgress(const QCborMap& progress)
{
    if (m_jobId != 0 && progress.value(QStringLiteral("jobId")).toInteger() == m_jobId)
        m_progress->setValue(int(progress.value(QStringLiteral("percent")).toInteger()));
}

void TorrentCreatorDialog::onCreated(const QCborMap& result)
{
    const int jobId = int(result.value(QStringLiteral("jobId")).toInteger());
    if (m_jobId == 0 || jobId != m_jobId)
        return;
    m_jobId = 0;
    setBusy(false);
    if (result.value(QStringLiteral("cancelled")).toBool())
        return;
    if (!result.value(QStringLiteral("ok")).toBool()) {
        m_progress->setValue(0);
        AsyncMessageBox::warning(this, tr("Torrent creation failed"),
                                 tr("Reason: %1").arg(result.value(QStringLiteral("error")).toString()));
        return;
    }
    m_progress->setValue(100);
    const QString seedError = result.value(QStringLiteral("seedError")).toString();
    Ipc::IpcMessage msg(Ipc::IpcMsgType::GetCreatedTorrent);
    msg.append(qint64(jobId));
    QPointer<TorrentCreatorDialog> self(this);
    m_ipc->sendRequest(std::move(msg), [self, seedError](const Ipc::IpcMessage& resp) {
        if (!self || !IpcFeedback::checkOrWarn(resp, self, tr("Torrent Creator")))
            return;
        const QCborMap m = resp.fieldMap(1);
        self->saveTorrent(m.value(QStringLiteral("data")).toByteArray(), m.value(QStringLiteral("name")).toString());
        if (!seedError.isEmpty())
            AsyncMessageBox::warning(self, tr("Torrent Creator"), tr("The torrent was not added for seeding: %1").arg(seedError));
    });
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void TorrentCreatorDialog::setPath(const QString& path, const QStringList& onlyFiles)
{
    m_pathEdit->setText(QDir::toNativeSeparators(path));
    m_onlyFiles = onlyFiles;
    m_selectionLabel->setText(tr("%n selected file(s) of this folder, as one torrent named after the folder.", nullptr,
                                 int(onlyFiles.size())));
    m_selectionLabel->setVisible(!onlyFiles.isEmpty());
    m_progress->setValue(0);
}

void TorrentCreatorDialog::selectFile()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Select file"), m_pathEdit->text());
    if (!file.isEmpty())
        setPath(QDir::fromNativeSeparators(file), {});
}

void TorrentCreatorDialog::selectFolder()
{
    const QString dir = FolderPickerDialog::getExistingDirectory(this, m_ipc, tr("Select folder"),
                                                                 QDir::fromNativeSeparators(m_pathEdit->text()));
    if (!dir.isEmpty())
        setPath(dir, {});
}

void TorrentCreatorDialog::updateEd3kNote()
{
    // torrent.md: a seeded torrent gets BLAKE3 roots and an ed3k set. Private (§6.3): only
    // with btShareMode "all", else off ed3k and its files not shared while it seeds
    QString text;
    if (thePrefs.ed3kEnabled()) {
        const bool privateOff = m_private->isChecked() && thePrefs.btShareMode() < 2;
        if (privateOff && m_startSeeding->isChecked())
            text = tr("ed3k: a private torrent stays off ed3k, and while it seeds its files are not "
                      "shared on eD2K/ed3k (Options › BitTorrent sharing).");
        else if (privateOff)
            text = tr("ed3k: a private torrent stays off ed3k.");
        else if (m_private->isChecked() && m_startSeeding->isChecked())
            text = tr("ed3k: while seeding, the private torrent is hashed with BLAKE3 and published as an "
                      "ed3k set; its info hash becomes visible on ed3k (Options › BitTorrent: share all torrents).");
        else if (m_startSeeding->isChecked())
            text = tr("ed3k: while seeding, its files are hashed with BLAKE3 and the torrent is "
                      "published as an ed3k set, so ed3k nodes can download it too.");
        else
            text = tr("ed3k: only a seeded torrent is hashed with BLAKE3 and published as an ed3k set.");
    }
    m_ed3kNote->setText(text);
    m_ed3kNote->setVisible(!text.isEmpty());
}

/// Private: the remembered picks stay, but off; they come back when unticked.
void TorrentCreatorDialog::updatePublicTrackers()
{
    m_publicTrackers->setEnabled(m_createButton->isEnabled() && !m_private->isChecked());
}

/// A dropped path is on this machine: a remote core can't read it.
bool TorrentCreatorDialog::takesDrops() const
{
    return m_createButton->isEnabled() && (!m_ipc || m_ipc->isLocalConnection());
}

void TorrentCreatorDialog::setBusy(bool busy)
{
    for (QWidget* w : std::initializer_list<QWidget*>{m_pathEdit, m_folderButton, m_format, m_pieceSize, m_private,
                                                      m_ignoreDotfiles, m_startSeeding, m_trackers, m_webSeeds,
                                                      m_comment, m_source, m_createButton})
        w->setEnabled(!busy);
    updatePublicTrackers();
    m_fileButton->setEnabled(!busy && m_ipc && m_ipc->isLocalConnection());
    setCursor(busy ? Qt::BusyCursor : Qt::ArrowCursor);
}

void TorrentCreatorDialog::loadSettings()
{
    const auto& ui = theUiState;
    if (const int i = m_format->findData(ui.torrentCreatorValue(kFormat, QStringLiteral("2")).toInt()); i >= 0)
        m_format->setCurrentIndex(i);
    if (const int i = m_pieceSize->findData(ui.torrentCreatorValue(kPieceSize, QStringLiteral("0")).toInt()); i >= 0)
        m_pieceSize->setCurrentIndex(i);
    m_private->setChecked(ui.torrentCreatorValue(kPrivate) == QLatin1String("1"));
    m_ignoreDotfiles->setChecked(ui.torrentCreatorValue(kDotfiles, QStringLiteral("1")) == QLatin1String("1"));
    m_startSeeding->setChecked(ui.torrentCreatorValue(kSeed, QStringLiteral("1")) == QLatin1String("1"));
    // none checked until the user picks some (#212)
    m_publicTrackers->setCheckedValues(ui.torrentCreatorValue(kPublicTrackers).split(u'\n', Qt::SkipEmptyParts));
    m_trackers->setPlainText(ui.torrentCreatorValue(kTrackers));
    m_webSeeds->setPlainText(ui.torrentCreatorValue(kWebSeeds));
    m_comment->setPlainText(ui.torrentCreatorValue(kComment));
    m_source->setText(ui.torrentCreatorValue(kSource));
}

void TorrentCreatorDialog::saveSettings() const
{
    auto& ui = theUiState;
    auto flag = [](bool on) { return on ? QStringLiteral("1") : QStringLiteral("0"); };
    ui.setTorrentCreatorValue(kFormat, QString::number(m_format->currentData().toInt()));
    ui.setTorrentCreatorValue(kPieceSize, QString::number(m_pieceSize->currentData().toInt()));
    ui.setTorrentCreatorValue(kPrivate, flag(m_private->isChecked()));
    ui.setTorrentCreatorValue(kDotfiles, flag(m_ignoreDotfiles->isChecked()));
    ui.setTorrentCreatorValue(kSeed, flag(m_startSeeding->isChecked()));
    ui.setTorrentCreatorValue(kPublicTrackers, m_publicTrackers->checkedValues().join(u'\n'));
    ui.setTorrentCreatorValue(kTrackers, m_trackers->toPlainText());
    ui.setTorrentCreatorValue(kWebSeeds, m_webSeeds->toPlainText());
    ui.setTorrentCreatorValue(kComment, m_comment->toPlainText());
    ui.setTorrentCreatorValue(kSource, m_source->text());
}

void TorrentCreatorDialog::saveTorrent(const QByteArray& data, const QString& name)
{
    QSaveFile f(m_savePath);
    if (data.isEmpty() || !f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        AsyncMessageBox::warning(this, tr("Torrent creation failed"),
                                 tr("Could not write \"%1\": %2").arg(QDir::toNativeSeparators(m_savePath), f.errorString()));
        return;
    }
    AsyncMessageBox::information(this, tr("Torrent Creator"),
                                 tr("Torrent \"%1\" created:\n%2").arg(name, QDir::toNativeSeparators(m_savePath)));
}

QCborMap TorrentCreatorDialog::params() const
{
    QCborArray only;
    for (const QString& f : m_onlyFiles)
        only.append(f);
    return QCborMap{{QStringLiteral("path"), QDir::fromNativeSeparators(m_pathEdit->text().trimmed())},
                    {QStringLiteral("onlyFiles"), only},
                    {QStringLiteral("format"), m_format->currentData().toInt()},
                    {QStringLiteral("pieceSize"), m_pieceSize->currentData().toInt()},
                    {QStringLiteral("private"), m_private->isChecked()},
                    {QStringLiteral("ignoreDotfiles"), m_ignoreDotfiles->isChecked()},
                    {QStringLiteral("trackers"), linesOf(trackerText(), true)},
                    {QStringLiteral("webSeeds"), linesOf(m_webSeeds->toPlainText(), false)},
                    {QStringLiteral("comment"), m_comment->toPlainText()},
                    {QStringLiteral("source"), m_source->text().trimmed()},
                    {QStringLiteral("seed"), m_startSeeding->isChecked()}};
}

/// Checked public trackers, one tier each, then the custom box (its own tiers kept).
QString TorrentCreatorDialog::trackerText() const
{
    // never a public tracker in a private torrent
    const QStringList picked = m_private->isChecked() ? QStringList{} : m_publicTrackers->checkedValues();
    QStringList custom;
    for (const QString& raw : m_trackers->toPlainText().split(u'\n')) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || !picked.contains(line))   // a duplicate of a pick goes once
            custom.append(line);
    }
    QStringList tiers = picked;
    if (const QString rest = custom.join(u'\n').trimmed(); !rest.isEmpty())
        tiers.append(rest);
    return tiers.join(QStringLiteral("\n\n"));
}

/// Custom URLs of a public torrent become drop-down entries for the next one (#212).
void TorrentCreatorDialog::rememberCustomTrackers()
{
    if (m_private->isChecked())   // passkey URLs stay out of the shared list
        return;
    const QStringList known = m_publicTrackers->values();
    bool added = false;
    for (const QString& raw : m_trackers->toPlainText().split(u'\n')) {
        const QString url = raw.trimmed();
        const QUrl u(url);
        if (url.isEmpty() || known.contains(url) || !u.isValid() || u.host().isEmpty()
            || (u.scheme() != u"udp" && u.scheme() != u"http" && u.scheme() != u"https"))
            continue;
        m_publicTrackers->addCheckItem(url);
        added = true;
    }
    if (added)
        storeTrackerList();
}

/// Back to the shipped list; checks on surviving entries stay.
void TorrentCreatorDialog::resetTrackerList()
{
    m_publicTrackers->hidePopup();
    if (QMessageBox::question(this, tr("Reset Public Trackers"),
                              tr("Replace the public tracker list with the default trackers? "
                                 "Trackers you added are removed."),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes)
        return;
    const QStringList checked = m_publicTrackers->checkedValues();
    m_publicTrackers->clearCheckItems();
    for (const QString& url : Preferences::defaultBtPublicTrackers())
        m_publicTrackers->addCheckItem(url);
    m_publicTrackers->setCheckedValues(checked);
    storeTrackerList();
}

/// The drop-down's entries into preferences.yml (bittorrent.publicTrackers).
void TorrentCreatorDialog::storeTrackerList() const
{
    const QStringList list = m_publicTrackers->values();
    thePrefs.setBtPublicTrackers(list);
    if (!m_ipc)
        return;
    Ipc::IpcMessage req(Ipc::IpcMsgType::SetPreferences);
    req.append(QStringLiteral("btPublicTrackers"));
    QCborArray arr;
    for (const QString& u : list)
        arr.append(u);
    req.append(arr);
    m_ipc->sendRequest(std::move(req));
}

} // namespace eMule
