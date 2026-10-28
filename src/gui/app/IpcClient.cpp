#include "pch.h"
/// @file IpcClient.cpp
/// @brief GUI-side IPC client — implementation.

#include "app/IpcClient.h"
#include "app/KnownCores.h"
#include "IpcTls.h"

#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QPointer>
#include <QSslConfiguration>

#include <memory>
#include <utility>


namespace eMule {

static QString ipcMsgTypeName(Ipc::IpcMsgType type)
{
    using T = Ipc::IpcMsgType;
    switch (type) {
    case T::Handshake:            return QStringLiteral("Handshake");
    case T::Ping:                 return QStringLiteral("Ping");
    case T::GetDownloads:         return QStringLiteral("GetDownloads");
    case T::GetDownload:          return QStringLiteral("GetDownload");
    case T::PauseDownload:        return QStringLiteral("PauseDownload");
    case T::ResumeDownload:       return QStringLiteral("ResumeDownload");
    case T::CancelDownload:       return QStringLiteral("CancelDownload");
    case T::SetDownloadPriority:  return QStringLiteral("SetDownloadPriority");
    case T::ClearCompleted:       return QStringLiteral("ClearCompleted");
    case T::GetDownloadSources:   return QStringLiteral("GetDownloadSources");
    case T::GetUploads:           return QStringLiteral("GetUploads");
    case T::GetDownloadClients:   return QStringLiteral("GetDownloadClients");
    case T::GetKnownClients:      return QStringLiteral("GetKnownClients");
    case T::GetServers:           return QStringLiteral("GetServers");
    case T::RemoveServer:         return QStringLiteral("RemoveServer");
    case T::RemoveAllServers:     return QStringLiteral("RemoveAllServers");
    case T::SetServerPriority:    return QStringLiteral("SetServerPriority");
    case T::SetServerStatic:      return QStringLiteral("SetServerStatic");
    case T::AddServer:            return QStringLiteral("AddServer");
    case T::SetServerEnabled:     return QStringLiteral("SetServerEnabled");
    case T::GetConnection:        return QStringLiteral("GetConnection");
    case T::ConnectToServer:      return QStringLiteral("ConnectToServer");
    case T::DisconnectFromServer: return QStringLiteral("DisconnectFromServer");
    case T::ReleaseConnectHold: return QStringLiteral("ReleaseConnectHold");
    case T::StartSearch:          return QStringLiteral("StartSearch");
    case T::GetSearchResults:     return QStringLiteral("GetSearchResults");
    case T::StopSearch:           return QStringLiteral("StopSearch");
    case T::SearchMore:           return QStringLiteral("SearchMore");
    case T::RemoveSearch:         return QStringLiteral("RemoveSearch");
    case T::ClearAllSearches:     return QStringLiteral("ClearAllSearches");
    case T::DownloadSearchFile:   return QStringLiteral("DownloadSearchFile");
    case T::GetSharedFiles:       return QStringLiteral("GetSharedFiles");
    case T::SetSharedFilePriority: return QStringLiteral("SetSharedFilePriority");
    case T::ReloadSharedFiles:    return QStringLiteral("ReloadSharedFiles");
    case T::GetEd2kLink:          return QStringLiteral("GetEd2kLink");
    case T::GetFriends:           return QStringLiteral("GetFriends");
    case T::AddFriend:            return QStringLiteral("AddFriend");
    case T::RemoveFriend:         return QStringLiteral("RemoveFriend");
    case T::SendChatMessage:      return QStringLiteral("SendChatMessage");
    case T::SetFriendSlot:        return QStringLiteral("SetFriendSlot");
    case T::EndChatSession:       return QStringLiteral("EndChatSession");
    case T::GetStats:             return QStringLiteral("GetStats");
    case T::GetPreferences:       return QStringLiteral("GetPreferences");
    case T::SetPreferences:       return QStringLiteral("SetPreferences");
    case T::Subscribe:            return QStringLiteral("Subscribe");
    case T::GetKadContacts:       return QStringLiteral("GetKadContacts");
    case T::GetKadStatus:         return QStringLiteral("GetKadStatus");
    case T::GetKadStats:          return QStringLiteral("GetKadStats");
    case T::BootstrapKad:         return QStringLiteral("BootstrapKad");
    case T::DisconnectKad:        return QStringLiteral("DisconnectKad");
    case T::SyncLogs:             return QStringLiteral("SyncLogs");
    case T::Shutdown:             return QStringLiteral("Shutdown");
    case T::GetKadSearches:       return QStringLiteral("GetKadSearches");
    case T::GetKadLookupHistory:  return QStringLiteral("GetKadLookupHistory");
    case T::GetNetworkInfo:       return QStringLiteral("GetNetworkInfo");
    case T::GetNetworkInterfaces: return QStringLiteral("GetNetworkInterfaces");
    case T::RecheckFirewall:      return QStringLiteral("RecheckFirewall");
    case T::ReloadIPFilter:       return QStringLiteral("ReloadIPFilter");
    case T::GetSchedules:         return QStringLiteral("GetSchedules");
    case T::SaveSchedules:        return QStringLiteral("SaveSchedules");
    case T::ScanImportFolder:     return QStringLiteral("ScanImportFolder");
    case T::GetConvertJobs:       return QStringLiteral("GetConvertJobs");
    case T::RemoveConvertJob:     return QStringLiteral("RemoveConvertJob");
    case T::RetryConvertJob:      return QStringLiteral("RetryConvertJob");
    case T::StopDownload:         return QStringLiteral("StopDownload");
    case T::OpenDownloadFile:     return QStringLiteral("OpenDownloadFile");
    case T::OpenDownloadFolder:   return QStringLiteral("OpenDownloadFolder");
    case T::MarkSearchSpam:       return QStringLiteral("MarkSearchSpam");
    case T::ResetStats:           return QStringLiteral("ResetStats");
    case T::RestoreStats:         return QStringLiteral("RestoreStats");
    case T::ProbeHttpCacheServer: return QStringLiteral("ProbeHttpCacheServer");
    case T::ApplyHttpCacheConfig: return QStringLiteral("ApplyHttpCacheConfig");
    case T::RenameSharedFile:     return QStringLiteral("RenameSharedFile");
    case T::RenameDownload:       return QStringLiteral("RenameDownload");
    case T::RenameFiles:          return QStringLiteral("RenameFiles");
    case T::DeleteSharedFile:     return QStringLiteral("DeleteSharedFile");
    case T::UnshareFile:          return QStringLiteral("UnshareFile");
    case T::SetDownloadCategory:  return QStringLiteral("SetDownloadCategory");
    case T::GetCategories:        return QStringLiteral("GetCategories");
    case T::SetCategories:        return QStringLiteral("SetCategories");
    case T::SetCategoryStatus:    return QStringLiteral("SetCategoryStatus");
    case T::GetDownloadDetails:   return QStringLiteral("GetDownloadDetails");
    case T::PreviewDownload:      return QStringLiteral("PreviewDownload");
    case T::RequestClientSharedFiles: return QStringLiteral("RequestClientSharedFiles");
    case T::RequestSearchPreview: return QStringLiteral("RequestSearchPreview");
    case T::GetClientDetails:     return QStringLiteral("GetClientDetails");
    case T::HandshakeOk:          return QStringLiteral("HandshakeOk");
    case T::Result:               return QStringLiteral("Result");
    case T::Error:                return QStringLiteral("Error");
    case T::PushStatsUpdate:      return QStringLiteral("PushStatsUpdate");
    case T::PushDownloadUpdate:   return QStringLiteral("PushDownloadUpdate");
    case T::PushDownloadAdded:    return QStringLiteral("PushDownloadAdded");
    case T::PushDownloadRemoved:  return QStringLiteral("PushDownloadRemoved");
    case T::GetKnownFiles:        return QStringLiteral("GetKnownFiles");
    case T::RecoverArchivePreview: return QStringLiteral("RecoverArchivePreview");
    case T::SwapA4AFToFile:       return QStringLiteral("SwapA4AFToFile");
    case T::UpdateIpFilterNow:    return QStringLiteral("UpdateIpFilterNow");
    case T::RehashAllSharedFiles: return QStringLiteral("RehashAllSharedFiles");
    case T::PushNotifierEvent:    return QStringLiteral("PushNotifierEvent");
    case T::PushServerState:      return QStringLiteral("PushServerState");
    case T::PushSearchResult:     return QStringLiteral("PushSearchResult");
    case T::PushLogMessage:       return QStringLiteral("PushLogMessage");
    case T::PushLogBatch:         return QStringLiteral("PushLogBatch");
    case T::PushCoreLoading:      return QStringLiteral("PushCoreLoading");
    case T::PushCoreRestarting:   return QStringLiteral("PushCoreRestarting");
    case T::PushSharedFileUpdate: return QStringLiteral("PushSharedFileUpdate");
    case T::PushSharedFileRemoved: return QStringLiteral("PushSharedFileRemoved");
    case T::PushUploadUpdate:     return QStringLiteral("PushUploadUpdate");
    case T::PushKadUpdate:        return QStringLiteral("PushKadUpdate");
    case T::PushKadSearchesChanged: return QStringLiteral("PushKadSearchesChanged");
    case T::PushKnownClientsChanged: return QStringLiteral("PushKnownClientsChanged");
    case T::PushChatMessage:      return QStringLiteral("PushChatMessage");
    case T::PushChatState:        return QStringLiteral("PushChatState");
    case T::PushChatCaptcha:      return QStringLiteral("PushChatCaptcha");
    case T::PushChatCaptchaResult: return QStringLiteral("PushChatCaptchaResult");
    case T::PushFriendListChanged: return QStringLiteral("PushFriendListChanged");
    case T::PushClientSharedFiles: return QStringLiteral("PushClientSharedFiles");
    case T::PushSearchPreview:    return QStringLiteral("PushSearchPreview");
    case T::PushPortMapStatus: return QStringLiteral("PushPortMapStatus");
    case T::GetIndexers:          return QStringLiteral("GetIndexers");
    case T::SetIndexers:          return QStringLiteral("SetIndexers");
    case T::TestIndexer:          return QStringLiteral("TestIndexer");
    case T::GetIndexerCaps:       return QStringLiteral("GetIndexerCaps");
    case T::StartIndexerSearch:   return QStringLiteral("StartIndexerSearch");
    case T::StopIndexerSearch:    return QStringLiteral("StopIndexerSearch");
    case T::RemoveIndexerSearch:  return QStringLiteral("RemoveIndexerSearch");
    case T::GrabIndexerResult:    return QStringLiteral("GrabIndexerResult");
    case T::PushIndexerResults:   return QStringLiteral("PushIndexerResults");
    case T::PushIndexerProgress:  return QStringLiteral("PushIndexerProgress");
    case T::PushIndexerSearchDone: return QStringLiteral("PushIndexerSearchDone");
    case T::PushIndexerFeedStatus: return QStringLiteral("PushIndexerFeedStatus");
    case T::GetNewsServers:       return QStringLiteral("GetNewsServers");
    case T::SetNewsServers:       return QStringLiteral("SetNewsServers");
    case T::TestNewsServer:       return QStringLiteral("TestNewsServer");
    case T::SetNewsServerUsage:   return QStringLiteral("SetNewsServerUsage");
    case T::GetUsenetStats:       return QStringLiteral("GetUsenetStats");
    case T::GetUsenetQueue:       return QStringLiteral("GetUsenetQueue");
    case T::GetUsenetKnownTypes:  return QStringLiteral("GetUsenetKnownTypes");
    case T::AddNzb:               return QStringLiteral("AddNzb");
    case T::AddNzbUrl:            return QStringLiteral("AddNzbUrl");
    case T::RemoveUsenetItem:     return QStringLiteral("RemoveUsenetItem");
    case T::PauseUsenetItem:      return QStringLiteral("PauseUsenetItem");
    case T::ResumeUsenetItem:     return QStringLiteral("ResumeUsenetItem");
    case T::SetUsenetItemPriority: return QStringLiteral("SetUsenetItemPriority");
    case T::SetUsenetItemPassword: return QStringLiteral("SetUsenetItemPassword");
    case T::CheckUsenetItem:      return QStringLiteral("CheckUsenetItem");
    case T::ListUsenetArchiveEntries: return QStringLiteral("ListUsenetArchiveEntries");
    case T::PushUsenetQueueItem:  return QStringLiteral("PushUsenetQueueItem");
    case T::PushUsenetItemRemoved: return QStringLiteral("PushUsenetItemRemoved");
    case T::PushUsenetItemFinished: return QStringLiteral("PushUsenetItemFinished");
    case T::PushUsenetEngineState: return QStringLiteral("PushUsenetEngineState");
    case T::SetUsenetFilesSkipped: return QStringLiteral("SetUsenetFilesSkipped");
    case T::SetUsenetPaused:      return QStringLiteral("SetUsenetPaused");
    case T::InspectNzb:           return QStringLiteral("InspectNzb");
    case T::ClearUsenetCompleted: return QStringLiteral("ClearUsenetCompleted");
    case T::GetTorrents:          return QStringLiteral("GetTorrents");
    case T::AddTorrentFile:       return QStringLiteral("AddTorrentFile");
    case T::AddMagnet:            return QStringLiteral("AddMagnet");
    case T::RemoveTorrent:        return QStringLiteral("RemoveTorrent");
    case T::PauseTorrent:         return QStringLiteral("PauseTorrent");
    case T::ResumeTorrent:        return QStringLiteral("ResumeTorrent");
    case T::GetTorrentDetails:    return QStringLiteral("GetTorrentDetails");
    case T::SetTorrentFilePriorities: return QStringLiteral("SetTorrentFilePriorities");
    case T::RecheckTorrent:       return QStringLiteral("RecheckTorrent");
    case T::ReannounceTorrent:    return QStringLiteral("ReannounceTorrent");
    case T::SetTorrentOptions:    return QStringLiteral("SetTorrentOptions");
    case T::AddTorrentTrackers:   return QStringLiteral("AddTorrentTrackers");
    case T::RemoveTorrentTrackers: return QStringLiteral("RemoveTorrentTrackers");
    case T::GetBtStatus:          return QStringLiteral("GetBtStatus");
    case T::GetBtStats:           return QStringLiteral("GetBtStats");
    case T::PushTorrentAdded:     return QStringLiteral("PushTorrentAdded");
    case T::PushTorrentUpdate:    return QStringLiteral("PushTorrentUpdate");
    case T::PushTorrentRemoved:   return QStringLiteral("PushTorrentRemoved");
    case T::PushBtStatus:         return QStringLiteral("PushBtStatus");
    case T::PushTorrentFinished:  return QStringLiteral("PushTorrentFinished");
    case T::GetEd3kStatus:        return QStringLiteral("GetEd3kStatus");
    case T::PushEd3kStatus:       return QStringLiteral("PushEd3kStatus");
    case T::OpenEd3kNodesLink:    return QStringLiteral("OpenEd3kNodesLink");
    case T::AnswerEd3kBootstrapAsk: return QStringLiteral("AnswerEd3kBootstrapAsk");
    case T::PushEd3kBootstrapAsk: return QStringLiteral("PushEd3kBootstrapAsk");
    case T::GetEd3kFriends:       return QStringLiteral("GetEd3kFriends");
    case T::AddEd3kFriend:        return QStringLiteral("AddEd3kFriend");
    case T::RemoveEd3kFriend:     return QStringLiteral("RemoveEd3kFriend");
    case T::AnswerEd3kFriendRequest: return QStringLiteral("AnswerEd3kFriendRequest");
    case T::GetEd3kMyFriendLink:  return QStringLiteral("GetEd3kMyFriendLink");
    case T::SendEd3kChat:         return QStringLiteral("SendEd3kChat");
    case T::EndEd3kChat:          return QStringLiteral("EndEd3kChat");
    case T::PushEd3kFriendsChanged: return QStringLiteral("PushEd3kFriendsChanged");
    case T::PushEd3kChatMessage:  return QStringLiteral("PushEd3kChatMessage");
    case T::PushEd3kChatSent:     return QStringLiteral("PushEd3kChatSent");
    case T::PushEd3kChatFailed:   return QStringLiteral("PushEd3kChatFailed");
    case T::PushEd3kFriendRequest: return QStringLiteral("PushEd3kFriendRequest");
    case T::PushEd3kChatState:    return QStringLiteral("PushEd3kChatState");
    case T::ResolveEd3kName:      return QStringLiteral("ResolveEd3kName");
    case T::OpenEd3kName:         return QStringLiteral("OpenEd3kName");
    case T::GetEd3kNames:         return QStringLiteral("GetEd3kNames");
    case T::SetEd3kPetname:       return QStringLiteral("SetEd3kPetname");
    case T::RemoveEd3kPetname:    return QStringLiteral("RemoveEd3kPetname");
    case T::UnpinEd3kName:        return QStringLiteral("UnpinEd3kName");
    case T::VouchEd3kName:        return QStringLiteral("VouchEd3kName");
    case T::FollowEd3kNameList:   return QStringLiteral("FollowEd3kNameList");
    case T::UnfollowEd3kNameList: return QStringLiteral("UnfollowEd3kNameList");
    case T::MoveEd3kNameList:     return QStringLiteral("MoveEd3kNameList");
    case T::NewEd3kCreditKey:     return QStringLiteral("NewEd3kCreditKey");
    case T::ImportEd3kCreditKey:  return QStringLiteral("ImportEd3kCreditKey");
    case T::ExportEd3kCreditKey:  return QStringLiteral("ExportEd3kCreditKey");
    case T::PushEd3kNameResolved: return QStringLiteral("PushEd3kNameResolved");
    case T::PushEd3kNameFailed:   return QStringLiteral("PushEd3kNameFailed");
    case T::PushEd3kNamesChanged: return QStringLiteral("PushEd3kNamesChanged");
    case T::PushEd3kNameFriendList: return QStringLiteral("PushEd3kNameFriendList");
    case T::GetEd3kRooms:         return QStringLiteral("GetEd3kRooms");
    case T::GetEd3kRoomPosts:     return QStringLiteral("GetEd3kRoomPosts");
    case T::CreateEd3kRoom:       return QStringLiteral("CreateEd3kRoom");
    case T::JoinEd3kRoom:         return QStringLiteral("JoinEd3kRoom");
    case T::LeaveEd3kRoom:        return QStringLiteral("LeaveEd3kRoom");
    case T::PostEd3kRoom:         return QStringLiteral("PostEd3kRoom");
    case T::SetEd3kRoomNick:      return QStringLiteral("SetEd3kRoomNick");
    case T::SetEd3kRoomInfo:      return QStringLiteral("SetEd3kRoomInfo");
    case T::ModerateEd3kRoom:     return QStringLiteral("ModerateEd3kRoom");
    case T::ImportEd3kRoomAdmit:  return QStringLiteral("ImportEd3kRoomAdmit");
    case T::MuteEd3kRoomMember:   return QStringLiteral("MuteEd3kRoomMember");
    case T::PushEd3kRoomsChanged: return QStringLiteral("PushEd3kRoomsChanged");
    case T::PushEd3kRoomPost:     return QStringLiteral("PushEd3kRoomPost");
    case T::PushEd3kRoomPostsRemoved: return QStringLiteral("PushEd3kRoomPostsRemoved");
    case T::PushEd3kRoomPresence: return QStringLiteral("PushEd3kRoomPresence");
    case T::PushEd3kRoomNotice:   return QStringLiteral("PushEd3kRoomNotice");
    case T::GetEd3kDownloads:     return QStringLiteral("GetEd3kDownloads");
    case T::AddEd3kDownload:      return QStringLiteral("AddEd3kDownload");
    case T::AddEd3kDownloadLink:  return QStringLiteral("AddEd3kDownloadLink");
    case T::Ed3kDownloadControl:  return QStringLiteral("Ed3kDownloadControl");
    case T::PushEd3kDownloads:    return QStringLiteral("PushEd3kDownloads");
    case T::GetEd3kSets:          return QStringLiteral("GetEd3kSets");
    case T::FollowEd3kSet:        return QStringLiteral("FollowEd3kSet");
    case T::UnfollowEd3kSet:      return QStringLiteral("UnfollowEd3kSet");
    case T::SetEd3kSetPolicy:     return QStringLiteral("SetEd3kSetPolicy");
    case T::SetEd3kSetSelection:  return QStringLiteral("SetEd3kSetSelection");
    case T::ApplyEd3kSetVersion:  return QStringLiteral("ApplyEd3kSetVersion");
    case T::DownloadEd3kSetParts: return QStringLiteral("DownloadEd3kSetParts");
    case T::WatchEd3kLive:        return QStringLiteral("WatchEd3kLive");
    case T::LeaveEd3kLive:        return QStringLiteral("LeaveEd3kLive");
    case T::GetEd3kLive:          return QStringLiteral("GetEd3kLive");
    case T::GetEd3kSetSite:       return QStringLiteral("GetEd3kSetSite");
    case T::PushEd3kSetsChanged:  return QStringLiteral("PushEd3kSetsChanged");
    case T::PushEd3kSetVersion:   return QStringLiteral("PushEd3kSetVersion");
    case T::PushEd3kLiveChanged:  return QStringLiteral("PushEd3kLiveChanged");
    case T::PushEd3kSetOffer:     return QStringLiteral("PushEd3kSetOffer");
    case T::GetEd3kPaid:          return QStringLiteral("GetEd3kPaid");
    case T::BuyEd3kPass:          return QStringLiteral("BuyEd3kPass");
    case T::ConfirmEd3kPaidAnon:  return QStringLiteral("ConfirmEd3kPaidAnon");
    case T::SetEd3kPassReports:   return QStringLiteral("SetEd3kPassReports");
    case T::BroadcastEd3kLive:    return QStringLiteral("BroadcastEd3kLive");
    case T::PushEd3kPaidChanged:  return QStringLiteral("PushEd3kPaidChanged");
    case T::PushEd3kPaidAnonWarn: return QStringLiteral("PushEd3kPaidAnonWarn");
    case T::PushEd3kPassArrived:  return QStringLiteral("PushEd3kPassArrived");
    case T::BurnEd3kStake:        return QStringLiteral("BurnEd3kStake");
    case T::ClaimEd3kStake:       return QStringLiteral("ClaimEd3kStake");
    case T::GetEd3kStakeClaims:   return QStringLiteral("GetEd3kStakeClaims");
    case T::RefreshEd3kStakeClaims: return QStringLiteral("RefreshEd3kStakeClaims");
    case T::PushEd3kStakeClaims:  return QStringLiteral("PushEd3kStakeClaims");
    case T::PushEd3kStakeTx:      return QStringLiteral("PushEd3kStakeTx");
    case T::ReadEd3kStakeClaim:   return QStringLiteral("ReadEd3kStakeClaim");
    case T::PushEd3kStakeLookup:  return QStringLiteral("PushEd3kStakeLookup");
    case T::GetEd3kFilters:       return QStringLiteral("GetEd3kFilters");
    case T::FollowEd3kFilter:     return QStringLiteral("FollowEd3kFilter");
    case T::UnfollowEd3kFilter:   return QStringLiteral("UnfollowEd3kFilter");
    case T::SetEd3kFilterMode:    return QStringLiteral("SetEd3kFilterMode");
    case T::SetEd3kFilterOwn:     return QStringLiteral("SetEd3kFilterOwn");
    case T::JudgeEd3kFilter:      return QStringLiteral("JudgeEd3kFilter");
    case T::OpenEd3kFilterFile:   return QStringLiteral("OpenEd3kFilterFile");
    case T::PublishEd3kFilterOwn: return QStringLiteral("PublishEd3kFilterOwn");
    case T::ShowEd3kFilterAnyway: return QStringLiteral("ShowEd3kFilterAnyway");
    case T::PushEd3kFiltersChanged: return QStringLiteral("PushEd3kFiltersChanged");
    case T::GetHosterItems:       return QStringLiteral("GetHosterItems");
    case T::AddHosterLinks:       return QStringLiteral("AddHosterLinks");
    case T::HosterAction:         return QStringLiteral("HosterAction");
    case T::AnswerHosterPuzzle:   return QStringLiteral("AnswerHosterPuzzle");
    case T::CheckHosterAccount:   return QStringLiteral("CheckHosterAccount");
    case T::DownloadJavaRuntime:  return QStringLiteral("DownloadJavaRuntime");
    case T::LocateJavaRuntime:    return QStringLiteral("LocateJavaRuntime");
    case T::GetHosterStats:       return QStringLiteral("GetHosterStats");
    case T::CheckHosterLinks:     return QStringLiteral("CheckHosterLinks");
    case T::PushHosterItem:       return QStringLiteral("PushHosterItem");
    case T::PushHosterItemRemoved: return QStringLiteral("PushHosterItemRemoved");
    case T::PushHosterPuzzle:     return QStringLiteral("PushHosterPuzzle");
    case T::PushHosterLinks:      return QStringLiteral("PushHosterLinks");
    case T::PushHosterAccount:    return QStringLiteral("PushHosterAccount");
    case T::PushHosterLinkCheck:  return QStringLiteral("PushHosterLinkCheck");
    default:
        return QStringLiteral("Unknown(%1)").arg(static_cast<int>(type));
    }
}

using namespace Ipc;

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

IpcClient::IpcClient(QObject* parent)
    : QObject(parent)
{
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &IpcClient::attemptReconnect);

