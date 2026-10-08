#include "pch.h"
/// @file FakeFileDetector.cpp
/// @brief Fake-file score: reasons, name grouping, rule file.

#include "FakeFileDetector.h"

#include "utils/Log.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"

#include <QFile>
#include <QHash>

#include <algorithm>
#include <numeric>
#include <vector>

namespace eMule {

namespace {

constexpr int kMaxScore = 100;
constexpr int kMaxNames = 64;          // names looked at per file
constexpr int kMinTagChars = 4;        // media tag length worth comparing with a name

// Words that describe the release, not the content.
const QSet<QString>& releaseWords()
{
    static const QSet<QString> words = [] {
        const QString list = QStringLiteral(
            // codec / source / resolution
            "x264 x265 h264 h265 hevc avc av1 xvid divx dvdrip bdrip brrip bluray webrip web webdl "
            "hdtv hdrip dvd dvdr cam hdcam telesync screener hd sd uhd fullhd microhd 720p 1080p "
            "2160p 480p 576p 10bit hdr remux rip "
            "aac ac3 eac3 dts mp3 flac dd5 dd51 atmos truehd "
            // language / subtitles
            "multi dual sub subs subbed dubbed ita eng esp spa fra fre ger deu por lat vose "
            "castellano spanish english italian french german latino ingles espanol italiano "
            // container
            "mkv avi mp4 wmv mpg mpeg mov flv iso rar zip exe pdf epub m4a ogg wav nfo srt part "
            // release notes
            "proper repack extended unrated remastered complete internal limited www com net org "
            "full version final new "
            // stopwords EN ES IT PT DE FR
            "the and for with from this that not you are "
            "los las del una uno por con para que "
            "gli dei della delle nel alla per non "
            "dos das uma com nao "
            "der die das und ein eine von mit nicht "
            "les des une dans pour avec pas est");
        const QStringList parts = list.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        return QSet<QString>(parts.cbegin(), parts.cend());
    }();
    return words;
}

/// Lower-case, accents folded, so "Posibilità" and "posibilita" are one word.
QString foldName(const QString& name)
{
    const QString decomposed = name.normalized(QString::NormalizationForm_KD).toLower();
    QString out;
    out.reserve(decomposed.size());
    for (const QChar c : decomposed) {
        if (c.category() != QChar::Mark_NonSpacing)
            out.append(c);
    }
    return out;
}

QString stripExtension(const QString& name)
{
    static const QRegularExpression ext(QStringLiteral("\\.[a-z0-9]{2,4}$"));
    QString out = name;
    out.remove(ext);
    return out;
}

bool isHexRun(const QString& word)
{
    return word.size() >= 8 && std::all_of(word.cbegin(), word.cend(), [](QChar c) {
        return c.isDigit() || (c >= QLatin1Char('a') && c <= QLatin1Char('f'));
    });
}

bool isNumber(const QString& word)
{
    return std::all_of(word.cbegin(), word.cend(), [](QChar c) { return c.isDigit(); });
}

/// The folded name as " word word " for whole-word matching.
QString paddedWords(const QString& text)
{
    QString out(QLatin1Char(' '));
    for (const QChar c : foldName(text))
        out.append(c.isLetterOrNumber() ? c : QLatin1Char(' '));
    out.append(QLatin1Char(' '));
    return out.simplified().prepend(QLatin1Char(' ')).append(QLatin1Char(' '));
}

enum class Kind : uint8 { Unknown, Media, Program, Document, Image };

Kind kindOfType(ED2KFileType type)
{
    switch (type) {
    case ED2KFileType::Audio:
    case ED2KFileType::Video:
        return Kind::Media;
    case ED2KFileType::Program:
    case ED2KFileType::Archive:
    case ED2KFileType::CDImage:
        return Kind::Program;   // archives and images are published as "Pro"
    case ED2KFileType::Document:
        return Kind::Document;
    case ED2KFileType::Image:
        return Kind::Image;
    default:
        return Kind::Unknown;
    }
}

Kind kindOfName(const QString& name)
{
    return kindOfType(getED2KFileTypeID(name));
}

Kind kindOfTypeString(const QString& type)
{
    if (type == QLatin1StringView(ED2KFTSTR_AUDIO) || type == QLatin1StringView(ED2KFTSTR_VIDEO))
        return Kind::Media;
    if (type == QLatin1StringView(ED2KFTSTR_PROGRAM) || type == QLatin1StringView(ED2KFTSTR_ARCHIVE)
        || type == QLatin1StringView(ED2KFTSTR_CDIMAGE))
        return Kind::Program;
    if (type == QLatin1StringView(ED2KFTSTR_DOCUMENT))
        return Kind::Document;
    if (type == QLatin1StringView(ED2KFTSTR_IMAGE))
        return Kind::Image;
    return Kind::Unknown;
}

void add(FakeFileVerdict& verdict, FakeReason reason, int points)
{
    if (!verdict.has(reason))
        verdict.reasons.push_back({reason, points});
}

/// @return how many unrelated contents the names describe.
int assessNames(const QStringList& names, const FakeFileInput& in, const FakeFileRules& rules,
                FakeFileVerdict& verdict)
{
    const int groups = countNameGroups(names, in.ignoredNameWords);
    if (groups >= 3)
        add(verdict, FakeReason::MultipleNames, 25);
    else if (groups == 2)
        add(verdict, FakeReason::MultipleNames, 10);

    QSet<Kind> kinds;
    for (const QString& name : names) {
        if (const Kind kind = kindOfName(name); kind != Kind::Unknown)
            kinds.insert(kind);
        if (rules.matches(name))
            add(verdict, FakeReason::BadSignalName, 25);
    }
    if (kinds.size() >= 2)
        add(verdict, FakeReason::NamesSpanKinds, 25);

    // Score only: an ambiguous word is a caution, a trade word makes the file suspect.
    int abuse = 0;
    for (const QString& name : names)
        abuse = std::max(abuse, abuseNameTier(name, rules));
    if (abuse > 0)
        add(verdict, FakeReason::AbuseContentName, abuse >= 2 ? 50 : 25);
    return groups;
}

void assessMedia(const FakeFileInput& in, const QStringList& names, FakeFileVerdict& verdict)
{
    ED2KFileType type = getED2KFileTypeID(in.name);
    if (in.claimedType == QLatin1StringView(ED2KFTSTR_VIDEO))
        type = ED2KFileType::Video;
    else if (in.claimedType == QLatin1StringView(ED2KFTSTR_AUDIO))
        type = ED2KFileType::Audio;
    const bool video = type == ED2KFileType::Video;
    const bool audio = type == ED2KFileType::Audio;

    if (in.mediaLengthSec > 0) {
        const uint32 len = in.mediaLengthSec;
        // Very long is fine while the size still pays for it: a 27 h audiobook at 64 kbit/s.
        const double impliedKbps = static_cast<double>(in.size) * 8.0 / len / 1000.0;
        if ((video && ((in.size >= 50ull << 20 && len < 60) || (len > 12 * 3600 && impliedKbps < 40)))
            || (audio && ((in.size >= 1ull << 20 && len < 2) || (len > 24 * 3600 && impliedKbps < 16))))
            add(verdict, FakeReason::ImplausibleMediaLength, 10);
    }
    if (in.mediaBitrateKbps > 0) {
        const uint32 rate = in.mediaBitrateKbps;
        if ((video && (rate < 40 || rate > 500'000)) || (audio && (rate < 16 || rate > 2000)))
            add(verdict, FakeReason::ImplausibleMediaBitrate, 10);
    }
    // Length x bitrate should come out near the size; real files stay within 2x.
    if ((video || audio) && in.mediaLengthSec > 0 && in.mediaBitrateKbps > 0 && in.size > 0) {
        const double implied = static_cast<double>(in.size) * 8.0 / in.mediaLengthSec / 1000.0;
        const double ratio = implied / in.mediaBitrateKbps;
        if (ratio > 3.0 || ratio < 1.0 / 3.0)
            add(verdict, FakeReason::MediaSizeMismatch, 10);
    }

    // A tag that names the content while no name does. Only when every usable tag misses,
    // and only for media: an archive's tags describe something inside it.
    if (!video && !audio)
        return;
    QSet<QString> nameTokens;
    for (const QString& name : names)
        nameTokens.unite(significantNameTokens(name));
    bool anyTag = false;
    bool anyHit = false;
    for (const QString& tag : {in.artist, in.album, in.title}) {
        if (tag.trimmed().size() < kMinTagChars)
            continue;
        const QSet<QString> tokens = significantNameTokens(tag);
        if (tokens.isEmpty())
            continue;
        anyTag = true;
        anyHit = anyHit || tokens.intersects(nameTokens);
    }
    if (anyTag && !anyHit && !nameTokens.isEmpty())
        add(verdict, FakeReason::NameMediaTagMismatch, 10);
}

} // namespace

KadTrust kadTrustFromPublishInfo(uint32 publishInfo)
{
    if (publishInfo == 0)
        return KadTrust::Unknown;
    const uint32 trust = publishInfo & 0xFFFF;
    if (trust < 100)
        return KadTrust::Low;
    return trust < 300 ? KadTrust::Normal : KadTrust::High;
}

QString fakeReasonId(FakeReason reason)
{
    switch (reason) {
    case FakeReason::MultipleNames:           return QStringLiteral("multiple_names");
    case FakeReason::NamesSpanKinds:          return QStringLiteral("names_span_kinds");
    case FakeReason::BadSignalName:           return QStringLiteral("bad_signal_name");
    case FakeReason::BadSignalComment:        return QStringLiteral("bad_signal_comment");
    case FakeReason::HeaderExtensionMismatch: return QStringLiteral("header_extension_mismatch");
    case FakeReason::ExecutableMasquerade:    return QStringLiteral("executable_masquerade");
    case FakeReason::ArchiveMasquerade:       return QStringLiteral("archive_masquerade");
    case FakeReason::ClaimedTypeMismatch:     return QStringLiteral("claimed_type_mismatch");
    case FakeReason::SpamScore:               return QStringLiteral("spam_score");
    case FakeReason::SpamStatus:              return QStringLiteral("spam_status");
    case FakeReason::BadRating:               return QStringLiteral("bad_rating");
    case FakeReason::FakeRating:              return QStringLiteral("fake_rating");
    case FakeReason::MultipleAich:            return QStringLiteral("multiple_aich");
    case FakeReason::ImplausibleMediaLength:  return QStringLiteral("implausible_media_length");
    case FakeReason::ImplausibleMediaBitrate: return QStringLiteral("implausible_media_bitrate");
    case FakeReason::MediaSizeMismatch:       return QStringLiteral("media_size_mismatch");
    case FakeReason::NameMediaTagMismatch:    return QStringLiteral("name_media_tag_mismatch");
    case FakeReason::AbuseContentName:        return QStringLiteral("abuse_content_name");
    }
    return {};
}

QString confidenceId(Confidence band)
{
    switch (band) {
    case Confidence::Spam:       return QStringLiteral("spam");
    case Confidence::LikelyFake: return QStringLiteral("likely_fake");
    case Confidence::Suspect:    return QStringLiteral("suspect");
    case Confidence::Caution:    return QStringLiteral("caution");
    case Confidence::LooksGood:  return QStringLiteral("looks_good");
    case Confidence::Genuine:    return QStringLiteral("genuine");
    }
    return {};
}

// ---------------------------------------------------------------------------
// Rules
// ---------------------------------------------------------------------------

FakeFileRules FakeFileRules::defaults()
{
    FakeFileRules rules;
    // No bare "password": it is in the name of every password recovery tool.
    rules.tokens = {QStringLiteral("fake"), QStringLiteral("corrupt"), QStringLiteral("wrong file"),
                    QStringLiteral("password protected"), QStringLiteral("virus"),
                    QStringLiteral("trojan"), QStringLiteral("malware")};
    for (const char* pattern : {"\\.mp4\\.exe$", "\\.avi\\.scr$"}) {
        rules.regexes.emplace_back(QString::fromLatin1(pattern),
                                   QRegularExpression::CaseInsensitiveOption);
    }
    return rules;
}

FakeFileRules FakeFileRules::parse(const QString& text, QStringList* errors)
{
    enum class Section { None, Tokens, Regex, Abuse } section = Section::None;
    FakeFileRules rules;

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            const QString name = line.mid(1, line.size() - 2).trimmed().toLower();
            section = name == QLatin1StringView("tokens") ? Section::Tokens
                    : name == QLatin1StringView("regex")  ? Section::Regex
                    : name == QLatin1StringView("abuse")  ? Section::Abuse
                                                          : Section::None;
            continue;
        }
        if (section == Section::Tokens || section == Section::Abuse) {
            QStringList& list = section == Section::Tokens ? rules.tokens : rules.abuseTokens;
            const QString token = paddedWords(line).trimmed();
            if (!token.isEmpty() && !list.contains(token))
                list.push_back(token);
        } else if (section == Section::Regex) {
            QRegularExpression re(line, QRegularExpression::CaseInsensitiveOption);
            if (re.isValid())
                rules.regexes.push_back(std::move(re));
            else if (errors)
                errors->push_back(QStringLiteral("%1: %2").arg(line, re.errorString()));
        }
    }
    return rules;
}

FakeFileRules FakeFileRules::load(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        logWarning(QStringLiteral("Fake-file rules: cannot read %1, using the built-in rules").arg(path));
        return defaults();
    }
    QStringList errors;
    FakeFileRules rules = parse(QString::fromUtf8(file.readAll()), &errors);
    for (const QString& error : std::as_const(errors))
        logWarning(QStringLiteral("Fake-file rules: skipped regex %1").arg(error));
    if (rules.tokens.isEmpty() && rules.regexes.isEmpty()) {
        // Only an [abuse] section: the built-in fake rules stay
        FakeFileRules merged = defaults();
        merged.abuseTokens = rules.abuseTokens;
        return merged;
    }
    return rules;
}

bool FakeFileRules::matches(const QString& text) const
{
    if (text.isEmpty())
        return false;
    if (!tokens.isEmpty()) {
        const QString words = paddedWords(text);
        for (const QString& token : tokens) {
            if (words.contains(QLatin1Char(' ') + token + QLatin1Char(' ')))
                return true;
        }
    }
    return std::any_of(regexes.cbegin(), regexes.cend(),
                       [&text](const QRegularExpression& re) { return re.match(text).hasMatch(); });
}

namespace {
FakeFileRules& rulesStore()
{
    static FakeFileRules rules = FakeFileRules::defaults();
    return rules;
}
} // namespace

const FakeFileRules& activeFakeFileRules()
{
    return rulesStore();
}

void setActiveFakeFileRules(FakeFileRules rules)
{
    rulesStore() = std::move(rules);
}

// ---------------------------------------------------------------------------
// Verdict
// ---------------------------------------------------------------------------

bool FakeFileVerdict::has(FakeReason reason) const
{
    return std::any_of(reasons.cbegin(), reasons.cend(),
                       [reason](const Reason& r) { return r.reason == reason; });
}

QStringList FakeFileVerdict::reasonIds() const
{
    QStringList ids;
    for (const Reason& r : reasons)
        ids.push_back(fakeReasonId(r.reason));
    return ids;
}

FakeFileVerdict assessFile(const FakeFileInput& in, const FakeFileRules& rules)
{
    FakeFileVerdict verdict;

    // Every distinct name, the row's own first; capped so one file cannot cost much.
    QStringList names;
    if (!in.name.isEmpty())
        names.push_back(in.name);
    for (const QString& name : in.observedNames) {
        if (names.size() >= kMaxNames)
            break;
        if (!name.isEmpty() && !names.contains(name, Qt::CaseInsensitive))
            names.push_back(name);
    }

    const int nameGroups = assessNames(names, in, rules, verdict);

    if (std::any_of(in.comments.cbegin(), in.comments.cend(),
                    [&rules](const QString& c) { return rules.matches(c); }))
        add(verdict, FakeReason::BadSignalComment, 15);

    if (in.container.isSuspect()) {
        add(verdict, FakeReason::HeaderExtensionMismatch, 45);
        if (in.container.foreign == ForeignHead::Executable)
            add(verdict, FakeReason::ExecutableMasquerade, 25);
        else if (in.container.foreign == ForeignHead::Archive)
            add(verdict, FakeReason::ArchiveMasquerade, 20);
    }

    // Only media against program/archive: the two a server type can be trusted to tell apart.
    const Kind claimed = kindOfTypeString(in.claimedType);
    const Kind named = kindOfName(in.name);
    if ((claimed == Kind::Media && named == Kind::Program)
        || (claimed == Kind::Program && named == Kind::Media))
        add(verdict, FakeReason::ClaimedTypeMismatch, 15);

    if (in.spamRating >= 60)
        add(verdict, FakeReason::SpamScore, 25);
    else if (in.spamRating >= 30)
        add(verdict, FakeReason::SpamScore, 15);
    if (in.consideredSpam)
        add(verdict, FakeReason::SpamStatus, 15);

    if (in.userRating == 1)
        add(verdict, FakeReason::FakeRating, 30);
    if (in.userRating == 2 || in.kadNoteRatedFake)
        add(verdict, FakeReason::BadRating, 20);

    if (in.multipleAICH)
        add(verdict, FakeReason::MultipleAich, 35);

    assessMedia(in, names, verdict);

    verdict.score = std::min(kMaxScore,
        std::accumulate(verdict.reasons.cbegin(), verdict.reasons.cend(), 0,
                        [](int sum, const FakeFileVerdict::Reason& r) { return sum + r.points; }));

    if (in.consideredSpam)
        verdict.band = Confidence::Spam;
    else if (verdict.score >= 75)
        verdict.band = Confidence::LikelyFake;
    else if (verdict.score >= 50)
        verdict.band = Confidence::Suspect;
    else if (verdict.score >= 25)
        verdict.band = Confidence::Caution;
    else if (verdict.score == 0
             && (in.userRating >= 4
                 // Many publishers alone is popularity, and popular fakes have them too.
                 // It counts when independent names also agree on what the file is.
                 || (in.kadTrust == KadTrust::High && names.size() >= 2 && nameGroups == 1)))
        verdict.band = Confidence::Genuine;
    else
        verdict.band = Confidence::LooksGood;
    return verdict;
}

// ---------------------------------------------------------------------------
// Name grouping
// ---------------------------------------------------------------------------

QSet<QString> significantNameTokens(const QString& name)
{
    static const QRegularExpression episode(
        QStringLiteral("(?<![a-z0-9])(?:s(\\d{1,2})[ ._-]?e(\\d{1,3})|(\\d{1,2})x(\\d{2,3}))(?![a-z0-9])"));
    static const QRegularExpression year(QStringLiteral("(?<!\\d)((?:19|20)\\d\\d)(?!\\d)"));

    const QString folded = stripExtension(foldName(name));
    QSet<QString> tokens;

    QString rest = folded;
    for (auto it = episode.globalMatch(folded); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        const bool sxe = !m.captured(1).isEmpty();
        tokens.insert(QStringLiteral("s%1e%2")
                          .arg((sxe ? m.captured(1) : m.captured(3)).toInt())
                          .arg((sxe ? m.captured(2) : m.captured(4)).toInt()));
    }
    rest.replace(episode, QStringLiteral(" "));
    for (auto it = year.globalMatch(rest); it.hasNext();)
        tokens.insert(QLatin1Char('y') + it.next().captured(1));

    QString word;
    const auto flush = [&] {
        if (word.size() >= 3 && !isNumber(word) && !isHexRun(word) && !releaseWords().contains(word))
            tokens.insert(word);
        word.clear();
    };
    for (const QChar c : std::as_const(rest)) {
        if (c.isLetterOrNumber())
            word.append(c);
        else
            flush();
    }
    flush();
    return tokens;
}

QSet<QString> searchKeywordTokens(const QString& expression)
{
    QSet<QString> words;
    const QStringList parts = paddedWords(expression).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString& word : parts) {
        if (word.size() >= 3)
            words.insert(word);
    }
    return words;
}

