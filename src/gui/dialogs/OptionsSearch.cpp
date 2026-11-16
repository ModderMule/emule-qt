#include "pch.h"
/// @file OptionsSearch.cpp
/// @brief Search box of the Options window — see OptionsSearch.h.

#include "dialogs/OptionsSearch.h"
#include "controls/AccordionSidebar.h"
#include "controls/OptionsTree.h"

#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QEvent>
#include <QGroupBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QTreeWidget>

#include <algorithm>

namespace eMule {

namespace {

constexpr int kTypingDelayMs = 150;
constexpr int kRevealMargin  = 24;

/// Plain, case-folded text: rich text stripped, mnemonic '&' dropped ("&&" stays one '&').
QString searchable(const QString& text)
{
    if (text.isEmpty())
        return {};
    QString plain = Qt::mightBeRichText(text)
        ? QTextDocumentFragment::fromHtml(text).toPlainText() : text;
    plain.replace(QStringLiteral("&&"), QStringLiteral("\x01"));
    plain.remove(QLatin1Char('&'));
    plain.replace(QLatin1Char('\x01'), QLatin1Char('&'));
    return plain.toCaseFolded();
}

bool matchesAll(const QString& haystack, const QStringList& terms)
{
    return !haystack.isEmpty()
        && std::ranges::all_of(terms, [&](const QString& t) { return haystack.contains(t); });
}

/// Text plus tooltip of @p w, searchable.
bool widgetMatches(const QString& text, const QWidget* w, const QStringList& terms)
{
    return matchesAll(searchable(text), terms) || matchesAll(searchable(w->toolTip()), terms);
}

/// On the page at all: not hidden on purpose (platform-only controls), not in a pop-up.
/// A page of a stacked widget (a tab) counts; reveal() switches to it.
bool onPage(const QWidget* w, const QWidget* root)
{
    for (const QWidget* a = w; a && a != root; a = a->parentWidget()) {
        if (a->isWindow())
            return false;
        const bool explicitHidden = a->testAttribute(Qt::WA_WState_Hidden)
                                 && a->testAttribute(Qt::WA_WState_ExplicitShowHide);
        if (explicitHidden && !qobject_cast<const QStackedWidget*>(a->parentWidget()))
            return false;
    }
    return true;
}

/// On the tab the page shows now: no hidden ancestor at all.
bool onShownTab(const QWidget* w, const QWidget* root)
{
    for (const QWidget* a = w; a && a != root; a = a->parentWidget())
        if (a->testAttribute(Qt::WA_WState_Hidden) && a->testAttribute(Qt::WA_WState_ExplicitShowHide))
            return false;
    return true;
}

bool searchesItems(const QTreeWidget* tree)
{
    return qobject_cast<const OptionsTree*>(tree) || tree->property(kOptionsSearchItems).toBool();
}

} // namespace

/// Paints the highlights over the page stack; clicks go through.
class OptionsSearch::Overlay : public QWidget {
public:
    Overlay(OptionsSearch* search, QWidget* parent)
        : QWidget(parent), m_search(search)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
        hide();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        // amber marker reads on light and dark palettes alike
        p.setPen(QPen(QColor(230, 150, 0), 1.5));
        p.setBrush(QColor(255, 200, 0, 70));
        for (const QRect& r : m_search->highlightRects())
            p.drawRoundedRect(QRectF(r).adjusted(0.75, 0.75, -0.75, -0.75), 3, 3);
    }

private:
    OptionsSearch* m_search;
};

OptionsSearch::OptionsSearch(QLineEdit* edit, QStackedWidget* pages, AccordionSidebar* sidebar,
                             QObject* parent)
    : QObject(parent)
    , m_edit(edit)
    , m_pages(pages)
    , m_sidebar(sidebar)
{
    m_editPalette = m_edit->palette();
    m_edit->setClearButtonEnabled(true);
    m_edit->installEventFilter(this);

    m_delay = new QTimer(this);
    m_delay->setSingleShot(true);
    m_delay->setInterval(kTypingDelayMs);
    connect(m_delay, &QTimer::timeout, this, &OptionsSearch::apply);
    connect(m_edit, &QLineEdit::textChanged, m_delay, qOverload<>(&QTimer::start));

    m_overlay = new Overlay(this, m_pages);
    m_overlay->setGeometry(m_pages->rect());
    m_pages->installEventFilter(this);

    connect(m_sidebar, &AccordionSidebar::currentItemChanged, this, [this](int id) {
        if (!m_matchingPages.isEmpty())
            reveal(id);
        m_overlay->update();
    });
    connectRepaints();
}

OptionsSearch::~OptionsSearch() = default;

void OptionsSearch::search(const QString& query)
{
    {
        const QSignalBlocker block(m_edit);
        m_edit->setText(query);
    }
    m_delay->stop();
    apply();
}