    m_handshakeTimer.setSingleShot(true);
    connect(&m_handshakeTimer, &QTimer::timeout, this, [this]() {
        if (waitOnBusyCore(/*handshake=*/true)) {
            m_handshakeTimer.start(m_handshakeTimeoutMs);
            return;
        }
        logWarning(QStringLiteral("IPC handshake timed out — will retry"));
        // A refusal already says more than "no answer" does
        if (m_coreLink != CoreLink::Refused)
            setCoreLink(CoreLink::NotResponding);
        onConnectionLost();
    });

    connect(&m_keepaliveTimer, &QTimer::timeout, this, &IpcClient::sendKeepalive);

    m_keepaliveTimeoutTimer.setSingleShot(true);
    connect(&m_keepaliveTimeoutTimer, &QTimer::timeout, this, [this]() {
        // A busy GUI reads late: the pong may sit behind a backlog of frames. A
        // reconnect then would refetch every list on top of that backlog.
        if (peerStillTalking()) {
            m_keepaliveTimeoutTimer.start(m_keepaliveTimeoutMs);
            return;
        }
        if (waitOnBusyCore(/*handshake=*/false)) {
            m_keepaliveTimeoutTimer.start(m_keepaliveTimeoutMs);
            return;
        }
        logWarning(QStringLiteral("IPC keepalive timed out — reconnecting"));
        setCoreLink(CoreLink::NotResponding);
        onConnectionLost();
    });
}

