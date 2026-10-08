/// @file tst_PublishKeywordList.cpp
/// @brief The Kad keyword list: lookup by index, exact removal, cursor safety.

#include "TestHelpers.h"

#include "files/KnownFile.h"
#include "files/PublishKeywordList.h"

#include <QElapsedTimer>
#include <QTest>

#include <cstring>
#include <memory>
#include <set>

using namespace eMule;

class tst_PublishKeywordList : public QObject {
    Q_OBJECT

private slots:
    void addAndRemove_keepOnlyReferencedKeywords();
    void keywordsMatchCaseInsensitively();
    void remove_usesTheWordsTheFileWasAddedUnder();
    void erasingTheCursorKeywordMovesTheCursorOn();
    void changesMakeThePublishWalkDueAgain();
    void purge_dropsWhatAReloadLeftUnreferenced();
    void manyFiles_addAndRemoveStayLinear();
};

namespace {

std::unique_ptr<KnownFile> makeFile(uint32 id, const QString& name)
{
    auto f = std::make_unique<KnownFile>();
    uint8 hash[16]{};
    std::memcpy(hash, &id, sizeof(id));
    f->setFileHash(hash);
    f->setFileName(name);
    return f;
}

std::set<QString> keywordsOf(PublishKeywordList& list)
{
    std::set<QString> out;
    list.resetNextKeyword();
    while (PublishKeyword* kw = list.getNextKeyword())
        out.insert(kw->keyword().toLower());
    return out;
}

int refsOf(PublishKeywordList& list, const QString& word)
{
    list.resetNextKeyword();
    while (PublishKeyword* kw = list.getNextKeyword())
        if (kw->keyword().compare(word, Qt::CaseInsensitive) == 0)
            return kw->refCount();
    return -1;
}

} // namespace

void tst_PublishKeywordList::addAndRemove_keepOnlyReferencedKeywords()
{
    PublishKeywordList list;
    auto a = makeFile(1, QStringLiteral("alpha shared.bin"));
    auto b = makeFile(2, QStringLiteral("beta shared.bin"));
    list.addKeywords(a.get());
    list.addKeywords(b.get());
    list.addKeywords(a.get());   // twice: one reference

    QCOMPARE(keywordsOf(list), (std::set<QString>{QStringLiteral("alpha"), QStringLiteral("beta"),
                                                  QStringLiteral("shared")}));
    QCOMPARE(refsOf(list, QStringLiteral("shared")), 2);
    QCOMPARE(refsOf(list, QStringLiteral("alpha")), 1);

    list.removeKeywords(a.get());
    QCOMPARE(keywordsOf(list), (std::set<QString>{QStringLiteral("beta"), QStringLiteral("shared")}));
    QCOMPARE(refsOf(list, QStringLiteral("shared")), 1);

    list.removeKeywords(a.get());   // not in it any more
    list.removeKeywords(b.get());
    QCOMPARE(list.keywordCount(), 0);
    QVERIFY(!list.getNextKeyword());
}

void tst_PublishKeywordList::keywordsMatchCaseInsensitively()
{
    PublishKeywordList list;
    auto a = makeFile(1, QStringLiteral("Holiday Film.avi"));
    auto b = makeFile(2, QStringLiteral("HOLIDAY film.avi"));
    list.addKeywords(a.get());
    list.addKeywords(b.get());
    QCOMPARE(list.keywordCount(), 2);
    QCOMPARE(refsOf(list, QStringLiteral("holiday")), 2);
}

void tst_PublishKeywordList::remove_usesTheWordsTheFileWasAddedUnder()
{
    PublishKeywordList list;
    auto a = makeFile(1, QStringLiteral("before rename.bin"));
    list.addKeywords(a.get());

    // renamed behind the list's back: the old words still hold the pointer
    a->setFileName(QStringLiteral("after change.bin"));
    list.removeKeywords(a.get());
    QCOMPARE(list.keywordCount(), 0);

    // the stock order: remove, rename, add
    list.addKeywords(a.get());
    QCOMPARE(keywordsOf(list), (std::set<QString>{QStringLiteral("after"), QStringLiteral("change")}));
}

void tst_PublishKeywordList::erasingTheCursorKeywordMovesTheCursorOn()
{
    PublishKeywordList list;
    auto a = makeFile(1, QStringLiteral("first.bin"));
    auto b = makeFile(2, QStringLiteral("second.bin"));
    auto c = makeFile(3, QStringLiteral("third.bin"));
    for (auto* f : {a.get(), b.get(), c.get()})
        list.addKeywords(f);

    list.resetNextKeyword();
    QCOMPARE(list.getNextKeyword()->keyword(), QStringLiteral("first"));
    list.removeKeywords(b.get());   // the cursor stands on "second"
    PublishKeyword* next = list.getNextKeyword();
    QVERIFY(next);
    QCOMPARE(next->keyword(), QStringLiteral("third"));
    QVERIFY(!list.getNextKeyword());
}

