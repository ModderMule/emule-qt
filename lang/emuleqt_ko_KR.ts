<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="ko_KR">
<context>
    <name>Ed2kLinkImporter</name>
    <message>
        <location filename="../src/gui/utils/Ed2kLinkImporter.cpp" line="+129"/>
        <source>already shared</source>
        <translation>이미 공유됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>already downloading</source>
        <translation>이미 다운로드 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>already downloaded</source>
        <translation>이미 다운로드됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>previously cancelled</source>
        <translation>이전에 취소됨</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>You already have the file &quot;%1&quot;.</source>
        <translation>파일 &quot;%1&quot;이(가) 이미 있습니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>You are already trying to download the file &quot;%1&quot;.</source>
        <translation>파일 &quot;%1&quot;을(를) 이미 다운로드하는 중입니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>You previously cancelled the download of &quot;%1&quot;.</source>
        <translation>이전에 &quot;%1&quot;의 다운로드를 취소했습니다.</translation>
    </message>
    <message numerus="yes">
        <location line="+71"/>
        <source>%n further HTTP Cache link(s) ignored — apply one at a time.</source>
        <translation>
            <numerusform>추가 HTTP 캐시 링크 %n개를 무시했습니다 — 한 번에 하나씩 적용하세요.</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="+96"/>
        <source>%n eD2K link(s) not added — already known</source>
        <translation>
            <numerusform>eD2K 링크 %n개가 추가되지 않았습니다 — 이미 알려짐</numerusform>
        </translation>
    </message>
    <message>
        <location line="+9"/>
        <source>eD2K Link</source>
        <translation>eD2K 링크</translation>
    </message>
</context>
<context>
    <name>HttpCacheLinkImporter</name>
    <message>
        <location filename="../src/gui/utils/HttpCacheLinkImporter.cpp" line="+44"/>
        <source>The core did not answer.</source>
        <translation>코어가 응답하지 않았습니다.</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>Server: %1</source>
        <translation>서버: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Key: %1</source>
        <translation>키: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Version %1%2</source>
        <translation>버전 %1%2</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>This server also accepts uploads without a key.</source>
        <translation>이 서버는 키 없이도 업로드를 받습니다.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>This replaces the entry already stored for %1.</source>
        <translation>%1에 대해 이미 저장된 항목을 대체합니다.</translation>
    </message>
    <message numerus="yes">
        <location line="+3"/>
        <source>Uploads are shared across your cache servers; this makes %n of them.</source>
        <translation>
            <numerusform>업로드는 캐시 서버들에 분산됩니다. 이렇게 하면 %n개가 됩니다.</numerusform>
        </translation>
    </message>
    <message>
        <location line="+10"/>
        <source>

This link uses plain HTTP. The key and every chunk address will cross the network unencrypted.</source>
        <translation>

이 링크는 평문 HTTP를 사용합니다. 키와 모든 청크 주소가 암호화되지 않은 채 네트워크를 지나갑니다.</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Update your HTTP Cache settings for &quot;%1&quot;?</source>
        <translation>&quot;%1&quot;의 HTTP 캐시 설정을 업데이트할까요?</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Add &quot;%1&quot; as an HTTP Cache server?</source>
        <translation>&quot;%1&quot;을(를) HTTP 캐시 서버로 추가할까요?</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Use &quot;%1&quot; as your HTTP Cache server?</source>
        <translation>&quot;%1&quot;을(를) HTTP 캐시 서버로 사용할까요?</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+26"/>
        <source>HTTP Cache</source>
        <translation>HTTP 캐시</translation>
    </message>
    <message>
        <location line="-23"/>
        <source>

HTTP Cache will be enabled and this key stored for uploads.</source>
        <translation>

HTTP 캐시가 활성화되고 이 키가 업로드용으로 저장됩니다.</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>HTTP Cache link refused: %1</source>
        <translation>HTTP 캐시 링크가 거부되었습니다: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+29"/>
        <source>HTTP Cache link refused</source>
        <translation>HTTP 캐시 링크가 거부되었습니다</translation>
    </message>
    <message>
        <location line="-16"/>
        <location line="+2"/>
        <source>HTTP Cache is already configured for %1.</source>
        <translation>HTTP 캐시가 이미 %1에 대해 설정되어 있습니다.</translation>
    </message>
    <message numerus="yes">
        <location line="+10"/>
        <source>You already have %n HTTP Cache server(s) configured. Remove one from preferences.yml before adding another.</source>
        <translation>
            <numerusform>이미 HTTP 캐시 서버 %n개가 설정되어 있습니다. 다른 서버를 추가하기 전에 preferences.yml에서 하나를 제거하세요.</numerusform>
        </translation>
    </message>
    <message>
        <location line="+17"/>
        <source>HTTP Cache configuration for %1 was not applied.</source>
        <translation>%1의 HTTP 캐시 설정이 적용되지 않았습니다.</translation>
    </message>
    <message>
        <location line="+17"/>
        <location line="+2"/>
        <source>HTTP Cache configured for %1.</source>
        <translation>%1에 대해 HTTP 캐시를 설정했습니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>HTTP Cache configuration failed: %1</source>
        <translation>HTTP 캐시 설정에 실패했습니다: %1</translation>
    </message>
</context>
<context>
    <name>IpcFeedback</name>
    <message>
        <location filename="../src/gui/utils/IpcFeedback.cpp" line="+22"/>
        <source>The request was rejected by eMule.</source>
        <translation>요청이 eMule에 의해 거부되었습니다.</translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <location filename="../src/gui/utils/Ed2kLinkImporter.cpp" line="-226"/>
        <source>eD2K Link</source>
        <translation>eD2K 링크</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Do you want to download the following file(s)?

%1</source>
        <translation>다음 파일을 다운로드하시겠습니까?

%1</translation>
    </message>
    <message>
        <location filename="../src/gui/app/main.cpp" line="+558"/>
        <source>Download Added</source>
        <translation>다운로드 추가됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>A new download has been added.</source>
        <translation>새 다운로드가 추가되었습니다.</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Chat Message from %1</source>
        <translation>%1의 채팅 메시지</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Log Entry</source>
        <translation>로그 항목</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Connection Lost</source>
        <translation>연결 끊김</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server connection has been lost.</source>
        <translation>서버 연결이 끊어졌습니다.</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/ClientListModel.cpp" line="+69"/>
        <location filename="../src/gui/controls/SharedFilesModel.cpp" line="+47"/>
        <source>Very Low</source>
        <translation>매우 낮음</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/gui/controls/SharedFilesModel.cpp" line="+1"/>
        <source>Low</source>
        <translation>낮음</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+3"/>
        <location filename="../src/gui/controls/SharedFilesModel.cpp" line="+1"/>
        <location line="+3"/>
        <source>Normal</source>
        <translation>보통</translation>
    </message>
    <message>
        <location line="-2"/>
        <location filename="../src/gui/controls/SharedFilesModel.cpp" line="-2"/>
        <source>High</source>
        <translation>높음</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/gui/controls/SharedFilesModel.cpp" line="+1"/>
        <source>Very High</source>
        <translation>매우 높음</translation>
    </message>
    <message>
        <location line="+4"/>
        <location filename="../src/gui/controls/SharedFilesModel.cpp" line="+4"/>
        <source>Auto [%1]</source>
        <translation>자동 [%1]</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Fake</source>
        <translation>가짜</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Poor</source>
        <translation>나쁨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Fair</source>
        <translation>보통</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Good</source>
        <translation>양호</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Excellent</source>
        <translation>우수</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Server</source>
        <translation>서버</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Kad</source>
        <translation>Kad</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Source Exch.</source>
        <translation>소스 교환</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/gui/controls/DownloadListModel.cpp" line="+92"/>
        <source>Passive</source>
        <translation>수동</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/gui/controls/DownloadListModel.cpp" line="+1"/>
        <source>Link</source>
        <translation>링크</translation>
    </message>
    <message>
        <location line="+2"/>
        <location filename="../src/gui/controls/DownloadListModel.cpp" line="+2"/>
        <source>HTTP Cache</source>
        <translation>HTTP 캐시</translation>
    </message>
    <message>
        <location line="+200"/>
        <location line="+14"/>
        <source>Yes</source>
        <translation>예</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/DownloadListModel.cpp" line="-34"/>
        <source>Never</source>
        <translation>안 함</translation>
    </message>
    <message>
        <location line="+7"/>
        <location filename="../src/gui/controls/SharedFilesModel.cpp" line="-25"/>
        <source>Archive</source>
        <translation>압축 파일</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/gui/controls/SharedFilesModel.cpp" line="+1"/>
        <source>Audio</source>
        <translation>오디오</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/gui/controls/SharedFilesModel.cpp" line="+1"/>
        <source>Video</source>
        <translation>비디오</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/gui/controls/SharedFilesModel.cpp" line="+1"/>
        <source>Image</source>
        <translation>이미지</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/gui/controls/SharedFilesModel.cpp" line="+1"/>
        <source>Program</source>
        <translation>프로그램</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/gui/controls/SharedFilesModel.cpp" line="+1"/>
        <source>Document</source>
        <translation>문서</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/gui/controls/SharedFilesModel.cpp" line="+1"/>
        <source>CD-Image</source>
        <translation>CD 이미지</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/gui/controls/SharedFilesModel.cpp" line="+1"/>
        <source>eMule Collection</source>
        <translation>eMule 컬렉션</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>eD2K Server</source>
        <translation>eD2K 서버</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Kademlia</source>
        <translation>Kademlia</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Source Exchange</source>
        <translation>소스 교환</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/ClientListModel.cpp" line="-215"/>
        <location filename="../src/gui/controls/DownloadListModel.cpp" line="+3"/>
        <source>SLS</source>
        <translation>SLS</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/DownloadListModel.cpp" line="+2"/>
        <source>Unknown</source>
        <translation>알 수 없음</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/SearchResultsModel.cpp" line="+56"/>
        <source>Shared</source>
        <translation>공유됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloading</source>
        <translation>다운로드 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloaded</source>
        <translation>다운로드됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Cancelled</source>
        <translation>취소됨</translation>
    </message>
    <message>
        <location filename="../src/gui/panels/StatisticsPanel.cpp" line="+267"/>
        <source>Total Overhead (Packets): 0 Bytes (0)</source>
        <translation>전체 오버헤드 (패킷): 0 Bytes (0)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>File Request Overhead (Packets): 0 Bytes (0)</source>
        <translation>파일 요청 오버헤드 (패킷): 0 Bytes (0)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Source Exchange Overhead (Packets): 0 Bytes (0)</source>
        <translation>소스 교환 오버헤드 (패킷): 0 Bytes (0)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server Overhead (Packets): 0 Bytes (0)</source>
        <translation>서버 오버헤드 (패킷): 0 Bytes (0)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Kad Overhead (Packets): 0 Bytes (0)</source>
        <translation>Kad 오버헤드 (패킷): 0 Bytes (0)</translation>
    </message>
    <message>
        <location line="+999"/>
        <source>%1 Bytes</source>
        <translation>%1 Bytes</translation>
    </message>
</context>
<context>
    <name>eMule::AddFriendDialog</name>
    <message>
        <location filename="../src/gui/dialogs/AddFriendDialog.cpp" line="+23"/>
        <source>Add...</source>
        <translation>추가...</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Required Information</source>
        <translation>필수 정보</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>IP Address:</source>
        <translation>IP 주소:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Port:</source>
        <translation>포트:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Additional Information</source>
        <translation>추가 정보</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Name:</source>
        <translation>이름:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Hash:</source>
        <translation>해시:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Unknown</source>
        <translation>알 수 없음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>KadID:</source>
        <translation>Kad ID:</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Last Seen:</source>
        <translation>마지막 확인:</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Add</source>
        <translation>추가</translation>
    </message>
    <message>
        <location line="+41"/>
        <location line="+6"/>
        <source>Add Friend</source>
        <translation>친구 추가</translation>
    </message>
    <message>
        <location line="-5"/>
        <source>Please enter an IP address.</source>
        <translation>IP 주소를 입력하세요.</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Please enter a valid port (1-65535).</source>
        <translation>유효한 포트(1-65535)를 입력하세요.</translation>
    </message>
</context>
<context>
    <name>eMule::ArchivePreviewPanel</name>
    <message>
        <location filename="../src/gui/dialogs/ArchivePreviewPanel.cpp" line="+66"/>
        <source>Scanning...</source>
        <translation>스캔 중...</translation>
    </message>
    <message>
        <location line="+85"/>
        <location line="+37"/>
        <location line="+18"/>
        <source>Archive type: --</source>
        <translation>압축 유형: --</translation>
    </message>
    <message>
        <location line="-54"/>
        <location line="+17"/>
        <location line="+41"/>
        <source>Ready</source>
        <translation>준비됨</translation>
    </message>
    <message>
        <location line="-25"/>
        <source>Archive type: %1</source>
        <translation>압축 유형: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>No entries found or unsupported format</source>
        <translation>항목을 찾을 수 없거나 지원되지 않는 형식</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Files: %1</source>
        <translation>파일: %1</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Create Preview Copy</source>
        <translation>미리보기 복사본 생성</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Update</source>
        <translation>업데이트</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Name</source>
        <translation>이름</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Size</source>
        <translation>크기</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>CRC</source>
        <translation>CRC</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Attributes</source>
        <translation>속성</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Last Modified</source>
        <translation>마지막 수정</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Comment</source>
        <translation>댓글</translation>
    </message>
</context>
<context>
    <name>eMule::BugReportDialog</name>
    <message>
        <location filename="../src/gui/dialogs/BugReportDialog.cpp" line="+54"/>
        <location line="+133"/>
        <location line="+127"/>
        <source>Submit Bug Report</source>
        <translation>버그 신고 제출</translation>
    </message>
    <message>
        <location line="-255"/>
        <source>Report Details</source>
        <translation>신고 상세 정보</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Bug Report</source>
        <translation>버그 신고</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Feature Request</source>
        <translation>기능 요청</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type:</source>
        <translation>유형:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Brief summary of the issue</source>
        <translation>문제에 대한 간단한 요약</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Title:</source>
        <translation>제목:</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+4"/>
        <source>(optional)</source>
        <translation>(선택 사항)</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>Name:</source>
        <translation>이름:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Email:</source>
        <translation>이메일:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Description</source>
        <translation>설명</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Describe the issue in detail...</source>
        <translation>문제를 자세히 설명하세요...</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Attachments</source>
        <translation>첨부 파일</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Screenshots:</source>
        <translation>스크린샷:</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Add...</source>
        <translation>추가...</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+13"/>
        <source>Remove</source>
        <translation>제거</translation>
    </message>
    <message>
        <location line="-6"/>
        <source>Crash Dump:</source>
        <translation>크래시 덤프:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Browse...</source>
        <translation>찾아보기...</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Alternatively, you can submit bug reports at &lt;a href=&quot;%1&quot;&gt;emule-qt.org/submit-bug-report&lt;/a&gt;</source>
        <translation>또는 &lt;a href=&quot;%1&quot;&gt;emule-qt.org/submit-bug-report&lt;/a&gt; 에서 버그를 신고할 수 있습니다</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Submit</source>
        <translation>제출</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Please fill in both the title and description fields.</source>
        <translation>제목과 설명을 모두 입력하세요.</translation>
    </message>
    <message>
        <location line="+122"/>
        <source>Bug report submitted successfully.</source>
        <translation>버그 신고를 제출했습니다.</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Error: %1</source>
        <translation>오류: %1</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Select Screenshots</source>
        <translation>스크린샷 선택</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp)</source>
        <translation>이미지 (*.png *.jpg *.jpeg *.bmp *.gif *.webp)</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Invalid Files</source>
        <translation>잘못된 파일</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The following files are not valid images and were skipped:
%1</source>
        <translation>다음 파일은 유효한 이미지가 아니므로 건너뛰었습니다:
%1</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Select Crash Dump</source>
        <translation>크래시 덤프 선택</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Dump Files (*.dmp *.crash *.txt);;All Files (*)</source>
        <translation>덤프 파일 (*.dmp *.crash *.txt);;모든 파일 (*)</translation>
    </message>
</context>
<context>
    <name>eMule::ClientDetailDialog</name>
    <message>
        <location filename="../src/gui/dialogs/ClientDetailDialog.cpp" line="+62"/>
        <source>Client Details: %1</source>
        <translation>클라이언트 상세: %1</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>General</source>
        <translation>일반</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>User Name</source>
        <translation>사용자 이름</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>User Hash</source>
        <translation>사용자 해시</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>ID</source>
        <translation>ID</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Low ID</source>
        <translation>Low ID</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>High ID</source>
        <translation>High ID</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Client Software</source>
        <translation>클라이언트 소프트웨어</translation>
    </message>
    <message>
        <location line="+15"/>
        <location line="+4"/>
        <source>Server</source>
        <translation>서버</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Identification</source>
        <translation>식별</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Obfuscation</source>
        <translation>난독화</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Kad</source>
        <translation>Kad</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Connected</source>
        <translation>연결됨</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Not connected</source>
        <translation>연결되지 않음</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Transfer</source>
        <translation>전송</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Currently Downloading</source>
        <translation>현재 다운로드 중</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Currently Uploading</source>
        <translation>현재 업로드 중</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Downloaded (Session)</source>
        <translation>다운로드됨 (세션)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Uploaded (Session)</source>
        <translation>업로드됨 (세션)</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+2"/>
        <source>Download Rate</source>
        <translation>다운로드 속도</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Downloaded (Total)</source>
        <translation>다운로드됨 (전체)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Uploaded (Total)</source>
        <translation>업로드됨 (전체)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Scores</source>
        <translation>점수</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>DL/UP Modifier</source>
        <translation>DL/UP 보정값</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Rating (Total)</source>
        <translation>평가 (전체)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Upload Queue Score</source>
        <translation>업로드 대기열 점수</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Friend Slot</source>
        <translation>친구 슬롯</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Yes</source>
        <translation>예</translation>
    </message>
