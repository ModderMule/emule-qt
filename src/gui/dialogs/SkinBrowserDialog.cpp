#include "pch.h"
/// @file SkinBrowserDialog.cpp
/// @brief Browse Skins window — see SkinBrowserDialog.h.

#include "dialogs/SkinBrowserDialog.h"

#include "app/SkinEngine.h"
#include "app/SkinIcons.h"
#include "app/SkinInstaller.h"
#include "app/UiState.h"
#include "app/VersionChecker.h"
#include "net/HttpDefaults.h"   // src/core
#include "utils/StringUtils.h"

#include <QCryptographicHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QResizeEvent>
#include <QSplitter>
#include <QUrl>
#include <QVBoxLayout>

namespace eMule {

namespace {

constexpr qint64 kMaxPreviewBytes = 8LL * 1024 * 1024;   // as scripts/publish-skin.py
constexpr int kIdRole = Qt::UserRole;

} // namespace

SkinBrowserDialog::SkinBrowserDialog(VersionChecker* checker, SkinInstaller* installer, QWidget* parent)
    : RememberedSizeDialog(parent)
    , m_checker(checker)
    , m_installer(installer)
    , m_nam(new QNetworkAccessManager(this))
{
    setWindowTitle(tr("Browse Skins"));
    setWindowIcon(skinIcon(QStringLiteral(":/icons/Display.ico")));
    resize(820, 520);

    auto* layout = new QVBoxLayout(this);
    auto* splitter = new QSplitter(Qt::Horizontal, this);

    m_list = new QListWidget(splitter);
    m_list->setMinimumWidth(180);

    auto* right = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    m_preview = new QLabel(right);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumSize(320, 180);
    m_preview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_preview->setFrameShape(QFrame::StyledPanel);
    rightLayout->addWidget(m_preview, 1);
    m_info = new QLabel(right);
    m_info->setWordWrap(true);
    m_info->setTextFormat(Qt::RichText);
    m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rightLayout->addWidget(m_info);

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({220, 600});
    layout->addWidget(splitter, 1);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    auto* buttons = new QHBoxLayout;
    m_defaultButton = new QPushButton(tr("Restore Default Skin"), this);
    buttons->addWidget(m_defaultButton);
    buttons->addStretch(1);
    m_applyButton = new QPushButton(tr("Apply"), this);
    m_applyButton->setDefault(true);
    buttons->addWidget(m_applyButton);
    auto* closeButton = new QPushButton(tr("Close"), this);
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    connect(m_list, &QListWidget::currentRowChanged, this, &SkinBrowserDialog::onSelectionChanged);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &SkinBrowserDialog::onApply);
    connect(m_applyButton, &QPushButton::clicked, this, &SkinBrowserDialog::onApply);
    connect(m_defaultButton, &QPushButton::clicked, this, [this] {
        emit restoreDefaultSkin();
        refreshStates();
    });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);

    connect(m_installer, &SkinInstaller::progress, this, [this](const QString& id, qint64 got, qint64 total) {
        if (id == m_pendingApply && total > 0)
            m_status->setText(tr("Downloading skin... %1 %").arg(got * 100 / total));
    });
    connect(m_installer, &SkinInstaller::finished, this, &SkinBrowserDialog::onInstallFinished);

    connect(m_checker, &VersionChecker::manifestReceived, this, [this](const VersionChecker::Manifest& info) {
        m_status->clear();
        setReleases(info.skins);
    });
    connect(m_checker, &VersionChecker::manifestFetchFailed, this, [this](const QString& reason) {
        if (m_releases.isEmpty())
            m_status->setText(tr("Could not load the skin list: %1").arg(reason));
    });

    if (m_checker->hasManifest()) {
        setReleases(m_checker->lastManifest().skins);
    } else {
        m_status->setText(tr("Loading the skin list..."));
        m_checker->fetchManifest();
    }
    updateButtons();
}

void SkinBrowserDialog::refreshStates()
{
    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem* item = m_list->item(row);
        if (row < m_releases.size()) {
            const QString state = stateText(m_releases[row]);
            item->setText(state.isEmpty() ? m_releases[row].name
                                          : QStringLiteral("%1 (%2)").arg(m_releases[row].name, state));
        }
    }
    onSelectionChanged();
}

