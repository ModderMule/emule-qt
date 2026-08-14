/// @file tst_TextLinks.cpp
/// @brief Linkifying remote text, and getting the link back on click.
///
/// The regression these pin down: an eD2K link cannot be carried as a QUrl. Its '|'
/// separators are illegal in the authority component, so QUrl("ed2k://|file|…") is
/// invalid and toString() hands back an EMPTY STRING — the click silently vanished
/// before it ever reached the importer. Every case here therefore checks the full
/// round trip linkify -> href -> fromHref, not just that an <a> was emitted.
///
/// The second half covers the shapes IrcPanel and MessagesPanel feed in: mIRC control
/// bytes, smileys, and query strings whose '&' must reach the handler as '&', not as
/// the "&amp;" the old detect-URLs-in-rendered-HTML pass captured.

#include "utils/TextLinks.h"

#include <QRegularExpression>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextFragment>
#include <QtTest>

using namespace eMule::TextLinks;

namespace {

/// Pull the nth href out of linkified HTML and decode it back to the link text.
QString decodedHref(const QString& html, int nth = 0)
{
    static const QRegularExpression re(QStringLiteral("href='([^']*)'"));
    auto it = re.globalMatch(html);
    for (int i = 0; it.hasNext(); ++i) {
        const QString href = it.next().captured(1);
        if (i == nth)
            return fromHref(QUrl(href));
    }
    return {};
}

qsizetype anchorCount(const QString& html)
{
    return html.count(QStringLiteral("<a href="));
}

/// A viewport point that lands on the browser's first anchor.
///
/// Scanned rather than computed: anchorAt() is the same lookup QTextBrowser itself
/// does on a mouse press, so a hit here is a hit for the real click too.
QPoint anchorPoint(const QTextBrowser& browser)
{
    for (int y = 2; y < browser.viewport()->height(); y += 2) {
        for (int x = 2; x < browser.viewport()->width(); x += 2) {
            const QPoint p(x, y);
            if (!browser.anchorAt(p).isEmpty())
                return p;
        }
    }
    return {-1, -1};   // caller's click then misses and the QVERIFY below fails
}

// The exact shape a real server advertises — "!! Sharing-Devils No.4 !!" greeting.
const QString kEd2kLink = QStringLiteral(
    "ed2k://|file|eMule_v0.50a_-XdP-_v5.6_RC3.rar|4960062"
    "|95818F5037C368B3D9750D76A73AA027|h=4XEIHO2SCM4DQVVWYA37NQGC5OCEYBMD|/");

} // namespace

class TestTextLinks : public QObject {
    Q_OBJECT

private slots:
    void ed2kLinkSurvivesRoundTrip();
    void ed2kLinkIsNotARepresentableQUrl();
    void hrefSurvivesQtRichTextParsing();
    void httpLinkKeepsQueryString();
    void bareWwwGetsSchemeButDisplaysAsSent();
    void versionCheckSentinelPassesThrough();
    void plainTextIsEscaped();
    void trailingPunctuationIsNotPartOfTheLink();
    void multipleLinksOnOneLine();
    void lineWithNoLinkIsJustEscaped();

    // Shapes the chat and IRC panes feed in
    void ampersandInQueryReachesTheHandlerUnescaped();
    void mircControlByteEndsTheUrl();
    void gapRendererNeverSeesTheUrl();
    void gapRendererRunsOnEverySegment();

    // The click itself, through a real widget
    void clickingAnAnchorDeliversThePlainLink();
    void clickingDoesNotNavigateTheBrowser();
};

void TestTextLinks::ed2kLinkSurvivesRoundTrip()
{
    const QString html = escapeAndLinkify(QStringLiteral("## ") + kEd2kLink);

    QCOMPARE(anchorCount(html), 1);
    QCOMPARE(decodedHref(html), kEd2kLink);
    // The pane shows the link exactly as the server sent it.
    QVERIFY(html.contains(QStringLiteral(">") + kEd2kLink.toHtmlEscaped()
                          + QStringLiteral("</a>")));
}

