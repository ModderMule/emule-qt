#include "pch.h"
/// @file OtherFunctions.cpp
/// @brief Core utility function implementations.

#include "OtherFunctions.h"
#include "Opcodes.h"

#include <QLocale>
#include <QUrl>

#include <algorithm>
#include <vector>

namespace eMule {

// ---------------------------------------------------------------------------
// MD4 string helpers
// ---------------------------------------------------------------------------

QString md4str(const uint8* hash)
{
    return encodeBase16({hash, kMdxDigestSize});
}

bool strmd4(const QString& str, uint8* hash)
{
    if (str.size() != kMdxDigestSize * 2)
        return false;
    return decodeBase16(str, hash, kMdxDigestSize) == kMdxDigestSize;
}

// ---------------------------------------------------------------------------
// Base16 encoding / decoding
// ---------------------------------------------------------------------------

static constexpr char kBase16Chars[] = "0123456789ABCDEF";

QString encodeBase16(std::span<const uint8> data)
{
    QString result;
    result.reserve(static_cast<qsizetype>(data.size()) * 2);
    for (auto byte : data) {
        result += QChar::fromLatin1(kBase16Chars[byte >> 4]);
        result += QChar::fromLatin1(kBase16Chars[byte & 0x0F]);
    }
    return result;
}

static int hexCharValue(QChar ch) noexcept
{
    const auto c = ch.unicode();
    if (c >= u'0' && c <= u'9') return c - u'0';
    if (c >= u'A' && c <= u'F') return c - u'A' + 10;
    if (c >= u'a' && c <= u'f') return c - u'a' + 10;
    return -1;
}

std::size_t decodeBase16(const QString& hex, uint8* output, std::size_t outputLen)
{
    if (hex.size() % 2 != 0)
        return 0;

    const auto bytesNeeded = static_cast<std::size_t>(hex.size()) / 2;
    if (bytesNeeded > outputLen)
        return 0;

    for (qsizetype i = 0; i < hex.size(); i += 2) {
        const int hi = hexCharValue(hex[i]);
        const int lo = hexCharValue(hex[i + 1]);
        if (hi < 0 || lo < 0)
            return 0;
        output[i / 2] = static_cast<uint8>((hi << 4) | lo);
    }
    return bytesNeeded;
}

// ---------------------------------------------------------------------------
// Base32 encoding / decoding
// ---------------------------------------------------------------------------

static constexpr char kBase32Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

QString encodeBase32(std::span<const uint8> data)
{
    QString result;
    const auto len = data.size();
    if (len == 0)
        return result;

    result.reserve(static_cast<qsizetype>((len * 8 + 4) / 5));

    std::size_t i = 0;
    int bits = 0;
    uint32 buffer = 0;

    while (i < len) {
        buffer = (buffer << 8) | data[i++];
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            result += QChar::fromLatin1(kBase32Chars[(buffer >> bits) & 0x1F]);
        }
    }
    if (bits > 0) {
        buffer <<= (5 - bits);
        result += QChar::fromLatin1(kBase32Chars[buffer & 0x1F]);
    }
    return result;
}

std::size_t decodeBase32(const QString& input, uint8* output, std::size_t outputLen)
{
    std::size_t written = 0;
    int bits = 0;
    uint32 buffer = 0;

    for (qsizetype i = 0; i < input.size(); ++i) {
        const auto c = input[i].toUpper().unicode();
        int val = -1;
        if (c >= u'A' && c <= u'Z')
            val = c - u'A';
        else if (c >= u'2' && c <= u'7')
            val = c - u'2' + 26;
        else
            return 0;  // invalid character

        buffer = (buffer << 5) | static_cast<uint32>(val);
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            if (written >= outputLen)
                return 0;
            output[written++] = static_cast<uint8>((buffer >> bits) & 0xFF);
        }
    }
    return written;
}

// ---------------------------------------------------------------------------
// URL encoding / decoding
// ---------------------------------------------------------------------------

QString urlEncode(const QString& input)
{
    return QString::fromUtf8(QUrl::toPercentEncoding(input));
}

QString urlDecode(const QString& input)
{
    return QUrl::fromPercentEncoding(input.toUtf8());
}

QString encodeUrlQueryParam(const QString& query)
{
    return QString::fromUtf8(QUrl::toPercentEncoding(query, QByteArrayLiteral(""), QByteArrayLiteral("+")));
}

// ---------------------------------------------------------------------------
// IP address helpers
// ---------------------------------------------------------------------------

