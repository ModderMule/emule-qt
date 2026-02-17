#pragma once

/// @file CollectionFile.h
/// @brief Collection file entry — replaces MFC CCollectionFile.
///
/// Represents a single file entry within an eMule collection (.emulecollection).
/// Can be constructed from an AbstractFile or deserialized from a FileDataIO stream.

#include "files/AbstractFile.h"

#include <QString>

namespace eMule {

class FileDataIO;

class CollectionFile : public AbstractFile {
public:
    CollectionFile();
    explicit CollectionFile(FileDataIO& data);
    explicit CollectionFile(const AbstractFile* file);

    void updateFileRatingCommentAvail(bool forceUpdate = false) override;

    bool writeCollectionInfo(FileDataIO& data) const;

    bool initFromLink(const QString& link);

    [[nodiscard]] bool hasCollectionExtraInfo() const { return m_hasCollectionExtraInfo; }

private:
    bool m_hasCollectionExtraInfo = false;
};

} // namespace eMule