</context>
<context>
    <name>eMule::ClientListModel</name>
    <message>
        <location filename="../src/gui/controls/ClientListModel.cpp" line="+290"/>
        <location line="+13"/>
        <location line="+13"/>
        <location line="+15"/>
        <source>User Name</source>
        <translation>사용자 이름</translation>
    </message>
    <message>
        <location line="-40"/>
        <location line="+14"/>
        <location line="+12"/>
        <source>File</source>
        <translation>파일</translation>
    </message>
    <message>
        <location line="-25"/>
        <location line="+14"/>
        <source>Speed</source>
        <translation>속도</translation>
    </message>
    <message>
        <location line="-13"/>
        <location line="+15"/>
        <location line="+1"/>
        <location line="+24"/>
        <source>Transferred</source>
        <translation>전송됨</translation>
    </message>
    <message>
        <location line="-39"/>
        <source>Waited</source>
        <translation>대기 시간</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload Time</source>
        <translation>업로드 시간</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status</source>
        <translation>상태</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+28"/>
        <source>Obtained Parts</source>
        <translation>획득한 파트</translation>
    </message>
    <message>
        <location line="-21"/>
        <location line="+32"/>
        <source>Software</source>
        <translation>소프트웨어</translation>
    </message>
    <message>
        <location line="-29"/>
        <source>Available Parts</source>
        <translation>사용 가능한 파트</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Source Type</source>
        <translation>소스 유형</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>File Priority</source>
        <translation>파일 우선순위</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Rating</source>
        <translation>평가</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Score</source>
        <translation>점수</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Asked</source>
        <translation>요청됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Last Seen</source>
        <translation>마지막 확인</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Entered Queue</source>
        <translation>대기열 진입</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Banned</source>
        <translation>차단됨</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Upload Status</source>
        <translation>업로드 상태</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Download Status</source>
        <translation>다운로드 상태</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Transferred Down</source>
        <translation>다운로드됨</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Connected</source>
        <translation>연결됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hash</source>
        <translation>해시</translation>
    </message>
</context>
<context>
    <name>eMule::ClientSharedFilesDialog</name>
    <message>
        <location filename="../src/gui/dialogs/ClientSharedFilesDialog.cpp" line="+49"/>
        <source>Shared Files — %1</source>
        <translation>공유 파일 — %1</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>File Name</source>
        <translation>파일 이름</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Size</source>
        <translation>크기</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Hash</source>
        <translation>해시</translation>
    </message>
    <message>
        <location line="+38"/>
        <source>Download Selected</source>
        <translation>선택 항목 다운로드</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Close</source>
        <translation>닫기</translation>
    </message>
</context>
<context>
    <name>eMule::CollectionCreateDialog</name>
    <message>
        <location filename="../src/gui/dialogs/CollectionCreateDialog.cpp" line="+52"/>
        <source>Modify Collection...</source>
        <translation>컬렉션 수정...</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Create Collection...</source>
        <translation>컬렉션 만들기...</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Shared (0)</source>
        <translation>공유됨 (0)</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+36"/>
        <source>File Name</source>
        <translation>파일 이름</translation>
    </message>
    <message>
        <location line="-20"/>
        <source>Add to collection</source>
        <translation>컬렉션에 추가</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Remove from collection</source>
        <translation>컬렉션에서 제거</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Collection List (0)</source>
        <translation>컬렉션 목록 (0)</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Basic Options</source>
        <translation>기본 옵션</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Name:</source>
        <translation>이름:</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Advanced Options</source>
        <translation>고급 옵션</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Save collection in plain text format</source>
        <translation>컬렉션을 일반 텍스트 형식으로 저장</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Sign collection with name and key</source>
        <translation>이름과 키로 컬렉션 서명</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Save</source>
        <translation>저장</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Cancel</source>
        <translation>취소</translation>
    </message>
    <message>
        <location line="+86"/>
        <source>Shared (%1)</source>
        <translation>공유됨 (%1)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Collection List (%1)</source>
        <translation>컬렉션 목록 (%1)</translation>
    </message>
    <message>
        <location line="+26"/>
        <location line="+6"/>
        <location line="+25"/>
        <source>Collection</source>
        <translation>컬렉션</translation>
    </message>
    <message>
        <location line="-31"/>
        <source>Please enter a collection name.</source>
        <translation>컬렉션 이름을 입력하세요.</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Collection is empty. Add files first.</source>
        <translation>컬렉션이 비어 있습니다. 파일을 먼저 추가하세요.</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Failed to save collection: %1</source>
        <translation>컬렉션을 저장하지 못했습니다: %1</translation>
    </message>
</context>
<context>
    <name>eMule::CollectionViewDialog</name>
    <message>
        <location filename="../src/gui/dialogs/CollectionViewDialog.cpp" line="+42"/>
        <source>Collection: %1</source>
        <translation>컬렉션: %1</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Collection List (%1)</source>
        <translation>컬렉션 목록 (%1)</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>File Name</source>
        <translation>파일 이름</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Size</source>
        <translation>크기</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Hash</source>
        <translation>해시</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Details</source>
        <translation>상세</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Author:</source>
        <translation>작성자:</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Author Key:</source>
        <translation>작성자 키:</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Options</source>
        <translation>옵션</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Add to new category</source>
        <translation>새 카테고리에 추가</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Download</source>
        <translation>다운로드</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Close</source>
        <translation>닫기</translation>
    </message>
</context>
<context>
    <name>eMule::CommentsPanel</name>
    <message>
        <location filename="../src/gui/dialogs/CommentsPanel.cpp" line="+71"/>
        <source>Kad</source>
        <translation>Kad</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>(Kad search in progress...)</source>
        <translation>(Kad 검색 중...)</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+46"/>
        <source>Search Kad</source>
        <translation>Kad 검색</translation>
    </message>
    <message>
        <location line="-29"/>
        <source>Rating</source>
        <translation>평가</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Comment</source>
        <translation>댓글</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>File Name</source>
        <translation>파일 이름</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>User Name</source>
        <translation>사용자 이름</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Network</source>
        <translation>네트워크</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>No comments or ratings available for this file.</source>
        <translation>이 파일에 대한 댓글이나 평가가 없습니다.</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Copy</source>
        <translation>복사</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Edit spam filter...</source>
        <translation>스팸 필터 편집...</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Edit spam filter for comments</source>
        <translation>댓글 스팸 필터 편집</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ignore comments containing: (Separator | )</source>
        <translation>포함된 댓글 무시: (구분자 | )</translation>
    </message>
</context>
<context>
    <name>eMule::ContactsGraph</name>
    <message>
        <location filename="../src/gui/controls/ContactsGraph.cpp" line="+87"/>
        <source>Contacts</source>
        <translation>연락처</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Time</source>
        <translation>시간</translation>
    </message>
</context>
<context>
    <name>eMule::CoreConnectDialog</name>
    <message>
        <location filename="../src/gui/dialogs/CoreConnectDialog.cpp" line="+23"/>
        <source>Connect to Core</source>
        <translation>코어에 연결</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Could not find a local eMule core.
Enter the address and authentication token of a remote core.</source>
        <translation>로컬 eMule 코어를 찾을 수 없습니다.
원격 코어의 주소와 인증 토큰을 입력하세요.</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Remote Core</source>
        <translation>원격 코어</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Address:</source>
        <translation>주소:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Port:</source>
        <translation>포트:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>paste token here</source>
        <translation>여기에 토큰 붙여넣기</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Token:</source>
        <translation>토큰:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Save token</source>
        <translation>토큰 저장</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Connect</source>
        <translation>연결</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Exit</source>
        <translation>종료</translation>
    </message>
</context>
<context>
    <name>eMule::DetailDialog</name>
    <message>
        <location filename="../src/gui/dialogs/DetailDialog.cpp" line="+201"/>
        <source>Search Kad</source>
        <translation>Kad 검색</translation>
    </message>
    <message>
        <location line="+102"/>
        <source>Previous</source>
        <translation>이전</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Next</source>
        <translation>다음</translation>
    </message>
</context>
<context>
    <name>eMule::DownloadListModel</name>
    <message>
        <location filename="../src/gui/controls/DownloadListModel.cpp" line="+139"/>
        <location line="+390"/>
        <source>Downloading</source>
        <translation>다운로드 중</translation>
    </message>
    <message>
        <location line="-319"/>
        <source>Auto [%1]</source>
        <translation>자동 [%1]</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>File Name:	%1
ED2K Hash:	%2
Size:	%3
Completed:	%4 (%5%)
Type:	%6
Status:	%7
Priority:	%8
Sources:	%9
Requests:	%10
Accepted Requests:	%11
Transferred Data:	%12</source>
        <translation>파일 이름:	%1
ED2K 해시:	%2
크기:	%3
완료:	%4 (%5%)
유형:	%6
상태:	%7
우선순위:	%8
소스:	%9
요청:	%10
수락된 요청:	%11
전송된 데이터:	%12</translation>
    </message>
    <message>
        <location line="+62"/>
        <source>File Name</source>
        <translation>파일 이름</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Size</source>
        <translation>크기</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Completed</source>
        <translation>완료됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Speed</source>
        <translation>속도</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Progress</source>
        <translation>진행률</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Sources</source>
        <translation>소스</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority</source>
        <translation>우선순위</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status</source>
        <translation>상태</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remaining</source>
        <translation>남은 시간</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Seen Complete</source>
        <translation>완료 확인됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Last reception</source>
        <translation>마지막 수신</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Category</source>
        <translation>카테고리</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Added On</source>
        <translation>추가일</translation>
    </message>
    <message>
        <location line="+197"/>
        <source>Importing part</source>
        <translation>파트 가져오는 중</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+5"/>
        <source>Hashing</source>
        <translation>해시 계산 중</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+1"/>
        <location line="+1"/>
        <source>Completing (%1)</source>
        <translation>완료 중 (%1)</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Copying</source>
        <translation>복사 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Uncompressing</source>
        <translation>압축 해제 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Completing</source>
        <translation>완료 중</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Complete</source>
        <translation>완료</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Stopped</source>
        <translation>중지됨</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Paused</source>
        <translation>일시정지됨</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+3"/>
        <source>Insufficient disk space</source>
        <translation>디스크 공간 부족</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Error</source>
        <translation>오류</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Waiting</source>
        <translation>대기 중</translation>
    </message>
</context>
<context>
    <name>eMule::FileDetailDialog</name>
    <message>
        <location filename="../src/gui/dialogs/FileDetailDialog.cpp" line="+99"/>
        <source>File Details: %1</source>
        <translation>파일 상세: %1</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+7"/>
        <source>General</source>
        <translation>일반</translation>
    </message>
    <message>
        <location line="-6"/>
        <location line="+7"/>
        <source>File Names</source>
        <translation>파일 이름</translation>
    </message>
    <message>
        <location line="-6"/>
        <location line="+7"/>
        <source>Comments</source>
        <translation>댓글</translation>
    </message>
    <message>
        <location line="-6"/>
        <location line="+7"/>
        <source>Media Info</source>
        <translation>미디어 정보</translation>
    </message>
    <message>
        <location line="-6"/>
        <location line="+7"/>
        <source>Metadata</source>
        <translation>메타데이터</translation>
    </message>
    <message>
        <location line="-6"/>
        <location line="+7"/>
        <source>ED2K Link</source>
        <translation>ED2K 링크</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+2"/>
        <source>Archive Preview</source>
        <translation>압축 파일 미리보기</translation>
    </message>
    <message>
        <location line="+24"/>
        <location line="+48"/>
        <source>File Name</source>
        <translation>파일 이름</translation>
    </message>
    <message>
        <location line="-47"/>
        <source>Hash (MD4)</source>
        <translation>해시 (MD4)</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>AICH Hash</source>
        <translation>AICH 해시</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>File Size</source>
        <translation>파일 크기</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Completed</source>
        <translation>완료됨</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Status</source>
        <translation>상태</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority</source>
        <translation>우선순위</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+24"/>
        <source>Sources</source>
        <translation>소스</translation>
    </message>
    <message>
        <location line="-19"/>
        <source>File Path</source>
        <translation>파일 경로</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Created</source>
        <translation>생성됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Last Seen Complete</source>
        <translation>마지막으로 완전한 상태로 확인됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Last Reception</source>
        <translation>마지막 수신</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>No alternative file names reported by sources. Use “Search Kad” to look them up.</source>
        <translation>소스에서 보고한 대체 파일 이름이 없습니다. “Kad 검색”을 사용하여 조회하세요.</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Search Kad</source>
        <translation>Kad 검색</translation>
    </message>
    <message>
        <location line="+73"/>
        <source>No media information available.</source>
        <translation>미디어 정보가 없습니다.</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Title</source>
        <translation>제목</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Artist</source>
        <translation>아티스트</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Album</source>
        <translation>앨범</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Codec</source>
        <translation>코덱</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Bitrate</source>
        <translation>비트레이트</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Length</source>
        <translation>길이</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Link Options</source>
        <translation>링크 옵션</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Include Hashset</source>
        <translation>해시셋 포함</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Include Hostname</source>
        <translation>호스트 이름 포함</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add your hostname or public IPv6 as a source</source>
        <translation>자신의 호스트 이름 또는 공용 IPv6를 소스로 추가</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>HTML Format</source>
        <translation>HTML 형식</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Copy to Clipboard</source>
        <translation>클립보드에 복사</translation>
    </message>
</context>
<context>
    <name>eMule::FindInListDialog</name>
    <message>
        <location filename="../src/gui/dialogs/FindInListDialog.cpp" line="+23"/>
        <source>Search</source>
        <translation>검색</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Search for:</source>
        <translation>검색어:</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Search in column:</source>
        <translation>열에서 검색:</translation>
    </message>
</context>
<context>
    <name>eMule::FirstStartWizard</name>
    <message>
        <location filename="../src/gui/dialogs/FirstStartWizard.cpp" line="+29"/>
        <source>eMule First Runtime Wizard</source>
        <translation>eMule 첫 실행 마법사</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>Ports and Connection</source>
        <translation>포트 및 연결</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Connection</source>
        <translation>연결</translation>
    </message>
    <message>
        <location line="+40"/>
        <source>eMule uses two ports for communication with servers and clients. These ports must be free and available for remote clients. The TCP port must be available to ensure the main functionality of eMule. The UDP port is used for Kad (serverless network) and to reduce network usage (Overhead).</source>
        <translation>eMule은 서버 및 클라이언트와의 통신에 두 개의 포트를 사용합니다. 이 포트는 원격 클라이언트에서 사용할 수 있어야 합니다. TCP 포트는 eMule의 주요 기능을 보장하기 위해 필요합니다. UDP 포트는 Kad(서버리스 네트워크) 및 네트워크 사용량 절감(오버헤드)에 사용됩니다.</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>You can change the ports here while no network activities have started.</source>
        <translation>네트워크 활동이 시작되지 않은 동안 여기에서 포트를 변경할 수 있습니다.</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>TCP:</source>
        <translation>TCP:</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>UDP:</source>
        <translation>UDP:</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Use UPnP to Setup Ports</source>
        <translation>UPnP로 포트 설정</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Choose which Network(s) you want to use</source>
        <translation>사용할 네트워크 선택</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Kad</source>
        <translation>Kad</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>eD2K</source>
        <translation>eD2K</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>&lt; Back</source>
        <translation>&lt; 뒤로</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Finish</source>
        <translation>완료</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Cancel</source>
        <translation>취소</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Help</source>
        <translation>도움말</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>Network</source>
        <translation>네트워크</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>You must enable at least one network (Kad or eD2K).</source>
        <translation>최소 하나의 네트워크(Kad 또는 eD2K)를 활성화해야 합니다.</translation>
    </message>
    <message>
        <location line="+62"/>
        <source>UPnP</source>
        <translation>UPnP</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>UPnP port mapping timed out. Your router may not support UPnP, or it may be disabled. You can set up port forwarding manually.</source>
        <translation>UPnP 포트 매핑 시간이 초과되었습니다. 라우터가 UPnP를 지원하지 않거나 비활성화되어 있을 수 있습니다. 포트 포워딩을 수동으로 설정할 수 있습니다.</translation>
    </message>
</context>
<context>
    <name>eMule::ImportDownloadsDialog</name>
    <message>
        <location filename="../src/gui/dialogs/ImportDownloadsDialog.cpp" line="+38"/>
        <source>Convert Part Files</source>
        <translation>Part 파일 변환</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Current Job</source>
        <translation>현재 작업</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+217"/>
        <source>Idle</source>
        <translation>유휴</translation>
    </message>
    <message>
        <location line="-206"/>
        <source>Job Queue</source>
        <translation>작업 대기열</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Filename</source>
        <translation>파일 이름</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Status</source>
        <translation>상태</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Size</source>
        <translation>크기</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>File Hash</source>
        <translation>파일 해시</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Add Imports...</source>
        <translation>가져오기 추가...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Retry Selected</source>
        <translation>선택 항목 재시도</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove Selected</source>
        <translation>선택 항목 제거</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Close</source>
        <translation>닫기</translation>
    </message>
    <message>
        <location line="+57"/>
        <source>Import Downloads</source>
        <translation>다운로드 가져오기</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Import Downloads is only available for local connections.</source>
        <translation>다운로드 가져오기는 로컬 연결에서만 사용 가능합니다.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Select folder to scan for importable downloads</source>
        <translation>가져올 수 있는 다운로드를 검색할 폴더 선택</translation>
    </message>
    <message>
        <location line="+119"/>
        <source>Converting...</source>
        <translation>변환 중...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Done</source>
        <translation>완료</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>OK</source>
        <translation>확인</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Queued</source>
        <translation>대기 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>In Progress</source>
        <translation>진행 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Out of Disk Space</source>
        <translation>디스크 공간 부족</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>.part.met Not Found</source>
        <translation>.part.met을 찾을 수 없음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>I/O Error</source>
        <translation>I/O 오류</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Failed</source>
        <translation>실패</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bad Format</source>
        <translation>잘못된 형식</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Already Exists</source>
        <translation>이미 존재함</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unknown</source>
        <translation>알 수 없음</translation>
    </message>
</context>
<context>
    <name>eMule::IrcPanel</name>
    <message>
        <location filename="../src/gui/panels/IrcPanel.cpp" line="+161"/>
        <source>Select an IRC nick.</source>
        <translation>IRC 닉네임을 선택하세요.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Should be no longer than 25 characters: letters, digits or symbols [_-{}]\.
Nick can be changed again in Options-&gt;IRC.</source>
        <translation>25자 이내: 문자, 숫자 또는 기호 [_-{}]\.
닉네임은 옵션-&gt;IRC에서 다시 변경할 수 있습니다.</translation>
    </message>
    <message>
        <location line="+84"/>
        <source>Disconnect</source>
        <translation>연결 해제</translation>
    </message>
    <message>
        <location line="+27"/>
        <location line="+394"/>
        <source>Connect</source>
        <translation>연결</translation>
    </message>
    <message>
        <location line="-103"/>
        <source>Nick in use</source>
        <translation>닉네임 사용 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The nick &quot;%1&quot; is already in use.
Please choose another:</source>
        <translation>닉네임 &quot;%1&quot;이(가) 이미 사용 중입니다.