int countNameGroups(const QStringList& names, const QSet<QString>& ignoredWords)
{
    static const QRegularExpression episodeToken(QStringLiteral("^s\\d+e\\d+$"));
    static const QRegularExpression yearToken(QStringLiteral("^y\\d{4}$"));

    struct Name {
        QSet<QString> words;      // what the content is called
        QSet<QString> episodes;
        QSet<QString> years;
    };
    std::vector<Name> list;
    for (const QString& name : names) {
        Name entry;
        const QSet<QString> tokens = significantNameTokens(name);
        for (const QString& token : tokens) {
            if (ignoredWords.contains(token))
                continue;
            if (episodeToken.match(token).hasMatch())
                entry.episodes.insert(token);
            else if (yearToken.match(token).hasMatch())
                entry.years.insert(token);
            else
                entry.words.insert(token);
        }
        if (!entry.words.isEmpty() || !entry.episodes.isEmpty() || !entry.years.isEmpty())
            list.push_back(std::move(entry));
    }

    // One shared word is not enough between long names: a search keyword is in every
    // name it returned, and that glued unrelated names into one group.
    const auto linked = [](const Name& a, const Name& b) {
        if (a.episodes.intersects(b.episodes))
            return true;
        const qsizetype shorter = std::min(a.words.size(), b.words.size());
        qsizetype shared = 0;
        for (const QString& word : a.words)
            shared += b.words.contains(word) ? 1 : 0;
        if (shared >= 2 || (shared == 1 && shorter <= 2))
            return true;
        // "TheFlash(2023)" against "The Flash 2023 ...": a year and next to nothing else
        return shared == 0 && shorter <= 1 && a.years.intersects(b.years);
    };

    std::vector<int> parent(list.size());
    std::iota(parent.begin(), parent.end(), 0);
    const auto find = [&parent](int i) {
        while (parent[static_cast<size_t>(i)] != i)
            i = parent[static_cast<size_t>(i)];
        return i;
    };
    const int count = static_cast<int>(list.size());
    for (int i = 0; i < count; ++i) {
        for (int j = i + 1; j < count; ++j) {
            if (find(i) != find(j) && linked(list[static_cast<size_t>(i)], list[static_cast<size_t>(j)]))
                parent[static_cast<size_t>(find(j))] = find(i);
        }
    }
    int groups = 0;
    for (int i = 0; i < count; ++i)
        groups += find(i) == i ? 1 : 0;
    return groups;
}

