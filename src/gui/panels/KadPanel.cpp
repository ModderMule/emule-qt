#include "pch.h"
#include "panels/KadPanel.h"

#include "panels/Ed2kKadPanel.h"
#include "panels/Ed3kKadPanel.h"
#include "app/SkinIcons.h"

#include <QTabWidget>
#include <QVBoxLayout>

namespace eMule {

KadPanel::KadPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_networkTabs = new QTabWidget(this);
    m_networkTabs->setDocumentMode(true);
    m_ed2k = new Ed2kKadPanel;
    m_ed3k = new Ed3kKadPanel;
    m_networkTabs->addTab(m_ed2k, skinIcon(QStringLiteral(":/icons/Kad.ico")), tr("eD2K"));     // Kad window icon, blue globe + donkey
    m_networkTabs->addTab(m_ed3k, skinIcon(QStringLiteral(":/icons/Ed3k.ico")), tr("ed3k"));      // red planet + donkey
    // eD2K first on every start, the ed3k tab isn't remembered
    connect(m_ed3k, &Ed3kKadPanel::editSourcesRequested, this, &KadPanel::editEd3kSourcesRequested);
    layout->addWidget(m_networkTabs);
}

KadPanel::~KadPanel() = default;

void KadPanel::setIpcClient(IpcClient* client)
{
    m_ed2k->setIpcClient(client);
    m_ed3k->setIpcClient(client);
}

void KadPanel::refreshNodesUrl()
{
    m_ed2k->refreshNodesUrl();
    m_ed3k->refreshPrefs();
}

void KadPanel::switchToSubTab(int index)
{
    const int network = index >= 2 ? 1 : 0;
    m_networkTabs->setCurrentIndex(network);
    if (network == 0)
        m_ed2k->switchToSubTab(index);
    else
        m_ed3k->switchToSubTab(index - 2);
}

} // namespace eMule