void TestTextLinks::ed2kLinkIsNotARepresentableQUrl()
{
    // Guards the premise of the whole design: if this ever starts passing as a valid
    // URL, the wrapper is no longer load-bearing — but until then, handing the raw
    // link to QUrl loses it completely.
    const QUrl direct(kEd2kLink);
    QVERIFY(!direct.isValid());
    QVERIFY(direct.toString().isEmpty());
}

void TestTextLinks::hrefSurvivesQtRichTextParsing()
{
    // Closes the last seam: the panes hand the HTML to a QTextBrowser, and what comes
    // back on click is whatever Qt's rich-text parser stored as the anchor href —
    // wrapped in QUrl by QTextBrowserPrivate::_q_activateAnchor before anchorClicked
    // fires. Build the same document and read that stored value back.
    QTextDocument doc;
    doc.setHtml(QStringLiteral("<span>") + escapeAndLinkify(kEd2kLink)
                + QStringLiteral("</span>"));

    QString href;
    for (QTextBlock b = doc.begin(); b != doc.end() && href.isEmpty(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (frag.isValid() && frag.charFormat().isAnchor()) {
                href = frag.charFormat().anchorHref();
                break;
            }
        }
    }

    QVERIFY(!href.isEmpty());
    QCOMPARE(fromHref(QUrl(href)), kEd2kLink);
}

void TestTextLinks::httpLinkKeepsQueryString()
{
    const QString url = QStringLiteral("https://airvpn.org/?referred_by=384424");
    const QString html = escapeAndLinkify(QStringLiteral("## ") + url);

    QCOMPARE(anchorCount(html), 1);
    QCOMPARE(decodedHref(html), url);
}

void TestTextLinks::bareWwwGetsSchemeButDisplaysAsSent()
{
    const QString html = escapeAndLinkify(QStringLiteral("visit www.emule-project.com today"));

    // Decoded target is openable...
    QCOMPARE(decodedHref(html), QStringLiteral("http://www.emule-project.com"));
    // ...while the pane still shows what the server wrote.
    QVERIFY(html.contains(QStringLiteral(">www.emule-project.com</a>")));
    QVERIFY(html.startsWith(QStringLiteral("visit <a href=")));
    QVERIFY(html.endsWith(QStringLiteral("</a> today")));
}

void TestTextLinks::versionCheckSentinelPassesThrough()
{
    // The banner writes this href itself; fromHref must not touch it.
    QCOMPARE(fromHref(QUrl(QStringLiteral("emuleqt:versioncheck"))),
             QStringLiteral("emuleqt:versioncheck"));
}

void TestTextLinks::plainTextIsEscaped()
{
    const QString html = escapeAndLinkify(QStringLiteral("Rock & Roll <b>not markup</b>"));
    QCOMPARE(html, QStringLiteral("Rock &amp; Roll &lt;b&gt;not markup&lt;/b&gt;"));
}

void TestTextLinks::trailingPunctuationIsNotPartOfTheLink()
{
    const QString html = escapeAndLinkify(QStringLiteral("see https://emule-qt.org/."));
    QCOMPARE(decodedHref(html), QStringLiteral("https://emule-qt.org/"));
    QVERIFY(html.endsWith(QStringLiteral("</a>.")));
}

void TestTextLinks::multipleLinksOnOneLine()
{
    const QString a = QStringLiteral("http://a.example/one");
    const QString b = QStringLiteral("https://b.example/two");
    const QString html = escapeAndLinkify(a + QStringLiteral(" and ") + b);

    QCOMPARE(anchorCount(html), 2);
    QCOMPARE(decodedHref(html, 0), a);
    QCOMPARE(decodedHref(html, 1), b);
}

void TestTextLinks::lineWithNoLinkIsJustEscaped()
{
    const QString line = QStringLiteral("#################################");
    QCOMPARE(escapeAndLinkify(line), line);
}