다른 닉네임을 선택하세요:</translation>
    </message>
    <message>
        <location line="+33"/>
        <location line="+561"/>
        <source>Nick</source>
        <translation>닉네임</translation>
    </message>
    <message>
        <location line="-502"/>
        <source>Status</source>
        <translation>상태</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Close</source>
        <translation>닫기</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Smileys</source>
        <translation>이모티콘</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Bold</source>
        <translation>굵게</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Italic</source>
        <translation>기울임</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Underline</source>
        <translation>밑줄</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Color</source>
        <translation>색상</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Reset Formatting</source>
        <translation>서식 초기화</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Type a message...</source>
        <translation>메시지 입력...</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Send</source>
        <translation>보내기</translation>
    </message>
    <message>
        <location line="+79"/>
        <source>Channel</source>
        <translation>채널</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Users</source>
        <translation>사용자</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Topic</source>
        <translation>주제</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Channels</source>
        <translation>채널</translation>
    </message>
    <message>
        <location line="+285"/>
        <source>Nick (%1)</source>
        <translation>닉네임 (%1)</translation>
    </message>
</context>
<context>
    <name>eMule::KadContactHistogram</name>
    <message>
        <location filename="../src/gui/controls/KadContactHistogram.cpp" line="+190"/>
        <source>Contacts</source>
        <translation>연락처</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Kademlia Network</source>
        <translation>Kademlia 네트워크</translation>
    </message>
</context>
<context>
    <name>eMule::KadContactsModel</name>
    <message>
        <location filename="../src/gui/controls/KadContactsModel.cpp" line="+81"/>
        <source>Status</source>
        <translation>상태</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Client ID</source>
        <translation>클라이언트 ID</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Distance</source>
        <translation>거리</translation>
    </message>
</context>
<context>
    <name>eMule::KadLookupGraph</name>
    <message>
        <location filename="../src/gui/controls/KadLookupGraph.cpp" line="+69"/>
        <source>No search selected</source>
        <translation>선택된 검색 없음</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Distance</source>
        <translation>거리</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Time</source>
        <translation>시간</translation>
    </message>
    <message>
        <location line="+235"/>
        <source>Our node (search initiator)</source>
        <translation>우리 노드(검색 시작자)</translation>
    </message>
</context>
<context>
    <name>eMule::KadPanel</name>
    <message>
        <location filename="../src/gui/panels/KadPanel.cpp" line="+113"/>
        <location line="+12"/>
        <location line="+189"/>
        <location line="+2"/>
        <location line="+37"/>
        <location line="+192"/>
        <source>▸ Contacts (0)</source>
        <translation>▸ 연락처 (0)</translation>
    </message>
    <message>
        <location line="-431"/>
        <location line="+12"/>
        <location line="+369"/>
        <location line="+92"/>
        <source>▸ Current Searches (0)</source>
        <translation>▸ 현재 검색 (0)</translation>
    </message>
    <message>
        <location line="-266"/>
        <location line="+328"/>
        <source>▸ Search Details</source>
        <translation>▸ 검색 상세</translation>
    </message>
    <message>
        <location line="-248"/>
        <source>Recheck Firewall</source>
        <translation>방화벽 재확인</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+235"/>
        <source>Connect</source>
        <translation>연결</translation>
    </message>
    <message>
        <location line="-443"/>
        <location line="+223"/>
        <location line="+28"/>
        <source>Bootstrap</source>
        <translation>부트스트랩</translation>
    </message>
    <message>
        <location line="-262"/>
        <source>Downloading...</source>
        <translation>다운로드 중...</translation>
    </message>
    <message>
        <location line="+14"/>
        <location line="+5"/>
        <location line="+8"/>
        <location line="+69"/>
        <source>Kademlia</source>
        <translation>Kademlia</translation>
    </message>
    <message>
        <location line="-81"/>
        <source>Failed to download nodes.dat: %1</source>
        <translation>nodes.dat 다운로드 실패: %1</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Downloaded nodes.dat is empty.</source>
        <translation>다운로드한 nodes.dat가 비어 있습니다.</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Failed to save nodes.dat: %1</source>
        <translation>nodes.dat 저장 실패: %1</translation>
    </message>
    <message>
        <location line="+210"/>
        <source>IP Address:</source>
        <translation>IP 주소:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Port:</source>
        <translation>포트:</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Nodes.dat from URL:</source>
        <translation>URL에서 Nodes.dat:</translation>
    </message>
    <message>
        <location line="+135"/>
        <location line="+3"/>
        <source>▸ Contacts (%1)</source>
        <translation>▸ 연락처 (%1)</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>▸ Current Searches (%1)</source>
        <translation>▸ 현재 검색 (%1)</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Disconnect</source>
        <translation>연결 해제</translation>
    </message>
    <message>
        <location line="+64"/>
        <source>▸ Search Details (%1)</source>
        <translation>▸ 검색 상세 (%1)</translation>
    </message>
</context>
<context>
    <name>eMule::KadSearchesModel</name>
    <message>
        <location filename="../src/gui/controls/KadSearchesModel.cpp" line="+76"/>
        <source>No.</source>
        <translation>번호</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Key</source>
        <translation>키</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type</source>
        <translation>유형</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Name</source>
        <translation>이름</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status</source>
        <translation>상태</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Load</source>
        <translation>불러오기</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Packets Sent</source>
        <translation>전송된 패킷</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Responses</source>
        <translation>응답</translation>
    </message>
</context>
<context>
    <name>eMule::LogWidget</name>
    <message>
        <location filename="../src/gui/controls/LogWidget.cpp" line="+66"/>
        <location line="+2"/>
        <source>Server Info</source>
        <translation>서버 정보</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+2"/>
        <source>Log</source>
        <translation>로그</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+2"/>
        <source>Verbose</source>
        <translation>상세</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+2"/>
        <source>Kad</source>
        <translation>Kad</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+2"/>
        <source>IPC</source>
        <translation>IPC</translation>
    </message>
    <message>
        <location line="+305"/>
        <source>Click here to check if a new version is available</source>
        <translation>여기를 클릭하여 새 버전이 있는지 확인하세요</translation>
    </message>
</context>
<context>
    <name>eMule::MainWindow</name>
    <message>
        <location filename="../src/gui/app/MainWindow.cpp" line="+73"/>
        <source>eMule Qt v%1</source>
        <translation>eMule Qt v%1</translation>
    </message>
    <message>
        <location line="+94"/>
        <location line="+9"/>
        <source>New Version Available</source>
        <translation>새 버전 사용 가능</translation>
    </message>
    <message>
        <location line="+169"/>
        <source>eD2K: Connected (LowID)</source>
        <translation>eD2K: 연결됨 (LowID)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>eD2K: Connected</source>
        <translation>eD2K: 연결됨</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>eD2K: Connecting...</source>
        <translation>eD2K: 연결 중...</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+952"/>
        <source>eD2K: Disconnected</source>
        <translation>eD2K: 연결 해제됨</translation>
    </message>
    <message>
        <location line="-938"/>
        <source>Kad: Connected</source>
        <translation>Kad: 연결됨</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Kad: Connected (Firewalled)</source>
        <translation>Kad: 연결됨 (방화벽)</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Kad: Connecting...</source>
        <translation>Kad: 연결 중...</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+930"/>
        <source>Kad: Disconnected</source>
        <translation>Kad: 연결 해제됨</translation>
    </message>
    <message>
        <location line="-921"/>
        <source>Users: %1 | Files: %2</source>
        <translation>사용자: %1 | 파일: %2</translation>
    </message>
    <message>
        <location line="+212"/>
        <source>Open Incoming Folder...</source>
        <translation>수신 폴더 열기...</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Import Downloads (eM,eD,ON)...</source>
        <translation>다운로드 가져오기 (eM,eD,ON)...</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>eMule First Runtime Wizard...</source>
        <translation>eMule 첫 실행 마법사...</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>IP Filter...</source>
        <translation>IP 필터...</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Paste eD2K Links...</source>
        <translation>eD2K 링크 붙여넣기...</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Links</source>
        <translation>링크</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>eMule Homepage</source>
        <translation>eMule 홈페이지</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>FAQ</source>
        <translation>자주 묻는 질문</translation>
    </message>
    <message>
        <location line="-456"/>
        <location line="+7"/>
        <location line="+452"/>
        <source>Version Check</source>
        <translation>버전 확인</translation>
    </message>
    <message>
        <location line="-514"/>
        <source>Quit eMule Qt</source>
        <translation>eMule Qt 종료</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>eMule Qt %1 has been released.</source>
        <translation>eMule Qt %1이(가) 출시되었습니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Version %1 of eMule Qt was released on %2.</source>
        <translation>eMule Qt 버전 %1은(는) %2에 출시되었습니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Version %1 of eMule Qt is available.</source>
        <translation>eMule Qt 버전 %1을(를) 사용할 수 있습니다.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>%1

You are running %2. Open the eMule Qt website?</source>
        <translation>%1

현재 %2을(를) 사용 중입니다. eMule Qt 웹사이트를 여시겠습니까?</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>You are running the latest version of eMule Qt (v%1).</source>
        <translation>최신 버전의 eMule Qt(v%1)를 사용 중입니다.</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Could not check for a new version:

%1</source>
        <translation>새 버전을 확인할 수 없습니다:

%1</translation>
    </message>
    <message>
        <location line="+63"/>
        <source>Cannot Connect</source>
        <translation>연결할 수 없음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Both the eD2K and Kad networks are disabled.

Enable at least one under Options → Connection to connect.</source>
        <translation>eD2K와 Kad 네트워크가 모두 비활성화되어 있습니다.

연결하려면 옵션 → 연결에서 최소한 하나를 활성화하세요.</translation>
    </message>
    <message>
        <location line="+178"/>
        <source>Connected</source>
        <translation>연결됨</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Disconnected</source>
        <translation>연결 끊김</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>eMule Qt v%1 (%2)
Up: %3 | Down: %4</source>
        <translation>eMule Qt v%1 (%2)
업로드: %3 | 다운로드: %4</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Confirm Exit</source>
        <translation>종료 확인</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Are you sure you want to exit eMule?</source>
        <translation>eMule을 종료하시겠습니까?</translation>
    </message>
    <message>
        <location line="+150"/>
        <source>Submit Bug Report...</source>
        <translation>버그 신고 제출...</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Scheduler</source>
        <translation>스케줄러</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Disable Scheduler</source>
        <translation>스케줄러 비활성화</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Enable Scheduler</source>
        <translation>스케줄러 활성화</translation>
    </message>
    <message>
        <location line="+192"/>
        <source>Main</source>
        <translation>메인</translation>
    </message>
    <message>
        <location line="+153"/>
        <source>Toolbar Skins</source>
        <translation>도구 모음 스킨</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Select Toolbar Bitmap...</source>
        <translation>도구 모음 비트맵 선택...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Select Toolbar Bitmap</source>
        <translation>도구 모음 비트맵 선택</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Images (*.bmp *.png *.jpg);;All Files (*)</source>
        <translation>이미지 (*.bmp *.png *.jpg);;모든 파일 (*)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Select Toolbar Bitmap Directory...</source>
        <translation>도구 모음 비트맵 폴더 선택...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Select Toolbar Bitmap Directory</source>
        <translation>도구 모음 비트맵 폴더 선택</translation>
    </message>
    <message>
        <location line="+17"/>
        <location line="+61"/>
        <source>Default</source>
        <translation>기본값</translation>
    </message>
    <message>
        <location line="-25"/>
        <source>Skin Profiles</source>
        <translation>스킨 프로필</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Select Skin File...</source>
        <translation>스킨 파일 선택...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Select Skin Profile</source>
        <translation>스킨 프로필 선택</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Skin Files (*.eMuleSkin.ini);;All Files (*)</source>
        <translation>스킨 파일 (*.eMuleSkin.ini);;모든 파일 (*)</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Select Skin Directory...</source>
        <translation>스킨 폴더 선택...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Select Skin Directory</source>
        <translation>스킨 폴더 선택</translation>
    </message>
    <message>
        <location line="+40"/>
        <source>Text Label Options</source>
        <translation>텍스트 레이블 옵션</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Customize Toolbar...</source>
        <translation>도구 모음 사용자 지정...</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Disconnect</source>
        <translation>연결 해제</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Connect</source>
        <translation>연결</translation>
    </message>
    <message>
        <location line="+71"/>
        <source>Ready</source>
        <translation>준비됨</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Users: 0 | Files: 0</source>
        <translation>사용자: 0 | 파일: 0</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Up: 0.0</source>
        <translation>업로드: 0.0</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Down: 0.0</source>
        <translation>다운로드: 0.0</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Double-click for Network Information</source>
        <translation>네트워크 정보를 보려면 더블 클릭</translation>
    </message>
</context>
<context>
    <name>eMule::MediaInfoPanel</name>
    <message>
        <location filename="../src/gui/dialogs/MediaInfoPanel.cpp" line="+44"/>
        <source>Scanning...</source>
        <translation>스캔 중...</translation>
    </message>
    <message>
        <location line="+17"/>
        <location line="+178"/>
        <source>No media information available.</source>
        <translation>미디어 정보가 없습니다.</translation>
    </message>
    <message>
        <location line="-146"/>
        <source>estimated</source>
        <translation>추정치</translation>
    </message>
    <message>
        <location line="+164"/>
        <source>General</source>
        <translation>일반</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Format:</source>
        <translation>형식:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Length:</source>
        <translation>길이:</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Video</source>
        <translation>비디오</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+17"/>
        <source>Codec:</source>
        <translation>코덱:</translation>
    </message>
    <message>
        <location line="-16"/>
        <location line="+17"/>
        <source>Bitrate:</source>
        <translation>비트레이트:</translation>
    </message>
    <message>
        <location line="-16"/>
        <source>Resolution:</source>
        <translation>해상도:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Aspect Ratio:</source>
        <translation>화면 비율:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>FPS:</source>
        <translation>FPS:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Audio</source>
        <translation>오디오</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Channels:</source>
        <translation>채널:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Sample Rate:</source>
        <translation>샘플 레이트:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Language:</source>
        <translation>언어:</translation>
    </message>
</context>
<context>
    <name>eMule::MessagesPanel</name>
    <message>
        <location filename="../src/gui/panels/MessagesPanel.cpp" line="+120"/>
        <source>Me</source>
        <translation>나</translation>
    </message>
    <message>
        <location line="+71"/>
        <source>Friends (0)</source>
        <translation>친구 (0)</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Info</source>
        <translation>정보</translation>
    </message>
    <message>
        <location line="+15"/>
        <location line="+289"/>
        <source>Name:</source>
        <translation>이름:</translation>
    </message>
    <message>
        <location line="-288"/>
        <source>Hash:</source>
        <translation>해시:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Software:</source>
        <translation>소프트웨어:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Identification:</source>
        <translation>식별:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Uploaded:</source>
        <translation>업로드됨:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloaded:</source>
        <translation>다운로드됨:</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Messages</source>
        <translation>메시지</translation>
    </message>
    <message>
        <location line="+34"/>
        <source>Smileys</source>
        <translation>이모티콘</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Type a message...</source>
        <translation>메시지 입력...</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Send</source>
        <translation>보내기</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Close</source>
        <translation>닫기</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Add...</source>
        <translation>추가...</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Remove</source>
        <translation>제거</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Send Message</source>
        <translation>메시지 보내기</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>View Shared Files</source>
        <translation>공유 파일 보기</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Establish Friend Slot</source>
        <translation>친구 슬롯 설정</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Find...</source>
        <translation>찾기...</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>Friends (%1)</source>
        <translation>친구 (%1)</translation>
    </message>
    <message>
        <location line="+108"/>
        <source>Find Friend</source>
        <translation>친구 찾기</translation>
    </message>
</context>
<context>
    <name>eMule::MetadataPage</name>
    <message>
        <location filename="../src/gui/dialogs/MetadataPage.cpp" line="+104"/>
        <source>No metadata tags available.</source>
        <translation>사용 가능한 메타데이터 태그가 없습니다.</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Tag Name</source>
        <translation>태그 이름</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type</source>
        <translation>유형</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Value</source>
        <translation>값</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Unknown</source>
        <translation>알 수 없음</translation>
    </message>
</context>
<context>
    <name>eMule::MiniMuleWidget</name>
    <message>
        <location filename="../src/gui/app/MiniMuleWidget.cpp" line="+73"/>
        <source>Yes</source>
        <translation>예</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>No</source>
        <translation>아니오</translation>
    </message>
    <message>
        <location line="+110"/>
        <source>Connected</source>
        <translation>연결됨</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Upload</source>
        <translation>업로드</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Download</source>
        <translation>다운로드</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Completed</source>
        <translation>완료됨</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Free Space</source>
        <translation>여유 공간</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Restore Window</source>
        <translation>창 복원</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Open Incoming Folder</source>
        <translation>수신 폴더 열기</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Options</source>
        <translation>옵션</translation>
    </message>
</context>
<context>
    <name>eMule::NetworkInfoDialog</name>
    <message>
        <location filename="../src/gui/dialogs/NetworkInfoDialog.cpp" line="+52"/>
        <source>Network Information</source>
        <translation>네트워크 정보</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>&lt;i&gt;Loading...&lt;/i&gt;</source>
        <translation>&lt;i&gt;로딩 중...&lt;/i&gt;</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>&lt;b&gt;Not connected to daemon.&lt;/b&gt;</source>
        <translation>&lt;b&gt;데몬에 연결되지 않았습니다.&lt;/b&gt;</translation>
    </message>
    <message>
        <location line="+56"/>
        <source>Connected</source>
        <translation>연결됨</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+87"/>
        <source>Connecting</source>
        <translation>연결 중</translation>
    </message>
    <message>
        <location line="-85"/>
        <location line="+87"/>
        <source>Disconnected</source>
        <translation>연결 해제됨</translation>
    </message>
    <message>
        <location line="-72"/>
        <source>Unknown</source>
        <translation>알 수 없음</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Low ID</source>
        <translation>Low ID</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>High ID</source>
        <translation>High ID</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Obfuscated</source>
        <translation>난독화됨</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Normal</source>
        <translation>보통</translation>
    </message>
    <message>
        <location line="+32"/>
        <location line="+13"/>
        <source>Firewalled</source>
        <translation>방화벽</translation>
    </message>
    <message>
        <location line="-13"/>
        <location line="+15"/>
        <source>Open</source>
        <translation>개방</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>unverified</source>
        <translation>미확인</translation>
    </message>
    <message>
        <location line="+53"/>
        <source>Disabled</source>
        <translation>비활성화됨</translation>
    </message>
