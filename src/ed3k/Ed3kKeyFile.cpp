/// @file Ed3kKeyFile.cpp
/// @brief Owner-only access for secret files — implementation.

#include "Ed3kKeyFile.h"

#include <QDir>
#include <QFile>
#include <QFileDevice>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>

#include <cstdint>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "advapi32.lib")
#endif
#endif

namespace eMule::ed3k {

namespace {

bool fail(QString* error, const QString& what)
{
    if (error)
        *error = what;
    return false;
}

#ifdef Q_OS_WIN
/// The TOKEN_USER of this process in an 8-aligned buffer; empty: not read.
std::vector<std::uint64_t> currentUser()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return {};
    DWORD len = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &len);   // the size needed
    std::vector<std::uint64_t> buf((std::size_t(len) + 7) / 8);
    const bool got = !buf.empty()
                     && GetTokenInformation(token, TokenUser, buf.data(), DWORD(buf.size() * sizeof(std::uint64_t)), &len);
    CloseHandle(token);
    if (!got)
        buf.clear();
    return buf;
}
#endif

} // namespace

bool ownerOnly(const QString& path, QString* error)
{
    if (!QFile::exists(path))
        return fail(error, QStringLiteral("%1 does not exist").arg(path));
#ifdef Q_OS_WIN
    // one ACE: all access for the current user; protected: nothing inherited from the folder
    const std::vector<std::uint64_t> user = currentUser();
    if (user.empty())
        return fail(error, QStringLiteral("no user token (error %1)").arg(GetLastError()));
    PSID sid = reinterpret_cast<const TOKEN_USER*>(user.data())->User.Sid;
    if (!sid || !IsValidSid(sid))
        return fail(error, QStringLiteral("no user SID"));
    const DWORD size = DWORD(sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD) + GetLengthSid(sid));
    std::vector<DWORD> aclBuf((std::size_t(size) + sizeof(DWORD) - 1) / sizeof(DWORD));   // DWORD-aligned
    auto* acl = reinterpret_cast<PACL>(aclBuf.data());
    if (!InitializeAcl(acl, size, ACL_REVISION) || !AddAccessAllowedAce(acl, ACL_REVISION, FILE_ALL_ACCESS, sid))
        return fail(error, QStringLiteral("ACL not made (error %1)").arg(GetLastError()));
    std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    const DWORD err = SetNamedSecurityInfoW(native.data(), SE_FILE_OBJECT,
                                            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr,
                                            nullptr, acl, nullptr);
    if (err != ERROR_SUCCESS)
        return fail(error, QStringLiteral("owner-only DACL of %1 not set (error %2)").arg(path).arg(err));
    return true;
#else
    // made 0600 already (open(2), QSaveFile); one copied in is mended
    constexpr auto others = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
                            | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
    if (!(QFile::permissions(path) & others))
        return true;
    if (!QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        return fail(error, QStringLiteral("%1 not made owner-only").arg(path));
    return true;
#endif
}

} // namespace eMule::ed3k
