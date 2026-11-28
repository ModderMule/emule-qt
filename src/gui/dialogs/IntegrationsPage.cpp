#include "pch.h"
/// @file IntegrationsPage.cpp
/// @brief Options > Advanced > Integrations — implementation.

#include "dialogs/IntegrationsPage.h"

#include "dialogs/RememberedSizeDialog.h"
#include "prefs/Preferences.h"
#include "IpcMessage.h"

#include <QCborArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <vector>

namespace eMule {

using Ipc::IpcMessage;

namespace {

constexpr int kMaxInstances = 16;

/// Add / edit one Sonarr or Radarr.
class ArrInstanceDialog final : public RememberedSizeDialog {
public:
    ArrInstanceDialog(const ArrInstance& instance, QWidget* parent)
        : RememberedSizeDialog(parent)
    {
        setWindowTitle(IntegrationsPage::tr("Sonarr / Radarr"));
        auto* form = new QFormLayout(this);
        m_name = new QLineEdit(instance.name, this);
        form->addRow(IntegrationsPage::tr("Name:"), m_name);
        m_kind = new QComboBox(this);
        m_kind->addItem(QStringLiteral("Sonarr"), QStringLiteral("sonarr"));
        m_kind->addItem(QStringLiteral("Radarr"), QStringLiteral("radarr"));
        m_kind->setCurrentIndex(instance.isRadarr() ? 1 : 0);
        form->addRow(IntegrationsPage::tr("Type:"), m_kind);
        m_url = new QLineEdit(instance.url, this);
        m_url->setPlaceholderText(QStringLiteral("http://localhost:8989"));
        form->addRow(IntegrationsPage::tr("Address:"), m_url);
        m_key = new QLineEdit(instance.apiKey, this);
        m_key->setEchoMode(QLineEdit::PasswordEchoOnEdit);
        m_key->setToolTip(IntegrationsPage::tr("Settings > General > API Key in Sonarr or Radarr"));
        form->addRow(IntegrationsPage::tr("API key:"), m_key);
        m_enabled = new QCheckBox(IntegrationsPage::tr("Enabled"), this);
        m_enabled->setChecked(instance.enabled);
        form->addRow(QString(), m_enabled);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        form->addRow(buttons);
        const auto updateOk = [this, buttons] {
            buttons->button(QDialogButtonBox::Ok)->setEnabled(!m_url->text().trimmed().isEmpty());
        };
        connect(m_url, &QLineEdit::textChanged, this, updateOk);
        updateOk();
        // Radarr's default port when the address is still empty
        connect(m_kind, &QComboBox::currentIndexChanged, this, [this](int index) {
            m_url->setPlaceholderText(index == 1 ? QStringLiteral("http://localhost:7878")
                                                 : QStringLiteral("http://localhost:8989"));
        });
        resize(420, sizeHint().height());
    }

    [[nodiscard]] ArrInstance instance() const
    {
        ArrInstance a;
        a.kind = m_kind->currentData().toString();
        a.name = m_name->text().trimmed();
        if (a.name.isEmpty())
            a.name = m_kind->currentText();
        a.url = m_url->text().trimmed();
        a.apiKey = m_key->text().trimmed();
        a.enabled = m_enabled->isChecked();
        return a;
    }

private:
    QLineEdit* m_name = nullptr;
    QComboBox* m_kind = nullptr;
    QLineEdit* m_url = nullptr;
    QLineEdit* m_key = nullptr;
    QCheckBox* m_enabled = nullptr;
};

} // namespace

IntegrationsPage::IntegrationsPage(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);

    // --- Sonarr / Radarr ---
    auto* arrGroup = new QGroupBox(tr("Sonarr / Radarr"), this);
    auto* arrLayout = new QVBoxLayout(arrGroup);
    m_arrCheck = new QCheckBox(tr("Act as qBittorrent download client and Torznab indexer for eD2K"), arrGroup);
    m_arrCheck->setToolTip(tr("Sonarr and Radarr add eD2K downloads and search eD2K and Kad. "
                              "Served on the Web Interface port."));
    arrLayout->addWidget(m_arrCheck);
    m_arrHint = new QLabel(arrGroup);
    m_arrHint->setWordWrap(true);
    m_arrHint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    arrLayout->addWidget(m_arrHint);