</context>
<context>
    <name>eMule::OptionsDialog</name>
    <message>
        <location filename="../src/gui/dialogs/OptionsDialog.cpp" line="+75"/>
        <source>Options</source>
        <translation>옵션</translation>
    </message>
    <message>
        <location line="+39"/>
        <location line="+1649"/>
        <source>OK</source>
        <translation>확인</translation>
    </message>
    <message>
        <location line="-1648"/>
        <location line="+1649"/>
        <source>Cancel</source>
        <translation>취소</translation>
    </message>
    <message>
        <location line="-1648"/>
        <location line="+2997"/>
        <source>Apply</source>
        <translation>적용</translation>
    </message>
    <message>
        <location line="-2996"/>
        <source>Help</source>
        <translation>도움말</translation>
    </message>
    <message>
        <location line="+234"/>
        <location line="+1640"/>
        <location line="+63"/>
        <location line="+5"/>
        <location line="+9"/>
        <location line="+11"/>
        <source>IP Filter</source>
        <translation>IP 필터</translation>
    </message>
    <message>
        <location line="-1727"/>
        <source>IP filter reloaded: %1 entries.</source>
        <translation>IP 필터 다시 로드됨: %1개 항목.</translation>
    </message>
    <message>
        <location line="+205"/>
        <source>User Name</source>
        <translation>사용자 이름</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+3020"/>
        <source>Language</source>
        <translation>언어</translation>
    </message>
    <message>
        <location line="-3017"/>
        <source>System Default</source>
        <translation>시스템 기본값</translation>
    </message>
    <message>
        <location line="+41"/>
        <location line="+580"/>
        <location line="+292"/>
        <location line="+372"/>
        <location line="+275"/>
        <source>Miscellaneous</source>
        <translation>기타</translation>
    </message>
    <message>
        <location line="-1516"/>
        <source>Bring to front on link click</source>
        <translation>링크 클릭 시 앞으로 가져오기</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Prompt on exit</source>
        <translation>종료 시 확인</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Enable online signature</source>
        <translation>온라인 서명 활성화</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Enable MiniMule</source>
        <translation>MiniMule 활성화</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Prevent standby mode while running</source>
        <translation>실행 중 대기 모드 방지</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Edit Web Services...</source>
        <translation>웹 서비스 편집...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Handle eD2K Links</source>
        <translation>eD2K 링크 처리</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Startup</source>
        <translation>시작</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Check for new version</source>
        <translation>새 버전 확인</translation>
    </message>
    <message>
        <location line="+4"/>
        <source> Days</source>
        <translation> 일</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Show splash screen</source>
        <translation>시작 화면 표시</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Start minimized</source>
        <translation>최소화 상태로 시작</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Start with macOS</source>
        <translation>macOS와 함께 시작</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Start with Windows</source>
        <translation>Windows와 함께 시작</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Start with system</source>
        <translation>시스템과 함께 시작</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+2928"/>
        <source>Core</source>
        <translation>코어</translation>
    </message>
    <message>
        <location line="-2923"/>
        <source>Address:</source>
        <translation>주소:</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+1048"/>
        <location line="+631"/>
        <source>Port:</source>
        <translation>포트:</translation>
    </message>
    <message>
        <location line="-1676"/>
        <source>authentication token</source>
        <translation>인증 토큰</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Token:</source>
        <translation>토큰:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Changes require a restart to take effect.</source>
        <translation>변경 사항을 적용하려면 재시작이 필요합니다.</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+9"/>
        <source>Shutdown eMule Core</source>
        <translation>eMule 코어 종료</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>This will shut down both the eMule Core and the GUI.

Are you sure you want to continue?</source>
        <translation>eMule 코어와 GUI가 모두 종료됩니다.

계속하시겠습니까?</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>Progressbar style</source>
        <translation>진행 표시줄 스타일</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>flat</source>
        <translation>평면</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>round</source>
        <translation>둥근</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Tooltip delay time [sec.]</source>
        <translation>툴팁 지연 시간 [초]</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Minimize to system tray</source>
        <translation>시스템 트레이로 최소화</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Download list double-click to expand</source>
        <translation>더블 클릭으로 다운로드 목록 확장</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show percentage of download completion in progressbar</source>
        <translation>진행 표시줄에 다운로드 완료율 표시</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show transfer rates on title</source>
        <translation>제목에 전송 속도 표시</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show download info on category tabs</source>
        <translation>카테고리 탭에 다운로드 정보 표시</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Auto clear completed downloads</source>
        <translation>완료된 다운로드 자동 삭제</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show additional toolbar on Transfers window</source>
        <translation>전송 창에 추가 도구 모음 표시</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show speed graph in toolbar</source>
        <translation>도구 모음에 속도 그래프 표시</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Speed graph time range (minutes):</source>
        <translation>속도 그래프 시간 범위(분):</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Remember open searches between restarts</source>
        <translation>재시작 간 열린 검색 기억</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Use original eMule icons</source>
        <translation>원본 eMule 아이콘 사용</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Save CPU &amp;&amp; Memory Usage</source>
        <translation>CPU &amp;&amp; 메모리 사용량 절약</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Disable Known Clients list</source>
        <translation>알려진 클라이언트 목록 비활성화</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Disable Queue list</source>
        <translation>대기열 목록 비활성화</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Font for Server-, Message- and IRC-Window</source>
        <translation>서버, 메시지 및 IRC 창의 글꼴</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Select Font...</source>
        <translation>글꼴 선택...</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Select Font</source>
        <translation>글꼴 선택</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Auto completion (history function)</source>
        <translation>자동 완성(기록 기능)</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+586"/>
        <location line="+926"/>
        <location line="+108"/>
        <location line="+295"/>
        <location line="+269"/>
        <location line="+28"/>
        <source>Enabled</source>
        <translation>활성화됨</translation>
    </message>
    <message>
        <location line="-2210"/>
        <source>Reset</source>
        <translation>초기화</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>Capacities</source>
        <translation>용량</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Download</source>
        <translation>다운로드</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+6"/>
        <source> KB/s</source>
        <translation> KB/s</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>Upload</source>
        <translation>업로드</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Limits</source>
        <translation>제한</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Download limit</source>
        <translation>다운로드 제한</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Upload limit</source>
        <translation>업로드 제한</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Client Port</source>
        <translation>클라이언트 포트</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>TCP</source>
        <translation>TCP</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>UDP</source>
        <translation>UDP</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Disable</source>
        <translation>비활성화</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Test Ports</source>
        <translation>포트 테스트</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Use UPnP to Setup Ports</source>
        <translation>UPnP로 포트 설정</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Port forwarding: unknown</source>
        <translation>포트 포워딩: 알 수 없음</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Max. Sources/File</source>
        <translation>최대 소스/파일</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Hard limit</source>
        <translation>하드 제한</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Connection Limits</source>
        <translation>연결 제한</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Max. connections</source>
        <translation>최대 연결 수</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Autoconnect on startup</source>
        <translation>시작 시 자동 연결</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Reconnect on loss</source>
        <translation>연결 끊김 시 재연결</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Show overhead bandwidth</source>
        <translation>오버헤드 대역폭 표시</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Wizard...</source>
        <translation>마법사...</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Network</source>
        <translation>네트워크</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Kad</source>
        <translation>Kad</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>eD2K</source>
        <translation>eD2K</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Separate IPv6 queue</source>
        <translation>별도의 IPv6 대기열</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Alternate freed upload slots between IPv4 and IPv6 clients when both are waiting, so IPv6 peers are not outbid on score alone. When only one family is waiting, no slot is held back.</source>
        <translation>IPv4와 IPv6 클라이언트가 모두 대기 중일 때 해제된 업로드 슬롯을 번갈아 배정하여 IPv6 피어가 점수만으로 밀리지 않도록 합니다. 한쪽만 대기 중이면 슬롯을 남겨두지 않습니다.</translation>
    </message>
    <message>
        <location line="+54"/>
        <location line="+1300"/>
        <source>General</source>
        <translation>일반</translation>
    </message>
    <message>
        <location line="-1297"/>
        <source>Enable proxy</source>
        <translation>프록시 활성화</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Proxy type:</source>
        <translation>프록시 유형:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>No Proxy</source>
        <translation>프록시 없음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>SOCKS4</source>
        <translation>SOCKS4</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>SOCKS4a</source>
        <translation>SOCKS4a</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>SOCKS5</source>
        <translation>SOCKS5</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>HTTP/1.0</source>
        <translation>HTTP/1.0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>HTTP/1.1</source>
        <translation>HTTP/1.1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Proxy host:</source>
        <translation>프록시 호스트:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Proxy port:</source>
        <translation>프록시 포트:</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Authentication</source>
        <translation>인증</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Enable authentication</source>
        <translation>인증 활성화</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Name:</source>
        <translation>이름:</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+653"/>
        <location line="+693"/>
        <location line="+19"/>
        <source>Password:</source>
        <translation>비밀번호:</translation>
    </message>
    <message>
        <location line="-1332"/>
        <source>Update</source>
        <translation>업데이트</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Remove dead servers after</source>
        <translation>응답 없는 서버 제거, 재시도 횟수:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>retries</source>
        <translation>회</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Auto-update server list at startup</source>
        <translation>시작 시 서버 목록 자동 업데이트</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>List...</source>
        <translation>목록...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Server List URL</source>
        <translation>서버 목록 URL</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Enter the URL for server.met download:</source>
        <translation>server.met 다운로드 URL을 입력하세요:</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Update server list when connecting to a server</source>
        <translation>서버 연결 시 서버 목록 업데이트</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Update server list when a client connects</source>
        <translation>클라이언트 연결 시 서버 목록 업데이트</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Use smart LowID check on connect</source>
        <translation>연결 시 스마트 LowID 확인 사용</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Safe Connect</source>
        <translation>안전 연결</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Autoconnect to servers in static list only</source>
        <translation>고정 목록의 서버에만 자동 연결</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Use priority system</source>
        <translation>우선순위 시스템 사용</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Use the manual server order (drag/Move Up-Down)</source>
        <translation>수동 서버 순서 사용(끌기/위로-아래로 이동)</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Set manually added servers to high priority</source>
        <translation>수동 추가 서버를 높은 우선순위로 설정</translation>
    </message>
    <message>
        <location line="+81"/>
        <source>Incoming Files</source>
        <translation>수신 파일</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Select Incoming Directory</source>
        <translation>수신 디렉터리 선택</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Temporary Files</source>
        <translation>임시 파일</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Select Temporary Directory</source>
        <translation>임시 디렉터리 선택</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Shared Directories (Ctrl+Click includes subdirectories)</source>
        <translation>공유 디렉터리(Ctrl+클릭으로 하위 디렉터리 포함)</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Add UNC share</source>
        <translation>UNC 공유 추가</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Add UNC Share</source>
        <translation>UNC 공유 추가</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Enter UNC path (e.g., \\server\share):</source>
        <translation>UNC 경로를 입력하세요(예: \\server\share):</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Invalid Path</source>
        <translation>잘못된 경로</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>A UNC path must start with \\.</source>
        <translation>UNC 경로는 \\로 시작해야 합니다.</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>UNC shares are only supported on Windows</source>
        <translation>UNC 공유는 Windows에서만 지원됩니다</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Initializations</source>
        <translation>초기화</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Add files to download in paused mode</source>
        <translation>일시정지 모드로 파일 다운로드 추가</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Add new shared files with auto priority</source>
        <translation>자동 우선순위로 새 공유 파일 추가</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Add new downloads with auto priority</source>
        <translation>자동 우선순위로 새 다운로드 추가</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Remember download sources between restarts</source>
        <translation>재시작 간 다운로드 소스 기억</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Stores each download&apos;s best sources in the temp folder and reconnects to them on the next start, so a rare file does not have to find its peers again.</source>
        <translation>각 다운로드의 최적 소스를 임시 폴더에 저장하고 다음 시작 시 다시 연결하므로, 희귀한 파일이 소스를 다시 찾을 필요가 없습니다.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Auto cleanup file names of new downloads</source>
        <translation>새 다운로드의 파일 이름 자동 정리</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+586"/>
        <source>Edit...</source>
        <translation>편집...</translation>
    </message>
    <message>
        <location line="-582"/>
        <source>Filename Cleanup Rules</source>
        <translation>파일명 정리 규칙</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Define patterns to automatically clean up filenames of new downloads.
Each rule replaces a regex pattern with a replacement string.</source>
        <translation>새 다운로드의 파일명을 자동으로 정리하는 패턴을 정의합니다.
