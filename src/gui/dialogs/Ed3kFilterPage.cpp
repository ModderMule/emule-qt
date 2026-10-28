/// @file Ed3kFilterPage.cpp
/// @brief Options → ed3k → Filters — implementation.

#include "pch.h"
#include "dialogs/Ed3kFilterPage.h"

#include "app/IpcClient.h"
#include "IpcMessage.h"

#include <QApplication>
#include <QCborArray>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace eMule {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

/// ED3K_FILTER_OWN: the own list's handle
constexpr qint64 kOwnList = 0;

QTreeWidget* makeTree(const QStringList& headers, QWidget* parent, int rows)
{
    auto* t = new QTreeWidget(parent);
    t->setHeaderLabels(headers);
    t->setRootIsDecorated(false);
    t->setUniformRowHeights(true);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->header()->setStretchLastSection(true);
    t->setFixedHeight(t->fontMetrics().lineSpacing() * rows);
    return t;
}

QComboBox* modeCombo(QWidget* parent)
{
    auto* c = new QComboBox(parent);
    for (int mode : {0, 1, 3})
        c->addItem(Ed3kFilterPage::modeText(mode));
    return c;
}

QTreeWidgetItem* selected(QTreeWidget* t)
{
    const auto items = t->selectedItems();
    return items.isEmpty() ? nullptr : items.first();
}

} // namespace

Ed3kFilterPage::Ed3kFilterPage(IpcClient* ipc, QWidget* parent)
    : QWidget(parent)
    , m_ipc(ipc)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    m_note = new QLabel(tr("Changes on this page take effect at once."), this);
    m_note->setWordWrap(true);
    layout->addWidget(m_note);

    // -- followed lists (§2): only those the user chose (§3)
    auto* listGroup = new QGroupBox(tr("Followed filter lists"), this);
    auto* listLayout = new QVBoxLayout(listGroup);
    auto* listNote = new QLabel(tr("A list marks fake, harmful or spam files in search results; whether it also "
                                   "hides them or refuses their download is set per list."),
                                listGroup);
    listNote->setWordWrap(true);
    listLayout->addWidget(listNote);
    m_lists = makeTree({tr("Label"), tr("Mode"), tr("Version"), tr("Files"), tr("Entries"), tr("Waiting")}, listGroup, 8);
    listLayout->addWidget(m_lists);
    auto* listRow = new QHBoxLayout;
    auto* followBtn = new QPushButton(tr("Follow…"), listGroup);
    auto* unfollowBtn = new QPushButton(tr("Unfollow"), listGroup);
    auto* openBtn = new QPushButton(tr("Open list file…"), listGroup);
    openBtn->setToolTip(tr("Uses a list file (.ed3kflt) by hand: it has no author and gets no updates."));
    m_listMode = modeCombo(listGroup);
    listRow->addWidget(followBtn);
    listRow->addWidget(unfollowBtn);
    listRow->addWidget(openBtn);
    listRow->addStretch();
    listLayout->addLayout(listRow);
    // the mode on a row of its own: the page fits an 800 px wide dialog
    auto* listModeRow = new QHBoxLayout;
    listModeRow->addWidget(new QLabel(tr("Mode of the selected list:"), listGroup));
    listModeRow->addWidget(m_listMode);
    listModeRow->addStretch();
    listLayout->addLayout(listModeRow);
    layout->addWidget(listGroup);

    // -- own verdicts (§2, §3): overrule every list
    auto* ownGroup = new QGroupBox(tr("Own verdicts (they overrule every list)"), this);
    auto* ownLayout = new QVBoxLayout(ownGroup);
    m_own = makeTree({tr("Entry"), tr("ID"), tr("Verdict"), tr("Reason")}, ownGroup, 7);
    ownLayout->addWidget(m_own);
    auto* ownRow = new QHBoxLayout;
    auto* addBtn = new QPushButton(tr("Add…"), ownGroup);
    auto* removeBtn = new QPushButton(tr("Remove"), ownGroup);
    m_ownMode = modeCombo(ownGroup);
    ownRow->addWidget(addBtn);
    ownRow->addWidget(removeBtn);
    ownRow->addSpacing(12);
    ownRow->addWidget(new QLabel(tr("Mode:"), ownGroup));
    ownRow->addWidget(m_ownMode);
    ownRow->addStretch();
    ownLayout->addLayout(ownRow);
    // §2: the own list published as a set others can follow
    auto* pubRow = new QHBoxLayout;
    m_publish = new QPushButton(tr("Publish my list…"), ownGroup);
    m_publish->setToolTip(tr("Shares your own verdicts as a filter list others can follow; published again on every change."));
    m_unpublish = new QPushButton(tr("Stop publishing"), ownGroup);
    m_pubLink = new QLineEdit(ownGroup);
    m_pubLink->setReadOnly(true);
    m_pubLink->setPlaceholderText(tr("not published"));
    m_copyLink = new QPushButton(tr("Copy link"), ownGroup);
    pubRow->addWidget(m_publish);
    pubRow->addWidget(m_unpublish);
    pubRow->addWidget(m_pubLink, 1);
    pubRow->addWidget(m_copyLink);
    ownLayout->addLayout(pubRow);
    layout->addWidget(ownGroup);
    layout->addStretch();

    connect(followBtn, &QPushButton::clicked, this, &Ed3kFilterPage::follow);
    connect(unfollowBtn, &QPushButton::clicked, this, &Ed3kFilterPage::unfollow);
    connect(openBtn, &QPushButton::clicked, this, &Ed3kFilterPage::openFile);
    connect(addBtn, &QPushButton::clicked, this, &Ed3kFilterPage::addOwn);
    connect(removeBtn, &QPushButton::clicked, this, &Ed3kFilterPage::removeOwn);
    connect(m_publish, &QPushButton::clicked, this, [this] { publishOwn(true); });
    connect(m_unpublish, &QPushButton::clicked, this, [this] { publishOwn(false); });
    connect(m_copyLink, &QPushButton::clicked, this, [this] {
        if (!m_pubLink->text().isEmpty())
            QApplication::clipboard()->setText(m_pubLink->text());
    });
    connect(m_listMode, &QComboBox::activated, this, [this] {
        if (auto* it = selected(m_lists))
            applyMode(m_listMode, it->data(0, Qt::UserRole).toLongLong());
    });
    connect(m_ownMode, &QComboBox::activated, this, [this] { applyMode(m_ownMode, kOwnList); });
    connect(m_lists, &QTreeWidget::itemSelectionChanged, this, [this] {
        if (auto* it = selected(m_lists)) {
            m_filling = true;
            m_listMode->setCurrentIndex(indexOfMode(it->data(1, Qt::UserRole).toInt()));
            m_filling = false;
        }
    });
    if (m_ipc)
        connect(m_ipc, &IpcClient::ed3kFiltersChanged, this, [this](const IpcMessage&) { refresh(); });
    refresh();
}

