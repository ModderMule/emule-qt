/// @file UiState.cpp
/// @brief GUI layout state — implementation.

#include "app/UiState.h"

#include "prefs/Preferences.h"

namespace eMule {

UiState theUiState;

void UiState::load()
{
    m_serverSplitSizes   = thePrefs.serverSplitSizes();
    m_kadSplitSizes      = thePrefs.kadSplitSizes();
    m_transferSplitSizes = thePrefs.transferSplitSizes();
    m_sharedHorzSplitSizes = thePrefs.sharedHorzSplitSizes();
    m_sharedVertSplitSizes = thePrefs.sharedVertSplitSizes();
    m_messagesSplitSizes   = thePrefs.messagesSplitSizes();
    m_ircSplitSizes        = thePrefs.ircSplitSizes();
    m_statsSplitSizes      = thePrefs.statsSplitSizes();
    m_windowWidth      = thePrefs.windowWidth();
    m_windowHeight     = thePrefs.windowHeight();
    m_windowMaximized  = thePrefs.windowMaximized();
}

void UiState::save()
{
    thePrefs.setServerSplitSizes(m_serverSplitSizes);
    thePrefs.setKadSplitSizes(m_kadSplitSizes);
    thePrefs.setTransferSplitSizes(m_transferSplitSizes);
    thePrefs.setSharedHorzSplitSizes(m_sharedHorzSplitSizes);
    thePrefs.setSharedVertSplitSizes(m_sharedVertSplitSizes);
    thePrefs.setMessagesSplitSizes(m_messagesSplitSizes);
    thePrefs.setIrcSplitSizes(m_ircSplitSizes);
    thePrefs.setStatsSplitSizes(m_statsSplitSizes);
    thePrefs.setWindowWidth(m_windowWidth);
    thePrefs.setWindowHeight(m_windowHeight);
    thePrefs.setWindowMaximized(m_windowMaximized);

    for (auto it = m_headerStates.cbegin(); it != m_headerStates.cend(); ++it)
        thePrefs.setHeaderState(it.key(), it.value());
}

void UiState::bindServerSplitter(QSplitter* splitter)
{
    if (m_serverSplitSizes.size() == 2)
        splitter->setSizes(m_serverSplitSizes);

    QObject::connect(splitter, &QSplitter::splitterMoved, splitter, [this, splitter]() {
        m_serverSplitSizes = splitter->sizes();
    });
}

void UiState::bindKadSplitter(QSplitter* splitter)
{
    if (m_kadSplitSizes.size() == 2)
        splitter->setSizes(m_kadSplitSizes);

    QObject::connect(splitter, &QSplitter::splitterMoved, splitter, [this, splitter]() {
        m_kadSplitSizes = splitter->sizes();
    });
}

void UiState::bindTransferSplitter(QSplitter* splitter)
{
    if (m_transferSplitSizes.size() == 2)
        splitter->setSizes(m_transferSplitSizes);

    QObject::connect(splitter, &QSplitter::splitterMoved, splitter, [this, splitter]() {
        m_transferSplitSizes = splitter->sizes();
    });
}

void UiState::bindSharedHorzSplitter(QSplitter* splitter)
{
    if (m_sharedHorzSplitSizes.size() == 2)
        splitter->setSizes(m_sharedHorzSplitSizes);

    QObject::connect(splitter, &QSplitter::splitterMoved, splitter, [this, splitter]() {
        m_sharedHorzSplitSizes = splitter->sizes();
    });
}

void UiState::bindSharedVertSplitter(QSplitter* splitter)
{
    if (m_sharedVertSplitSizes.size() == 2)
        splitter->setSizes(m_sharedVertSplitSizes);

    QObject::connect(splitter, &QSplitter::splitterMoved, splitter, [this, splitter]() {
        m_sharedVertSplitSizes = splitter->sizes();
    });
}

void UiState::bindMessagesSplitter(QSplitter* splitter)
{
    if (m_messagesSplitSizes.size() == 2)
        splitter->setSizes(m_messagesSplitSizes);

    QObject::connect(splitter, &QSplitter::splitterMoved, splitter, [this, splitter]() {
        m_messagesSplitSizes = splitter->sizes();
    });
}

void UiState::bindIrcSplitter(QSplitter* splitter)
{
    if (m_ircSplitSizes.size() == 2)
        splitter->setSizes(m_ircSplitSizes);

    QObject::connect(splitter, &QSplitter::splitterMoved, splitter, [this, splitter]() {
        m_ircSplitSizes = splitter->sizes();
    });
}

void UiState::bindStatsSplitter(QSplitter* splitter)
{
    if (m_statsSplitSizes.size() == 2)
        splitter->setSizes(m_statsSplitSizes);

    QObject::connect(splitter, &QSplitter::splitterMoved, splitter, [this, splitter]() {
        m_statsSplitSizes = splitter->sizes();
    });
}

void UiState::bindMainWindow(QMainWindow* window)
{
    if (m_windowWidth > 0 && m_windowHeight > 0)
        window->resize(m_windowWidth, m_windowHeight);
}

void UiState::captureMainWindow(QMainWindow* window)
{
    m_windowMaximized = window->isMaximized();
    if (!m_windowMaximized) {
        m_windowWidth  = window->width();
        m_windowHeight = window->height();
    }
    // When maximized, keep the last saved normal size so it restores correctly.
}

void UiState::bindHeaderView(QHeaderView* header, const QString& key)
{
    // Restore saved state (overrides hardcoded defaults if available)
    QByteArray state = thePrefs.headerState(key);
    if (!state.isEmpty())
        header->restoreState(state);

    // Cache current state and auto-update on any change
    m_headerStates[key] = header->saveState();

    auto capture = [this, header, key]() {
        m_headerStates[key] = header->saveState();
    };

    QObject::connect(header, &QHeaderView::sectionResized, header, capture);
    QObject::connect(header, &QHeaderView::sectionMoved,   header, capture);
    QObject::connect(header, &QHeaderView::sortIndicatorChanged, header, capture);
}

} // namespace eMule
