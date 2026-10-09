#pragma once

/// @file CollectionCategory.h
/// @brief The category a collection's downloads go into — MFC
///        CCollectionViewDialog::DownloadSelected (CollectionViewDialog.cpp:156-169).
///
/// Works on the daemon's category list as GetCategories returns it, so the dialog
/// needs neither the Transfers panel nor a second copy of the list. Header-only.

#include <QCborArray>
#include <QCborMap>
#include <QString>

namespace eMule::CollectionCategory {

/// Index of the category named like the collection, without regard to case; 0 when
/// there is none. Index 0 ("All") never matches, and the last match wins, as in MFC.
[[nodiscard]] inline int indexFor(const QCborArray& categories, const QString& collectionName)
{
    for (qsizetype i = categories.size() - 1; i > 0; --i) {
        if (categories.at(i).toMap().value(QStringLiteral("title")).toString()
                .compare(collectionName, Qt::CaseInsensitive) == 0)
            return static_cast<int>(i);
    }
    return 0;
}

/// The list to send back with SetCategories so that one more category, named
/// @p collectionName, exists at the end. Every existing entry names where it came
/// from ("oldIndex"), which is what keeps downloads and view filters with it.
[[nodiscard]] inline QCborArray withNewCategory(const QCborArray& categories, const QString& collectionName)
{
    QCborArray out;
    for (qsizetype i = 0; i < categories.size(); ++i) {
        QCborMap entry = categories.at(i).toMap();
        entry.insert(QStringLiteral("oldIndex"), i);
        out.append(entry);
    }
    // Title only: the incoming folder stays the default one, as MFC passes it.
    out.append(QCborMap{{QStringLiteral("title"), collectionName}});
    return out;
}

} // namespace eMule::CollectionCategory