각 규칙은 정규식 패턴을 대체 문자열로 바꿉니다.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Pattern</source>
        <translation>패턴</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Replacement</source>
        <translation>대체</translation>
    </message>
    <message>
        <location line="+50"/>
        <source>Try to transfer full chunks to all uploads</source>
        <translation>모든 업로드에 전체 청크 전송 시도</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Try to download preview chunks first</source>
        <translation>미리보기 청크를 먼저 다운로드 시도</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Watch clipboard for eD2K links</source>
        <translation>클립보드에서 eD2K 파일 링크 모니터링</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Use advanced calculation method for remaining time</source>
        <translation>남은 시간에 고급 계산 방법 사용</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Start next paused file when a file completes</source>
        <translation>파일 완료 시 다음 일시정지 파일 시작</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Prefer same category</source>
        <translation>같은 카테고리 선호</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Only in same category</source>
        <translation>같은 카테고리만</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Remember downloaded files</source>
        <translation>다운로드한 파일 기억</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Remember cancelled files</source>
        <translation>취소한 파일 기억</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Video Player</source>
        <translation>비디오 플레이어</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Command</source>
        <translation>명령</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Select Video Player</source>
        <translation>비디오 플레이어 선택</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Arguments</source>
        <translation>인수</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Create backup to preview</source>
        <translation>미리보기용 백업 생성</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Pop-up Message</source>
        <translation>팝업 메시지</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>No sound</source>
        <translation>소리 없음</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Test</source>
        <translation>테스트</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Play sound</source>
        <translation>소리 재생</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Speak notification message</source>
        <translation>알림 메시지 읽기</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Select Sound File</source>
        <translation>소리 파일 선택</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Sound Files (*.wav *.mp3 *.ogg);;All Files (*)</source>
        <translation>소리 파일 (*.wav *.mp3 *.ogg);;모든 파일 (*)</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Pop-up when</source>
        <translation>팝업 조건</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Log entry added</source>
        <translation>로그 항목 추가됨</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Chat session started</source>
        <translation>채팅 세션 시작됨</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Chat message received</source>
        <translation>채팅 메시지 수신됨</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Download added</source>
        <translation>다운로드 추가됨</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Download finished (*)</source>
        <translation>다운로드 완료됨 (*)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Urgent: out of disk space, server connection lost (*)</source>
        <translation>긴급: 디스크 공간 부족, 서버 연결 끊김 (*)</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>(*) Email Notifications</source>
        <translation>(*) 이메일 알림</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Enable email notifications</source>
        <translation>이메일 알림 활성화</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>SMTP server...</source>
        <translation>SMTP 서버...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Recipient address:</source>
        <translation>수신자 주소:</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Sender address:</source>
        <translation>발신자 주소:</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>SMTP Server Settings</source>
        <translation>SMTP 서버 설정</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Server:</source>
        <translation>서버:</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>None</source>
        <translation>없음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Plain</source>
        <translation>평문</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Authentication:</source>
        <translation>인증:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Use TLS/STARTTLS</source>
        <translation>TLS/STARTTLS 사용</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Username:</source>
        <translation>사용자 이름:</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Server</source>
        <translation>서버</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Nick</source>
        <translation>닉네임</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Channels</source>
        <translation>채널</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Use channel list filter</source>
        <translation>채널 목록 필터 사용</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Name</source>
        <translation>이름</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Users</source>
        <translation>사용자</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Perform</source>
        <translation>실행</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Use perform string on connect</source>
        <translation>연결 시 perform 문자열 사용</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Connect to help channel</source>
        <translation>도움말 채널에 연결</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Load server channel list on connect</source>
        <translation>연결 시 서버 채널 목록 로드</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Add timestamp to messages</source>
        <translation>메시지에 타임스탬프 추가</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Ignore info messages</source>
        <translation>정보 메시지 무시</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Ignore misc. info messages</source>
        <translation>기타 정보 메시지 무시</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Ignore Join info messages</source>
        <translation>Join 메시지 무시</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Ignore Part info messages</source>
        <translation>Part 메시지 무시</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Ignore Quit info messages</source>
        <translation>Quit 메시지 무시</translation>
    </message>
    <message>
        <location line="+34"/>
        <source>Messages</source>
        <translation>메시지</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Filter messages containing: (Separator | )</source>
        <translation>포함된 메시지 필터: (구분자 | )</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Accept from friends only</source>
        <translation>친구로부터만 수락</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Advanced spam filter</source>
        <translation>고급 스팸 필터</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Require captcha authentication</source>
        <translation>CAPTCHA 인증 요구</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Show smileys</source>
        <translation>이모티콘 표시</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Comments</source>
        <translation>댓글</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Ignore comments containing: (Separator | )</source>
        <translation>포함된 댓글 무시: (구분자 | )</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Indicate downloads with comments/rating by icon</source>
        <translation>댓글/평가가 있는 다운로드를 아이콘으로 표시</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Filter servers too</source>
        <translation>서버도 필터</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Filter level:   &lt;</source>
        <translation>필터 수준:   &lt;</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+388"/>
        <source>Reload</source>
        <translation>다시 로드</translation>
    </message>
    <message>
        <location line="-365"/>
        <source>http://example.com/ipfilter.dat</source>
        <translation>http://example.com/ipfilter.dat</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+19"/>
        <source>Load</source>
        <translation>불러오기</translation>
    </message>
    <message>
        <location line="-13"/>
        <source>Loading...</source>
        <translation>로딩 중...</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Failed to download IP filter: %1</source>
        <translation>IP 필터 다운로드 실패: %1</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Downloaded IP filter is empty.</source>
        <translation>다운로드한 IP 필터가 비어 있습니다.</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Failed to save IP filter: %1</source>
        <translation>IP 필터 저장 실패: %1</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>IP filter updated and reloaded.</source>
        <translation>IP 필터가 업데이트되고 다시 로드되었습니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>IP filter updated and reloaded (unpacked &quot;%1&quot;).</source>
        <translation>IP 필터가 업데이트되고 다시 로드되었습니다(&quot;%1&quot; 압축 해제됨).</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>See My Shared Files/Directories</source>
        <translation>내 공유 파일/디렉터리 보기</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Everybody</source>
        <translation>모든 사람</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Friends only</source>
        <translation>친구만</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nobody</source>
        <translation>아무도</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Protocol Obfuscation</source>
        <translation>프로토콜 난독화</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Enable protocol obfuscation</source>
        <translation>프로토콜 난독화 활성화</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Allow obfuscated connections only (not recommended)</source>
        <translation>난독화 연결만 허용(권장하지 않음)</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Disable support for obfuscated connections</source>
        <translation>난독화 연결 지원 비활성화</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Use secure identification</source>
        <translation>보안 식별 사용</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Run eMule as unprivileged user</source>
        <translation>비특권 사용자로 eMule 실행</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Enable spam filter for search results</source>
        <translation>검색 결과에 스팸 필터 활성화</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Warn when opening untrusted files</source>
        <translation>신뢰할 수 없는 파일 열 때 경고</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Graphs</source>
        <translation>그래프</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Update delay: 3 sec</source>
        <translation>업데이트 지연: 3초</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+153"/>
        <source>Update delay: %1 sec</source>
        <translation>업데이트 지연: %1초</translation>
    </message>
    <message>
        <location line="-152"/>
        <location line="+153"/>
        <source>Update delay: disabled</source>
        <translation>업데이트 지연: 비활성화</translation>
    </message>
    <message>
        <location line="-146"/>
        <source>Time for average graph: 5 mins</source>
        <translation>평균 그래프 시간: 5분</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Time for average graph: %1 mins</source>
        <translation>평균 그래프 시간: %1분</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Colors</source>
        <translation>색상</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Background</source>
        <translation>배경</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Grid</source>
        <translation>격자</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Download Current</source>
        <translation>현재 다운로드</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Download Average</source>
        <translation>다운로드 평균</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Download Session</source>
        <translation>다운로드 세션</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Upload Current</source>
        <translation>현재 업로드</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Upload Average</source>
        <translation>업로드 평균</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Upload Session</source>
        <translation>업로드 세션</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Active Connections</source>
        <translation>활성 연결</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Total Uploads</source>
        <translation>총 업로드</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Active Uploads</source>
        <translation>활성 업로드</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Icon Bar</source>
        <translation>아이콘 바</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Active Downloads</source>
        <translation>활성 다운로드</translation>
    </message>
    <message>
        <location line="-4"/>
        <source>Upload Friend Slots</source>
        <translation>업로드 친구 슬롯</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Upload Slots (no overhead)</source>
        <translation>업로드 슬롯(오버헤드 없음)</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Default</source>
        <translation>기본값</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Restore this colour to the eMule default</source>
        <translation>이 색상을 eMule 기본값으로 되돌립니다</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Auto</source>
        <translation>자동</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Select Color</source>
        <translation>색상 선택</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Draw filled graphs</source>
        <translation>채워진 그래프 그리기</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Connections statistics Y-axis scale:</source>
        <translation>연결 통계 Y축 스케일:</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Active connections ratio:</source>
        <translation>활성 연결 비율:</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Statistics Tree</source>
        <translation>통계 트리</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Update delay: 5 sec</source>
        <translation>업데이트 지연: 5초</translation>
    </message>
    <message>
        <location line="+40"/>
        <source>Enable REST API</source>
        <translation>REST API 활성화</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Gzip compression</source>
        <translation>Gzip 압축</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Include port into UPnP setup</source>
        <translation>UPnP 설정에 포트 포함</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Template:</source>
        <translation>템플릿:</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+37"/>
        <location line="+10"/>
        <source>...</source>
        <translation>...</translation>
    </message>
    <message>
        <location line="-33"/>
        <source>Session Time out:</source>
        <translation>세션 시간 초과:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>minutes</source>
        <translation>분</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Use HTTPS</source>
        <translation>HTTPS 사용</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Create new certificate</source>
        <translation>새 인증서 생성</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Certificate:</source>
        <translation>인증서:</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Key:</source>
        <translation>키:</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>REST API Key:</source>
        <translation>REST API 키:</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Administrator</source>
        <translation>관리자</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Allow exit eMule, reboot and shutdown</source>
        <translation>eMule 종료, 재부팅 및 종료 허용</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Guest</source>
        <translation>게스트</translation>
    </message>
    <message>
        <location line="+40"/>
        <source>Select Template File</source>
        <translation>템플릿 파일 선택</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Template files (*.tmpl);;All files (*)</source>
        <translation>템플릿 파일 (*.tmpl);;모든 파일 (*)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Select Certificate File</source>
        <translation>인증서 파일 선택</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>PEM files (*.pem *.crt);;All files (*)</source>
        <translation>PEM 파일 (*.pem *.crt);;모든 파일 (*)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Select Key File</source>
        <translation>키 파일 선택</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>PEM files (*.pem *.key);;All files (*)</source>
        <translation>PEM 파일 (*.pem *.key);;모든 파일 (*)</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Save Certificate</source>
        <translation>인증서 저장</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>PEM files (*.pem)</source>
        <translation>PEM 파일 (*.pem)</translation>
    </message>
    <message>
        <location line="+54"/>
        <source>Warning: Do not change these settings unless you know what you are doing. Otherwise you can easily make things worse for yourself. eMule will run fine without adjusting any of these settings.</source>
        <translation>경고: 무엇을 하고 있는지 알지 못하는 한 이 설정을 변경하지 마세요. 그렇지 않으면 상황을 쉽게 악화시킬 수 있습니다. eMule은 이 설정을 조정하지 않아도 정상적으로 작동합니다.</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>TCP/IP connections</source>
        <translation>TCP/IP 연결</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Max. new connections / 5 secs.:</source>
        <translation>5초당 최대 새 연결 수:</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Max. half-open connections:</source>
        <translation>최대 반개방 연결 수:</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Server connection refresh interval [min.]:</source>
        <translation>서버 연결 갱신 간격 [분]:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Disabled</source>
        <translation>비활성화됨</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Autotake eD2K links only during runtime</source>
        <translation>실행 중에만 eD2K 링크 자동 수락</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Use credit system (reward uploaders)</source>
        <translation>크레딧 시스템 사용(업로더 보상)</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Remember the upload queue between restarts</source>
        <translation>재시작 후에도 업로드 대기열 기억</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Stores the longest-waiting clients in your upload queue and puts them back, with the places they had earned, when eMule starts again. They are not contacted on startup — they simply wait their turn as usual.</source>
        <translation>업로드 대기열에서 가장 오래 기다린 클라이언트를 저장했다가 eMule을 다시 시작할 때 얻었던 순번 그대로 되돌려 놓습니다. 시작할 때 해당 클라이언트에 접속하지는 않습니다 — 평소처럼 자기 차례를 기다릴 뿐입니다.</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Open/close ports on WinXP firewall when starting/exiting eMule</source>
        <translation>eMule 시작/종료 시 WinXP 방화벽 포트 열기/닫기</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Filter server and client LAN IPs</source>
        <translation>서버 및 클라이언트 LAN IP 필터</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show more controls (advanced mode controls)</source>
        <translation>더 많은 컨트롤 표시(고급 모드)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Disable A4AF checks to save CPU</source>
        <translation>CPU 절약을 위한 A4AF 확인 비활성화</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Disable automatic archive preview start in file details</source>
        <translation>파일 상세에서 자동 아카이브 미리보기 비활성화</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Host name for own eD2K links:</source>
        <translation>자체 eD2K 링크의 호스트 이름:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>A DNS name or an IPv6 literal</source>
        <translation>DNS 이름 또는 IPv6 리터럴</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Add own IPv6 address to eD2K links</source>
        <translation>eD2K 링크에 자신의 IPv6 주소 추가</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Only when a public IPv6 address is confirmed. Legacy clients ignore it.</source>
        <translation>공용 IPv6 주소가 확인된 경우에만 적용됩니다. 구형 클라이언트는 이를 무시합니다.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Create new part files as &apos;sparse&apos; (NTFS only)</source>
        <translation>새 part 파일을 &apos;sparse&apos;로 생성(NTFS만)</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Allocate full file size for non-sparse part files</source>
        <translation>비-sparse part 파일에 전체 파일 크기 할당</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Check disk space</source>
        <translation>디스크 공간 확인</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Min. free disk space [MB]:</source>
        <translation>최소 여유 디스크 공간 [MB]:</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Safe .met/.dat file writing</source>
        <translation>안전한 .met/.dat 파일 쓰기</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+15"/>
        <source>Never</source>
        <translation>안 함</translation>
    </message>
    <message>
        <location line="-14"/>
        <source>On shutdown</source>
        <translation>종료 시</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Always</source>
        <translation>항상</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Extract meta data</source>
        <translation>메타데이터 추출</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>MediaInfo Library</source>
        <translation>MediaInfo 라이브러리</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Resolve shell links in shared directories</source>
        <translation>공유 디렉터리의 셸 링크 해석</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Verbose (additional program feedback)</source>
        <translation>상세(추가 프로그램 피드백)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Log level:</source>
        <translation>로그 수준:</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Log client source exchange and server source queries/answers</source>
        <translation>클라이언트 소스 교환 및 서버 소스 쿼리/응답 기록</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Log banned clients</source>
        <translation>차단된 클라이언트 기록</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Log received file descriptions and ratings</source>
        <translation>수신된 파일 설명 및 평가 기록</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Log secure ident</source>
        <translation>보안 식별 기록</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Log filtered and/or ignored IPs</source>
        <translation>필터링 및/또는 무시된 IP 기록</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Log file save actions</source>
        <translation>파일 저장 작업 기록</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Log A4AF actions</source>
        <translation>A4AF 작업 기록</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Log upload/download events</source>
        <translation>업로드/다운로드 이벤트 기록</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Log raw socket packets</source>
        <translation>원시 소켓 패킷 기록</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Upload SpeedSense (not recommended)</source>
        <translation>업로드 속도 감지(권장하지 않음)</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Find best upload limit automatically</source>
        <translation>최적의 업로드 제한을 자동으로 찾기</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Ping tolerance (% of lowest ping):</source>
        <translation>Ping 허용 범위(최소 ping의 %):</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Ping tolerance (ms):</source>
        <translation>Ping 허용 범위(ms):</translation>
    </message>
    <message>
        <location line="+3"/>
        <source> ms</source>
        <translation> ms</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Method for ping tolerance:</source>
        <translation>Ping 허용 범위 방법:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Percent (%)</source>
        <translation>퍼센트 (%)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Milliseconds (ms)</source>
        <translation>밀리초 (ms)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Going up slowness:</source>
        <translation>상승 속도:</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Going down slowness:</source>
        <translation>하강 속도:</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Max number of pings for average:</source>
        <translation>평균을 위한 최대 ping 수:</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>UPnP</source>
        <translation>UPnP</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Remove UPnP port forwarding on exit</source>
        <translation>종료 시 UPnP 포트 포워딩 제거</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Sharing eMule with other computer users</source>
        <translation>다른 컴퓨터 사용자와 eMule 공유</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Each user has its own configuration and downloads</source>
        <translation>각 사용자가 자체 구성 및 다운로드 보유</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Everyone has the same configuration and downloads</source>
        <translation>모든 사용자가 동일한 구성 및 다운로드 공유</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Store config and downloads in the program directory</source>
        <translation>프로그램 디렉터리에 구성 및 다운로드 저장</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>File buffer size: %1 MB</source>
        <translation>파일 버퍼 크기: %1 MB</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Queue size: %1</source>
        <translation>대기열 크기: %1</translation>
    </message>
    <message>
        <location line="-1536"/>
        <location line="+1579"/>
        <location line="+279"/>
        <source>Remove</source>
        <translation>제거</translation>
    </message>
    <message>
        <location line="-1647"/>
        <source>New eMule Qt version detected</source>
        <translation>새 eMule Qt 버전이 감지되었습니다</translation>
    </message>
    <message>
        <location line="+356"/>
        <source>Update from URL: (filter.dat- or PeerGuardian-format, .gz/.zip accepted)</source>
        <translation>URL에서 업데이트: (filter.dat 또는 PeerGuardian 형식, .gz/.zip 허용)</translation>
    </message>
    <message>
        <location line="+726"/>
        <source>Write eMule core logs to disk</source>
        <translation>eMule 코어 로그를 디스크에 기록</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Write eMule GUI logs to disk</source>
        <translation>eMule GUI 로그를 디스크에 기록</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Log server connection &amp;&amp; search details (TCP/UDP handshake)</source>
        <translation>서버 연결 &amp;&amp; 검색 세부 정보 기록(TCP/UDP 핸드셰이크)</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Log web server requests</source>
        <translation>웹 서버 요청 기록</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Log public IP address on startup</source>
        <translation>시작 시 공용 IP 주소 기록</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Enable IPC log tab</source>
        <translation>IPC 로그 탭 활성화</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Start core with console (debug)</source>
        <translation>콘솔과 함께 코어 시작(디버그)</translation>
    </message>
    <message>
        <location line="+89"/>
        <source>PCP (RFC 6887) — preferred, supports IPv6</source>
        <translation>PCP (RFC 6887) — 권장, IPv6 지원</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>NAT-PMP (RFC 6886) — IPv4 only</source>
        <translation>NAT-PMP (RFC 6886) — IPv4 전용</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>UPnP IGD — fallback</source>
        <translation>UPnP IGD — 대체 수단</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Open IPv6 firewall pinholes</source>
        <translation>IPv6 방화벽 핀홀 열기</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Requested lease:</source>
        <translation>요청 임대 시간:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source> s</source>
        <translation> 초</translation>
    </message>
    <message>
        <location line="+114"/>
        <source>New</source>
        <translation>새로 만들기</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+18"/>
        <source>Title</source>
        <translation>제목</translation>
    </message>
    <message>
        <location line="-18"/>
        <source>Days</source>
        <translation>일</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Start Time</source>
        <translation>시작 시간</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Details</source>
        <translation>상세</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Time</source>
        <translation>시간</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+111"/>
        <source>Daily</source>
        <translation>매일</translation>
    </message>
    <message>
        <location line="-111"/>
        <source>Monday</source>
        <translation>월요일</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Tuesday</source>
        <translation>화요일</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Wednesday</source>
        <translation>수요일</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Thursday</source>
        <translation>목요일</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Friday</source>
        <translation>금요일</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Saturday</source>
        <translation>토요일</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Sunday</source>
        <translation>일요일</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Mon-Fri</source>
        <translation>월~금</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Mon-Sat</source>
        <translation>월~토</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Sat-Sun</source>
        <translation>토~일</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>No end time</source>
        <translation>종료 시간 없음</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+4"/>
        <source>Action</source>
        <translation>동작</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Value</source>
        <translation>값</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>New Schedule</source>
        <translation>새 일정</translation>
    </message>
    <message>
        <location line="-1673"/>
        <location line="+1834"/>
        <source>Add</source>
        <translation>추가</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Action Value</source>
        <translation>동작 값</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+15"/>
        <source>Enter value:</source>
        <translation>값 입력:</translation>
    </message>
    <message>
        <location line="-3"/>
        <location line="+2"/>
        <source>Edit Value</source>
        <translation>값 편집</translation>
    </message>
    <message>
        <location line="+93"/>
        <source>The %1 settings page is not yet implemented.</source>
        <translation>설정 페이지 %1은(는) 아직 구현되지 않았습니다.</translation>
    </message>
    <message>
        <location line="+157"/>
        <source>Proxy</source>
        <translation>프록시</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Proxy settings will only apply to new connections.
Restart eMule for all connections to use the new proxy settings.</source>
        <translation>프록시 설정은 새 연결에만 적용됩니다.
모든 연결에서 새 프록시 설정을 사용하려면 eMule을 재시작하세요.</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>The language change will take effect after restarting the application.</source>
        <translation>언어 변경은 애플리케이션을 재시작한 후 적용됩니다.</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Core connection settings will take effect after restarting the application.</source>
        <translation>코어 연결 설정은 애플리케이션을 재시작한 후 적용됩니다.</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Icons</source>
        <translation>아이콘</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The icon change will take effect after restarting the application.</source>
        <translation>아이콘 변경은 애플리케이션을 재시작한 후 적용됩니다.</translation>
    </message>
</context>
<context>
    <name>eMule::PasteLinksDialog</name>
    <message>
        <location filename="../src/gui/dialogs/PasteLinksDialog.cpp" line="+21"/>
        <source>Paste eD2K Links</source>
        <translation>eD2K 링크 붙여넣기</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>eD2K Links:</source>
        <translation>eD2K 링크:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Paste one or more ed2k:// links here, one per line...</source>
        <translation>여기에 ed2k:// 링크를 한 줄에 하나씩 붙여넣으세요...</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Download</source>
        <translation>다운로드</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Cancel</source>
        <translation>취소</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Not Connected</source>
        <translation>연결되지 않음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not connected to the daemon.</source>
        <translation>데몬에 연결되지 않았습니다.</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Invalid Links</source>
        <translation>잘못된 링크</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The following links could not be parsed:

%1</source>
        <translation>다음 링크를 분석할 수 없습니다:

%1</translation>
    </message>
</context>
<context>
    <name>eMule::SearchDetailDialog</name>
    <message>
        <location filename="../src/gui/dialogs/SearchDetailDialog.cpp" line="+42"/>
        <source>Details: %1</source>
        <translation>상세: %1</translation>
    </message>
    <message>
        <location line="+19"/>
        <location line="+2"/>
        <source>Metadata</source>
        <translation>메타데이터</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+2"/>
        <source>Comments</source>
        <translation>댓글</translation>
    </message>
