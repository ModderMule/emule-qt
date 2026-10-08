#pragma once

/// @file FakeFileDetector.h
/// @brief Scores how likely a file is not what its name says, and why.
///
/// Warning-only: the verdict is shown, nothing is hidden or blocked. The detector
/// is a pure function over what the caller already knows about a file -- the names
/// its hash goes by, ratings and comments, the spam rating, media tags, and (for a
/// download) its first bytes. Each reason counts once; the score is capped at 100.

#include "media/ContainerSniffer.h"
#include "utils/Types.h"

#include <QList>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

namespace eMule {

inline constexpr auto kFakeFileFilterName = "FakeFileFilter.dat";

enum class FakeReason : uint8 {
    MultipleNames,
    NamesSpanKinds,
    BadSignalName,
    BadSignalComment,
    HeaderExtensionMismatch,
    ExecutableMasquerade,
    ArchiveMasquerade,
    ClaimedTypeMismatch,
    SpamScore,
    SpamStatus,
    BadRating,
    FakeRating,
    MultipleAich,
    ImplausibleMediaLength,
    ImplausibleMediaBitrate,
    MediaSizeMismatch,
    NameMediaTagMismatch,
};

/// Worst first, so the numeric value sorts a column.
enum class Confidence : uint8 {
    Spam,
    LikelyFake,
    Suspect,
    Caution,
    LooksGood,
    Genuine,
};

enum class KadTrust : uint8 { Unknown, Low, Normal, High };

/// Trust from a Kad TAG_PUBLISHINFO value (trust x100 in the low 16 bits).
[[nodiscard]] KadTrust kadTrustFromPublishInfo(uint32 publishInfo);

/// Stable ids for IPC / REST ("multiple_names", "likely_fake", ...).
[[nodiscard]] QString fakeReasonId(FakeReason reason);
[[nodiscard]] QString confidenceId(Confidence band);

/// The user's bad-word rules (FakeFileFilter.dat).
struct FakeFileRules {
    QStringList tokens;                    ///< lower-case, matched as whole words
    QList<QRegularExpression> regexes;     ///< case-insensitive, matched on the full name

    [[nodiscard]] static FakeFileRules defaults();
    /// `[tokens]` / `[regex]` sections, `#` comments. A bad regex is skipped and named
    /// in @p errors.
    [[nodiscard]] static FakeFileRules parse(const QString& text, QStringList* errors = nullptr);
    /// Reads the file; the defaults when it is missing, unreadable or empty.
    [[nodiscard]] static FakeFileRules load(const QString& path);

    [[nodiscard]] bool matches(const QString& text) const;
};

/// The rules assessments run with. Defaults until setActiveFakeFileRules().
[[nodiscard]] const FakeFileRules& activeFakeFileRules();
void setActiveFakeFileRules(FakeFileRules rules);

struct FakeFileInput {
    QString name;
    uint64 size = 0;
    QString claimedType;            ///< ED2KFTSTR_*; empty when nobody claimed one
    QStringList observedNames;      ///< other names seen for the hash; `name` is added
    QStringList comments;
    uint32 spamRating = 0;
    bool consideredSpam = false;
    uint32 userRating = 0;          ///< 0 = none, 1 = fake ... 5 = excellent
    bool kadNoteRatedFake = false;
    bool multipleAICH = false;
    uint32 mediaLengthSec = 0;
    uint32 mediaBitrateKbps = 0;
    QString artist;
    QString album;
    QString title;
    ContainerCheck container;       ///< downloads only
    KadTrust kadTrust = KadTrust::Unknown;
};

struct FakeFileVerdict {
    struct Reason {
        FakeReason reason;
        int points;
        bool operator==(const Reason&) const = default;
    };

    int score = 0;
    QList<Reason> reasons;
    Confidence band = Confidence::LooksGood;

    [[nodiscard]] bool has(FakeReason reason) const;
    [[nodiscard]] QStringList reasonIds() const;
    bool operator==(const FakeFileVerdict&) const = default;
};

[[nodiscard]] FakeFileVerdict assessFile(const FakeFileInput& input, const FakeFileRules& rules);

/// The words of a name that say what the content is: release, codec, language and
/// container words, stopwords, bare numbers and hex runs are dropped. Year and
/// episode markers come back as "y2024" / "s1e8".
[[nodiscard]] QSet<QString> significantNameTokens(const QString& name);

/// How many unrelated contents the names describe. Two names belong together when
/// they share one token; names with nothing significant in them are not counted.
[[nodiscard]] int countNameGroups(const QStringList& names);

} // namespace eMule
