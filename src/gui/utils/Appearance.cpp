#include "pch.h"
/// @file Appearance.cpp
/// @brief Theme (light/dark) override and app-wide font scaling.

#include "utils/Appearance.h"
#include "utils/Log.h"
#include "utils/TooltipDelayStyle.h"

#include <QApplication>
#include <QFont>
#include <QList>
#include <QPalette>
#include <QProxyStyle>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

#include <algorithm>
#include <cmath>
#include <utility>

namespace eMule {

namespace {

ThemeChoice s_theme = ThemeChoice::System;
bool s_fusionFallback = false;
qreal s_fontScale = 1.0;

QPalette darkPalette()
{
    const QColor window(45, 45, 45);
    const QColor base(30, 30, 30);
    const QColor text(220, 220, 220);
    const QColor disabled(127, 127, 127);

    QPalette pal;
    pal.setColor(QPalette::Window, window);
    pal.setColor(QPalette::WindowText, text);
    pal.setColor(QPalette::Base, base);
    pal.setColor(QPalette::AlternateBase, window);
    pal.setColor(QPalette::ToolTipBase, window);
    pal.setColor(QPalette::ToolTipText, text);
    pal.setColor(QPalette::PlaceholderText, disabled);
    pal.setColor(QPalette::Text, text);
    pal.setColor(QPalette::Button, window);
    pal.setColor(QPalette::ButtonText, text);
    pal.setColor(QPalette::BrightText, Qt::red);
    pal.setColor(QPalette::Light, QColor(70, 70, 70));
    pal.setColor(QPalette::Midlight, QColor(60, 60, 60));
    pal.setColor(QPalette::Mid, QColor(40, 40, 40));
    pal.setColor(QPalette::Dark, QColor(25, 25, 25));
    pal.setColor(QPalette::Shadow, Qt::black);
    pal.setColor(QPalette::Link, QColor(90, 160, 255));
    pal.setColor(QPalette::LinkVisited, QColor(170, 130, 255));
    pal.setColor(QPalette::Highlight, QColor(42, 110, 200));
    pal.setColor(QPalette::HighlightedText, Qt::white);
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        pal.setColor(QPalette::Disabled, role, disabled);
    return pal;
}

// Class names QApplication maps platform theme fonts to (qapplication.cpp)
constexpr const char* kThemedFontClasses[] = {
    "QMenu", "QMenuBar", "QMenuItem", "QMessageBox", "QLabel", "QTipLabel",
    "QStatusBar", "QMdiSubWindowTitleBar", "QDockWidgetTitle", "QPushButton",
    "QCheckBox", "QRadioButton", "QToolButton", "QAbstractItemView", "QListView",
    "QHeaderView", "QListBox", "QComboMenuItem", "QComboLineEdit", "QSmallFont",
    "QMiniFont", "QGroupBox", "QTabBar", "QTabWidget", "QTextEdit", "QPlainTextEdit",
    "QLineEdit", "QComboBox", "QSpinBox", "QTreeView", "QTableView",
};

QFont scaled(QFont font)
{
    if (font.pointSizeF() > 0)
        font.setPointSizeF(font.pointSizeF() * s_fontScale);
    else if (font.pixelSize() > 0)
        font.setPixelSize(scaledPixelSize(font.pixelSize()));
    return font;
}

Qt::ColorScheme toQt(ThemeChoice theme)
{
    switch (theme) {
    case ThemeChoice::Light: return Qt::ColorScheme::Light;
    case ThemeChoice::Dark:  return Qt::ColorScheme::Dark;
    case ThemeChoice::System: break;
    }
    return Qt::ColorScheme::Unknown;
}

// Windows 10 native styles draw with the light system theme and ignore a dark palette;
// only windows11 and Fusion draw dark (report #10)
bool styleDrawsDark()
{
    const QStyle* style = QApplication::style();
    if (const auto* proxy = qobject_cast<const QProxyStyle*>(style))
        style = proxy->baseStyle();
    if (!style)
        return true;
    const QString name = style->name();
    return name.compare(QLatin1StringView("windowsvista"), Qt::CaseInsensitive) != 0
        && name.compare(QLatin1StringView("windows"), Qt::CaseInsensitive) != 0;
}

} // namespace

bool applyColorScheme(int scheme, bool allowFallback)
{
    s_theme = static_cast<ThemeChoice>(std::clamp(scheme, 0, 2));
    const Qt::ColorScheme wanted = toQt(s_theme);
    auto* hints = QGuiApplication::styleHints();
    hints->setColorScheme(wanted);

    // Fusion was set up for an earlier choice: only a restart restores the native style
    if (s_fusionFallback) {
        if (s_theme == ThemeChoice::System)
            return false;
        QApplication::setPalette(s_theme == ThemeChoice::Dark
                                     ? darkPalette()
                                     : QApplication::style()->standardPalette());
        return true;
    }

    // Windows accepts the dark scheme (forced or system) even where its style cannot draw it
    const bool wantsDark = s_theme == ThemeChoice::Dark
        || (s_theme == ThemeChoice::System && hints->colorScheme() == Qt::ColorScheme::Dark);
    const bool styleRefusesDark = wantsDark && !styleDrawsDark();
    if (!styleRefusesDark
        && (wanted == Qt::ColorScheme::Unknown || hints->colorScheme() == wanted))
        return true;
    if (!allowFallback) {
        logInfo(QStringLiteral("Theme: colour scheme needs Fusion, applies after restart"));
        return false;
    }

    // Platform theme refuses the override: Fusion draws any palette
    auto* fusion = QStyleFactory::create(QStringLiteral("Fusion"));
    if (!fusion) {
        logWarning(QStringLiteral("Theme: platform ignored the colour scheme and Fusion is missing"));
        return false;
    }
    QApplication::setStyle(new TooltipDelayStyle(fusion));
    QApplication::setPalette(wantsDark ? darkPalette() : fusion->standardPalette());
    s_fusionFallback = true;
    logInfo(styleRefusesDark
                ? QStringLiteral("Theme: native style cannot draw dark, using Fusion")
                : QStringLiteral("Theme: platform ignored the colour scheme, using Fusion"));
    return true;
}

bool isDarkScheme()
{
    if (s_theme != ThemeChoice::System)
        return s_theme == ThemeChoice::Dark;
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

void applyFontScale(int percent)
{
    s_fontScale = std::clamp(percent, 80, 200) / 100.0;
    if (qFuzzyCompare(s_fontScale, 1.0))
        return;

    // Platform themes add per-class fonts (macOS: small group box titles, headers, ...);
    // read them before the default changes, then put them back scaled.
    const QFont appFont = QApplication::font();
    QList<std::pair<const char*, QFont>> classFonts;
    for (const char* name : kThemedFontClasses) {
        const QFont f = QApplication::font(name);
        if (f != appFont)
            classFonts.emplaceBack(name, f);
    }

    QApplication::setFont(scaled(appFont));
    for (const auto& [name, f] : classFonts)
        QApplication::setFont(scaled(f), name);
}

qreal fontScale()
{
    return s_fontScale;
}

QColor themed(const QColor& light, const QColor& dark)
{
    return isDarkScheme() ? dark : light;
}

QColor roleColor(TextRole role)
{
    // Light values checked on #ECECEC, dark ones on #2D2D2D (worst window greys)
    switch (role) {
    case TextRole::Info:    return themed(QColor(0x26, 0x80, 0xE0), QColor(0x33, 0x99, 0xFF));
    case TextRole::Success: return themed(QColor(0x00, 0x00, 0xFF), QColor(0x6C, 0xA8, 0xFF));
    case TextRole::Warning: return themed(QColor(0x80, 0x00, 0x80), QColor(0xD0, 0x8C, 0xFF));
    case TextRole::Error:   return themed(QColor(0xFF, 0x00, 0x00), QColor(0xFF, 0x6B, 0x6B));
    case TextRole::Good:    return themed(QColor(0x00, 0x80, 0x00), QColor(0x5B, 0xC8, 0x5B));
    case TextRole::Caution: return themed(QColor(0xCC, 0x66, 0x00), QColor(0xFF, 0xA5, 0x00));
    }
    return {};
}

QString roleColorName(TextRole role)
{
    return roleColor(role).name();
}

int scaledPointSize(int basePt)
{
    return std::max(1, static_cast<int>(std::lround(basePt * s_fontScale)));
}

int scaledPixelSize(int basePx)
{
    return std::max(1, static_cast<int>(std::lround(basePx * s_fontScale)));
}

} // namespace eMule