</context>
<context>
    <name>eMule::SearchPanel</name>
    <message>
        <location filename="../src/gui/panels/SearchPanel.cpp" line="+194"/>
        <location line="+416"/>
        <source>Download</source>
        <translation>다운로드</translation>
    </message>
    <message>
        <location line="-404"/>
        <source>Close All Searches</source>
        <translation>모든 검색 닫기</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Name:</source>
        <translation>이름:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Enter search keywords...</source>
        <translation>검색 키워드 입력...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Type:</source>
        <translation>유형:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Any</source>
        <translation>모든</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Audio</source>
        <translation>오디오</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Video</source>
        <translation>비디오</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Image</source>
        <translation>이미지</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Document</source>
        <translation>문서</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Program</source>
        <translation>프로그램</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Archive</source>
        <translation>압축 파일</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>CD-Image</source>
        <translation>CD 이미지</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Collection</source>
        <translation>컬렉션</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Method:</source>
        <translation>방법:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Automatic</source>
        <translation>자동</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Kad Network</source>
        <translation>Kad 네트워크</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ed2k Server</source>
        <translation>Ed2k 서버</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ed2k Global</source>
        <translation>Ed2k 글로벌</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Reset</source>
        <translation>초기화</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Min. Size [MB]:</source>
        <translation>최소 크기 [MB]:</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Max. Size [MB]:</source>
        <translation>최대 크기 [MB]:</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Availability:</source>
        <translation>가용성:</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Complete Sources:</source>
        <translation>완전한 소스:</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Extension:</source>
        <translation>확장자:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Codec:</source>
        <translation>코덱:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Min. Bitrate [kbps]:</source>
        <translation>최소 비트레이트 [kbps]:</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Min. Length [s]:</source>
        <translation>최소 길이 [초]:</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Title:</source>
        <translation>제목:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Album:</source>
        <translation>앨범:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Artist:</source>
        <translation>아티스트:</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Start</source>
        <translation>시작</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Cancel</source>
        <translation>취소</translation>
    </message>
    <message>
        <location line="+32"/>
        <location line="+20"/>
        <source>Not connected to daemon — search cannot be started.</source>
        <translation>데몬에 연결되어 있지 않습니다 — 검색을 시작할 수 없습니다.</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Search</source>
        <translation>검색</translation>
    </message>
    <message>
        <location line="+47"/>
        <source>Kad: &quot;%1&quot; is already being searched — using &quot;%2&quot; as the search target.</source>
        <translation>Kad: &quot;%1&quot;은(는) 이미 검색 중입니다 — &quot;%2&quot;을(를) 검색 대상으로 사용합니다.</translation>
    </message>
    <message>
        <location line="+147"/>
        <source>Details...</source>
        <translation>상세...</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Comments...</source>
        <translation>댓글...</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Copy eD2K Links</source>
        <translation>eD2K 링크 복사</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Copy eD2K Links (HTML)</source>
        <translation>eD2K 링크 복사 (HTML)</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Mark as not Spam</source>
        <translation>스팸 아님으로 표시</translation>
    </message>
    <message>
        <location line="+61"/>
        <location line="+502"/>
        <source>Preview</source>
        <translation>미리보기</translation>
    </message>
    <message>
        <location line="+186"/>
        <source>Asking servers: %1 / %2</source>
        <translation>서버 조회 중: %1 / %2</translation>
    </message>
    <message>
        <location line="-749"/>
        <location line="+14"/>
        <source>Mark as Spam</source>
        <translation>스팸으로 표시</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Remove</source>
        <translation>제거</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Close Search Results</source>
        <translation>검색 결과 닫기</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Close All Search Results</source>
        <translation>모든 검색 결과 닫기</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Find...</source>
        <translation>찾기...</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Search Related Files</source>
        <translation>관련 파일 검색</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Web Services</source>
        <translation>웹 서비스</translation>
    </message>
    <message>
        <location line="+481"/>
        <source>Preview not available — web server is not running or stream token not received.</source>
        <translation>미리보기를 사용할 수 없습니다 — 웹 서버가 실행 중이 아니거나 스트림 토큰을 받지 못했습니다.</translation>
    </message>
</context>
<context>
    <name>eMule::SearchResultsModel</name>
    <message>
        <location filename="../src/gui/controls/SearchResultsModel.cpp" line="+116"/>
        <source>File Name</source>
        <translation>파일 이름</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Size</source>
        <translation>크기</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Availability</source>
        <translation>가용성</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Complete Sources</source>
        <translation>완전한 소스</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type</source>
        <translation>유형</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Artist</source>
        <translation>아티스트</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Album</source>
        <translation>앨범</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Title</source>
        <translation>제목</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Length</source>
        <translation>길이</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bitrate</source>
        <translation>비트레이트</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Codec</source>
        <translation>코덱</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Known</source>
        <translation>알려진</translation>
    </message>
</context>
<context>
    <name>eMule::ServerListModel</name>
    <message>
        <location filename="../src/gui/controls/ServerListModel.cpp" line="+31"/>
        <location line="+3"/>
        <source>Yes</source>
        <translation>예</translation>
    </message>
    <message>
        <location line="-3"/>
        <location line="+3"/>
        <source>No</source>
        <translation>아니오</translation>
    </message>
    <message>
        <location line="+52"/>
        <source>Server Name</source>
        <translation>서버 이름</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>IP</source>
        <translation>IP</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Description</source>
        <translation>설명</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ping</source>
        <translation>Ping</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Users</source>
        <translation>사용자</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Max Users</source>
        <translation>최대 사용자</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Preference</source>
        <translation>환경 설정</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Failed</source>
        <translation>실패</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Static</source>
        <translation>고정</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Soft Files</source>
        <translation>소프트 파일</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Low ID</source>
        <translation>Low ID</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Obfuscation</source>
        <translation>난독화</translation>
    </message>
</context>
<context>
    <name>eMule::ServerPanel</name>
    <message>
        <location filename="../src/gui/panels/ServerPanel.cpp" line="+135"/>
        <location line="+94"/>
        <location line="+2"/>
        <source>Disconnect</source>
        <translation>연결 해제</translation>
    </message>
    <message>
        <location line="-96"/>
        <location line="+7"/>
        <location line="+113"/>
        <location line="+48"/>
        <source>Cancel</source>
        <translation>취소</translation>
    </message>
    <message>
        <location line="-165"/>
        <location line="+95"/>
        <location line="+438"/>
        <source>Connect</source>
        <translation>연결</translation>
    </message>
    <message>
        <location line="-488"/>
        <source>Invalid URL: %1</source>
        <translation>잘못된 URL: %1</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Downloading server.met from %1 ...</source>
        <translation>%1에서 server.met 다운로드 중...</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Failed to download server.met: %1</source>
        <translation>server.met 다운로드 실패: %1</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Downloaded empty server.met file.</source>
        <translation>다운로드한 server.met 파일이 비어 있습니다.</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Downloaded server.met (%1 bytes). Parsing...</source>
        <translation>server.met 다운로드 완료 (%1바이트). 분석 중...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloaded server.met, unpacked &quot;%1&quot; (%2 bytes). Parsing...</source>
        <translation>server.met을 다운로드하고 &quot;%1&quot;의 압축을 풀었습니다(%2바이트). 분석 중...</translation>
    </message>
    <message>
        <location line="+584"/>
        <location line="+2"/>
        <location line="+24"/>
        <location line="+39"/>
        <source>IP:Port:</source>
        <translation>IP:포트:</translation>
    </message>
    <message>
        <location line="-65"/>
        <source>Unknown</source>
        <translation>알 수 없음</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+64"/>
        <source>ID:</source>
        <translation>ID:</translation>
    </message>
    <message>
        <location line="-48"/>
        <source>eD2K Server</source>
        <translation>eD2K 서버</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Description:</source>
        <translation>설명:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Version:</source>
        <translation>버전:</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+48"/>
        <source>Users:</source>
        <translation>사용자:</translation>
    </message>
    <message>
        <location line="-47"/>
        <location line="+49"/>
        <source>Files:</source>
        <translation>파일:</translation>
    </message>
    <message>
        <location line="-48"/>
        <source>Connection:</source>
        <translation>연결:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Obfuscated</source>
        <translation>난독화됨</translation>
    </message>
    <message>
        <location line="+18"/>
        <location line="+7"/>
        <source>Open</source>
        <translation>개방</translation>
    </message>
    <message>
        <location line="-3"/>
        <location line="+6"/>
        <source>UDP Status:</source>
        <translation>UDP 상태:</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>unverified</source>
        <translation>미확인</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Extern UDP Port:</source>
        <translation>외부 UDP 포트:</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Web Interface</source>
        <translation>웹 인터페이스</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Enabled</source>
        <translation>활성화됨</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Disabled</source>
        <translation>비활성화됨</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>▸ Servers (%1)</source>
        <translation>▸ 서버 (%1)</translation>
    </message>
    <message>
        <location line="-622"/>
        <source>Connect To</source>
        <translation>연결 대상</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Priority</source>
        <translation>우선순위</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Low</source>
        <translation>낮음</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+511"/>
        <source>Normal</source>
        <translation>보통</translation>
    </message>
    <message>
        <location line="-510"/>
        <source>High</source>
        <translation>높음</translation>
    </message>
    <message>
        <location line="+93"/>
        <source>Move Up</source>
        <translation>위로 이동</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Move Down</source>
        <translation>아래로 이동</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Add To Static List</source>
        <translation>고정 목록에 추가</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Remove From Static List</source>
        <translation>고정 목록에서 제거</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Copy eD2K Links</source>
        <translation>eD2K 링크 복사</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Paste eD2K Links</source>
        <translation>eD2K 링크 붙여넣기</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Remove</source>
        <translation>제거</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Remove All</source>
        <translation>모두 제거</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Find...</source>
        <translation>찾기...</translation>
    </message>
    <message>
        <location line="+52"/>
        <source>▸ Servers (0)</source>
        <translation>▸ 서버 (0)</translation>
    </message>
    <message>
        <location line="+64"/>
        <source>New Server</source>
        <translation>새 서버</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>IP Address:</source>
        <translation>IP 주소:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Port:</source>
        <translation>포트:</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+120"/>
        <source>Name:</source>
        <translation>이름:</translation>
    </message>
    <message>
        <location line="-114"/>
        <source>Add to list</source>
        <translation>목록에 추가</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Update server.met from URL</source>
        <translation>URL에서 server.met 업데이트</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Update server.met from URL:</source>
        <translation>URL에서 server.met 업데이트:</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Update</source>
        <translation>업데이트</translation>
    </message>
    <message>
        <location line="+46"/>
        <source>My Info</source>
        <translation>내 정보</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>eD2K Network</source>
        <translation>eD2K 네트워크</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+43"/>
        <location line="+2"/>
        <location line="+9"/>
        <location line="+3"/>
        <location line="+32"/>
        <location line="+4"/>
        <location line="+5"/>
        <source>Status:</source>
        <translation>상태:</translation>
    </message>
    <message>
        <location line="-95"/>
        <source>Connected</source>
        <translation>연결됨</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>High ID</source>
        <translation>High ID</translation>
    </message>
    <message>
        <location line="+25"/>
        <location line="+47"/>
        <source>Connecting...</source>
        <translation>연결 중...</translation>
    </message>
    <message>
        <location line="-45"/>
        <location line="+48"/>
        <source>Disconnected</source>
        <translation>연결 해제됨</translation>
    </message>
    <message>
        <location line="-44"/>
        <source>Kad Network</source>
        <translation>Kad 네트워크</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+8"/>
        <source>Firewalled</source>
        <translation>방화벽 뒤</translation>
    </message>
    <message>
        <location line="-46"/>
        <source>Low ID</source>
        <translation>Low ID</translation>
    </message>
    <message>
        <location line="+277"/>
        <source>Invalid server.met header: 0x%1</source>
        <translation>잘못된 server.met 헤더: 0x%1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Server count too large: %1</source>
        <translation>서버 수가 너무 많음: %1</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Corrupt server.met: tag count %1 at server %2</source>
        <translation>손상된 server.met: 서버 %2의 태그 수 %1</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Corrupt server.met: truncated tag name</source>
        <translation>손상된 server.met: 잘린 태그 이름</translation>
    </message>
    <message>
        <location line="+52"/>
        <source>Corrupt server.met: truncated hash tag</source>
        <translation>손상된 server.met: 해시 태그가 잘렸습니다</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>Unknown tag type 0x%1 at server %2, stopping parse</source>
        <translation>서버 %2에서 알 수 없는 태그 유형 0x%1, 분석 중단</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>server.met processed: %1 servers added, %2 skipped (duplicates/invalid).</source>
        <translation>server.met 처리 완료: %1개 서버 추가, %2개 건너뜀 (중복/잘못됨).</translation>
    </message>
</context>
<context>
    <name>eMule::SharedFilesModel</name>
    <message>
        <location filename="../src/gui/controls/SharedFilesModel.cpp" line="+194"/>
        <source>File Name</source>
        <translation>파일 이름</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Size</source>
        <translation>크기</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type</source>
        <translation>유형</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority</source>
        <translation>우선순위</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Requests</source>
        <translation>요청</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Transferred Data</source>
        <translation>전송된 데이터</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Shared parts</source>
        <translation>공유 파트</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Complete Sources</source>
        <translation>완전한 소스</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Shared eD2K/Kad</source>
        <translation>공유 eD2K/Kad</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Folder</source>
        <translation>폴더</translation>
    </message>
</context>
<context>
    <name>eMule::SharedFilesPanel</name>
    <message>
        <location filename="../src/gui/panels/SharedFilesPanel.cpp" line="+154"/>
        <location line="+12"/>
        <location line="+386"/>
        <location line="+362"/>
        <location line="+98"/>
        <source>Shared Files (0)</source>
        <translation>공유 파일 (0)</translation>
    </message>
    <message>
        <location line="-754"/>
        <source>Open File</source>
        <translation>파일 열기</translation>
    </message>
    <message>
        <location line="+13"/>
        <location line="+1302"/>
        <source>Open Folder</source>
        <translation>폴더 열기</translation>
    </message>
    <message>
        <location line="-1290"/>
        <source>Rename...</source>
        <translation>이름 바꾸기...</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Rename File</source>
        <translation>파일 이름 변경</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>New file name:</source>
        <translation>새 파일 이름:</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Delete From Disk</source>
        <translation>디스크에서 삭제</translation>
    </message>
    <message>
        <location line="+792"/>
        <source>Delete File</source>
        <translation>파일 삭제</translation>
    </message>
    <message>
        <location line="-6"/>
        <source>Are you sure you want to permanently delete &quot;%1&quot; from disk?</source>
        <translation>&quot;%1&quot;을(를) 디스크에서 영구적으로 삭제하시겠습니까?</translation>
    </message>
    <message>
        <location line="-771"/>
        <source>Unshare</source>
        <translation>공유 해제</translation>
    </message>
    <message>
        <location line="+804"/>
        <source>Unshare File</source>
        <translation>파일 공유 해제</translation>
    </message>
    <message>
        <location line="-6"/>
        <source>Remove &quot;%1&quot; from the shared files list?

The file will remain on disk.</source>
        <translation>공유 파일 목록에서 &quot;%1&quot;을(를) 제거하시겠습니까?

파일은 디스크에 남아 있습니다.</translation>
    </message>
    <message>
        <location line="-787"/>
        <source>Priority (Upload)</source>
        <translation>우선순위(업로드)</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Very Low</source>
        <translation>매우 낮음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Low</source>
        <translation>낮음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Normal</source>
        <translation>보통</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>High</source>
        <translation>높음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Very High</source>
        <translation>매우 높음</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Auto</source>
        <translation>자동</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Collection</source>
        <translation>컬렉션</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Create Collection...</source>
        <translation>컬렉션 만들기...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Modify Collection...</source>
        <translation>컬렉션 수정...</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>View Collection...</source>
        <translation>컬렉션 보기...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Search Author&apos;s Collections...</source>
        <translation>작성자의 컬렉션 검색...</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+6"/>
        <source>Search Author&apos;s Collections</source>
        <translation>작성자의 컬렉션 검색</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>This collection carries no author key, so its author&apos;s other collections cannot be looked up.</source>
        <translation>이 컬렉션에는 작성자 키가 없으므로 해당 작성자의 다른 컬렉션을 조회할 수 없습니다.</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Details...</source>
        <translation>상세...</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Comments...</source>
        <translation>댓글...</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>eD2K Links...</source>
        <translation>eD2K 링크...</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Find...</source>
        <translation>찾기...</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Web Services</source>
        <translation>웹 서비스</translation>
    </message>
    <message>
        <location line="+72"/>
        <source>Reload</source>
        <translation>다시 로드</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>File Name</source>
        <translation>파일 이름</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>All Shared Files</source>
        <translation>모든 공유 파일</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Incoming Files</source>
        <translation>수신 파일</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Incomplete Files</source>
        <translation>불완전한 파일</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Shared Directories</source>
        <translation>공유 디렉터리</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>All Directories</source>
        <translation>모든 디렉터리</translation>
    </message>
    <message>
        <location line="+130"/>
        <source>Current Session</source>
        <translation>현재 세션</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+45"/>
        <source>Popularity Rank:</source>
        <translation>인기 순위:</translation>
    </message>
    <message>
        <location line="-39"/>
        <location line="+45"/>
        <source>  Requests:</source>
        <translation>  요청 수:</translation>
    </message>
    <message>
        <location line="-38"/>
        <source>On Queue:</source>
        <translation>대기열:</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+40"/>
        <source>  Accepted Uploads:</source>
        <translation>  수락된 업로드:</translation>
    </message>
    <message>
        <location line="-33"/>
        <source>Uploading:</source>
        <translation>업로드 중:</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+35"/>
        <source>  Transferred:</source>
        <translation>  전송됨:</translation>
    </message>
    <message>
        <location line="-27"/>
        <source>Total</source>
        <translation>합계</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Statistics</source>
        <translation>통계</translation>
    </message>
    <message>
        <location line="+227"/>
        <source>%1 (%2 of %3 shared)</source>
        <translation>%1 (%3개 중 %2개 공유됨)</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Could not share that file</source>
        <translation>해당 파일을 공유할 수 없습니다</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Could not unshare that file</source>
        <translation>해당 파일의 공유를 해제할 수 없습니다</translation>
    </message>
    <message>
        <location line="+518"/>
        <source>Share Directory</source>
        <translation>디렉터리 공유</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Share with Subdirectories</source>
        <translation>하위 디렉터리 포함 공유</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Unshare Directory</source>
        <translation>디렉터리 공유 해제</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Unshare with Subdirectories</source>
        <translation>하위 디렉터리 포함 공유 해제</translation>
    </message>
    <message>
        <location line="+69"/>
        <source>Open File not available — web server is not running or stream token not received.</source>
        <translation>파일 열기를 사용할 수 없습니다 — 웹 서버가 실행 중이 아니거나 스트림 토큰을 받지 못했습니다.</translation>
    </message>
    <message>
        <location line="-861"/>
        <source>Content</source>
        <translation>콘텐츠</translation>
    </message>
    <message>
        <location line="+58"/>
        <source>eD2K Links</source>
        <translation>eD2K 링크</translation>
    </message>
    <message>
        <location line="-18"/>
        <source>Copy</source>
        <translation>복사</translation>
    </message>
    <message>
        <location line="-26"/>
        <source>Basic Options</source>
        <translation>기본 옵션</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Add Source</source>
        <translation>소스 추가</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Not available (requires public IP and open firewall)</source>
        <translation>사용할 수 없음(공용 IP와 열린 방화벽 필요)</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Advanced Options</source>
        <translation>고급 옵션</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Add HTML</source>
        <translation>HTML 추가</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add Hashset</source>
        <translation>해시셋 추가</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hostname</source>
        <translation>호스트 이름</translation>
    </message>
    <message>
        <location line="+24"/>
        <location line="+405"/>
        <source>Requires a hostname configured in Preferences, or a public IPv6</source>
        <translation>환경 설정에 구성된 호스트 이름 또는 공용 IPv6가 필요합니다</translation>
    </message>
    <message>
        <location line="-294"/>
        <source>Shared Files (%1)</source>
        <translation>공유 파일 (%1)</translation>
    </message>
    <message numerus="yes">
        <location line="+107"/>
        <source>Are you sure you want to permanently delete %n selected file(s) from disk?</source>
        <translation>
            <numerusform>선택한 파일 %n개를 디스크에서 영구적으로 삭제하시겠습니까?</numerusform>
        </translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Delete Files</source>
        <translation>파일 삭제</translation>
    </message>
    <message numerus="yes">
        <location line="+5"/>
        <source>Deleting %n shared file(s) from disk</source>
        <translation>
            <numerusform>공유 파일 %n개를 디스크에서 삭제하는 중</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="+18"/>
        <source>Remove %n selected file(s) from the shared files list?

