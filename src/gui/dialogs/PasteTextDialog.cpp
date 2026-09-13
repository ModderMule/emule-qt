#include "pch.h"
#include "dialogs/PasteTextDialog.h"

#include "controls/UsenetQueueModel.h"

#include "utils/DialogSizing.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace eMule {

PasteTextDialog::PasteTextDialog(const Chrome& chrome, QWidget* parent)
    : QDialog(parent)
    , m_acceptText(chrome.acceptText)
    , m_readOnlyText(chrome.readOnlyText)
{
    setWindowTitle(chrome.title);
    if (!chrome.iconPath.isEmpty())
        setWindowIcon(QIcon(chrome.iconPath));

    auto* layout = new QVBoxLayout(this);

    auto* label = new QLabel(chrome.label, this);
    layout->addWidget(label);

    m_edit = new QPlainTextEdit(this);
    m_edit->setPlaceholderText(chrome.placeholder);
    m_edit->setReadOnly(chrome.readOnlyText);
    layout->addWidget(m_edit);

    if (!chrome.passwordLabel.isEmpty()) {
        auto* pwRow = new QHBoxLayout;
        pwRow->addWidget(new QLabel(chrome.passwordLabel, this));
        m_password = new QLineEdit(this);
        m_password->setEchoMode(QLineEdit::Password);
        m_password->setPlaceholderText(tr("optional"));
        pwRow->addWidget(m_password, 1);
        layout->addLayout(pwRow);
    }

    if (!chrome.queueCategories.isEmpty()) {
        // One row, three choices, all optional — the release queues exactly as it
        // used to if the user touches none of them. The category box carries the
        // *index* as item data, the way every other category combo in the app
        // does, because the names are the user's and may repeat.
        auto* queueRow = new QHBoxLayout;

        queueRow->addWidget(new QLabel(tr("Category:"), this));
        m_category = new QComboBox(this);
        for (int i = 0; i < chrome.queueCategories.size(); ++i)
            m_category->addItem(chrome.queueCategories.at(i), i);
        queueRow->addWidget(m_category, 1);

        queueRow->addWidget(new QLabel(tr("Priority:"), this));
        m_priority = new QComboBox(this);
        for (const int level : kUsenetPriorityLevels)
            m_priority->addItem(usenetPriorityName(level), level);
        m_priority->setCurrentIndex(m_priority->findData(0));
        queueRow->addWidget(m_priority);

        m_paused = new QCheckBox(tr("Start paused"), this);
        queueRow->addWidget(m_paused);

        layout->addLayout(queueRow);
    }

    auto* btnLayout = new QHBoxLayout;
    btnLayout->addStretch();

    m_acceptBtn = new QPushButton(chrome.acceptText, this);
    m_acceptBtn->setDefault(true);
    // A read-only box is a list the caller filled in, so there is nothing for
    // the user to type and nothing to wait for.
    m_acceptBtn->setEnabled(chrome.readOnlyText);
    btnLayout->addWidget(m_acceptBtn);

    auto* cancelBtn = new QPushButton(tr("Cancel"), this);
    btnLayout->addWidget(cancelBtn);

    layout->addLayout(btnLayout);

    connect(m_edit, &QPlainTextEdit::textChanged, this, [this] {
        m_acceptBtn->setEnabled(!m_edit->toPlainText().trimmed().isEmpty());
    });
    if (m_password) {
        // Enter in the password field submits, the way it does in the box.
        connect(m_password, &QLineEdit::returnPressed, this, [this] {
            if (m_acceptBtn->isEnabled())
                onAccepted();
        });
    }
    connect(m_acceptBtn, &QPushButton::clicked, this, [this] { onAccepted(); });
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    DialogSizing::applySize(this, {}, chrome.defaultSize, DialogSizing::Fit::Layout);
}

QString PasteTextDialog::text() const
{
    return m_edit->toPlainText().trimmed();
}

QString PasteTextDialog::password() const
{
    return m_password ? m_password->text() : QString();
}

int PasteTextDialog::queueCategory() const
{
    return m_category ? m_category->currentData().toInt() : 0;
}

int PasteTextDialog::queuePriority() const
{
    return m_priority ? m_priority->currentData().toInt() : 0;
}

bool PasteTextDialog::queuePaused() const
{
    return m_paused && m_paused->isChecked();
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
    m_edit->setReadOnly(busy || m_readOnlyText);
    if (m_password)
        m_password->setEnabled(!busy);
    m_acceptBtn->setEnabled(!busy
                            && (m_readOnlyText || !m_edit->toPlainText().trimmed().isEmpty()));
    m_acceptBtn->setText(busy ? tr("Working…") : m_acceptText);
}

} // namespace eMule
