#include "pch.h"
/// @file MetadataPage.cpp
/// @brief ED2K-tag list page — see MetadataPage.h.

#include "MetadataPage.h"

#include "controls/AbstractListView.h"

#include <QCborArray>
#include <QHeaderView>
#include <QLabel>
#include <QTreeWidget>

namespace eMule {

namespace {

/// Map a well-known ED2K tag nameId to a human-readable string.
QString tagNameFromId(int nameId)
{
    static const QHash<int, QString> names{
        {0x01, QStringLiteral("Filename")},
        {0x02, QStringLiteral("File Size")},
        {0x03, QStringLiteral("File Type")},
        {0x04, QStringLiteral("File Format")},
        {0x05, QStringLiteral("Last Seen Complete")},
        {0x08, QStringLiteral("Transferred")},
        {0x09, QStringLiteral("Gap Start")},
        {0x0A, QStringLiteral("Gap End")},
        {0x0B, QStringLiteral("Description")},
        {0x12, QStringLiteral("Part Filename")},
        {0x14, QStringLiteral("Status")},
        {0x15, QStringLiteral("Sources")},
        {0x16, QStringLiteral("Permissions")},
        {0x18, QStringLiteral("Download Priority")},
        {0x19, QStringLiteral("Upload Priority")},
        {0x1A, QStringLiteral("Compression")},
        {0x1B, QStringLiteral("Corrupted")},
        {0x20, QStringLiteral("Kad Last Publish Key")},
        {0x21, QStringLiteral("Kad Last Publish Src")},
        {0x22, QStringLiteral("Flags")},
        {0x23, QStringLiteral("DL Active Time")},
        {0x24, QStringLiteral("Corrupted Parts")},
        {0x25, QStringLiteral("DL Preview")},
        {0x26, QStringLiteral("Kad Last Publish Notes")},
        {0x27, QStringLiteral("AICH Hash")},
        {0x28, QStringLiteral("File Hash")},
        {0x30, QStringLiteral("Complete Sources")},
        {0x31, QStringLiteral("Collection Author")},
        {0x32, QStringLiteral("Collection Author Key")},
        {0x33, QStringLiteral("Publish Info")},
        {0x34, QStringLiteral("Last Shared")},
        {0x35, QStringLiteral("AICH Hashset")},
        {0x38, QStringLiteral("Folder Name")},
        {0x3A, QStringLiteral("File Size Hi")},
        {0x50, QStringLiteral("All-Time Transferred")},
        {0x51, QStringLiteral("All-Time Requested")},
        {0x52, QStringLiteral("All-Time Accepted")},
        {0x53, QStringLiteral("Category")},
        {0x54, QStringLiteral("All-Time Transferred Hi")},
        {0x55, QStringLiteral("Max Sources")},
        {0xD0, QStringLiteral("Media Artist")},
        {0xD1, QStringLiteral("Media Album")},
        {0xD2, QStringLiteral("Media Title")},
        {0xD3, QStringLiteral("Media Length")},
        {0xD4, QStringLiteral("Media Bitrate")},
        {0xD5, QStringLiteral("Media Codec")},
        {0xF6, QStringLiteral("File Comment")},
        {0xF7, QStringLiteral("File Rating")},
    };
    auto it = names.find(nameId);
    return it != names.end() ? *it : QStringLiteral("0x%1").arg(nameId, 2, 16, QLatin1Char('0'));
}

/// Map a TAGTYPE value to a display string.
QString tagTypeName(int type)
{
    switch (type) {
    case 0x01: return QStringLiteral("Hash");
    case 0x02: return QStringLiteral("String");
    case 0x03: return QStringLiteral("UInt32");
    case 0x04: return QStringLiteral("Float");
    case 0x05: return QStringLiteral("Bool");
    case 0x06: return QStringLiteral("Bool Array");
    case 0x07: return QStringLiteral("Blob");
    case 0x08: return QStringLiteral("UInt16");
    case 0x09: return QStringLiteral("UInt8");
    case 0x0A: return QStringLiteral("Blob Unsi");
    case 0x0B: return QStringLiteral("UInt64");
    default:   return QStringLiteral("0x%1").arg(type, 2, 16, QLatin1Char('0'));
    }
}

} // anonymous namespace

QWidget* createMetadataPage(const QCborMap& details, const QString& stateKey)
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);

    const QCborArray tagArr = details.value(QLatin1StringView("tags")).toArray();

    if (tagArr.isEmpty()) {
        layout->addWidget(new QLabel(QCoreApplication::translate(
            "eMule::MetadataPage", "No metadata tags available.")));
        layout->addStretch();
        return page;
    }

    // Spell the context out at every call: lupdate resolves a tr()-shaped
    // lambda against the enclosing namespace and would file these strings
    // under context "eMule", where the runtime lookup never finds them.
    auto* tree = new ListTreeWidget;
    tree->setHeaderLabels({QCoreApplication::translate("eMule::MetadataPage", "Tag Name"),
                           QCoreApplication::translate("eMule::MetadataPage", "Type"),
                           QCoreApplication::translate("eMule::MetadataPage", "Value")});
    tree->setRootIsDecorated(false);
    tree->setAlternatingRowColors(true);
    tree->setSortingEnabled(true);
    tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree->header()->setStretchLastSection(true);
    tree->bindColumns(stateKey, {200, 100, 260});

    for (const auto& entry : tagArr) {
        const QCborMap t = entry.toMap();
        auto* item = new QTreeWidgetItem(tree);

        // Tag name: prefer string name, fall back to numeric nameId
        const QString tagName = t.value(QLatin1StringView("name")).toString();
        const qint64 nameId = t.value(QLatin1StringView("nameId")).toInteger();
        if (!tagName.isEmpty())
            item->setText(0, tagName);
        else if (nameId > 0)
            item->setText(0, tagNameFromId(static_cast<int>(nameId)));
        else
            item->setText(0, QCoreApplication::translate("eMule::MetadataPage", "Unknown"));

        // Tag type
        const int tagType = static_cast<int>(t.value(QLatin1StringView("type")).toInteger());
        item->setText(1, tagTypeName(tagType));

        // Tag value
        if (t.contains(QLatin1StringView("strValue")))
            item->setText(2, t.value(QLatin1StringView("strValue")).toString());
        else if (t.contains(QLatin1StringView("intValue")))
            item->setText(2, QString::number(t.value(QLatin1StringView("intValue")).toInteger()));
        else if (t.contains(QLatin1StringView("floatValue")))
            item->setText(2, QString::number(t.value(QLatin1StringView("floatValue")).toDouble(), 'g', 6));
        else if (t.contains(QLatin1StringView("hashValue")))
            item->setText(2, t.value(QLatin1StringView("hashValue")).toString());
    }

    tree->sortByColumn(0, Qt::AscendingOrder);
    layout->addWidget(tree);
    return page;
}

} // namespace eMule