void TestTextLinks::ampersandInQueryReachesTheHandlerUnescaped()
{
    // IrcPanel used to regex URLs out of already-escaped HTML, which captured
    // "a=1&amp;b=2" as the link text. That only worked because the rich-text parser
    // unescaped the href again; percent-encoding it into the wrapper would have
    // delivered a literal "&amp;" to QDesktopServices. Scanning the raw text fixes it.
    const QString url = QStringLiteral("http://x.example/?a=1&b=2");
    const QString html = escapeAndLinkify(QStringLiteral("see ") + url);

    QCOMPARE(decodedHref(html), url);
    // The visible text is still escaped for the parser.
    QVERIFY(html.contains(QStringLiteral(">http://x.example/?a=1&amp;b=2</a>")));
}

void TestTextLinks::mircControlByteEndsTheUrl()
{
    // "\x0304http://x.example\x03" — a colour-wrapped URL, the shape IRC spam uses.
    // The trailing 0x03 must not become part of the link.
    const QString line = QChar(0x03) + QStringLiteral("04http://x.example")
                         + QChar(0x03) + QStringLiteral(" rest");

    const QString html = linkify(line, [](const QString& s) { return s.toHtmlEscaped(); });

    QCOMPARE(anchorCount(html), 1);
    QCOMPARE(decodedHref(html), QStringLiteral("http://x.example"));
}

void TestTextLinks::gapRendererNeverSeesTheUrl()
{
    // MessagesPanel relies on this: a smiley substitution that ran over the whole
    // string could rewrite characters inside a URL.
    QStringList seen;
    std::ignore = linkify(QStringLiteral("a :-) http://x.example/:-) b"),
                          [&seen](const QString& s) {
        seen << s;
        return s.toHtmlEscaped();
    });

    for (const QString& segment : seen)
        QVERIFY(!segment.contains(QStringLiteral("http://")));
}

void TestTextLinks::gapRendererRunsOnEverySegment()
{
    // Both sides of the link, including the empty leading segment, go through the
    // renderer — IrcPanel threads its mIRC state through these calls, so a skipped
    // segment would drop formatting.
    int calls = 0;
    const QString html = linkify(QStringLiteral("http://x.example tail"),
                                 [&calls](const QString& s) {
        ++calls;
        return s.toHtmlEscaped() + QStringLiteral("|");
    });

    QCOMPARE(calls, 2);
    QCOMPARE(html.count(QLatin1Char('|')), 2);
    QVERIFY(html.endsWith(QStringLiteral(" tail|")));
}

void TestTextLinks::clickingAnAnchorDeliversThePlainLink()
{
    // The Server Info, chat and IRC panes all go through wireLinkClicks(), so one
    // real click proves the whole path for all three: rich-text parse -> stored href
    // -> QUrl -> fromHref -> handler.
    QTextBrowser browser;
    QString received;
    bool called = false;
    wireLinkClicks(&browser, &browser, [&](const QString& link) {
        received = link;
        called = true;
    });

    browser.setHtml(escapeAndLinkify(kEd2kLink));
    browser.resize(900, 200);
    browser.show();
    QVERIFY(QTest::qWaitForWindowExposed(&browser));

    QTest::mouseClick(browser.viewport(), Qt::LeftButton, {}, anchorPoint(browser));

    QVERIFY(called);
    QCOMPARE(received, kEd2kLink);
}

void TestTextLinks::clickingDoesNotNavigateTheBrowser()
{
    // wireLinkClicks() turns openLinks off. Left on, QTextBrowser answers the click
    // with setSource(), which tries to load the href as a document and wipes the pane.
    QTextBrowser browser;
    wireLinkClicks(&browser, &browser, [](const QString&) {});

    browser.setHtml(escapeAndLinkify(QStringLiteral("see https://emule-qt.org/ now")));
    browser.resize(900, 200);
    browser.show();
    QVERIFY(QTest::qWaitForWindowExposed(&browser));

    const QString before = browser.toPlainText();
    QTest::mouseClick(browser.viewport(), Qt::LeftButton, {}, anchorPoint(browser));

    QCOMPARE(browser.toPlainText(), before);
    QVERIFY(browser.source().isEmpty());
}

QTEST_MAIN(TestTextLinks)
#include "tst_TextLinks.moc"
