/// @file AbstractFile.cpp
/// @brief Abstract base class for all file types — replaces MFC CAbstractFile.

#include "files/AbstractFile.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QObject>
#include <QSettings>

#include <algorithm>

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
    QString link;
    if (html)
        link = QStringLiteral("<a href=\"");

    link += QStringLiteral("ed2k://|file|%1|%2|%3|")
        .arg(urlEncode(stripInvalidFilenameChars(m_fileName)))
        .arg(static_cast<uint64>(m_fileSize))
        .arg(encodeBase16({fileHash(), 16}));

    if (hashset
        && m_fileIdentifier.getAvailableMD4PartHashCount() > 0
        && m_fileIdentifier.hasExpectedMD4HashCount())
    {
        link += QStringLiteral("p=");
        for (uint16 j = 0; j < m_fileIdentifier.getAvailableMD4PartHashCount(); ++j) {
            if (j > 0)
                link += QChar(u':');
            link += encodeBase16({m_fileIdentifier.getMD4PartHash(j), 16});
        }
        link += QChar(u'|');
    }

    if (m_fileIdentifier.hasAICHHash())
        link += QStringLiteral("h=%1|").arg(m_fileIdentifier.getAICHHash().getString());

    if (hostname) {
        const auto& hn = thePrefs.ed2kHostname();
        if (hn.contains(u'.'))
            link += QStringLiteral("|sources,%1:%2|/").arg(hn).arg(thePrefs.port());
        else
            link += QChar(u'/');
    } else {
        link += QChar(u'/');
    }

    if (html)
        link += QStringLiteral("\">%1</a>").arg(stripInvalidFilenameChars(m_fileName));

    return link;
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

    const QString iniPath = thePrefs.configDir() + QStringLiteral("/filecomments.ini");
    if (!QFile::exists(iniPath))
        return;

    QSettings settings(iniPath, QSettings::IniFormat);
    const QString key = encodeBase16({fileHash(), 16});
    const QString value = settings.value(key).toString();
    if (value.isEmpty())
        return;

    // Format: "rating|comment"
    const auto sepIdx = value.indexOf(u'|');
    if (sepIdx < 0)
        return;

    bool ok = false;
    const uint32 rating = value.left(sepIdx).toUInt(&ok);
    if (ok && rating <= 5)
        m_rating = rating;

    const QString comment = value.mid(sepIdx + 1);
    if (!comment.isEmpty()) {
        m_comment = comment;
        m_hasComment = true;
    }
}

void AbstractFile::setKadCommentSearchRunning(bool val)
{
    if (val != m_kadCommentSearchRunning) {
        m_kadCommentSearchRunning = val;
        updateFileRatingCommentAvail(true);
    }
}

void AbstractFile::addKadNote(uint8 rating, const QString& comment)
{
    m_kadNotesCache.emplace_back(rating, comment);
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
