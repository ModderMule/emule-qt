#include "pch.h"
/// @file Ed3kLinkMenu.cpp
/// @brief "Copy ed3k Links" / "ed3k Links..." — implementation.

#include "utils/Ed3kLinkMenu.h"

#include "app/IpcClient.h"
#include "dialogs/RememberedSizeDialog.h"
#include "IpcMessage.h"
#include "IpcProtocol.h"
#include "prefs/Preferences.h"
#include "utils/Clipboard.h"
#include "utils/DialogSizing.h"
#include "utils/MenuUtils.h"

#include <QAction>
#include <QCborArray>
#include <QCborMap>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

#include <functional>
#include <memory>

namespace eMule::Ed3kLinkMenu {

namespace {

/// the translation context (lupdate reads Q_DECLARE_TR_FUNCTIONS)
struct Words {
    Q_DECLARE_TR_FUNCTIONS(Ed3kLinkMenu)
};

/// Own size key: one class, not every plain RememberedSizeDialog
class LinksDialog : public RememberedSizeDialog {
public:
    using RememberedSizeDialog::RememberedSizeDialog;

protected:
    [[nodiscard]] QString sizeKey() const override { return QStringLiteral("Ed3kLinksDialog"); }
};

/// The links of @p files, the ones without a root left out; @p apply runs on @p owner's thread
/// (also with none: the caller greys or reports).
void request(IpcClient* ipc, QObject* owner, const QList<File>& files, std::function<void(const QStringList&)> apply)
{
    QCborArray items;
    for (const File& f : files.mid(0, Ipc::MaxEd2kLinkBatch))
        items.append(QCborMap{{QStringLiteral("h"), f.hash}, {QStringLiteral("n"), f.name}, {QStringLiteral("s"), f.size}});
    Ipc::IpcMessage req(Ipc::IpcMsgType::GetEd3kFileLinks);
    req.append(items);
    ipc->sendRequest(std::move(req), [owner = QPointer(owner), apply = std::move(apply)](const Ipc::IpcMessage& resp) {
        if (!owner || resp.type() != Ipc::IpcMsgType::Result || !resp.fieldBool(0))
            return;
        QStringList links;
        for (const auto& v : resp.fieldMap(1).value(QStringLiteral("links")).toArray())
            if (const QString link = v.toString(); !link.isEmpty())
                links << link;
        apply(links);
    });
}

void showDialog(QWidget* parent, const QStringList& links)
{
    auto* dlg = new LinksDialog(parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(Words::tr("ed3k Links"));
    auto* layout = new QVBoxLayout(dlg);
    auto* text = new QPlainTextEdit(links.join(QLatin1Char('\n')), dlg);
    text->setReadOnly(true);
    text->setLineWrapMode(QPlainTextEdit::NoWrap);
    layout->addWidget(text);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    auto* copy = buttons->addButton(Words::tr("Copy to Clipboard"), QDialogButtonBox::ActionRole);
    QObject::connect(copy, &QPushButton::clicked, dlg, [text] { Clipboard::setText(text->toPlainText()); });
    QObject::connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    layout->addWidget(buttons);
    DialogSizing::applySize(dlg, QSize(420, 180), QSize(640, 260));
    dlg->show();
}

} // namespace

QString copyText()
{
    return Words::tr("Copy ed3k Links");
}

QString showText()
{
    return Words::tr("ed3k Links...");
}

void addActions(QMenu* menu, QWidget* parent, IpcClient* ipc, const QList<File>& files)
{
    if (!thePrefs.ed3kEnabled())
        return;
    // grey until the core says a selected file has a root; the links asked once, on open
    auto links = std::make_shared<QStringList>();
    QPointer<QAction> copy = menu->addAction(menuIcon("Ed3k.ico"), copyText(), parent, [links] {
        Clipboard::setText(links->join(QLatin1Char('\n')));
    });
    QPointer<QAction> show = menu->addAction(menuIcon("Ed3k.ico"), showText(), parent, [parent, links] {
        showDialog(parent, *links);
    });
    copy->setEnabled(false);
    show->setEnabled(false);
    if (files.isEmpty() || !ipc || !ipc->isConnected())
        return;
    request(ipc, parent, files, [links, copy, show](const QStringList& got) {
        *links = got;
        for (const QPointer<QAction>& act : {copy, show})
            if (act)
                act->setEnabled(!got.isEmpty());
    });
}

} // namespace eMule::Ed3kLinkMenu
