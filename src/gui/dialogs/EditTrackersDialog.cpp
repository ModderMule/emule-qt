#include "pch.h"

/// @file EditTrackersDialog.cpp
/// @brief One tracker edit for many torrents (#143) — implementation.

#include "dialogs/EditTrackersDialog.h"

#include "utils/DialogSizing.h"
#include "utils/MenuUtils.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSet>
#include <QVBoxLayout>

#include <algorithm>

namespace eMule {

namespace {

QStringList cleanedLines(const QString& text)
{
    QStringList out;
    for (const QString& line : text.split(QLatin1Char('\n')))
        if (const QString url = line.trimmed(); !url.isEmpty() && !out.contains(url))
            out << url;
    return out;
}

} // namespace

EditTrackersDialog::EditTrackersDialog(const QList<QStringList>& lists, QWidget* parent)
    : RememberedSizeDialog(parent)
    , m_common(commonTrackers(lists))
    , m_torrents(int(lists.size()))
{
    QSet<QString> all;
    for (const QStringList& l : lists)
        all.unite(QSet<QString>(l.cbegin(), l.cend()));
    m_others = int(all.size()) - int(m_common.size());

    setWindowTitle(tr("Edit Trackers"));
    setWindowIcon(menuIcon("Server.ico"));

    auto* layout = new QVBoxLayout(this);
    auto* label = new QLabel(
        m_torrents == 1 ? tr("Tracker URLs, one per line:")
                        : tr("Trackers of all %n selected torrent(s), one per line. Added lines go to "
                             "all of them, removed lines come off all of them.", nullptr, m_torrents),
        this);
    label->setWordWrap(true);
    layout->addWidget(label);

    m_edit = new QPlainTextEdit(this);
    m_edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_edit->setPlainText(m_common.join(QLatin1Char('\n')));
    layout->addWidget(m_edit, 1);

    // only the trackers some torrents lack make "all others" differ from the box
    m_exact = new QCheckBox(tr("Remove all other trackers"), this);
    m_exact->setVisible(m_others > 0);
    layout->addWidget(m_exact);
    m_note = new QLabel(this);
    m_note->setWordWrap(true);
    m_note->setVisible(m_others > 0);
    layout->addWidget(m_note);
    connect(m_exact, &QCheckBox::toggled, this, &EditTrackersDialog::updateNote);
    updateNote();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &EditTrackersDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &EditTrackersDialog::reject);
    layout->addWidget(buttons);

    DialogSizing::applySize(this, {}, {520, 320}, DialogSizing::Fit::Layout);
}

EditTrackersDialog::Edit EditTrackersDialog::edit() const
{
    return editFor(m_common, cleanedLines(m_edit->toPlainText()), m_exact->isChecked());
}

QStringList EditTrackersDialog::commonTrackers(const QList<QStringList>& lists)
{
    if (lists.isEmpty())
        return {};
    QStringList out;
    for (const QString& url : lists.first())
        if (!out.contains(url)
            && std::ranges::all_of(lists, [&url](const QStringList& l) { return l.contains(url); }))
            out << url;
    return out;
}

EditTrackersDialog::Edit EditTrackersDialog::editFor(const QStringList& common, const QStringList& lines,
                                                     bool exact)
{
    if (exact)
        return {lines, {}, true};
    Edit e;
    for (const QString& url : lines)
        if (!common.contains(url))
            e.add << url;
    for (const QString& url : common)
        if (!lines.contains(url))
            e.remove << url;
    return e;
}

void EditTrackersDialog::accept()
{
    const Edit e = edit();
    if (e.exact && e.add.isEmpty()
        && QMessageBox::question(this, windowTitle(),
                                 tr("Remove all trackers from %n torrent(s)? They then find peers by "
                                    "DHT and peer exchange only.", nullptr, m_torrents))
               != QMessageBox::Yes)
        return;
    RememberedSizeDialog::accept();
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void EditTrackersDialog::updateNote()
{
    m_note->setText(m_exact->isChecked()
                        ? tr("%n tracker(s) on only some of the torrents will be removed.", nullptr, m_others)
                        : tr("%n tracker(s) on only some of the torrents stay as they are.", nullptr, m_others));
}

} // namespace eMule
