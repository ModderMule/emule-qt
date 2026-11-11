#pragma once

/// @file ScrollMessageBox.h
/// @brief Message boxes that never outgrow the screen (issue #176).
///
/// QMessageBox sizes itself to its whole text with no cap: a list of 20 skipped
/// torrents pushes the buttons below the screen edge. These take the same arguments
/// as the QMessageBox statics; short text still gets a plain QMessageBox (native on
/// macOS), text taller than the screen allows gets a resizable box whose text scrolls.
///
/// Use for every box whose text holds a list of variable length.
/// Header-only on purpose, like AsyncMessageBox.h: GUI tests list sources by hand.

#include <QAbstractButton>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QGridLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>

#include <algorithm>

#include "utils/ModalHost.h"

namespace eMule::ScrollMessageBox {

namespace detail {

/// Width the text wraps at — QMessageBox's own soft limit.
constexpr int kTextWidth = 500;

inline QRect availableArea(const QWidget* parent)
{
    const QScreen* screen = parent ? parent->screen() : nullptr;
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    return screen ? screen->availableGeometry() : QRect(0, 0, 1024, 768);
}

/// Lines of text shown before it scrolls, on a screen tall enough for them.
constexpr int kMaxLines = 24;

/// Tallest the text may get before it scrolls: kMaxLines, and never more than two
/// thirds of the screen, which leaves the title bar and the buttons on it.
inline int maxTextHeight(const QWidget* parent)
{
    const QFontMetrics fm(parent ? parent->font() : QApplication::font());
    return std::max(120, std::min(kMaxLines * fm.lineSpacing(), availableArea(parent).height() * 2 / 3));
}

inline int textWidth(const QWidget* parent)
{
    return std::min(kTextWidth, std::max(240, availableArea(parent).width() / 2));
}

/// Height of @p text wrapped the way the box would wrap it.
inline int textHeight(const QString& text, const QWidget* parent)
{
    QLabel probe(text);
    probe.setWordWrap(true);
    if (parent)
        probe.setFont(parent->font());
    return probe.heightForWidth(textWidth(parent));
}

inline bool tooTall(const QString& text, const QWidget* parent)
{
    return textHeight(text, parent) > maxTextHeight(parent);
}

/// What Escape / the close button answers, as QMessageBox picks it.
inline QMessageBox::StandardButton escapeButton(QMessageBox::StandardButtons buttons)
{
    for (const auto b : {QMessageBox::Cancel, QMessageBox::No, QMessageBox::Abort,
                         QMessageBox::Close, QMessageBox::Ok}) {
        if (buttons.testFlag(b))
            return b;
    }
    return QMessageBox::NoButton;
}

inline QStyle::StandardPixmap iconPixmap(QMessageBox::Icon icon)
{
    switch (icon) {
    case QMessageBox::Information: return QStyle::SP_MessageBoxInformation;
    case QMessageBox::Warning:     return QStyle::SP_MessageBoxWarning;
    case QMessageBox::Critical:    return QStyle::SP_MessageBoxCritical;
    case QMessageBox::Question:    return QStyle::SP_MessageBoxQuestion;
    default:                       return QStyle::SP_CustomBase;
    }
}

/// Icon, scrolling text, buttons; sized to the text but never past maxTextHeight().
/// The result code is the QMessageBox::StandardButton clicked, 0 on Escape.
inline QDialog* build(QMessageBox::Icon icon, QWidget* parent, const QString& title,
                      const QString& text, QMessageBox::StandardButtons buttons,
                      QMessageBox::StandardButton defaultButton)
{
    auto* dlg = new QDialog(parent);
    dlg->setWindowTitle(title);
    QStyle* style = dlg->style();

    auto* grid = new QGridLayout(dlg);
    if (const auto sp = iconPixmap(icon); sp != QStyle::SP_CustomBase) {
        const int size = style->pixelMetric(QStyle::PM_MessageBoxIconSize, nullptr, dlg);
        auto* iconLabel = new QLabel(dlg);
        iconLabel->setPixmap(style->standardIcon(sp, nullptr, dlg).pixmap(size, size));
        grid->addWidget(iconLabel, 0, 0, Qt::AlignTop);
    }

    auto* label = new QLabel(text);
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);

