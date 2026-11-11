#pragma once

/// @file PreviewNfoView.h
/// @brief Search Details › NFO: the release's .nfo text in a fixed-width font,
///        as drawn (no wrapping), or a note while loading.

#include "controls/PreviewTabView.h"

#include <QList>
#include <QString>

class QPlainTextEdit;

namespace eMule {

class PreviewNfoView : public PreviewTabView {
    Q_OBJECT

public:
    struct Nfo {
        QString name;
        QString text;
    };

    explicit PreviewNfoView(QWidget* parent = nullptr);

    /// None (or only blank ones) shows @p emptyText. Several get a name line each.
    void setNfos(const QList<Nfo>& nfos, const QString& emptyText);

    [[nodiscard]] QString text() const;

private:
    QPlainTextEdit* m_text = nullptr;
};

} // namespace eMule
