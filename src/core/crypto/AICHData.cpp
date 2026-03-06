#include "pch.h"
#include "AICHData.h"
#include "utils/SafeFile.h"

namespace eMule {

void AICHHash::read(FileDataIO& file)
{
    file.read(m_buffer.data(), kAICHHashSize);
}

void AICHHash::write(FileDataIO& file) const
{
    file.write(m_buffer.data(), kAICHHashSize);
}

void AICHHash::skip(qint64 distance, FileDataIO& file)
{
    file.seek(distance, 1); // SEEK_CUR
}

} // namespace eMule