IpcClient::~IpcClient()
{
    // Silent: a listener told "offline" from in here would look at an object
    // that is half gone, or write to state of its own that already is.
    blockSignals(true);
    disconnectFromDaemon();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void IpcClient::connectToDaemon(const QHostAddress& address, uint16_t port)
{
    // Store params for auto-reconnect
    m_address = address;
    m_hostname.clear();
    m_port = port;
    m_autoReconnect = true;
    m_reconnectDelayMs = 1000;
    m_reconnectTimer.stop();

    m_connectAttempts = 0;
    m_coreLink = CoreLink::Offline;   // a fresh target: nothing sticks from the last
    m_pendingFingerprint.clear();
    beginConnect();
}

void IpcClient::trustNewCertificate()
{
    if (m_pendingFingerprint.isEmpty())
        return;
    KnownCores& store = m_knownCores ? *m_knownCores : KnownCores::instance();
    store.pin(pinKey(), m_pendingFingerprint);
    logInfo(QStringLiteral("IPC: trusting new certificate of %1 (%2)")
                .arg(pinKey(), m_pendingFingerprint));
    m_pendingFingerprint.clear();
    m_reconnectDelayMs = 1000;
    beginConnect();
}

void IpcClient::connectToDaemon(const QString& host, uint16_t port)
{
    // Try parsing as IP address first — avoids unnecessary DNS lookup
    QHostAddress addr(host);
    if (!addr.isNull()) {
        connectToDaemon(addr, port);
        return;
    }

    // Hostname — store for reconnect; QTcpSocket handles DNS internally
    m_hostname = host;
    m_address = QHostAddress{};
    m_port = port;
    m_autoReconnect = true;
    m_reconnectDelayMs = 1000;
    m_reconnectTimer.stop();

    m_connectAttempts = 0;
    m_coreLink = CoreLink::Offline;
    m_pendingFingerprint.clear();
    beginConnect();
}

void IpcClient::disconnectFromDaemon()
{
    m_autoReconnect = false;
    m_reconnectTimer.stop();

    m_handshaked = false;
    failPendingRequests();

    dropConnection();
    m_socket = nullptr;
    setCoreLink(CoreLink::Offline);
}

bool IpcClient::isConnected() const
{
    return m_handshaked && m_connection && m_connection->isConnected();
}

bool IpcClient::isLocalConnection() const
{
    return m_address.isLoopback();
}

int IpcClient::pollingInterval() const
{
    return isLocalConnection() ? LocalPollingMs : m_remotePollingMs;
}

void IpcClient::setRemotePollingMs(int ms)
{
    m_remotePollingMs = std::clamp(ms, 200, 10000);
}

void IpcClient::setAuthToken(const QString& token)
{
    m_authToken = token;
}

void IpcClient::sendShutdown()
{
    if (!isConnected())
        return;

    IpcMessage msg(IpcMsgType::Shutdown, m_nextSeqId++);
    m_connection->sendMessage(msg);

    // Flush the socket's write buffer into the OS send buffer synchronously.
    // Without this, disconnectFromDaemon() destroys the socket immediately and
    // the Shutdown frame never reaches the daemon.
    if (QTcpSocket* sock = m_connection->socket())
        sock->flush();

    // Disable auto-reconnect — the daemon is going away intentionally
    disconnectFromDaemon();
}

void IpcClient::sendRestart()
{
    if (!isConnected())
        return;
    expectRestart();
    IpcMessage msg(IpcMsgType::Shutdown);
    msg.append(true);
    sendRequest(std::move(msg));
}

bool IpcClient::daemonRestarting() const
{
    return !m_restartDeadline.hasExpired();
}

void IpcClient::expectDaemonStart()
{
    m_startDeadline = QDeadlineTimer(m_startGraceMs);
    if (m_coreLink == CoreLink::Connecting)
        setCoreLink(CoreLink::Starting);
}

bool IpcClient::daemonStarting() const
{
    return !m_startDeadline.hasExpired();
}

int IpcClient::sendRequest(IpcMessage msg, ResponseCallback callback)
{
    if (!m_connection || !m_handshaked)
        return -1;

    // Overwrite the seqId with our tracked ID
    const int seqId = m_nextSeqId++;
    // Rebuild message with our seqId
    QCborArray arr = msg.toArray();
    if (arr.size() >= 2) {
        arr[1] = seqId;
    }
    IpcMessage tagged(std::move(arr));

    if (callback)
        m_pendingCallbacks[seqId] = std::move(callback);

    m_connection->sendMessage(tagged);

    if (thePrefs.enableIpcLog())
        emit ipcLogMessage(
            QStringLiteral("%1 [seq=%2]").arg(ipcMsgTypeName(tagged.type())).arg(seqId),
            /*outgoing=*/true);

    return seqId;
}

void IpcClient::cancelRequest(int seqId)
{
    m_pendingCallbacks.erase(seqId);
}

void IpcClient::sendPoll(const QObject* owner, IpcMessage msg, ResponseCallback callback)
{
    if (!m_connection || !m_handshaked)
        return;

    const QByteArray identity = requestIdentity(msg)
        + QByteArray::number(reinterpret_cast<quintptr>(owner), 16);
    if (const auto it = m_polls.find(identity); it != m_polls.end()) {
        it->again = true;
        it->msg = std::move(msg);
        it->callback = std::move(callback);
        ++m_pollsSkipped;
        return;
    }

    // The entry goes in before the send and is matched by seqId on the way out: a
    // reply for a connection since dropped must not release the poll of the new one.
    auto seq = std::make_shared<int>(-1);
    m_polls.insert(identity, Poll{});
    *seq = sendRequest(std::move(msg), [this, owner, identity, seq, callback](const IpcMessage& reply) {
        Poll next;
        if (const auto it = m_polls.find(identity); it != m_polls.end() && it->seqId == *seq) {
            next = std::move(*it);
            m_polls.erase(it);
        }
        if (callback)
            callback(reply);
        // One turn of the event loop first: applying a list and asking for the next
        // back to back would leave no room for input on a machine that is behind.
        if (next.again && reply.isValid()) {
            QTimer::singleShot(0, this, [this, owner, next = std::move(next)]() mutable {
                sendPoll(owner, std::move(next.msg), std::move(next.callback));
            });
        }
    });
    if (*seq < 0)
        m_polls.remove(identity);
    else
        m_polls[identity].seqId = *seq;
}

void IpcClient::setTimeouts(int handshakeMs, int keepaliveIntervalMs, int keepaliveTimeoutMs)
{
    m_handshakeTimeoutMs = handshakeMs;
    m_keepaliveIntervalMs = keepaliveIntervalMs;
    m_keepaliveTimeoutMs = keepaliveTimeoutMs;
    if (m_keepaliveTimer.isActive())
        m_keepaliveTimer.start(m_keepaliveIntervalMs);
}

void IpcClient::sendBatchRequest(const QStringList& keys,
                                 const std::function<IpcMessage(const QString&)>& build,
                                 QObject* context, std::function<void()> onAllDone,
                                 std::function<void(const QString&, const IpcMessage&)> onEach)
{
    if (keys.isEmpty() || !build || !isConnected())
        return;

    auto pending = std::make_shared<qsizetype>(keys.size());
    auto done = std::make_shared<std::function<void()>>(std::move(onAllDone));
    auto each = std::make_shared<std::function<void(const QString&, const IpcMessage&)>>(
        std::move(onEach));
    const QPointer<QObject> guard(context);

    auto settle = [pending, done, guard]() {
        if (--(*pending) == 0 && guard && *done)
            (*done)();
    };
    auto report = [each, guard](const QString& key, const IpcMessage& reply) {
        if (guard && *each)
            (*each)(key, reply);
    };

    for (const QString& key : keys) {
        if (key.isEmpty()) {
            report(key, {});
            settle();
            continue;
        }
        // A request that never got queued must still settle, or a disconnect in the middle
        // of a batch would leave the completion — and with it the list refresh — hanging.
        if (sendRequest(build(key), [settle, report, key](const IpcMessage& reply) {
                report(key, reply);
                settle();
            }) < 0) {
            report(key, {});
            settle();
        }
    }
}

// -- ed3k readable names and websites ----------------------------------------

int IpcClient::resolveEd3kName(const QString& text, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::ResolveEd3kName);
    msg.append(text);
    return sendRequest(std::move(msg), std::move(callback));
}

