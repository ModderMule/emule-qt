#include "pch.h"
/// @file UiState.cpp
/// @brief GUI layout state — reads/writes its own uistate.yml file.

#include "app/UiState.h"

#include <QSaveFile>

#include <yaml-cpp/yaml.h>

#include "utils/Log.h"

namespace eMule {

UiState theUiState;

// ---------------------------------------------------------------------------
// YAML helpers
// ---------------------------------------------------------------------------

static QList<int> readIntList(const YAML::Node& node, const char* key)
{
    QList<int> result;
    if (auto n = node[key]; n && n.IsSequence()) {
        for (const auto& item : n)
            result.append(item.as<int>(0));
    }
    return result;
}

static void writeIntList(YAML::Emitter& out, const char* key, const QList<int>& list)
{
    out << YAML::Key << key << YAML::Value << YAML::Flow << YAML::BeginSeq;
    for (int v : list)
        out << v;
    out << YAML::EndSeq;
}

// ---------------------------------------------------------------------------
// load / save
// ---------------------------------------------------------------------------

void UiState::load(const QString& configDir)
{
    const QString path = configDir + QStringLiteral("/uistate.yml");

    try {
        YAML::Node root = YAML::LoadFile(path.toStdString());
        if (!root.IsMap())
            return;

        m_serverSplitSizes     = readIntList(root, "serverSplitSizes");
        m_kadSplitSizes        = readIntList(root, "kadSplitSizes");
        m_transferSplitSizes   = readIntList(root, "transferSplitSizes");
        m_sharedHorzSplitSizes = readIntList(root, "sharedHorzSplitSizes");
        m_sharedVertSplitSizes = readIntList(root, "sharedVertSplitSizes");
        m_messagesSplitSizes   = readIntList(root, "messagesSplitSizes");
        m_ircSplitSizes        = readIntList(root, "ircSplitSizes");
        m_statsSplitSizes      = readIntList(root, "statsSplitSizes");

        m_windowWidth      = root["windowWidth"].as<int>(m_windowWidth);
        m_windowHeight     = root["windowHeight"].as<int>(m_windowHeight);
        m_windowMaximized  = root["windowMaximized"].as<bool>(m_windowMaximized);
        m_optionsLastPage  = root["optionsLastPage"].as<int>(m_optionsLastPage);
        m_toolbarButtonStyle = root["toolbarButtonStyle"].as<int>(m_toolbarButtonStyle);

        m_toolbarSkinPath = QString::fromStdString(
            root["toolbarSkinPath"].as<std::string>(std::string{}));
        m_skinProfilePath = QString::fromStdString(
            root["skinProfilePath"].as<std::string>(std::string{}));

        m_toolbarButtonOrder = readIntList(root, "toolbarButtonOrder");

        if (auto hdr = root["headers"]; hdr && hdr.IsMap()) {
            for (const auto& pair : hdr) {
                auto key = QString::fromStdString(pair.first.as<std::string>());
                auto val = QByteArray::fromBase64(
                    QByteArray::fromStdString(pair.second.as<std::string>()));
                m_headerStates[key] = val;
            }
        }
    } catch (const YAML::BadFile&) {
        // File doesn't exist yet — use defaults
    } catch (const YAML::Exception& ex) {
        logWarning(QStringLiteral("Failed to parse uistate.yml: %1 — using defaults")
                       .arg(QString::fromStdString(ex.what())));
    }
}

void UiState::save(const QString& configDir)
{
    const QString path = configDir + QStringLiteral("/uistate.yml");

    YAML::Emitter out;
    out << YAML::BeginMap;

    writeIntList(out, "serverSplitSizes",     m_serverSplitSizes);
    writeIntList(out, "kadSplitSizes",        m_kadSplitSizes);
    writeIntList(out, "transferSplitSizes",   m_transferSplitSizes);
    writeIntList(out, "sharedHorzSplitSizes", m_sharedHorzSplitSizes);
    writeIntList(out, "sharedVertSplitSizes", m_sharedVertSplitSizes);
    writeIntList(out, "messagesSplitSizes",   m_messagesSplitSizes);
    writeIntList(out, "ircSplitSizes",        m_ircSplitSizes);
    writeIntList(out, "statsSplitSizes",      m_statsSplitSizes);

    out << YAML::Key << "windowWidth"      << YAML::Value << m_windowWidth;
    out << YAML::Key << "windowHeight"     << YAML::Value << m_windowHeight;
    out << YAML::Key << "windowMaximized"  << YAML::Value << m_windowMaximized;
    out << YAML::Key << "optionsLastPage"  << YAML::Value << m_optionsLastPage;
    out << YAML::Key << "toolbarButtonStyle" << YAML::Value << m_toolbarButtonStyle;

    if (!m_toolbarSkinPath.isEmpty())
        out << YAML::Key << "toolbarSkinPath" << YAML::Value << m_toolbarSkinPath.toStdString();
    if (!m_skinProfilePath.isEmpty())
        out << YAML::Key << "skinProfilePath" << YAML::Value << m_skinProfilePath.toStdString();

    if (!m_toolbarButtonOrder.isEmpty())
        writeIntList(out, "toolbarButtonOrder", m_toolbarButtonOrder);

    if (!m_headerStates.isEmpty()) {
        out << YAML::Key << "headers" << YAML::Value << YAML::BeginMap;
        for (auto it = m_headerStates.cbegin(); it != m_headerStates.cend(); ++it)
            out << YAML::Key << it.key().toStdString()
                << YAML::Value << it.value().toBase64().toStdString();
        out << YAML::EndMap;
    }

    out << YAML::EndMap;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        logError(QStringLiteral("Failed to open uistate.yml for writing: %1").arg(path));
        return;
    }

    file.write(out.c_str(), static_cast<qint64>(out.size()));
    file.write("\n", 1);

    if (!file.commit())
        logError(QStringLiteral("Failed to commit uistate.yml: %1").arg(path));
}

// ---------------------------------------------------------------------------
// Splitter bindings
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Window / header bindings
// ---------------------------------------------------------------------------

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
    if (auto it = m_headerStates.constFind(key); it != m_headerStates.constEnd() && !it->isEmpty())
        header->restoreState(*it);

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
