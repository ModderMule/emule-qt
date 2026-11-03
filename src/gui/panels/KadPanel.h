#pragma once

/// @file KadPanel.h
/// @brief Kad tab: one page per network, eD2K (the MFC KademliaWnd, Ed2kKadPanel) and ed3k
/// (Ed3kKadPanel), as tabs above both. Each page has its own Contacts / Search Details tabs,
/// controls column and lookups list (KadPanelBase).

#include <QWidget>

class QTabWidget;

namespace eMule {

class Ed2kKadPanel;
class Ed3kKadPanel;
class IpcClient;

class KadPanel : public QWidget {
    Q_OBJECT

public:
    explicit KadPanel(QWidget* parent = nullptr);
    ~KadPanel() override;

    /// Connect both pages to the IPC client.
    void setIpcClient(IpcClient* client);

    /// The daemon's preferences landed: the nodes.dat URL field, the ed3k node list prefs.
    void refreshNodesUrl();

    /// 0 = eD2K Contacts, 1 = eD2K Search Details, 2 = ed3k Contacts, 3 = ed3k Search Details.
    void switchToSubTab(int index);

    [[nodiscard]] Ed2kKadPanel* ed2kPanel() const { return m_ed2k; }
    [[nodiscard]] Ed3kKadPanel* ed3kPanel() const { return m_ed3k; }

signals:
    /// The ed3k page wants the node list sources edited (Options, ed3k connection).
    void editEd3kSourcesRequested();

private:
    QTabWidget* m_networkTabs = nullptr;
    Ed2kKadPanel* m_ed2k = nullptr;
    Ed3kKadPanel* m_ed3k = nullptr;
};

} // namespace eMule
