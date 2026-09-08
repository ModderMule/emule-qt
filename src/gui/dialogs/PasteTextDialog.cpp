#include "pch.h"
#include "dialogs/PasteTextDialog.h"

#include "utils/DialogSizing.h"

#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace eMule {

PasteTextDialog::PasteTextDialog(const Chrome& chrome, QWidget* parent)
    : QDialog(parent)
    , m_acceptText(chrome.acceptText)
{
    setWindowTitle(chrome.title);
    if (!chrome.iconPath.isEmpty())
        setWindowIcon(QIcon(chrome.iconPath));

    auto* layout = new QVBoxLayout(this);

    auto* label = new QLabel(chrome.label, this);
    layout->addWidget(label);

    m_edit = new QPlainTextEdit(this);
    m_edit->setPlaceholderText(chrome.placeholder);
    layout->addWidget(m_edit);

    auto* btnLayout = new QHBoxLayout;
    btnLayout->addStretch();

    m_acceptBtn = new QPushButton(chrome.acceptText, this);
    m_acceptBtn->setDefault(true);
    m_acceptBtn->setEnabled(false);
    btnLayout->addWidget(m_acceptBtn);

    auto* cancelBtn = new QPushButton(tr("Cancel"), this);
    btnLayout->addWidget(cancelBtn);

    layout->addLayout(btnLayout);

    connect(m_edit, &QPlainTextEdit::textChanged, this, [this] {
        m_acceptBtn->setEnabled(!m_edit->toPlainText().trimmed().isEmpty());
    });
    connect(m_acceptBtn, &QPushButton::clicked, this, [this] { onAccepted(); });
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    DialogSizing::applySize(this, {}, chrome.defaultSize, DialogSizing::Fit::Layout);
}

QString PasteTextDialog::text() const
{
    return m_edit->toPlainText().trimmed();
}

QStringList PasteTextDialog::lines() const
{
    QStringList out;
    const auto raw = m_edit->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : raw) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty())
            out.append(trimmed);
    }
    return out;
}

void PasteTextDialog::setLines(const QStringList& lines)
{
    m_edit->setPlainText(lines.join(QLatin1Char('\n')));
}

void PasteTextDialog::setBusy(bool busy)
{
    m_edit->setReadOnly(busy);
    m_acceptBtn->setEnabled(!busy && !m_edit->toPlainText().trimmed().isEmpty());
    m_acceptBtn->setText(busy ? tr("Working…") : m_acceptText);
}

} // namespace eMule