Ed3kFilterPage::~Ed3kFilterPage() = default;

void Ed3kFilterPage::fill(const QCborMap& f)
{
    m_filling = true;
    const bool running = f.value(QStringLiteral("running")).toBool();
    m_note->setText(running ? tr("Changes on this page take effect at once.")
                            : tr("The ed3k node is not running: filter lists can be changed while it runs."));
    const qint64 sel = selected(m_lists) ? selected(m_lists)->data(0, Qt::UserRole).toLongLong() : -1;
    m_lists->clear();
    for (const QCborValue& v : f.value(QStringLiteral("lists")).toArray()) {
        const QCborMap l = v.toMap();
        auto* it = new QTreeWidgetItem(m_lists);
        QString label = l.value(QStringLiteral("label")).toString();
        if (l.value(QStringLiteral("byHand")).toBool())
            label = tr("%1 (opened by hand: no author, no updates)").arg(label);
        it->setText(0, label);
        const int mode = int(l.value(QStringLiteral("mode")).toInteger());
        it->setText(1, modeText(mode));
        const qint64 seq = l.value(QStringLiteral("seq")).toInteger(-1);
        it->setText(2, seq < 0 ? tr("none yet") : QString::number(seq));
        it->setText(3, QString::number(l.value(QStringLiteral("files")).toInteger()));
        it->setText(4, QString::number(l.value(QStringLiteral("entries")).toInteger()));
        it->setText(5, QString::number(l.value(QStringLiteral("waiting")).toInteger()));
        it->setData(0, Qt::UserRole, l.value(QStringLiteral("id")).toInteger());
        it->setData(1, Qt::UserRole, mode);
        if (l.value(QStringLiteral("id")).toInteger() == sel)
            m_lists->setCurrentItem(it);
    }
    m_ownMode->setCurrentIndex(indexOfMode(int(f.value(QStringLiteral("ownMode")).toInteger())));
    m_own->clear();
    for (const QCborValue& v : f.value(QStringLiteral("own")).toArray()) {
        const QCborMap o = v.toMap();
        auto* it = new QTreeWidgetItem(m_own);
        const int subject = int(o.value(QStringLiteral("subject")).toInteger());
        it->setText(0, subjectText(subject));
        it->setText(1, o.value(QStringLiteral("id")).toString());
        it->setText(2, verdictText(int(o.value(QStringLiteral("verdict")).toInteger())));
        it->setText(3, o.value(QStringLiteral("reason")).toString());
        it->setData(0, Qt::UserRole, subject);
    }
    const QCborMap pub = f.value(QStringLiteral("published")).toMap();
    m_pubLink->setText(pub.value(QStringLiteral("link")).toString());
    m_pubTitle = pub.value(QStringLiteral("title")).toString();
    const bool published = !m_pubLink->text().isEmpty();
    m_publish->setText(published ? tr("Publish again…") : tr("Publish my list…"));
    m_publish->setEnabled(running);
    m_unpublish->setEnabled(running && published);
    m_copyLink->setEnabled(published);
    m_filling = false;
}