int IpcClient::openEd3kName(qint64 id, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::OpenEd3kName);
    msg.append(id);
    return sendRequest(std::move(msg), std::move(callback));
}

int IpcClient::requestEd3kNames(ResponseCallback callback)
{
    return sendRequest(IpcMessage(IpcMsgType::GetEd3kNames), std::move(callback));
}

int IpcClient::setEd3kPetname(const QString& text, const QString& zone, bool shareable, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::SetEd3kPetname);
    msg.append(text);
    msg.append(zone);
    msg.append(shareable);
    return sendRequest(std::move(msg), std::move(callback));
}

int IpcClient::removeEd3kPetname(const QString& text, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::RemoveEd3kPetname);
    msg.append(text);
    return sendRequest(std::move(msg), std::move(callback));
}

int IpcClient::unpinEd3kName(const QString& name, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::UnpinEd3kName);
    msg.append(name);
    return sendRequest(std::move(msg), std::move(callback));
}

int IpcClient::vouchEd3kName(const QString& name, bool on, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::VouchEd3kName);
    msg.append(name);
    msg.append(on);
    return sendRequest(std::move(msg), std::move(callback));
}

int IpcClient::followEd3kNameList(const QString& link, const QString& label, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::FollowEd3kNameList);
    msg.append(link);
    msg.append(label);
    return sendRequest(std::move(msg), std::move(callback));
}

