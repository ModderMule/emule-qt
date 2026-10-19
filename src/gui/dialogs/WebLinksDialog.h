#pragma once

/// @file WebLinksDialog.h
/// @brief The links behind an eNode web search row.
///
/// The daemon fetched and verified the row's text/uri-list and parsed it; this
/// shows the links (file, name, size, hoster) and offers Copy Links, Open in
/// Browser and Save List. Mirrors of one file share a file number.

#include <QByteArray>
#include <QCborMap>
#include <QDialog>
#include <QString>
#include <QStringList>

class QPushButton;
class QTreeWidget;

namespace eMule {

class WebLinksDialog : public QDialog {
    Q_OBJECT

public:
    /// @p links: the daemon's map — title, protected, links [{url,file,name,size,hoster}].
    /// @p content: the uri-list as served, for Save List; @p fileName its suggested name.
    WebLinksDialog(const QString& rowName, const QCborMap& links, const QByteArray& content,
                   const QString& fileName, QWidget* parent = nullptr);

private:
    /// Whether any listed link is on a Tor hidden service.
    [[nodiscard]] bool hasOnionLink() const;
    /// The selected links, or every link when none is selected.
    [[nodiscard]] QStringList chosenUrls() const;
    void copyLinks();
    void openLinks();
    void saveList();

    QTreeWidget* m_list = nullptr;
    QPushButton* m_openBtn = nullptr;
    QByteArray m_content;
    QString m_fileName;
};

} // namespace eMule
