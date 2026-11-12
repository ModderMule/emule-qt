#pragma once

/// @file TorrentOptionsForm.h
/// @brief Speed, seeding and peer limits of torrents: the inspector's Options tab and
///        the Options… dialog for a multi-selection (#143).
///
/// Map keys as SetTorrentOptions. Filled from several torrents, a setting they do not
/// share is marked "Varies" and left out of changedOptions() until the user changes it.

#include <QCborMap>
#include <QList>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace eMule {

class TorrentOptionsForm : public QWidget {
    Q_OBJECT

public:
    /// One setting; a setting is one or two map keys.
    enum Field { FieldDown = 0, FieldUp, FieldRatio, FieldSeedTime, FieldMaxPeers, FieldI2p, FieldCount };

    explicit TorrentOptionsForm(QWidget* parent = nullptr);

    /// Fill from one torrent's options. Emits nothing, clears the edited marks.
    void setOptions(const QCborMap& options);
    /// Fill from several torrents: shared settings shown, the others marked "Varies".
    void setOptionsList(const QList<QCborMap>& list);
    /// Every setting as shown.
    [[nodiscard]] QCborMap options() const;
    /// Only the settings the user changed since the last fill.
    [[nodiscard]] QCborMap changedOptions() const;
    /// Whether @p field differs between the torrents of the last fill.
    [[nodiscard]] bool varies(Field field) const { return m_varies[field]; }
    /// Rate spins top out at the global / BitTorrent limits.
    void applyRateMaxima();
    /// True once after the user picked an I2P mode while the I2P router is off.
    [[nodiscard]] bool takeI2pRouterWarning();

    /// The keys of @p field.
    [[nodiscard]] static QList<QString> keysOf(Field field);

signals:
    /// The user changed a setting (not a fill).
    void edited();

private:
    void fill(const QCborMap& options);
    void markEdited(Field field);
    void updateEnables();
    QLabel* addVariesLabel(Field field, QWidget* parent);

    QCheckBox* m_limitDown = nullptr;
    QSpinBox* m_limitDownKb = nullptr;
    QCheckBox* m_limitUp = nullptr;
    QSpinBox* m_limitUpKb = nullptr;
    QComboBox* m_ratioMode = nullptr;
    QDoubleSpinBox* m_ratio100 = nullptr;
    QComboBox* m_seedTimeMode = nullptr;
    QSpinBox* m_seedMinutes = nullptr;
    QSpinBox* m_maxPeers = nullptr;
    QComboBox* m_i2pMode = nullptr;
    QLabel* m_variesLabel[FieldCount] = {};

    bool m_varies[FieldCount] = {};
    bool m_edited[FieldCount] = {};
    /// a fill is under way: changes are not the user's
    bool m_filling = false;
    /// from the last fill: the router is on / the session mixes (no "I2P only" then)
    bool m_i2pEnabled = true;
    bool m_i2pMixed = false;
    bool m_i2pWarn = false;
};

} // namespace eMule