QList<int> OptionsSearch::matchingPages() const
{
    return m_matchingPages;
}

QList<QWidget*> OptionsSearch::highlightedWidgets(int page) const
{
    QList<QWidget*> widgets;
    for (const Match& m : m_matches.value(page))
        if (m.widget)
            widgets.append(m.widget);
    return widgets;
}

QList<QTreeWidgetItem*> OptionsSearch::highlightedItems(int page) const
{
    QList<QTreeWidgetItem*> items;
    for (const Match& m : m_matches.value(page))
        if (m.widget && m.item)
            items.append(m.item);
    return items;
}

bool OptionsSearch::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_edit && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        // Enter: next matching page, not the dialog's OK; Esc: clear, not Cancel
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
            && !m_edit->text().isEmpty()) {
            m_delay->stop();
            apply();
            if (m_matchingPages.size() > 1)
                m_sidebar->stepCurrentItem(key->modifiers() & Qt::ShiftModifier ? -1 : 1);
            return true;
        }
        if (key->key() == Qt::Key_Escape && !m_edit->text().isEmpty()) {
            m_edit->clear();
            return true;
        }
    } else if (watched == m_pages && event->type() == QEvent::Resize) {
        m_overlay->setGeometry(m_pages->rect());
    } else if (watched != m_edit && watched != m_pages) {
        // a matched control moved, resized, showed or hid
        switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::Hide:
            m_overlay->update();
            break;
        default:
            break;
        }
    }
    return QObject::eventFilter(watched, event);
}

void OptionsSearch::apply()
{
    clearMatches();

    m_terms.clear();
    for (const QString& word : m_edit->text().split(QLatin1Char(' '), Qt::SkipEmptyParts))
        if (const QString t = searchable(word); !t.isEmpty())
            m_terms.append(t);

    const QList<int> ids = m_sidebar->itemIds();
    if (m_terms.isEmpty()) {
        for (int id : ids)
            m_sidebar->setItemHidden(id, false);
        showNoMatch(false);
        m_overlay->hide();
        return;
    }

    for (int id : ids) {
        QWidget* root = m_pages->widget(id);
        if (!root)
            continue;
        QList<Match> found = scanPage(root, m_terms);
        const bool titleMatch = matchesAll(
            searchable(m_sidebar->itemGroupTitle(id) + QLatin1Char(' ') + m_sidebar->itemText(id)),
            m_terms);
        if (found.isEmpty() && !titleMatch)
            continue;
        m_matchingPages.append(id);
        for (const Match& m : found)
            m.widget->installEventFilter(this);
        m_matches.insert(id, std::move(found));
    }

    // Nothing found: keep every page listed, say so in the box.
    showNoMatch(m_matchingPages.isEmpty());
    for (int id : ids)
        m_sidebar->setItemHidden(id, !m_matchingPages.isEmpty() && !m_matchingPages.contains(id));
    if (m_matchingPages.isEmpty()) {
        m_overlay->hide();
        return;
    }

    const int current = m_sidebar->currentItemId();
    if (m_matchingPages.contains(current))
        reveal(current);
    else
        m_sidebar->setCurrentItemId(m_matchingPages.first());   // reveals via the signal

    m_overlay->raise();
    m_overlay->show();
    m_overlay->update();
}

void OptionsSearch::clearMatches()
{
    for (const QList<Match>& list : std::as_const(m_matches))
        for (const Match& m : list)
            if (m.widget)
                m.widget->removeEventFilter(this);
    m_matches.clear();
    m_matchingPages.clear();
}

QList<OptionsSearch::Match> OptionsSearch::scanPage(QWidget* root, const QStringList& terms) const
{
    QList<Match> found;
    for (QWidget* w : root->findChildren<QWidget*>()) {
        if (w == m_overlay || !onPage(w, root))
            continue;

        if (auto* label = qobject_cast<QLabel*>(w)) {
            if (widgetMatches(label->text(), label, terms))
                found.append({w});
        } else if (auto* button = qobject_cast<QAbstractButton*>(w)) {
            if (widgetMatches(button->text(), button, terms))
                found.append({w});
        } else if (auto* box = qobject_cast<QGroupBox*>(w)) {
            if (widgetMatches(box->title(), box, terms))
                found.append({w, nullptr, -1, true});
        } else if (auto* tabs = qobject_cast<QTabWidget*>(w)) {
            for (int i = 0; i < tabs->count(); ++i)
                if (matchesAll(searchable(tabs->tabText(i)), terms)
                    || matchesAll(searchable(tabs->tabToolTip(i)), terms))
                    found.append({w, nullptr, i});
        } else if (auto* tree = qobject_cast<QTreeWidget*>(w); tree && searchesItems(tree)) {
            QList<QTreeWidgetItem*> pending{tree->invisibleRootItem()};
            while (!pending.isEmpty()) {
                QTreeWidgetItem* item = pending.takeFirst();
                for (int c = 0; c < item->childCount(); ++c)
                    pending.append(item->child(c));
                if (item == tree->invisibleRootItem() || item->isHidden())
                    continue;
                for (int col = 0; col < tree->columnCount(); ++col) {
                    if (matchesAll(searchable(item->text(col)), terms)
                        || matchesAll(searchable(item->toolTip(col)), terms)) {
                        found.append({w, item});
                        break;
                    }
                }
            }
        } else if (!w->toolTip().isEmpty() && matchesAll(searchable(w->toolTip()), terms)) {
            found.append({w});   // an input explained only by its tooltip
        }
    }
    return found;
}