The files will remain on disk.</source>
        <translation>
            <numerusform>선택한 파일 %n개를 공유 파일 목록에서 제거하시겠습니까?

파일은 디스크에 그대로 남습니다.</numerusform>
        </translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Unshare Files</source>
        <translation>파일 공유 해제</translation>
    </message>
    <message>
        <location line="+155"/>
        <source>Add your hostname or public IPv6 as a source</source>
        <translation>자신의 호스트 이름 또는 공용 IPv6를 소스로 추가</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Showing eD2K links for the first %1 of %2 selected files.</source>
        <translation>선택한 %2개 파일 중 처음 %1개의 eD2K 링크를 표시합니다.</translation>
    </message>
</context>
<context>
    <name>eMule::StatisticsPanel</name>
    <message>
        <location filename="../src/gui/panels/StatisticsPanel.cpp" line="-1047"/>
        <location line="+7"/>
        <source>Session average</source>
        <translation>세션 평균</translation>
    </message>
    <message>
        <location line="-6"/>
        <location line="+7"/>
        <source>Average (3 min)</source>
        <translation>평균 (3분)</translation>
    </message>
    <message>
        <location line="-6"/>
        <location line="+7"/>
        <source>Current</source>
        <translation>현재</translation>
    </message>
    <message>
        <location line="-6"/>
        <location line="+9"/>
        <source>KB/s</source>
        <translation>KB/s</translation>
    </message>
    <message>
        <location line="-2"/>
        <source>Current (excl. overhead)</source>
        <translation>현재(오버헤드 제외)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Friend slots</source>
        <translation>친구 슬롯</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Active connections</source>
        <translation>활성 연결</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Active uploads</source>
        <translation>활성 업로드</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total uploads</source>
        <translation>총 업로드</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Active downloads</source>
        <translation>활성 다운로드</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Transfer</source>
        <translation>전송</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Session UL:DL Ratio: -</source>
        <translation>세션 UL:DL 비율: -</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Friend Session UL:DL Ratio: -</source>
        <translation>친구 세션 UL:DL 비율: -</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Cumulative UL:DL Ratio: -</source>
        <translation>누적 UL:DL 비율: -</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+176"/>
        <location line="+19"/>
        <source>Uploads</source>
        <translation>업로드</translation>
    </message>
    <message>
        <location line="-191"/>
        <location line="+63"/>
        <location line="+78"/>
        <location line="+21"/>
        <location line="+47"/>
        <source>Session</source>
        <translation>세션</translation>
    </message>
    <message>
        <location line="-206"/>
        <location line="+32"/>
        <source>Uploaded Data: 0 Bytes</source>
        <translation>업로드된 데이터: 0 Bytes</translation>
    </message>
    <message>
        <location line="-19"/>
        <source>Uploaded Data to Friends: 0 Bytes</source>
        <translation>친구에게 업로드된 데이터: 0 Bytes</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Active Uploads: 0</source>
        <translation>활성 업로드: 0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Waiting Uploads: 0</source>
        <translation>대기 중 업로드: 0</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+27"/>
        <source>Upload Sessions</source>
        <translation>업로드 세션</translation>
    </message>
    <message>
        <location line="-26"/>
        <location line="+27"/>
        <location line="+36"/>
        <location line="+33"/>
        <source>Successful: 0</source>
        <translation>성공: 0</translation>
    </message>
    <message>
        <location line="-95"/>
        <location line="+27"/>
        <location line="+36"/>
        <location line="+33"/>
        <source>Failed: 0</source>
        <translation>실패: 0</translation>
    </message>
    <message>
        <location line="-94"/>
        <location line="+27"/>
        <source>Average Upload Per Session: 0 Bytes</source>
        <translation>세션당 평균 업로드: 0 Bytes</translation>
    </message>
    <message>
        <location line="-25"/>
        <location line="+27"/>
        <source>Average Upload Time: 0:00:00</source>
        <translation>평균 업로드 시간: 0:00:00</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+118"/>
        <location line="+19"/>
        <source>Downloads</source>
        <translation>다운로드</translation>
    </message>
    <message>
        <location line="-130"/>
        <location line="+39"/>
        <source>Downloaded Data: 0 Bytes</source>
        <translation>다운로드된 데이터: 0 Bytes</translation>
    </message>
    <message>
        <location line="-30"/>
        <source>Active Downloads: 0</source>
        <translation>활성 다운로드: 0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Found Sources: 0</source>
        <translation>발견된 소스: 0</translation>
    </message>
    <message>
        <location line="+82"/>
        <source>Connection</source>
        <translation>연결</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Active Connections: 0</source>
        <translation>활성 연결: 0</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+21"/>
        <source>Peak Connections: 0</source>
        <translation>최대 연결: 0</translation>
    </message>
    <message>
        <location line="-20"/>
        <source>Max Connections Limit Reached: 0</source>
        <translation>최대 연결 제한 도달: 0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Reconnects: 0</source>
        <translation>재연결: 0</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Time Statistics</source>
        <translation>시간 통계</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Time Since Last Reset: -</source>
        <translation>마지막 초기화 이후 시간: -</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Runtime: 0:00:00</source>
        <translation>실행 시간: 0:00:00</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+8"/>
        <source>Transfer Time: 0:00:00</source>
        <translation>전송 시간: 0:00:00</translation>
    </message>
    <message>
        <location line="-7"/>
        <location line="+8"/>
        <source>Upload Time: 0:00:00</source>
        <translation>업로드 시간: 0:00:00</translation>
    </message>
    <message>
        <location line="-7"/>
        <location line="+8"/>
        <source>Download Time: 0:00:00</source>
        <translation>다운로드 시간: 0:00:00</translation>
    </message>
    <message>
        <location line="-7"/>
        <source>Server Duration: 0:00:00</source>
        <translation>서버 접속 시간: 0:00:00</translation>
    </message>
    <message>
        <location line="-211"/>
        <location line="+32"/>
        <location line="+31"/>
        <location line="+39"/>
        <location line="+120"/>
        <source>Clients</source>
        <translation>클라이언트</translation>
    </message>
    <message>
        <location line="-219"/>
        <location line="+32"/>
        <location line="+31"/>
        <location line="+39"/>
        <source>%1: 0 Bytes</source>
        <translation>%1: 0 Bytes</translation>
    </message>
    <message>
        <location line="-101"/>
        <location line="+32"/>
        <location line="+31"/>
        <location line="+39"/>
        <source>Port</source>
        <translation>포트</translation>
    </message>
    <message>
        <location line="-101"/>
        <location line="+32"/>
        <location line="+31"/>
        <location line="+39"/>
        <source>Default Port 4662: 0 Bytes</source>
        <translation>기본 포트 4662: 0 Bytes</translation>
    </message>
    <message>
        <location line="-101"/>
        <location line="+32"/>
        <location line="+31"/>
        <location line="+39"/>
        <source>Other Ports: 0 Bytes</source>
        <translation>기타 포트: 0 Bytes</translation>
    </message>
    <message>
        <location line="-101"/>
        <location line="+32"/>
        <source>Data Source</source>
        <translation>데이터 소스</translation>
    </message>
    <message>
        <location line="-31"/>
        <location line="+32"/>
        <source>Complete File: 0 Bytes</source>
        <translation>완전한 파일: 0 Bytes</translation>
    </message>
    <message>
        <location line="-31"/>
        <location line="+32"/>
        <source>Part File: 0 Bytes</source>
        <translation>부분 파일: 0 Bytes</translation>
    </message>
    <message>
        <location line="-13"/>
        <location line="+70"/>
        <location line="+47"/>
        <location line="+34"/>
        <location line="+34"/>
        <source>Cumulative</source>
        <translation>누적</translation>
    </message>
    <message>
        <location line="-136"/>
        <location line="+33"/>
        <source>Completed Downloads: 0</source>
        <translation>완료된 다운로드: 0</translation>
    </message>
    <message>
        <location line="-31"/>
        <location line="+33"/>
        <source>Download Sessions</source>
        <translation>다운로드 세션</translation>
    </message>
    <message>
        <location line="-29"/>
        <location line="+33"/>
        <source>Average Download Per Session: 0 Bytes</source>
        <translation>세션당 평균 다운로드: 0 Bytes</translation>
    </message>
    <message>
        <location line="-31"/>
        <location line="+33"/>
        <source>Average Download Time: 0:00:00</source>
        <translation>평균 다운로드 시간: 0:00:00</translation>
    </message>
    <message>
        <location line="-30"/>
        <location line="+33"/>
        <source>Gain Due To Compression: 0 Bytes (0.0%)</source>
        <translation>압축으로 인한 이득: 0 Bytes (0.0%)</translation>
    </message>
    <message>
        <location line="-31"/>
        <location line="+33"/>
        <source>Lost Due To Corruption: 0 Bytes (0.0%)</source>
        <translation>손상으로 인한 손실: 0 Bytes (0.0%)</translation>
    </message>
    <message>
        <location line="-31"/>
        <location line="+33"/>
        <source>Parts Saved Due To ICH: 0</source>
        <translation>ICH로 복구된 파트: 0</translation>
    </message>
    <message>
        <location line="+36"/>
        <location line="+21"/>
        <source>General</source>
        <translation>일반</translation>
    </message>
    <message>
        <location line="-16"/>
        <source>Average Connections: 0.0</source>
        <translation>평균 연결 수: 0.0</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Upload Speed: 0 KB/s</source>
        <translation>업로드 속도: 0 KB/s</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+19"/>
        <source>Max Upload Rate: 0 KB/s</source>
        <translation>최대 업로드 속도: 0 KB/s</translation>
    </message>
    <message>
        <location line="-18"/>
        <location line="+19"/>
        <source>Max Average Upload Rate: 0 KB/s</source>
        <translation>최대 평균 업로드 속도: 0 KB/s</translation>
    </message>
    <message>
        <location line="-16"/>
        <source>Download Speed: 0 KB/s</source>
        <translation>다운로드 속도: 0 KB/s</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+19"/>
        <source>Max Download Rate: 0 KB/s</source>
        <translation>최대 다운로드 속도: 0 KB/s</translation>
    </message>
    <message>
        <location line="-18"/>
        <location line="+19"/>
        <source>Max Average Download Rate: 0 KB/s</source>
        <translation>최대 평균 다운로드 속도: 0 KB/s</translation>
    </message>
    <message>
        <location line="-12"/>
        <source>Server Reconnects: 0</source>
        <translation>서버 재연결: 0</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Connection Limit Reached: 0</source>
        <translation>연결 한도 도달: 0</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Average Upload Rate: 0 KB/s</source>
        <translation>평균 업로드 속도: 0 KB/s</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Average Download Rate: 0 KB/s</source>
        <translation>평균 다운로드 속도: 0 KB/s</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+435"/>
        <location line="+4"/>
        <source>Statistics Last Reset: %1</source>
        <translation>통계 마지막 초기화: %1</translation>
    </message>
    <message>
        <location line="-742"/>
        <location line="+303"/>
        <location line="+433"/>
        <location line="+7"/>
        <source>Unknown</source>
        <translation>알 수 없음</translation>
    </message>
    <message>
        <location line="-750"/>
        <source>Statistics Tree</source>
        <translation>통계 트리</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+746"/>
        <source>Statistics last reset: %1</source>
        <translation>통계 마지막 초기화: %1</translation>
    </message>
    <message>
        <location line="-568"/>
        <source>UDP File Re-asks: 0, Failed: 0 (0.0%)</source>
        <translation>UDP 파일 재요청: 0, 실패: 0 (0.0%)</translation>
    </message>
    <message>
        <location line="+58"/>
        <source>HTTP Cache</source>
        <translation>HTTP 캐시</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+8"/>
        <source>Published: 0 Bytes</source>
        <translation>게시됨: 0 Bytes</translation>
    </message>
    <message>
        <location line="-7"/>
        <location line="+8"/>
        <source>Fetched: 0 Bytes</source>
        <translation>가져옴: 0 Bytes</translation>
    </message>
    <message>
        <location line="-7"/>
        <location line="+8"/>
        <source>Upload Saved: 0 Bytes</source>
        <translation>절약한 업로드: 0 Bytes</translation>
    </message>
    <message>
        <location line="-7"/>
        <location line="+8"/>
        <source>Chunks Published: 0</source>
        <translation>게시한 청크: 0</translation>
    </message>
    <message>
        <location line="-7"/>
        <location line="+8"/>
        <source>Chunks Fetched: 0</source>
        <translation>가져온 청크: 0</translation>
    </message>
    <message>
        <location line="+64"/>
        <source>Run Time: 0:00:00</source>
        <translation>실행 시간: 0:00:00</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Total Server Duration: 0:00:00</source>
        <translation>총 서버 지속 시간: 0:00:00</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Known Clients: 0</source>
        <translation>알려진 클라이언트: 0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Client Software</source>
        <translation>클라이언트 소프트웨어</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Low ID: 0 (0.0%)</source>
        <translation>Low ID: 0 (0.0%)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Banned Clients: 0</source>
        <translation>차단된 클라이언트: 0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Filtered Clients: 0</source>
        <translation>필터된 클라이언트: 0</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Servers</source>
        <translation>서버</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Working Servers: 0</source>
        <translation>작동 중인 서버: 0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Failed Servers: 0</source>
        <translation>실패한 서버: 0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total: 0</source>
        <translation>합계: 0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Users: 0</source>
        <translation>총 사용자: 0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Files: 0</source>
        <translation>총 파일: 0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Low ID Users: 0</source>
        <translation>Low ID 사용자: 0</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+13"/>
        <source>Records</source>
        <translation>기록</translation>
    </message>
    <message>
        <location line="-12"/>
        <source>Most Working Servers: 0</source>
        <translation>최다 작동 서버: 0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Most Users Online: 0</source>
        <translation>최다 온라인 사용자: 0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Most Files Available: 0</source>
        <translation>최다 이용 가능 파일: 0</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Shared Files</source>
        <translation>공유 파일</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Number of Shared Files: 0</source>
        <translation>공유 파일 수: 0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Size: 0 Bytes</source>
        <translation>총 크기: 0 Bytes</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Average File Size: 0 Bytes</source>
        <translation>평균 파일 크기: 0 Bytes</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Largest Shared File: 0 Bytes</source>
        <translation>최대 공유 파일: 0 Bytes</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Most Files Shared: 0</source>
        <translation>최다 공유 파일: 0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Largest Share Size: 0 Bytes</source>
        <translation>최대 공유 크기: 0 Bytes</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Largest Average File Size: 0 Bytes</source>
        <translation>최대 평균 파일 크기: 0 Bytes</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Largest File Size: 0 Bytes</source>
        <translation>최대 파일 크기: 0 Bytes</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Total Downloads</source>
        <translation>전체 다운로드</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Number of Downloads: 0</source>
        <translation>다운로드 수: 0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Size of Downloads: 0 Bytes</source>
        <translation>다운로드 총 크기: 0 Bytes</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Size Downloaded: 0 Bytes</source>
        <translation>다운로드된 총 크기: 0 Bytes</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Size Left to Download: 0 Bytes</source>
        <translation>남은 다운로드 크기: 0 Bytes</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Free Space on Drive: 0 Bytes</source>
        <translation>드라이브 여유 공간: 0 Bytes</translation>
    </message>
    <message>
        <location line="+114"/>
        <source>Session UL:DL Ratio: %1</source>
        <translation>세션 UL:DL 비율: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Friend Session UL:DL Ratio: %1</source>
        <translation>친구 세션 UL:DL 비율: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Cumulative UL:DL Ratio: %1</source>
        <translation>누적 UL:DL 비율: %1</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+56"/>
        <source>Uploaded Data: %1</source>
        <translation>업로드된 데이터: %1</translation>
    </message>
    <message>
        <location line="-45"/>
        <location line="+55"/>
        <location line="+66"/>
        <location line="+48"/>
        <source>Default Port 4662: %1 %2</source>
        <translation>기본 포트 4662: %1 %2</translation>
    </message>
    <message>
        <location line="-166"/>
        <location line="+55"/>
        <location line="+66"/>
        <location line="+48"/>
        <source>Other Ports: %1 %2</source>
        <translation>기타 포트: %1 %2</translation>
    </message>
    <message>
        <location line="-166"/>
        <location line="+55"/>
        <source>Complete File: %1 %2</source>
        <translation>완전한 파일: %1 %2</translation>
    </message>
    <message>
        <location line="-52"/>
        <location line="+55"/>
        <source>Part File: %1 %2</source>
        <translation>부분 파일: %1 %2</translation>
    </message>
    <message>
        <location line="-50"/>
        <source>Uploaded Data to Friends: %1</source>
        <translation>친구에게 업로드된 데이터: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Active Uploads: %1</source>
        <translation>활성 업로드: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Waiting Uploads: %1</source>
        <translation>대기 중 업로드: %1</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+48"/>
        <source>Successful: %1%2</source>
        <translation>성공: %1%2</translation>
    </message>
    <message>
        <location line="-46"/>
        <location line="+48"/>
        <source>Failed: %1</source>
        <translation>실패: %1</translation>
    </message>
    <message>
        <location line="-45"/>
        <location line="+48"/>
        <source>Average Upload Per Session: %1</source>
        <translation>세션당 평균 업로드: %1</translation>
    </message>
    <message>
        <location line="-46"/>
        <location line="+48"/>
        <source>Average Upload Time: %1</source>
        <translation>평균 업로드 시간: %1</translation>
    </message>
    <message>
        <location line="-44"/>
        <source>%1: %2</source>
        <translation>%1: %2</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+66"/>
        <location line="+48"/>
        <location line="+36"/>
        <source>Total Overhead (Packets)</source>
        <translation>전체 오버헤드 (패킷)</translation>
    </message>
    <message>
        <location line="-149"/>
        <location line="+66"/>
        <location line="+48"/>
        <location line="+36"/>
        <source>File Request Overhead (Packets)</source>
        <translation>파일 요청 오버헤드 (패킷)</translation>
    </message>
    <message>
        <location line="-149"/>
        <location line="+66"/>
        <location line="+48"/>
        <location line="+36"/>
        <source>Source Exchange Overhead (Packets)</source>
        <translation>소스 교환 오버헤드 (패킷)</translation>
    </message>
    <message>
        <location line="-149"/>
        <location line="+66"/>
        <location line="+48"/>
        <location line="+36"/>
        <source>Server Overhead (Packets)</source>
        <translation>서버 오버헤드 (패킷)</translation>
    </message>
    <message>
        <location line="-149"/>
        <location line="+66"/>
        <location line="+48"/>
        <location line="+36"/>
        <source>Kad Overhead (Packets)</source>
        <translation>Kad 오버헤드 (패킷)</translation>
    </message>
    <message>
        <location line="-100"/>
        <location line="+6"/>
        <source>Published</source>
        <translation>게시됨</translation>
    </message>
    <message>
        <location line="-5"/>
        <location line="+6"/>
        <source>Fetched</source>
        <translation>가져옴</translation>
    </message>
    <message>
        <location line="-5"/>
        <location line="+6"/>
        <source>Upload Saved</source>
        <translation>절약한 업로드</translation>
    </message>
    <message>
        <location line="-5"/>
        <location line="+6"/>
        <source>Chunks Published</source>
        <translation>게시한 청크</translation>
    </message>
    <message>
        <location line="-5"/>
        <location line="+6"/>
        <source>Chunks Fetched</source>
        <translation>가져온 청크</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+48"/>
        <source>Downloaded Data: %1</source>
        <translation>다운로드된 데이터: %1</translation>
    </message>
    <message>
        <location line="-30"/>
        <source>Active Downloads: %1</source>
        <translation>활성 다운로드: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Found Sources: %1</source>
        <translation>발견된 소스: %1</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>UDP File Re-asks: %1, Failed: %2 %3</source>
        <translation>UDP 파일 재요청: %1, 실패: %2 %3</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+37"/>
        <source>Completed Downloads: %1</source>
        <translation>완료된 다운로드: %1</translation>
    </message>
    <message>
        <location line="-31"/>
        <location line="+36"/>
        <source>Gain Due To Compression: %1 %2</source>
        <translation>압축으로 인한 이득: %1 %2</translation>
    </message>
    <message>
        <location line="-34"/>
        <location line="+36"/>
        <source>Lost Due To Corruption: %1 %2</source>
        <translation>손상으로 인한 손실: %1 %2</translation>
    </message>
    <message>
        <location line="-34"/>
        <location line="+36"/>
        <source>Parts Saved Due To ICH: %1</source>
        <translation>ICH로 복구된 파트: %1</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Active Connections: %1</source>
        <translation>활성 연결: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+19"/>
        <source>Peak Connections: %1</source>
        <translation>최대 연결: %1</translation>
    </message>
    <message>
        <location line="-17"/>
        <source>Max Connections Limit Reached: %1</source>
        <translation>최대 연결 제한 도달: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Reconnects: %1</source>
        <translation>재연결: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Average Connections: %1</source>
        <translation>평균 연결: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Upload Speed: %1</source>
        <translation>업로드 속도: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+15"/>
        <source>Max Upload Rate: %1</source>
        <translation>최대 업로드 속도: %1</translation>
    </message>
    <message>
        <location line="-14"/>
        <location line="+15"/>
        <source>Max Average Upload Rate: %1</source>
        <translation>최대 평균 업로드 속도: %1</translation>
    </message>
    <message>
        <location line="-14"/>
        <source>Download Speed: %1</source>
        <translation>다운로드 속도: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+15"/>
        <source>Max Download Rate: %1</source>
        <translation>최대 다운로드 속도: %1</translation>
    </message>
    <message>
        <location line="-14"/>
        <location line="+15"/>
        <source>Max Average Download Rate: %1</source>
        <translation>최대 평균 다운로드 속도: %1</translation>
    </message>
    <message>
        <location line="-11"/>
        <source>Server Reconnects: %1</source>
        <translation>서버 재연결: %1</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Connection Limit Reached: %1</source>
        <translation>연결 한도 도달: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Average Upload Rate: %1</source>
        <translation>평균 업로드 속도: %1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Average Download Rate: %1</source>
        <translation>평균 다운로드 속도: %1</translation>
    </message>
    <message>
        <location line="+13"/>
        <location line="+4"/>
        <source>Time Since Last Reset: %1</source>
        <translation>마지막 초기화 이후 시간: %1</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Runtime: %1</source>
        <translation>실행 시간: %1</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+17"/>
        <source>Transfer Time: %1 %2</source>
        <translation>전송 시간: %1 %2</translation>
    </message>
    <message>
        <location line="-15"/>
        <location line="+17"/>
        <source>Upload Time: %1 %2</source>
        <translation>업로드 시간: %1 %2</translation>
    </message>
    <message>
        <location line="-15"/>
        <location line="+17"/>
        <source>Download Time: %1 %2</source>
        <translation>다운로드 시간: %1 %2</translation>
    </message>
    <message>
        <location line="-15"/>
        <source>Server Duration: %1 %2</source>
        <translation>서버 접속 시간: %1 %2</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Run Time: %1</source>
        <translation>실행 시간: %1</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Total Server Duration: %1 %2</source>
        <translation>총 서버 지속 시간: %1 %2</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Known Clients: %1</source>
        <translation>알려진 클라이언트: %1</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Low ID: %1 %2</source>
        <translation>Low ID: %1 %2</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Banned Clients: %1</source>
        <translation>차단된 클라이언트: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Filtered Clients: %1</source>
        <translation>필터된 클라이언트: %1</translation>
    </message>
    <message>
        <location line="+87"/>
        <source>Working Servers: %1</source>
        <translation>작동 중인 서버: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Failed Servers: %1</source>
        <translation>실패한 서버: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Total: %1</source>
        <translation>합계: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Total Users: %1</source>
        <translation>총 사용자: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Total Files: %1</source>
        <translation>총 파일: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Low ID Users: %1</source>
        <translation>Low ID 사용자: %1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Most Working Servers: %1</source>
        <translation>최다 작동 서버: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Most Users Online: %1</source>
        <translation>최다 온라인 사용자: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Most Files Available: %1</source>
        <translation>최다 이용 가능 파일: %1</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Number of Shared Files: %1</source>
        <translation>공유 파일 수: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Total Size: %1</source>
        <translation>총 크기: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Average File Size: %1</source>
        <translation>평균 파일 크기: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Largest Shared File: %1</source>
        <translation>최대 공유 파일: %1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Most Files Shared: %1</source>
        <translation>최다 공유 파일: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Largest Share Size: %1</source>
        <translation>최대 공유 크기: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Largest Average File Size: %1</source>
        <translation>최대 평균 파일 크기: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Largest File Size: %1</source>
        <translation>최대 파일 크기: %1</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Number of Downloads: %1</source>
        <translation>다운로드 수: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Total Size of Downloads: %1</source>
        <translation>다운로드 총 크기: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Total Size Downloaded: %1</source>
        <translation>다운로드된 총 크기: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Total Size Left to Download: %1</source>
        <translation>남은 다운로드 크기: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Free Space on Drive: %1</source>
        <translation>드라이브 여유 공간: %1</translation>
    </message>
    <message>
        <location line="+16"/>
        <location line="+39"/>
        <source>Reset Statistics</source>
        <translation>통계 초기화</translation>
    </message>
    <message>
        <location line="-38"/>
        <location line="+58"/>
        <source>Restore Statistics</source>
        <translation>통계 복원</translation>
    </message>
    <message>
        <location line="-52"/>
        <source>Expand Main Sections</source>
        <translation>주요 섹션 펼치기</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Expand All Sections</source>
        <translation>모든 섹션 펼치기</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Collapse All Sections</source>
        <translation>모든 섹션 접기</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Copy Branch</source>
        <translation>분기 복사</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Copy All Visible</source>
        <translation>보이는 모든 항목 복사</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Copy All Statistics</source>
        <translation>모든 통계 복사</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Are you sure you wish to reset your cumulative statistics?

