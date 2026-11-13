#include "pch.h"
/// @file SearchResultsProxy.cpp
/// @brief Sort proxy of a search result list that can hide rows by network.

#include "controls/SearchResultsProxy.h"
#include "controls/FilterEdit.h"
#include "controls/SearchResultsModel.h"

namespace eMule {

SearchResultsProxy::SearchResultsProxy(QObject* parent)
    : SortValueProxy(parent)
{
}

void SearchResultsProxy::setNetworkFilter(const NetworkFilter& filter)
{
    if (filter == m_filter)
        return;
    beginFilterChange();
    m_filter = filter;
    endFilterChange(Direction::Rows);
}

void SearchResultsProxy::setTextFilter(const QStringList& tokens, int column)
{
    if (tokens == m_tokens && column == m_tokenColumn)
        return;
    beginFilterChange();
    m_tokens = tokens;
    m_tokenColumn = column;
    endFilterChange(Direction::Rows);
}

void SearchResultsProxy::setKeywords(const QStringList& keywords)
{
    if (keywords == m_keywords)
        return;
    beginFilterChange();
    m_keywords = keywords;
    endFilterChange(Direction::Rows);
}

QStringList SearchResultsProxy::keywordsOf(const QString& expression)
{
    // words; a quoted run is one
    QStringList words;
    QString cur;
    bool quoted = false;
    const auto flush = [&] {
        if (!cur.isEmpty())
            words.append(std::exchange(cur, {}));
    };
    for (const QChar ch : expression) {
        if (ch == u'"') {
            quoted = !quoted;
            flush();
        } else if (quoted) {
            cur.append(ch);
        } else if (ch.isSpace()) {
            flush();
        } else if (ch == u'(' || ch == u')') {
            return {};
        } else {
            cur.append(ch);
        }
    }
    flush();

    QStringList keywords;
    bool skipNext = false;
    for (const QString& word : std::as_const(words)) {
        if (word == u"OR")
            return {};
        if (word == u"AND")
            continue;
        if (word == u"NOT") {
            skipNext = true;
            continue;
        }
        if (std::exchange(skipNext, false) || (word.size() > 1 && word.startsWith(u'-')))
            continue;
        if (const QString folded = foldForMatch(word); !folded.isEmpty())
            keywords.append(folded);
    }
    return keywords;
}

QString SearchResultsProxy::foldForMatch(const QString& text)
{
    QString out;
    out.reserve(text.size());
    bool gap = false;
    for (const QChar ch : text) {
        if (ch.isLetterOrNumber()) {
            if (gap && !out.isEmpty())
                out.append(u' ');
            gap = false;
            out.append(ch.toLower());
        } else {
            gap = true;
        }
    }
    return out;
}

int SearchResultsProxy::hiddenCount() const
{
    return sourceModel() ? sourceModel()->rowCount() - rowCount() : 0;
}

bool SearchResultsProxy::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    // A name row goes wherever its file goes
    if (sourceParent.isValid())
        return true;

    if (!m_tokens.isEmpty()) {
        const QString target = sourceModel()->index(sourceRow, m_tokenColumn).data().toString();
        if (!FilterEdit::matches(m_tokens, target))
            return false;
    }

    // an indexer list has rows of one network only
    const auto* model = qobject_cast<const SearchResultsModel*>(sourceModel());
    const SearchResultRow* row = model ? model->resultAt(sourceRow) : nullptr;
    if (!row)
        return true;

    // a release's file without the keywords: listed only for its release's name
    if (m_filter.hideRelatedContent && !m_keywords.isEmpty() && row->isMetaFileRow()) {
        const QString name = foldForMatch(row->fileName);
        for (const QString& kw : m_keywords) {
            if (!name.contains(kw))
                return false;
        }
    }

    if (row->isEd3k() && row->ed3kHidden)
        return m_filter.ed3kHidden;
    if (row->isUsenet())
        return m_filter.usenet;
    if (row->isTorrent())
        return m_filter.torrent;
    if (row->isWeb() || row->isContainer() || row->webOrigin)
        return m_filter.web;
    if (row->kadOrigin)
        return m_filter.kad;
    return true;
}

bool SearchResultsProxy::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
    if (!qobject_cast<const SearchResultsModel*>(sourceModel()))
        return SortValueProxy::lessThan(left, right);

    // Name rows: by name or AICH when that is the sort column, otherwise the most
    // available first (MFC CompareChild, SearchListCtrl.cpp:604-618).
    if (left.parent().isValid()) {
        const int column = left.column();
        if (column == SearchResultsModel::ColFileName || column == SearchResultsModel::ColAichHash)
            return SortValueProxy::lessThan(left, right);
        const auto sources = [](const QModelIndex& idx) {
            return idx.siblingAtColumn(SearchResultsModel::ColAvailability)
                .data(Qt::UserRole).toLongLong();
        };
        return sources(left) > sources(right);
    }

    // Spam stays at the bottom in either direction (SearchListCtrl.cpp:622-629)
    const bool leftSpam = left.data(SearchResultsModel::SpamRole).toBool();
    const bool rightSpam = right.data(SearchResultsModel::SpamRole).toBool();
    if (leftSpam != rightSpam)
        return (sortOrder() == Qt::AscendingOrder) ? rightSpam : leftSpam;

    return SortValueProxy::lessThan(left, right);
}

} // namespace eMule
