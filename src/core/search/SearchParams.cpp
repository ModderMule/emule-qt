#include "pch.h"
/// @file SearchParams.cpp
/// @brief Search query parameters — port of MFC SearchParams.

#include "search/SearchParams.h"
#include "utils/SafeFile.h"

namespace eMule {

// ---------------------------------------------------------------------------
// Persistence — partial serialization for search tab state
// ---------------------------------------------------------------------------

SearchParams::SearchParams(FileDataIO& file)
{
    searchID = file.readUInt32();
    type = static_cast<SearchType>(file.readUInt8());
    clientSharedFiles = file.readUInt8() != 0;
    specialTitle = file.readString(true);
    expression = file.readString(true);
    fileType = file.readString(true);
}

void SearchParams::storePartially(FileDataIO& file) const
{
    file.writeUInt32(searchID);
    file.writeUInt8(static_cast<uint8>(type));
    file.writeUInt8(clientSharedFiles ? 1 : 0);
    file.writeString(specialTitle, UTF8Mode::Raw);
    file.writeString(expression, UTF8Mode::Raw);
    file.writeString(fileType, UTF8Mode::Raw);
}

} // namespace eMule
