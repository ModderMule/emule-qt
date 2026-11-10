#include "pch.h"
/// @file LegacyImportPage.cpp
/// @brief First start wizard page: take over settings and unfinished downloads from eMule or aMule.

#include "dialogs/LegacyImportPage.h"

#include "app/IpcClient.h"
#include "IpcMessage.h"
#include "utils/DialogSizing.h"
#include "utils/IpcFeedback.h"
#include "utils/Log.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include <chrono>

namespace eMule {

namespace {

constexpr int kKindAmule = 1;
/// A core reads its part files before it answers: generous
constexpr auto kRestartTimeout = std::chrono::seconds(120);
/// Download folders in one field
constexpr QChar kDirSeparator = u';';

QString profileText(const QCborMap& p)
{
    const QString client = p.value(QLatin1StringView("kind")).toInteger() == kKindAmule
                               ? QStringLiteral("aMule") : QStringLiteral("eMule");
    QString text = client;
    if (!p.value(QLatin1StringView("wine")).toString().isEmpty())
        text += LegacyImportPage::tr(" (Wine)");
    text += QStringLiteral(" — ") + QDir::toNativeSeparators(p.value(QLatin1StringView("dir")).toString());
    const int parts = static_cast<int>(p.value(QLatin1StringView("partFiles")).toInteger());
    if (parts > 0)
        text += u' ' + LegacyImportPage::tr("(%n unfinished download(s))", nullptr, parts);
    return text;
}

QString joinDirs(const QCborArray& dirs)
{
    QStringList out;
    for (const QCborValue& v : dirs)
        out.append(QDir::toNativeSeparators(v.toString()));
    return out.join(QString(kDirSeparator) + u' ');
}

} // namespace

LegacyImportPage::LegacyImportPage(IpcClient* ipc, QWidget* parent)
    : QWidget(parent)
    , m_ipc(ipc)
{
    DialogSizing::enableHeightForWidth(this);
    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(16, 12, 16, 4);

    auto* intro = new QLabel(
        tr("Take over the settings and unfinished downloads of eMule or aMule. The settings "
           "are only read. Unfinished downloads are moved here, so the old client no longer "
           "has them."),
        this);
    intro->setWordWrap(true);
    DialogSizing::enableHeightForWidth(intro);
    vbox->addWidget(intro);
    vbox->addSpacing(8);

    m_foundLabel = new QLabel(tr("Searching for eMule and aMule..."), this);
    m_foundLabel->setWordWrap(true);
    DialogSizing::enableHeightForWidth(m_foundLabel);
    vbox->addWidget(m_foundLabel);
    m_profileCombo = new QComboBox(this);
    m_profileCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_profileCombo->setMinimumContentsLength(20);
    m_profileCombo->hide();
    vbox->addWidget(m_profileCombo);
    vbox->addSpacing(8);

    const bool local = m_ipc && m_ipc->isLocalConnection();
    auto* grid = new QGridLayout;
    grid->setColumnStretch(1, 1);

    m_settingsCheck = new QCheckBox(
        tr("Import settings (nick, ports, limits, categories, shared folders, credits and user hash)"), this);
    m_settingsCheck->setToolTip(tr("The core restarts to load them. Files it replaces are moved "
                                   "to an import-backup folder in the config folder."));
    grid->addWidget(m_settingsCheck, 0, 0, 1, 3);
    grid->addWidget(new QLabel(tr("Settings folder:"), this), 1, 0);
    m_settingsEdit = new QLineEdit(this);
    m_settingsEdit->setPlaceholderText(tr("Folder with preferences.ini or amule.conf"));
    grid->addWidget(m_settingsEdit, 1, 1);
    m_settingsBrowse = new QPushButton(tr("Browse..."), this);
    m_settingsBrowse->setVisible(local);   // a remote core's folders are not ours to browse
    grid->addWidget(m_settingsBrowse, 1, 2);

    m_partsCheck = new QCheckBox(tr("Import unfinished downloads (moved, not copied)"), this);
    grid->addWidget(m_partsCheck, 2, 0, 1, 3);
    grid->addWidget(new QLabel(tr("Download folders:"), this), 3, 0);
    m_partsEdit = new QLineEdit(this);
    m_partsEdit->setPlaceholderText(tr("Temp folders with .part.met files, separated by ;"));
    grid->addWidget(m_partsEdit, 3, 1);
    m_partsBrowse = new QPushButton(tr("Browse..."), this);
    m_partsBrowse->setVisible(local);
    grid->addWidget(m_partsBrowse, 3, 2);
    vbox->addLayout(grid);

    vbox->addSpacing(8);
    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    DialogSizing::enableHeightForWidth(m_statusLabel);
    vbox->addWidget(m_statusLabel);
    vbox->addStretch(1);

    m_restartTimeout = new QTimer(this);
    m_restartTimeout->setSingleShot(true);

    connect(m_profileCombo, &QComboBox::currentIndexChanged, this, &LegacyImportPage::onProfileChosen);
    connect(m_settingsEdit, &QLineEdit::editingFinished, this, &LegacyImportPage::onSettingsDirEdited);
    connect(m_partsEdit, &QLineEdit::textEdited, this, [this] {
        m_partsEdited = true;
        updateEnabled();
    });
    connect(m_settingsEdit, &QLineEdit::textChanged, this, &LegacyImportPage::updateEnabled);
    connect(m_settingsCheck, &QCheckBox::toggled, this, &LegacyImportPage::updateEnabled);
    connect(m_partsCheck, &QCheckBox::toggled, this, &LegacyImportPage::updateEnabled);
    connect(m_settingsBrowse, &QPushButton::clicked, this, &LegacyImportPage::browseSettings);
    connect(m_partsBrowse, &QPushButton::clicked, this, &LegacyImportPage::browseDownloads);

    updateEnabled();
    requestProfiles();
}

void LegacyImportPage::apply(std::function<void(bool ok, bool restarted)> done)
{
    const bool settings = m_settingsCheck->isChecked();
    const bool parts = m_partsCheck->isChecked();
    if (m_done || (!settings && !parts)) {
        done(true, false);
        return;
    }
    if (!m_ipc || !m_ipc->isConnected()) {
        m_statusLabel->setText(tr("Not connected to the core."));
        done(false, false);
        return;
    }
    const QString dir = QDir::fromNativeSeparators(m_settingsEdit->text().trimmed());
    if (settings && dir.isEmpty()) {
        m_statusLabel->setText(tr("Pick the settings folder of eMule or aMule."));
        done(false, false);
        return;
    }

    QCborArray temps;
    for (const QString& d : downloadDirs())
        temps.append(d);
    Ipc::IpcMessage req(Ipc::IpcMsgType::ImportLegacyProfile);
    req.append(dir);
    req.append(settings);
    req.append(parts);
    req.append(temps);
    setBusy(true, settings ? tr("Importing... the core restarts to load the settings.")
                           : tr("Importing unfinished downloads..."));
    m_ipc->sendRequest(std::move(req), [self = QPointer<LegacyImportPage>(this), done,
                                        settings](const Ipc::IpcMessage& resp) {
        if (!self)
            return;
        // A restart may drop the link before the reply: the request was written anyway
        if (!resp.isValid() && settings) {
            self->waitForCore(done);
            return;
        }
        if (!IpcFeedback::checkOrWarn(resp, self, tr("Import from eMule"))) {
            self->setBusy(false, tr("Nothing was imported."));
            done(false, false);
            return;
        }
        const QCborMap result = resp.fieldMap(1);
        if (result.value(QLatin1StringView("restart")).toBool()) {
            self->waitForCore(done);
            return;
        }
        self->m_done = true;
        const int queued = static_cast<int>(result.value(QLatin1StringView("queued")).toInteger());
        self->setBusy(false, tr("%n unfinished download(s) queued for import. Tools > Import "
                                "Downloads shows their progress.", nullptr, queued));
        done(true, false);
    });
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void LegacyImportPage::requestProfiles()
{
    if (!m_ipc || !m_ipc->isConnected()) {
        m_foundLabel->setText(tr("Not connected to the core: type the folders below."));
        // Queued: the wizard connects after the constructor
        QMetaObject::invokeMethod(this, [this] { emit searchFinished(false); }, Qt::QueuedConnection);
        return;
    }
    m_ipc->sendRequest(Ipc::IpcMessage(Ipc::IpcMsgType::FindLegacyProfiles),
                       [self = QPointer<LegacyImportPage>(this)](const Ipc::IpcMessage& resp) {
                           if (self)
                               self->showProfiles(resp.isValid() && resp.fieldBool(0)
                                                      ? resp.fieldArray(1) : QCborArray{});
                       });
}

void LegacyImportPage::showProfiles(const QCborArray& found)
{
    m_found = found;
    if (found.isEmpty()) {
        m_foundLabel->setText(tr("No eMule or aMule installation found. To import from one "
                                 "anyway, enter its folders below."));
        emit searchFinished(false);
        return;
    }
    m_foundLabel->setText(tr("Found:"));
    {
        const QSignalBlocker block(m_profileCombo);
        for (const QCborValue& p : found)
            m_profileCombo->addItem(profileText(p.toMap()));
    }
    m_profileCombo->setVisible(found.size() > 1);
    if (found.size() == 1)
        m_foundLabel->setText(tr("Found: %1").arg(profileText(found.first().toMap())));
    // Something to take over: on by default, as the old installer did
    m_settingsCheck->setChecked(true);
    m_partsCheck->setChecked(true);
    onProfileChosen(0);
    emit searchFinished(true);
}

void LegacyImportPage::onProfileChosen(int index)
{
    if (index < 0 || index >= m_found.size())
        return;
    m_partsEdited = false;
    fillFromProfile(m_found.at(index).toMap());
}

void LegacyImportPage::onSettingsDirEdited()
{
    const QString dir = QDir::fromNativeSeparators(m_settingsEdit->text().trimmed());
    if (dir.isEmpty() || dir == m_probedDir || !m_ipc || !m_ipc->isConnected())
        return;
    m_probedDir = dir;
    Ipc::IpcMessage req(Ipc::IpcMsgType::FindLegacyProfiles);
    req.append(dir);
    m_ipc->sendRequest(std::move(req), [self = QPointer<LegacyImportPage>(this), dir](
                                           const Ipc::IpcMessage& resp) {
        if (!self || self->m_probedDir != dir)
            return;
        const QCborArray found = resp.isValid() && resp.fieldBool(0) ? resp.fieldArray(1) : QCborArray{};
        if (found.isEmpty()) {
            self->m_statusLabel->setText(tr("No preferences.ini or amule.conf in this folder."));
            return;
        }
        self->m_statusLabel->clear();
        self->fillFromProfile(found.first().toMap());
    });
}

void LegacyImportPage::browseSettings()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Settings folder of eMule or aMule"), m_settingsEdit->text());
    if (dir.isEmpty())
        return;
    m_settingsEdit->setText(QDir::toNativeSeparators(dir));
    m_settingsCheck->setChecked(true);
    onSettingsDirEdited();
}

void LegacyImportPage::browseDownloads()
{
    const QStringList current = downloadDirs();
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Download folder of eMule or aMule"), current.value(0));
    if (dir.isEmpty())
        return;
    // Picked = replaces the detected ones; add more by typing
    m_partsEdit->setText(QDir::toNativeSeparators(dir));
    m_partsEdited = true;
    m_partsCheck->setChecked(true);
    updateEnabled();
}

void LegacyImportPage::fillFromProfile(const QCborMap& profile)
{
    const QString dir = profile.value(QLatin1StringView("dir")).toString();
    m_probedDir = dir;
    m_settingsEdit->setText(QDir::toNativeSeparators(dir));
    if (!m_partsEdited)
        m_partsEdit->setText(joinDirs(profile.value(QLatin1StringView("tempDirs")).toArray()));
    updateEnabled();
}

void LegacyImportPage::updateEnabled()
{
    const bool idle = !m_busy && !m_done;   // done: Next does not import again
    m_profileCombo->setEnabled(idle);
    m_settingsCheck->setEnabled(idle);
    m_partsCheck->setEnabled(idle);
    m_settingsEdit->setEnabled(idle);
    m_settingsBrowse->setEnabled(idle);
    m_partsEdit->setEnabled(idle);
    m_partsBrowse->setEnabled(idle);
}

void LegacyImportPage::setBusy(bool busy, const QString& status)
{
    m_statusLabel->setText(status);
    if (m_busy == busy)
        return;
    m_busy = busy;
    updateEnabled();
    emit busyChanged(busy);
}

QStringList LegacyImportPage::downloadDirs() const
{
    QStringList dirs;
    for (const QString& part : m_partsEdit->text().split(kDirSeparator, Qt::SkipEmptyParts)) {
        const QString dir = QDir::fromNativeSeparators(part.trimmed());
        if (!dir.isEmpty() && !dirs.contains(dir))
            dirs.append(dir);
    }
    return dirs;
}

void LegacyImportPage::waitForCore(std::function<void(bool ok, bool restarted)> done)
{
    auto connection = std::make_shared<QMetaObject::Connection>();
    const auto finish = [self = QPointer<LegacyImportPage>(this), connection, done](bool ok) {
        QObject::disconnect(*connection);
        if (!self)
            return;
        self->m_restartTimeout->stop();
        self->m_restartTimeout->disconnect();
        if (ok) {
            self->m_done = true;
            self->setBusy(false, tr("Imported. The pages that follow show the imported settings."));
        } else {
            self->setBusy(false, tr("The core did not come back. Its log tells what happened."));
        }
        done(ok, true);
    };
    *connection = connect(m_ipc, &IpcClient::connected, this, [finish] { finish(true); });
    connect(m_restartTimeout, &QTimer::timeout, this, [finish] { finish(false); });
    m_restartTimeout->start(kRestartTimeout);
    logInfo(QStringLiteral("Import: waiting for the core to restart"));
}

} // namespace eMule
