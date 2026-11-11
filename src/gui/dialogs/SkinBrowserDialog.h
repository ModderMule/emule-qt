#pragma once

/// @file SkinBrowserDialog.h
/// @brief Browse Skins: skins of the update manifest, preview, apply, restore default.
///
/// The list comes from the last manifest of VersionChecker; none yet → it fetches one.
/// Preview: the installed bundle's preview/overview.png (or [Skin] Preview=), else the
/// manifest's preview image, else none.

#include "dialogs/RememberedSizeDialog.h"

#include "app/VersionManifest.h"   // src/core

#include <QHash>
#include <QPixmap>
#include <QSet>
#include <QString>

class QLabel;
class QListWidget;
class QNetworkAccessManager;
class QPushButton;

namespace eMule {

class SkinInstaller;
class VersionChecker;

class SkinBrowserDialog : public RememberedSizeDialog {
    Q_OBJECT

public:
    SkinBrowserDialog(VersionChecker* checker, SkinInstaller* installer, QWidget* parent = nullptr);

    /// Re-read installed/active state, after a skin was applied elsewhere.
    void refreshStates();

signals:
    /// Installed skin @p id should become the active one.
    void applySkin(const QString& id);
    void restoreDefaultSkin();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void setReleases(const QList<SkinRelease>& releases);
    void onSelectionChanged();
    void onApply();
    void onInstallFinished(const QString& id, bool ok, const QString& error);
    void showPreview(const QString& id);
    void fetchPreview(const SkinRelease& release);
    void scalePreview();
    void updateButtons();
    [[nodiscard]] const SkinRelease* currentRelease() const;
    [[nodiscard]] QString stateText(const SkinRelease& release) const;

    VersionChecker* m_checker = nullptr;
    SkinInstaller* m_installer = nullptr;
    QNetworkAccessManager* m_nam = nullptr;

    QListWidget* m_list = nullptr;
    QLabel* m_preview = nullptr;
    QLabel* m_info = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_applyButton = nullptr;
    QPushButton* m_defaultButton = nullptr;

    QList<SkinRelease> m_releases;
    QHash<QString, QPixmap> m_previewCache;   ///< id → downloaded preview
    QSet<QString> m_previewFetching;
    QPixmap m_previewPixmap;
    QString m_pendingApply;                   ///< id installing for Apply
};

} // namespace eMule
