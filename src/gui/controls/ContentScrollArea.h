#pragma once

/// @file ContentScrollArea.h
/// @brief A scroll area that admits how tall its content really is.
///
/// QScrollArea::sizeHint() clamps itself to 36 by 24 character cells, on the assumption
/// that whatever it holds is meant to scroll. A dialog that asks its layout how much room
/// the content needs would therefore be told "384 pixels" no matter how long the form is —
/// the very number DialogSizing::applySize() exists to get right. This subclass reports
/// the content's own hint instead, and keeps the small minimum hint, so the scrollbar
/// appears only where the screen is genuinely too short.
///
/// heightForWidth() has to be asked for by name: QWidgetItem answers it from the *scroll
/// area's* internal viewport layout and never reaches the override, so a dialog
/// measuring its layout is told nothing about the form inside. DialogSizing::applySize()
/// calls contentHeightForWidth() directly for that reason.

#include <QScrollArea>
#include <QSize>

namespace eMule {

class ContentScrollArea : public QScrollArea {
    Q_OBJECT

public:
    explicit ContentScrollArea(QWidget* parent = nullptr);

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] int   heightForWidth(int width) const override;

    /// How tall the content is when this area is @p areaWidth wide, scrollbar included.
    [[nodiscard]] int contentHeightForWidth(int areaWidth) const;
};

} // namespace eMule