// ---------------------------------------------------------------------------
// Abuse words
// ---------------------------------------------------------------------------

int abuseNameTier(const QString& name, const FakeFileRules& rules)
{
    // Words that only advertise child abuse material.
    static const QStringList strong{
        QStringLiteral("pthc"), QStringLiteral("ptsc"), QStringLiteral("hussyfan"),
        QStringLiteral("raygold"), QStringLiteral("r ygold"), QStringLiteral("babyshivid"),
        QStringLiteral("kingpass"), QStringLiteral("kdquality"), QStringLiteral("childporn"),
        QStringLiteral("child porn"), QStringLiteral("kinderporno")};
    // Words with an innocent use too: a novel, a band, Spanish slang, a parenting book.
    static const QStringList weak{
        QStringLiteral("preteen"), QStringLiteral("pre teen"), QStringLiteral("underage"),
        QStringLiteral("pedo"), QStringLiteral("jailbait"), QStringLiteral("lolita"),
        QStringLiteral("lolitas")};
    // An age under 16 written the way these names do: "12yo"
    static const QRegularExpression age(QStringLiteral(" (?:[1-9]|1[0-5])yo "));

    if (name.isEmpty())
        return 0;
    const QString words = paddedWords(name);
    const auto has = [&words](const QStringList& tokens) {
        return std::any_of(tokens.cbegin(), tokens.cend(), [&words](const QString& token) {
            return words.contains(QLatin1Char(' ') + token + QLatin1Char(' '));
        });
    };
    if (has(strong) || has(rules.abuseTokens))
        return 2;
    return has(weak) || age.match(words).hasMatch() ? 1 : 0;
}

} // namespace eMule
