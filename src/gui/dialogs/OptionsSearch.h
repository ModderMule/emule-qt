#pragma once

/// @file OptionsSearch.h
/// @brief Search box of the Options window (issue #193, like BiglyBT's settings search).
///
/// Typing filters the sidebar to the pages holding a match and highlights the matching
/// labels, check boxes, buttons, group titles, tab titles and settings-tree rows on the
/// page shown. Every word typed must occur in one control's text or tooltip. The pages
/// are walked live on each search, so a label that changed text is found by its new one.

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QPalette>

class QLineEdit;
class QStackedWidget;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;

namespace eMule {

class AccordionSidebar;

/// Set to true on a QTreeWidget whose rows are settings (not user data) to search them.
/// OptionsTree rows are searched without it.
inline constexpr char kOptionsSearchItems[] = "optionsSearchItems";

class OptionsSearch : public QObject {
    Q_OBJECT

public:
    /// @p pages: stack index = sidebar item id, as OptionsDialog keeps them.
    OptionsSearch(QLineEdit* edit, QStackedWidget* pages, AccordionSidebar* sidebar,
                  QObject* parent = nullptr);
    ~OptionsSearch() override;

    /// Search now, no typing delay (tests, programmatic use).
    void search(const QString& query);

    /// Matching page ids in sidebar order; empty while nothing is searched or nothing matched.
    [[nodiscard]] QList<int> matchingPages() const;
    /// Controls highlighted on @p page (tree rows and tabs count their view / tab widget).
    [[nodiscard]] QList<QWidget*> highlightedWidgets(int page) const;
    /// Tree rows matched on @p page.
    [[nodiscard]] QList<QTreeWidgetItem*> highlightedItems(int page) const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// One highlight: a control, a settings-tree row, a tab title or a group box title.
    struct Match {
        QPointer<QWidget> widget;
        QTreeWidgetItem*  item = nullptr;   ///< row of widget (a QTreeWidget)
        int               tab = -1;         ///< tab of widget (a QTabWidget)
        bool              titleOnly = false;  ///< widget is a QGroupBox: its title strip
    };

    class Overlay;

    void apply();
    void clearMatches();
    [[nodiscard]] QList<Match> scanPage(QWidget* root, const QStringList& terms) const;
    /// Bring the first match of @p page into view: its tab, scroll position, tree row.
    void reveal(int page);
    void connectRepaints();
    /// Highlight rectangles of the current page in overlay coordinates.
    [[nodiscard]] QList<QRect> highlightRects() const;
    void showNoMatch(bool noMatch);

    QLineEdit*        m_edit = nullptr;
    QStackedWidget*   m_pages = nullptr;
    AccordionSidebar* m_sidebar = nullptr;
    QTimer*           m_delay = nullptr;
    Overlay*          m_overlay = nullptr;
    QPalette          m_editPalette;
    QStringList       m_terms;
    QHash<int, QList<Match>> m_matches;
    QList<int>        m_matchingPages;
};

} // namespace eMule