    auto* arrForm = new QFormLayout;
    m_waitSpin = new QSpinBox(arrGroup);
    m_waitSpin->setRange(5, 90);
    m_waitSpin->setSuffix(tr(" s"));
    m_waitSpin->setToolTip(tr("How long a search from Sonarr or Radarr collects eD2K and Kad results"));
    arrForm->addRow(tr("Search time:"), m_waitSpin);
    m_pollSpin = new QSpinBox(arrGroup);
    m_pollSpin->setRange(0, 1440);
    m_pollSpin->setSuffix(tr(" min"));
    m_pollSpin->setSpecialValueText(tr("off"));
    m_pollSpin->setToolTip(tr("eD2K has no RSS feed. The Wanted lists below are searched in the "
                              "background and the hits are served as the feed Sonarr and Radarr check."));
    arrForm->addRow(tr("Read Wanted lists every:"), m_pollSpin);
    arrLayout->addLayout(arrForm);

    m_table = new QTreeWidget(arrGroup);
    m_table->setRootIsDecorated(false);
    m_table->setHeaderLabels({tr("Name"), tr("Type"), tr("Address")});
    m_table->header()->setStretchLastSection(true);
    m_table->setMinimumHeight(90);
    arrLayout->addWidget(m_table);
    auto* buttons = new QHBoxLayout;
    m_addBtn = new QPushButton(tr("Add..."), arrGroup);
    m_editBtn = new QPushButton(tr("Edit..."), arrGroup);
    m_removeBtn = new QPushButton(tr("Remove"), arrGroup);
    buttons->addWidget(m_addBtn);
    buttons->addWidget(m_editBtn);
    buttons->addWidget(m_removeBtn);
    buttons->addStretch();
    arrLayout->addLayout(buttons);
    layout->addWidget(arrGroup);

    // --- Gluetun ---
    auto* gtGroup = new QGroupBox(tr("Gluetun VPN"), this);
    auto* gtLayout = new QVBoxLayout(gtGroup);
    m_gluetunCheck = new QCheckBox(tr("Use the port Gluetun forwards"), gtGroup);
    m_gluetunCheck->setToolTip(tr("Behind a VPN, UPnP cannot open a port. The VPN provider's forwarded "
                                  "port is read from Gluetun every minute and set as the listen port."));
    gtLayout->addWidget(m_gluetunCheck);
    auto* gtForm = new QFormLayout;
    gtForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);   // macOS keeps size hints
    m_gluetunUrlEdit = new QLineEdit(gtGroup);
    m_gluetunUrlEdit->setPlaceholderText(QStringLiteral("http://127.0.0.1:8000"));
    gtForm->addRow(tr("Control server:"), m_gluetunUrlEdit);
    m_gluetunKeyEdit = new QLineEdit(gtGroup);
    m_gluetunKeyEdit->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    m_gluetunKeyEdit->setPlaceholderText(tr("optional"));
    m_gluetunKeyEdit->setToolTip(tr("The API key of Gluetun's control server auth config"));
    gtForm->addRow(tr("API key:"), m_gluetunKeyEdit);
    m_gluetunTargetCombo = new QComboBox(gtGroup);
    m_gluetunTargetCombo->addItem(tr("eD2K (TCP and UDP)"), QStringLiteral("ed2k"));
    m_gluetunTargetCombo->addItem(tr("BitTorrent"), QStringLiteral("bt"));
    gtForm->addRow(tr("Port for:"), m_gluetunTargetCombo);
    gtLayout->addLayout(gtForm);
    layout->addWidget(gtGroup);
    layout->addStretch();

    const auto dirty = [this] { emit changed(); };
    connect(m_arrCheck, &QCheckBox::toggled, this, [this, dirty] { updateEnabled(); dirty(); });
    connect(m_waitSpin, &QSpinBox::valueChanged, this, dirty);
    connect(m_pollSpin, &QSpinBox::valueChanged, this, dirty);
    connect(m_gluetunCheck, &QCheckBox::toggled, this, [this, dirty] { updateEnabled(); dirty(); });
    connect(m_gluetunUrlEdit, &QLineEdit::textEdited, this, dirty);
    connect(m_gluetunKeyEdit, &QLineEdit::textEdited, this, dirty);
    connect(m_gluetunTargetCombo, &QComboBox::currentIndexChanged, this, dirty);
    connect(m_addBtn, &QPushButton::clicked, this, [this] { editInstance(-1); });
    connect(m_editBtn, &QPushButton::clicked, this, [this] {
        editInstance(m_table->indexOfTopLevelItem(m_table->currentItem()));
    });
    connect(m_table, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        editInstance(m_table->indexOfTopLevelItem(item));
    });
    connect(m_removeBtn, &QPushButton::clicked, this, [this] {
        const int row = m_table->indexOfTopLevelItem(m_table->currentItem());
        if (row < 0 || row >= m_instances.size())
            return;
        m_instances.removeAt(row);
        fillTable();
        emit changed();
    });
    connect(m_table, &QTreeWidget::currentItemChanged, this, &IntegrationsPage::updateEnabled);
    updateEnabled();
}

