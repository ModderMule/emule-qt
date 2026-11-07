#include "pch.h"

/// @file TorrentOptionsDialog.cpp
/// @brief Options… for all selected torrents (#143) — implementation.

#include "dialogs/TorrentOptionsDialog.h"

#include "controls/TorrentOptionsForm.h"
#include "utils/DialogSizing.h"
#include "utils/MenuUtils.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>

namespace eMule {

TorrentOptionsDialog::TorrentOptionsDialog(const QList<QCborMap>& options, QWidget* parent)
    : RememberedSizeDialog(parent)
{
    setWindowTitle(tr("Torrent Options"));
    setWindowIcon(menuIcon("Preferences.ico"));

    auto* layout = new QVBoxLayout(this);
    auto* label = new QLabel(tr("Options of %n selected torrent(s):", nullptr, int(options.size())), this);
    label->setWordWrap(true);
    layout->addWidget(label);

    m_form = new TorrentOptionsForm(this);
    m_form->setOptionsList(options);
    layout->addWidget(m_form, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &TorrentOptionsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &TorrentOptionsDialog::reject);
    layout->addWidget(buttons);

    DialogSizing::applySize(this, {}, {460, 360}, DialogSizing::Fit::Layout);
}

QCborMap TorrentOptionsDialog::changedOptions() const
{
    return m_form->changedOptions();
}

} // namespace eMule
