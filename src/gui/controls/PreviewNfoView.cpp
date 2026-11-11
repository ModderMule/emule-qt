#include "pch.h"

/// @file PreviewNfoView.cpp
/// @brief .nfo text of a search result — see PreviewNfoView.h.

#include "controls/PreviewNfoView.h"

#include <QFontDatabase>
#include <QPlainTextEdit>

namespace eMule {

PreviewNfoView::PreviewNfoView(QWidget* parent)
    : PreviewTabView(parent)
{
    m_text = new QPlainTextEdit(this);
    m_text->setReadOnly(true);
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);   // NFO art breaks when wrapped
    m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_text->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    setContentPage(m_text);
}

void PreviewNfoView::setNfos(const QList<Nfo>& nfos, const QString& emptyText)
{
    QStringList parts;
    for (const Nfo& nfo : nfos) {
        if (nfo.text.trimmed().isEmpty())
            continue;
        parts.append(nfos.size() > 1 ? QStringLiteral("── %1 ──\n\n%2").arg(nfo.name, nfo.text) : nfo.text);
    }
    if (parts.isEmpty()) {
        m_text->clear();
        showMessage(emptyText);
        return;
    }
    m_text->setPlainText(parts.join(QStringLiteral("\n\n\n")));
    showContent();
}

QString PreviewNfoView::text() const
{
    return m_text->toPlainText();
}

} // namespace eMule