QString Ed3kFilterPage::verdictText(int verdict)
{
    switch (verdict) {
    case 1: return tr("Fake");
    case 2: return tr("Harmful");
    case 3: return tr("Spam");
    case 4: return tr("Good");
    default: return tr("None");
    }
}

QString Ed3kFilterPage::subjectText(int subject)
{
    switch (subject) {
    case 1: return tr("File (ed3k root)");
    case 2: return tr("File (eD2K hash)");
    case 3: return tr("Author (node ID)");
    default: return {};
    }
}

QString Ed3kFilterPage::modeText(int mode)
{
    if (mode & 2)
        return tr("Hide and refuse downloads");
    if (mode & 1)
        return tr("Hide");
    return tr("Mark only");
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kFilterPage::refresh()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;
    m_ipc->sendRequest(IpcMessage(IpcMsgType::GetEd3kFilters), [self = QPointer<Ed3kFilterPage>(this)](const IpcMessage& r) {
        if (self && r.type() == IpcMsgType::Result && r.fieldBool(0))
            self->fill(r.fieldMap(1));
    });
}

void Ed3kFilterPage::follow()
{
    if (!m_ipc)
        return;
    bool ok = false;
    const QString link = QInputDialog::getText(this, tr("Follow filter list"), tr("Link of the filter list (ed3k:set?…):"),
                                               QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || link.isEmpty())
        return;
    const QString label = QInputDialog::getText(this, tr("Follow filter list"), tr("Label:"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok)
        return;
    IpcMessage req(IpcMsgType::FollowEd3kFilter);
    req.append(link);
    req.append(label);
    m_ipc->sendRequest(std::move(req), [self = QPointer<Ed3kFilterPage>(this)](const IpcMessage& r) {
        if (self)
            self->failed(r, tr("Follow filter list"));
    });
}

void Ed3kFilterPage::unfollow()
{
    auto* it = selected(m_lists);
    if (!it || !m_ipc)
        return;
    IpcMessage req(IpcMsgType::UnfollowEd3kFilter);
    req.append(it->data(0, Qt::UserRole).toLongLong());
    m_ipc->sendRequest(std::move(req), [self = QPointer<Ed3kFilterPage>(this)](const IpcMessage& r) {
        if (self)
            self->failed(r, tr("Unfollow"));
    });
}

void Ed3kFilterPage::openFile()
{
    if (!m_ipc)
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open filter list file"), QString(),
                                                      tr("Filter lists (*.ed3kflt);;All files (*)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    constexpr qint64 kMax = 4 * 1024 * 1024;   // filter-list.md §2
    if (!f.open(QIODevice::ReadOnly) || f.size() > kMax) {
        QMessageBox::warning(this, tr("Open filter list file"), tr("Could not read %1 (at most 4 MiB).").arg(path));
        return;
    }
    IpcMessage req(IpcMsgType::OpenEd3kFilterFile);
    req.append(QFileInfo(path).completeBaseName());
    req.append(QCborValue(f.readAll()));
    m_ipc->sendRequest(std::move(req), [self = QPointer<Ed3kFilterPage>(this)](const IpcMessage& r) {
        if (self)
            self->failed(r, tr("Open filter list file"));
    });
}

void Ed3kFilterPage::applyMode(QComboBox* combo, qint64 id)
{
    if (m_filling || !m_ipc)
        return;
    const int mode = modeOfIndex(combo->currentIndex());
    IpcMessage req(IpcMsgType::SetEd3kFilterMode);
    req.append(id);
    req.append((mode & 1) != 0);
    req.append((mode & 2) != 0);
    m_ipc->sendRequest(std::move(req), [self = QPointer<Ed3kFilterPage>(this)](const IpcMessage& r) {
        if (self)
            self->failed(r, tr("Mode"));
    });
}

void Ed3kFilterPage::addOwn()
{
    if (!m_ipc)
        return;
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Own verdict"));
    auto* form = new QFormLayout;
    auto* subject = new QComboBox(&dlg);
    for (int s : {1, 2, 3})
        subject->addItem(subjectText(s), s);
    form->addRow(tr("Entry:"), subject);
    auto* id = new QLineEdit(&dlg);
    id->setPlaceholderText(tr("hexadecimal: a root (64 digits), an eD2K hash or a node ID (32 digits)"));
    form->addRow(tr("ID:"), id);
    auto* verdict = new QComboBox(&dlg);
    for (int v : {1, 2, 3, 4})
        verdict->addItem(verdictText(v), v);
    form->addRow(tr("Verdict:"), verdict);
    auto* reason = new QLineEdit(&dlg);
    form->addRow(tr("Reason:"), reason);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto* layout = new QVBoxLayout(&dlg);
    layout->addLayout(form);
    layout->addWidget(buttons);
    dlg.setMinimumWidth(dlg.fontMetrics().horizontalAdvance(QLatin1Char('0')) * 72);
    if (dlg.exec() != QDialog::Accepted)
        return;
    // a FILE entry is the first 16 bytes of the root (filter-list.md §1)
    static const QRegularExpression kHex(QStringLiteral("^([0-9a-fA-F]{32}|[0-9a-fA-F]{64})$"));
    QString hex = id->text().trimmed();
    if (!kHex.match(hex).hasMatch() || (hex.size() == 64 && subject->currentData().toInt() != 1)) {
        QMessageBox::warning(this, tr("Own verdict"), tr("The ID is 32 hexadecimal digits (a root: 64)."));
        return;
    }
    hex = hex.left(32).toLower();
    IpcMessage req(IpcMsgType::SetEd3kFilterOwn);
    req.append(qint64(subject->currentData().toInt()));
    req.append(hex);
    req.append(qint64(verdict->currentData().toInt()));
    req.append(reason->text().trimmed());
    m_ipc->sendRequest(std::move(req), [self = QPointer<Ed3kFilterPage>(this)](const IpcMessage& r) {
        if (self)
            self->failed(r, tr("Own verdict"));
    });
}

void Ed3kFilterPage::removeOwn()
{
    auto* it = selected(m_own);
    if (!it || !m_ipc)
        return;
    IpcMessage req(IpcMsgType::SetEd3kFilterOwn);
    req.append(qint64(it->data(0, Qt::UserRole).toInt()));
    req.append(it->text(1));
    req.append(qint64(0));   // None takes it back
    req.append(QString());
    m_ipc->sendRequest(std::move(req), [self = QPointer<Ed3kFilterPage>(this)](const IpcMessage& r) {
        if (self)
            self->failed(r, tr("Remove"));
    });
}

void Ed3kFilterPage::publishOwn(bool on)
{
    if (!m_ipc)
        return;
    QString title;
    if (on) {
        bool ok = false;
        title = QInputDialog::getText(this, tr("Publish my list"),
                                      tr("Title of your filter list (followers see it):"), QLineEdit::Normal,
                                      m_pubTitle.isEmpty() ? tr("My filter list") : m_pubTitle, &ok)
                    .trimmed();
        if (!ok || title.isEmpty())
            return;
    }
    IpcMessage req(IpcMsgType::PublishEd3kFilterOwn);
    req.append(title);
    req.append(on);
    m_ipc->sendRequest(std::move(req), [self = QPointer<Ed3kFilterPage>(this), on](const IpcMessage& r) {
        if (!self || self->failed(r, on ? tr("Publish my list") : tr("Stop publishing")))
            return;
        const QString link = r.fieldMap(1).value(QStringLiteral("link")).toString();
        self->m_pubLink->setText(link);
        if (on && !link.isEmpty()) {
            QApplication::clipboard()->setText(link);
            QMessageBox::information(self, tr("Publish my list"),
                                     tr("Your filter list is published. Its link is in the clipboard: whoever follows "
                                        "it sees your verdicts."));
        }
        self->refresh();
    });
}

bool Ed3kFilterPage::failed(const IpcMessage& reply, const QString& what)
{
    if (reply.type() == IpcMsgType::Result && reply.fieldBool(0))
        return false;
    QString why;
    if (reply.type() == IpcMsgType::Result) {
        const qint64 err = reply.fieldMap(1).value(QStringLiteral("error")).toInteger();
        why = err == 0 ? tr("the ed3k node is not running") : tr("error %1").arg(err);
    } else {
        why = reply.fieldString(1).isEmpty() ? tr("no answer from the core") : reply.fieldString(1);
    }
    QMessageBox::warning(this, tr("ed3k"), tr("%1 failed: %2").arg(what, why));
    return true;
}

int Ed3kFilterPage::modeOfIndex(int index)
{
    return index == 2 ? 3 : index == 1 ? 1 : 0;
}

int Ed3kFilterPage::indexOfMode(int mode)
{
    return (mode & 2) ? 2 : (mode & 1) ? 1 : 0;
}

} // namespace eMule
