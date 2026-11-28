#include "pch.h"
/// @file Ed3kRoomMediaView.cpp
/// @brief The media of an ed3k room's posts in its log — implementation.

#include "panels/Ed3kRoomMediaView.h"

#include "controls/LogTextView.h"
#include "prefs/Preferences.h"
#include "utils/MediaSniff.h"
#include "utils/ArgStr.h"

#include <QAudioOutput>
#include <QBuffer>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImageReader>
#include <QLabel>
#include <QLocale>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QMovie>
#include <QPainter>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QScreen>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QVideoFrame>
#include <QVideoSink>

namespace eMule {

namespace {

constexpr const char* kScheme = "emuleqt-media";
constexpr int kBoxW = 320;          // largest shown box, CSS pixels
constexpr int kBoxH = 240;
constexpr int kDefaultW = 240;      // no size told
constexpr int kDefaultH = 135;
constexpr int kFrameEveryMs = 66;   // video frames into the log: ~15 a second

QString sizeText(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

/// @p img scaled into @p box (aspect kept), at the screen's pixel ratio
QImage fitted(const QImage& img, QSize box, qreal dpr)
{
    QImage out = img.scaled(box * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    out.setDevicePixelRatio(dpr);
    return out;
}

} // namespace

Ed3kRoomMediaView::Ed3kRoomMediaView(LogTextView* log, QObject* parent)
    : QObject(parent)
    , m_log(log)
{
}

Ed3kRoomMediaView::~Ed3kRoomMediaView() = default;

QString Ed3kRoomMediaView::html(const Ref& m)
{
    auto& slot = m_items[m.root];
    if (!slot) {
        slot = std::make_shared<Item>();
        slot->ref = m;
        slot->box = boxOf(m);
    } else if (slot->ref.state != m.state && m.state != None) {
        slot->ref.state = m.state;
    }
    Item& it = *slot;
    if (isFile(it.ref))
        return fileLine(it);
    const bool shown = MediaSniff::isImage(it.ref.type) || MediaSniff::isVideo(it.ref.type);
    if (!showsInline(it)) {
        // §6: the text and the file; a click asks for it
        const QString file = QStringLiteral("&#128206; %1 (%2)").arg(it.ref.name.toHtmlEscaped(), sizeText(it.ref.size));
        if (!shown || it.ref.state == Ignored)
            return QStringLiteral("<br><span>%1</span>").arg(file);
        QString what = tr("click to download");
        if (it.ref.state == Fetching)
            what = tr("downloading...");
        else if (it.ref.state == Failed)
            what = tr("not found, click to try again");
        return QStringLiteral("<br><a href=\"%1\">%2 — %3</a>").arg(urlOf(it.ref.root).toString(), file, what);
    }
    if (it.frame.isNull())
        putPlaceholder(it);
    else
        m_log->document()->addResource(QTextDocument::ImageResource, urlOf(it.ref.root), it.frame);
    loadIfReady(it);
    const QString url = urlOf(it.ref.root).toString();
    return QStringLiteral("<br><a href=\"%1\"><img src=\"%1\" width=\"%2\" height=\"%3\"></a>").arg(argStr(url), argStr(it.box.width()), argStr(it.box.height()));
}

bool Ed3kRoomMediaView::setState(const QString& root, int state)
{
    const auto found = m_items.find(root);
    if (found == m_items.end())
        return false;
    Item& it = **found;
    const bool before = showsInline(it);
    it.ref.state = state;
    const bool after = showsInline(it);
    if (after && it.frame.isNull())
        putPlaceholder(it);
    loadIfReady(it);
    // a file line's words change too: drawn again
    return before != after || !after;
}

int Ed3kRoomMediaView::state(const QString& root) const
{
    const auto it = m_items.find(root);
    return it == m_items.end() ? None : (*it)->ref.state;
}

void Ed3kRoomMediaView::setBytes(const QString& root, const QByteArray& bytes)
{
    const auto found = m_items.find(root);
    if (found == m_items.end())
        return;
    Item& it = **found;
    it.loading = false;
    if (isFile(it.ref)) {
        it.bytes = bytes;
        if (std::exchange(it.openWhenLoaded, false) && !bytes.isEmpty())
            openFile(it);
        return;
    }
    if (bytes.isEmpty()) {
        it.broken = true;
        putPlaceholder(it);
        return;
    }
    it.bytes = bytes;
    decode(it);
}

void Ed3kRoomMediaView::setActive(bool on)
{
    m_active = on;
    for (const auto& item : std::as_const(m_items)) {
        if (item->movie)
            item->movie->setPaused(!on);
        if (item->player)
            on ? item->player->play() : item->player->pause();
    }
}

void Ed3kRoomMediaView::clicked(const QString& root, QWidget* parent)
{
    const auto found = m_items.find(root);
    if (found == m_items.end())
        return;
    Item& it = **found;
    if (it.ref.state == Ignored)
        return;
    if (isFile(it.ref)) {
        clickedFile(it, parent);
        return;
    }
    if (it.ref.state == Ready) {
        if (it.decoded)
            showBig(it, parent);
        else
            loadIfReady(it);
        return;
    }
    if (it.ref.state == Fetching)
        return;
    // showing off: the user confirms each file (user's rule)
    if (!thePrefs.chatRoomMedia()
        && QMessageBox::question(parent, tr("ed3k room"),
                                 tr("Download \"%1\" (%2) from your room neighbours?").arg(it.ref.name, sizeText(it.ref.size)))
            != QMessageBox::Yes)
        return;
    emit fetchRequested(root);
}

QString Ed3kRoomMediaView::rootOfLink(const QString& link)
{
    const QString prefix = QString::fromLatin1(kScheme) + QLatin1Char(':');
    if (!link.startsWith(prefix))
        return {};
    const QString root = link.mid(prefix.size()).toLower();
    return root.size() == 64 ? root : QString();
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

QSize Ed3kRoomMediaView::boxOf(const Ref& m)
{
    if (m.width <= 0 || m.height <= 0)
        return {kDefaultW, kDefaultH};
    QSize s(m.width, m.height);
    if (s.width() > kBoxW || s.height() > kBoxH)
        s.scale(kBoxW, kBoxH, Qt::KeepAspectRatio);
    return s.expandedTo({16, 16});
}

bool Ed3kRoomMediaView::showsInline(const Item& it) const
{
    if (it.ref.state == Ignored)
        return false;
    if (!MediaSniff::isImage(it.ref.type) && !MediaSniff::isVideo(it.ref.type))
        return false;
    // off: a file line until the user fetched it
    return thePrefs.chatRoomMedia() || it.ref.state == Ready;
}

void Ed3kRoomMediaView::putPlaceholder(Item& it)
{
    if (!m_log)
        return;
    const qreal dpr = m_log->devicePixelRatioF();
    QImage img(it.box * dpr, QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = m_log->palette();
    p.setPen(QPen(pal.color(QPalette::Mid), 1));
    p.setBrush(pal.color(QPalette::AlternateBase));
    p.drawRoundedRect(QRectF(0.5, 0.5, it.box.width() - 1, it.box.height() - 1), 6, 6);
    const bool video = MediaSniff::isVideo(it.ref.type);
    QString text;
    if (it.broken)
        text = tr("This file cannot be shown");
    else if (it.ref.state == Click || it.ref.state == None)
        text = video ? tr("Click here to download video") : tr("Click here to download image");
    else if (it.ref.state == Failed)
        text = tr("Not found. Click to try again");
    else
        text = tr("Loading...");
    text += QStringLiteral("\n%1 (%2)").arg(it.ref.name, sizeText(it.ref.size));
    p.setPen(pal.color(QPalette::Text));
    p.setFont(m_log->font());
    p.drawText(QRectF(6, 6, it.box.width() - 12, it.box.height() - 12), Qt::AlignCenter | Qt::TextWordWrap, text);
    p.end();
    m_log->document()->addResource(QTextDocument::ImageResource, urlOf(it.ref.root), img);
    m_log->viewport()->update();
}

void Ed3kRoomMediaView::putFrame(Item& it, const QImage& frame)
{
    if (!m_log || frame.isNull())
        return;
    it.frame = fitted(frame, it.box, m_log->devicePixelRatioF());
    m_log->document()->addResource(QTextDocument::ImageResource, urlOf(it.ref.root), it.frame);
    m_log->viewport()->update();
}

void Ed3kRoomMediaView::decode(Item& it)
{
    const QString type = MediaSniff::type(it.bytes);
    if (MediaSniff::isVideo(type)) {
        startVideo(it);
        return;
    }
    if (!MediaSniff::isImage(type)) {
        it.broken = true;
        putPlaceholder(it);
        return;
    }
    it.buffer = std::make_unique<QBuffer>();
    it.buffer->setData(it.bytes);
    it.buffer->open(QIODevice::ReadOnly);
    QImageReader reader(it.buffer.get());
    const QSize size = reader.size();
    // §6: pixels capped before decoding
    if (!size.isValid() || size.width() > kMaxPixels || size.height() > kMaxPixels) {
        it.broken = true;
        putPlaceholder(it);
        return;
    }
    if (it.ref.width <= 0 || it.ref.height <= 0) {
        // no size told: the box follows the picture (the line may grow once)
        it.ref.width = size.width();
        it.ref.height = size.height();
        it.box = boxOf(it.ref);
    }
    if (reader.supportsAnimation() && reader.imageCount() != 1) {
        it.buffer->seek(0);
        it.movie = std::make_unique<QMovie>(it.buffer.get());
        it.movie->setCacheMode(QMovie::CacheNone);
        connect(it.movie.get(), &QMovie::frameChanged, this, [this, root = it.ref.root](int) {
            const auto found = m_items.find(root);
            if (found != m_items.end() && (*found)->movie)
                putFrame(**found, (*found)->movie->currentImage());
        });
        it.decoded = true;
        it.movie->start();
        if (!m_active)
            it.movie->setPaused(true);
        return;
    }
    const QImage img = reader.read();
    it.buffer.reset();
    if (img.isNull()) {
        it.broken = true;
        putPlaceholder(it);
        return;
    }
    it.decoded = true;
    putFrame(it, img);
}

void Ed3kRoomMediaView::startVideo(Item& it)
{
    // muted (no audio output) and looped, frames into the log (user's rule: QtMultimedia only)
    it.buffer = std::make_unique<QBuffer>();
    it.buffer->setData(it.bytes);
    it.buffer->open(QIODevice::ReadOnly);
    it.sink = std::make_unique<QVideoSink>();
    it.player = std::make_unique<QMediaPlayer>();
    it.player->setVideoSink(it.sink.get());
    it.player->setLoops(QMediaPlayer::Infinite);
    connect(it.sink.get(), &QVideoSink::videoFrameChanged, this, [this, root = it.ref.root](const QVideoFrame& frame) {
        const auto found = m_items.find(root);
        if (found == m_items.end())
            return;
        Item& item = **found;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - item.lastFrameMs < kFrameEveryMs)
            return;
        item.lastFrameMs = now;
        putFrame(item, frame.toImage());
    });
    connect(it.player.get(), &QMediaPlayer::errorOccurred, this, [this, root = it.ref.root](QMediaPlayer::Error, const QString&) {
        const auto found = m_items.find(root);
        if (found == m_items.end())
            return;
        (*found)->broken = true;
        if ((*found)->frame.isNull())
            putPlaceholder(**found);
    });
    const QString ext = MediaSniff::type(it.bytes) == u"video/webm" ? QStringLiteral("webm") : QStringLiteral("mp4");
    it.player->setSourceDevice(it.buffer.get(), QUrl(QStringLiteral("media.") + ext));
    it.decoded = true;
    if (m_active)
        it.player->play();
}

void Ed3kRoomMediaView::loadIfReady(Item& it)
{
    if (it.ref.state != Ready || it.loading || it.decoded || it.broken || !m_load || !showsInline(it))
        return;
    it.loading = true;
    m_load(it.ref.root);
}

QUrl Ed3kRoomMediaView::urlOf(const QString& root) const
{
    return QUrl(QString::fromLatin1(kScheme) + QLatin1Char(':') + root);
}

bool Ed3kRoomMediaView::isFile(const Ref& m)
{
    return !MediaSniff::isMediaType(m.type);
}

QString Ed3kRoomMediaView::fileLine(const Item& it) const
{
    const QString file = QStringLiteral("&#128196; %1 (%2)").arg(it.ref.name.toHtmlEscaped(), sizeText(it.ref.size));
    // only the kinds we open: anything else stays text (§6)
    const auto kind = MediaSniff::claimedKind(it.ref.type, it.ref.name);
    if (kind == MediaSniff::FileKind::None || it.ref.state == Ignored)
        return QStringLiteral("<br><span>%1</span>").arg(file);
    QString what = tr("click to download");
    if (it.ref.state == Fetching)
        what = tr("downloading...");
    else if (it.ref.state == Failed)
        what = tr("not found, click to try again");
    else if (it.ref.state == Ready)
        what = kind == MediaSniff::FileKind::Text ? tr("click to show") : tr("click to open");
    return QStringLiteral("<br><a href=\"%1\">%2 — %3</a>").arg(urlOf(it.ref.root).toString(), file, what);
}

void Ed3kRoomMediaView::clickedFile(Item& it, QWidget* parent)
{
    if (MediaSniff::claimedKind(it.ref.type, it.ref.name) == MediaSniff::FileKind::None || it.ref.state == Fetching)
        return;
    if (it.ref.state == Ready) {
        if (!it.bytes.isEmpty()) {
            openFile(it);
        } else if (m_load && !it.loading) {
            it.loading = true;
            it.openWhenLoaded = true;
            m_load(it.ref.root);
        }
        return;
    }
    // a file: always asked, whatever the media prefs (room.md §6)
    if (QMessageBox::question(parent, tr("ed3k room"),
                              tr("Download \"%1\" (%2) from your room neighbours?\nIt opens only when you click it again.")
                                  .arg(it.ref.name, sizeText(it.ref.size)))
        != QMessageBox::Yes)
        return;
    emit fetchRequested(it.ref.root);
}

void Ed3kRoomMediaView::openFile(Item& it)
{
    // by name and bytes now, not the claim
    switch (MediaSniff::fileKind(it.bytes, it.ref.name)) {
    case MediaSniff::FileKind::Text:
        showText(it);
        return;
    case MediaSniff::FileKind::Nzb:
    case MediaSniff::FileKind::Dlc:
    case MediaSniff::FileKind::Torrent:
        emit fileOpenRequested(it.ref.name, it.bytes);
        return;
    case MediaSniff::FileKind::None:
        break;
    }
    QMessageBox::information(m_log, tr("ed3k room"), tr("\"%1\" is not a text, NZB, DLC or torrent file and is not opened.").arg(it.ref.name));
}

void Ed3kRoomMediaView::showText(const Item& it)
{
    auto* dlg = new QDialog(m_log);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(it.ref.name);
    auto* text = new QPlainTextEdit(dlg);
    text->setReadOnly(true);
    text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    text->setPlainText(QString::fromUtf8(it.bytes));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close, dlg);
    connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Save), &QPushButton::clicked, dlg, [dlg, name = it.ref.name, bytes = it.bytes] {
        const QString path = QFileDialog::getSaveFileName(dlg, tr("Save"), name, tr("Text files (*.txt)"));
        if (path.isEmpty())
            return;
        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit())
            QMessageBox::warning(dlg, tr("Save"), tr("The file could not be written."));
    });
    auto* lay = new QVBoxLayout(dlg);
    lay->addWidget(text);
    lay->addWidget(buttons);
    dlg->resize(640, 480);
    dlg->show();
}

