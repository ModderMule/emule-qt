#include "pch.h"
/// @file CategoryDialog.cpp
/// @brief Add/edit one download category — matches MFC's IDD_CAT layout.

#include "dialogs/CategoryDialog.h"
#include "files/KnownFile.h" // kPrLow / kPrNormal / kPrHigh
#include "utils/DialogSizing.h"

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QStyle>
#include <QVBoxLayout>

namespace eMule {

CategoryDialog::CategoryDialog(const DownloadCategory& category,
                               const QString& defaultIncomingDir, QWidget* parent)
    : QDialog(parent)
    , m_category(category)
    , m_defaultIncomingDir(defaultIncomingDir)
    , m_color(category.color)
{
    setWindowTitle(tr("Edit Category-Properties"));

    auto* mainLayout = new QVBoxLayout(this);
    auto* form = new QFormLayout;

    m_titleEdit = new QLineEdit(m_category.title, this);
    form->addRow(tr("Title"), m_titleEdit);

    m_commentEdit = new QLineEdit(m_category.comment, this);
    form->addRow(tr("Comments"), m_commentEdit);

    // Incoming dir + browse. The warning in the label is MFC's and is not
    // decoration: this folder becomes shared, unconditionally and
    // un-unshareably, the moment it is set.
    auto* incomingRow = new QHBoxLayout;
    m_incomingEdit = new QLineEdit(m_category.incomingPath, this);
    m_incomingEdit->setPlaceholderText(m_defaultIncomingDir);
    incomingRow->addWidget(m_incomingEdit);

    auto* browseButton = new QPushButton(this);
    browseButton->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    browseButton->setFixedSize(28, 28);
    browseButton->setToolTip(tr("Choose a folder for incoming files"));
    connect(browseButton, &QPushButton::clicked, this, &CategoryDialog::onBrowseClicked);
    incomingRow->addWidget(browseButton);

    form->addRow(tr("Incoming Files  (Folder will be shared!)"), incomingRow);

    // Index *is* the value, as in MFC (srchybrid/CatDialog.cpp:137-140): the
    // three entries line up with kPrLow / kPrNormal / kPrHigh.
    m_prioCombo = new QComboBox(this);
    m_prioCombo->addItem(tr("Low"), kPrLow);
    m_prioCombo->addItem(tr("Normal"), kPrNormal);
    m_prioCombo->addItem(tr("High"), kPrHigh);
    m_prioCombo->setCurrentIndex(m_prioCombo->findData(m_category.prio));
    if (m_prioCombo->currentIndex() < 0)
        m_prioCombo->setCurrentIndex(m_prioCombo->findData(kPrNormal));
    form->addRow(tr("Priority for this category"), m_prioCombo);

    m_colorButton = new QPushButton(this);
    m_colorButton->setFixedSize(60, 24);
    connect(m_colorButton, &QPushButton::clicked, this, &CategoryDialog::onColorClicked);
    updateColorButton();
    form->addRow(tr("Color"), m_colorButton);

    m_autocatEdit = new QLineEdit(m_category.autocat, this);
    form->addRow(tr("Auto cat. assignment (separate patterns with |)"), m_autocatEdit);

    m_autocatRegexpCheck = new QCheckBox(tr("As Regular Expression"), this);
    m_autocatRegexpCheck->setChecked(m_category.autocatIsRegexp);
    form->addRow(QString(), m_autocatRegexpCheck);

    m_regexpEdit = new QLineEdit(m_category.regexp, this);
    // Stored and validated, but nothing consults it yet: the per-category view
    // filter this belongs to (MFC's filter mode 18) is not ported. Left enabled
    // so a configuration imported from eMule survives a round trip through this
    // dialog instead of being silently blanked.
    m_regexpEdit->setToolTip(tr("Stored for compatibility — the per-category view "
                                "filter is not implemented yet."));
    form->addRow(tr("Regular expression for view filter:"), m_regexpEdit);

    mainLayout->addLayout(form);
    mainLayout->addStretch();

    // MFC answers a bad field with an ErrorBalloon anchored to it, not with a
    // message box (srchybrid/CatDialog.cpp:167). A modal box on top of a modal
    // dialog is the wrong shape for "you mistyped a path", so this is the same
    // idea: a line under the form, and focus moved to the offending field.
    m_errorLabel = new QLabel(this);
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet(QStringLiteral("color: palette(link-visited);"));
    m_errorLabel->hide();
    mainLayout->addWidget(m_errorLabel);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &CategoryDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    // MFC's 230x221 dialog units at the default font. A floor, not a cap — the
    // labels here are long enough that a translation will need more.
    DialogSizing::applyFixedSize(this, QSize(400, 340));
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void CategoryDialog::onBrowseClicked()
{
    const QString start = m_incomingEdit->text().isEmpty() ? m_defaultIncomingDir
                                                           : m_incomingEdit->text();
    const QString dir =
        QFileDialog::getExistingDirectory(this, tr("Choose a folder for incoming files"), start);
    if (!dir.isEmpty())
        m_incomingEdit->setText(dir);
}

void CategoryDialog::onColorClicked()
{
    const QColor current = m_color == kCategoryColorAuto ? QColor(Qt::white)
                                                         : QColor::fromRgb(m_color);
    const QColor chosen = QColorDialog::getColor(current, this, tr("Color"));
    if (!chosen.isValid())
        return;

    m_color = chosen.rgb() & 0x00FFFFFFu;
    updateColorButton();
}

void CategoryDialog::onAccept()
{
    const QString title = m_titleEdit->text().trimmed();
    if (title.isEmpty()) {
        showFieldError(m_titleEdit, tr("A category needs a title."));
        return;
    }

    // An empty path is a valid answer meaning "the global incoming directory",
    // and it is how the user gets back there after having set one. Only a
    // non-empty path is validated.
    const QString incoming = m_incomingEdit->text().trimmed();
    if (!incoming.isEmpty()) {
        const QDir dir(incoming);
        if (!dir.exists() && !QDir().mkpath(incoming)) {
            // Like MFC the dialog stays open: the alternative is accepting a
            // path that quietly falls back to the global incoming dir, so the
            // user never learns their folder is not being used.
            showFieldError(m_incomingEdit,
                           tr("Invalid folder. Folder can not be created. "
                              "Please check name and location."));
            return;
        }
    }

    const QString autocat = m_autocatEdit->text().trimmed();
    if (m_autocatRegexpCheck->isChecked() && !autocat.isEmpty()
        && !QRegularExpression(autocat).isValid())
    {
        showFieldError(m_autocatEdit, tr("Bad regular expression"));
        return;
    }

    const QString regexp = m_regexpEdit->text().trimmed();
    if (!regexp.isEmpty() && !QRegularExpression(regexp).isValid()) {
        showFieldError(m_regexpEdit, tr("Bad regular expression"));
        return;
    }

    m_category.title = title;
    m_category.comment = m_commentEdit->text();
    m_category.incomingPath = incoming.isEmpty()
                                  ? QString()
                                  : QDir::cleanPath(QDir(incoming).absolutePath());
    m_category.prio = static_cast<quint8>(m_prioCombo->currentData().toUInt());
    m_category.color = m_color;
    m_category.autocat = autocat;
    m_category.autocatIsRegexp = m_autocatRegexpCheck->isChecked();
    m_category.regexp = regexp;

    accept();
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void CategoryDialog::showFieldError(QWidget* field, const QString& text)
{
    m_errorLabel->setText(text);
    m_errorLabel->show();
    if (field)
        field->setFocus();
}

void CategoryDialog::updateColorButton()
{
    if (m_color == kCategoryColorAuto) {
        m_colorButton->setStyleSheet(QString());
        m_colorButton->setText(tr("Default"));
        return;
    }

    m_colorButton->setText(QString());
    m_colorButton->setStyleSheet(
        QStringLiteral("background-color: %1; border: 1px solid gray;")
            .arg(QColor::fromRgb(m_color).name()));
}

} // namespace eMule