int IpcClient::unfollowEd3kNameList(qint64 id, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::UnfollowEd3kNameList);
    msg.append(id);
    return sendRequest(std::move(msg), std::move(callback));
}

int IpcClient::moveEd3kNameList(qint64 id, qint64 place, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::MoveEd3kNameList);
    msg.append(id);
    msg.append(place);
    return sendRequest(std::move(msg), std::move(callback));
}

// -- ed3k rooms ----------------------------------------------------------------

void IpcClient::requestEd3kRooms(const QObject* owner, ResponseCallback callback)
{
    sendPoll(owner, IpcMessage(IpcMsgType::GetEd3kRooms), std::move(callback));
}

void IpcClient::requestEd3kDownloads(const QObject* owner, ResponseCallback callback)
{
    sendPoll(owner, IpcMessage(IpcMsgType::GetEd3kDownloads), std::move(callback));
}

int IpcClient::addEd3kDownload(const QString& root, qint64 size, const QString& name, int category, bool paused,
                               ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::AddEd3kDownload);
    msg.append(root);
    msg.append(size);
    msg.append(name);
    msg.append(qint64(category));
    msg.append(paused);
    return sendRequest(std::move(msg), std::move(callback));
}

int IpcClient::addEd3kDownloadLink(const QString& link, int category, bool paused, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::AddEd3kDownloadLink);
    msg.append(link);
    msg.append(qint64(category));
    msg.append(paused);
    return sendRequest(std::move(msg), std::move(callback));
}

int IpcClient::controlEd3kDownload(const QString& root, int action, qint64 value, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::Ed3kDownloadControl);
    msg.append(root);
    msg.append(qint64(action));
    msg.append(value);
    return sendRequest(std::move(msg), std::move(callback));
}

// -- ed3k sets and live streams --------------------------------------------------

void IpcClient::requestEd3kSets(const QObject* owner, ResponseCallback callback)
{
    sendPoll(owner, IpcMessage(IpcMsgType::GetEd3kSets), std::move(callback));
}

int IpcClient::followEd3kSet(const QString& link, int policy, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::FollowEd3kSet);
    msg.append(link);
    msg.append(qint64(policy));
    return sendRequest(std::move(msg), std::move(callback));
}

int IpcClient::unfollowEd3kSet(qint64 id, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::UnfollowEd3kSet);
    msg.append(id);
    return sendRequest(std::move(msg), std::move(callback));
}

int IpcClient::setEd3kSetPolicy(qint64 id, int policy, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::SetEd3kSetPolicy);
    msg.append(id);
    msg.append(qint64(policy));
    return sendRequest(std::move(msg), std::move(callback));
}

int IpcClient::setEd3kSetSelection(qint64 id, const QStringList& skipped, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::SetEd3kSetSelection);
    msg.append(id);
    QCborArray paths;
    for (const QString& p : skipped)
        paths.append(p);
    msg.append(paths);
    return sendRequest(std::move(msg), std::move(callback));
}

int IpcClient::applyEd3kSetVersion(qint64 id, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::ApplyEd3kSetVersion);
    msg.append(id);
    return sendRequest(std::move(msg), std::move(callback));
}

int IpcClient::downloadEd3kSetParts(qint64 id, const QString& author, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::DownloadEd3kSetParts);
    msg.append(id);
    msg.append(author);
    return sendRequest(std::move(msg), std::move(callback));
}

int IpcClient::requestEd3kSetSite(const QString& link, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::GetEd3kSetSite);
    msg.append(link);
    return sendRequest(std::move(msg), std::move(callback));
}

int IpcClient::watchEd3kLive(const QString& link, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::WatchEd3kLive);
    msg.append(link);
    return sendRequest(std::move(msg), std::move(callback));
}

int IpcClient::leaveEd3kLive(const QString& id, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::LeaveEd3kLive);
    msg.append(id);
    return sendRequest(std::move(msg), std::move(callback));
}

void IpcClient::requestEd3kLive(const QObject* owner, ResponseCallback callback)
{
    sendPoll(owner, IpcMessage(IpcMsgType::GetEd3kLive), std::move(callback));
}

int IpcClient::broadcastEd3kLive(const QString& folder, const QString& title, bool isPrivate, const QString& playlist,
                                 const QString& codecs, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::BroadcastEd3kLive);
    msg.append(folder);
    msg.append(title);
    msg.append(isPrivate);
    msg.append(playlist);
    msg.append(codecs);
    return sendRequest(std::move(msg), std::move(callback));
}

void IpcClient::requestEd3kPaid(const QObject* owner, ResponseCallback callback)
{
    sendPoll(owner, IpcMessage(IpcMsgType::GetEd3kPaid), std::move(callback));
}

int IpcClient::buyEd3kPass(const QString& scope, bool anon, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::BuyEd3kPass);
    msg.append(scope);
    msg.append(anon);
    return sendRequest(std::move(msg), std::move(callback));
}

int IpcClient::confirmEd3kPaidAnon(const QString& scope, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::ConfirmEd3kPaidAnon);
    msg.append(scope);
    return sendRequest(std::move(msg), std::move(callback));
}

int IpcClient::setEd3kPassReports(bool on, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::SetEd3kPassReports);
    msg.append(on);
    return sendRequest(std::move(msg), std::move(callback));
}

int IpcClient::requestEd3kRoomPosts(const QString& room, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::GetEd3kRoomPosts);
    msg.append(room);
    return sendRequest(std::move(msg), std::move(callback));
}

int IpcClient::createEd3kRoom(const QString& title, const QString& about, int flags, const QString& nick,
                              ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::CreateEd3kRoom);
    msg.append(title);
    msg.append(about);
    msg.append(qint64(flags));
    msg.append(nick);
    return sendRequest(std::move(msg), std::move(callback));
}

int IpcClient::joinEd3kRoom(const QString& link, const QString& nick, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::JoinEd3kRoom);
    msg.append(link);
    msg.append(nick);
    return sendRequest(std::move(msg), std::move(callback));
}

int IpcClient::leaveEd3kRoom(const QString& room, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::LeaveEd3kRoom);
    msg.append(room);
    return sendRequest(std::move(msg), std::move(callback));
}

int IpcClient::postEd3kRoom(const QString& room, const QString& text, const QString& replyTo, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::PostEd3kRoom);
    msg.append(room);
    msg.append(text);
    msg.append(replyTo);
    return sendRequest(std::move(msg), std::move(callback));
}

int IpcClient::setEd3kRoomNick(const QString& room, const QString& nick, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::SetEd3kRoomNick);
    msg.append(room);
    msg.append(nick);
    return sendRequest(std::move(msg), std::move(callback));
}

int IpcClient::setEd3kRoomInfo(const QString& room, const QString& title, const QString& about, int flags,
                               const QStringList& mods, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::SetEd3kRoomInfo);
    msg.append(room);
    msg.append(title);
    msg.append(about);
    msg.append(qint64(flags));
    msg.append(QCborArray::fromStringList(mods));
    return sendRequest(std::move(msg), std::move(callback));
}

int IpcClient::moderateEd3kRoom(const QString& room, int action, const QString& member, qint64 until,
                                ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::ModerateEd3kRoom);
    msg.append(room);
    msg.append(qint64(action));
    msg.append(member);
    msg.append(until);
    return sendRequest(std::move(msg), std::move(callback));
}

int IpcClient::importEd3kRoomAdmit(const QString& text, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::ImportEd3kRoomAdmit);
    msg.append(text);
    return sendRequest(std::move(msg), std::move(callback));
}