void Ed3kRoomMediaView::showBig(Item& it, QWidget* parent)
{
    auto* dlg = new QDialog(parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(it.ref.name);
    auto* label = new QLabel(dlg);
    label->setAlignment(Qt::AlignCenter);
    auto* lay = new QVBoxLayout(dlg);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(label);
    const QScreen* screen = parent ? parent->screen() : QGuiApplication::primaryScreen();
    const QSize room = screen ? screen->availableGeometry().size() * 0.85 : QSize(1024, 768);
    const qreal dpr = label->devicePixelRatioF();
    auto* buffer = new QBuffer(dlg);
    buffer->setData(it.bytes);
    buffer->open(QIODevice::ReadOnly);
    const QString type = MediaSniff::type(it.bytes);
    QSize shown = QSize(it.ref.width, it.ref.height).isValid() && it.ref.width > 0 ? QSize(it.ref.width, it.ref.height) : it.box;
    shown = shown.scaled(shown.boundedTo(room), Qt::KeepAspectRatio);
    if (MediaSniff::isVideo(type)) {
        // clicked: with sound now
        auto* player = new QMediaPlayer(dlg);
        auto* sink = new QVideoSink(dlg);
        auto* audio = new QAudioOutput(dlg);
        player->setAudioOutput(audio);
        player->setVideoSink(sink);
        player->setLoops(QMediaPlayer::Infinite);
        connect(sink, &QVideoSink::videoFrameChanged, label, [label, shown, dpr](const QVideoFrame& f) {
            label->setPixmap(QPixmap::fromImage(fitted(f.toImage(), shown, dpr)));
        });
        player->setSourceDevice(buffer, QUrl(type == u"video/webm" ? QStringLiteral("media.webm") : QStringLiteral("media.mp4")));
        player->play();
    } else if (it.movie) {
        auto* movie = new QMovie(buffer, QByteArray(), dlg);
        movie->setScaledSize(shown);
        label->setMovie(movie);
        movie->start();
    } else {
        QImageReader reader(buffer);
        const QImage img = reader.read();
        label->setPixmap(QPixmap::fromImage(fitted(img, shown, dpr)));
    }
    label->setMinimumSize(shown.boundedTo(room));
    dlg->resize(shown);
    dlg->show();
}

} // namespace eMule