void OptionsSearch::reveal(int page)
{
    QWidget* root = m_pages->widget(page);
    const QList<Match> matches = m_matches.value(page);
    if (!root || matches.isEmpty())
        return;

    // Stay on the shown tab while it holds a match; else open the first match's tab.
    const auto shown = std::ranges::find_if(matches, [root](const Match& m) {
        return m.widget && onShownTab(m.widget, root);
    });
    const Match& target = shown != matches.end() ? *shown : matches.first();
    if (!target.widget)
        return;

    if (shown == matches.end()) {
        for (QWidget* child = target.widget; child && child != root; child = child->parentWidget()) {
            auto* stack = qobject_cast<QStackedWidget*>(child->parentWidget());
            if (!stack)
                continue;
            if (auto* tabs = qobject_cast<QTabWidget*>(stack->parentWidget()))
                tabs->setCurrentWidget(child);
            else
                stack->setCurrentWidget(child);
        }
    }

    if (auto* tree = qobject_cast<QTreeWidget*>(target.widget.data()); tree && target.item) {
        for (QTreeWidgetItem* p = target.item->parent(); p; p = p->parent())
            p->setExpanded(true);
        tree->scrollToItem(target.item);
    }
    if (auto* area = qobject_cast<QScrollArea*>(root))
        area->ensureWidgetVisible(target.widget, kRevealMargin, kRevealMargin);
}

void OptionsSearch::connectRepaints()
{
    const auto repaint = [this] { if (m_overlay->isVisible()) m_overlay->update(); };
    // QStackedLayout raises the page it shows: over the overlay again
    connect(m_pages, &QStackedWidget::currentChanged, this, [this] {
        m_overlay->raise();
        m_overlay->update();
    });
    for (auto* area : m_pages->findChildren<QAbstractScrollArea*>()) {
        connect(area->verticalScrollBar(), &QScrollBar::valueChanged, this, repaint);
        connect(area->horizontalScrollBar(), &QScrollBar::valueChanged, this, repaint);
        if (auto* tree = qobject_cast<QTreeView*>(area)) {
            connect(tree, &QTreeView::expanded, this, repaint);
            connect(tree, &QTreeView::collapsed, this, repaint);
        }
    }
    for (auto* tabs : m_pages->findChildren<QTabWidget*>())
        connect(tabs, &QTabWidget::currentChanged, this, repaint);
}

QList<QRect> OptionsSearch::highlightRects() const
{
    QList<QRect> rects;
    for (const Match& m : m_matches.value(m_pages->currentIndex())) {
        QWidget* w = m.widget;
        if (!w || !w->isVisible())
            continue;

        QRect r = w->rect();
        if (m.item) {
            auto* tree = static_cast<QTreeWidget*>(w);
            w = tree->viewport();
            r = tree->visualItemRect(m.item);
            if (r.isEmpty())
                continue;   // under a collapsed row
            r.setLeft(0);
        } else if (m.tab >= 0) {
            QTabBar* bar = static_cast<QTabWidget*>(w)->tabBar();
            w = bar;
            r = bar->tabRect(m.tab);
        } else if (m.titleOnly) {
            r.setHeight(w->fontMetrics().height() + 4);
        }

        // clipped by every ancestor: a scrolled-away row draws nothing
        QRect mapped(w->mapTo(m_pages, r.topLeft()), r.size());
        for (QWidget* a = w; a && a != m_pages; a = a->parentWidget())
            mapped &= QRect(a->mapTo(m_pages, QPoint(0, 0)), a->size());
        if (!mapped.isEmpty())
            rects.append(mapped);
    }
    return rects;
}

void OptionsSearch::showNoMatch(bool noMatch)
{
    QPalette pal = m_editPalette;
    if (noMatch)
        pal.setColor(QPalette::Text, QColor(0xD0, 0x30, 0x30));
    m_edit->setPalette(pal);
}

} // namespace eMule
