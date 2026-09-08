#include "pch.h"
/// @file AbstractFile.cpp
/// @brief Abstract base class for all file types — replaces MFC CAbstractFile.

#include "files/AbstractFile.h"
#include "prefs/Preferences.h"
#include "protocol/ED2KLink.h"
#include "utils/Opcodes.h"
#include "utils/SettingsUtils.h"

#include <QObject>
#include <QStringList>


namespace eMule {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

AbstractFile::AbstractFile()
    : m_fileIdentifier(m_fileSize)
{
}

AbstractFile::AbstractFile(const AbstractFile& other)
    : m_tags(other.m_tags)
    , m_fileSize(other.m_fileSize)
    , m_fileIdentifier(other.m_fileIdentifier, m_fileSize)
    , m_fileName(other.m_fileName)
    , m_comment(other.m_comment)
    , m_fileType(other.m_fileType)
    , m_rating(other.m_rating)
    , m_userRating(other.m_userRating)
    , m_commentLoaded(other.m_commentLoaded)
    , m_hasComment(other.m_hasComment)
    , m_kadCommentSearchRunning(other.m_kadCommentSearchRunning)
{
}

AbstractFile& AbstractFile::operator=(const AbstractFile& other)
{
    if (this != &other) {
        m_tags = other.m_tags;
        m_fileSize = other.m_fileSize;
        // FileIdentifier has a reference member so can't be copy-assigned.
        // Destroy and reconstruct in place, binding to our own m_fileSize.
        m_fileIdentifier.~FileIdentifier();
        new (&m_fileIdentifier) FileIdentifier(other.m_fileIdentifier, m_fileSize);
        m_fileName = other.m_fileName;
        m_comment = other.m_comment;
        m_fileType = other.m_fileType;
        m_rating = other.m_rating;
        m_userRating = other.m_userRating;
        m_commentLoaded = other.m_commentLoaded;
        m_hasComment = other.m_hasComment;
        m_kadCommentSearchRunning = other.m_kadCommentSearchRunning;
    }
    return *this;
}

// ---------------------------------------------------------------------------
// Filename
// ---------------------------------------------------------------------------

void AbstractFile::setFileName(const QString& name,
                               bool replaceInvalidChars,
                               bool autoSetFileType,
                               bool removeControlChars)
{
    m_fileName = name;
    if (replaceInvalidChars) {
        static constexpr QChar badChars[] = {
            u'\"', u'*', u'<', u'>', u'?', u'|', u'\\', u'/', u':'
        };
        for (auto ch : badChars)
            m_fileName.replace(ch, QChar(u'-'));
    }
    if (autoSetFileType)
        setFileType(getFileTypeByName(m_fileName));
    if (removeControlChars) {
        for (qsizetype i = m_fileName.size(); --i >= 0;)
            if (m_fileName[i] < QChar(u' '))
                m_fileName.remove(i, 1);
    }
}

void AbstractFile::setFileType(const QString& type)
{
    m_fileType = type;
}

QString AbstractFile::fileTypeDisplayStr() const
{
    // Map ED2K protocol type strings to user-friendly localized display names.
    // Localization is better done by GUI
    // if (m_fileType == QLatin1String(ED2KFTSTR_AUDIO))
    //     return QObject::tr("Audio");
    // if (m_fileType == QLatin1String(ED2KFTSTR_VIDEO))
    //     return QObject::tr("Video");
    // if (m_fileType == QLatin1String(ED2KFTSTR_IMAGE))
    //     return QObject::tr("Image");
    // if (m_fileType == QLatin1String(ED2KFTSTR_DOCUMENT))
    //     return QObject::tr("Document");
    // if (m_fileType == QLatin1String(ED2KFTSTR_PROGRAM))
    //     return QObject::tr("Program");
    // if (m_fileType == QLatin1String(ED2KFTSTR_ARCHIVE))
    //     return QObject::tr("Archive");
    // if (m_fileType == QLatin1String(ED2KFTSTR_CDIMAGE))
    //     return QObject::tr("CD Image");
    // if (m_fileType == QLatin1String(ED2KFTSTR_EMULECOLLECTION))
    //     return QObject::tr("eMule Collection");
    return m_fileType;
}

// ---------------------------------------------------------------------------
// Hash
// ---------------------------------------------------------------------------

bool AbstractFile::hasNullHash() const
{
    return isnulmd4(m_fileIdentifier.getMD4Hash());
}

// ---------------------------------------------------------------------------
// ED2K link
// ---------------------------------------------------------------------------

QString AbstractFile::getED2kLink(bool hashset, bool html, bool hostname) const
{
    // The link grammar lives in ED2KFileLink::toLink() — including where the '/'
    // terminator goes and how an IPv6 hint is kept out of the legacy `sources,` block.
    ED2KFileLink link;
    link.name = m_fileName;
    link.size = static_cast<uint64>(m_fileSize);
    md4cpy(link.hash.data(), fileHash());

    const bool emitPartHashes = hashset
        && m_fileIdentifier.getAvailableMD4PartHashCount() > 0
        && m_fileIdentifier.hasExpectedMD4HashCount();
    if (emitPartHashes) {
        for (uint16 j = 0; j < m_fileIdentifier.getAvailableMD4PartHashCount(); ++j) {
            std::array<uint8, 16> partHash{};
            md4cpy(partHash.data(), m_fileIdentifier.getMD4PartHash(j));
            link.partHashes.push_back(partHash);
        }
    }

    if (m_fileIdentifier.hasAICHHash()) {
        link.aichHash = m_fileIdentifier.getAICHHash();
        link.hasValidAICHHash = true;
    }

    if (hostname)
        link.hostnameSources = ownLinkSourceHints();

    return link.toLink({.partHashes = emitPartHashes,
                        .aichHash   = true,
                        .sources    = hostname && !link.hostnameSources.empty(),
                        .html       = html});
}

// ---------------------------------------------------------------------------
// Comment / rating
// ---------------------------------------------------------------------------

const QString& AbstractFile::getFileComment()
{
    if (!m_commentLoaded)
        loadComment();
    return m_comment;
}

uint32 AbstractFile::getFileRating()
{
    if (!m_commentLoaded)
        loadComment();
    return m_rating;
}

void AbstractFile::loadComment()
{
    m_commentLoaded = true;

    // MFC CAbstractFile::LoadComment (srchybrid/AbstractFile.cpp:126): one section
    // per file in fileinfo.ini, named by the MD4 hex hash, keys "Comment" (UTF-8)
    // and "Rate". Same layout as the original client, so an eMule config directory
    // can be used as-is.
    //
    // Deliberately does not touch m_hasComment: that flag is the *aggregate* over
    // peers and Kad notes, owned by updateFileRatingCommentAvail(). Setting it from
    // your own comment made a file you commented briefly claim someone else had,
    // until the next recompute wiped it. Your own comment shows as the
    // FileCommentsOvl mark instead. MFC's LoadComment leaves it alone too.
    Settings ini(thePrefs.fileCommentsFilePath());
    const QString section = encodeBase16({fileHash(), 16}) + QLatin1Char('/');

    // Read as a list and re-join, never as a plain QString. A comment the original
    // client wrote is bare text, and a bare comma is QSettings' list separator — so
    // "german audio, full length" parses as two elements and converts to an *empty*
    // QString, silently losing every comment that has a comma in it. Reading it as
    // the list QSettings thinks it is and joining it back is lossless either way: a
    // value we wrote ourselves comes back quoted, hence as a single element.
    m_comment = ini.value<QStringList>(section + QStringLiteral("Comment"))
                    .join(QStringLiteral(", "))
                    .left(MAXFILECOMMENTLEN);

    const uint rate = ini.value<uint>(section + QStringLiteral("Rate"), 0u);
    m_rating = (rate <= 5) ? rate : 0;
}

void AbstractFile::saveComment() const
{
    // The write half of loadComment(); the two are adjacent because they are the
    // only code that knows the on-disk layout. Both keys go out together: every
    // caller runs the lazy getters first, so the in-memory pair is always whole.
    Settings ini(thePrefs.fileCommentsFilePath());
    const QString section = encodeBase16({fileHash(), 16}) + QLatin1Char('/');

    ini.setValue(section + QStringLiteral("Comment"), m_comment);
    ini.setValue(section + QStringLiteral("Rate"), static_cast<uint>(m_rating));
    ini.sync();
}

void AbstractFile::setKadCommentSearchRunning(bool val)
{
    if (val != m_kadCommentSearchRunning) {
        m_kadCommentSearchRunning = val;
        updateFileRatingCommentAvail(true);
    }
}

void AbstractFile::addKadNote(const QByteArray& publisherId, uint8 rating,
                              const QString& comment)
{
    // Last note from a publisher wins — re-running a NOTES lookup must refresh
    // that peer's entry, not append a second copy of it.
    m_kadNotesCache[publisherId] = KadNote{rating, comment};
    updateFileRatingCommentAvail(true);
}

void AbstractFile::clearKadNotes()
{
    m_kadNotesCache.clear();
}

// ---------------------------------------------------------------------------
// Tag access — by numeric ID
// ---------------------------------------------------------------------------

uint32 AbstractFile::getIntTagValue(uint8 tagId) const
{
    for (auto it = m_tags.rbegin(); it != m_tags.rend(); ++it)
        if (it->nameId() == tagId && it->isInt())
            return it->intValue();
    return 0;
}

bool AbstractFile::getIntTagValue(uint8 tagId, uint32& value) const
{
    for (auto it = m_tags.rbegin(); it != m_tags.rend(); ++it)
        if (it->nameId() == tagId && it->isInt()) {
            value = it->intValue();
            return true;
        }
    return false;
}

uint64 AbstractFile::getInt64TagValue(uint8 tagId) const
{
    for (auto it = m_tags.rbegin(); it != m_tags.rend(); ++it)
        if (it->nameId() == tagId && it->isInt64(true))
            return it->int64Value();
    return 0;
}

bool AbstractFile::getInt64TagValue(uint8 tagId, uint64& value) const
{
    for (auto it = m_tags.rbegin(); it != m_tags.rend(); ++it)
        if (it->nameId() == tagId && it->isInt64(true)) {
            value = it->int64Value();
            return true;
        }
    return false;
}

void AbstractFile::setIntTagValue(uint8 tagId, uint32 value)
{
    for (auto& tag : m_tags)
        if (tag.nameId() == tagId && tag.isInt()) {
            tag.setInt(value);
            return;
        }
    m_tags.emplace_back(tagId, value);
}

void AbstractFile::setInt64TagValue(uint8 tagId, uint64 value)
{
    for (auto& tag : m_tags)
        if (tag.nameId() == tagId && tag.isInt64(true)) {
            tag.setInt64(value);
            return;
        }
    m_tags.emplace_back(tagId, value);
}

// ---------------------------------------------------------------------------
// Tag access — by string name
// ---------------------------------------------------------------------------

uint32 AbstractFile::getIntTagValue(const QByteArray& tagName) const
{
    for (auto it = m_tags.rbegin(); it != m_tags.rend(); ++it)
        if (it->nameId() == 0 && it->isInt() && it->name() == tagName)
            return it->intValue();
    return 0;
}

uint64 AbstractFile::getInt64TagValue(const QByteArray& tagName) const
{
    for (auto it = m_tags.rbegin(); it != m_tags.rend(); ++it)
        if (it->nameId() == 0 && it->isInt64(true) && it->name() == tagName)
            return it->int64Value();
    return 0;
}

static const QString s_emptyString;

const QString& AbstractFile::getStrTagValue(uint8 tagId) const
{
    for (auto it = m_tags.rbegin(); it != m_tags.rend(); ++it)
        if (it->nameId() == tagId && it->isStr())
            return it->strValue();
    return s_emptyString;
}

const QString& AbstractFile::getStrTagValue(const QByteArray& tagName) const
{
    for (auto it = m_tags.rbegin(); it != m_tags.rend(); ++it)
        if (it->nameId() == 0 && it->isStr() && it->name() == tagName)
            return it->strValue();
    return s_emptyString;
}

void AbstractFile::setStrTagValue(uint8 tagId, const QString& value)
{
    for (auto& tag : m_tags)
        if (tag.nameId() == tagId && tag.isStr()) {
            tag.setStr(value);
            return;
        }
    m_tags.emplace_back(tagId, value);
}

// ---------------------------------------------------------------------------
// Tag lookup
// ---------------------------------------------------------------------------

const Tag* AbstractFile::getTag(uint8 tagId, uint8 tagType) const
{
    for (auto it = m_tags.rbegin(); it != m_tags.rend(); ++it)
        if (it->nameId() == tagId && it->type() == tagType)
            return &(*it);
    return nullptr;
}

const Tag* AbstractFile::getTag(const QByteArray& tagName, uint8 tagType) const
{
    for (auto it = m_tags.rbegin(); it != m_tags.rend(); ++it)
        if (it->nameId() == 0 && it->type() == tagType && it->name() == tagName)
            return &(*it);
    return nullptr;
}

const Tag* AbstractFile::getTag(uint8 tagId) const
{
    for (auto it = m_tags.rbegin(); it != m_tags.rend(); ++it)
        if (it->nameId() == tagId)
            return &(*it);
    return nullptr;
}

const Tag* AbstractFile::getTag(const QByteArray& tagName) const
{
    for (auto it = m_tags.rbegin(); it != m_tags.rend(); ++it)
        if (it->nameId() == 0 && it->name() == tagName)
            return &(*it);
    return nullptr;
}

// ---------------------------------------------------------------------------
// Tag management
// ---------------------------------------------------------------------------

void AbstractFile::addTagUnique(Tag tag)
{
    for (auto& existing : m_tags) {
        bool nameMatch = false;
        if (existing.nameId() != 0 && existing.nameId() == tag.nameId())
            nameMatch = true;
        else if (existing.hasName() && tag.hasName() && existing.name() == tag.name())
            nameMatch = true;

        if (nameMatch && existing.type() == tag.type()) {
            existing = std::move(tag);
            return;
        }
    }
    m_tags.push_back(std::move(tag));
}

void AbstractFile::deleteTag(uint8 tagId)
{
    auto it = std::find_if(m_tags.begin(), m_tags.end(),
        [tagId](const Tag& t) { return t.nameId() == tagId; });
    if (it != m_tags.end())
        m_tags.erase(it);
}

void AbstractFile::clearTags()
{
    m_tags.clear();
}

void AbstractFile::copyTags(const std::vector<Tag>& tags)
{
    m_tags.insert(m_tags.end(), tags.begin(), tags.end());
}

} // namespace eMule