void SkinBrowserDialog::resizeEvent(QResizeEvent* event)
{
    RememberedSizeDialog::resizeEvent(event);
    scalePreview();
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void SkinBrowserDialog::setReleases(const QList<SkinRelease>& releases)
{
    const QString keep = currentRelease() ? currentRelease()->id : QString{};
    m_releases = releases;
    m_list->clear();
    int select = -1;
    const QString activeId = SkinInstaller::bundleIdOf(theUiState.skinProfilePath());
    for (int row = 0; row < m_releases.size(); ++row) {
        auto* item = new QListWidgetItem(m_list);
        item->setData(kIdRole, m_releases[row].id);
        if (m_releases[row].id == keep || (keep.isEmpty() && m_releases[row].id == activeId))
            select = row;
    }
    refreshStates();
    if (m_releases.isEmpty())
        m_status->setText(tr("No skins are published yet."));
    m_list->setCurrentRow(select >= 0 ? select : (m_releases.isEmpty() ? -1 : 0));
    updateButtons();
}

void SkinBrowserDialog::onSelectionChanged()
{
    const SkinRelease* r = currentRelease();
    if (!r) {
        m_info->clear();
        m_previewPixmap = {};
        m_preview->clear();
        updateButtons();
        return;
    }
    QString html = QStringLiteral("<b>%1</b>").arg(r->name.toHtmlEscaped());
    if (!r->author.isEmpty())
        html += QStringLiteral("<br>") + tr("by %1").arg(r->author.toHtmlEscaped());
    QStringList facts;
    if (r->date.isValid())
        facts << QLocale().toString(r->date.toLocalTime().date(), QLocale::ShortFormat);
    facts << formatByteSize(r->file.size);
    if (const QString state = stateText(*r); !state.isEmpty())
        facts << state;
    html += QStringLiteral("<br>") + facts.join(QStringLiteral(" · ")).toHtmlEscaped();
    if (!r->description.isEmpty())
        html += QStringLiteral("<p>%1</p>").arg(r->description.toHtmlEscaped());
    m_info->setText(html);
    showPreview(r->id);
    updateButtons();
}

void SkinBrowserDialog::onApply()
{
    const SkinRelease* r = currentRelease();
    if (!r || !m_pendingApply.isEmpty())
        return;
    const auto inst = SkinInstaller::installed(r->id);
    if (inst && inst->sha256 == r->file.sha256) {
        emit applySkin(r->id);
        refreshStates();
        return;
    }
    m_pendingApply = r->id;
    m_status->setText(tr("Downloading skin..."));
    updateButtons();
    if (!m_installer->isBusy(r->id))
        m_installer->install(*r);
}

void SkinBrowserDialog::onInstallFinished(const QString& id, bool ok, const QString& error)
{
    if (id == m_pendingApply) {
        m_pendingApply.clear();
        if (ok) {
            m_status->clear();
            emit applySkin(id);
        } else {
            m_status->setText(tr("Could not install the skin: %1").arg(error));
        }
    }
    refreshStates();
}

void SkinBrowserDialog::showPreview(const QString& id)
{
    m_previewPixmap = {};
    // installed bundle first: newest, no download
    if (SkinInstaller::installed(id)) {
        if (const QString path = SkinEngine::previewImage(SkinInstaller::profilePath(id)); !path.isEmpty())
            m_previewPixmap = QPixmap(path);
    }
    if (m_previewPixmap.isNull())
        m_previewPixmap = m_previewCache.value(id);

    if (m_previewPixmap.isNull()) {
        const SkinRelease* r = currentRelease();
        m_preview->setPixmap({});
        if (r && !r->preview.url.isEmpty()) {
            m_preview->setText(tr("Loading preview..."));
            fetchPreview(*r);
        } else {
            m_preview->setText(tr("No preview"));
        }
        return;
    }
    scalePreview();
}

void SkinBrowserDialog::fetchPreview(const SkinRelease& release)
{
    if (m_previewFetching.contains(release.id) || release.preview.size > kMaxPreviewBytes)
        return;
    m_previewFetching.insert(release.id);
    QNetworkReply* reply = m_nam->get(Http::makeRequest(QUrl(release.preview.url)));
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply, size = release.preview.size](qint64 got, qint64) {
        if (got > size)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, id = release.id, file = release.preview] {
        reply->deleteLater();
        m_previewFetching.remove(id);
        const QByteArray data = reply->readAll();
        QPixmap pix;
        if (reply->error() == QNetworkReply::NoError && data.size() == file.size
            && QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex()) == file.sha256)
            pix.loadFromData(data);
        const SkinRelease* r = currentRelease();
        if (!pix.isNull())
            m_previewCache.insert(id, pix);
        if (r && r->id == id) {
            if (pix.isNull())
                m_preview->setText(tr("No preview"));
            else
                showPreview(id);
        }
    });
}

void SkinBrowserDialog::scalePreview()
{
    if (m_previewPixmap.isNull())
        return;
    const qreal dpr = devicePixelRatioF();
    QPixmap scaled = m_previewPixmap.scaled(m_preview->contentsRect().size() * dpr,
                                            Qt::KeepAspectRatio, Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(dpr);
    m_preview->setPixmap(scaled);
}

void SkinBrowserDialog::updateButtons()
{
    m_applyButton->setEnabled(currentRelease() && m_pendingApply.isEmpty());
    m_defaultButton->setEnabled(!theUiState.skinProfilePath().isEmpty()
                                || !theUiState.toolbarSkinPath().isEmpty());
}

const SkinRelease* SkinBrowserDialog::currentRelease() const
{
    const int row = m_list ? m_list->currentRow() : -1;
    return row >= 0 && row < m_releases.size() ? &m_releases[row] : nullptr;
}

QString SkinBrowserDialog::stateText(const SkinRelease& release) const
{
    if (release.id == m_pendingApply || m_installer->isBusy(release.id))
        return tr("downloading");
    const auto inst = SkinInstaller::installed(release.id);
    if (!inst)
        return {};
    if (SkinInstaller::bundleIdOf(theUiState.skinProfilePath()) == release.id)
        return inst->sha256 == release.file.sha256 ? tr("active") : tr("active, update available");
    return inst->sha256 == release.file.sha256 ? tr("installed") : tr("update available");
}

} // namespace eMule