If you change your mind, you can reverse this action by clicking the &apos;Restore Stats&apos; button.</source>
        <translation>누적 통계를 초기화하시겠습니까?

마음이 바뀌면 &apos;통계 복원&apos; 버튼을 클릭하여 이 작업을 되돌릴 수 있습니다.</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Are you sure you wish to restore your cumulative statistics from the backup file?

Clicking &apos;Restore Stats&apos; again will reload your current statistics.</source>
        <translation>백업 파일에서 누적 통계를 복원하시겠습니까?

&apos;통계 복원&apos;을 다시 클릭하면 현재 통계가 다시 불러와집니다.</translation>
    </message>
</context>
<context>
    <name>eMule::ToolbarCustomizeDialog</name>
    <message>
        <location filename="../src/gui/dialogs/ToolbarCustomizeDialog.cpp" line="+72"/>
        <source>Customize Toolbar</source>
        <translation>도구 모음 사용자 지정</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Available toolbar buttons:</source>
        <translation>사용 가능한 도구 모음 단추:</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Add -&gt;</source>
        <translation>추가 -&gt;</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&lt;- Remove</source>
        <translation>&lt;- 제거</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Current toolbar buttons:</source>
        <translation>현재 도구 모음 단추:</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Close</source>
        <translation>닫기</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Reset</source>
        <translation>초기화</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Move Up</source>
        <translation>위로 이동</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Move Down</source>
        <translation>아래로 이동</translation>
    </message>
</context>
<context>
    <name>eMule::TransferPanel</name>
    <message>
        <location filename="../src/gui/panels/TransferPanel.cpp" line="+617"/>
        <source>Downloads</source>
        <translation>다운로드</translation>
    </message>
    <message>
        <location line="-286"/>
        <source>Priority (Download)</source>
        <translation>우선순위(다운로드)</translation>
    </message>
    <message>
        <location line="+29"/>
        <location line="+1568"/>
        <source>Low</source>
        <translation>낮음</translation>
    </message>
    <message>
        <location line="-1567"/>
        <location line="+1568"/>
        <source>Normal</source>
        <translation>보통</translation>
    </message>
    <message>
        <location line="-1567"/>
        <location line="+1568"/>
        <source>High</source>
        <translation>높음</translation>
    </message>
    <message>
        <location line="-1566"/>
        <location line="+1568"/>
        <source>Very Low</source>
        <translation>매우 낮음</translation>
    </message>
    <message>
        <location line="-1567"/>
        <location line="+1568"/>
        <source>Very High</source>
        <translation>매우 높음</translation>
    </message>
    <message>
        <location line="-1566"/>
        <location line="+1569"/>
        <source>Auto</source>
        <translation>자동</translation>
    </message>
    <message>
        <location line="-1558"/>
        <location line="+505"/>
        <source>Pause</source>
        <translation>일시정지</translation>
    </message>
    <message>
        <location line="-496"/>
        <location line="+502"/>
        <source>Stop</source>
        <translation>중지</translation>
    </message>
    <message>
        <location line="-493"/>
        <location line="+499"/>
        <source>Resume</source>
        <translation>재개</translation>
    </message>
    <message>
        <location line="-486"/>
        <location line="+492"/>
        <source>Cancel</source>
        <translation>취소</translation>
    </message>
    <message>
        <location line="-489"/>
        <location line="+503"/>
        <source>Cancel Download</source>
        <translation>다운로드 취소</translation>
    </message>
    <message>
        <location line="-502"/>
        <location line="+503"/>
        <source>Cancel download &quot;%1&quot;?</source>
        <translation>&quot;%1&quot; 다운로드를 취소하시겠습니까?</translation>
    </message>
    <message>
        <location line="-499"/>
        <location line="+503"/>
        <source>Cancel Downloads</source>
        <translation>다운로드 취소</translation>
    </message>
    <message>
        <location line="-502"/>
        <location line="+503"/>
        <source>Cancel %1 selected downloads?</source>
        <translation>선택한 다운로드 %1개를 취소하시겠습니까?</translation>
    </message>
    <message>
        <location line="-490"/>
        <location line="+501"/>
        <source>Open File</source>
        <translation>파일 열기</translation>
    </message>
    <message>
        <location line="-492"/>
        <location line="+506"/>
        <source>Preview</source>
        <translation>미리보기</translation>
    </message>
    <message>
        <location line="-500"/>
        <location line="+1531"/>
        <location line="+87"/>
        <source>Details...</source>
        <translation>상세...</translation>
    </message>
    <message>
        <location line="-1612"/>
        <source>Comments...</source>
        <translation>댓글...</translation>
    </message>
    <message>
        <location line="+17"/>
        <location line="+525"/>
        <source>Clear Completed</source>
        <translation>완료됨 삭제</translation>
    </message>
    <message>
        <location line="-515"/>
        <source>eD2K Links...</source>
        <translation>eD2K 링크...</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Paste eD2K Links</source>
        <translation>eD2K 링크 붙여넣기</translation>
    </message>
    <message>
        <location line="+21"/>
        <location line="+1537"/>
        <location line="+82"/>
        <source>Find...</source>
        <translation>찾기...</translation>
    </message>
    <message>
        <location line="-1615"/>
        <source>Search Related Files</source>
        <translation>관련 파일 검색</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Web Services</source>
        <translation>웹 서비스</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+447"/>
        <source>Assign To Category</source>
        <translation>카테고리에 할당</translation>
    </message>
    <message>
        <location line="-445"/>
        <location line="+452"/>
        <source>(All)</source>
        <translation>(전체)</translation>
    </message>
    <message>
        <location line="-351"/>
        <source>All</source>
        <translation>모두</translation>
    </message>
    <message>
        <location line="+1166"/>
        <source>Uploading</source>
        <translation>업로드 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloading</source>
        <translation>다운로드 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>On Queue</source>
        <translation>대기 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Known Clients</source>
        <translation>알려진 클라이언트</translation>
    </message>
    <message>
        <location line="-945"/>
        <source>Clients on queue:   0</source>
        <translation>대기열의 클라이언트:   0</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Priority</source>
        <translation>우선순위</translation>
    </message>
    <message>
        <location line="+61"/>
        <source>Open Folder</source>
        <translation>폴더 열기</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Details</source>
        <translation>상세</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Comments</source>
        <translation>댓글</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>eD2K Links</source>
        <translation>eD2K 링크</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Search Related</source>
        <translation>관련 검색</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Find</source>
        <translation>찾기</translation>
    </message>
    <message>
        <location line="+423"/>
        <source>Preview not available — web server is not running or stream token not received.</source>
        <translation>미리보기를 사용할 수 없습니다 — 웹 서버가 실행 중이 아니거나 스트림 토큰을 받지 못했습니다.</translation>
    </message>
    <message>
        <location line="+31"/>
        <source>Open File not available — web server is not running or stream token not received.</source>
        <translation>파일 열기를 사용할 수 없습니다 — 웹 서버가 실행 중이 아니거나 스트림 토큰을 받지 못했습니다.</translation>
    </message>
    <message>
        <location line="+350"/>
        <source>Downloads (%1)</source>
        <translation>다운로드 (%1)</translation>
    </message>
    <message>
        <location line="-11"/>
        <source>Uploading (%1)</source>
        <translation>업로드 중 (%1)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloading (%1)</source>
        <translation>다운로드 중 (%1)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>On Queue (%1)</source>
        <translation>대기 중 (%1)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Known Clients (%1)</source>
        <translation>알려진 클라이언트 (%1)</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Clients on queue:   %1</source>
        <translation>대기열의 클라이언트:   %1</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Cat %1</source>
        <translation>카테고리 %1</translation>
    </message>
    <message>
        <location line="+124"/>
        <location line="+86"/>
        <source>Add To Friends</source>
        <translation>친구에 추가</translation>
    </message>
    <message>
        <location line="-64"/>
        <location line="+7"/>
        <location line="+81"/>
        <location line="+3"/>
        <source>Send Message</source>
        <translation>메시지 보내기</translation>
    </message>
    <message>
        <location line="-84"/>
        <location line="+84"/>
        <source>Message:</source>
        <translation>메시지:</translation>
    </message>
    <message>
        <location line="-73"/>
        <location line="+86"/>
        <source>View Shared Files</source>
        <translation>공유 파일 보기</translation>
    </message>
</context>
<context>
    <name>eMule::TrayMenuManager</name>
    <message>
        <location filename="../src/gui/app/TrayMenuManager.cpp" line="+70"/>
        <source>eMule Speed</source>
        <translation>eMule 속도</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Download:</source>
        <translation>다운로드:</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+15"/>
        <source> KB/s</source>
        <translation> KB/s</translation>
    </message>
    <message>
        <location line="-14"/>
        <location line="+15"/>
        <source>Unlimited</source>
        <translation>무제한</translation>
    </message>
    <message>
        <location line="-5"/>
        <source>Upload:</source>
        <translation>업로드:</translation>
    </message>
    <message>
        <location line="+34"/>
        <source>Set Full Up/Down-Speed</source>
        <translation>업로드/다운로드 속도 최대로 설정</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Throttle Up/Down-Speed</source>
        <translation>업로드/다운로드 속도 제한</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Connect</source>
        <translation>연결</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Disconnect</source>
        <translation>연결 해제</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Options</source>
        <translation>옵션</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Restore</source>
        <translation>복원</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Exit</source>
        <translation>종료</translation>
    </message>
</context>
<context>
    <name>eMule::VersionChecker</name>
    <message>
        <location filename="../src/gui/app/VersionChecker.cpp" line="+100"/>
        <source>the version manifest is not a JSON object</source>
        <translation>버전 매니페스트가 JSON 객체가 아닙니다</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>invalid JSON response: %1</source>
        <translation>잘못된 JSON 응답: %1</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>the version manifest has no &apos;latest&apos; field</source>
        <translation>버전 매니페스트에 &apos;latest&apos; 필드가 없습니다</translation>
    </message>
</context>
</TS>
