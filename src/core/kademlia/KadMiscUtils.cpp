#include "pch.h"
/// @file KadMiscUtils.cpp
/// @brief Kad utility function implementation.

#include "kademlia/KadMiscUtils.h"
#include "crypto/MD4Hash.h"

#include <QStringList>

namespace eMule::kad {

namespace {
/// Characters that delimit keywords in Kad searches.
constexpr const char* kInvKadKeywordChars = " ()[]{}<>,._-!?:;\\/\"";
} // namespace

// ---------------------------------------------------------------------------
// Public functions
// ---------------------------------------------------------------------------

QString ipToString(uint32 ip)
{
    return QStringLiteral("%1.%2.%3.%4")
        .arg((ip >> 24) & 0xFF)
        .arg((ip >> 16) & 0xFF)
        .arg((ip >> 8) & 0xFF)
        .arg(ip & 0xFF);
}

void getKeywordHash(const QString& keyword, UInt128& outHash)
{
    QByteArray utf8 = keyword.toUtf8();
    MD4Hasher hasher;
    hasher.add(utf8.constData(), static_cast<std::size_t>(utf8.size()));
    hasher.finish();
    outHash.setValueBE(hasher.getHash());
}

QByteArray getKeywordBytes(const QString& keyword)
{
    return keyword.toUtf8();
}

void getWords(const QString& str, std::vector<QString>& outWords)
{
    // Split on any of the invalid keyword characters
    QString delims = QString::fromLatin1(kInvKadKeywordChars);
    qsizetype start = 0;
    qsizetype len = str.length();

    while (start < len) {
        // Skip delimiters
        while (start < len && delims.contains(str[start]))
            ++start;

        if (start >= len)
            break;

        // Find end of word
        qsizetype end = start;
        while (end < len && !delims.contains(str[end]))
            ++end;

        QString word = str.mid(start, end - start);
        if (!word.isEmpty()) {
            // Lowercase for dedup and Kad keyword matching
            QString lower = word.toLower();

            // Filter: minimum 3 UTF-8 bytes (matching MFC KadGetKeywordBytes check)
            if (lower.toUtf8().size() >= 3) {
                // Dedup: skip if already present (case-insensitive)
                bool duplicate = false;
                for (const auto& existing : outWords) {
                    if (existing.toLower() == lower) {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate)
                    outWords.push_back(std::move(word));
            }
        }

        start = end;
    }

    // Remove trailing file extension: if last word is <= 3 chars (3 UTF-8 bytes)
    // and there are multiple words, it's likely a file extension — remove it
    if (outWords.size() > 1) {
        const auto& last = outWords.back();
        if (last.toUtf8().size() <= 3)
            outWords.pop_back();
    }
}

QString kadTagStrToLower(const QString& str)
{
    return str.toLower();
}

} // namespace eMule::kad