    auto* area = new QScrollArea(dlg);
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area->setWidget(label);
    const int width = textWidth(parent) + area->verticalScrollBar()->sizeHint().width();
    area->setMinimumWidth(width);
    area->setMinimumHeight(std::min(textHeight(text, parent), maxTextHeight(parent)));
    grid->addWidget(area, 0, 1);
    grid->setRowStretch(0, 1);
    grid->setColumnStretch(1, 1);

    auto* box = new QDialogButtonBox(QDialogButtonBox::StandardButtons(int(buttons)), dlg);
    if (defaultButton != QMessageBox::NoButton) {
        if (QPushButton* b = box->button(QDialogButtonBox::StandardButton(int(defaultButton))))
            b->setDefault(true);
    }
    QObject::connect(box, &QDialogButtonBox::clicked, dlg, [dlg, box](QAbstractButton* b) {
        dlg->done(int(box->standardButton(b)));
    });
    grid->addWidget(box, 1, 0, 1, 2);

    // Opens at the text's height; afterwards the user may shrink it, the text scrolls
    dlg->adjustSize();
    area->setMinimumHeight(std::min(area->minimumHeight(), 120));
    return dlg;
}

inline QMessageBox::StandardButton exec(QMessageBox::Icon icon, QWidget* parent,
                                        const QString& title, const QString& text,
                                        QMessageBox::StandardButtons buttons,
                                        QMessageBox::StandardButton defaultButton)
{
    if (!tooTall(text, parent)) {
        QMessageBox box(icon, title, text, buttons, parent);
        box.setDefaultButton(defaultButton);
        const int r = box.exec();
        return r == -1 ? escapeButton(buttons) : QMessageBox::StandardButton(r);
    }

    QPointer<QDialog> dlg = build(icon, parent, title, text, buttons, defaultButton);
    const int r = dlg->exec();
    delete dlg.data();
    return r == 0 ? escapeButton(buttons) : QMessageBox::StandardButton(r);
}

} // namespace detail

inline QMessageBox::StandardButton
warning(QWidget* parent, const QString& title, const QString& text,
        QMessageBox::StandardButtons buttons = QMessageBox::Ok,
        QMessageBox::StandardButton defaultButton = QMessageBox::NoButton)
{
    return detail::exec(QMessageBox::Warning, parent, title, text, buttons, defaultButton);
}

inline QMessageBox::StandardButton
information(QWidget* parent, const QString& title, const QString& text,
            QMessageBox::StandardButtons buttons = QMessageBox::Ok,
            QMessageBox::StandardButton defaultButton = QMessageBox::NoButton)
{
    return detail::exec(QMessageBox::Information, parent, title, text, buttons, defaultButton);
}

inline QMessageBox::StandardButton
critical(QWidget* parent, const QString& title, const QString& text,
         QMessageBox::StandardButtons buttons = QMessageBox::Ok,
         QMessageBox::StandardButton defaultButton = QMessageBox::NoButton)
{
    return detail::exec(QMessageBox::Critical, parent, title, text, buttons, defaultButton);
}

inline QMessageBox::StandardButton
question(QWidget* parent, const QString& title, const QString& text,
         QMessageBox::StandardButtons buttons = QMessageBox::Yes | QMessageBox::No,
         QMessageBox::StandardButton defaultButton = QMessageBox::NoButton)
{
    return detail::exec(QMessageBox::Question, parent, title, text, buttons, defaultButton);
}

/// Non-blocking, OK only: window-modal (non-modal over Options), deletes itself.
inline void open(QMessageBox::Icon icon, QWidget* parent, const QString& title, const QString& text)
{
    QDialog* dlg = detail::tooTall(text, parent)
                       ? detail::build(icon, parent, title, text, QMessageBox::Ok, QMessageBox::Ok)
                       : new QMessageBox(icon, title, text, QMessageBox::Ok, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    ModalHost::open(dlg);
}

} // namespace eMule::ScrollMessageBox