void IntegrationsPage::load(const QCborMap& prefs)
{
    std::vector<QSignalBlocker> blockers;
    for (QObject* o : std::initializer_list<QObject*>{m_arrCheck, m_waitSpin, m_pollSpin, m_gluetunCheck,
                                                      m_gluetunUrlEdit, m_gluetunKeyEdit, m_gluetunTargetCombo})
        blockers.emplace_back(o);

    m_webPort = int(prefs.value(QStringLiteral("webServerPort")).toInteger(4711));
    m_arrCheck->setChecked(prefs.value(QStringLiteral("arrApiEnabled")).toBool());
    m_waitSpin->setValue(int(prefs.value(QStringLiteral("arrSearchWaitSeconds")).toInteger(30)));
    m_pollSpin->setValue(int(prefs.value(QStringLiteral("arrWantedPollMinutes")).toInteger(60)));
    m_instances = arrInstancesFromCbor(prefs.value(QStringLiteral("arrInstances")).toArray());
    fillTable();

    m_gluetunCheck->setChecked(prefs.value(QStringLiteral("gluetunEnabled")).toBool());
    m_gluetunUrlEdit->setText(prefs.value(QStringLiteral("gluetunUrl")).toString());
    m_gluetunKeyEdit->setText(prefs.value(QStringLiteral("gluetunApiKey")).toString());
    m_gluetunTargetCombo->setCurrentIndex(
        prefs.value(QStringLiteral("gluetunPortTarget")).toString() == QLatin1StringView("bt") ? 1 : 0);
    updateHint();
    updateEnabled();
}

void IntegrationsPage::appendTo(IpcMessage& req) const
{
    const auto put = [&req](const char* key, const QCborValue& v) {
        req.append(QString::fromLatin1(key));
        req.append(v);
    };
    put("arrApiEnabled", m_arrCheck->isChecked());
    put("arrSearchWaitSeconds", qint64(m_waitSpin->value()));
    put("arrWantedPollMinutes", qint64(m_pollSpin->value()));
    QCborArray list;
    for (const ArrInstance& a : m_instances) {
        list.append(QCborMap{{QStringLiteral("name"), a.name}, {QStringLiteral("kind"), a.kind},
                             {QStringLiteral("url"), a.url}, {QStringLiteral("apiKey"), a.apiKey},
                             {QStringLiteral("enabled"), a.enabled}});
    }
    put("arrInstances", list);
    put("gluetunEnabled", m_gluetunCheck->isChecked());
    put("gluetunUrl", m_gluetunUrlEdit->text().trimmed());
    put("gluetunApiKey", m_gluetunKeyEdit->text().trimmed());
    put("gluetunPortTarget", m_gluetunTargetCombo->currentData().toString());
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void IntegrationsPage::fillTable()
{
    m_table->clear();
    for (const ArrInstance& a : std::as_const(m_instances)) {
        auto* item = new QTreeWidgetItem(m_table, {a.name, a.isRadarr() ? QStringLiteral("Radarr")
                                                                         : QStringLiteral("Sonarr"), a.url});
        if (!a.enabled)
            item->setForeground(0, palette().color(QPalette::Disabled, QPalette::Text));
    }
    updateEnabled();
}

void IntegrationsPage::editInstance(int row)
{
    const bool adding = row < 0 || row >= m_instances.size();
    if (adding && m_instances.size() >= kMaxInstances)
        return;
    ArrInstanceDialog dialog(adding ? ArrInstance{} : m_instances.at(row), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (adding)
        m_instances.append(dialog.instance());
    else
        m_instances[row] = dialog.instance();
    fillTable();
    emit changed();
}

void IntegrationsPage::updateEnabled()
{
    const bool arr = m_arrCheck->isChecked();
    for (QWidget* w : std::initializer_list<QWidget*>{m_waitSpin, m_pollSpin, m_table, m_addBtn, m_arrHint})
        w->setEnabled(arr);
    const bool selected = arr && m_table->currentItem() != nullptr;
    m_editBtn->setEnabled(selected);
    m_removeBtn->setEnabled(selected);
    m_addBtn->setEnabled(arr && m_instances.size() < kMaxInstances);

    const bool gt = m_gluetunCheck->isChecked();
    for (QWidget* w : std::initializer_list<QWidget*>{m_gluetunUrlEdit, m_gluetunKeyEdit, m_gluetunTargetCombo})
        w->setEnabled(gt);
}

void IntegrationsPage::updateHint()
{
    m_arrHint->setText(
        tr("Download client: qBittorrent, host of this computer, port %1, any user name, password "
           "= the REST API key (Web Interface page).\nIndexer: Torznab, URL http://<host>:%1/torznab, "
           "API key = the same key.")
            .arg(m_webPort));
}

} // namespace eMule
