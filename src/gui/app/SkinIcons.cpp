#include "pch.h"
/// @file SkinIcons.cpp
/// @brief Skin profile [Icons] for the built-in icons.

#include "app/SkinIcons.h"

#include <QFileInfo>
#include <QIconEngine>
#include <QLabel>
#include <QPainter>

namespace eMule {

using namespace Qt::StringLiterals;

namespace {

/// Icon file → resource names, from srchybrid/emule.rc (files lower-cased)
const QHash<QString, QStringList>& rcNames()
{
    static const QHash<QString, QStringList> names{
        {u"archivepreview.ico"_s, {u"ARCHIVE_PREVIEW"_s}},
        {u"browse8.ico"_s, {u"BROWSEFOLDERSMALL"_s}},
        {u"cancel.ico"_s, {u"CANCEL"_s}},
        {u"category.ico"_s, {u"CATEGORY"_s}},
        {u"chat.ico"_s, {u"CHAT"_s}},
        {u"client0.ico"_s, {u"SRCDOWNLOADING"_s}},
        {u"client1.ico"_s, {u"SRCCONNECTING"_s}},
        {u"client2.ico"_s, {u"SRCONQUEUE"_s}},
        {u"client3.ico"_s, {u"SRCNNPQF"_s}},
        {u"client4.ico"_s, {u"SRCUNKNOWN"_s}},
        {u"clientamule.ico"_s, {u"CLIENTAMULE"_s}},
        {u"clientamuleplus.ico"_s, {u"CLIENTAMULEPLUS"_s}},
        {u"clientcompatible.ico"_s, {u"CLIENTCOMPATIBLE"_s}},
        {u"clientcompatibleplus.ico"_s, {u"CLIENTCOMPATIBLEPLUS"_s}},
        {u"clientdefault.ico"_s, {u"CLIENTEDONKEY"_s}},
        {u"clientdefaultplus.ico"_s, {u"CLIENTEDONKEYPLUS"_s}},
        {u"clientlphant.ico"_s, {u"CLIENTLPHANT"_s}},
        {u"clientlphantplus.ico"_s, {u"CLIENTLPHANTPLUS"_s}},
        {u"clientmldonkey.ico"_s, {u"CLIENTMLDONKEY"_s}},
        {u"clientmldonkeyplus.ico"_s, {u"CLIENTMLDONKEYPLUS"_s}},
        {u"clientsecurehashovl.ico"_s, {u"CLIENTSECUREOVL"_s}},
        {u"clientshareaza.ico"_s, {u"CLIENTSHAREAZA"_s}},
        {u"clientshareazaplus.ico"_s, {u"CLIENTSHAREAZAPLUS"_s}},
        {u"clientedonkeyhybrid.ico"_s, {u"CLIENTEDONKEYHYBRID"_s}},
        {u"clientedonkeyhybridplus.ico"_s, {u"CLIENTEDONKEYHYBRIDPLUS"_s}},
        {u"clientobfuovl.ico"_s, {u"OVERLAYOBFU"_s}},
        {u"clientsknown.ico"_s, {u"CLIENTSKNOWN"_s}},
        {u"clientsonqueue.ico"_s, {u"CLIENTSONQUEUE"_s}},
        {u"clientsecurehasobfuhovl.ico"_s, {u"OVERLAYSECUREOBFU"_s}},
        {u"closetab.ico"_s, {u"CLOSETAB"_s}},
        {u"closetabselected.ico"_s, {u"CLOSETABSELECTED"_s}},
        {u"collapse.ico"_s, {u"COLLAPSE"_s}},
        {u"connectdo.ico"_s, {u"CONNECT"_s}},
        {u"connectdrop.ico"_s, {u"DISCONNECT"_s}},
        {u"connectstop.ico"_s, {u"STOPCONNECTING"_s}},
        {u"connectedhighhigh.ico"_s, {u"CONNECTEDHIGHHIGH"_s}},
        {u"connectedhighlow.ico"_s, {u"CONNECTEDHIGHLOW"_s}},
        {u"connectedhighnot.ico"_s, {u"CONNECTEDHIGHNOT"_s}},
        {u"connectedlowhigh.ico"_s, {u"CONNECTEDLOWHIGH"_s}},
        {u"connectedlowlow.ico"_s, {u"CONNECTEDLOWLOW"_s}},
        {u"connectedlownot.ico"_s, {u"CONNECTEDLOWNOT"_s}},
        {u"connectednothigh.ico"_s, {u"CONNECTEDNOTHIGH"_s}},
        {u"connectednotlow.ico"_s, {u"CONNECTEDNOTLOW"_s}},
        {u"connectednotnot.ico"_s, {u"CONNECTEDNOTNOT"_s}},
        {u"connection.ico"_s, {u"CONNECTION"_s}},
        {u"contact0.ico"_s, {u"CONTACT0"_s}},
        {u"contact1.ico"_s, {u"CONTACT1"_s}},
        {u"contact2.ico"_s, {u"CONTACT2"_s}},
        {u"contact3.ico"_s, {u"CONTACT3"_s}},
        {u"contact4.ico"_s, {u"CONTACT4"_s}},
        {u"contentdb.ico"_s, {u"SEARCHMETHOD_CONTENTDB"_s}},
        {u"convert.ico"_s, {u"CONVERT"_s}},
        {u"copy.ico"_s, {u"COPY"_s}},
        {u"copyselected.ico"_s, {u"COPYSELECTED"_s}},
        {u"copyvisible.ico"_s, {u"COPYVISIBLE"_s}},
        {u"delete.ico"_s, {u"DELETE"_s}},
        {u"deleteall.ico"_s, {u"CLEARCOMPLETE"_s}},
        {u"deleteselected.ico"_s, {u"DELETESELECTED"_s}},
        {u"display.ico"_s, {u"DISPLAY"_s}},
        {u"download.ico"_s, {u"DOWNLOAD"_s}},
        {u"downloadfiles.ico"_s, {u"DOWNLOADFILES"_s}},
        {u"email.ico"_s, {u"EMAIL"_s}},
        {u"empty.ico"_s, {u"EMPTY"_s}},
        {u"exit.ico"_s, {u"EXIT"_s}},
        {u"expandall.ico"_s, {u"EXPANDALL"_s}},
        {u"expandall2.ico"_s, {u"EXPANDMAIN"_s}},
        {u"exportall.ico"_s, {u"EXPORTALL"_s}},
        {u"filecomments.ico"_s, {u"FILECOMMENTS"_s}},
        {u"filecommentsovl.ico"_s, {u"FILECOMMENTSOVL"_s}},
        {u"fileimportparts.ico"_s, {u"FILEIMPORTPARTS"_s}},
        {u"fileinfo.ico"_s, {u"FILEINFO"_s}},
        {u"fileopen.ico"_s, {u"OPENFILE"_s}},
        {u"filepriority.ico"_s, {u"FILEPRIORITY"_s}},
        {u"filerating0.ico"_s, {u"RATING_NOTRATED"_s}},
        {u"filerating1.ico"_s, {u"RATING_FAKE"_s}},
        {u"filerating2.ico"_s, {u"RATING_POOR"_s}},
        {u"filerating3.ico"_s, {u"RATING_FAIR"_s}},
        {u"filerating4.ico"_s, {u"RATING_GOOD"_s}},
        {u"filerating5.ico"_s, {u"RATING_EXCELLENT"_s}},
        {u"filerename.ico"_s, {u"FILERENAME"_s, u"RENAME"_s}},
        {u"filesharedkad.ico"_s, {u"FILESHAREDKAD"_s}},
        {u"filesharedserver.ico"_s, {u"FILESHAREDSERVER"_s}},
        {u"filetypeany.ico"_s, {u"SEARCHFILETYPE_ANY"_s}},
        {u"filetypearchive.ico"_s, {u"SEARCHFILETYPE_ARCHIVE"_s}},
        {u"filetypeaudio.ico"_s, {u"SEARCHFILETYPE_AUDIO"_s}},
        {u"filetypecdimage.ico"_s, {u"SEARCHFILETYPE_CDIMAGE"_s}},
        {u"filetypedocument.ico"_s, {u"SEARCHFILETYPE_DOCUMENT"_s}},
        {u"filetypepicture.ico"_s, {u"SEARCHFILETYPE_PICTURE"_s}},
        {u"filetypeprogram.ico"_s, {u"SEARCHFILETYPE_PROGRAM"_s}},
        {u"filetypevideo.ico"_s, {u"SEARCHFILETYPE_VIDEO"_s}},
        {u"folderopen.ico"_s, {u"OPENFOLDER"_s}},
        {u"folders.ico"_s, {u"FOLDERS"_s}},
        {u"friend.ico"_s, {u"FRIEND"_s}},
        {u"friendslot.ico"_s, {u"FRIENDSLOT"_s}},
        {u"friends1.ico"_s, {u"FRIENDNOCLIENT"_s}},
        {u"friends2.ico"_s, {u"FRIENDWITHCLIENT"_s}},
        {u"friends3.ico"_s, {u"FRIENDCONNECTED"_s}},
        {u"global.ico"_s, {u"SEARCHMETHOD_GLOBAL"_s}},
        {u"harddisk.ico"_s, {u"HARDDISK"_s}},
        {u"help.ico"_s, {u"HELP"_s}},
        {u"ipfilter.ico"_s, {u"IPFILTER"_s}},
        {u"irc.ico"_s, {u"IRC"_s}},
        {u"ircclipboard.ico"_s, {u"IRCCLIPBOARD"_s}},
        {u"incoming.ico"_s, {u"INCOMING"_s}},
        {u"info.ico"_s, {u"INFO"_s}},
        {u"kad.ico"_s, {u"KADEMLIA"_s}},
        {u"kadbootstrap.ico"_s, {u"KADBOOTSTRAP"_s}},
        {u"kadcontactlist.ico"_s, {u"KADCONTACTLIST"_s}},
        {u"kadcurrentsearches.ico"_s, {u"KADCURRENTSEARCHES"_s}},
        {u"kadfilesearch.ico"_s, {u"KADFILESEARCH"_s}},
        {u"kadnodesearch.ico"_s, {u"KADNODESEARCH"_s}},
        {u"kadserver.ico"_s, {u"SEARCHMETHOD_KADSERVER"_s}},
        {u"kadstorefile.ico"_s, {u"KADSTOREFILE"_s}},
        {u"kadstoreword.ico"_s, {u"KADSTOREWORD"_s}},
        {u"kadwordsearch.ico"_s, {u"KADWORDSEARCH"_s}},
        {u"listadd.ico"_s, {u"LISTADD"_s}},
        {u"listremove.ico"_s, {u"LISTREMOVE"_s}},
        {u"log.ico"_s, {u"LOG"_s}},
        {u"mediainfo.ico"_s, {u"MEDIAINFO"_s}},
        {u"message.ico"_s, {u"MESSAGE"_s}},
        {u"messagepending.ico"_s, {u"MESSAGEPENDING"_s}},
        {u"messages.ico"_s, {u"MESSAGES"_s}},
        {u"metadata.ico"_s, {u"METADATA"_s}},
        {u"mule_vista.ico"_s, {u"AAAEMULEAPP"_s}},
        {u"navigateback.ico"_s, {u"BACK"_s}},
        {u"navigateforward.ico"_s, {u"FORWARD"_s}},
        {u"noaccessfolderovl.ico"_s, {u"NOACCESSFOLDEROVL"_s}},
        {u"notifications.ico"_s, {u"NOTIFICATIONS"_s}},
        {u"pause.ico"_s, {u"PAUSE"_s}},
        {u"preferences.ico"_s, {u"PREFERENCES"_s}},
        {u"preview.ico"_s, {u"PREVIEW"_s}},
        {u"priority.ico"_s, {u"PRIORITY"_s}},
        {u"proxy.ico"_s, {u"PROXY"_s}},
        {u"records.ico"_s, {u"STATSRECORDS"_s}},
        {u"restore.ico"_s, {u"RESTORE"_s}},
        {u"restorewindow.ico"_s, {u"RESTOREWINDOW"_s}},
        {u"scheduler.ico"_s, {u"SCHEDULER"_s}},
        {u"search.ico"_s, {u"SEARCH"_s}},
        {u"searchedit.ico"_s, {u"SEARCHEDIT"_s}},
        {u"searchglobalactive.ico"_s, {u"SEARCHMETHOD_GLOBALACTIVE"_s}},
        {u"searchkad.ico"_s, {u"SEARCHMETHOD_KADEMLIA"_s}},
        {u"searchkadactive.ico"_s, {u"SEARCHMETHOD_KADEMLIAACTIVE"_s}},
        {u"searchparams.ico"_s, {u"SEARCHPARAMS"_s}},
        {u"searchresults.ico"_s, {u"SEARCHRESULTS"_s}},
        {u"searchserveractive.ico"_s, {u"SEARCHMETHOD_SERVERACTIVE"_s}},
        {u"security.ico"_s, {u"SECURITY"_s}},
        {u"server.ico"_s, {u"SERVER"_s}},
        {u"serveradd.ico"_s, {u"ADDSERVER"_s}},
        {u"serverinfo.ico"_s, {u"SERVERINFO"_s}},
        {u"serverlist.ico"_s, {u"SERVERLIST"_s}},
        {u"serversearch.ico"_s, {u"SEARCHMETHOD_SERVER"_s}},
        {u"serversupdate.ico"_s, {u"SERVERUPDATEMET"_s}},
        {u"sharedfiles.ico"_s, {u"SHAREDFILES"_s}},
        {u"sharedfileslist.ico"_s, {u"SHAREDFILESLIST"_s}},
        {u"sharedfolderovl.ico"_s, {u"SHAREDFOLDEROVL"_s}},
        {u"smiley_cry.ico"_s, {u"SMILEY_CRY"_s}},
        {u"smiley_disgust.ico"_s, {u"SMILEY_DISGUST"_s}},
        {u"smiley_happy.ico"_s, {u"SMILEY_HAPPY"_s}},
        {u"smiley_interest.ico"_s, {u"SMILEY_INTEREST"_s}},
        {u"smiley_laugh.ico"_s, {u"SMILEY_LAUGH"_s}},
        {u"smiley_love.ico"_s, {u"SMILEY_LOVE"_s}},
        {u"smiley_ph34r.ico"_s, {u"SMILEY_PH34R"_s}},
        {u"smiley_sad.ico"_s, {u"SMILEY_SAD"_s}},
        {u"smiley_sealed.ico"_s, {u"SMILEY_SEALED"_s}},
        {u"smiley_skeptic.ico"_s, {u"SMILEY_SKEPTIC"_s}},
        {u"smiley_smile.ico"_s, {u"SMILEY_SMILE"_s}},
        {u"smiley_tongue.ico"_s, {u"SMILEY_TONGUE"_s}},
        {u"smiley_wink.ico"_s, {u"SMILEY_WINK"_s}},
        {u"smiley_lookside.ico"_s, {u"SMILEY_LOOKSIDE"_s}},
        {u"smiley_omg.ico"_s, {u"SMILEY_OMG"_s}},
        {u"smiley_sadq.ico"_s, {u"SMILEY_SADQ"_s}},
        {u"smiley_smileq.ico"_s, {u"SMILEY_SMILEQ"_s}},
        {u"spam.ico"_s, {u"SPAM"_s}},
        {u"speed.ico"_s, {u"SPEED"_s}},
        {u"speedmax.ico"_s, {u"SPEEDMAX"_s}},
        {u"speedmin.ico"_s, {u"SPEEDMIN"_s}},
        {u"splitwindow.ico"_s, {u"SPLITWINDOW"_s}},
        {u"start.ico"_s, {u"RESUME"_s}},
        {u"statistics.ico"_s, {u"STATISTICS"_s}},
        {u"statisticsdetail.ico"_s, {u"STATSDETAIL"_s}},
        {u"statscumulative.ico"_s, {u"STATSCUMULATIVE"_s}},
        {u"statsday.ico"_s, {u"STATSDAY"_s}},
        {u"statsgeneric.ico"_s, {u"STATSGENERIC"_s}},
        {u"statsmonth.ico"_s, {u"STATSMONTH"_s}},
        {u"statsprojected.ico"_s, {u"STATSPROJECTED"_s}},
        {u"statstime.ico"_s, {u"STATSTIME"_s}},
        {u"statsyear.ico"_s, {u"STATSYEAR"_s}},
        {u"stop.ico"_s, {u"STOP"_s}},
        {u"tools.ico"_s, {u"TOOLS"_s}},
        {u"transfer.ico"_s, {u"TRANSFER"_s}},
        {u"transferupdown.ico"_s, {u"TRANSFERUPDOWN"_s}},
        {u"trayconnected.ico"_s, {u"TRAYCONNECTED"_s}},
        {u"traydisconnected.ico"_s, {u"TRAYNOTCONNECTED"_s}},
        {u"traylowid.ico"_s, {u"TRAYLOWID"_s}},
        {u"tweak.ico"_s, {u"TWEAK"_s}},
        {u"up0down0.ico"_s, {u"UP0DOWN0"_s}},
        {u"up0down1.ico"_s, {u"UP0DOWN1"_s}},
        {u"up1down0.ico"_s, {u"UP1DOWN0"_s}},
        {u"up1down1.ico"_s, {u"UP1DOWN1"_s}},
        {u"upload.ico"_s, {u"UPLOAD"_s}},
        {u"user.ico"_s, {u"STATSCLIENTS"_s}},
        {u"useradd.ico"_s, {u"ADDFRIEND"_s}},
        {u"userdelete.ico"_s, {u"DELETEFRIEND"_s}},
        {u"userdetails.ico"_s, {u"CLIENTDETAILS"_s}},
        {u"userfiles.ico"_s, {u"VIEWFILES"_s}},
        {u"usermessage.ico"_s, {u"SENDMESSAGE"_s}},
        {u"web.ico"_s, {u"WEB"_s}},
        {u"wizard.ico"_s, {u"WIZARD"_s}},
        {u"all.ico"_s, {u"ALLFILES"_s}},
        {u"bold.ico"_s, {u"BOLD"_s}},
        {u"clear1.ico"_s, {u"FILTERCLEAR1"_s}},
        {u"clear2.ico"_s, {u"FILTERCLEAR2"_s}},
        {u"colour.ico"_s, {u"COLOUR"_s}},
        {u"ed2klink.ico"_s, {u"ED2KLINK"_s}},
        {u"ed2klinkpaste.ico"_s, {u"PASTELINK"_s}},
        {u"emulecolladd.ico"_s, {u"COLLECTION_ADD"_s}},
        {u"emulecolledit.ico"_s, {u"COLLECTION_EDIT"_s}},
        {u"emulecollsearch.ico"_s, {u"COLLECTION_SEARCH"_s}},
        {u"emulecollview.ico"_s, {u"COLLECTION_VIEW"_s}},
        {u"emulecollectionfiletype.ico"_s, {u"AABCOLLECTIONFILETYPE"_s}},
        {u"incomplete.ico"_s, {u"INCOMPLETE"_s}},
        {u"italic.ico"_s, {u"ITALIC"_s}},
        {u"resetformat.ico"_s, {u"RESETFORMAT"_s}},
        {u"underline.ico"_s, {u"UNDERLINE"_s}},
    };
    return names;
}

/// Looks the skin up on use; refreshes when the profile changes.
class SkinIconEngine final : public QIconEngine {
public:
    explicit SkinIconEngine(QString path) : m_path(std::move(path)) {}

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override
    {
        icon().paint(painter, rect, Qt::AlignCenter, mode, state);
    }
    QSize actualSize(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        return icon().actualSize(size, mode, state);
    }
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        return icon().pixmap(size, 1.0, mode, state);
    }
    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        return icon().pixmap(size, scale, mode, state);
    }
    QList<QSize> availableSizes(QIcon::Mode mode, QIcon::State state) override
    {
        return icon().availableSizes(mode, state);
    }
    bool isNull() override { return icon().isNull(); }
    QString key() const override { return u"eMuleSkinIcon"_s; }
    QIconEngine* clone() const override { return new SkinIconEngine(m_path); }

private:
    QString m_path;
    QIcon m_icon;
    quint64 m_generation = ~quint64{0};

