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
    m_windowWidth      = thePrefs.windowWidth();
    m_windowHeight     = thePrefs.windowHeight();
    m_windowMaximized  = thePrefs.windowMaximized();
}

void UiState::save()
{
    thePrefs.setServerSplitSizes(m_serverSplitSizes);
    thePrefs.setKadSplitSizes(m_kadSplitSizes);
    thePrefs.setTransferSplitSizes(m_transferSplitSizes);
    thePrefs.setWindowWidth(m_windowWidth);
    thePrefs.setWindowHeight(m_windowHeight);
    thePrefs.setWindowMaximized(m_windowMaximized);
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

} // namespace eMule