bool isGoodIP(uint32 nIP, bool forceCheck)
{
    // filter LAN IPs
    // ---IsLanIP()---IsGoodIP()IsGoodIPPort()IsGoodIP()---
    if (isLanIP(nIP) && !forceCheck)
        return false;

    // 0.x.x.x is invalid
    const auto a = static_cast<uint8>(nIP);
    if (a == 0)
        return false;

    return true;
}

bool isLanIP(uint32 nIP)
{
    // IP is in network byte order (little-endian on x86)
    const auto a = static_cast<uint8>(nIP);
    const auto b = static_cast<uint8>(nIP >> 8);

    // 10.x.x.x
    if (a == 10)
        return true;
    // 172.16.0.0 - 172.31.255.255
    if (a == 172 && b >= 16 && b <= 31)
        return true;
    // 192.168.x.x
    if (a == 192 && b == 168)
        return true;
    // 127.x.x.x (loopback)
    if (a == 127)
        return true;
    // 169.254.x.x (link-local)
    if (a == 169 && b == 254)
        return true;

    return false;
}

QString ipstr(uint32 nIP)
{
    return QStringLiteral("%1.%2.%3.%4")
        .arg(static_cast<uint8>(nIP))
        .arg(static_cast<uint8>(nIP >> 8))
        .arg(static_cast<uint8>(nIP >> 16))
        .arg(static_cast<uint8>(nIP >> 24));
}

QString ipstr(uint32 nIP, uint16 nPort)
{
    return QStringLiteral("%1:%2").arg(ipstr(nIP)).arg(nPort);
}

// ---------------------------------------------------------------------------
// Random number generation
// ---------------------------------------------------------------------------

std::mt19937& randomEngine()
{
    thread_local std::mt19937 engine{std::random_device{}()};
    return engine;
}

uint16 getRandomUInt16()
{
    std::uniform_int_distribution<uint32> dist(0, 0xFFFF);
    return static_cast<uint16>(dist(randomEngine()));
}

uint32 getRandomUInt32()
{
    std::uniform_int_distribution<uint32> dist;
    return dist(randomEngine());
}

// ---------------------------------------------------------------------------
// RC4 encryption
// ---------------------------------------------------------------------------

RC4Key rc4CreateKey(std::span<const uint8> keyData, bool skipDiscard)
{
    RC4Key key;
    for (int i = 0; i < 256; ++i)
        key.state[static_cast<std::size_t>(i)] = static_cast<uint8>(i);

    uint8 j = 0;
    for (int i = 0; i < 256; ++i) {
        const auto idx = static_cast<std::size_t>(i);
        j = static_cast<uint8>(j + key.state[idx] + keyData[idx % keyData.size()]);
        std::swap(key.state[idx], key.state[j]);
    }

    if (!skipDiscard) {
        // Discard first 1024 bytes (RC4-drop[1024])
        uint8 dummy = 0;
        for (int i = 0; i < 1024; ++i)
            rc4Crypt(&dummy, &dummy, 1, key);
    }

    return key;
}

void rc4Crypt(const uint8* input, uint8* output, uint32 len, RC4Key& key)
{
    for (uint32 i = 0; i < len; ++i) {
        key.x = static_cast<uint8>(key.x + 1);
        key.y = static_cast<uint8>(key.y + key.state[key.x]);
        std::swap(key.state[key.x], key.state[key.y]);
        output[i] = input[i] ^ key.state[static_cast<uint8>(key.state[key.x] + key.state[key.y])];
    }
}

void rc4Crypt(uint8* data, uint32 len, RC4Key& key)
{
    rc4Crypt(data, data, len, key);
}

// ---------------------------------------------------------------------------
// ED2K file type detection
// ---------------------------------------------------------------------------

