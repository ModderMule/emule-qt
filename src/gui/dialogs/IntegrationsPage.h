#pragma once

/// @file IntegrationsPage.h
/// @brief Options > Advanced > Integrations: the Sonarr/Radarr bridge (qBittorrent
///        API, Torznab, synthetic feed from the Wanted lists) and Gluetun port sync.
///        No MFC original.

#include "prefs/ArrInstance.h"

#include <QCborMap>
#include <QList>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTreeWidget;

namespace eMule {

namespace Ipc {
class IpcMessage;
}

class IntegrationsPage : public QWidget {
    Q_OBJECT

public:
    explicit IntegrationsPage(QWidget* parent);

    /// From the GetPreferences map (arr*, gluetun*, webServerPort). Signals stay quiet.
    void load(const QCborMap& prefs);
    /// Appends the arr* / gluetun* key/value pairs to a SetPreferences request.
    void appendTo(Ipc::IpcMessage& req) const;

signals:
    void changed();

private:
    void fillTable();
    void editInstance(int row);
    void updateEnabled();
    void updateHint();

    QCheckBox* m_arrCheck = nullptr;
    QLabel* m_arrHint = nullptr;
    QSpinBox* m_waitSpin = nullptr;
    QSpinBox* m_pollSpin = nullptr;
    QTreeWidget* m_table = nullptr;
    QPushButton* m_addBtn = nullptr;
    QPushButton* m_editBtn = nullptr;
    QPushButton* m_removeBtn = nullptr;
    QList<ArrInstance> m_instances;
    int m_webPort = 4711;

    QCheckBox* m_gluetunCheck = nullptr;
    QLineEdit* m_gluetunUrlEdit = nullptr;
    QLineEdit* m_gluetunKeyEdit = nullptr;
    QComboBox* m_gluetunTargetCombo = nullptr;
};

} // namespace eMule