void tst_PublishKeywordList::changesMakeThePublishWalkDueAgain()
{
    // The publish walk runs on a short list timer and each keyword keeps its own 24 h.
    // A file added later must not wait a day: a new keyword wakes the walk, and a
    // published keyword that gains a file is put back to "long ago".
    PublishKeywordList list;
    const time_t later = std::time(nullptr) + 24 * 3600;
    const auto keyword = [&](const QString& word) -> PublishKeyword* {
        list.resetNextKeyword();
        while (PublishKeyword* kw = list.getNextKeyword())
            if (kw->keyword().compare(word, Qt::CaseInsensitive) == 0)
                return kw;
        return nullptr;
    };

    auto a = makeFile(1, QStringLiteral("alpha shared.bin"));
    list.addKeywords(a.get());
    keyword(QStringLiteral("alpha"))->setNextPublishTime(later);
    keyword(QStringLiteral("shared"))->setNextPublishTime(later);

    // Same file again: nothing new behind the keyword, its time stands.
    list.setNextPublishTime(later);
    list.addKeywords(a.get());
    QCOMPARE(keyword(QStringLiteral("shared"))->nextPublishTime(), later);
    QCOMPARE(list.nextPublishTime(), later);

    // A second file: its own keyword is new, the common one gained a file.
    auto b = makeFile(2, QStringLiteral("beta shared.bin"));
    list.addKeywords(b.get());
    QCOMPARE(list.nextPublishTime(), time_t{0});
    QCOMPARE(keyword(QStringLiteral("beta"))->nextPublishTime(), time_t{0});
    QVERIFY(keyword(QStringLiteral("shared"))->nextPublishTime() < std::time(nullptr));
    QCOMPARE(keyword(QStringLiteral("alpha"))->nextPublishTime(), later);

    // A keyword going away wakes the walk too.
    list.setNextPublishTime(later);
    list.removeKeywords(b.get());
    QCOMPARE(list.nextPublishTime(), time_t{0});
}

void tst_PublishKeywordList::purge_dropsWhatAReloadLeftUnreferenced()
{
    PublishKeywordList list;
    auto a = makeFile(1, QStringLiteral("stays here.bin"));
    auto b = makeFile(2, QStringLiteral("goes away.bin"));
    list.addKeywords(a.get());
    list.addKeywords(b.get());

    list.removeAllKeywordReferences();
    list.addKeywords(a.get());
    list.purgeUnreferencedKeywords();
    QCOMPARE(keywordsOf(list), (std::set<QString>{QStringLiteral("stays"), QStringLiteral("here")}));

    // the index went with the keywords: the word can come back
    list.addKeywords(b.get());
    QCOMPARE(refsOf(list, QStringLiteral("goes")), 1);
    list.removeKeywords(b.get());
    QCOMPARE(list.keywordCount(), 2);
}

void tst_PublishKeywordList::manyFiles_addAndRemoveStayLinear()
{
    // 20k files, five words each out of 6000, one of them rare. With a scan of the
    // keyword list per word this took minutes.
    constexpr int kFiles = 20000;
    std::vector<std::unique_ptr<KnownFile>> files;
    files.reserve(kFiles);
    for (int i = 0; i < kFiles; ++i)
        files.push_back(makeFile(static_cast<uint32>(i + 1),
            QStringLiteral("word%1 word%2 word%3 word%4 only%5.bin")
                .arg(i % 6000).arg((i * 7) % 6000).arg((i * 13) % 6000).arg((i * 31) % 6000).arg(i)));

    PublishKeywordList list;
    QElapsedTimer timer;
    timer.start();
    for (auto& f : files)
        list.addKeywords(f.get());
    const qint64 addMs = timer.restart();
    QCOMPARE(list.keywordCount(), 6000 + kFiles);

    for (int i = 0; i < kFiles; i += 2)
        list.removeKeywords(files[static_cast<size_t>(i)].get());
    const qint64 removeMs = timer.elapsed();
    QCOMPARE(list.keywordCount(), 3000 + kFiles / 2);   // odd files keep the odd words
    QCOMPARE(refsOf(list, QStringLiteral("only0")), -1);
    QCOMPARE(refsOf(list, QStringLiteral("only1")), 1);
    for (int i = 1; i < kFiles; i += 2)
        list.removeKeywords(files[static_cast<size_t>(i)].get());
    QCOMPARE(list.keywordCount(), 0);

    qInfo("add %lld ms, remove %lld ms", static_cast<long long>(addMs), static_cast<long long>(removeMs));
    // MSVC debug (checked iterators, debug heap) is several times slower; the scan this
    // guards against took minutes, so the wider budget still catches it.
#if defined(_MSC_VER) && defined(_DEBUG)
    constexpr qint64 kBudgetMs = 30000;
#else
    constexpr qint64 kBudgetMs = 5000;
#endif
    QVERIFY2(addMs < kBudgetMs && removeMs < kBudgetMs, "keyword bookkeeping is no longer linear");
}

QTEST_GUILESS_MAIN(tst_PublishKeywordList)
#include "tst_PublishKeywordList.moc"