ED2KFileType getED2KFileTypeID(const QString& fileName)
{
    const auto dotPos = fileName.lastIndexOf(QChar(u'.'));
    if (dotPos < 0)
        return ED2KFileType::Any;

    const QString ext = fileName.mid(dotPos).toLower();

    // Audio
    static constexpr std::array audioExts = {
        u".mp3", u".mp2", u".mpc", u".wav", u".ogg", u".oga", u".flac",
        u".aac", u".m4a", u".wma", u".ape", u".opus"
    };
    for (auto e : audioExts)
        if (ext == e) return ED2KFileType::Audio;

    // Video
    static constexpr std::array videoExts = {
        u".avi", u".mpg", u".mpeg", u".mp4", u".mkv", u".ogm", u".ogv",
        u".wmv", u".mov", u".divx", u".vob", u".flv", u".webm", u".ts",
        u".m4v", u".rm", u".rmvb", u".3gp"
    };
    for (auto e : videoExts)
        if (ext == e) return ED2KFileType::Video;

    // Image
    static constexpr std::array imageExts = {
        u".jpg", u".jpeg", u".bmp", u".gif", u".png", u".tiff", u".tif",
        u".psd", u".ico", u".svg", u".webp"
    };
    for (auto e : imageExts)
        if (ext == e) return ED2KFileType::Image;

    // Program
    static constexpr std::array progExts = {
        u".exe", u".com", u".msi", u".dmg", u".app", u".deb", u".rpm",
        u".apk", u".bat", u".cmd", u".sh"
    };
    for (auto e : progExts)
        if (ext == e) return ED2KFileType::Program;

    // Document
    static constexpr std::array docExts = {
        u".doc", u".docx", u".txt", u".pdf", u".xls", u".xlsx", u".ppt",
        u".pptx", u".htm", u".html", u".rtf", u".odt", u".ods", u".odp",
        u".epub", u".djvu", u".chm"
    };
    for (auto e : docExts)
        if (ext == e) return ED2KFileType::Document;

    // Archive
    static constexpr std::array archiveExts = {
        u".zip", u".rar", u".7z", u".gz", u".bz2", u".xz", u".tar",
        u".ace", u".cab", u".lzh", u".arj"
    };
    for (auto e : archiveExts)
        if (ext == e) return ED2KFileType::Archive;

    // CD/DVD Image
    static constexpr std::array cdExts = {
        u".iso", u".bin", u".cue", u".nrg", u".img", u".mdf", u".mds"
    };
    for (auto e : cdExts)
        if (ext == e) return ED2KFileType::CDImage;

    // eMule collection
    if (ext == u".emulecollection")
        return ED2KFileType::EmuleCollection;

    return ED2KFileType::Any;
}

QString getFileTypeByName(const QString& fileName)
{
    switch (getED2KFileTypeID(fileName)) {
    case ED2KFileType::Audio:           return QStringLiteral(ED2KFTSTR_AUDIO);
    case ED2KFileType::Video:           return QStringLiteral(ED2KFTSTR_VIDEO);
    case ED2KFileType::Image:           return QStringLiteral(ED2KFTSTR_IMAGE);
    case ED2KFileType::Document:        return QStringLiteral(ED2KFTSTR_DOCUMENT);
    case ED2KFileType::Program:         return QStringLiteral(ED2KFTSTR_PROGRAM);
    case ED2KFileType::Archive:         return QStringLiteral(ED2KFTSTR_ARCHIVE);
    case ED2KFileType::CDImage:         return QStringLiteral(ED2KFTSTR_CDIMAGE);
    case ED2KFileType::EmuleCollection: return QStringLiteral(ED2KFTSTR_EMULECOLLECTION);
    default:                            return {};
    }
}

// ---------------------------------------------------------------------------
// String helpers
// ---------------------------------------------------------------------------

QString stripInvalidFilenameChars(const QString& text)
{
    static constexpr std::array invalidChars = {
        u'\"', u'*', u'<', u'>', u'?', u'|', u'\\', u'/', u':'
    };
    QString result = text;
    for (auto ch : invalidChars)
        result.replace(QChar(ch), QChar(u'_'));
    return result;
}

QString stringLimit(const QString& input, int maxLength)
{
    if (input.size() <= maxLength)
        return input;
    return input.left(maxLength - 3) + QStringLiteral("...");
}

uint32 levenshteinDistance(const QString& str1, const QString& str2)
{
    const auto m = static_cast<std::size_t>(str1.size());
    const auto n = static_cast<std::size_t>(str2.size());

    if (m == 0) return static_cast<uint32>(n);
    if (n == 0) return static_cast<uint32>(m);

    std::vector<std::size_t> prev(n + 1);
    std::vector<std::size_t> curr(n + 1);

    for (std::size_t j = 0; j <= n; ++j)
        prev[j] = j;

    for (std::size_t i = 1; i <= m; ++i) {
        curr[0] = i;
        for (std::size_t j = 1; j <= n; ++j) {
            const std::size_t cost = (str1[static_cast<qsizetype>(i - 1)] == str2[static_cast<qsizetype>(j - 1)]) ? 0 : 1;
            curr[j] = std::min({prev[j] + 1, curr[j - 1] + 1, prev[j - 1] + cost});
        }
        std::swap(prev, curr);
    }
    return static_cast<uint32>(prev[n]);
}

} // namespace eMule