int IpcClient::muteEd3kRoomMember(const QString& room, const QString& memberId, bool on, ResponseCallback callback)
{
    IpcMessage msg(IpcMsgType::MuteEd3kRoomMember);
    msg.append(room);
    msg.append(memberId);
    msg.append(on);
    return sendRequest(std::move(msg), std::move(callback));
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void IpcClient::onSocketConnected()
{
    // A connected() that outlived its socket being dropped. onSocketError()
    // disconnects before deleting now, so this should be unreachable — it stays
    // because the cost of being wrong here is a null dereference in
    // IpcConnection's constructor rather than a missed reconnect.
    if (!m_socket)
        return;

    // Darwin 27 reports a refused connect as connected(), with no peer. Taken for
    // real, it never raised connectionFailed — so the core was never launched.
    if (m_socket->peerPort() != m_port) {
        failConnect(QStringLiteral("Connection refused"));
        return;
    }

    // Reset backoff on successful TCP connect
    m_reconnectDelayMs = 1000;

    // When connected by hostname, capture the resolved peer address so that
    // isLoopback() checks and handshake encryption decisions work correctly.
    if (!m_hostname.isEmpty() && m_socket)
        m_address = m_socket->peerAddress();

    // TLS for any remote core, and whenever a token goes out: a Docker port
    // forward looks like loopback here but not to the core.
    if (!m_address.isLoopback() || !m_authToken.isEmpty()) {
        QSslConfiguration cfg = m_socket->sslConfiguration();
        cfg.setProtocol(QSsl::TlsV1_2OrLater);
        cfg.setPeerVerifyMode(QSslSocket::VerifyNone);   // self-signed: pinned below
        m_socket->setSslConfiguration(cfg);
        connect(m_socket, &QSslSocket::encrypted, this, &IpcClient::onSocketEncrypted);
        m_socket->startClientEncryption();
        return;
    }
    startSession();
}

void IpcClient::onSocketEncrypted()
{
    if (!m_socket)
        return;
    const QString fp = Ipc::certFingerprint(m_socket->peerCertificate());
    KnownCores& store = m_knownCores ? *m_knownCores : KnownCores::instance();
    const QString key = pinKey();
    switch (store.check(key, fp)) {
    case KnownCores::Check::New:
        store.pin(key, fp);
        logInfo(QStringLiteral("IPC: pinned certificate of %1 (%2)").arg(key, fp));
        break;
    case KnownCores::Check::Match:
        break;
    case KnownCores::Check::Mismatch: {
        // Nothing sent yet, the token included. Stop until the user decides.
        const QString pinned = store.pinned(key);
        logWarning(QStringLiteral("IPC: certificate of %1 changed (pinned %2, got %3) — "
                                  "not connecting")
                       .arg(key, pinned, fp));
        m_pendingFingerprint = fp;
        m_reconnectTimer.stop();
        disconnect(m_socket, nullptr, this, nullptr);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
        m_refusalText = tr("The core's certificate changed");
        setCoreLink(CoreLink::Refused);
        emit certificateChanged(key, pinned, fp);
        return;
    }
    }
    startSession();
}

void IpcClient::startSession()
{
    // Detach the pre-handoff error handler: IpcConnection owns the socket now
    // and handles errors through its own onErrorOccurred → disconnected signal.
    // Leaving errorOccurred connected would fire both onConnectionLost() AND
    // onSocketError() on any subsequent error, causing spurious reconnect loops.
    disconnect(m_socket, &QAbstractSocket::errorOccurred,
               this, &IpcClient::onSocketError);

    // Hand off socket to IpcConnection
    m_connection = std::make_unique<IpcConnection>(m_socket, this);
    m_socket = nullptr;  // Now owned by IpcConnection

    connect(m_connection.get(), &IpcConnection::messageReceived,
            this, &IpcClient::onMessageReceived);
    connect(m_connection.get(), &IpcConnection::disconnected,
            this, &IpcClient::onConnectionLost);
    // Any byte from the core proves it alive, the pong included or not
    connect(m_connection.get(), &IpcConnection::activity,
            &m_keepaliveTimeoutTimer, &QTimer::stop);
    connect(m_connection.get(), &IpcConnection::dispatchBlocked,
            this, &IpcClient::dispatchBlocked);
    // Of the state pushes waiting to be handled only the newest of a kind matters
    m_connection->setCoalesceIncoming(true);

    m_waitingOnBusyCore = false;
    if (m_coreLink != CoreLink::NotResponding && m_coreLink != CoreLink::Refused)
        setCoreLink(CoreLink::Handshaking);
    performHandshake();
    m_handshakeTimer.start(m_handshakeTimeoutMs);
}

void IpcClient::onMessageReceived(const IpcMessage& msg)
{
    if (thePrefs.enableIpcLog())
        emit ipcLogMessage(
            QStringLiteral("%1 [seq=%2]").arg(ipcMsgTypeName(msg.type())).arg(msg.seqId()),
            /*outgoing=*/false);
    m_waitingOnBusyCore = false;   // it answers again: the next timeout is judged afresh

    // Check if this is a handshake response
    if (!m_handshaked && msg.type() == IpcMsgType::HandshakeOk) {
        m_handshakeTimer.stop();
        m_handshaked = true;
        m_keepaliveTimer.start(m_keepaliveIntervalMs);
        m_connectAttempts = 0;
        m_refusalText.clear();
        // Fields 3, 4: the core is still reading its files, and which
        m_loadStage = msg.fieldString(4);
        // Field 5: the core's pid; 0 from an older core
        m_corePid = msg.fieldInt(5);
        setCoreLink(msg.fieldBool(3) ? CoreLink::Loading : CoreLink::Ready);
        logInfo(QStringLiteral("IPC handshake complete — daemon version: %1")
                    .arg(msg.fieldString(0)));

        // Subscribe to all events
        IpcMessage sub(IpcMsgType::Subscribe, m_nextSeqId++);
        sub.append(qint64(0xFFFF));  // Subscribe to all event types
        m_connection->sendMessage(sub);

        // Field 2: daemon session token.
        // On token change (GUI restart or daemon restart), reset per-type IDs to 0
        // so the full daemon buffer is replayed. On reconnect within the same GUI
        // session (same token), IDs are kept so only missed events are fetched.
        const QString newToken = msg.fieldString(2);
        if (newToken != m_daemonToken) {
            m_lastKadId = m_lastUsenetId = m_lastServerId = m_lastLogId = m_lastVerboseId = 0;
            m_lastEd3kId = m_lastEd3kVerboseId = m_lastEd3kKadId = m_lastLeecherId = 0;
            m_lastServerMsgId = 0;
            m_daemonToken = newToken;
        }
        // Delay the initial log sync by 500 ms so any brief startup instability
        // has settled before we fetch the buffer.
        QTimer::singleShot(500, this, [this]() {
            if (m_handshaked) {
                requestLogSync();
                requestServerMessages();
            }
        });

        m_restartDeadline = QDeadlineTimer();
        m_startDeadline = QDeadlineTimer();
        emit connected();
        return;
    }

    // The handshake was turned down (wrong or missing token, version). The core
    // closes right after; without this the only trace was a later timeout.
    if (!m_handshaked && msg.type() == IpcMsgType::Error) {
        m_refusalText = msg.fieldString(1);
        logWarning(QStringLiteral("Core refused the connection: %1").arg(m_refusalText));
        setCoreLink(CoreLink::Refused);
        return;
    }

    // Check for response to a pending request
    const int seqId = msg.seqId();
    if (seqId > 0) {
        auto it = m_pendingCallbacks.find(seqId);
        if (it != m_pendingCallbacks.end()) {
            // Taken out of the map and onto the stack *before* it runs. A callback
            // that opens a modal dialog — several do — spins a nested event loop,
            // and the replies that arrive during it re-enter here: an insert can
            // rehash this unordered_map and leave `it` dangling, a clear() on
            // disconnect would destroy the std::function mid-call, and a nested
            // erase of the same seqId would make the erase below a second one.
            ResponseCallback callback = std::move(it->second);
            m_pendingCallbacks.erase(it);
            if (callback)
                callback(msg);
            return;
        }
    }

    // Push event (seqId == 0)
    if (seqId == 0)
        dispatchPushEvent(msg);
}

void IpcClient::onConnectionLost()
{
    m_handshakeTimer.stop();
    m_keepaliveTimer.stop();
    m_keepaliveTimeoutTimer.stop();
    m_handshaked = false;
    failPendingRequests();
    // The core went away on its own; a timeout or a refusal has set its state already
    if (m_coreLink == CoreLink::Ready || m_coreLink == CoreLink::Loading
        || m_coreLink == CoreLink::Handshaking)
        setCoreLink(m_autoReconnect ? CoreLink::Connecting : CoreLink::Offline);
    emit disconnected();

    scheduleReconnect();
}

void IpcClient::onSocketError()
{
    failConnect(m_socket ? m_socket->errorString() : QStringLiteral("Unknown error"));
}

void IpcClient::attemptReconnect()
{
    if (!m_autoReconnect || m_port == 0)
        return;

    if (!daemonStarting()) {
        const QString display = m_hostname.isEmpty() ? m_address.toString() : m_hostname;
        logInfo(QStringLiteral("Reconnecting to daemon at %1:%2...")
                    .arg(display).arg(m_port));
    }

    beginConnect();
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void IpcClient::performHandshake()
{
    IpcMessage handshake(IpcMsgType::Handshake, m_nextSeqId++);
    handshake.append(QString::fromLatin1(ProtocolVersion));
    if (!m_authToken.isEmpty())
        handshake.append(m_authToken);   // inside TLS whenever set (onSocketConnected)
    m_connection->sendMessage(handshake);
}

void IpcClient::dispatchPushEvent(const IpcMessage& msg)
{
    switch (msg.type()) {
    case IpcMsgType::PushStatsUpdate:      emit statsUpdated(msg); break;
    case IpcMsgType::PushDownloadUpdate:   emit downloadUpdated(msg); break;
    case IpcMsgType::PushDownloadAdded:    emit downloadAdded(msg); break;
    case IpcMsgType::PushDownloadRemoved:  emit downloadRemoved(msg); break;
    case IpcMsgType::PushNotifierEvent:    emit notifierEvent(msg); break;
    case IpcMsgType::PushServerState:      emit serverStateChanged(msg); break;
    case IpcMsgType::PushServerMessage: {
        // Track the checkpoint here so a live push and a replayed backlog entry
        // advance the same counter — a reconnect must not re-show either.
        if (const qint64 id = msg.fieldInt(0); id > m_lastServerMsgId)
            m_lastServerMsgId = id;
        emit serverMessageReceived(msg);
        break;
    }
    case IpcMsgType::PushSearchResult:     emit searchResultReceived(msg); break;
    case IpcMsgType::PushGlobalSearchProgress: emit globalSearchProgress(msg); break;
    case IpcMsgType::PushSearchState:     emit searchStateChanged(msg); break;
    case IpcMsgType::PushCoreLoading:
        if (m_handshaked) {
            m_loadStage = msg.fieldString(1);
            const CoreLink link = msg.fieldBool(0) ? CoreLink::Loading : CoreLink::Ready;
            if (link == m_coreLink)
                emit coreLinkChanged();   // same state, another stage
            else
                setCoreLink(link);
        }
        break;
    case IpcMsgType::PushCoreRestarting:
        expectRestart();
        break;
    case IpcMsgType::PushLogBatch:
        replayLogEntries(msg.fieldArray(0));
        break;
    case IpcMsgType::PushLogMessage: {
        const qint64 logId  = msg.fieldInt(0);
        const QString cat    = msg.fieldString(1);
        const auto severity  = static_cast<QtMsgType>(msg.fieldInt(2));

        int64_t& typeId = lastLogId(cat, severity);
        if (logId > typeId)
            typeId = logId;

        emit logMessageReceived(msg);
        break;
    }
    case IpcMsgType::PushIndexerResults:   emit indexerResultsReceived(msg); break;
    case IpcMsgType::PushIndexerProgress:  emit indexerSearchProgress(msg); break;
    case IpcMsgType::PushIndexerSearchDone: emit indexerSearchFinished(msg); break;
    case IpcMsgType::PushIndexerFeedStatus: emit indexerFeedStatus(msg); break;
    case IpcMsgType::PushUsenetQueueItem:  emit usenetItemUpdated(msg); break;
    case IpcMsgType::PushUsenetItemRemoved: emit usenetItemRemoved(msg); break;
    case IpcMsgType::PushUsenetItemFinished: emit usenetItemFinished(msg); break;
    case IpcMsgType::PushUsenetEngineState:
        setUsenetEnginePaused(msg.fieldMap(0).value(QStringLiteral("paused")).toBool());
        break;
    case IpcMsgType::PushTorrentAdded:     emit torrentAdded(msg); break;
    case IpcMsgType::PushTorrentUpdate:    emit torrentUpdated(msg); break;
    case IpcMsgType::PushTorrentRemoved:   emit torrentRemoved(msg); break;
    case IpcMsgType::PushTorrentFinished:  emit torrentFinished(msg); break;
    case IpcMsgType::PushBtStatus:         emit btStatusChanged(msg); break;
    case IpcMsgType::PushEd3kStatus:       emit ed3kStatusChanged(msg); break;
    case IpcMsgType::PushEd3kBootstrapAsk: emit ed3kBootstrapAsk(msg); break;
    case IpcMsgType::PushEd3kFriendsChanged: emit ed3kFriendsChanged(msg); break;
    case IpcMsgType::PushEd3kChatMessage:  emit ed3kChatMessageReceived(msg); break;
    case IpcMsgType::PushEd3kChatSent:     emit ed3kChatSent(msg); break;
    case IpcMsgType::PushEd3kChatFailed:   emit ed3kChatFailed(msg); break;
    case IpcMsgType::PushEd3kFriendRequest: emit ed3kFriendRequestReceived(msg); break;
    case IpcMsgType::PushEd3kChatState:    emit ed3kChatStateReceived(msg); break;
    case IpcMsgType::PushEd3kNameResolved: emit ed3kNameResolved(msg); break;
    case IpcMsgType::PushEd3kNameFailed:   emit ed3kNameFailed(msg); break;
    case IpcMsgType::PushEd3kNamesChanged: emit ed3kNamesChanged(msg); break;
    case IpcMsgType::PushEd3kNameFriendList: emit ed3kNameFriendListOffered(msg); break;
    case IpcMsgType::PushEd3kRoomsChanged: emit ed3kRoomsChanged(msg); break;
    case IpcMsgType::PushEd3kRoomPost:     emit ed3kRoomPost(msg); break;
    case IpcMsgType::PushEd3kRoomPostsRemoved: emit ed3kRoomPostsRemoved(msg); break;
    case IpcMsgType::PushEd3kRoomPresence: emit ed3kRoomPresence(msg); break;
    case IpcMsgType::PushEd3kRoomNotice:   emit ed3kRoomNotice(msg); break;
    case IpcMsgType::PushEd3kDownloads:    emit ed3kDownloadsChanged(msg); break;
    case IpcMsgType::PushEd3kSetsChanged:  emit ed3kSetsChanged(msg); break;
    case IpcMsgType::PushEd3kSetVersion:   emit ed3kSetVersion(msg); break;
    case IpcMsgType::PushEd3kLiveChanged:  emit ed3kLiveChanged(msg); break;
    case IpcMsgType::PushEd3kPaidChanged:  emit ed3kPaidChanged(msg); break;
    case IpcMsgType::PushEd3kPaidAnonWarn: emit ed3kPaidAnonWarn(msg); break;
    case IpcMsgType::PushEd3kPassArrived:  emit ed3kPassArrived(msg); break;
    case IpcMsgType::PushEd3kSetOffer:     emit ed3kSetOffered(msg); break;
    case IpcMsgType::PushEd3kStakeClaims:  emit ed3kStakeClaimsChanged(msg); break;
    case IpcMsgType::PushEd3kStakeTx:      emit ed3kStakeTx(msg); break;
    case IpcMsgType::PushEd3kStakeLookup:  emit ed3kStakeLookup(msg); break;
    case IpcMsgType::PushEd3kFiltersChanged: emit ed3kFiltersChanged(msg); break;
    case IpcMsgType::PushHosterItem:       emit hosterItemChanged(msg); break;
    case IpcMsgType::PushHosterItemRemoved: emit hosterItemRemoved(msg); break;
    case IpcMsgType::PushHosterPuzzle:     emit hosterPuzzleAsked(msg); break;
    case IpcMsgType::PushHosterLinks:      emit hosterLinksExpanded(msg); break;
    case IpcMsgType::PushHosterAccount:    emit hosterAccountChecked(msg); break;
    case IpcMsgType::PushHosterLinkCheck:  emit hosterLinkChecked(msg); break;
    case IpcMsgType::PushSharedFileUpdate: emit sharedFileUpdated(msg); break;
    case IpcMsgType::PushSharedFileRemoved: emit sharedFileRemoved(msg); break;
    case IpcMsgType::PushUploadUpdate:     emit uploadUpdated(msg); break;
    case IpcMsgType::PushKadUpdate:        emit kadUpdated(msg); break;
    case IpcMsgType::PushKadSearchesChanged: emit kadSearchesChanged(msg); break;
    case IpcMsgType::PushKnownClientsChanged: emit knownClientsChanged(msg); break;
    case IpcMsgType::PushCategoriesChanged:   emit categoriesChanged(msg); break;
    case IpcMsgType::PushChatMessage:        emit chatMessageReceived(msg); break;
    case IpcMsgType::PushChatState:          emit chatStateReceived(msg); break;
    case IpcMsgType::PushChatCaptcha:        emit chatCaptchaReceived(msg); break;
    case IpcMsgType::PushChatCaptchaResult:  emit chatCaptchaResultReceived(msg); break;
    case IpcMsgType::PushFriendListChanged:  emit friendListChanged(msg); break;
    case IpcMsgType::PushClientSharedFiles:  emit clientSharedFilesReceived(msg); break;
    case IpcMsgType::PushSearchPreview:      emit searchPreviewReceived(msg); break;
    case IpcMsgType::PushPortMapStatus:      emit portMapStatusChanged(msg); break;
    default: break;
    }
}

void IpcClient::requestLogSync()
{
    // Request from the minimum of all per-type IDs:
    //  - Fresh GUI start: all IDs == currentMax → min == currentMax → empty response.
    //  - Daemon restart: all IDs == 0 → min == 0 → full new-daemon buffer shown.
    //  - Reconnect within session: min of per-type IDs → only missed events fetched.
    const int64_t fromId = std::min({m_lastKadId, m_lastUsenetId, m_lastServerId,
                                     m_lastLogId,  m_lastVerboseId, m_lastEd3kId,
                                     m_lastEd3kVerboseId, m_lastEd3kKadId, m_lastLeecherId});
    IpcMessage req(IpcMsgType::SyncLogs);
    req.append(static_cast<qint64>(fromId));

    sendRequest(std::move(req), [this](const IpcMessage& resp) {
        replayLogEntries(resp.fieldArray(1));
    });
}

void IpcClient::replayLogEntries(const QCborArray& entries)
{
    if (entries.isEmpty())
        return;

    // Bracketed so the log view lays out and repaints once for the lot
    emit logBatchStarted();
    for (int i = 0; i < entries.size(); ++i) {
        const QCborArray entry = entries[i].toArray();
        if (entry.size() < 4)
            continue;

        const qint64 logId   = entry[0].toInteger();
        const QString cat     = entry[1].toString();
        const auto severity   = static_cast<QtMsgType>(entry[2].toInteger());

        // Select per-type checkpoint; skip if already seen for this type
        int64_t& typeId = lastLogId(cat, severity);
        if (logId <= typeId)
            continue;   // already displayed
        typeId = logId;

        // Build synthetic PushLogMessage for uniform GUI handling
        IpcMessage synth(IpcMsgType::PushLogMessage, 0);
        synth.append(logId);
        synth.append(cat);
        synth.append(static_cast<qint64>(entry[2].toInteger()));  // severity
        synth.append(entry[3].toString());                       // message
        synth.append(entry.size() >= 5 ? entry[4].toInteger() : qint64(0));
        emit logMessageReceived(synth);
    }
    emit logBatchFinished();
}

void IpcClient::requestServerMessages()
{
    IpcMessage req(IpcMsgType::GetServerMessages);
    req.append(static_cast<qint64>(m_lastServerMsgId));

    sendRequest(std::move(req), [this](const IpcMessage& resp) {
        const QCborArray entries = resp.fieldArray(1);
        for (int i = 0; i < entries.size(); ++i) {
            const QCborArray entry = entries[i].toArray();
            if (entry.size() < 3)
                continue;

            const qint64 id = entry[0].toInteger();
            if (id <= m_lastServerMsgId)
                continue;   // already displayed
            m_lastServerMsgId = id;

            // Replay as a synthetic push so the GUI has a single handling path.
            IpcMessage synth(IpcMsgType::PushServerMessage, 0);
            synth.append(id);
            synth.append(entry[1].toInteger());
            synth.append(entry[2].toString());
            emit serverMessageReceived(synth);
        }
    });
}

void IpcClient::sendKeepalive()
{
    sendRequest(IpcMessage(IpcMsgType::Ping), [this](const IpcMessage&) {
        m_keepaliveTimeoutTimer.stop();
    });
    m_keepaliveTimeoutTimer.start(m_keepaliveTimeoutMs);
}

void IpcClient::scheduleReconnect()
{
    // A changed certificate waits for the user (trustNewCertificate())
    if (!m_autoReconnect || m_reconnectTimer.isActive() || !m_pendingFingerprint.isEmpty())
        return;

    // A core we launched is still loading: ask again shortly, quietly. Backing
    // off here left the window empty for seconds after the port had opened.
    if (daemonStarting()) {
        m_reconnectTimer.start(StartRetryDelayMs);
        return;
    }

    logInfo(QStringLiteral("Will retry in %1s...").arg(m_reconnectDelayMs / 1000));
    m_reconnectTimer.start(m_reconnectDelayMs);

    // Exponential backoff: 1s → 2s → 4s → 8s → 16s → 30s
    m_reconnectDelayMs = std::min(m_reconnectDelayMs * 2, MaxReconnectDelay);
}

void IpcClient::failPendingRequests()
{
    // They used to be dropped: a batch never settled and anything waiting on a reply
    // waited forever. Deferred so no callback runs inside the socket teardown (one may
    // open a dialog), and skipped entirely if this client is destroyed first.
    m_polls.clear();   // their replies will not come; the callbacks fail below
    if (m_pendingCallbacks.empty())
        return;
    using Pending = std::vector<std::pair<int, ResponseCallback>>;
    auto pending = std::make_shared<Pending>(std::make_move_iterator(m_pendingCallbacks.begin()),
                                             std::make_move_iterator(m_pendingCallbacks.end()));
    m_pendingCallbacks = {};
    std::ranges::sort(*pending, {}, &Pending::value_type::first);   // in send order

    QTimer::singleShot(0, this, [pending] {
        for (auto& [seqId, callback] : *pending) {
            if (callback)
                callback(IpcMessage{});
        }
    });
}

void IpcClient::failConnect(const QString& error)
{
    emit connectionFailed(error);

    // Clean up the failed socket so scheduleReconnect starts fresh.
    //
    // Disconnect *before* deleteLater, exactly as resetConnection() does.
    // deleteLater is deferred, so the socket outlives this slot and stays wired
    // to onSocketConnected() — and QAbstractSocket can still emit connected()
    // afterwards from the same fetchConnectionParameters() pass. That reached
    // onSocketConnected() with m_socket already null and crashed in
    // IpcConnection's constructor, which dereferences the socket it is handed.
    if (m_socket) {
        disconnect(m_socket, nullptr, this, nullptr);
        m_socket->deleteLater();
        m_socket = nullptr;
    }

    scheduleReconnect();
}

void IpcClient::resetConnection()
{
    m_handshakeTimer.stop();
    m_keepaliveTimer.stop();
    m_keepaliveTimeoutTimer.stop();
    m_handshaked = false;
    failPendingRequests();

    dropConnection();
    if (m_socket) {
        disconnect(m_socket, nullptr, this, nullptr);
        m_socket->deleteLater();
        m_socket = nullptr;
    }
}

void IpcClient::dropConnection()
{
    if (!m_connection)
        return;
    // Disconnect all signals before closing so that close() → disconnectFromHost()
    // does not cascade into onConnectionLost() → scheduleReconnect() while we are
    // already inside attemptReconnect(), which would queue a second reconnect on
    // top of the one being set up right now.
    disconnect(m_connection.get(), nullptr, this, nullptr);
    m_connection->close();
    // Not destroyed here: a quit while a prompt opened by a message handler is up
    // arrives with this socket's readyRead still on the stack below the prompt.
    // Stays our child, so it goes with us if the loop never turns again.
    m_connection.release()->deleteLater();
}

void IpcClient::beginConnect()
{
    resetConnection();

    ++m_connectAttempts;
    // NotResponding and Refused stay up until a handshake goes through: the retry
    // in between is not news, and flipping back to "Connecting" would hide them.
    if (m_coreLink != CoreLink::NotResponding && m_coreLink != CoreLink::Refused)
        setCoreLink(daemonStarting() ? CoreLink::Starting : CoreLink::Connecting);
    else
        emit coreLinkChanged();   // the attempt count moved

    m_socket = new QSslSocket(this);

    connect(m_socket, &QTcpSocket::connected,
            this, &IpcClient::onSocketConnected);
    connect(m_socket, &QAbstractSocket::errorOccurred,
            this, &IpcClient::onSocketError);

    if (m_hostname.isEmpty())
        m_socket->connectToHost(m_address, m_port);
    else
        m_socket->connectToHost(m_hostname, m_port);
}

QString IpcClient::pinKey() const
{
    return KnownCores::key(m_hostname.isEmpty() ? m_address.toString() : m_hostname, m_port);
}

void IpcClient::setCoreLink(CoreLink link)
{
    if (link == m_coreLink)
        return;
    m_coreLink = link;
    if (link != CoreLink::Loading)
        m_loadStage.clear();
    emit coreLinkChanged();
}

bool IpcClient::peerStillTalking() const
{
    return m_connection && m_connection->isConnected() && m_connection->hasPendingInput();
}

bool IpcClient::waitOnBusyCore(bool handshake)
{
    // Remote: a silent link may be a dead path, and only a reconnect finds out.
    // Locally a core that is gone resets the socket at once.
    if (!m_connection || !m_connection->isConnected() || !m_address.isLoopback())
        return false;
    // Handshake: a core we launched or restarted (its load stages block the loop
    // for seconds with a large share). Keepalive: a core that said it is loading.
    const bool busy = m_waitingOnBusyCore
                   || (handshake ? daemonStarting() || daemonRestarting()
                                 : m_coreLink == CoreLink::Loading);
    if (!busy)
        return false;
    if (!m_waitingOnBusyCore) {
        logInfo(handshake ? QStringLiteral("Core is busy starting up, still waiting for its answer")
                          : QStringLiteral("Core is busy loading its files, still waiting for its answer"));
        m_waitingOnBusyCore = true;
    }
    return true;
}

void IpcClient::expectRestart()
{
    // Time for a graceful stop (saving part files) plus the start
    constexpr int kRestartGraceMs = 60'000;
    m_restartDeadline = QDeadlineTimer(kRestartGraceMs);
}

int64_t& IpcClient::lastLogId(const QString& cat, QtMsgType severity)
{
    if (cat == QStringLiteral("emule.kad"))
        return m_lastKadId;
    if (cat == QStringLiteral("emule.usenet"))
        return m_lastUsenetId;
    if (cat == QStringLiteral("emule.server"))
        return m_lastServerId;
    if (cat == QStringLiteral("emule.ed3k.kad"))
        return m_lastEd3kKadId;
    if (cat == QStringLiteral("emule.leecher"))
        return m_lastLeecherId;
    if (cat == QStringLiteral("emule.ed3k"))
        return severity == QtDebugMsg ? m_lastEd3kVerboseId : m_lastEd3kId;
    return severity == QtDebugMsg ? m_lastVerboseId : m_lastLogId;
}

} // namespace eMule
