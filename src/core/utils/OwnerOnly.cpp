#include "pch.h"
/// @file OwnerOnly.cpp
/// @brief Owner-only access for secret files — implementation.

#include "utils/OwnerOnly.h"
#include "utils/ArgStr.h"

#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QRandomGenerator>
#include <QSaveFile>

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

namespace eMule {

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

/// One ACE in @p buf: all access for the current user. nullptr (with @p error): not made.
PACL ownerOnlyAcl(std::vector<std::uint64_t>& user, std::vector<DWORD>& buf, QString* error)
{
    user = currentUser();
    if (user.empty())
        return fail(error, QStringLiteral("no user token (error %1)").arg(GetLastError())), nullptr;
    PSID sid = reinterpret_cast<const TOKEN_USER*>(user.data())->User.Sid;
    if (!sid || !IsValidSid(sid))
        return fail(error, QStringLiteral("no user SID")), nullptr;
    const DWORD size = DWORD(sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD) + GetLengthSid(sid));
    buf.assign((std::size_t(size) + sizeof(DWORD) - 1) / sizeof(DWORD), 0);   // DWORD-aligned
    auto* acl = reinterpret_cast<PACL>(buf.data());
    if (!InitializeAcl(acl, size, ACL_REVISION) || !AddAccessAllowedAce(acl, ACL_REVISION, FILE_ALL_ACCESS, sid))
        return fail(error, QStringLiteral("ACL not made (error %1)").arg(GetLastError())), nullptr;
    return acl;
}
#endif

} // namespace

bool makeOwnerOnly(const QString& path, QString* error)
{
    if (!QFile::exists(path))
        return fail(error, QStringLiteral("%1 does not exist").arg(path));
#ifdef Q_OS_WIN
    // protected: nothing inherited from the folder
    std::vector<std::uint64_t> user;
    std::vector<DWORD> aclBuf;
    PACL acl = ownerOnlyAcl(user, aclBuf, error);
    if (!acl)
        return false;
    std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    const DWORD err = SetNamedSecurityInfoW(native.data(), SE_FILE_OBJECT,
                                            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr,
                                            nullptr, acl, nullptr);
    if (err != ERROR_SUCCESS)
        return fail(error, QStringLiteral("owner-only DACL of %1 not set (error %2)").arg(argStr(path), argStr(err)));
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

bool isOwnerOnly(const QString& path)
{
    if (!QFile::exists(path))
        return false;
#ifdef Q_OS_WIN
    const std::vector<std::uint64_t> user = currentUser();
    if (user.empty())
        return false;
    PSID sid = reinterpret_cast<const TOKEN_USER*>(user.data())->User.Sid;
    std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (GetNamedSecurityInfoW(native.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl,
                              nullptr, &sd) != ERROR_SUCCESS)
        return false;
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    // NULL DACL: everyone; not protected: the folder's ACEs flow in later
    bool ok = dacl && GetSecurityDescriptorControl(sd, &control, &revision) && (control & SE_DACL_PROTECTED);
    bool userAllowed = false;
    for (DWORD i = 0; ok && i < dacl->AceCount; ++i) {
        void* ace = nullptr;
        if (!GetAce(dacl, i, &ace)) {
            ok = false;
            break;
        }
        const auto* header = static_cast<const ACE_HEADER*>(ace);
        if (header->AceType == ACCESS_DENIED_ACE_TYPE)
            continue;   // denying takes nothing away from owner-only
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) {
            ok = false;
            break;
        }
        PSID aceSid = &static_cast<ACCESS_ALLOWED_ACE*>(ace)->SidStart;
        if (EqualSid(aceSid, sid))
            userAllowed = true;
        else
            ok = false;   // someone else is let in
    }
    LocalFree(sd);
    return ok && userAllowed;
#else
    constexpr auto others = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
                            | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
    const auto perms = QFile::permissions(path);
    return (perms & QFileDevice::ReadOwner) && !(perms & others);
#endif
}

bool writeOwnerOnly(const QString& path, QByteArrayView data, QString* error)
{
#ifdef Q_OS_WIN
    // temp file born with the protected owner-only DACL; a same-volume rename keeps it
    std::vector<std::uint64_t> user;
    std::vector<DWORD> aclBuf;
    PACL acl = ownerOnlyAcl(user, aclBuf, error);
    if (!acl)
        return false;
    SECURITY_DESCRIPTOR sd{};
    if (!InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION) || !SetSecurityDescriptorDacl(&sd, TRUE, acl, FALSE)
        || !SetSecurityDescriptorControl(&sd, SE_DACL_PROTECTED, SE_DACL_PROTECTED))
        return fail(error, QStringLiteral("security descriptor not made (error %1)").arg(GetLastError()));
    SECURITY_ATTRIBUTES sa{sizeof(sa), &sd, FALSE};
    const QString tmp = QStringLiteral("%1.%2.tmp").arg(argStr(path), argFmt(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0')));
    const std::wstring nativeTmp = QDir::toNativeSeparators(tmp).toStdWString();
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    HANDLE h = CreateFileW(nativeTmp.c_str(), GENERIC_WRITE, 0, &sa, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return fail(error, QStringLiteral("%1 not created (error %2)").arg(tmp).arg(GetLastError()));
    bool ok = true;
    for (qsizetype done = 0; ok && done < data.size();) {
        DWORD wrote = 0;
        const DWORD chunk = DWORD(std::min<qsizetype>(data.size() - done, 1 << 30));
        ok = WriteFile(h, data.data() + done, chunk, &wrote, nullptr) && wrote > 0;
        done += wrote;
    }
    ok = ok && FlushFileBuffers(h);
    const DWORD writeErr = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(h);
    if (ok && !MoveFileExW(nativeTmp.c_str(), native.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD moveErr = GetLastError();
        DeleteFileW(nativeTmp.c_str());
        return fail(error, QStringLiteral("%1 not replaced (error %2)").arg(argStr(path), argStr(moveErr)));
    }
    if (!ok) {
        DeleteFileW(nativeTmp.c_str());
        return fail(error, QStringLiteral("%1 not written (error %2)").arg(tmp).arg(writeErr));
    }
    return true;
#else
    // temp file 0600 before the first byte, then renamed
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return fail(error, QStringLiteral("%1 not opened: %2").arg(path, f.errorString()));
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (f.write(data.data(), data.size()) != data.size()) {
        f.cancelWriting();
        return fail(error, QStringLiteral("%1 not written: %2").arg(path, f.errorString()));
    }
    if (!f.commit())
        return fail(error, QStringLiteral("%1 not committed: %2").arg(path, f.errorString()));
    return true;
#endif
}

} // namespace eMule