    const QIcon& icon()
    {
        auto& skin = SkinIcons::instance();
        if (m_generation != skin.generation()) {
            m_icon = skin.resolve(m_path);
            m_generation = skin.generation();
        }
        return m_icon;
    }
};

} // namespace

QIcon skinIcon(const QString& resourcePath)
{
    // a file on disk is no built-in icon
    if (!resourcePath.startsWith(u":/"))
        return QIcon(resourcePath);
    return QIcon(new SkinIconEngine(resourcePath));
}

void setSkinPixmap(QLabel* label, const QString& resourcePath, int size)
{
    if (!label)
        return;
    const auto apply = [label, resourcePath, size] {
        label->setPixmap(skinIcon(resourcePath).pixmap(size, size));
    };
    apply();
    QObject::connect(&SkinIcons::instance(), &SkinIcons::changed, label, apply);
}

SkinIcons& SkinIcons::instance()
{
    static SkinIcons skin;
    return skin;
}

bool SkinIcons::setFiles(const QHash<QString, QString>& files)
{
    if (files == m_files)
        return false;
    m_files = files;
    m_cache.clear();
    ++m_generation;
    emit changed();
    return true;
}

QString SkinIcons::file(const QString& resourcePath) const
{
    if (m_files.isEmpty())
        return {};
    for (const QString& name : resourceNames(resourcePath)) {
        if (const auto it = m_files.constFind(name.toLower()); it != m_files.cend())
            return it.value();
    }
    return {};
}

QIcon SkinIcons::resolve(const QString& resourcePath)
{
    if (const auto it = m_cache.constFind(resourcePath); it != m_cache.cend())
        return it.value();
    const QString skinned = file(resourcePath);
    const QIcon icon(skinned.isEmpty() ? resourcePath : skinned);
    m_cache.insert(resourcePath, icon);
    return icon;
}

bool SkinIcons::changedSince(quint64& seen) const
{
    if (seen == m_generation)
        return false;
    seen = m_generation;
    return true;
}

QStringList SkinIcons::resourceNames(const QString& resourcePath)
{
    const QFileInfo info(resourcePath);
    const auto it = rcNames().constFind(info.fileName().toLower());
    return it != rcNames().cend() ? it.value() : QStringList{info.completeBaseName()};
}

} // namespace eMule
