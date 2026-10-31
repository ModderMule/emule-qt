<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="ko_KR">
<context>
    <name>ContainerSniffer</name>
    <message>
        <location filename="../src/core/media/ContainerSniffer.cpp" line="+242"/>
        <source>Named .%1 but matches no media container we recognise — very likely a fake.</source>
        <translation>.%1(으)로 이름이 붙어 있지만 알려진 미디어 컨테이너와 전혀 일치하지 않습니다 — 가짜일 가능성이 매우 높습니다.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Named .%1 but the contents are %2.</source>
        <translation>.%1(으)로 이름이 붙어 있지만 실제 내용은 %2입니다.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Only the extension is wrong — most players still open it.</source>
        <translation>확장자만 잘못되었습니다 — 대부분의 플레이어에서 열 수 있습니다.</translation>
    </message>
</context>
<context>
    <name>Ed2kLinkImporter</name>
    <message>
        <location filename="../src/gui/utils/Ed2kLinkImporter.cpp" line="+214"/>
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
        <location line="+197"/>
        <source>%n further HTTP Cache link(s) ignored — apply one at a time.</source>
        <translation>
            <numerusform>추가 HTTP 캐시 링크 %n개를 무시했습니다 — 한 번에 하나씩 적용하세요.</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="+104"/>
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
        <location line="+22"/>
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
        <location filename="../src/gui/utils/IpcFeedback.cpp" line="+28"/>
        <source>The request was rejected by eMule.</source>
        <translation>요청이 eMule에 의해 거부되었습니다.</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Not connected to daemon — %1</source>
        <translation>데몬에 연결되어 있지 않습니다 — %1</translation>
    </message>
</context>
<context>
    <name>MassRenameAction</name>
    <message>
        <location filename="../src/gui/utils/MassRenameAction.cpp" line="+22"/>
        <location line="+19"/>
        <location line="+12"/>
        <source>Mass Rename</source>
        <translation>일괄 이름 변경</translation>
    </message>
    <message numerus="yes">
        <location line="+1"/>
        <source>%n file(s) could not be renamed:</source>
        <translation>
            <numerusform>%n개 파일의 이름을 변경할 수 없습니다:</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>Notifier</name>
    <message>
        <location filename="../src/gui/utils/NotifierText.h" line="+30"/>
        <source>Downloaded:</source>
        <translation>다운로드됨:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Connection lost</source>
        <translation>연결이 끊어졌습니다</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Out of disk space</source>
        <translation>디스크 공간 부족</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>You have insufficient disk space to download &quot;%1&quot;!</source>
        <translation>&quot;%1&quot;을(를) 다운로드할 디스크 공간이 부족합니다!</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Port not available</source>
        <translation>포트를 사용할 수 없음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Fatal Error: Unable to create socket on port %1</source>
        <translation>치명적 오류: 포트 %1에 소켓을 만들 수 없습니다</translation>
    </message>
</context>
<context>
    <name>PortChange</name>
    <message>
        <location filename="../src/gui/dialogs/PortChangeNotice.h" line="+42"/>
        <source>Now listening on TCP port %1 and UDP port %2.</source>
        <translation>이제 TCP 포트 %1 및 UDP 포트 %2에서 수신 대기합니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The new ports take effect after restarting eMule, because the core is connected to a network or to other clients.

Until then it keeps listening on TCP port %1 and UDP port %2.</source>
        <translation>코어가 네트워크 또는 다른 클라이언트에 연결되어 있으므로 새 포트는 eMule을 다시 시작한 후에 적용됩니다.

그때까지는 TCP 포트 %1 및 UDP 포트 %2에서 계속 수신 대기합니다.</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>The new port could not be opened. It may be in use by another program.

The core keeps listening on TCP port %1 and UDP port %2.</source>
        <translation>새 포트를 열 수 없습니다. 다른 프로그램에서 사용 중일 수 있습니다.

코어는 TCP 포트 %1 및 UDP 포트 %2에서 계속 수신 대기합니다.</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Ports</source>
        <translation>포트</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Restart Now</source>
        <translation>지금 다시 시작</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Later</source>
        <translation>나중에</translation>
    </message>
</context>
<context>
    <name>PortMapStatus</name>
    <message>
        <location filename="../src/gui/dialogs/PortMapStatusText.h" line="+37"/>
        <source>Ports forwarded via %1.</source>
        <translation>%1(으)로 포트를 전달했습니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ports forwarded via %1 (external address %2).</source>
        <translation>%1(으)로 포트를 전달했습니다 (외부 주소 %2).</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>The router granted the ports, but they are not reachable from the Internet.</source>
        <translation>라우터가 포트를 허용했지만 인터넷에서 접근할 수 없습니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The router granted the ports, but its address %1 is not public, so they stay unreachable over IPv4.</source>
        <translation>라우터가 포트를 허용했지만 주소 %1이(가) 공인 주소가 아니어서 IPv4로는 접근할 수 없습니다.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>No router answered PCP, NAT-PMP or UPnP. Forward the ports manually.</source>
        <translation>PCP, NAT-PMP, UPnP에 응답한 라우터가 없습니다. 포트를 수동으로 전달하세요.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>The router refused the port forwarding.</source>
        <translation>라우터가 포트 전달을 거부했습니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Automatic port forwarding is switched off.</source>
        <translation>자동 포트 전달이 꺼져 있습니다.</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Asking the router...</source>
        <translation>라우터에 문의하는 중...</translation>
    </message>
</context>
<context>
    <name>PortTest</name>
    <message>
        <location filename="../src/gui/dialogs/PortTest.h" line="+82"/>
        <source>reachable</source>
        <translation>접근 가능</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>not reachable: closed, nothing is listening on this port</source>
        <translation>접근 불가: 닫힘, 이 포트에서 수신 대기 중인 것이 없습니다</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>not reachable: rejected by a firewall</source>
        <translation>접근 불가: 방화벽이 거부함</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>not reachable: no answer (firewall or missing port forwarding)</source>
        <translation>접근 불가: 응답 없음 (방화벽 또는 포트 전달 누락)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>not tested: the test server may not send to this port</source>
        <translation>테스트 안 됨: 테스트 서버가 이 포트로 보낼 수 없습니다</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>not tested: the test server cannot reach this IPv6 address</source>
        <translation>테스트 안 됨: 테스트 서버가 이 IPv6 주소에 접근할 수 없습니다</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>not tested: the test packet could not be sent</source>
        <translation>테스트 안 됨: 테스트 패킷을 보낼 수 없었습니다</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>not tested: the address is not public</source>
        <translation>테스트 안 됨: 주소가 공인 주소가 아닙니다</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>not tested: too many tests, try again in a minute</source>
        <translation>테스트 안 됨: 테스트가 너무 많습니다. 1분 후 다시 시도하세요</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>not reachable (%1)</source>
        <translation>접근 불가 (%1)</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>BitTorrent is not running, so its port cannot be tested.</source>
        <translation>BitTorrent가 실행 중이 아니므로 포트를 테스트할 수 없습니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The ed3k node is not running, so its port cannot be tested.</source>
        <translation>ed3k 노드가 실행 중이 아니므로 포트를 테스트할 수 없습니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The ed3k node could not make a test packet.</source>
        <translation>ed3k 노드가 테스트 패킷을 만들 수 없었습니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The test server emule-qt.org did not answer.</source>
        <translation>테스트 서버 emule-qt.org가 응답하지 않았습니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The test server refused the test.</source>
        <translation>테스트 서버가 테스트를 거부했습니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The test server cannot test this port yet. Please try again later.</source>
        <translation>테스트 서버가 아직 이 포트를 테스트할 수 없습니다. 나중에 다시 시도하세요.</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+90"/>
        <source>The core did not run the test.</source>
        <translation>코어가 테스트를 실행하지 않았습니다.</translation>
    </message>
    <message>
        <location line="-59"/>
        <source>TCP: %1</source>
        <translation>TCP: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>UDP: %1</source>
        <translation>UDP: %1</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>BitTorrent port %1. TCP and UDP must both be reachable from the internet.</source>
        <translation>BitTorrent 포트 %1. TCP와 UDP 모두 인터넷에서 접근 가능해야 합니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Both TCP and UDP are reachable.</source>
        <translation>TCP와 UDP 모두 접근 가능합니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>TCP and UDP are both reachable over one address family only.</source>
        <translation>TCP와 UDP 모두 하나의 주소 체계로만 접근 가능합니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>TCP and UDP are not both reachable. Other peers can still be reached, but fewer can connect to you.</source>
        <translation>TCP와 UDP가 모두 접근 가능하지는 않습니다. 다른 피어에는 계속 연결할 수 있지만, 나에게 연결할 수 있는 피어는 줄어듭니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>ed3k UDP port %1. Only UDP must be reachable: the ed3k node uses no TCP port.</source>
        <translation>ed3k UDP 포트 %1. UDP만 접근 가능하면 됩니다: ed3k 노드는 TCP 포트를 사용하지 않습니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>UDP is reachable.</source>
        <translation>UDP에 접근 가능합니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>UDP is reachable over one address family only.</source>
        <translation>UDP는 하나의 주소 체계로만 접근 가능합니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>UDP is not reachable. The node then finds fewer peers and relies on hole punching.</source>
        <translation>UDP에 접근할 수 없습니다. 노드가 찾는 피어가 줄고 홀 펀칭에 의존하게 됩니다.</translation>
    </message>
    <message>
        <location line="+10"/>
        <location line="+19"/>
        <location line="+4"/>
        <location line="+5"/>
        <source>Test Ports</source>
        <translation>포트 테스트</translation>
    </message>
    <message>
        <location line="-28"/>
        <source>Not connected to the core.</source>
        <translation>코어에 연결되지 않았습니다.</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Testing...</source>
        <translation>테스트 중...</translation>
    </message>
</context>
<context>
    <name>PreviewLauncher</name>
    <message>
        <location filename="../src/gui/utils/PreviewLauncher.cpp" line="+319"/>
        <source>Not connected to the core.</source>
        <translation>코어에 연결되지 않았습니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The core has not sent its stream token yet. It arrives with the next status update — try again in a moment.</source>
        <translation>코어가 아직 스트림 토큰을 보내지 않았습니다. 다음 상태 업데이트와 함께 전달되니 잠시 후 다시 시도하세요.</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>The core runs on another machine and its web server only listens on localhost.

Enable Web Interface or REST API under Options → Web Interface.</source>
        <translation>코어가 다른 컴퓨터에서 실행 중이며 코어의 웹 서버는 localhost에서만 수신 대기합니다.

옵션 → 웹 인터페이스에서 웹 인터페이스 또는 REST API를 활성화하세요.</translation>
    </message>
</context>
<context>
    <name>Priority</name>
    <message>
        <location filename="../src/gui/utils/PriorityText.cpp" line="+20"/>
        <source>Very Low</source>
        <translation>매우 낮음</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Auto [Lo]</source>
        <translation>자동 [하]</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Low</source>
        <translation>낮음</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Auto [Hi]</source>
        <translation>자동 [상]</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>High</source>
        <translation>높음</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Release</source>
        <translation>릴리스</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Very High</source>
        <translation>매우 높음</translation>
    </message>
    <message>
        <location line="-24"/>
        <source>Auto [No]</source>
        <translation>자동 [중]</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Normal</source>
        <translation>보통</translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <location filename="../src/gui/utils/Ed2kLinkImporter.cpp" line="-400"/>
        <location line="+20"/>
        <source>eD2K Link</source>
        <translation>eD2K 링크</translation>
    </message>
    <message>
        <location line="-19"/>
        <source>Do you want to download the following file(s)?

%1</source>
        <translation>다음 파일을 다운로드하시겠습니까?

%1</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>You have already downloaded the following file(s). Download them again?

%1</source>
        <translation>다음 파일은 이미 다운로드했습니다. 다시 다운로드할까요?

%1</translation>
    </message>
    <message>
        <location filename="../src/gui/app/main.cpp" line="+500"/>
        <source>eMule Qt</source>
        <translation>eMule Qt</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>eMule Qt is already running with this configuration but does not respond.

Start it with --new-instance or --config &lt;dir&gt; to open another window.</source>
        <translation>eMule Qt가 이 설정으로 이미 실행 중이지만 응답하지 않습니다.

다른 창을 열려면 --new-instance 또는 --config &lt;폴더&gt; 옵션으로 실행하세요.</translation>
    </message>
    <message>
        <location line="+202"/>
        <source>Core Certificate Changed</source>
        <translation>코어 인증서가 변경됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The core at %1 shows a different certificate than the one trusted before. This happens when its ipc-cert.pem was regenerated, but can also mean someone is intercepting the connection.

Trusted: %2
Presented: %3

Trust the new certificate?</source>
        <translation>%1의 코어가 이전에 신뢰한 것과 다른 인증서를 제시합니다. ipc-cert.pem이 다시 생성되면 이런 일이 생기지만, 누군가 연결을 가로채고 있다는 뜻일 수도 있습니다.

신뢰함: %2
제시됨: %3

새 인증서를 신뢰하시겠습니까?</translation>
    </message>
    <message>
        <location line="+309"/>
        <source>Download Added</source>
        <translation>다운로드 추가됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>A new download has been added.</source>
        <translation>새 다운로드가 추가되었습니다.</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Message from %1</source>
        <translation>%1의 메시지</translation>
    </message>
    <message>
        <source>Chat Message from %1</source>
        <translation type="vanished">%1의 채팅 메시지</translation>
    </message>
    <message>
        <source>Log Entry</source>
        <translation type="vanished">로그 항목</translation>
    </message>
    <message>
        <source>Connection Lost</source>
        <translation type="vanished">연결 끊김</translation>
    </message>
    <message>
        <source>Server connection has been lost.</source>
        <translation type="vanished">서버 연결이 끊어졌습니다.</translation>
    </message>
    <message>
        <source>Very Low</source>
        <translation type="vanished">매우 낮음</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/UsenetQueueModel.cpp" line="+205"/>
        <source>Skipped</source>
        <translation>건너뜀</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Held back — fetched if a repair needs it</source>
        <translation>보류됨 — 복구에 필요하면 가져옵니다</translation>
    </message>
    <message numerus="yes">
        <location line="+2"/>
        <location line="+4"/>
        <source>%n article(s) missing</source>
        <translation>
            <numerusform>기사 %n개 누락</numerusform>
        </translation>
    </message>
    <message>
        <location line="-3"/>
        <source>Complete</source>
        <translation>완료</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Queued</source>
        <translation>대기 중</translation>
    </message>
    <message>
        <location line="+445"/>
        <source>Very high</source>
        <translation>매우 높음</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Low</source>
        <translation>낮음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Very low</source>
        <translation>매우 낮음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Normal</source>
        <translation>보통</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>High</source>
        <translation>높음</translation>
    </message>
    <message>
        <source>Very High</source>
        <translation type="vanished">매우 높음</translation>
    </message>
    <message>
        <source>Auto [%1]</source>
        <translation type="vanished">자동 [%1]</translation>
    </message>
    <message>
        <location filename="../src/gui/utils/RatingIcons.cpp" line="+59"/>
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
        <location line="+113"/>
        <source>
Rating:	%1</source>
        <translation>
평가:	%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>
Has comments</source>
        <translation>
코멘트 있음</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/ClientListModel.cpp" line="+56"/>
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
        <location filename="../src/gui/controls/DownloadListModel.cpp" line="+79"/>
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
        <source>Yes</source>
        <translation type="vanished">예</translation>
    </message>
    <message>
        <source>Never</source>
        <translation type="vanished">안 함</translation>
    </message>
    <message>
        <source>Archive</source>
        <translation type="vanished">압축 파일</translation>
    </message>
    <message>
        <source>Audio</source>
        <translation type="vanished">오디오</translation>
    </message>
    <message>
        <source>Video</source>
        <translation type="vanished">비디오</translation>
    </message>
    <message>
        <source>Image</source>
        <translation type="vanished">이미지</translation>
    </message>
    <message>
        <source>Program</source>
        <translation type="vanished">프로그램</translation>
    </message>
    <message>
        <source>Document</source>
        <translation type="vanished">문서</translation>
    </message>
    <message>
        <source>CD-Image</source>
        <translation type="vanished">CD 이미지</translation>
    </message>
    <message>
        <source>eMule Collection</source>
        <translation type="vanished">eMule 컬렉션</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/DownloadListModel.cpp" line="-6"/>
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
        <location filename="../src/gui/controls/ClientListModel.cpp" line="-1"/>
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
        <location filename="../src/gui/controls/KnownTypeStyle.h" line="+24"/>
        <source>Shared</source>
        <translation>공유됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/gui/controls/UsenetQueueModel.cpp" line="-448"/>
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
        <location filename="../src/gui/panels/StatisticsPanel.cpp" line="+351"/>
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
        <source>%1 Bytes</source>
        <translation type="vanished">%1 Bytes</translation>
    </message>
    <message>
        <location filename="../src/gui/dialogs/OptionsDialog.cpp" line="+3424"/>
        <source>Test</source>
        <translation>테스트</translation>
    </message>
    <message>
        <location filename="../src/gui/utils/FileAssociation.cpp" line="+480"/>
        <source>Could not write the file association to the registry.</source>
        <translation>파일 연결을 레지스트리에 기록하지 못했습니다.</translation>
    </message>
    <message>
        <source>Could not remove the file association from the registry.</source>
        <translation type="vanished">레지스트리에서 파일 연결을 제거하지 못했습니다.</translation>
    </message>
    <message>
        <location line="-75"/>
        <source>Not started from the app bundle.</source>
        <translation>앱 번들에서 시작되지 않았습니다.</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>LaunchServices refused %1.</source>
        <translation>LaunchServices가 %1을(를) 거부했습니다.</translation>
    </message>
    <message>
        <location line="+85"/>
        <source>No writable data directory.</source>
        <translation>쓰기 가능한 데이터 디렉터리가 없습니다.</translation>
    </message>
    <message>
        <location line="+34"/>
        <source>Could not create %1.</source>
        <translation>%1을(를) 만들지 못했습니다.</translation>
    </message>
    <message>
        <location filename="../src/gui/utils/NzbAdd.cpp" line="+25"/>
        <location filename="../src/gui/utils/TorrentAdd.cpp" line="+37"/>
        <source>Could not add &quot;%1&quot;.</source>
        <translation>&quot;%1&quot;을(를) 추가하지 못했습니다.</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+42"/>
        <location line="+10"/>
        <source>Add NZB</source>
        <translation>NZB 추가</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1

Download it again?</source>
        <translation>%1

다시 다운로드할까요?</translation>
    </message>
    <message>
        <location filename="../src/gui/utils/TorrentAdd.cpp" line="-13"/>
        <source>Torrent &quot;%1&quot; added.</source>
        <translation>토렌트 &quot;%1&quot;이(가) 추가되었습니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>&quot;%1&quot; is already in the torrent list.</source>
        <translation>&quot;%1&quot;은(는) 이미 토렌트 목록에 있습니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>&quot;%1&quot; is not a valid torrent or magnet link.</source>
        <translation>&quot;%1&quot;은(는) 올바른 토렌트 또는 마그넷 링크가 아닙니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&quot;%1&quot; is not a valid torrent or magnet link.

%2</source>
        <translation>&quot;%1&quot;은(는) 올바른 토렌트 또는 마그넷 링크가 아닙니다.

%2</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>BitTorrent is off — enable it in Options to add &quot;%1&quot;.</source>
        <translation>BitTorrent가 꺼져 있습니다 — &quot;%1&quot;을(를) 추가하려면 옵션에서 활성화하세요.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Could not add &quot;%1&quot;.

%2</source>
        <translation>&quot;%1&quot;을(를) 추가할 수 없습니다.

%2</translation>
    </message>
    <message>
        <location line="+73"/>
        <source>Add Torrent</source>
        <translation>토렌트 추가</translation>
    </message>
</context>
<context>
    <name>Rating</name>
    <message>
        <location filename="../src/core/utils/OtherFunctions.cpp" line="+528"/>
        <source>Not rated</source>
        <translation>평가 없음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Invalid / Corrupt / Fake</source>
        <translation>잘못됨 / 손상됨 / 가짜</translation>
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
</context>
<context>
    <name>RenameFileDialog</name>
    <message>
        <location filename="../src/gui/dialogs/RenameFileDialog.h" line="+23"/>
        <source>Rename</source>
        <translation>이름 바꾸기</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>File name:</source>
        <translation>파일 이름:</translation>
    </message>
</context>
<context>
    <name>Units</name>
    <message>
        <location filename="../src/core/utils/StringUtils.cpp" line="+72"/>
        <location filename="../src/core/webserver/WebServer.cpp" line="+4087"/>
        <source>Bytes</source>
        <translation>Bytes</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/core/webserver/WebServer.cpp" line="+1"/>
        <source>KB</source>
        <translation>KB</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/core/webserver/WebServer.cpp" line="+1"/>
        <source>MB</source>
        <translation>MB</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/core/webserver/WebServer.cpp" line="+1"/>
        <source>GB</source>
        <translation>GB</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/core/webserver/WebServer.cpp" line="+1"/>
        <source>TB</source>
        <translation>TB</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>B/s</source>
        <translation>B/s</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>KB/s</source>
        <translation>KB/s</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>MB/s</source>
        <translation>MB/s</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>GB/s</source>
        <translation>GB/s</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>TB/s</source>
        <translation>TB/s</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>k</source>
        <translation>k</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>M</source>
        <translation>M</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>G</source>
        <translation>G</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>T</source>
        <translation>T</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>secs</source>
        <translation>초</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>mins</source>
        <translation>분</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>h</source>
        <translation>시</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>d</source>
        <translation>일</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Seconds</source>
        <translation>초</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Minutes</source>
        <translation>분</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hours</source>
        <translation>시간</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Days</source>
        <translation>일</translation>
    </message>
</context>
<context>
    <name>UsenetDetailsDialog</name>
    <message numerus="yes">
        <location filename="../src/gui/dialogs/UsenetDetailsDialog.cpp" line="+66"/>
        <source>%1 (and %n other(s))</source>
        <translation>
            <numerusform>%1 (외 %n개)</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n article(s) missing</source>
        <translation type="vanished">
            <numerusform>기사 %n개 누락</numerusform>
        </translation>
    </message>
    <message>
        <source>Complete</source>
        <translation type="vanished">완료</translation>
    </message>
    <message>
        <source>Downloading</source>
        <translation type="vanished">다운로드 중</translation>
    </message>
    <message>
        <source>Queued</source>
        <translation type="vanished">대기 중</translation>
    </message>
</context>
<context>
    <name>eMule</name>
    <message>
        <source>Unknown</source>
        <translation type="vanished">알 수 없음</translation>
    </message>
    <message>
        <source>Server</source>
        <translation type="vanished">서버</translation>
    </message>
    <message>
        <source>Rating</source>
        <translation type="vanished">평가</translation>
    </message>
    <message>
        <source>Asked for another file</source>
        <translation type="vanished">다른 파일 요청함</translation>
    </message>
    <message>
        <source>Country: %1</source>
        <translation type="vanished">국가: %1</translation>
    </message>
    <message>
        <source>Downloaded:</source>
        <translation type="vanished">다운로드됨:</translation>
    </message>
</context>
<context>
    <name>eMule::AddFriendDialog</name>
    <message>
        <location filename="../src/gui/dialogs/AddFriendDialog.cpp" line="+28"/>
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
        <location line="+6"/>
        <source>Port:</source>
        <translation>포트:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Additional Information</source>
        <translation>추가 정보</translation>
    </message>
    <message>
        <location line="+5"/>
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
        <location line="+5"/>
        <source>ed3k</source>
        <translation>ed3k</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>ed3k link:</source>
        <translation>ed3k 링크:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Request text (optional)</source>
        <translation>요청 문구(선택 사항)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Message:</source>
        <translation>메시지:</translation>
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
        <location line="+102"/>
        <source>Details</source>
        <translation>상세</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>User ID:</source>
        <translation>사용자 ID:</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>You have to enter a valid IP and port!</source>
        <translation>올바른 IP와 포트를 입력해야 합니다!</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Friend not added.

There is already a friend with same IP address and port available.</source>
        <translation>친구를 추가하지 못했습니다.

같은 IP 주소와 포트를 가진 친구가 이미 있습니다.</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>This is not an ed3k friend link.</source>
        <translation>ed3k 친구 링크가 아닙니다.</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Friend not added.

%1</source>
        <translation>친구가 추가되지 않았습니다.

%1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>The ed3k node is not running.</source>
        <translation>ed3k 노드가 실행 중이 아닙니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The ed3k link is invalid.</source>
        <translation>ed3k 링크가 잘못되었습니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The message is too long (at most 96 bytes).</source>
        <translation>메시지가 너무 깁니다(최대 96바이트).</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>This is your own ed3k link.</source>
        <translation>자신의 ed3k 링크입니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Too many ed3k friends (at most 64).</source>
        <translation>ed3k 친구가 너무 많습니다(최대 64명).</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>ed3k friends are switched off.</source>
        <translation>ed3k 친구 기능이 꺼져 있습니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>ed3k error %1.</source>
        <translation>ed3k 오류 %1.</translation>
    </message>
    <message>
        <source>Friend Details</source>
        <translation type="vanished">친구 상세</translation>
    </message>
    <message>
        <location line="-87"/>
        <source>Never</source>
        <translation>안 함</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Close</source>
        <translation>닫기</translation>
    </message>
    <message>
        <location line="+19"/>
        <location line="+24"/>
        <location line="+13"/>
        <location line="+13"/>
        <source>Add Friend</source>
        <translation>친구 추가</translation>
    </message>
    <message>
        <source>Please enter an IP address.</source>
        <translation type="vanished">IP 주소를 입력하세요.</translation>
    </message>
    <message>
        <source>Please enter a valid port (1-65535).</source>
        <translation type="vanished">유효한 포트(1-65535)를 입력하세요.</translation>
    </message>
</context>
<context>
    <name>eMule::AddNzbFilesDialog</name>
    <message>
        <location filename="../src/gui/dialogs/AddNzbFilesDialog.cpp" line="+15"/>
        <source>Add NZB</source>
        <translation>NZB 추가</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>NZB files:</source>
        <translation>NZB 파일:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Add</source>
        <translation>추가</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Password:</source>
        <translation>비밀번호:</translation>
    </message>
    <message>
        <location line="+18"/>
        <location line="+31"/>
        <source>Choose Files…</source>
        <translation>파일 선택…</translation>
    </message>
    <message numerus="yes">
        <location line="-1"/>
        <source>Choose Files… (%n skipped)</source>
        <translation>
            <numerusform>파일 선택… (%n개 건너뜀)</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>eMule::AddNzbUrlDialog</name>
    <message>
        <location filename="../src/gui/dialogs/AddNzbUrlDialog.cpp" line="+18"/>
        <location line="+27"/>
        <location line="+31"/>
        <location line="+74"/>
        <source>Add NZB from URL</source>
        <translation>URL에서 NZB 추가</translation>
    </message>
    <message>
        <location line="-130"/>
        <source>NZB URLs:</source>
        <translation>NZB URL:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paste one or more http(s) links to .nzb files here, one per line...</source>
        <translation>.nzb 파일에 대한 http(s) 링크를 한 줄에 하나씩 여기에 붙여 넣으세요...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Download</source>
        <translation>다운로드</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Password:</source>
        <translation>비밀번호:</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Not Connected</source>
        <translation>연결되지 않음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not connected to the eMule core.</source>
        <translation>eMule 코어에 연결되지 않았습니다.</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Please add at most %1 links at a time.</source>
        <translation>한 번에 최대 %1개의 링크만 추가하세요.</translation>
    </message>
    <message numerus="yes">
        <location line="+23"/>
        <source>Queued %n NZB(s) from URL.</source>
        <translation>
            <numerusform>URL에서 NZB %n개를 대기열에 추가했습니다.</numerusform>
        </translation>
    </message>
    <message>
        <location line="+8"/>
        <source>These links could not be added:

%1</source>
        <translation>다음 링크는 추가할 수 없습니다:

%1</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Could not reach the eMule core.</source>
        <translation>eMule 코어에 연결할 수 없습니다.</translation>
    </message>
    <message numerus="yes">
        <location line="+42"/>
        <source>You have already downloaded %n of these. Download them again?

%1</source>
        <translation>
            <numerusform>이 중 %n개는 이미 다운로드했습니다. 다시 다운로드할까요?

%1</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>eMule::AddSourceDialog</name>
    <message>
        <location filename="../src/gui/dialogs/AddSourceDialog.cpp" line="+23"/>
        <source>Add Sources</source>
        <translation>소스 추가</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add Sources (%1)</source>
        <translation>소스 추가 (%1)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Source Type</source>
        <translation>소스 유형</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Source</source>
        <translation>소스</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>User IP</source>
        <translation>사용자 IP</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Port</source>
        <translation>포트</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+2"/>
        <source>URL</source>
        <translation>URL</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Add</source>
        <translation>추가</translation>
    </message>
</context>
<context>
    <name>eMule::AddTorrentDialog</name>
    <message>
        <location filename="../src/gui/dialogs/AddTorrentDialog.cpp" line="+29"/>
        <source>Add Magnet Links</source>
        <translation>마그넷 링크 추가</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add Torrent Files</source>
        <translation>토렌트 파일 추가</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Magnet links:</source>
        <translation>마그넷 링크:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Torrent files:</source>
        <translation>토렌트 파일:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Paste one or more magnet:?xt=urn:btih:… links here, one per line...</source>
        <translation>magnet:?xt=urn:btih:… 링크를 한 줄에 하나씩 여기에 붙여넣으세요...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Download</source>
        <translation>다운로드</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Save to:</source>
        <translation>저장 위치:</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>Not Connected</source>
        <translation>연결되지 않음</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Not connected to the eMule core.</source>
        <translation>eMule 코어에 연결되지 않았습니다.</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>These lines are not BitTorrent magnet links:

%1</source>
        <translation>다음 줄은 BitTorrent 마그넷 링크가 아닙니다:

%1</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Cannot read:

%1</source>
        <translation>읽을 수 없음:

%1</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Please add at most %1 torrents at a time.</source>
        <translation>한 번에 최대 %1개의 토렌트만 추가하세요.</translation>
    </message>
    <message numerus="yes">
        <location line="+39"/>
        <source>Added %n torrent(s).</source>
        <translation>
            <numerusform>토렌트 %n개를 추가했습니다.</numerusform>
        </translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Could not reach the eMule core.</source>
        <translation>eMule 코어에 연결할 수 없습니다.</translation>
    </message>
</context>
<context>
    <name>eMule::ArchivePreviewPanel</name>
    <message>
        <source>Scanning...</source>
        <translation type="vanished">스캔 중...</translation>
    </message>
    <message>
        <location filename="../src/gui/dialogs/ArchivePreviewPanel.cpp" line="+179"/>
        <location line="+76"/>
        <source>Archive type: --</source>
        <translation>압축 유형: --</translation>
    </message>
    <message>
        <location line="-75"/>
        <location line="+79"/>
        <source>Ready</source>
        <translation>준비됨</translation>
    </message>
    <message>
        <location line="-23"/>
        <source>Archive type: %1</source>
        <translation>압축 유형: %1</translation>
    </message>
    <message>
        <source>No entries found or unsupported format</source>
        <translation type="vanished">항목을 찾을 수 없거나 지원되지 않는 형식</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Files: %1</source>
        <translation>파일: %1</translation>
    </message>
    <message>
        <source>Create Preview Copy</source>
        <translation type="vanished">미리보기 복사본 생성</translation>
    </message>
    <message>
        <location line="-160"/>
        <source>Scanning archive...</source>
        <translation>압축 파일 검사 중...</translation>
    </message>
    <message>
        <location line="+39"/>
        <location line="+35"/>
        <location line="+17"/>
        <source>Password protection</source>
        <translation>암호 보호</translation>
    </message>
    <message>
        <location line="-53"/>
        <source>bootable</source>
        <translation>부팅 가능</translation>
    </message>
    <message>
        <location line="+16"/>
        <location line="+38"/>
        <source>Archive scanned.</source>
        <translation>압축 파일 검사 완료.</translation>
    </message>
    <message>
        <location line="-37"/>
        <location line="+37"/>
        <source>File list may be incomplete.</source>
        <translation>파일 목록이 불완전할 수 있습니다.</translation>
    </message>
    <message>
        <location line="-36"/>
        <location line="+15"/>
        <source>Headers encrypted - unable to read archive.</source>
        <translation>헤더가 암호화되어 압축 파일을 읽을 수 없습니다.</translation>
    </message>
    <message>
        <location line="-14"/>
        <location line="+6"/>
        <source>Insufficient data available.</source>
        <translation>사용 가능한 데이터가 부족합니다.</translation>
    </message>
    <message>
        <location line="-5"/>
        <location line="+14"/>
        <source>Table of contents not found.</source>
        <translation>목차를 찾을 수 없습니다.</translation>
    </message>
    <message>
        <location line="+85"/>
        <source>(unknown/unsupported)</source>
        <translation>(알 수 없음/지원되지 않음)</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Create preview file</source>
        <translation>미리보기 파일 만들기</translation>
    </message>
    <message>
        <location line="+6"/>
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
        <location line="+33"/>
        <source>Info:</source>
        <translation>정보:</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Archive Preview</source>
        <translation>압축 파일 미리보기</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>File attributes can contain:

P	file is password protected
D	is a directory
C	Comment
Lx	Compression level x
M	compressed data not completely downloaded yet
&lt;	file continued from previous volume
&gt;	file continues in next volume
</source>
        <translation>파일 속성에는 다음이 포함될 수 있습니다:

P	암호로 보호된 파일
D	디렉터리
C	코멘트
Lx	압축 수준 x
M	압축된 데이터가 아직 모두 다운로드되지 않음
&lt;	이전 볼륨에서 이어지는 파일
&gt;	다음 볼륨으로 이어지는 파일
</translation>
    </message>
    <message>
        <location line="-202"/>
        <location line="+161"/>
        <source>Comment</source>
        <translation>댓글</translation>
    </message>
</context>
<context>
    <name>eMule::BugReportDialog</name>
    <message>
        <location filename="../src/gui/dialogs/BugReportDialog.cpp" line="+55"/>
        <location line="+139"/>
        <location line="+128"/>
        <source>Submit Bug Report</source>
        <translation>버그 신고 제출</translation>
    </message>
    <message>
        <location line="-262"/>
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
        <location line="+7"/>
        <source>Attachments</source>
        <translation>첨부 파일</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Screenshots:</source>
        <translation>스크린샷:</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Add...</source>
        <translation>추가...</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+14"/>
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
        <location line="+123"/>
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
    <name>eMule::CategoryDialog</name>
    <message>
        <location filename="../src/gui/dialogs/CategoryDialog.cpp" line="+36"/>
        <source>Edit Category-Properties</source>
        <translation>카테고리 속성 편집</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Title</source>
        <translation>제목</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Comments</source>
        <translation>댓글</translation>
    </message>
    <message>
        <location line="+13"/>
        <location line="+79"/>
        <source>Choose a folder for incoming files</source>
        <translation>수신 파일을 저장할 폴더 선택</translation>
    </message>
    <message>
        <location line="-75"/>
        <source>Incoming Files  (Folder will be shared!)</source>
        <translation>수신 파일  (폴더가 공유됩니다!)</translation>
    </message>
    <message>
        <location line="+5"/>
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
        <location line="+4"/>
        <source>Priority for this category</source>
        <translation>이 카테고리의 우선순위</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>More Colors...</source>
        <translation>다른 색...</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+59"/>
        <source>Color</source>
        <translation>색상</translation>
    </message>
    <message>
        <location line="-56"/>
        <source>Auto cat. assignment (separate patterns with |)</source>
        <translation>자동 카테고리 할당 (패턴은 |로 구분)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>As Regular Expression</source>
        <translation>정규식으로 처리</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Shows only the files whose whole name matches. Clearing it turns the filter off again.</source>
        <translation>이름 전체가 일치하는 파일만 표시합니다. 비우면 필터가 다시 꺼집니다.</translation>
    </message>
    <message>
        <source>Used when the tab&apos;s view filter is set to &quot;Regular Expression&quot;; it must match the whole file name.</source>
        <translation type="vanished">탭의 보기 필터가 &quot;정규 표현식&quot;일 때 사용됩니다. 파일 이름 전체와 일치해야 합니다.</translation>
    </message>
    <message>
        <source>Stored for compatibility — the per-category view filter is not implemented yet.</source>
        <translation type="vanished">호환성을 위해 저장됩니다 — 카테고리별 보기 필터는 아직 구현되지 않았습니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Regular expression for view filter:</source>
        <translation>보기 필터 정규식:</translation>
    </message>
    <message>
        <location line="+55"/>
        <source>A category needs a title.</source>
        <translation>카테고리에는 제목이 필요합니다.</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Invalid folder. Folder can not be created. Please check name and location.</source>
        <translation>잘못된 폴더입니다. 폴더를 만들 수 없습니다. 이름과 위치를 확인하세요.</translation>
    </message>
    <message>
        <location line="+10"/>
        <location line="+6"/>
        <source>Bad regular expression</source>
        <translation>잘못된 정규식</translation>
    </message>
    <message>
        <location line="-110"/>
        <location line="+151"/>
        <source>Default</source>
        <translation>기본값</translation>
    </message>
</context>
<context>
    <name>eMule::CategoryTabBar</name>
    <message>
        <location filename="../src/gui/controls/CategoryTabBar.cpp" line="+86"/>
        <location line="+49"/>
        <location line="+63"/>
        <location line="+20"/>
        <location line="+37"/>
        <source>All</source>
        <translation>전체</translation>
    </message>
    <message>
        <location line="-113"/>
        <source>Cat %1</source>
        <translation>카테고리 %1</translation>
    </message>
    <message>
        <location line="+77"/>
        <source>Uncategorized</source>
        <translation>미분류</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Incomplete</source>
        <translation>미완료</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Completed</source>
        <translation>완료됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Waiting</source>
        <translation>대기 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloading</source>
        <translation>다운로드 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Erroneous</source>
        <translation>오류</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paused</source>
        <translation>일시정지됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ever Seen Complete</source>
        <translation>완전한 상태로 본 적 있음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Video</source>
        <translation>비디오</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Audio</source>
        <translation>오디오</translation>
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
        <source>Document</source>
        <translation>문서</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Picture</source>
        <translation>그림</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Program</source>
        <translation>프로그램</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Regular Expression</source>
        <translation>정규 표현식</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Collection</source>
        <translation>컬렉션</translation>
    </message>
    <message>
        <location line="+69"/>
        <source>Select View Filter</source>
        <translation>보기 필터 선택</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Evaluate All Files</source>
        <translation>모든 파일 평가</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Negate Filter</source>
        <translation>필터 반전</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Category</source>
        <translation>카테고리</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Category (%1)</source>
        <translation>카테고리 (%1)</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Open Incoming Folder</source>
        <translation>수신 폴더 열기</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Add Category...</source>
        <translation>카테고리 추가...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Edit Category...</source>
        <translation>카테고리 편집...</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+164"/>
        <source>Remove Category</source>
        <translation>카테고리 제거</translation>
    </message>
    <message>
        <location line="-61"/>
        <source>Could not save categories: %1</source>
        <translation>카테고리를 저장하지 못했습니다: %1</translation>
    </message>
    <message>
        <location line="+62"/>
        <source>Remove the category &quot;%1&quot;?

Its downloads keep their files and move to All.</source>
        <translation>&quot;%1&quot; 카테고리를 제거할까요?

해당 다운로드는 파일을 그대로 유지한 채 전체로 이동합니다.</translation>
    </message>
</context>
<context>
    <name>eMule::ClientDetailDialog</name>
    <message>
        <location filename="../src/gui/dialogs/ClientDetailDialog.cpp" line="+93"/>
        <location line="+20"/>
        <source>Client Details: %1</source>
        <translation>클라이언트 상세: %1</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>General</source>
        <translation>일반</translation>
    </message>
    <message>
        <source>User Name</source>
        <translation type="vanished">사용자 이름</translation>
    </message>
    <message>
        <source>User Hash</source>
        <translation type="vanished">사용자 해시</translation>
    </message>
    <message>
        <location line="+12"/>
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
        <source>Client Software</source>
        <translation type="vanished">클라이언트 소프트웨어</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Country</source>
        <translation>국가</translation>
    </message>
    <message>
        <source>Server</source>
        <translation type="vanished">서버</translation>
    </message>
    <message>
        <location line="-18"/>
        <source>Name</source>
        <translation>이름</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Hash</source>
        <translation>해시</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Software</source>
        <translation>소프트웨어</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>Server IP</source>
        <translation>서버 IP</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server Name</source>
        <translation>서버 이름</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Identification</source>
        <translation>식별</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Successful</source>
        <translation>성공</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Invalid</source>
        <translation>잘못됨</translation>
    </message>
    <message>
        <location line="-23"/>
        <location line="+24"/>
        <source>Not supported or disabled</source>
        <translation>지원되지 않거나 비활성화됨</translation>
    </message>
    <message>
        <location line="-27"/>
        <source>Obfuscation</source>
        <translation>난독화</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Enabled</source>
        <translation>활성화됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Supported</source>
        <translation>지원됨</translation>
    </message>
    <message>
        <location line="+31"/>
        <location line="+2"/>
        <source>Connected (%1)</source>
        <translation>연결됨 (%1)</translation>
    </message>
    <message>
        <location line="-2"/>
        <location line="+8"/>
        <source>QUIC</source>
        <translation>QUIC</translation>
    </message>
    <message>
        <location line="-6"/>
        <location line="+4"/>
        <source>IPv6 fallback</source>
        <translation>IPv6 대체 경로</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Not supported</source>
        <translation>지원 안 함</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Supported (%1)</source>
        <translation>지원 (%1)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>NAT-T</source>
        <translation>NAT-T</translation>
    </message>
    <message>
        <location line="-48"/>
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
        <source>Disconnected</source>
        <translation>연결 해제됨</translation>
    </message>
    <message>
        <location line="+66"/>
        <source>Currently downloading</source>
        <translation>현재 다운로드 중</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Requested to upload</source>
        <translation>업로드 요청됨</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Downloaded (this session)</source>
        <translation>다운로드됨 (이번 세션)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Uploaded (this session)</source>
        <translation>업로드됨 (이번 세션)</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Average Downloadrate</source>
        <translation>평균 다운로드 속도</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Average Upload rate</source>
        <translation>평균 업로드 속도</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Downloaded total</source>
        <translation>총 다운로드</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Uploaded total</source>
        <translation>총 업로드</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Rating (total)</source>
        <translation>평가 (전체)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>(Friend Slot)</source>
        <translation>(친구 슬롯)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Uploadqueue Score</source>
        <translation>업로드 대기열 점수</translation>
    </message>
    <message>
        <source>Not connected</source>
        <translation type="vanished">연결되지 않음</translation>
    </message>
    <message>
        <location line="-66"/>
        <source>Transfer</source>
        <translation>전송</translation>
    </message>
    <message>
        <source>Currently Downloading</source>
        <translation type="vanished">현재 다운로드 중</translation>
    </message>
    <message>
        <source>Currently Uploading</source>
        <translation type="vanished">현재 업로드 중</translation>
    </message>
    <message>
        <source>Downloaded (Session)</source>
        <translation type="vanished">다운로드됨 (세션)</translation>
    </message>
    <message>
        <source>Uploaded (Session)</source>
        <translation type="vanished">업로드됨 (세션)</translation>
    </message>
    <message>
        <source>Download Rate</source>
        <translation type="vanished">다운로드 속도</translation>
    </message>
    <message>
        <source>Downloaded (Total)</source>
        <translation type="vanished">다운로드됨 (전체)</translation>
    </message>
    <message>
        <source>Uploaded (Total)</source>
        <translation type="vanished">업로드됨 (전체)</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Scores</source>
        <translation>점수</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>DL/UP Modifier</source>
        <translation>DL/UP 보정값</translation>
    </message>
    <message>
        <source>Rating (Total)</source>
        <translation type="vanished">평가 (전체)</translation>
    </message>
    <message>
        <source>Upload Queue Score</source>
        <translation type="vanished">업로드 대기열 점수</translation>
    </message>
    <message>
        <source>Friend Slot</source>
        <translation type="vanished">친구 슬롯</translation>
    </message>
    <message>
        <source>Yes</source>
        <translation type="vanished">예</translation>
    </message>
</context>
<context>
    <name>eMule::ClientListModel</name>
    <message>
        <location filename="../src/gui/controls/ClientListModel.cpp" line="+330"/>
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
        <source>Transferred</source>
        <translation>전송됨</translation>
    </message>
    <message>
        <location line="+1"/>
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
        <location line="+2"/>
        <location line="+24"/>
        <source>Transferred Up</source>
        <translation>업로드됨</translation>
    </message>
    <message>
        <location line="-23"/>
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
        <location line="+21"/>
        <source>Low ID, %1</source>
        <translation>Low ID, %1</translation>
    </message>
    <message>
        <location line="-47"/>
        <location line="+27"/>
        <source>Transferred Down</source>
        <translation>다운로드됨</translation>
    </message>
    <message>
        <location line="-308"/>
        <source>%1 (ed3k)</source>
        <translation>%1 (ed3k)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>place %1</source>
        <translation>%1위</translation>
    </message>
    <message>
        <location line="+51"/>
        <source>Country</source>
        <translation>국가</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Shared List</source>
        <translation>공유 목록</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Uploaded</source>
        <translation>업로드됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloaded</source>
        <translation>다운로드됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ratio</source>
        <translation>비율</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Session Ratio</source>
        <translation>세션 비율</translation>
    </message>
    <message>
        <location line="+263"/>
        <source>QUIC</source>
        <translation>QUIC</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>IPv6</source>
        <translation>IPv6</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Low ID</source>
        <translation>Low ID</translation>
    </message>
    <message>
        <location line="-319"/>
        <location line="+162"/>
        <location line="+15"/>
        <source>Yes</source>
        <translation>예</translation>
    </message>
    <message>
        <location line="-177"/>
        <location line="+162"/>
        <location line="+15"/>
        <source>No</source>
        <translation>아니오</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Unknown</source>
        <translation>알 수 없음</translation>
    </message>
    <message>
        <location line="+125"/>
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
        <source>Shared Files — %1</source>
        <translation type="vanished">공유 파일 — %1</translation>
    </message>
    <message>
        <source>File Name</source>
        <translation type="vanished">파일 이름</translation>
    </message>
    <message>
        <source>Size</source>
        <translation type="vanished">크기</translation>
    </message>
    <message>
        <source>Hash</source>
        <translation type="vanished">해시</translation>
    </message>
    <message>
        <source>Download Selected</source>
        <translation type="vanished">선택 항목 다운로드</translation>
    </message>
    <message>
        <source>Close</source>
        <translation type="vanished">닫기</translation>
    </message>
</context>
<context>
    <name>eMule::ClientState</name>
    <message>
        <location filename="../src/gui/utils/ClientStateText.h" line="+33"/>
        <source>Via ed3k</source>
        <translation>ed3k 경유</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>Unknown</source>
        <translation>알 수 없음</translation>
    </message>
</context>
<context>
    <name>eMule::CollectionCreateDialog</name>
    <message>
        <location filename="../src/gui/dialogs/CollectionCreateDialog.cpp" line="+57"/>
        <source>Modify Collection...</source>
        <translation>컬렉션 수정...</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Create Collection...</source>
        <translation>컬렉션 만들기...</translation>
    </message>
    <message>
        <source>Shared (0)</source>
        <translation type="vanished">공유됨 (0)</translation>
    </message>
    <message>
        <location line="+20"/>
        <location line="+36"/>
        <source>File Name</source>
        <translation>파일 이름</translation>
    </message>
    <message>
        <location line="-36"/>
        <location line="+36"/>
        <source>Size</source>
        <translation>크기</translation>
    </message>
    <message>
        <location line="-36"/>
        <location line="+36"/>
        <source>Hash</source>
        <translation>해시</translation>
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
        <location line="-95"/>
        <location line="+197"/>
        <source>Shared (%1)</source>
        <translation>공유됨 (%1)</translation>
    </message>
    <message>
        <location line="-195"/>
        <source>Switch between the shared files and every file known</source>
        <translation>공유 파일과 알려진 모든 파일 사이를 전환</translation>
    </message>
    <message>
        <location line="+195"/>
        <source>Known (%1)</source>
        <translation>알려진 파일 (%1)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Collection List (%1)</source>
        <translation>컬렉션 목록 (%1)</translation>
    </message>
    <message>
        <location line="+26"/>
        <location line="+6"/>
        <location line="+42"/>
        <location line="+9"/>
        <source>Collection</source>
        <translation>컬렉션</translation>
    </message>
    <message>
        <location line="-57"/>
        <source>Please enter a collection name.</source>
        <translation>컬렉션 이름을 입력하세요.</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Collection is empty. Add files first.</source>
        <translation>컬렉션이 비어 있습니다. 파일을 먼저 추가하세요.</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Do you want to replace existing file?</source>
        <translation>기존 파일을 바꾸시겠습니까?</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Failed to save collection: %1</source>
        <translation>컬렉션을 저장하지 못했습니다: %1</translation>
    </message>
</context>
<context>
    <name>eMule::CollectionViewDialog</name>
    <message>
        <location filename="../src/gui/dialogs/CollectionViewDialog.cpp" line="+43"/>
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
        <location line="+29"/>
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
        <location line="+4"/>
        <source>Add new downloads into the collection category</source>
        <translation>새 다운로드를 컬렉션 카테고리에 추가</translation>
    </message>
    <message>
        <source>Add to new category</source>
        <translation type="vanished">새 카테고리에 추가</translation>
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
    <name>eMule::CommentEditPanel</name>
    <message>
        <location filename="../src/gui/dialogs/CommentEditPanel.cpp" line="+70"/>
        <source>Comment This File! (This text will be shown to all users.)</source>
        <translation>이 파일에 댓글을 남겨 주세요! (이 텍스트는 모든 사용자에게 표시됩니다.)</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>For a film, you can say its length, its story, the language... And if it is a fake, you can inform other eMule users...</source>
        <translation>영화라면 상영 시간, 줄거리, 언어 등을 적을 수 있습니다... 가짜라면 다른 eMule 사용자에게 알릴 수도 있습니다...</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>File Quality</source>
        <translation>파일 품질</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Choose the file rating or advice users if the file is invalid!</source>
        <translation>파일 평가를 선택하거나, 파일이 잘못되었다면 다른 사용자에게 알려 주세요!</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Reset</source>
        <translation>초기화</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Apply</source>
        <translation>적용</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Comment</source>
        <translation>댓글</translation>
    </message>
</context>
<context>
    <name>eMule::CommentsPanel</name>
    <message>
        <location filename="../src/gui/dialogs/CommentsPanel.cpp" line="+61"/>
        <source>Kad</source>
        <translation>Kad</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>(Kad search in progress...)</source>
        <translation>(Kad 검색 중...)</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+55"/>
        <source>Search Kad</source>
        <translation>Kad 검색</translation>
    </message>
    <message>
        <location line="-37"/>
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
        <location line="+12"/>
        <source>No comments or ratings available for this file.</source>
        <translation>이 파일에 대한 댓글이나 평가가 없습니다.</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Copy</source>
        <translation>복사</translation>
    </message>
    <message>
        <location line="+11"/>
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
    <name>eMule::ConfidenceText</name>
    <message>
        <location filename="../src/core/search/ConfidenceText.h" line="+27"/>
        <source>Spam</source>
        <translation>스팸</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Likely fake</source>
        <translation>가짜일 가능성 높음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Suspect</source>
        <translation>의심스러움</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Caution: %1%</source>
        <translation>주의: %1%</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Looks good</source>
        <translation>문제 없어 보임</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Genuine</source>
        <translation>진짜</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Shared under names that describe different content</source>
        <translation>서로 다른 내용을 가리키는 이름들로 공유됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Shared as different kinds of file (e.g. video and archive)</source>
        <translation>서로 다른 종류의 파일로 공유됨 (예: 동영상과 압축 파일)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>A name matches a rule in FakeFileFilter.dat</source>
        <translation>이름이 FakeFileFilter.dat의 규칙과 일치함</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>A comment matches a rule in FakeFileFilter.dat</source>
        <translation>댓글이 FakeFileFilter.dat의 규칙과 일치함</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The first bytes are not what the extension claims</source>
        <translation>첫 바이트가 확장자가 나타내는 형식과 다름</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>It is a program named like a media file</source>
        <translation>미디어 파일처럼 이름 붙인 프로그램임</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>It is an archive named like a media file</source>
        <translation>미디어 파일처럼 이름 붙인 압축 파일임</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The published type does not fit the extension</source>
        <translation>게시된 유형이 확장자와 맞지 않음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The spam filter rates it high</source>
        <translation>스팸 필터 점수가 높음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Counted as spam</source>
        <translation>스팸으로 분류됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Rated poor, or a Kad note calls it a fake</source>
        <translation>평가가 나쁘거나 Kad 노트에서 가짜라고 함</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Rated as fake by users</source>
        <translation>사용자들이 가짜로 평가함</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Sources disagree on the AICH hash</source>
        <translation>소스마다 AICH 해시가 다름</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The length does not fit a file of this size</source>
        <translation>재생 시간이 이 크기의 파일과 맞지 않음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The bitrate is not plausible</source>
        <translation>비트레이트가 비정상적임</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Length and bitrate do not add up to the size</source>
        <translation>재생 시간과 비트레이트가 파일 크기와 맞지 않음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Artist, album and title appear in none of the names</source>
        <translation>아티스트, 앨범, 제목이 어느 이름에도 없음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>A name uses words that advertise child abuse material</source>
        <translation>이름에 아동 학대물을 광고하는 단어가 있습니다</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The release is password protected</source>
        <translation>릴리스가 암호로 보호되어 있습니다</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>%1 (fake score %2 of 100)</source>
        <translation>%1 (가짜 점수 %2 / 100)</translation>
    </message>
</context>
<context>
    <name>eMule::ContactsGraph</name>
    <message>
        <location filename="../src/gui/controls/ContactsGraph.cpp" line="+88"/>
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
        <location filename="../src/gui/dialogs/DetailDialog.cpp" line="+204"/>
        <source>Rename</source>
        <translation>이름 바꾸기</translation>
    </message>
    <message>
        <location line="+16"/>
        <location line="+7"/>
        <source>Archive Preview</source>
        <translation>압축 파일 미리보기</translation>
    </message>
    <message>
        <location line="-6"/>
        <source>A preview file can only be opened when the core runs on this computer.</source>
        <translation>미리보기 파일은 코어가 이 컴퓨터에서 실행 중일 때만 열 수 있습니다.</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Search Kad</source>
        <translation>Kad 검색</translation>
    </message>
    <message>
        <location line="+203"/>
        <source>Comments</source>
        <translation>댓글</translation>
    </message>
    <message>
        <location line="+37"/>
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
        <location filename="../src/gui/controls/DownloadListModel.cpp" line="+817"/>
        <source>Downloading</source>
        <translation>다운로드 중</translation>
    </message>
    <message>
        <source>Auto [%1]</source>
        <translation type="vanished">자동 [%1]</translation>
    </message>
    <message>
        <location line="-538"/>
        <source>Queue Full</source>
        <translation>대기열 가득 참</translation>
    </message>
    <message>
        <source>Available parts: %1 / %2</source>
        <translation type="vanished">사용 가능한 파트: %1 / %2</translation>
    </message>
    <message>
        <location line="-145"/>
        <source>Country: %1</source>
        <translation>국가: %1</translation>
    </message>
    <message>
        <location line="+272"/>
        <location line="+2"/>
        <source>Never</source>
        <translation>안 함</translation>
    </message>
    <message>
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
        <translation type="vanished">파일 이름:	%1
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
        <location line="+36"/>
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
Uploaded:	%12</source>
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
업로드됨:	%12</translation>
    </message>
    <message>
        <location line="+100"/>
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
        <source>Country</source>
        <translation>국가</translation>
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
        <location line="+1"/>
        <source>Confidence</source>
        <translation>신뢰도</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Transferred</source>
        <translation>전송됨</translation>
    </message>
    <message>
        <location line="+229"/>
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
    <message>
        <location line="+28"/>
        <source>HTTP Cache</source>
        <translation>HTTP 캐시</translation>
    </message>
    <message>
        <location line="-723"/>
        <location line="+725"/>
        <source>Asked for another file</source>
        <translation>다른 파일 요청함</translation>
    </message>
    <message>
        <location line="-747"/>
        <source>Available parts: %1</source>
        <translation>사용 가능한 파트: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>User Name: %1</source>
        <translation>사용자 이름: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unknown</source>
        <translation>알 수 없음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server</source>
        <translation>서버</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Next re-ask</source>
        <translation>다음 재요청</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Asked: %1 times; Available parts: %2</source>
        <translation>요청: %1회; 사용 가능한 파트: %2</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Client&apos;s file name:</source>
        <translation>클라이언트의 파일 이름:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>File comment:</source>
        <translation>파일 코멘트:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Rating</source>
        <translation>평가</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>A4AF files</source>
        <translation>A4AF 파일</translation>
    </message>
    <message>
        <location line="+264"/>
        <source>%1 (ed3k)</source>
        <translation>%1 (ed3k)</translation>
    </message>
    <message>
        <location line="+461"/>
        <source>Swap blocked</source>
        <translation>전환 차단됨</translation>
    </message>
</context>
<context>
    <name>eMule::Ed3kFilterPage</name>
    <message>
        <location filename="../src/gui/dialogs/Ed3kFilterPage.cpp" line="+76"/>
        <location line="+100"/>
        <source>Changes on this page take effect at once.</source>
        <translation>이 페이지의 변경 사항은 즉시 적용됩니다.</translation>
    </message>
    <message>
        <location line="-95"/>
        <source>Followed filter lists</source>
        <translation>팔로우 중인 필터 목록</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>A list marks fake, harmful or spam files in search results; whether it also hides them or refuses their download is set per list.</source>
        <translation>목록은 검색 결과에서 가짜, 유해 또는 스팸 파일을 표시합니다. 숨기거나 다운로드를 거부할지는 목록마다 설정합니다.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Label</source>
        <translation>레이블</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+246"/>
        <source>Mode</source>
        <translation>모드</translation>
    </message>
    <message>
        <location line="-246"/>
        <source>Version</source>
        <translation>버전</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Files</source>
        <translation>파일</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Entries</source>
        <translation>항목</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Waiting</source>
        <translation>대기 중</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Follow…</source>
        <translation>팔로우…</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+204"/>
        <source>Unfollow</source>
        <translation>팔로우 해제</translation>
    </message>
    <message>
        <location line="-203"/>
        <source>Open list file…</source>
        <translation>목록 파일 열기…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Uses a list file (.ed3kflt) by hand: it has no author and gets no updates.</source>
        <translation>목록 파일(.ed3kflt)을 수동으로 사용합니다: 작성자가 없고 업데이트도 받지 않습니다.</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Mode of the selected list:</source>
        <translation>선택한 목록의 모드:</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Own verdicts (they overrule every list)</source>
        <translation>자체 판정 (모든 목록보다 우선)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Entry</source>
        <translation>항목</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>ID</source>
        <translation>ID</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Verdict</source>
        <translation>판정</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Reason</source>
        <translation>이유</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Add…</source>
        <translation>추가…</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+282"/>
        <source>Remove</source>
        <translation>제거</translation>
    </message>
    <message>
        <location line="-277"/>
        <source>Mode:</source>
        <translation>모드:</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+88"/>
        <source>Publish my list…</source>
        <translation>내 목록 게시…</translation>
    </message>
    <message>
        <location line="-87"/>
        <source>Shares your own verdicts as a filter list others can follow; published again on every change.</source>
        <translation>내 판정을 다른 사람이 팔로우할 수 있는 필터 목록으로 공유합니다. 변경될 때마다 다시 게시됩니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+291"/>
        <source>Stop publishing</source>
        <translation>게시 중지</translation>
    </message>
    <message>
        <location line="-288"/>
        <source>not published</source>
        <translation>게시되지 않음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Copy link</source>
        <translation>링크 복사</translation>
    </message>
    <message>
        <location line="+44"/>
        <source>The ed3k node is not running: filter lists can be changed while it runs.</source>
        <translation>ed3k 노드가 실행 중이 아닙니다: 필터 목록은 실행 중일 때만 변경할 수 있습니다.</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>%1 (opened by hand: no author, no updates)</source>
        <translation>%1 (수동으로 열림: 작성자 없음, 업데이트 없음)</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>none yet</source>
        <translation>아직 없음</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Publish again…</source>
        <translation>다시 게시…</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Fake</source>
        <translation>가짜</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Harmful</source>
        <translation>유해</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Spam</source>
        <translation>스팸</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Good</source>
        <translation>양호</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>None</source>
        <translation>없음</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>File (ed3k root)</source>
        <translation>파일 (ed3k 루트)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>File (eD2K hash)</source>
        <translation>파일 (eD2K 해시)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Author (node ID)</source>
        <translation>작성자 (Node ID)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Hide and refuse downloads</source>
        <translation>숨기고 다운로드 거부</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Hide</source>
        <translation>숨기기</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Mark only</source>
        <translation>표시만</translation>
    </message>
    <message>
        <location line="+22"/>
        <location line="+4"/>
        <location line="+8"/>
        <source>Follow filter list</source>
        <translation>필터 목록 팔로우</translation>
    </message>
    <message>
        <location line="-12"/>
        <source>Link of the filter list (ed3k:set?…):</source>
        <translation>필터 목록 링크 (ed3k:set?…):</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Label:</source>
        <translation>레이블:</translation>
    </message>
    <message>
        <location line="+29"/>
        <location line="+7"/>
        <location line="+8"/>
        <source>Open filter list file</source>
        <translation>필터 목록 파일 열기</translation>
    </message>
    <message>
        <location line="-14"/>
        <source>Filter lists (*.ed3kflt);;All files (*)</source>
        <translation>필터 목록 (*.ed3kflt);;모든 파일 (*)</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Could not read %1 (at most 4 MiB).</source>
        <translation>%1을(를) 읽을 수 없습니다 (최대 4 MiB).</translation>
    </message>
    <message>
        <location line="+32"/>
        <location line="+28"/>
        <location line="+11"/>
        <source>Own verdict</source>
        <translation>자체 판정</translation>
    </message>
    <message>
        <location line="-34"/>
        <source>Entry:</source>
        <translation>항목:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>hexadecimal: a root (64 digits), an eD2K hash or a node ID (32 digits)</source>
        <translation>16진수: 루트(64자리), eD2K 해시 또는 Node ID(32자리)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>ID:</source>
        <translation>ID:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Verdict:</source>
        <translation>판정:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Reason:</source>
        <translation>이유:</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>The ID is 32 hexadecimal digits (a root: 64).</source>
        <translation>ID는 16진수 32자리입니다 (루트: 64).</translation>
    </message>
    <message>
        <location line="+38"/>
        <location line="+11"/>
        <location line="+6"/>
        <source>Publish my list</source>
        <translation>내 목록 게시</translation>
    </message>
    <message>
        <location line="-16"/>
        <source>Title of your filter list (followers see it):</source>
        <translation>필터 목록 제목 (팔로워에게 표시):</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>My filter list</source>
        <translation>내 필터 목록</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Your filter list is published. Its link is in the clipboard: whoever follows it sees your verdicts.</source>
        <translation>필터 목록이 게시되었습니다. 링크가 클립보드에 있습니다. 팔로우하는 사람은 내 판정을 보게 됩니다.</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>the ed3k node is not running</source>
        <translation>ed3k 노드가 실행 중이 아님</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>error %1</source>
        <translation>오류 %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>no answer from the core</source>
        <translation>코어 응답 없음</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>ed3k</source>
        <translation>ed3k</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>%1 failed: %2</source>
        <translation>%1 실패: %2</translation>
    </message>
</context>
<context>
    <name>eMule::Ed3kNameOpener</name>
    <message>
        <location filename="../src/gui/app/Ed3kNameOpener.cpp" line="+78"/>
        <source>yours</source>
        <translation>내 것</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>pinned on %1</source>
        <translation>%1에 고정됨</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>a name list</source>
        <translation>이름 목록</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>the list &quot;%1&quot;</source>
        <translation>목록 &quot;%1&quot;</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>%1 of %2 friends</source>
        <translation>친구 %2명 중 %1명</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>network, %1 sites</source>
        <translation>네트워크, 사이트 %1개</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>domain-verified</source>
        <translation>도메인 검증됨</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>DNS, unverified</source>
        <translation>DNS, 검증 안 됨</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>key %1</source>
        <translation>키 %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>unknown</source>
        <translation>알 수 없음</translation>
    </message>
    <message>
        <location line="+34"/>
        <source>The name does not exist in its zone.</source>
        <translation>이 이름은 해당 영역에 존재하지 않습니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>No source names a zone for it.</source>
        <translation>이 이름의 영역을 알려 주는 소스가 없습니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>It passes too many zones.</source>
        <translation>거치는 영역이 너무 많습니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Neither DNS nor the web server names a usable link.</source>
        <translation>DNS와 웹 서버 모두 사용 가능한 링크를 알려 주지 않습니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>It could not be resolved.</source>
        <translation>확인할 수 없었습니다.</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+30"/>
        <source>Sources tie between the zones %1.</source>
        <translation>소스 결과가 영역 %1 사이에서 동률입니다.</translation>
    </message>
    <message>
        <location line="-28"/>
        <location line="+35"/>
        <source>ed3k name</source>
        <translation>ed3k 이름</translation>
    </message>
    <message>
        <location line="-35"/>
        <source>%1 could not be opened.

%2</source>
        <translation>%1을(를) 열 수 없습니다.

%2</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>%1 is the zone %2 (%3).</source>
        <translation>%1은(는) 영역 %2 (%3)입니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Only the network knows this name: no friend and no list you follow knows it.</source>
        <translation>이 이름은 네트워크만 알고 있습니다. 친구도, 팔로우 중인 목록도 알지 못합니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>It comes from DNS only and is not verified by the domain.</source>
        <translation>DNS에서만 가져온 것으로 도메인에서 검증되지 않았습니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The domain&apos;s DNS record and its ed3k.json name different keys; ed3k.json decides.</source>
        <translation>도메인의 DNS 레코드와 ed3k.json이 서로 다른 키를 가리킵니다. ed3k.json이 우선합니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Warning: %1 names another zone, %2, than %3.</source>
        <translation>경고: %1은(는) %3과(와) 다른 영역 %2을(를) 가리킵니다.</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Your friends learn the names you open.</source>
        <translation>내가 연 이름은 친구에게 알려집니다.</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Open it?</source>
        <translation>여시겠습니까?</translation>
    </message>
    <message>
        <location line="+49"/>
        <source>ed3k name list</source>
        <translation>ed3k 이름 목록</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Your ed3k friend %1 publishes a name list. Follow it?

Its names are then resolved after the lists you already follow.</source>
        <translation>ed3k 친구 %1이(가) 이름 목록을 게시합니다. 팔로우하시겠습니까?

그 목록의 이름은 이미 팔로우 중인 목록 다음에 확인됩니다.</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Friend %1</source>
        <translation>친구 %1</translation>
    </message>
</context>
<context>
    <name>eMule::Ed3kOptionsPages</name>
    <message>
        <source>The ed3k node uses the nickname of the General page: %1</source>
        <translation type="vanished">ed3k 노드는 일반 페이지의 닉네임을 사용합니다: %1</translation>
    </message>
    <message>
        <location filename="../src/gui/dialogs/Ed3kOptionsPages.cpp" line="+352"/>
        <location line="+11"/>
        <location line="+499"/>
        <location line="+11"/>
        <location line="+23"/>
        <location line="+19"/>
        <location line="+3"/>
        <location line="+21"/>
        <location line="+24"/>
        <source>ed3k credits</source>
        <translation>ed3k 크레딧</translation>
    </message>
    <message>
        <location line="-610"/>
        <source>The ledger contract is an address: 0x and 40 hexadecimal digits.</source>
        <translation>원장 컨트랙트는 주소입니다: 0x와 16진수 40자리.</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>The chain ID is a whole number.</source>
        <translation>chain ID는 정수입니다.</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Enable the ed3k node</source>
        <translation>ed3k 노드 사용</translation>
    </message>
    <message>
        <source>Runs the ed3k node (packmule) in the core, on a UDP port of its own. It finds other nodes over Kad and serves the shared files.</source>
        <translation type="vanished">코어에서 ed3k 노드(packmule)를 전용 UDP 포트로 실행합니다. Kad로 다른 노드를 찾고 공유 파일을 제공합니다.</translation>
    </message>
    <message>
        <source>Identity</source>
        <translation type="vanished">신원</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Sharing</source>
        <translation>공유</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Share the shared files over ed3k too</source>
        <translation>공유 파일을 ed3k로도 공유</translation>
    </message>
    <message>
        <source>Hold Usenet posts back</source>
        <translation type="vanished">Usenet 게시물 보류</translation>
    </message>
    <message>
        <source>Share the files of Usenet downloads, but not their article lists, post sets or nzb records.</source>
        <translation type="vanished">Usenet 다운로드의 파일은 공유하지만, 기사 목록, 게시물 세트, nzb 레코드는 공유하지 않습니다.</translation>
    </message>
    <message>
        <source>Extensions</source>
        <translation type="vanished">확장 기능</translation>
    </message>
    <message>
        <source>Optional parts of the ed3k protocol. A change restarts the node.</source>
        <translation type="vanished">ed3k 프로토콜의 선택적 부분입니다. 변경하면 노드가 다시 시작됩니다.</translation>
    </message>
    <message>
        <location line="+64"/>
        <source>Ports</source>
        <translation>포트</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+23"/>
        <source>Random</source>
        <translation>무작위</translation>
    </message>
    <message>
        <source>UDP port of the ed3k node, not the eD2K UDP port. A change restarts the node.</source>
        <translation type="vanished">ed3k 노드의 UDP 포트입니다(eD2K UDP 포트가 아님). 변경하면 노드가 다시 시작됩니다.</translation>
    </message>
    <message>
        <location line="-15"/>
        <source>Test Ports</source>
        <translation>포트 테스트</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Asks emule-qt.org whether the UDP port the node uses now is reachable. ed3k needs only UDP. Apply a changed port first.</source>
        <translation>노드가 현재 사용하는 UDP 포트에 접근 가능한지 emule-qt.org에 확인합니다. ed3k는 UDP만 필요합니다. 포트를 변경했다면 먼저 적용하세요.</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>UDP port:</source>
        <translation>UDP 포트:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>TCP port of the local website server (this computer only). It is part of every site&apos;s address: a change loses what sites stored in the browser.</source>
        <translation>로컬 웹사이트 서버의 TCP 포트입니다(이 컴퓨터 전용). 모든 사이트 주소의 일부이므로 변경하면 사이트가 브라우저에 저장한 내용이 사라집니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Website server port:</source>
        <translation>웹사이트 서버 포트:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Finding nodes</source>
        <translation>노드 찾기</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Find ed3k nodes in the local network (restarts the node)</source>
        <translation>로컬 네트워크에서 ed3k 노드 찾기(노드 다시 시작)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Node lists for the first contact, one ed3k:nodes link per line:</source>
        <translation>첫 연결용 노드 목록, 한 줄에 ed3k:nodes 링크 하나:</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Chaintube</source>
        <translation>Chaintube</translation>
    </message>
    <message>
        <source>JSON-RPC endpoints of the chaintube chain, one URL per line (https, or http to a node on this computer). The ledger of the credits is on the Credits page.</source>
        <translation type="vanished">chaintube 체인의 JSON-RPC 엔드포인트, 한 줄에 URL 하나(https, 또는 이 컴퓨터의 노드에 대해서는 http). 크레딧 원장은 크레딧 페이지에 있습니다.</translation>
    </message>
    <message>
        <location line="+73"/>
        <source>Use credits (off by default)</source>
        <translation>크레딧 사용(기본값: 꺼짐)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Earn credits for uploading, and spend them on priority upload slots at others.</source>
        <translation>업로드로 크레딧을 얻고, 다른 사용자의 우선 업로드 슬롯에 사용합니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Ledger of the network</source>
        <translation>네트워크 원장</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>assigned at deployment</source>
        <translation>배포 시 할당</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>There is no default: take it from the network you are in (credit.md §7.1).</source>
        <translation>기본값이 없습니다. 참여 중인 네트워크에서 가져오십시오(credit.md §7.1).</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Chain ID:</source>
        <translation>Chain ID:</translation>
    </message>
    <message>
        <location line="-429"/>
        <source>Not valid</source>
        <translation>유효하지 않음</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Premium until %1</source>
        <translation>%1까지 프리미엄</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Free until %1</source>
        <translation>%1까지 무료</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Premium</source>
        <translation>프리미엄</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Free</source>
        <translation>무료</translation>
    </message>
    <message>
        <source>A contract is an address: 0x and 40 hexadecimal digits.</source>
        <translation type="vanished">컨트랙트는 주소입니다: 0x와 16진수 40자리.</translation>
    </message>
    <message>
        <source>The content contract of the network. No default.</source>
        <translation type="vanished">네트워크의 콘텐츠 컨트랙트. 기본값 없음.</translation>
    </message>
    <message>
        <source>The comment contract of the network. No default.</source>
        <translation type="vanished">네트워크의 댓글 컨트랙트. 기본값 없음.</translation>
    </message>
    <message>
        <source>The vote contract of the network. No default.</source>
        <translation type="vanished">네트워크의 투표 컨트랙트. 기본값 없음.</translation>
    </message>
    <message>
        <source>The forwarder of requests a site sends; empty: none.</source>
        <translation type="vanished">사이트가 보내는 요청의 포워더. 비어 있으면 없음.</translation>
    </message>
    <message>
        <source>Content contract:</source>
        <translation type="vanished">콘텐츠 컨트랙트:</translation>
    </message>
    <message>
        <source>Comment contract:</source>
        <translation type="vanished">댓글 컨트랙트:</translation>
    </message>
    <message>
        <source>Vote contract:</source>
        <translation type="vanished">투표 컨트랙트:</translation>
    </message>
    <message>
        <source>Forwarder:</source>
        <translation type="vanished">포워더:</translation>
    </message>
    <message>
        <location line="+233"/>
        <source>Share Usenet posts over ed3k too</source>
        <translation>Usenet 게시물도 ed3k로 공유</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Off: the files of Usenet downloads are still shared, but not their article lists, post sets or nzb records.</source>
        <translation>끄기: Usenet 다운로드의 파일은 계속 공유되지만 기사 목록, 게시물 세트, nzb 레코드는 공유되지 않습니다.</translation>
    </message>
    <message>
        <location line="+195"/>
        <source>The ledger contract of the network: 0x and 40 hexadecimal digits. No default.</source>
        <translation>네트워크의 원장 컨트랙트: 0x와 16진수 40자리. 기본값 없음.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ledger contract:</source>
        <translation>원장 컨트랙트:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>JSON-RPC endpoints of the ledger, one URL per line, best of different operators. They go through the proxy you configured. Without one, receipts are signed and kept, nothing is sold or settled.</source>
        <translation>원장의 JSON-RPC 엔드포인트, 한 줄에 URL 하나, 서로 다른 운영자의 것이 가장 좋습니다. 설정한 프록시를 거칩니다. 엔드포인트가 없으면 영수증은 서명되어 보관되지만, 아무것도 판매되거나 정산되지 않습니다.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>RPC endpoints:</source>
        <translation>RPC 엔드포인트:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>How many endpoints must agree on a read the node acts on (credit.md §7.3: two).</source>
        <translation>노드가 따르는 읽기 결과에 대해 일치해야 하는 엔드포인트 수(credit.md §7.3: 2).</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Endpoints that must agree:</source>
        <translation>일치해야 하는 엔드포인트:</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+10"/>
        <location line="+985"/>
        <source> credits/MiB</source>
        <translation> 크레딧/MiB</translation>
    </message>
    <message>
        <location line="-994"/>
        <source>Never pay (receipts only)</source>
        <translation>지불 안 함(영수증만)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The most paid for a priority slot per MiB, for every ed3k download.</source>
        <translation>모든 ed3k 다운로드에서 우선 슬롯에 MiB당 지불하는 최대 금액입니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bid at most:</source>
        <translation>최대 입찰가:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Sell priority upload slots</source>
        <translation>우선 업로드 슬롯 판매</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Lowest bid taken:</source>
        <translation>수락하는 최저 입찰가:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Those of the upload queue</source>
        <translation>업로드 대기열의 슬롯</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Upload slots the priority lane counts; at most two thirds go to bidders.</source>
        <translation>우선 레인이 계산하는 업로드 슬롯입니다. 입찰자에게는 최대 3분의 2가 배정됩니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload slots:</source>
        <translation>업로드 슬롯:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Credit key</source>
        <translation>크레딧 키</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>New key…</source>
        <translation>새 키…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Import…</source>
        <translation>가져오기…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Export…</source>
        <translation>내보내기…</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Only from a GUI on the computer of the core: the key is a secret.</source>
        <translation>코어가 있는 컴퓨터의 GUI에서만 가능합니다. 키는 비밀입니다.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Account</source>
        <translation>계정</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+157"/>
        <source>Not known while credits are off.</source>
        <translation>크레딧이 꺼져 있는 동안에는 알 수 없습니다.</translation>
    </message>
    <message>
        <location line="-149"/>
        <source>What switching credits on means</source>
        <translation>크레딧을 켜는 것의 의미</translation>
    </message>
    <message>
        <location line="+50"/>
        <location line="+265"/>
        <source>Changes on this page take effect at once.</source>
        <translation>이 페이지의 변경 사항은 즉시 적용됩니다.</translation>
    </message>
    <message>
        <location line="-249"/>
        <source>Pinned names</source>
        <translation>고정된 이름</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Name</source>
        <translation>이름</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+20"/>
        <source>Key</source>
        <translation>키</translation>
    </message>
    <message>
        <location line="-20"/>
        <source>How</source>
        <translation>방법</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Vouched</source>
        <translation>보증함</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+7"/>
        <source>Unpin</source>
        <translation>고정 해제</translation>
    </message>
    <message>
        <location line="-6"/>
        <source>Vouch / Withdraw</source>
        <translation>보증 / 철회</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Vouching publishes a claim for the name and answers friends with it.</source>
        <translation>보증하면 이름에 대한 주장이 게시되고 친구에게 그것으로 응답합니다.</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Vouch</source>
        <translation>보증</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Petnames</source>
        <translation>펫네임</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Petname</source>
        <translation>펫네임</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Shared with friends</source>
        <translation>친구와 공유</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Add…</source>
        <translation>추가…</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+6"/>
        <location line="+587"/>
        <source>Remove</source>
        <translation>제거</translation>
    </message>
    <message>
        <location line="-583"/>
        <source>Followed name lists (the first one wins)</source>
        <translation>팔로우 중인 이름 목록(첫 번째가 우선)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Label</source>
        <translation>레이블</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Entries</source>
        <translation>항목</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Refused</source>
        <translation>거부됨</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Link</source>
        <translation>링크</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Follow…</source>
        <translation>팔로우…</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+10"/>
        <source>Unfollow</source>
        <translation>팔로우 해제</translation>
    </message>
    <message>
        <location line="-9"/>
        <source>Move up</source>
        <translation>위로 이동</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Move down</source>
        <translation>아래로 이동</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>This core was built without ed3k: the settings are kept, but have no effect.</source>
        <translation>이 코어는 ed3k 없이 빌드되었습니다. 설정은 유지되지만 효과가 없습니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The node is running on UDP port %1.</source>
        <translation>노드가 UDP 포트 %1에서 실행 중입니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The node is not running.</source>
        <translation>노드가 실행 중이 아닙니다.</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>No credit key yet: one is made when credits are switched on.</source>
        <translation>아직 크레딧 키가 없습니다. 크레딧을 켜면 만들어집니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Credit address: %1</source>
        <translation>크레딧 주소: %1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Not in use: %1</source>
        <translation>사용 안 함: %1</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Balance: %1</source>
        <translation>잔액: %1</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+829"/>
        <source>not read yet</source>
        <translation>아직 읽지 않음</translation>
    </message>
    <message>
        <location line="-827"/>
        <source>The account is overdrawn: a new credit key is the way on.</source>
        <translation>계정이 초과 인출 상태입니다. 계속하려면 새 크레딧 키가 필요합니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Mature: the receipts you sign mint credits.</source>
        <translation>성숙됨: 내가 서명한 영수증이 크레딧을 발행합니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not mature yet: the receipts you sign mint nothing.</source>
        <translation>아직 성숙되지 않음: 내가 서명한 영수증은 아무것도 발행하지 않습니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Receipts signed: %1, received: %2</source>
        <translation>서명한 영수증: %1, 받은 영수증: %2</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Stake: a credit buys %1 E3S</source>
        <translation>스테이크: 크레딧 1개로 %1 E3S 구매</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Stake: burning is off</source>
        <translation>스테이크: 소각 꺼짐</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Stake claims waiting: %1</source>
        <translation>대기 중인 스테이크 청구: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Some endpoints are on another chain and are not used.</source>
        <translation>일부 엔드포인트는 다른 체인에 있어 사용되지 않습니다.</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Switch credits on?</source>
        <translation>크레딧을 켜시겠습니까?</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>A new credit key gives a new credit address: no balance, and not mature for 30 days. The old address keeps its credits; they move only by a transfer, which is public and costs a part of them. Export the old key first if you may need it.

Make a new key?</source>
        <translation>새 크레딧 키는 새 크레딧 주소를 만듭니다. 잔액이 없으며 30일 동안 성숙되지 않습니다. 이전 주소의 크레딧은 그대로 남으며, 송금으로만 옮길 수 있습니다. 송금은 공개되며 크레딧의 일부가 비용으로 듭니다. 필요할 수 있다면 먼저 이전 키를 내보내십시오.

새 키를 만드시겠습니까?</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>New key</source>
        <translation>새 키</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Import credit key</source>
        <translation>크레딧 키 가져오기</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+55"/>
        <source>Credit keys (*.key *.txt);;All files (*)</source>
        <translation>크레딧 키 (*.key *.txt);;모든 파일 (*)</translation>
    </message>
    <message>
        <location line="-50"/>
        <source>Could not read %1.</source>
        <translation>%1을(를) 읽을 수 없습니다.</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>This is not a credit key: 32 bytes or 64 hexadecimal digits.</source>
        <translation>크레딧 키가 아닙니다(32바이트 또는 16진수 64자리).</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>The imported key replaces the current credit key. Export the current one first if you may need it.

Import?</source>
        <translation>가져온 키가 현재 크레딧 키를 대체합니다. 필요할 수 있다면 먼저 현재 키를 내보내십시오.

가져오시겠습니까?</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Import</source>
        <translation>가져오기</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>The file will hold the secret credit key: whoever has it can spend your credits and sign as you. Keep it as private as a password.

Export?</source>
        <translation>이 파일에는 비밀 크레딧 키가 담깁니다. 이 파일을 가진 사람은 누구나 내 크레딧을 쓰고 나로서 서명할 수 있습니다. 비밀번호처럼 안전하게 보관하십시오.

내보내시겠습니까?</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Export credit key</source>
        <translation>크레딧 키 내보내기</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Export</source>
        <translation>내보내기</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Could not write %1.</source>
        <translation>%1에 쓸 수 없습니다.</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>The ed3k node is not running: names can be changed while it runs.</source>
        <translation>ed3k 노드가 실행 중이 아닙니다. 이름은 노드가 실행 중일 때 변경할 수 있습니다.</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+10"/>
        <source>Yes</source>
        <translation>예</translation>
    </message>
    <message>
        <location line="-10"/>
        <location line="+10"/>
        <source>No</source>
        <translation>아니오</translation>
    </message>
    <message>
        <location line="+19"/>
        <location line="+3"/>
        <location line="+4"/>
        <location line="+3"/>
        <source>Add petname</source>
        <translation>펫네임 추가</translation>
    </message>
    <message>
        <location line="-10"/>
        <source>Petname:</source>
        <translation>펫네임:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Key of the zone (hex or base32):</source>
        <translation>영역 키(hex 또는 base32):</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Share this petname with your ed3k friends?</source>
        <translation>이 펫네임을 ed3k 친구와 공유하시겠습니까?</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+4"/>
        <location line="+3"/>
        <source>Follow name list</source>
        <translation>이름 목록 팔로우</translation>
    </message>
    <message>
        <location line="-7"/>
        <source>Link of the name list (ed3k:set?…):</source>
        <translation>이름 목록 링크(ed3k:set?…):</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Label:</source>
        <translation>레이블:</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Move</source>
        <translation>이동</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>%1 (the eD2K TCP/UDP port)</source>
        <translation>%1 (eD2K TCP/UDP 포트)</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>In use: eD2K and ed3k share port %1, and the user hash is the one derived from the ed3k node.</source>
        <translation>사용 중: eD2K와 ed3k가 포트 %1을(를) 공유하며, 사용자 해시는 ed3k 노드에서 파생된 것입니다.</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>No connection to the core.</source>
        <translation>코어에 연결되어 있지 않습니다.</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>UDP port %1 is used by another program or by BitTorrent. Change the TCP port first.</source>
        <translation>UDP 포트 %1은(는) 다른 프로그램이나 BitTorrent가 사용 중입니다. 먼저 TCP 포트를 변경하세요.</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>New user hash: credits earned at other eD2K clients are lost once. The core restarts.</source>
        <translation>새 사용자 해시: 다른 eD2K 클라이언트에서 얻은 크레딧이 한 번 사라집니다. 코어가 다시 시작됩니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>eD2K UDP and ed3k move to port %1 (was UDP %2, ed3k %3).</source>
        <translation>eD2K UDP와 ed3k가 포트 %1(으)로 이동합니다(이전 UDP %2, ed3k %3).</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Open UDP port %1 in your router.</source>
        <translation>라우터에서 UDP 포트 %1을(를) 여세요.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Nothing to change.</source>
        <translation>변경할 내용이 없습니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Switch to eD2K compatibility?</source>
        <translation>eD2K 호환으로 전환하시겠습니까?</translation>
    </message>
    <message>
        <location line="+34"/>
        <source>the ed3k node is not running</source>
        <translation>ed3k 노드가 실행 중이 아님</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>error %1</source>
        <translation>오류 %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>no answer from the core</source>
        <translation>코어 응답 없음</translation>
    </message>
    <message>
        <location line="+90"/>
        <source>Lets the resolver open hoster pages, wait out their timers, show their puzzles and use the accounts below. On by default; needs the ed3k node.</source>
        <translation>리졸버가 호스터 페이지를 열고, 타이머를 기다리고, 퍼즐을 표시하고, 아래 계정을 사용하도록 허용합니다. 기본값은 켜짐이며 ed3k 노드가 필요합니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Hoster downloads need the ed3k node (Options, ed3k, General).</source>
        <translation>호스터 다운로드에는 ed3k 노드가 필요합니다 (옵션, ed3k, 일반).</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Check hoster links on search automatically</source>
        <translation>검색 시 호스터 링크 자동 확인</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The links window of a web search result checks which links are online. Download prefers online links at hosters you have an account at.</source>
        <translation>웹 검색 결과의 링크 창에서 어떤 링크가 온라인인지 확인합니다. 다운로드는 계정이 있는 호스터의 온라인 링크를 우선합니다.</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>The resolver runs JDownloader&apos;s hoster plugins in a Java program of its own.</source>
        <translation>리졸버는 JDownloader의 호스터 플러그인을 별도의 Java 프로그램에서 실행합니다.</translation>
    </message>
    <message>
        <location line="+138"/>
        <source>JDownloader: not installed</source>
        <translation>JDownloader: 설치되지 않음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>JDownloader version: %1</source>
        <translation>JDownloader 버전: %1</translation>
    </message>
    <message>
        <location line="+169"/>
        <source>Autostart highspeed download</source>
        <translation>고속 다운로드 자동 시작</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Once the credit balance reaches the threshold, every ed3k download bids the credit factor for priority upload slots. It stops before about 20 credits are left, from what downloads spent in the past seconds, so the account is never overdrawn.</source>
        <translation>크레딧 잔액이 임계값에 도달하면 모든 ed3k 다운로드가 우선 업로드 슬롯에 크레딧 배수로 입찰합니다. 최근 몇 초 동안 다운로드가 사용한 양을 기준으로 약 20 크레딧이 남기 전에 멈추므로 계정이 초과 인출되지 않습니다.</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>The most paid per MiB for a priority slot while the autostart runs.</source>
        <translation>자동 시작이 실행되는 동안 우선 슬롯에 MiB당 지불하는 최대 금액.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Credit factor:</source>
        <translation>크레딧 배수:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source> credits</source>
        <translation> 크레딧</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The balance at which the autostart begins. It then runs down to about 20 credits.</source>
        <translation>자동 시작이 시작되는 잔액. 이후 약 20 크레딧까지 계속됩니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Autostart threshold:</source>
        <translation>자동 시작 임계값:</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>ed3k is off: this has no effect.</source>
        <translation>ed3k가 꺼져 있습니다: 이 설정은 효과가 없습니다.</translation>
    </message>
    <message>
        <source>Needs ed3k credits in use with ledger endpoints.</source>
        <translation type="vanished">원장 엔드포인트와 함께 ed3k 크레딧을 사용 중이어야 합니다.</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>The account is overdrawn: nothing is bid.</source>
        <translation>계정이 초과 인출되었습니다: 입찰하지 않습니다.</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Highspeed download is running. Balance: %1</source>
        <translation>고속 다운로드 실행 중. 잔액: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Highspeed download is not running. Balance: %1</source>
        <translation>고속 다운로드가 실행 중이 아닙니다. 잔액: %1</translation>
    </message>
    <message>
        <location line="-487"/>
        <source>ed3k</source>
        <translation>ed3k</translation>
    </message>
    <message>
        <location line="-778"/>
        <source>Runs the ed3k node (packmule) in the core, on the eD2K UDP port or one of its own. It finds other nodes over Kad and serves the shared files.</source>
        <translation>코어에서 ed3k 노드(packmule)를 eD2K UDP 포트 또는 자체 포트로 실행합니다. Kad로 다른 노드를 찾고 공유 파일을 제공합니다.</translation>
    </message>
    <message>
        <source>This network is early alpha, experimental and not for live use yet. Please come back later.</source>
        <translation type="vanished">이 네트워크는 초기 알파 단계의 실험 버전으로 아직 실사용할 수 없습니다. 나중에 다시 확인해 주세요.</translation>
    </message>
    <message>
        <location line="+17"/>
        <location line="+692"/>
        <location line="+6"/>
        <location line="+9"/>
        <location line="+13"/>
        <location line="+5"/>
        <location line="+7"/>
        <source>eD2K compatibility</source>
        <translation>eD2K 호환</translation>
    </message>
    <message>
        <location line="-728"/>
        <source>One port for eD2K and ed3k, and a user hash that tells eD2K peers this client also speaks ed3k.</source>
        <translation>eD2K와 ed3k에 하나의 포트를 쓰고, 이 클라이언트가 ed3k도 지원함을 eD2K 피어에 알리는 사용자 해시를 사용합니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Migrate...</source>
        <translation>마이그레이션...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Shows the changes for this configuration and asks before making them.</source>
        <translation>이 구성에 대한 변경 사항을 보여 주고 적용하기 전에 묻습니다.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Download eD2K files from ed3k peers too</source>
        <translation>eD2K 파일을 ed3k 피어에서도 다운로드</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Finds the ed3k copy of each eD2K download by its eD2K hash and fetches the missing parts there too. A source that also speaks ed3k is used over ed3k.</source>
        <translation>각 eD2K 다운로드의 ed3k 사본을 eD2K 해시로 찾아 빠진 부분을 그곳에서도 가져옵니다. ed3k도 지원하는 소스는 ed3k로 사용합니다.</translation>
    </message>
    <message>
        <location line="+47"/>
        <source>UDP port of the ed3k node. On the eD2K UDP port it shares that socket. A change restarts the node.</source>
        <translation>ed3k 노드의 UDP 포트입니다. eD2K UDP 포트와 같으면 그 소켓을 공유합니다. 변경하면 노드가 다시 시작됩니다.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>ed3k shares the eD2K TCP/UDP port; change it on the Connection page.</source>
        <translation>ed3k는 eD2K TCP/UDP 포트를 공유합니다. 연결 페이지에서 변경하세요.</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Open Router</source>
        <translation>라우터 열기</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Opens the web page of this computer&apos;s router, where ports are forwarded</source>
        <translation>이 컴퓨터의 라우터 웹 페이지를 엽니다. 여기서 포트를 포워딩합니다</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>JSON-RPC endpoints of the chaintube chain, one URL per line (https, or http to a node on this computer).</source>
        <translation>chaintube 체인의 JSON-RPC 엔드포인트, 한 줄에 URL 하나 (https, 또는 이 컴퓨터의 노드로 가는 http).</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Only one remote endpoint: nothing checks its answers. Two endpoints of independent providers are recommended, or a chain node on this computer.</source>
        <translation>원격 엔드포인트가 하나뿐입니다. 응답을 검증할 수단이 없습니다. 독립된 제공자의 엔드포인트 두 개 또는 이 컴퓨터의 체인 노드를 권장합니다.</translation>
    </message>
    <message>
        <location line="+653"/>
        <source>%1 failed: %2</source>
        <translation>%1 실패: %2</translation>
    </message>
    <message>
        <location line="+52"/>
        <source>&lt;p&gt;&lt;b&gt;The ledger is public and stays so.&lt;/b&gt; For every pair of credit addresses it shows how much one downloaded from the other, on which day, and what was paid; no file, host or node ID. Your node tells its credit address to every peer that opens a file stream to it, so others can link your node and host address to your credit address and read your whole trade history. A bid (a hold) is public at once. A burn for stake tokens is public too: it ties your credit address to the address it pays, and through the bridge to an address on the C-Chain, where an exchange may know your name.&lt;/p&gt;&lt;p&gt;&lt;b&gt;Hidden (anonymous) use&lt;/b&gt; needs a credit address of its own that is never used outside the anonymous path; never move credits or gas coin between that address and this one, and never burn for it: a transfer is a public link for good.&lt;/p&gt;&lt;p&gt;A remote ledger endpoint sees your IP address and every credit address it is asked about; the calls go through the proxy you configured.&lt;/p&gt;&lt;p&gt;&lt;b&gt;Not checked by a lawyer:&lt;/b&gt; credits buy E3S stake tokens, which are traded on exchanges. Whether credits and E3S are crypto-assets under the law of your country, and what being paid in them means for you, was not checked by a lawyer.&lt;/p&gt;</source>
        <translation>&lt;p&gt;&lt;b&gt;원장은 공개되어 있으며 앞으로도 공개됩니다.&lt;/b&gt; 크레딧 주소의 모든 쌍에 대해 한쪽이 다른 쪽에서 얼마나 다운로드했는지, 어느 날인지, 얼마를 지불했는지를 보여 줍니다. 파일, 호스트, 노드 ID는 포함되지 않습니다. 내 노드는 파일 스트림을 여는 모든 피어에게 자신의 크레딧 주소를 알려 주므로, 다른 사람은 내 노드와 호스트 주소를 크레딧 주소와 연결하여 내 전체 거래 내역을 읽을 수 있습니다. 입찰(홀드)은 즉시 공개됩니다. 스테이크 토큰을 위한 소각도 공개됩니다. 소각은 내 크레딧 주소를 지불받는 주소와 연결하고, 브리지를 통해 C-Chain의 주소와도 연결하며, 그곳에서는 거래소가 내 이름을 알고 있을 수 있습니다.&lt;/p&gt;&lt;p&gt;&lt;b&gt;숨김(익명) 사용&lt;/b&gt;에는 익명 경로 밖에서는 절대 사용하지 않는 전용 크레딧 주소가 필요합니다. 그 주소와 이 주소 사이에서 크레딧이나 가스 코인을 절대 옮기지 말고, 그 주소를 위해 절대 소각하지 마십시오. 송금은 영구히 남는 공개 연결입니다.&lt;/p&gt;&lt;p&gt;원격 원장 엔드포인트는 내 IP 주소와 조회 요청을 받은 모든 크레딧 주소를 볼 수 있습니다. 호출은 설정한 프록시를 거칩니다.&lt;/p&gt;&lt;p&gt;&lt;b&gt;변호사의 검토를 받지 않음:&lt;/b&gt; 크레딧으로 E3S 스테이크 토큰을 구매하며, E3S는 거래소에서 거래됩니다. 크레딧과 E3S가 거주 국가의 법률상 암호자산에 해당하는지, 그리고 이것으로 지불받는 것이 나에게 어떤 의미인지는 변호사의 검토를 받지 않았습니다.&lt;/p&gt;</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>%1.%2 credits</source>
        <translation>%1.%2 크레딧</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Downloads from file hosters</source>
        <translation>파일 호스터에서 다운로드</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Allow downloads from file hosters</source>
        <translation>파일 호스터에서 다운로드 허용</translation>
    </message>
    <message>
        <source>Lets the resolver open hoster pages, wait out their timers, show their puzzles and use the accounts below. Off by default.</source>
        <translation type="vanished">리졸버가 호스터 페이지를 열고, 대기 시간을 기다리고, 인증을 표시하고, 아래 계정을 사용하도록 허용합니다. 기본값은 꺼짐입니다.</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Hoster downloads at once. Never more than one per hoster and account.</source>
        <translation>동시 호스터 다운로드 수. 호스터 및 계정당 하나를 넘지 않습니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloads at once:</source>
        <translation>동시 다운로드:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Show image puzzles (captchas)</source>
        <translation>이미지 인증(캡차) 표시</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Off: a link that asks for a puzzle waits instead.</source>
        <translation>끄기: 인증을 요구하는 링크는 대기합니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Use hoster links that other ed3k nodes name</source>
        <translation>다른 ed3k 노드가 알려준 호스터 링크 사용</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Java runtime</source>
        <translation>Java 런타임</translation>
    </message>
    <message>
        <source>The resolver runs JDownloader&apos;s hoster plugins in a Java program of its own. eMuleQt looks for Java 17 or newer and can download one into its folder.</source>
        <translation type="vanished">리졸버는 JDownloader의 호스터 플러그인을 자체 Java 프로그램에서 실행합니다. eMuleQt는 Java 17 이상을 찾으며, 자체 폴더에 다운로드할 수도 있습니다.</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Find automatically</source>
        <translation>자동으로 찾기</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Java program:</source>
        <translation>Java 프로그램:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Browse...</source>
        <translation>찾아보기...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Java program</source>
        <translation>Java 프로그램</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Download a Java runtime when none is found</source>
        <translation>찾을 수 없으면 Java 런타임 다운로드</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Eclipse Temurin from adoptium.net, checked by its SHA-256, into the eMuleQt folder.</source>
        <translation>adoptium.net의 Eclipse Temurin을 SHA-256으로 확인한 뒤 eMuleQt 폴더에 설치합니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Look again</source>
        <translation>다시 찾기</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Download Java</source>
        <translation>Java 다운로드</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Sharing links</source>
        <translation>링크 공유</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+146"/>
        <source>Publish hoster links</source>
        <translation>호스터 링크 게시</translation>
    </message>
    <message>
        <location line="-144"/>
        <source>A downloaded file is shared either way. Published, its link is found in ed3k and handed to other nodes: a link is often all that protects a file at a hoster.</source>
        <translation>다운로드한 파일은 어느 쪽이든 공유됩니다. 게시하면 그 링크가 ed3k에서 검색되고 다른 노드에 전달됩니다. 호스터의 파일을 보호하는 것은 링크뿐인 경우가 많습니다.</translation>
    </message>
    <message>
        <location line="+140"/>
        <source>&lt;p&gt;Sharing hoster links means: once a file from a link is downloaded and verified, its link is published in ed3k and handed to other nodes that download the same file.&lt;/p&gt;&lt;p&gt;A link is often all that keeps a file at a hoster private. Whoever knows the link can find the file in ed3k, and anyone can find the link of a file you share.&lt;/p&gt;&lt;p&gt;Publish hoster links?&lt;/p&gt;</source>
        <translation>&lt;p&gt;호스터 링크 공유란, 링크의 파일을 다운로드하고 검증한 뒤 그 링크를 ed3k에 게시하고 같은 파일을 다운로드하는 다른 노드에 전달하는 것을 뜻합니다.&lt;/p&gt;&lt;p&gt;호스터의 파일을 비공개로 지키는 것은 링크뿐인 경우가 많습니다. 링크를 아는 사람은 ed3k에서 파일을 찾을 수 있고, 누구나 사용자가 공유하는 파일의 링크를 찾을 수 있습니다.&lt;/p&gt;&lt;p&gt;호스터 링크를 게시할까요?&lt;/p&gt;</translation>
    </message>
    <message>
        <source>Hold hoster links back</source>
        <translation type="vanished">호스터 링크 공개 보류</translation>
    </message>
    <message>
        <source>A downloaded file is shared either way. Held back, its link is not published and not handed to other nodes: a link is often all that protects a file at a hoster.</source>
        <translation type="vanished">다운로드한 파일은 어느 경우든 공유됩니다. 보류하면 링크가 게시되지 않고 다른 노드에도 전달되지 않습니다. 호스터의 파일을 보호하는 것은 링크뿐인 경우가 많습니다.</translation>
    </message>
    <message>
        <location line="-135"/>
        <source>Accounts</source>
        <translation>계정</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Logins at hosters. They stay on this computer: never sent to another node, never logged.</source>
        <translation>호스터 로그인 정보. 이 컴퓨터에만 저장되며, 다른 노드로 전송되거나 로그에 기록되지 않습니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Hoster</source>
        <translation>호스터</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>User</source>
        <translation>사용자</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Status</source>
        <translation>상태</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Add...</source>
        <translation>추가...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Edit...</source>
        <translation>편집...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Check</source>
        <translation>확인</translation>
    </message>
    <message>
        <location line="+54"/>
        <source>This build has no hoster resolver: hoster links are stored and passed on, but not downloaded.</source>
        <translation>이 빌드에는 호스터 리졸버가 없습니다. 호스터 링크는 저장되고 전달되지만 다운로드되지는 않습니다.</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>The resolver is ready.</source>
        <translation>리졸버가 준비되었습니다.</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Resolver: %1</source>
        <translation>리졸버: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The resolver is not ready: %1</source>
        <translation>리졸버가 준비되지 않았습니다: %1</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Looking for Java...</source>
        <translation>Java 찾는 중...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Java %1 (%2): %3</source>
        <translation>Java %1 (%2): %3</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>No Java 17 or newer found.</source>
        <translation>Java 17 이상을 찾을 수 없습니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloading Java... %1%</source>
        <translation>Java 다운로드 중... %1%</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Installing Java...</source>
        <translation>Java 설치 중...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Java: %1</source>
        <translation>Java: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Java is looked for when downloads from hosters are allowed.</source>
        <translation>호스터 다운로드가 허용되면 Java를 찾습니다.</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Allow downloads from file hosters and apply first.</source>
        <translation>먼저 파일 호스터 다운로드를 허용하고 적용하세요.</translation>
    </message>
    <message>
        <source>&lt;p&gt;Downloads from file hosters run JDownloader&apos;s hoster plugins on this computer, in a program of their own.&lt;/p&gt;&lt;ul&gt;&lt;li&gt;The hoster sees your address and which file you fetch; with an account, also who you are to it.&lt;/li&gt;&lt;li&gt;Their pages may show puzzles and timers; eMuleQt shows image puzzles and waits out timers for you.&lt;/li&gt;&lt;li&gt;Fetching by program is against the terms of many hosters. That is between you and the hoster.&lt;/li&gt;&lt;li&gt;Every block that ed3k knows is verified; a file ed3k does not know is checked as a whole when complete.&lt;/li&gt;&lt;/ul&gt;&lt;p&gt;Allow downloads from file hosters?&lt;/p&gt;</source>
        <translation type="vanished">&lt;p&gt;파일 호스터 다운로드는 JDownloader의 호스터 플러그인을 이 컴퓨터에서 별도 프로그램으로 실행합니다.&lt;/p&gt;&lt;ul&gt;&lt;li&gt;호스터는 사용자의 주소와 받는 파일을 알 수 있으며, 계정을 사용하면 사용자가 누구인지도 알 수 있습니다.&lt;/li&gt;&lt;li&gt;호스터 페이지는 인증 문자와 대기 시간을 표시할 수 있습니다. eMuleQt는 이미지 인증을 표시하고 대기 시간을 대신 기다립니다.&lt;/li&gt;&lt;li&gt;프로그램으로 파일을 받는 것은 많은 호스터의 약관에 위배됩니다. 이는 사용자와 호스터 간의 문제입니다.&lt;/li&gt;&lt;li&gt;ed3k가 아는 모든 블록은 검증되며, ed3k가 모르는 파일은 완료 시 전체를 검사합니다.&lt;/li&gt;&lt;/ul&gt;&lt;p&gt;파일 호스터에서 다운로드를 허용하시겠습니까?&lt;/p&gt;</translation>
    </message>
    <message>
        <source>&lt;p&gt;Sharing hoster links means: once a file from a link is downloaded and verified, its link is published in ed3k and handed to other nodes that download the same file.&lt;/p&gt;&lt;p&gt;A link is often all that keeps a file at a hoster private. Whoever knows the link can find the file in ed3k, and anyone can find the link of a file you share.&lt;/p&gt;&lt;p&gt;Share hoster links?&lt;/p&gt;</source>
        <translation type="vanished">&lt;p&gt;호스터 링크 공유란, 링크의 파일을 다운로드하고 검증한 뒤 그 링크를 ed3k에 게시하고 같은 파일을 다운로드하는 다른 노드에 전달하는 것을 뜻합니다.&lt;/p&gt;&lt;p&gt;호스터의 파일을 비공개로 지키는 것은 링크뿐인 경우가 많습니다. 링크를 아는 사람은 ed3k에서 파일을 찾을 수 있고, 누구나 사용자가 공유하는 파일의 링크를 찾을 수 있습니다.&lt;/p&gt;&lt;p&gt;호스터 링크를 공유하시겠습니까?&lt;/p&gt;</translation>
    </message>
    <message>
        <source>Share hoster links</source>
        <translation type="vanished">호스터 링크 공유</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Add hoster account</source>
        <translation>호스터 계정 추가</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Edit hoster account</source>
        <translation>호스터 계정 편집</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>unchanged</source>
        <translation>변경 안 함</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hoster:</source>
        <translation>호스터:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>User:</source>
        <translation>사용자:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Password:</source>
        <translation>비밀번호:</translation>
    </message>
    <message>
        <location line="+38"/>
        <source>Check hoster account</source>
        <translation>호스터 계정 확인</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Password of %1 at %2:</source>
        <translation>%2의 %1 비밀번호:</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Checking...</source>
        <translation>확인 중...</translation>
    </message>
</context>
<context>
    <name>eMule::Ed3kRoomList</name>
    <message>
        <location filename="../src/gui/panels/Ed3kRoomList.cpp" line="+28"/>
        <location line="+100"/>
        <source>Join by link...</source>
        <translation>링크로 참가...</translation>
    </message>
    <message>
        <location line="-99"/>
        <location line="+100"/>
        <source>Create...</source>
        <translation>만들기...</translation>
    </message>
    <message>
        <location line="-90"/>
        <source>Room</source>
        <translation>방</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Nick</source>
        <translation>닉네임</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Members</source>
        <translation>멤버</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Kind</source>
        <translation>종류</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>State</source>
        <translation>상태</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>Closed</source>
        <translation>폐쇄형</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Private</source>
        <translation>비공개</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Public</source>
        <translation>공개</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>moderated</source>
        <translation>관리됨</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>owner</source>
        <translation>소유자</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>moderator</source>
        <translation>관리자</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Not admitted: send your member key to a moderator</source>
        <translation>승인되지 않음: 멤버 키를 관리자에게 보내세요</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Waiting for the room key</source>
        <translation>방 키 대기 중</translation>
    </message>
    <message numerus="yes">
        <location line="+2"/>
        <source>Joined (%n neighbour(s))</source>
        <translation>
            <numerusform>참여함 (이웃 %n)</numerusform>
        </translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Looking for members</source>
        <translation>멤버 찾는 중</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Open</source>
        <translation>개방</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Leave</source>
        <translation>나가기</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Copy link</source>
        <translation>링크 복사</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Change nick...</source>
        <translation>닉네임 변경...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Copy my member key</source>
        <translation>내 멤버 키 복사</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Import admit...</source>
        <translation>승인 가져오기...</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Admit a member key...</source>
        <translation>멤버 키 승인...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>New generation</source>
        <translation>새 세대</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Room info...</source>
        <translation>방 정보...</translation>
    </message>
</context>
<context>
    <name>eMule::Ed3kRoomTab</name>
    <message>
        <location filename="../src/gui/panels/Ed3kRoomTab.cpp" line="+94"/>
        <source>ed3k room %1</source>
        <translation>ed3k 방 %1</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>%1 (ed3k)</source>
        <translation>%1 (ed3k)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>closed</source>
        <translation>폐쇄형</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>private</source>
        <translation>비공개</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>public</source>
        <translation>공개</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>moderated</source>
        <translation>관리됨</translation>
    </message>
    <message>
        <location line="+163"/>
        <source>Reply</source>
        <translation>답장</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Unmute %1</source>
        <translation>%1 음소거 해제</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Mute %1</source>
        <translation>%1 음소거</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Copy member key</source>
        <translation>멤버 키 복사</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Cancel reply</source>
        <translation>답장 취소</translation>
    </message>
    <message>
        <location line="+45"/>
        <source>answer to a post not held</source>
        <translation>보유하지 않은 게시물에 대한 답장</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>This room is closed and you are not admitted yet. Send this member key to a moderator:</source>
        <translation>이 방은 비공개이며 아직 승인되지 않았습니다. 이 멤버 키를 관리자에게 보내세요:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Copy</source>
        <translation>복사</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Import admit...</source>
        <translation>승인 가져오기...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Only needed when a moderator gave you an admit text</source>
        <translation>관리자에게 승인 텍스트를 받은 경우에만 필요</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hide</source>
        <translation>숨기기</translation>
    </message>
    <message>
        <source>a post not held</source>
        <translation type="vanished">보관되지 않은 글</translation>
    </message>
</context>
<context>
    <name>eMule::Ed3kSetsDialog</name>
    <message>
        <location filename="../src/gui/dialogs/Ed3kSetsDialog.cpp" line="+95"/>
        <location line="+755"/>
        <source>ed3k Sets</source>
        <translation>ed3k 세트</translation>
    </message>
    <message>
        <location line="-745"/>
        <source>Name</source>
        <translation>이름</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Author</source>
        <translation>작성자</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Version</source>
        <translation>버전</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Files</source>
        <translation>파일</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+6"/>
        <source>Size</source>
        <translation>크기</translation>
    </message>
    <message>
        <location line="-6"/>
        <source>New Versions</source>
        <translation>새 버전</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+6"/>
        <location line="+51"/>
        <source>Status</source>
        <translation>상태</translation>
    </message>
    <message>
        <location line="-51"/>
        <source>File</source>
        <translation>파일</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Follow Link...</source>
        <translation>링크 팔로우...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unfollow</source>
        <translation>팔로우 해제</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>New versions:</source>
        <translation>새 버전:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Download Selected</source>
        <translation>선택 항목 다운로드</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Download Parts</source>
        <translation>파트 다운로드</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Buy Pass...</source>
        <translation>패스 구매...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Share reports</source>
        <translation>신고 공유</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Report a pass key seen at more computers than the seller allows (subscription.md §5.3). The report shows the seller the node IDs that used the key.</source>
        <translation>판매자가 허용한 것보다 많은 컴퓨터에서 발견된 패스 키를 신고합니다 (subscription.md §5.3). 신고에는 키를 사용한 노드 ID가 판매자에게 표시됩니다.</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Sets</source>
        <translation>세트</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Channel</source>
        <translation>채널</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Peers</source>
        <translation>피어</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Segments</source>
        <translation>세그먼트</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Received</source>
        <translation>수신됨</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Players</source>
        <translation>플레이어</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Watch Link...</source>
        <translation>링크 시청...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Open</source>
        <translation>개방</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Stop</source>
        <translation>중지</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Broadcast...</source>
        <translation>방송...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Copy Link</source>
        <translation>링크 복사</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Live Streams</source>
        <translation>라이브 스트림</translation>
    </message>
    <message>
        <location line="+52"/>
        <source>(hidden)</source>
        <translation>(숨김)</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Error</source>
        <translation>오류</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>New version</source>
        <translation>새 버전</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Looking for it</source>
        <translation>찾는 중</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Waiting for ed3k</source>
        <translation>ed3k 대기 중</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+78"/>
        <source>Complete</source>
        <translation>완료</translation>
    </message>
    <message>
        <location line="-78"/>
        <source>Downloading (%1 of %2)</source>
        <translation>다운로드 중 (%1 / %2)</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>On air</source>
        <translation>방송 중</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>On air (%1)</source>
        <translation>방송 중 (%1)</translation>
    </message>
    <message>
        <location line="+34"/>
        <source>Download</source>
        <translation>다운로드</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Ask</source>
        <translation>묻기</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Not selected</source>
        <translation>선택 안 됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Waiting</source>
        <translation>대기 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloading</source>
        <translation>다운로드 중</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Failed</source>
        <translation>실패</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paused</source>
        <translation>일시정지됨</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Joining</source>
        <translation>참가 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Playing</source>
        <translation>재생 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Interrupted</source>
        <translation>중단됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>No peers</source>
        <translation>피어 없음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ended</source>
        <translation>종료됨</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Pass needed</source>
        <translation>패스 필요</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Buying a pass</source>
        <translation>패스 구매 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Pass held</source>
        <translation>패스 보유</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Pass refused</source>
        <translation>패스 거부됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>No pass came</source>
        <translation>패스를 받지 못함</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Waiting for your answer</source>
        <translation>응답 대기 중</translation>
    </message>
    <message>
        <location line="+59"/>
        <source>Folder: %1</source>
        <translation>폴더: %1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Version %1 is waiting: %2 added, %3 changed, %4 removed.</source>
        <translation>버전 %1 대기 중: %2개 추가, %3개 변경, %4개 제거.</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>A new version of the mounted folder(s) %1 is waiting: %2 added, %3 changed, %4 removed.</source>
        <translation>마운트된 폴더 %1의 새 버전이 대기 중입니다: %2개 추가, %3개 변경, %4개 제거.</translation>
    </message>
    <message numerus="yes">
        <location line="+8"/>
        <source>%n open part(s) by other authors.</source>
        <translation>
            <numerusform>다른 작성자의 열린 부분 %n개.</numerusform>
        </translation>
    </message>
    <message>
        <location line="+106"/>
        <source>Follow ed3k Set</source>
        <translation>ed3k 세트 팔로우</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>ed3k:set link:</source>
        <translation>ed3k:set 링크:</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>The set could not be followed.</source>
        <translation>세트를 팔로우할 수 없습니다.</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Unfollow ed3k Set</source>
        <translation>ed3k 세트 팔로우 해제</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Stop following &quot;%1&quot;? The files downloaded stay where they are.</source>
        <translation>&quot;%1&quot; 팔로우를 해제할까요? 다운로드한 파일은 그대로 남습니다.</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>The selection could not be set.</source>
        <translation>선택을 설정할 수 없습니다.</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>The parts could not be downloaded.</source>
        <translation>파트를 다운로드할 수 없습니다.</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Watch ed3k Live Stream</source>
        <translation>ed3k 라이브 스트림 시청</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>ed3k:live link:</source>
        <translation>ed3k:live 링크:</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>The stream could not be watched.</source>
        <translation>스트림을 시청할 수 없습니다.</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>Broadcast ed3k Live Stream</source>
        <translation>ed3k 라이브 스트림 방송</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Browse...</source>
        <translation>찾아보기...</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Private (only who has the link can watch)</source>
        <translation>비공개 (링크가 있는 사람만 시청 가능)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The folder of an HLS output with fMP4 segments, e.g. OBS, or
ffmpeg … -f hls -hls_segment_type fmp4 -hls_time 2 -hls_list_size 6 live.m3u8</source>
        <translation>fMP4 세그먼트를 사용하는 HLS 출력 폴더. 예: OBS 또는
ffmpeg … -f hls -hls_segment_type fmp4 -hls_time 2 -hls_list_size 6 live.m3u8</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Title:</source>
        <translation>제목:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Folder:</source>
        <translation>폴더:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Codecs:</source>
        <translation>코덱:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Broadcast</source>
        <translation>방송</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>HLS Output Folder</source>
        <translation>HLS 출력 폴더</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>The broadcast could not start.</source>
        <translation>방송을 시작할 수 없습니다.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>ed3k Live Stream</source>
        <translation>ed3k 라이브 스트림</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>On air. The link was copied to the clipboard:
%1</source>
        <translation>방송 중입니다. 링크가 클립보드에 복사되었습니다:
%1</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Buy a pass for &quot;%1&quot;?</source>
        <translation>&quot;%1&quot;의 패스를 구매하시겠습니까?</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Price: %1</source>
        <translation>가격: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Terms: %1</source>
        <translation>약관: %1</translation>
    </message>
    <message numerus="yes">
        <location line="+1"/>
        <source>The seller&apos;s page at %1 opens in your browser; payment is up to the seller, not ed3k. Your pass key may be used on %n computer(s) at once.</source>
        <translation>
            <numerusform>판매자 페이지 %1이(가) 브라우저에서 열립니다. 결제는 ed3k가 아니라 판매자가 처리합니다. 패스 키는 동시에 컴퓨터 %n대에서 사용할 수 있습니다.</numerusform>
        </translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Buy ed3k Pass</source>
        <translation>ed3k 패스 구매</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Buy</source>
        <translation>구매</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Buy for Anonymous Key</source>
        <translation>익명 키로 구매</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The pass is shown under the anonymous key of the hops (hops.md) only.</source>
        <translation>패스는 홉(hops.md)의 익명 키로만 제시됩니다.</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+26"/>
        <source>The pass could not be bought.</source>
        <translation>패스를 구매할 수 없습니다.</translation>
    </message>
    <message>
        <location line="-11"/>
        <source>Pass for the Anonymous Key</source>
        <translation>익명 키용 패스</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Buying a pass for &quot;%1&quot; and asking the seller for it are not ed3k: the seller sees an address of this computer and can tie it to the pass key, which is shown under your anonymous key.

Go on?</source>
        <translation>&quot;%1&quot;의 패스 구매와 판매자에 대한 요청은 ed3k를 거치지 않습니다. 판매자는 이 컴퓨터의 주소를 보고 익명 키로 제시되는 패스 키와 연결할 수 있습니다.

계속하시겠습니까?</translation>
    </message>
    <message>
        <location line="+42"/>
        <source>Paid: %1</source>
        <translation>결제됨: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Paid; the price is not known yet.</source>
        <translation>결제됨. 가격은 아직 알 수 없습니다.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Pass valid until %1.</source>
        <translation>패스 유효 기한: %1.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The seller does not renew it.</source>
        <translation>판매자가 갱신하지 않습니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Renewed from the seller in its last 48 hours.</source>
        <translation>마지막 48시간 이내에 판매자로부터 갱신됩니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The pass ended on %1; what it opened stays open.</source>
        <translation>패스가 %1에 만료되었습니다. 열린 항목은 계속 열려 있습니다.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Under the anonymous key.</source>
        <translation>익명 키로.</translation>
    </message>
</context>
<context>
    <name>eMule::Ed3kStakeGroup</name>
    <message>
        <location filename="../src/gui/dialogs/Ed3kStakeGroup.cpp" line="+78"/>
        <source>Stake tokens (E3S)</source>
        <translation>스테이크 토큰 (E3S)</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+11"/>
        <location line="+296"/>
        <source>Claim</source>
        <translation>청구</translation>
    </message>
    <message>
        <location line="-307"/>
        <source>Amount (E3S)</source>
        <translation>수량 (E3S)</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>To</source>
        <translation>받는 주소</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+77"/>
        <source>Ready</source>
        <translation>준비됨</translation>
    </message>
    <message>
        <location line="-77"/>
        <source>State</source>
        <translation>상태</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Burn for stake…</source>
        <translation>스테이크용 소각…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Burns credits of this credit address for stake tokens. It cannot be undone and is public.</source>
        <translation>이 크레딧 주소의 크레딧을 소각하여 스테이크 토큰을 받습니다. 되돌릴 수 없으며 공개됩니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Mints a ready claim to its address.</source>
        <translation>준비된 청구를 해당 주소로 발행합니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Claim id…</source>
        <translation>청구 ID…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Reads any claim by its id, shows it, and mints it to its address once ready.</source>
        <translation>ID로 청구를 읽어 표시하고, 준비되면 해당 주소로 발행합니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Refresh</source>
        <translation>새로 고침</translation>
    </message>
    <message>
        <source>Stake tokens need credits in use with ledger endpoints.</source>
        <translation type="vanished">스테이크 토큰을 쓰려면 원장 엔드포인트와 함께 사용 중인 크레딧이 필요합니다.</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>The stake rate is not read yet.</source>
        <translation>스테이크 비율을 아직 읽지 않았습니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Burning for stake is off on this ledger.</source>
        <translation>이 원장에서는 스테이크용 소각이 꺼져 있습니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>A credit buys %1 E3S, claimable %2 day(s) after the burn.</source>
        <translation>크레딧 1개로 %1 E3S 구매, 소각 %2일 후 청구 가능.</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Claiming…</source>
        <translation>청구 중…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Waiting</source>
        <translation>대기 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paid</source>
        <translation>지급됨</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Burn: 0x%1</source>
        <translation>소각: 0x%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Claim: 0x%1</source>
        <translation>청구: 0x%1</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>&lt;p&gt;&lt;b&gt;A burn cannot be undone.&lt;/b&gt; The credits are gone; nothing turns stake tokens back into credits. The claim can be minted to the address once its delay has passed.&lt;/p&gt;&lt;p&gt;&lt;b&gt;A burn is public.&lt;/b&gt; The ledger shows it for good, and it ties your credit address to the address it pays, and through the bridge to an address on the C-Chain, where an exchange may know your name. To avoid that link, burn to a new address and move nothing back to the credit address.&lt;/p&gt;&lt;p&gt;Only your node&apos;s own credit address burns, never an anonymous one.&lt;/p&gt;</source>
        <translation>&lt;p&gt;&lt;b&gt;소각은 되돌릴 수 없습니다.&lt;/b&gt; 크레딧은 사라지며, 스테이크 토큰을 다시 크레딧으로 바꿀 방법은 없습니다. 청구는 대기 기간이 지나면 주소로 발행할 수 있습니다.&lt;/p&gt;&lt;p&gt;&lt;b&gt;소각은 공개됩니다.&lt;/b&gt; 원장에 영구히 표시되며, 크레딧 주소를 지급받는 주소와, 브리지를 통해 C-Chain의 주소와 연결합니다. 거래소는 그 주소의 실명을 알 수 있습니다. 이 연결을 피하려면 새 주소로 소각하고 크레딧 주소로 아무것도 되돌리지 마세요.&lt;/p&gt;&lt;p&gt;소각은 노드 자신의 크레딧 주소로만 하며, 익명 주소로는 절대 하지 않습니다.&lt;/p&gt;</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Claim %1 could not be read from the ledger. Try again later.</source>
        <translation>원장에서 청구 %1을(를) 읽을 수 없습니다. 나중에 다시 시도하세요.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Claim %1 is paid already.</source>
        <translation>청구 %1은(는) 이미 지급되었습니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>There is no claim %1 to mint: it was never made, or it is paid already.</source>
        <translation>발행할 청구 %1이(가) 없습니다. 생성된 적이 없거나 이미 지급되었습니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Claim %1: %2 E3S to %3.</source>
        <translation>청구 %1: %2 E3S → %3.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Ready since %1.</source>
        <translation>%1부터 준비됨.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ready at %1.</source>
        <translation>%1에 준비됨.</translation>
    </message>
    <message>
        <location line="+22"/>
        <location line="+42"/>
        <location line="+5"/>
        <location line="+17"/>
        <source>Burn for stake</source>
        <translation>스테이크용 소각</translation>
    </message>
    <message>
        <location line="-58"/>
        <source> credits</source>
        <translation> 크레딧</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Burn:</source>
        <translation>소각:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>To my credit address (%1)</source>
        <translation>내 크레딧 주소로 (%1)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>To another address:</source>
        <translation>다른 주소로:</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Burn…</source>
        <translation>소각…</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>The address is 0x and 40 hexadecimal digits, not zero.</source>
        <translation>주소는 0x와 16진수 40자리이며 0이 아니어야 합니다.</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Burn %1 credits for stake tokens to %2?</source>
        <translation>크레딧 %1개를 소각하여 스테이크 토큰을 %2(으)로 보낼까요?</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>This cannot be undone, and it is public.</source>
        <translation>되돌릴 수 없으며 공개됩니다.</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>The burn was sent; its claim shows here once it is mined.</source>
        <translation>소각을 보냈습니다. 채굴되면 청구가 여기에 표시됩니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The burn was refused (error %1).</source>
        <translation>소각이 거부되었습니다 (오류 %1).</translation>
    </message>
    <message>
        <location line="+21"/>
        <location line="+7"/>
        <location line="+11"/>
        <location line="+16"/>
        <location line="+3"/>
        <source>Claim id</source>
        <translation>청구 ID</translation>
    </message>
    <message>
        <location line="-37"/>
        <source>Id of the stake claim (anyone may mint it to its address):</source>
        <translation>스테이크 청구 ID (누구나 해당 주소로 발행할 수 있음):</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>A claim id is a whole number above 0.</source>
        <translation>청구 ID는 0보다 큰 정수입니다.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Reading claim %1…</source>
        <translation>청구 %1 읽는 중…</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>The claim could not be read (error %1).</source>
        <translation>청구를 읽을 수 없습니다 (오류 %1).</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Mint it now? Your credit address pays the gas; the stake tokens go to the claim&apos;s address.</source>
        <translation>지금 발행하시겠습니까? 가스비는 크레딧 주소가 지불하고, 스테이크 토큰은 청구 주소로 갑니다.</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>The claim was refused (error %1).</source>
        <translation>청구가 거부되었습니다 (오류 %1).</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>The burn did not go through (revert %1).</source>
        <translation>소각이 처리되지 않았습니다 (revert %1).</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The claim did not go through (revert %1).</source>
        <translation>청구가 처리되지 않았습니다 (revert %1).</translation>
    </message>
</context>
<context>
    <name>eMule::ExternalLinkHandler</name>
    <message>
        <location filename="../src/gui/app/ExternalLinkHandler.cpp" line="+228"/>
        <source>ed3k link</source>
        <translation>ed3k 링크</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>The ed3k link could not be added.</source>
        <translation>ed3k 링크를 추가할 수 없습니다.</translation>
    </message>
    <message>
        <location line="+115"/>
        <source>ed3k node list</source>
        <translation>ed3k 노드 목록</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>A link wants to add a source of ed3k node lists:

%1

Node lists are used only to find the first ed3k nodes. Add this source?</source>
        <translation>링크가 ed3k 노드 목록 소스를 추가하려고 합니다:

%1

노드 목록은 첫 ed3k 노드를 찾는 데만 사용됩니다. 이 소스를 추가하시겠습니까?</translation>
    </message>
    <message>
        <location line="+21"/>
        <location line="+34"/>
        <location line="+51"/>
        <source>ed3k set</source>
        <translation>ed3k 세트</translation>
    </message>
    <message>
        <location line="-84"/>
        <source>This ed3k set is no website. Follow it and download its files?</source>
        <translation>이 ed3k 세트는 웹사이트가 아닙니다. 팔로우하고 파일을 다운로드할까요?</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Follow this ed3k set%1?

Its files go to the incoming folder, below a folder of the set&apos;s own; new versions are downloaded as they come.</source>
        <translation>이 ed3k 세트%1을(를) 팔로우할까요?

파일은 수신 폴더 안의 세트 전용 폴더에 저장되며, 새 버전은 나오는 대로 다운로드됩니다.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Follow and Download</source>
        <translation>팔로우 및 다운로드</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Choose Files...</source>
        <translation>파일 선택...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Open as Website</source>
        <translation>웹사이트로 열기</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>The set could not be followed.</source>
        <translation>세트를 팔로우할 수 없습니다.</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>ed3k live stream</source>
        <translation>ed3k 라이브 스트림</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>The stream could not be watched.</source>
        <translation>스트림을 시청할 수 없습니다.</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Version %1 of the ed3k set &quot;%2&quot; is here: %3 new and %4 changed file(s).</source>
        <translation>ed3k 세트 &quot;%2&quot;의 버전 %1이(가) 도착했습니다: 새 파일 %3개, 변경된 파일 %4개.</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Show</source>
        <translation>보기</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Later</source>
        <translation>나중에</translation>
    </message>
</context>
<context>
    <name>eMule::FileDetailDialog</name>
    <message>
        <location filename="../src/gui/dialogs/FileDetailDialog.cpp" line="+232"/>
        <source>File Details: %1</source>
        <translation>파일 상세: %1</translation>
    </message>
    <message>
        <location line="-38"/>
        <location line="+49"/>
        <location line="+7"/>
        <location line="+159"/>
        <source>General</source>
        <translation>일반</translation>
    </message>
    <message>
        <location line="-165"/>
        <location line="+7"/>
        <source>File Names</source>
        <translation>파일 이름</translation>
    </message>
    <message>
        <location line="-56"/>
        <location line="+50"/>
        <location line="+7"/>
        <source>Comments</source>
        <translation>댓글</translation>
    </message>
    <message>
        <location line="-56"/>
        <location line="+50"/>
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
        <location line="-57"/>
        <location line="+51"/>
        <location line="+7"/>
        <source>ED2K Link</source>
        <translation>ED2K 링크</translation>
    </message>
    <message>
        <location line="+10"/>
        <location line="+2"/>
        <source>Archive Preview</source>
        <translation>압축 파일 미리보기</translation>
    </message>
    <message>
        <location line="+37"/>
        <location line="+20"/>
        <location line="+237"/>
        <source>File Name</source>
        <translation>파일 이름</translation>
    </message>
    <message>
        <location line="-256"/>
        <location line="+30"/>
        <source>Hash (MD4)</source>
        <translation>해시 (MD4)</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+79"/>
        <source>AICH Hash</source>
        <translation>AICH 해시</translation>
    </message>
    <message>
        <location line="-112"/>
        <location line="+49"/>
        <source>File Size</source>
        <translation>파일 크기</translation>
    </message>
    <message>
        <location line="-275"/>
        <source>%1   (%2 ago)</source>
        <translation>%1   (%2 전)</translation>
    </message>
    <message>
        <location line="+226"/>
        <location line="+49"/>
        <source>%1 (%2 Bytes)</source>
        <translation>%1 (%2 Bytes)</translation>
    </message>
    <message>
        <location line="-48"/>
        <location line="+53"/>
        <location line="+151"/>
        <location line="+6"/>
        <source>Completed</source>
        <translation>완료됨</translation>
    </message>
    <message>
        <location line="-206"/>
        <location line="+55"/>
        <location line="+125"/>
        <location line="+2"/>
        <source>Status</source>
        <translation>상태</translation>
    </message>
    <message>
        <location line="-181"/>
        <location line="+55"/>
        <location line="+139"/>
        <source>Priority</source>
        <translation>우선순위</translation>
    </message>
    <message>
        <location line="-193"/>
        <location line="+59"/>
        <location line="+127"/>
        <location line="+44"/>
        <location line="+18"/>
        <source>Sources</source>
        <translation>소스</translation>
    </message>
    <message>
        <location line="-228"/>
        <location line="+82"/>
        <source>File Path</source>
        <translation>파일 경로</translation>
    </message>
    <message>
        <location line="-98"/>
        <location line="+58"/>
        <source>Created</source>
        <translation>생성됨</translation>
    </message>
    <message numerus="yes">
        <location line="-195"/>
        <source>File Details: %n files</source>
        <translation>
            <numerusform>파일 정보: 파일 %n개</numerusform>
        </translation>
    </message>
    <message>
        <location line="+130"/>
        <location line="+55"/>
        <location line="+144"/>
        <location line="+7"/>
        <location line="+29"/>
        <source>Transferred</source>
        <translation>전송됨</translation>
    </message>
    <message>
        <location line="-227"/>
        <location line="+58"/>
        <source>Last Seen Complete</source>
        <translation>마지막으로 완전한 상태로 확인됨</translation>
    </message>
    <message>
        <location line="-57"/>
        <location line="+58"/>
        <source>Last Reception</source>
        <translation>마지막 수신</translation>
    </message>
    <message>
        <source>Part.met File</source>
        <translation type="vanished">Part.met 파일</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Name</source>
        <translation>이름</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Hash</source>
        <translation>해시</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+27"/>
        <location line="+78"/>
        <location line="+2"/>
        <location line="+16"/>
        <source>Unknown</source>
        <translation>알 수 없음</translation>
    </message>
    <message>
        <location line="-121"/>
        <source>Size</source>
        <translation>크기</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>%1 (%2 Bytes); Size on disk: %3</source>
        <translation>%1 (%2 바이트); 디스크 상의 크기: %3</translation>
    </message>
    <message>
        <location line="+12"/>
        <location line="+3"/>
        <source>%1 (Verified)</source>
        <translation>%1 (검사됨)</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+5"/>
        <source>Invalid file extension: %1</source>
        <translation>잘못된 파일 확장자: %1</translation>
    </message>
    <message>
        <location line="-1"/>
        <location line="+5"/>
        <location line="+90"/>
        <source>%1 (Unverified)</source>
        <translation>%1 (검사되지 않음)</translation>
    </message>
    <message>
        <location line="-86"/>
        <source>Type</source>
        <translation>유형</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Parts</source>
        <translation>부분</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>%1; available: %2 (%3%)</source>
        <translation>%1; 사용 가능: %2 (%3%)</translation>
    </message>
    <message numerus="yes">
        <location line="+7"/>
        <source>Downloading (from %n source(s))</source>
        <translation>
            <numerusform>다운로드 (%n개 소스로부터)</numerusform>
        </translation>
    </message>
    <message>
        <location line="-116"/>
        <location line="+126"/>
        <location line="+13"/>
        <source>No</source>
        <translation>아니오</translation>
    </message>
    <message>
        <location line="-145"/>
        <location line="+198"/>
        <source>BLAKE3 Hash</source>
        <translation>BLAKE3 해시</translation>
    </message>
    <message>
        <location line="-198"/>
        <source>Not hashed yet</source>
        <translation>아직 해시되지 않음</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>eD2K alias</source>
        <translation>eD2K 별칭</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Keywords</source>
        <translation>키워드</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Published on ed3k</source>
        <translation>ed3k에 게시됨</translation>
    </message>
    <message>
        <location line="+128"/>
        <location line="+5"/>
        <location line="+2"/>
        <location line="+2"/>
        <source>Yes</source>
        <translation>예</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Hashset available</source>
        <translation>해시셋 사용 가능</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Transfer</source>
        <translation>전송</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>%1 (Useful: %2, NNP: %3); A4AF: %4</source>
        <translation>%1 (유용: %2, NNP: %3); A4AF: %4</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Datarate</source>
        <translation>전송 속도</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Compression</source>
        <translation>압축</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Corrupted</source>
        <translation>손상됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+5"/>
        <source>Remaining</source>
        <translation>남은 시간</translation>
    </message>
    <message>
        <location line="-4"/>
        <source>Recovered</source>
        <translation>복구됨</translation>
    </message>
    <message numerus="yes">
        <location line="+0"/>
        <source>%n Part(s)</source>
        <translation>
            <numerusform>%n 부분</numerusform>
        </translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Dates</source>
        <translation>날짜</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Download added</source>
        <translation>다운로드 추가됨</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Download time</source>
        <translation>다운로드 시간</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Seen complete</source>
        <translation>마지막 완전체</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+1"/>
        <source>Never</source>
        <translation>안 함</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Last reception</source>
        <translation>마지막 수신</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>ed3k</source>
        <translation>ed3k</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>%1 (Trusted)</source>
        <translation>%1 (신뢰됨)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Searching...</source>
        <translation>검색 중...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Not found</source>
        <translation>찾을 수 없음</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>%1 (transferring %2)</source>
        <translation>%1 (전송 중 %2)</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>No alternative file names reported by sources. Use “Search Kad” to look them up.</source>
        <translation>소스에서 보고한 대체 파일 이름이 없습니다. “Kad 검색”을 사용하여 조회하세요.</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>&amp;Take over</source>
        <translation>가져오기(&amp;T)</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>&amp;Cleanup</source>
        <translation>정리(&amp;C)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Rename</source>
        <translation>이름 바꾸기</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Take over</source>
        <translation>가져오기</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Copy</source>
        <translation>복사</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Search Kad</source>
        <translation>Kad 검색</translation>
    </message>
    <message>
        <location line="+116"/>
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
        <location line="+2"/>
        <source>Include Media Info</source>
        <translation>미디어 정보 포함</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add a line with resolution, codecs, length and bitrate under the HTML link of a complete file</source>
        <translation>완료된 파일의 HTML 링크 아래에 해상도, 코덱, 길이, 비트레이트 줄을 추가합니다</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Copy to Clipboard</source>
        <translation>클립보드에 복사</translation>
    </message>
</context>
<context>
    <name>eMule::FindInListDialog</name>
    <message>
        <location filename="../src/gui/dialogs/FindInListDialog.cpp" line="+66"/>
        <location line="+55"/>
        <source>Search</source>
        <translation>검색</translation>
    </message>
    <message>
        <location line="-54"/>
        <source>No matching entry found.</source>
        <translation>일치하는 항목이 없습니다.</translation>
    </message>
    <message>
        <location line="+68"/>
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
        <location filename="../src/gui/dialogs/FirstStartWizard.cpp" line="+62"/>
        <source>eMule First Runtime Wizard</source>
        <translation>eMule 첫 실행 마법사</translation>
    </message>
    <message>
        <location line="+208"/>
        <source>Show more controls (advanced mode controls)</source>
        <translation>더 많은 컨트롤 표시(고급 모드)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Shows advanced entries in context menus and extra details in lists.</source>
        <translation>컨텍스트 메뉴에 고급 항목을, 목록에 추가 정보를 표시합니다.</translation>
    </message>
    <message>
        <location line="+147"/>
        <source>Ports and Connection</source>
        <translation>포트 및 연결</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Connection</source>
        <translation>연결</translation>
    </message>
    <message>
        <location line="+123"/>
        <source>TCP/UDP:</source>
        <translation>TCP/UDP:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>eMule uses one port number for TCP and UDP, for eD2K, Kad and ed3k. It must be free and available for remote clients over both TCP and UDP. TCP is needed for the main functionality of eMule; UDP for Kad (serverless network), ed3k and to reduce network usage (Overhead).</source>
        <translation>eMule은 TCP와 UDP, eD2K·Kad·ed3k에 하나의 포트 번호를 사용합니다. 이 포트는 비어 있어야 하며 원격 클라이언트가 TCP와 UDP 모두로 접속할 수 있어야 합니다. TCP는 eMule의 주요 기능에, UDP는 Kad(서버 없는 네트워크), ed3k 및 네트워크 사용량(오버헤드) 감소에 쓰입니다.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>eMule uses two ports for communication with servers and clients. These ports must be free and available for remote clients. The TCP port must be available to ensure the main functionality of eMule. The UDP port is used for Kad (serverless network) and to reduce network usage (Overhead).</source>
        <translation>eMule은 서버 및 클라이언트와의 통신에 두 개의 포트를 사용합니다. 이 포트는 원격 클라이언트에서 사용할 수 있어야 합니다. TCP 포트는 eMule의 주요 기능을 보장하기 위해 필요합니다. UDP 포트는 Kad(서버리스 네트워크) 및 네트워크 사용량 절감(오버헤드)에 사용됩니다.</translation>
    </message>
    <message>
        <location line="-498"/>
        <location line="+302"/>
        <source>Unlimited</source>
        <translation>무제한</translation>
    </message>
    <message>
        <source>This wizard will guide you through the first steps in configuring eMule.</source>
        <translation type="vanished">이 마법사는 eMule 설정의 첫 단계를 안내합니다.</translation>
    </message>
    <message>
        <source>To continue, click Next.</source>
        <translation type="vanished">계속하려면 다음을 클릭하십시오.</translation>
    </message>
    <message>
        <source>Please enter your user name:</source>
        <translation type="vanished">사용자 이름을 입력하십시오:</translation>
    </message>
    <message>
        <source>Start eMule when the computer starts.</source>
        <translation type="vanished">컴퓨터를 시작할 때 eMule을 시작합니다.</translation>
    </message>
    <message>
        <source>Enable this option if you want eMule to connect at startup.</source>
        <translation type="vanished">eMule이 시작할 때 연결하도록 하려면 이 옵션을 켜십시오.</translation>
    </message>
    <message>
        <source>Enable this option if you want eMule to manage your download priorities.

Turning this on will allow eMule to make sure downloads with a lot of sources do not interfere with downloads that have few sources. This option will only affect future downloads.</source>
        <translation type="vanished">eMule이 다운로드 우선순위를 관리하도록 하려면 이 옵션을 켜십시오.

켜면 소스가 많은 다운로드가 소스가 적은 다운로드를 방해하지 않도록 eMule이 조정합니다. 이 옵션은 앞으로의 다운로드에만 적용됩니다.</translation>
    </message>
    <message>
        <source>Enable this option if you want eMule to manage your upload priorities.

Turning this on will allow eMule to boost rare files meaning popular files will be harder for other people to get. Turning this off will allow eMule to upload popular files more often meaning rare files will be harder for other people to get. This option will only affect future shared files.</source>
        <translation type="vanished">eMule이 업로드 우선순위를 관리하도록 하려면 이 옵션을 켜십시오.

켜면 eMule이 희귀한 파일을 우선하므로 인기 있는 파일은 다른 사람이 받기 어려워집니다. 끄면 인기 있는 파일을 더 자주 업로드하므로 희귀한 파일은 받기 어려워집니다. 이 옵션은 앞으로 공유하는 파일에만 적용됩니다.</translation>
    </message>
    <message>
        <source>Enable this option if you want to use protocol obfuscation

If your ISP tries throttle or block eMule, enabling obfuscation will help to circumvent such restrictions.</source>
        <translation type="vanished">프로토콜 난독화를 사용하려면 이 옵션을 켜십시오

ISP가 eMule을 제한하거나 차단하려는 경우, 난독화를 켜면 그런 제한을 우회하는 데 도움이 됩니다.</translation>
    </message>
    <message>
        <location line="-194"/>
        <source>You can change the ports here while no network activities have started.</source>
        <translation>네트워크 활동이 시작되지 않은 동안 여기에서 포트를 변경할 수 있습니다.</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+372"/>
        <source>TCP:</source>
        <translation>TCP:</translation>
    </message>
    <message>
        <location line="-368"/>
        <source>UDP:</source>
        <translation>UDP:</translation>
    </message>
    <message>
        <source>Disable</source>
        <translation type="vanished">비활성화</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Use UPnP to Setup Ports</source>
        <translation>UPnP로 포트 설정</translation>
    </message>
    <message>
        <location line="+45"/>
        <source>Here you can test, if your TCP and UDP port can be connected to from remote. This success of this test is required for servers and clients to connect you. The TCP port have to succeed!</source>
        <translation>여기서 TCP 포트와 UDP 포트에 외부에서 연결할 수 있는지 테스트할 수 있습니다. 서버와 클라이언트가 연결하려면 이 테스트가 성공해야 합니다. TCP 포트는 반드시 성공해야 합니다!</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Test Ports</source>
        <translation>포트 테스트</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Open Router</source>
        <translation>라우터 열기</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Opens the web page of this computer&apos;s router, where ports are forwarded</source>
        <translation>이 컴퓨터의 라우터 웹 페이지를 엽니다. 여기서 포트를 포워딩합니다</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Choose which Network(s) you want to use</source>
        <translation>사용할 네트워크 선택</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Kad</source>
        <translation>Kad</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>eD2K</source>
        <translation>eD2K</translation>
    </message>
    <message>
        <source>Safe Connect</source>
        <translation type="vanished">안전 연결</translation>
    </message>
    <message>
        <source>Turning this feature off allows eMule to connect to servers a little faster, but can cause you to get more false LowID connects.</source>
        <translation type="vanished">이 기능을 끄면 eMule이 서버에 조금 더 빨리 연결하지만 잘못된 LowID 연결이 늘어날 수 있습니다.</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>Select your internet connection. eMule derives its download and upload limits from it, leaving room for your other applications. You can change the limits at any time in the Options.</source>
        <translation>인터넷 연결을 선택하세요. eMule은 이를 바탕으로 다운로드 및 업로드 제한을 정하고 다른 프로그램을 위한 여유를 남겨 둡니다. 제한은 옵션에서 언제든지 변경할 수 있습니다.</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Type</source>
        <translation>유형</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Down</source>
        <translation>다운로드</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Up</source>
        <translation>업로드</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Keep current settings</source>
        <translation>현재 설정 유지</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unknown (recommended defaults)</source>
        <translation>알 수 없음 (권장 기본값)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Custom</source>
        <translation>사용자 지정</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+1"/>
        <source>(enter below)</source>
        <translation>(아래에 입력)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1 Mbit/s</source>
        <translation>%1 Mbit/s</translation>
    </message>
    <message>
        <location line="+23"/>
        <source> Mbit/s</source>
        <translation> Mbit/s</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Download:</source>
        <translation>다운로드:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload:</source>
        <translation>업로드:</translation>
    </message>
    <message>
        <location line="+7"/>
        <source> KB/s</source>
        <translation> KB/s</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Download limit:</source>
        <translation>다운로드 제한:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload limit:</source>
        <translation>업로드 제한:</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>&lt; Back</source>
        <translation>&lt; 뒤로</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+28"/>
        <source>Next &gt;</source>
        <translation>다음 &gt;</translation>
    </message>
    <message>
        <location line="+38"/>
        <source>UDP is disabled in the config file (udpPort: 0)</source>
        <translation>설정 파일에서 UDP가 비활성화되어 있습니다 (udpPort: 0)</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Kad needs UDP, which is disabled in the config file</source>
        <translation>Kad에는 UDP가 필요하지만 설정 파일에서 비활성화되어 있습니다</translation>
    </message>
    <message>
        <source>Welcome to eMule</source>
        <translation type="vanished">eMule에 오신 것을 환영합니다</translation>
    </message>
    <message>
        <source>General</source>
        <translation type="vanished">일반</translation>
    </message>
    <message>
        <source>User Name</source>
        <translation type="vanished">사용자 이름</translation>
    </message>
    <message>
        <source>Download</source>
        <translation type="vanished">다운로드</translation>
    </message>
    <message>
        <source>Upload</source>
        <translation type="vanished">업로드</translation>
    </message>
    <message>
        <source>Priority</source>
        <translation type="vanished">우선순위</translation>
    </message>
    <message>
        <source>Security</source>
        <translation type="vanished">보안</translation>
    </message>
    <message>
        <source>Obfuscation</source>
        <translation type="vanished">난독화</translation>
    </message>
    <message>
        <location line="-47"/>
        <source>Connection Speed</source>
        <translation>연결 속도</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bandwidth</source>
        <translation>대역폭</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Finish</source>
        <translation>완료</translation>
    </message>
    <message>
        <location line="-25"/>
        <source>Cancel</source>
        <translation>취소</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Help</source>
        <translation>도움말</translation>
    </message>
    <message>
        <source>Download limit: %1    Upload limit: %2</source>
        <translation type="vanished">다운로드 제한: %1    업로드 제한: %2</translation>
    </message>
    <message>
        <location line="+243"/>
        <source>The ports are forwarded when the core starts.</source>
        <translation>포트는 코어가 시작될 때 전달됩니다.</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>The core did not accept the port settings.</source>
        <translation>코어가 포트 설정을 받아들이지 않았습니다.</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>No answer from the core about port forwarding.</source>
        <translation>포트 전달에 대해 코어의 응답이 없습니다.</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Network</source>
        <translation>네트워크</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>You must enable at least one network (Kad or eD2K).</source>
        <translation>최소 하나의 네트워크(Kad 또는 eD2K)를 활성화해야 합니다.</translation>
    </message>
    <message>
        <source>UPnP</source>
        <translation type="vanished">UPnP</translation>
    </message>
    <message>
        <source>UPnP port mapping timed out. Your router may not support UPnP, or it may be disabled. You can set up port forwarding manually.</source>
        <translation type="vanished">UPnP 포트 매핑 시간이 초과되었습니다. 라우터가 UPnP를 지원하지 않거나 비활성화되어 있을 수 있습니다. 포트 포워딩을 수동으로 설정할 수 있습니다.</translation>
    </message>
</context>
<context>
    <name>eMule::FriendListModel</name>
    <message>
        <location filename="../src/gui/controls/FriendListModel.cpp" line="+36"/>
        <source>%1 (ed3k)</source>
        <translation>%1 (ed3k)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>ed3k friend, request pending</source>
        <translation>ed3k 친구, 요청 대기 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>ed3k friend</source>
        <translation>ed3k 친구</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>ed3k friend, not confirmed by the other side</source>
        <translation>ed3k 친구, 상대방이 확인하지 않음</translation>
    </message>
</context>
<context>
    <name>eMule::HosterPanel</name>
    <message>
        <location filename="../src/gui/panels/HosterPanel.cpp" line="+61"/>
        <source>Finding a resolver</source>
        <translation>리졸버 찾는 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Looking up in ed3k</source>
        <translation>ed3k에서 조회 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Checking</source>
        <translation>확인 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Queued</source>
        <translation>대기 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Waiting</source>
        <translation>대기 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloading</source>
        <translation>다운로드 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Puzzle</source>
        <translation>인증 문자</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paused</source>
        <translation>일시정지됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Complete</source>
        <translation>완료</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>File removed</source>
        <translation>파일 삭제됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Needs you</source>
        <translation>사용자 조치 필요</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>No resolver</source>
        <translation>리졸버 없음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Failed</source>
        <translation>실패</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Waiting for the ed3k node</source>
        <translation>ed3k 노드 대기 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>This build has no hoster resolver</source>
        <translation>이 빌드에는 호스터 리졸버가 없습니다</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The resolver is not ready: %1</source>
        <translation>리졸버가 준비되지 않았습니다: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>No resolver plugin for %1</source>
        <translation>%1용 리졸버 플러그인이 없습니다</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloads from hosters are off (Options, ed3k, Hoster)</source>
        <translation>호스터 다운로드가 꺼져 있습니다(옵션 &gt; ed3k &gt; 호스터)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The link was refused (%1)</source>
        <translation>링크가 거부되었습니다(%1)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Busy</source>
        <translation>사용 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hoster timer</source>
        <translation>호스터 대기 시간</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hoster quota</source>
        <translation>호스터 할당량</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hoster busy</source>
        <translation>호스터 사용 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The hoster wants a login (Options, ed3k, Hoster)</source>
        <translation>호스터가 로그인을 요구합니다(옵션 &gt; ed3k &gt; 호스터)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The hoster wants a puzzle this resolver cannot show</source>
        <translation>호스터가 이 리졸버로 표시할 수 없는 인증을 요구합니다</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>A puzzle, a login or a quota stands in the way</source>
        <translation>인증, 로그인 또는 할당량 때문에 진행할 수 없습니다</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The hoster ended the transfer early</source>
        <translation>호스터가 전송을 일찍 종료했습니다</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The hoster says the file was removed</source>
        <translation>호스터에 따르면 파일이 삭제되었습니다</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The hoster&apos;s file is not the one ed3k knows</source>
        <translation>호스터의 파일이 ed3k가 아는 파일과 다릅니다</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The hoster&apos;s file has another size</source>
        <translation>호스터의 파일 크기가 다릅니다</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Transfer failed</source>
        <translation>전송 실패</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Resolver error: %1</source>
        <translation>리졸버 오류: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Cannot write the file: %1</source>
        <translation>파일을 쓸 수 없습니다: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Cannot move the file to %1</source>
        <translation>파일을 %1(으)로 이동할 수 없습니다</translation>
    </message>
    <message numerus="yes">
        <location line="+1"/>
        <source>Folder: %n link(s)</source>
        <translation>
            <numerusform>폴더: 링크 %n개</numerusform>
        </translation>
    </message>
    <message>
        <location line="+1"/>
        <source>From a link a peer named (%1)</source>
        <translation>피어가 알려준 링크에서 (%1)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The hoster link is offline, no longer published</source>
        <translation>호스터 링크가 오프라인이므로 더 이상 게시되지 않습니다</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Hoster downloads</source>
        <translation>호스터 다운로드</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+310"/>
        <source>Add links...</source>
        <translation>링크 추가...</translation>
    </message>
    <message>
        <location line="+150"/>
        <source>JDownloader %1</source>
        <translation>JDownloader %1</translation>
    </message>
    <message>
        <source>Options...</source>
        <translation type="vanished">옵션...</translation>
    </message>
    <message>
        <location line="-454"/>
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
        <source>Progress</source>
        <translation>진행률</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Speed</source>
        <translation>속도</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Hoster</source>
        <translation>호스터</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Status</source>
        <translation>상태</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Source</source>
        <translation>소스</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Add hoster links...</source>
        <translation>호스터 링크 추가...</translation>
    </message>
    <message numerus="yes">
        <location line="+12"/>
        <source>Remove %n download(s) from the list?</source>
        <translation>
            <numerusform>다운로드 %n개를 목록에서 제거하시겠습니까?</numerusform>
        </translation>
    </message>
    <message>
        <location line="+62"/>
        <location line="+33"/>
        <source>Hoster links</source>
        <translation>호스터 링크</translation>
    </message>
    <message>
        <location line="-26"/>
        <source>Add hoster links</source>
        <translation>호스터 링크 추가</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Links to files at hosters, one per line:</source>
        <translation>호스터의 파일 링크(한 줄에 하나씩):</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Package Name:</source>
        <translation>패키지 이름:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>optional</source>
        <translation>선택 사항</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The links download under one entry, into a folder of this name</source>
        <translation>링크가 하나의 항목 아래 이 이름의 폴더로 다운로드됩니다</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Not connected to the eMule core.</source>
        <translation>eMule 코어에 연결되지 않았습니다.</translation>
    </message>
    <message numerus="yes">
        <location line="+118"/>
        <source>%n hoster(s)</source>
        <translation>
            <numerusform>호스터 %n개</numerusform>
        </translation>
    </message>
    <message>
        <location line="+3"/>
        <source>%1 of %2 complete</source>
        <translation>%2개 중 %1개 완료</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Mirror %1 of %2</source>
        <translation>미러 %1 / %2</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>ed3k + link</source>
        <translation>ed3k + 링크</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>link</source>
        <translation>링크</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Known in ed3k: every block is verified, peers help</source>
        <translation>ed3k에 알려짐: 모든 블록 검증, 피어 지원</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unknown in ed3k: checked as a whole when complete</source>
        <translation>ed3k에 알려지지 않음: 완료 시 전체 검사</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Pause</source>
        <translation>일시정지</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Resume</source>
        <translation>재개</translation>
    </message>
    <message>
        <location line="-269"/>
        <location line="+271"/>
        <source>Remove</source>
        <translation>제거</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+35"/>
        <source>Remove and delete file</source>
        <translation>제거 및 파일 삭제</translation>
    </message>
    <message>
        <location line="-33"/>
        <source>Copy link</source>
        <translation>링크 복사</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Open folder</source>
        <translation>폴더 열기</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Paste links</source>
        <translation>링크 붙여넣기</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Delete the selected files from disk?</source>
        <translation>선택한 파일을 디스크에서 삭제하시겠습니까?</translation>
    </message>
    <message>
        <location line="+61"/>
        <source>Links of a folder</source>
        <translation>폴더의 링크</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The link names a folder. Add which of its files?</source>
        <translation>이 링크는 폴더를 가리킵니다. 어떤 파일을 추가할까요?</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Add to the package &quot;%1&quot;</source>
        <translation>패키지 &quot;%1&quot;에 추가</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Add as a new package</source>
        <translation>새 패키지로 추가</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Add as a new package &quot;%1&quot;</source>
        <translation>새 패키지 &quot;%1&quot;(으)로 추가</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Folder</source>
        <translation>폴더</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Downloads from hosters are off (Options, ed3k, Hoster).</source>
        <translation>호스터 다운로드가 꺼져 있습니다(옵션 &gt; ed3k &gt; 호스터).</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Resolver not ready: %1</source>
        <translation>리졸버 준비 안 됨: %1</translation>
    </message>
</context>
<context>
    <name>eMule::HosterPuzzleDialog</name>
    <message>
        <location filename="../src/gui/dialogs/HosterPuzzleDialog.cpp" line="+22"/>
        <source>Puzzle from %1</source>
        <translation>%1의 인증</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>For %1</source>
        <translation>%1용</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>The puzzle image could not be shown.</source>
        <translation>인증 이미지를 표시할 수 없습니다.</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Type what the image shows</source>
        <translation>이미지에 보이는 내용을 입력하세요</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Skip</source>
        <translation>건너뛰기</translation>
    </message>
    <message numerus="yes">
        <location line="+24"/>
        <source>%n second(s) left</source>
        <translation>
            <numerusform>%n초 남음</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>eMule::ImportDownloadsDialog</name>
    <message>
        <location filename="../src/gui/dialogs/ImportDownloadsDialog.cpp" line="+40"/>
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
        <location line="+224"/>
        <source>Idle</source>
        <translation>유휴</translation>
    </message>
    <message>
        <location line="-213"/>
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
        <location line="+13"/>
        <source>Import Downloads</source>
        <translation>다운로드 가져오기</translation>
    </message>
    <message>
        <location line="-12"/>
        <source>Import Downloads is only available for local connections.</source>
        <translation>다운로드 가져오기는 로컬 연결에서만 사용 가능합니다.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Select folder to scan for importable downloads</source>
        <translation>가져올 수 있는 다운로드를 검색할 폴더 선택</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Do you want to delete the source files after a successful import?</source>
        <translation>가져오기가 성공하면 원본 파일을 삭제하시겠습니까?</translation>
    </message>
    <message>
        <location line="+118"/>
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
    <name>eMule::IndexerResultsModel</name>
    <message>
        <location filename="../src/gui/controls/IndexerResultsModel.cpp" line="+26"/>
        <source>today</source>
        <translation>오늘</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>1 day</source>
        <translation>1일</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1 days</source>
        <translation>%1일</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1 months</source>
        <translation>%1개월</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1 years</source>
        <translation>%1년</translation>
    </message>
    <message>
        <location line="+109"/>
        <source>Posted: %1</source>
        <translation>게시일: %1</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>%1 files</source>
        <translation>%1개 파일</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Password protected</source>
        <translation>비밀번호 보호됨</translation>
    </message>
    <message>
        <location line="+31"/>
        <source>Name</source>
        <translation>이름</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Size</source>
        <translation>크기</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Age</source>
        <translation>경과</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Category</source>
        <translation>카테고리</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Grabs</source>
        <translation>받은 횟수</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Indexer</source>
        <translation>인덱서</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Seeders</source>
        <translation>시더</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Peers</source>
        <translation>피어</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Known</source>
        <translation>알려진</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Confidence</source>
        <translation>신뢰도</translation>
    </message>
</context>
<context>
    <name>eMule::IpcClient</name>
    <message>
        <location filename="../src/gui/app/IpcClient.cpp" line="+1034"/>
        <source>The core&apos;s certificate changed</source>
        <translation>코어 인증서가 변경되었습니다</translation>
    </message>
</context>
<context>
    <name>eMule::IrcPanel</name>
    <message>
        <location filename="../src/gui/panels/IrcPanel.cpp" line="+146"/>
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
        <location line="+20"/>
        <source>Connecting</source>
        <translation>연결 중</translation>
    </message>
    <message>
        <location line="+73"/>
        <source>Connected</source>
        <translation>연결됨</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Disconnect</source>
        <translation>연결 해제</translation>
    </message>
    <message>
        <location line="+27"/>
        <location line="+493"/>
        <source>Connect</source>
        <translation>연결</translation>
    </message>
    <message>
        <location line="-482"/>
        <source>* Disconnected</source>
        <translation>* 연결 끊김</translation>
    </message>
    <message>
        <location line="+153"/>
        <source>* %1 has joined %2</source>
        <translation>* %1 님이 %2에 들어왔습니다</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>* %1 has parted %2 (%3)</source>
        <translation>* %1 님이 %2에서 나갔습니다 (%3)</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>* %1 has quit (%2)</source>
        <translation>* %1 님이 종료했습니다 (%2)</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>* %1 was kicked by %2 (%3)</source>
        <translation>* %1 님이 %2 님에게 강퇴당했습니다 (%3)</translation>
    </message>
    <message>
        <location line="+30"/>
        <location filename="../src/gui/panels/IrcPanelEd3k.cpp" line="+302"/>
        <source>* %1 is now known as %2</source>
        <translation>* %1 님의 이름이 이제 %2입니다</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>* %1 sets mode: %2 %3</source>
        <translation>* %1 님이 모드 설정: %2 %3</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>* Channel Topic: %1</source>
        <translation>* 채널 주제: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>* %1 changes topic to &apos;%2&apos;</source>
        <translation>* %1 님이 주제를 &apos;%2&apos;(으)로 바꿨습니다</translation>
    </message>
    <message>
        <location line="+68"/>
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
        <location line="+66"/>
        <location line="+615"/>
        <source>Nick</source>
        <translation>닉네임</translation>
    </message>
    <message>
        <location line="-545"/>
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
        <location line="+76"/>
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
        <location line="+14"/>
        <source>Join</source>
        <translation>참가</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Channels</source>
        <translation>채널</translation>
    </message>
    <message>
        <location line="+325"/>
        <source>Nick (%1)</source>
        <translation>닉네임 (%1)</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>* Private chat session started</source>
        <translation>* 비공개 채팅 세션 시작됨</translation>
    </message>
    <message>
        <location line="+33"/>
        <location filename="../src/gui/panels/IrcPanelEd3k.cpp" line="+236"/>
        <source>Nick : %1</source>
        <translation>닉네임: %1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Private Message</source>
        <translation>비공개 메시지</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Add to friends list</source>
        <translation>친구 목록에 추가</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Send this to friend: </source>
        <translation>친구에게 보내기: </translation>
    </message>
    <message>
        <location line="+1"/>
        <source>No Shared File Selected</source>
        <translation>선택된 공유 파일 없음</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Kick</source>
        <translation>강퇴</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Ban</source>
        <translation>차단</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Slap</source>
        <translation>때리기</translation>
    </message>
    <message>
        <location line="+95"/>
        <source>%1 attempted to send you a file. If you wanted to accept the files from this person, enable Receive files in the IRC Preferences.</source>
        <translation>%1 님이 파일을 보내려고 했습니다. 이 사용자의 파일을 받으려면 IRC 환경설정에서 파일 수신을 켜십시오.</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>%1 sent (%2) to be auto started.</source>
        <translation>%1 님이 자동 시작할 (%2)을(를) 보냈습니다.</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>%1 attempted to send you a file. If you wanted to accept the files from this person, add him as a friend or change your IRC Preferences.</source>
        <translation>%1 님이 파일을 보내려고 했습니다. 이 사용자의 파일을 받으려면 친구로 추가하거나 IRC 환경설정을 변경하십시오.</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>%1 added you as friend!</source>
        <translation>%1 님이 당신을 친구로 추가했습니다!</translation>
    </message>
    <message>
        <location filename="../src/gui/panels/IrcPanelEd3k.cpp" line="-462"/>
        <location line="+20"/>
        <source>Public: anyone with the link</source>
        <translation>공개: 링크가 있는 누구나</translation>
    </message>
    <message>
        <location line="-19"/>
        <location line="+14"/>
        <source>Private: posts sealed, anyone with the link</source>
        <translation>비공개: 글 봉인, 링크가 있는 누구나</translation>
    </message>
    <message>
        <location line="-13"/>
        <location line="+14"/>
        <source>Closed: only admitted members</source>
        <translation>폐쇄형: 승인된 멤버만</translation>
    </message>
    <message>
        <location line="-13"/>
        <source>Moderated: only admitted members post</source>
        <translation>관리됨: 승인된 멤버만 글쓰기</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Title:</source>
        <translation>제목:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>About:</source>
        <translation>소개:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Your nick:</source>
        <translation>닉네임:</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+9"/>
        <source>Kind:</source>
        <translation>종류:</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>the ed3k node is not running</source>
        <translation>ed3k 노드가 실행 중이 아님</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>not a valid room link or admit</source>
        <translation>올바른 방 링크나 승인이 아닙니다</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>no key of the current generation yet</source>
        <translation>아직 현재 세대의 키가 없습니다</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>too many rooms (at most 16)</source>
        <translation>방이 너무 많습니다 (최대 16개)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>not allowed (not admitted, banned or no moderator)</source>
        <translation>허용되지 않음 (미승인, 차단됨 또는 관리자 아님)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>unknown room</source>
        <translation>알 수 없는 방</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>a key that cannot be used here</source>
        <translation>여기서 사용할 수 없는 키</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>the room keys could not be stored</source>
        <translation>방 키를 저장할 수 없습니다</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>error %1</source>
        <translation>오류 %1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>room %1</source>
        <translation>방 %1</translation>
    </message>
    <message>
        <location line="+14"/>
        <location line="+233"/>
        <location line="+47"/>
        <location line="+339"/>
        <source>ed3k room</source>
        <translation>ed3k 방</translation>
    </message>
    <message>
        <location line="-618"/>
        <source>Join the ed3k room &quot;%1&quot;?

Other members see your nick and posts; your node takes part in passing the room&apos;s posts on.</source>
        <translation>ed3k 방 &quot;%1&quot;에 참가할까요?

다른 멤버가 내 닉네임과 글을 볼 수 있으며, 내 노드는 방의 글을 전달하는 데 참여합니다.</translation>
    </message>
    <message>
        <location line="+58"/>
        <source>Rooms (ed3k)</source>
        <translation>방 (ed3k)</translation>
    </message>
    <message>
        <location line="+87"/>
        <source>* %1 has left the room</source>
        <translation>* %1님이 방을 나갔습니다</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>* %1 has joined the room</source>
        <translation>* %1님이 방에 들어왔습니다</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>* Now in the room</source>
        <translation>* 방에 참가했습니다</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>* %1 was banned</source>
        <translation>* %1님이 차단되었습니다</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>* Room key of generation %1 taken</source>
        <translation>* 세대 %1의 방 키를 받았습니다</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>* The room key of generation %1 is missing: posting waits for a moderator</source>
        <translation>* 세대 %1의 방 키가 없습니다: 글쓰기가 관리자를 기다립니다</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>* A moderator&apos;s room key (generation %1) does not open</source>
        <translation>* 관리자의 방 키(세대 %1)를 열 수 없습니다</translation>
    </message>
    <message>
        <source>* A member refused us: not admitted. Send a moderator your member key, then import the admit (Rooms (ed3k) tab)</source>
        <translation type="vanished">* 멤버가 거부했습니다: 미승인. 관리자에게 멤버 키를 보낸 후 승인을 가져오세요 (방 (ed3k) 탭)</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>* A member refused us: this room is closed and you are not admitted yet. Send your member key (shown above) to a moderator</source>
        <translation>* 멤버가 거부했습니다: 이 방은 비공개이며 아직 승인되지 않았습니다. 멤버 키(위에 표시됨)를 관리자에게 보내세요</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>* A member refused us: banned in this room</source>
        <translation>* 멤버가 거부했습니다: 이 방에서 차단됨</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>* A member sent a bad item</source>
        <translation>* 멤버가 잘못된 항목을 보냈습니다</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Leave this room? You own it: its room key stays on this computer, so joining by its link makes you the owner again.</source>
        <translation>이 방을 나갈까요? 내가 소유자입니다: 방 키가 이 컴퓨터에 남으므로 링크로 참가하면 다시 소유자가 됩니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Leave this room? To come back you need its link.</source>
        <translation>이 방을 나갈까요? 돌아오려면 링크가 필요합니다.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Leaving the room failed</source>
        <translation>방 나가기 실패</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>* Room link copied. It holds the room&apos;s secret: give it to members only</source>
        <translation>* 방 링크를 복사했습니다. 방의 비밀이 들어 있으니 멤버에게만 주세요</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>* Room link copied</source>
        <translation>* 방 링크를 복사했습니다</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>* Your member key of this room was copied: send it to a moderator to be admitted</source>
        <translation>* 이 방의 멤버 키를 복사했습니다: 승인받으려면 관리자에게 보내세요</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Import admit</source>
        <translation>승인 가져오기</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paste the admit text a moderator gave you:</source>
        <translation>관리자에게 받은 승인 텍스트를 붙여 넣으세요:</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>The admit was not taken</source>
        <translation>승인이 받아들여지지 않았습니다</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>* Admit taken in: you can join the room&apos;s members now</source>
        <translation>* 승인을 받았습니다: 이제 방의 멤버에 참가할 수 있습니다</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Your nick in this room:</source>
        <translation>이 방에서의 닉네임:</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>The nick was not changed</source>
        <translation>닉네임이 변경되지 않았습니다</translation>
    </message>
    <message>
        <location line="+88"/>
        <source>* Not posted: %1</source>
        <translation>* 게시되지 않음: %1</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Copy member key</source>
        <translation>멤버 키 복사</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Reply to last post</source>
        <translation>마지막 게시물에 답장</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Unmute</source>
        <translation>음소거 해제</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Mute</source>
        <translation>음소거</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Import admit...</source>
        <translation>승인 가져오기...</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+125"/>
        <source>Admit</source>
        <translation>승인</translation>
    </message>
    <message>
        <location line="-124"/>
        <source>Ban (30 days)</source>
        <translation>차단 (30일)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Lift ban</source>
        <translation>차단 해제</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Remove moderator</source>
        <translation>관리자 해제</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Make moderator</source>
        <translation>관리자로 지정</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Admit a member key...</source>
        <translation>멤버 키 승인...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>New generation</source>
        <translation>새 세대</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Create an ed3k room</source>
        <translation>ed3k 방 만들기</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>The room was not created</source>
        <translation>방을 만들지 못했습니다</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>* ed3k room created. Copy its link from the Rooms (ed3k) tab to invite members</source>
        <translation>* ed3k 방을 만들었습니다. 멤버를 초대하려면 방 (ed3k) 탭에서 링크를 복사하세요</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Join an ed3k room</source>
        <translation>ed3k 방 참가</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Room link (ed3k:room?...):</source>
        <translation>방 링크 (ed3k:room?...):</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>* Not an ed3k room link</source>
        <translation>* ed3k 방 링크가 아닙니다</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>The room was not joined</source>
        <translation>방에 참가하지 못했습니다</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Room info</source>
        <translation>방 정보</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>The room info was not changed</source>
        <translation>방 정보가 변경되지 않았습니다</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Admit a member</source>
        <translation>멤버 승인</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The newcomer&apos;s member key (52 characters):</source>
        <translation>새 멤버의 멤버 키 (52자):</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Not admitted</source>
        <translation>승인되지 않음</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>The admit is in the clipboard. Hand it to the newcomer, who imports it (Rooms (ed3k) tab, Import admit).</source>
        <translation>승인이 클립보드에 있습니다. 새 멤버에게 전달하면 가져오기를 합니다 (방 (ed3k) 탭, 승인 가져오기).</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>The moderation failed</source>
        <translation>관리 작업에 실패했습니다</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>* New generation %1 started</source>
        <translation>* 새 세대 %1 시작됨</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>* Ban lifted</source>
        <translation>* 차단 해제됨</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>The moderators were not changed</source>
        <translation>관리자가 변경되지 않았습니다</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>* Your next post answers %1: %2</source>
        <translation>* 다음 게시물은 %1에 대한 답장입니다: %2</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Not muted</source>
        <translation>음소거되지 않음</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Not unmuted</source>
        <translation>음소거 해제되지 않음</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>* Member muted: its posts are hidden here</source>
        <translation>* 멤버 음소거됨: 해당 게시물은 여기에서 숨겨집니다</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>* Member unmuted</source>
        <translation>* 멤버 음소거 해제됨</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>no core connection</source>
        <translation>코어 연결 없음</translation>
    </message>
</context>
<context>
    <name>eMule::KadContactHistogram</name>
    <message>
        <location filename="../src/gui/controls/KadContactHistogram.cpp" line="+194"/>
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
        <source>Status</source>
        <translation type="vanished">상태</translation>
    </message>
    <message>
        <source>Client ID</source>
        <translation type="vanished">클라이언트 ID</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/KadContactsModel.cpp" line="+79"/>
        <source>Type</source>
        <translation>유형</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>ID</source>
        <translation>ID</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Distance</source>
        <translation>거리</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Country</source>
        <translation>국가</translation>
    </message>
</context>
<context>
    <name>eMule::KadLookupGraph</name>
    <message>
        <source>No search selected</source>
        <translation type="vanished">선택된 검색 없음</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/KadLookupGraph.cpp" line="+160"/>
        <location line="+274"/>
        <source>Distance</source>
        <translation>거리</translation>
    </message>
    <message>
        <location line="-272"/>
        <source>Time</source>
        <translation>시간</translation>
    </message>
    <message>
        <location line="+104"/>
        <source>Automatically show active searches</source>
        <translation>활성 검색 자동 표시</translation>
    </message>
    <message>
        <location line="+136"/>
        <source>Routing Table</source>
        <translation>라우팅 테이블</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1 Node(s)</source>
        <translation>%1 노드</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>No Response</source>
        <translation>응답 없음</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+14"/>
        <source>Asking</source>
        <translation>요청 중</translation>
    </message>
    <message>
        <location line="-6"/>
        <location line="+10"/>
        <source>Not Asked</source>
        <translation>요청 안 함</translation>
    </message>
    <message>
        <location line="-4"/>
        <source>None</source>
        <translation>없음</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Protocol Version</source>
        <translation>프로토콜 버전</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Discovered by</source>
        <translation>발견 출처</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Found Nodes (useful)</source>
        <translation>찾은 노드 (유용)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Search Results</source>
        <translation>검색 결과</translation>
    </message>
    <message>
        <source>Our node (search initiator)</source>
        <translation type="vanished">우리 노드(검색 시작자)</translation>
    </message>
</context>
<context>
    <name>eMule::KadPanel</name>
    <message>
        <location filename="../src/gui/panels/KadPanel.cpp" line="+115"/>
        <location line="+12"/>
        <location line="+206"/>
        <location line="+2"/>
        <location line="+50"/>
        <location line="+215"/>
        <source>▸ Contacts (0)</source>
        <translation>▸ 연락처 (0)</translation>
    </message>
    <message>
        <location line="-484"/>
        <location line="+12"/>
        <location line="+419"/>
        <location line="+99"/>
        <source>▸ Current Searches (0)</source>
        <translation>▸ 현재 검색 (0)</translation>
    </message>
    <message>
        <location line="-456"/>
        <source>The URL is invalid.</source>
        <translation>URL이 잘못되었습니다.</translation>
    </message>
    <message>
        <location line="+153"/>
        <location line="+2"/>
        <location line="+489"/>
        <source>▸ Search Details</source>
        <translation>▸ 검색 상세</translation>
    </message>
    <message>
        <location line="-394"/>
        <source>Recheck Firewall</source>
        <translation>방화벽 재확인</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+256"/>
        <source>Connect</source>
        <translation>연결</translation>
    </message>
    <message>
        <location line="-238"/>
        <source>IP or Address:</source>
        <translation>IP 또는 주소:</translation>
    </message>
    <message>
        <location line="-254"/>
        <location line="+251"/>
        <location line="+40"/>
        <source>Bootstrap</source>
        <translation>부트스트랩</translation>
    </message>
    <message>
        <location line="-300"/>
        <source>Downloading...</source>
        <translation>다운로드 중...</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+77"/>
        <source>Kademlia</source>
        <translation>Kademlia</translation>
    </message>
    <message>
        <source>Failed to download nodes.dat: %1</source>
        <translation type="vanished">nodes.dat 다운로드 실패: %1</translation>
    </message>
    <message>
        <source>Downloaded nodes.dat is empty.</source>
        <translation type="vanished">다운로드한 nodes.dat가 비어 있습니다.</translation>
    </message>
    <message>
        <source>Failed to save nodes.dat: %1</source>
        <translation type="vanished">nodes.dat 저장 실패: %1</translation>
    </message>
    <message>
        <source>IP Address:</source>
        <translation type="vanished">IP 주소:</translation>
    </message>
    <message>
        <location line="+179"/>
        <source>Port:</source>
        <translation>포트:</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Nodes.dat from URL:</source>
        <translation>URL에서 Nodes.dat:</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>From known clients</source>
        <translation>알려진 클라이언트에서</translation>
    </message>
    <message>
        <location line="+144"/>
        <location line="+3"/>
        <source>▸ Contacts (%1)</source>
        <translation>▸ 연락처 (%1)</translation>
    </message>
    <message>
        <location line="+41"/>
        <source>▸ Current Searches (%1)</source>
        <translation>▸ 현재 검색 (%1)</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Disconnect</source>
        <translation>연결 해제</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Cancel</source>
        <translation>취소</translation>
    </message>
    <message>
        <location line="+138"/>
        <source>▸ Search Details (%1)</source>
        <translation>▸ 검색 상세 (%1)</translation>
    </message>
</context>
<context>
    <name>eMule::KadSearchesModel</name>
    <message>
        <source>No.</source>
        <translation type="vanished">번호</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/KadSearchesModel.cpp" line="+36"/>
        <source>Stopping</source>
        <translation>중지 중</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Active</source>
        <translation>활성</translation>
    </message>
    <message>
        <location line="+42"/>
        <source>Number</source>
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
    <message>
        <location line="+8"/>
        <source>Search Sources</source>
        <translation>소스 검색</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Search Keywords</source>
        <translation>키워드 검색</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Node Lookup</source>
        <translation>노드 조회</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Store File</source>
        <translation>파일 저장</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Store Keyword</source>
        <translation>키워드 저장</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Find Buddy</source>
        <translation>버디 찾기</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Store Notes</source>
        <translation>노트 저장</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Notes</source>
        <translation>노트</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unknown</source>
        <translation>알 수 없음</translation>
    </message>
</context>
<context>
    <name>eMule::LogTextView</name>
    <message>
        <location filename="../src/gui/controls/LogTextView.cpp" line="+86"/>
        <source>Autoscroll</source>
        <translation>자동 스크롤</translation>
    </message>
</context>
<context>
    <name>eMule::LogWidget</name>
    <message>
        <location filename="../src/gui/controls/LogWidget.cpp" line="+87"/>
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
        <location line="+4"/>
        <source>Leecher</source>
        <translation>리처</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+2"/>
        <source>Usenet</source>
        <translation>Usenet</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+2"/>
        <source>Kad</source>
        <translation>Kad</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>ed3k</source>
        <translation>ed3k</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>ed3k Verbose</source>
        <translation>ed3k 상세</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>ed3k Kad</source>
        <translation>ed3k Kad</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+2"/>
        <source>IPC</source>
        <translation>IPC</translation>
    </message>
    <message>
        <location line="+441"/>
        <source>Click here to check if a new version is available</source>
        <translation>여기를 클릭하여 새 버전이 있는지 확인하세요</translation>
    </message>
</context>
<context>
    <name>eMule::MainWindow</name>
    <message>
        <location filename="../src/gui/app/MainWindow.cpp" line="+107"/>
        <location line="+627"/>
        <source>eMule Qt v%1</source>
        <translation>eMule Qt v%1</translation>
    </message>
    <message>
        <location line="-464"/>
        <location line="+9"/>
        <source>New Version Available</source>
        <translation>새 버전 사용 가능</translation>
    </message>
    <message>
        <location line="+258"/>
        <source>eD2K: Connected (LowID)</source>
        <translation>eD2K: 연결됨 (LowID)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>eD2K: Connected</source>
        <translation>eD2K: 연결됨</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>eD2K: Connecting...</source>
        <translation>eD2K: 연결 중...</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+1428"/>
        <source>eD2K: Disconnected</source>
        <translation>eD2K: 연결 해제됨</translation>
    </message>
    <message>
        <location line="-1368"/>
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
        <location line="+1360"/>
        <source>Kad: Disconnected</source>
        <translation>Kad: 연결 해제됨</translation>
    </message>
    <message>
        <location line="-1308"/>
        <source>Users: %1 | Files: %2</source>
        <translation>사용자: %1 | 파일: %2</translation>
    </message>
    <message>
        <location line="+475"/>
        <source>Open Incoming Folder...</source>
        <translation>수신 폴더 열기...</translation>
    </message>
    <message>
        <location line="+13"/>
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
        <location line="+34"/>
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
        <source>Features</source>
        <translation>기능</translation>
    </message>
    <message>
        <location line="-927"/>
        <location line="+7"/>
        <location line="+923"/>
        <source>Version Check</source>
        <translation>버전 확인</translation>
    </message>
    <message>
        <location line="-996"/>
        <source>Quit eMule Qt</source>
        <translation>eMule Qt 종료</translation>
    </message>
    <message>
        <location line="+46"/>
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
        <location line="+77"/>
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
        <location line="+78"/>
        <source>eMule Version %1 ready</source>
        <translation>eMule 버전 %1 준비됨</translation>
    </message>
    <message>
        <location line="+94"/>
        <source>Blocked: network interface not available</source>
        <translation>차단됨: 네트워크 인터페이스를 사용할 수 없음</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>eD2K Server: %1  (%2 Users)</source>
        <translation>eD2K 서버: %1  (사용자 %2명)</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Preparing...</source>
        <translation>준비 중...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Waiting...</source>
        <translation>대기 중...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Error!</source>
        <translation>오류!</translation>
    </message>
    <message>
        <location line="+62"/>
        <source>ed3k: Connected</source>
        <translation>ed3k: 연결됨</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>ed3k: Connected (Firewalled)</source>
        <translation>ed3k: 연결됨 (방화벽)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>ed3k: Connecting...</source>
        <translation>ed3k: 연결 중...</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>open</source>
        <translation>개방</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>cone NAT</source>
        <translation>콘 NAT</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>symmetric NAT</source>
        <translation>대칭형 NAT</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>unknown</source>
        <translation>알 수 없음</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>ed3k node %1
UDP port: %2
Contacts: %3
IPv4: %4
IPv6: %5
Router: %6</source>
        <translation>ed3k 노드 %1
UDP 포트: %2
연락처: %3
IPv4: %4
IPv6: %5
라우터: %6</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>yes</source>
        <translation>예</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>no</source>
        <translation>아니오</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>Up: %1(%2)</source>
        <translation>업: %1(%2)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Down: %1(%2)</source>
        <translation>다운: %1(%2)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Up: %1</source>
        <translation>업: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Down: %1</source>
        <translation>다운: %1</translation>
    </message>
    <message>
        <location line="+38"/>
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
        <location line="+80"/>
        <source>Confirm Exit</source>
        <translation>종료 확인</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Are you sure you want to exit eMule?</source>
        <translation>eMule을 종료하시겠습니까?</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>The eMule core stopped right after it was started.</source>
        <translation>eMule 코어가 시작 직후 중지되었습니다.</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>It left no reason, so it may have crashed. Crash reports go to %1.</source>
        <translation>남긴 원인이 없으므로 충돌했을 수 있습니다. 충돌 보고서 위치: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Reason: %1</source>
        <translation>원인: %1</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>The eMule core at %1 accepts the connection but does not answer. It may be busy or stuck.</source>
        <translation>%1의 eMule 코어가 연결은 수락하지만 응답하지 않습니다. 사용 중이거나 멈춘 상태일 수 있습니다.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>No eMule core is reachable at %1.</source>
        <translation>%1에서 연결할 수 있는 eMule 코어가 없습니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The eMule core at %1 refused the connection: %2</source>
        <translation>%1의 eMule 코어가 연결을 거부했습니다: %2</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>eMule Core</source>
        <translation>eMule 코어</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Click Retry to start it again.</source>
        <translation>다시 시작하려면 [다시 시도]를 클릭하세요.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>eMule keeps trying in the background.</source>
        <translation>eMule이 백그라운드에서 계속 시도합니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Retry</source>
        <translation>다시 시도</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Open Log</source>
        <translation>로그 열기</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Quit</source>
        <translation>종료</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Log Entry</source>
        <translation>로그 항목</translation>
    </message>
    <message>
        <location line="+141"/>
        <source>eMule Log</source>
        <translation>eMule 로그</translation>
    </message>
    <message>
        <location line="+87"/>
        <source>Open Downloads Folder in Browser</source>
        <translation>브라우저에서 다운로드 폴더 열기</translation>
    </message>
    <message>
        <location line="+42"/>
        <source>Open WebUI</source>
        <translation>웹 UI 열기</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Submit Bug Report...</source>
        <translation>버그 신고 제출...</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Edit Web Services...</source>
        <translation>웹 서비스 편집...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Web Services</source>
        <translation>웹 서비스</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>webservices.dat was not found in the config folder.</source>
        <translation>설정 폴더에서 webservices.dat를 찾을 수 없습니다.</translation>
    </message>
    <message>
        <location line="+6"/>
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
        <location line="+41"/>
        <source>Cannot Open Downloads Folder</source>
        <translation>다운로드 폴더를 열 수 없음</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Cannot Open Web Interface</source>
        <translation>웹 인터페이스를 열 수 없음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not connected to the core.</source>
        <translation>코어에 연결되지 않았습니다.</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Web Interface Disabled</source>
        <translation>웹 인터페이스 비활성화됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The web interface is disabled.

Enable it under Options → Web Interface, then try again.</source>
        <translation>웹 인터페이스가 비활성화되어 있습니다.

옵션 → 웹 인터페이스에서 활성화한 뒤 다시 시도하세요.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Open Options</source>
        <translation>옵션 열기</translation>
    </message>
    <message>
        <location line="+196"/>
        <source>Main</source>
        <translation>메인</translation>
    </message>
    <message>
        <location line="+164"/>
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
        <location line="+62"/>
        <source>Default</source>
        <translation>기본값</translation>
    </message>
    <message>
        <location line="-26"/>
        <source>Skin Profiles</source>
        <translation>스킨 프로필</translation>
    </message>
    <message>
        <location line="+3"/>
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
        <location line="+39"/>
        <source>Disconnect</source>
        <translation>연결 해제</translation>
    </message>
    <message>
        <location line="+5"/>
        <location filename="../src/gui/app/ToolbarButtons.h" line="+50"/>
        <source>Connect</source>
        <translation>연결</translation>
    </message>
    <message>
        <location line="+71"/>
        <source>Ready</source>
        <translation>준비됨</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Users: 0 | Files: 0</source>
        <translation>사용자: 0 | 파일: 0</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Up: 0.0</source>
        <translation>업로드: 0.0</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Down: 0.0</source>
        <translation>다운로드: 0.0</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Double-click for Network Information</source>
        <translation>네트워크 정보를 보려면 더블 클릭</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>New message — double-click to read</source>
        <translation>새 메시지 — 두 번 클릭하여 읽기</translation>
    </message>
    <message>
        <location line="+337"/>
        <source>eD2K Link</source>
        <translation>eD2K 링크</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Do you want to download and use the Kad nodes file from
%1 ?</source>
        <translation>다음 위치에서 Kad 노드 파일을 내려받아 사용하시겠습니까?
%1</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Kademlia</source>
        <translation>Kademlia</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Not connected to the core</source>
        <translation>코어에 연결되지 않음</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Starting the core…</source>
        <translation>코어를 시작하는 중…</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Connecting to the core… (attempt %1)</source>
        <translation>코어에 연결하는 중… (시도 %1)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Connecting to the core…</source>
        <translation>코어에 연결하는 중…</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Waiting for the core to answer…</source>
        <translation>코어의 응답을 기다리는 중…</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Connected to the core</source>
        <translation>코어에 연결됨</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The core is not responding — retrying (attempt %1)</source>
        <translation>코어가 응답하지 않습니다 — 다시 시도 중 (시도 %1)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The core refused the connection: %1</source>
        <translation>코어가 연결을 거부했습니다: %1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>The core is still loading its files…</source>
        <translation>코어가 아직 파일을 불러오는 중입니다…</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>The core is loading: client credits…</source>
        <translation>코어가 불러오는 중: 클라이언트 크레딧…</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The core is loading: known files…</source>
        <translation>코어가 불러오는 중: 알려진 파일…</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The core is loading: shared files…</source>
        <translation>코어가 불러오는 중: 공유 파일…</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The core is loading: downloads…</source>
        <translation>코어가 불러오는 중: 다운로드…</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The core is starting its networks…</source>
        <translation>코어가 네트워크를 시작하는 중…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The core is loading its files…</source>
        <translation>코어가 파일을 불러오는 중…</translation>
    </message>
    <message>
        <location line="+83"/>
        <source>Other clients cannot connect to you: eD2K gives you a Low ID and Kad reports you as firewalled.</source>
        <translation>다른 클라이언트가 연결할 수 없습니다: eD2K에서 Low ID를 받았고 Kad가 방화벽 뒤에 있다고 보고합니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Other clients cannot connect to you: Kad&apos;s firewall check reports you as firewalled.</source>
        <translation>다른 클라이언트가 연결할 수 없습니다: Kad 방화벽 검사 결과 방화벽 뒤에 있습니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Low ID</source>
        <translation>Low ID</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>You get fewer sources and slower downloads, and two Low ID clients can rarely exchange data. Usually your router or firewall does not forward your ports. UPnP or a port forwarding rule in your router fixes it in most cases.</source>
        <translation>소스가 줄고 다운로드가 느려지며, Low ID 클라이언트끼리는 거의 데이터를 주고받을 수 없습니다. 보통 라우터나 방화벽이 포트를 포워딩하지 않기 때문입니다. 대부분 UPnP나 라우터의 포트 포워딩 규칙으로 해결됩니다.</translation>
    </message>
    <message>
        <source>Other clients cannot connect to you: eD2K gives you a Low ID and Kad reports you as firewalled.

You get fewer sources and slower downloads, and two Low ID clients can rarely exchange data. Usually your router or firewall does not forward your ports. UPnP or a port forwarding rule in your router fixes it in most cases.</source>
        <translation type="vanished">다른 클라이언트가 접속할 수 없습니다: eD2K에서 Low ID를 받았고 Kad에서는 방화벽 뒤에 있다고 보고합니다.

소스가 줄고 다운로드가 느려지며, Low ID 클라이언트끼리는 거의 데이터를 주고받을 수 없습니다. 보통 라우터나 방화벽이 포트를 전달하지 않기 때문입니다. 대부분 UPnP나 라우터의 포트 포워딩 규칙으로 해결됩니다.</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Open Connection Settings</source>
        <translation>연결 설정 열기</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>More Info</source>
        <translation>자세한 정보</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Don&apos;t show this again</source>
        <translation>다시 표시하지 않음</translation>
    </message>
    <message>
        <location filename="../src/gui/app/ToolbarButtons.h" line="+1"/>
        <source>Kad</source>
        <translation>Kad</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Servers</source>
        <translation>서버</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Transfers</source>
        <translation>전송</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Search</source>
        <translation>검색</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Shared Files</source>
        <translation>공유 파일</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Messages</source>
        <translation>메시지</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>IRC</source>
        <translation>IRC</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Statistics</source>
        <translation>통계</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Usenet</source>
        <translation>Usenet</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Torrents</source>
        <translation>토렌트</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Hoster</source>
        <translation>호스터</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Downloads Folder</source>
        <translation>다운로드 폴더</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Options</source>
        <translation>옵션</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Tools</source>
        <translation>도구</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Help</source>
        <translation>도움말</translation>
    </message>
</context>
<context>
    <name>eMule::MassRenameDialog</name>
    <message>
        <location filename="../src/gui/dialogs/MassRenameDialog.cpp" line="+106"/>
        <source>Mass-Rename</source>
        <translation>일괄 이름 변경</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Filename mask (changes here will be done to all filenames below):</source>
        <translation>파일 이름 마스크 (여기서 변경하면 아래 모든 파일 이름에 적용됩니다):</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>&amp;Reset</source>
        <translation>초기화(&amp;R)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Filenames left-justified</source>
        <translation>파일 이름 왼쪽 정렬</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Filenames right-justified</source>
        <translation>파일 이름 오른쪽 정렬</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>New filenames:</source>
        <translation>새 파일 이름:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Original filenames:</source>
        <translation>원래 파일 이름:</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>&amp;Cleanup</source>
        <translation>정리(&amp;C)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Simple cleanup</source>
        <translation>간단 정리(&amp;S)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Insert text column from clipboard</source>
        <translation>클립보드에서 텍스트 열 삽입(&amp;I)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>OK</source>
        <translation>확인</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Cancel</source>
        <translation>취소</translation>
    </message>
    <message>
        <location line="+63"/>
        <source>Not enough filenames in the list of new filenames. Rename not possible.</source>
        <translation>새 파일 이름 목록에 파일 이름이 부족합니다. 이름을 변경할 수 없습니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Invalid filename in line %1. Rename not possible.</source>
        <translation>%1번째 줄의 파일 이름이 올바르지 않습니다. 이름을 변경할 수 없습니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Two or more equal filenames within the same directory are not allowed (Line %1). Rename not possible.</source>
        <translation>같은 폴더에 같은 파일 이름을 둘 이상 둘 수 없습니다 (%1번째 줄). 이름을 변경할 수 없습니다.</translation>
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
        <location filename="../src/gui/panels/MessagesPanel.cpp" line="+168"/>
        <location line="+108"/>
        <location line="+529"/>
        <source> ...failed</source>
        <translation> ...실패</translation>
    </message>
    <message>
        <location line="-635"/>
        <source>Me</source>
        <translation>나</translation>
    </message>
    <message>
        <location line="+83"/>
        <location line="+217"/>
        <source>ed3k %1</source>
        <translation>ed3k %1</translation>
    </message>
    <message>
        <location line="-199"/>
        <source> ...failed: the peer does not chat</source>
        <translation> ...실패: 상대가 채팅을 지원하지 않음</translation>
    </message>
    <message>
        <location line="+2"/>
        <source> ...failed: refused, try again in %1 s</source>
        <translation> ...실패: 거부됨, %1초 후에 다시 시도하십시오</translation>
    </message>
    <message>
        <location line="+1"/>
        <source> ...failed: refused</source>
        <translation> ...실패: 거부됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source> ...failed: not reachable</source>
        <translation> ...실패: 연결할 수 없음</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Friends (0)</source>
        <translation>친구 (0)</translation>
    </message>
    <message>
        <location line="+41"/>
        <source>Info</source>
        <translation>정보</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Name:</source>
        <translation>이름:</translation>
    </message>
    <message>
        <location line="+1"/>
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
        <location line="+36"/>
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
        <location line="+288"/>
        <source>Close</source>
        <translation>닫기</translation>
    </message>
    <message>
        <location line="-264"/>
        <location line="+228"/>
        <location line="+3"/>
        <source>&amp;Details...</source>
        <translation>상세(&amp;D)...</translation>
    </message>
    <message>
        <location line="-227"/>
        <source>Add...</source>
        <translation>추가...</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+245"/>
        <source>Remove</source>
        <translation>제거</translation>
    </message>
    <message>
        <location line="-242"/>
        <source>Send Message</source>
        <translation>메시지 보내기</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>View Shared Files</source>
        <translation>공유 파일 보기</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>cannot view shared files.</source>
        <translation>공유 파일을 볼 수 없습니다.</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Establish Friend Slot</source>
        <translation>친구 슬롯 설정</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Find...</source>
        <translation>찾기...</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Copy my ed3k friend link</source>
        <translation>내 ed3k 친구 링크 복사</translation>
    </message>
    <message>
        <location line="+35"/>
        <location line="+7"/>
        <source>Friends (%1)</source>
        <translation>친구 (%1)</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Request pending</source>
        <translation>요청 대기 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+46"/>
        <source>Successful</source>
        <translation>성공</translation>
    </message>
    <message>
        <location line="-45"/>
        <source>Not confirmed</source>
        <translation>확인되지 않음</translation>
    </message>
    <message>
        <location line="+46"/>
        <source>Invalid</source>
        <translation>잘못됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not supported or disabled</source>
        <translation>지원되지 않거나 비활성화됨</translation>
    </message>
    <message>
        <location line="+52"/>
        <source>Add to friends list</source>
        <translation>친구 목록에 추가</translation>
    </message>
    <message>
        <location line="+109"/>
        <source>*** Connecting</source>
        <translation>*** 연결 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>*** Authenticating friend</source>
        <translation>*** 친구 인증 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>*** Searching friend in Kad</source>
        <translation>*** Kad에서 친구 검색 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source> ...found</source>
        <translation> ...찾음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source> ...OK</source>
        <translation> ...OK</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>*** In order to avoid spam messages, this user requires you to solve a captcha before you can send him a message. Please enter the letters you see on this image as response:</source>
        <translation>*** 스팸 메시지를 피하기 위하여, 이 사용자에게 메시지를 보내기 전에 captcha를 풀어야 합니다. 그림에서 보이는 문자를 답장으로 입력하십시오:</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>*** You have passed the captcha check and the user has received your message.</source>
        <translation>*** captcha 확인을 통과하여 사용자가 메시지를 받았습니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>*** Your response to the captcha was wrong and your message has been ignored. You can request a new captcha by sending a new message.</source>
        <translation>*** captcha에 대한 응답이 잘못되어 메시지가 무시되었습니다. 새 메시지를 보내면 새로운 captcha를 요청할 수 있습니다.</translation>
    </message>
    <message>
        <location line="+335"/>
        <source>ed3k Friend Request</source>
        <translation>ed3k 친구 요청</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The ed3k node %1 asks to become your friend.</source>
        <translation>ed3k 노드 %1이(가) 친구 요청을 보냈습니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The ed3k node %1 asks to become your friend:

%2</source>
        <translation>ed3k 노드 %1이(가) 친구 요청을 보냈습니다:

%2</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Accept</source>
        <translation>수락</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Decline</source>
        <translation>거절</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Later</source>
        <translation>나중에</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>cannot read the ed3k friend link.</source>
        <translation>ed3k 친구 링크를 읽을 수 없습니다.</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>ed3k</source>
        <translation>ed3k</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>The ed3k node is not running.</source>
        <translation>ed3k 노드가 실행 중이 아닙니다.</translation>
    </message>
    <message>
        <source>Find Friend</source>
        <translation type="vanished">친구 찾기</translation>
    </message>
</context>
<context>
    <name>eMule::MetaAccountDialog</name>
    <message>
        <location filename="../src/gui/dialogs/MetaAccountDialog.cpp" line="+64"/>
        <source>User name:</source>
        <translation>사용자 이름:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Password:</source>
        <translation>비밀번호:</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Log In</source>
        <translation>로그인</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Check Again</source>
        <translation>다시 확인</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Log Out</source>
        <translation>로그아웃</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>eNode Account — %1</source>
        <translation>eNode 계정 — %1</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>&lt;b&gt;%1&lt;/b&gt; needs an account to download torrent and Usenet search results. Log in, or register an account on the server&apos;s website.</source>
        <translation>&lt;b&gt;%1&lt;/b&gt;에서 토렌트 및 Usenet 검색 결과를 다운로드하려면 계정이 필요합니다. 로그인하거나 서버 웹사이트에서 계정을 등록하세요.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Log in to your account on &lt;b&gt;%1&lt;/b&gt;.</source>
        <translation>&lt;b&gt;%1&lt;/b&gt;의 계정에 로그인합니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>is not active yet</source>
        <translation>은(는) 아직 활성화되지 않았습니다</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>has expired</source>
        <translation>은(는) 만료되었습니다</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>has been disabled by the operator</source>
        <translation>은(는) 운영자에 의해 비활성화되었습니다</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Your account &lt;b&gt;%1&lt;/b&gt; on %2 %3. Complete the steps below on the server&apos;s website, then choose Check Again.</source>
        <translation>%2의 계정 &lt;b&gt;%1&lt;/b&gt;%3. 서버 웹사이트에서 아래 단계를 완료한 다음 &apos;다시 확인&apos;을 선택하세요.</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>no expiry</source>
        <translation>만료 없음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Logged in to &lt;b&gt;%1&lt;/b&gt; as &lt;b&gt;%2&lt;/b&gt;.&lt;br&gt;Access until: %3</source>
        <translation>&lt;b&gt;%1&lt;/b&gt;에 &lt;b&gt;%2&lt;/b&gt;(으)로 로그인됨.&lt;br&gt;이용 기한: %3</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>&lt;b&gt;%1&lt;/b&gt; needs no account — its torrent and Usenet results are free to download.</source>
        <translation>&lt;b&gt;%1&lt;/b&gt;은(는) 계정이 필요 없습니다 — 토렌트 및 Usenet 결과를 자유롭게 다운로드할 수 있습니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Checking the account on &lt;b&gt;%1&lt;/b&gt;...</source>
        <translation>&lt;b&gt;%1&lt;/b&gt;의 계정 확인 중...</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Open steps:</source>
        <translation>남은 단계:</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Register an account</source>
        <translation>계정 등록</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Open account page</source>
        <translation>계정 페이지 열기</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Cancel</source>
        <translation>취소</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Close</source>
        <translation>닫기</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Not connected to the eMule core.</source>
        <translation>eMule 코어에 연결되지 않았습니다.</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Enter a user name and a password.</source>
        <translation>사용자 이름과 비밀번호를 입력하세요.</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Logging in...</source>
        <translation>로그인 중...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>The connection to the eMule core was lost.</source>
        <translation>eMule 코어와의 연결이 끊어졌습니다.</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>Checking...</source>
        <translation>확인 중...</translation>
    </message>
</context>
<context>
    <name>eMule::MetaResultActions</name>
    <message>
        <location filename="../src/gui/utils/MetaResultActions.cpp" line="+65"/>
        <source>NZB files (*.nzb)</source>
        <translation>NZB 파일 (*.nzb)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Link containers (*.dlc *.ccf *.rsdf)</source>
        <translation>링크 컨테이너 (*.dlc *.ccf *.rsdf)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Torrent files (*.torrent)</source>
        <translation>토렌트 파일 (*.torrent)</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Save NZB File</source>
        <translation>NZB 파일 저장</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Save Link Container</source>
        <translation>링크 컨테이너 저장</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Save Torrent File</source>
        <translation>토렌트 파일 저장</translation>
    </message>
    <message>
        <location line="+53"/>
        <source>Save Files To</source>
        <translation>파일 저장 위치</translation>
    </message>
    <message numerus="yes">
        <location line="+33"/>
        <source>Copied %n magnet link(s) to the clipboard.</source>
        <translation>
            <numerusform>마그넷 링크 %n개를 클립보드에 복사했습니다.</numerusform>
        </translation>
    </message>
    <message>
        <location line="+66"/>
        <source>Queued &quot;%1&quot; for Usenet download.</source>
        <translation>&quot;%1&quot;을(를) Usenet 다운로드 대기열에 추가했습니다.</translation>
    </message>
    <message>
        <location line="+41"/>
        <source>Download</source>
        <translation>다운로드</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>%1

Download it again?</source>
        <translation>%1

다시 다운로드할까요?</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Could not queue &quot;%1&quot;: %2</source>
        <translation>&quot;%1&quot;을(를) 대기열에 추가하지 못했습니다: %2</translation>
    </message>
    <message>
        <location line="+41"/>
        <location line="+87"/>
        <source>Could not download &quot;%1&quot;: %2</source>
        <translation>&quot;%1&quot;을(를) 다운로드할 수 없습니다: %2</translation>
    </message>
    <message>
        <location line="-67"/>
        <source>Could not write %1: %2</source>
        <translation>%1을(를) 쓸 수 없습니다: %2</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Saved %1</source>
        <translation>%1 저장됨</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+24"/>
        <source>Could not fetch the links of &quot;%1&quot;: %2</source>
        <translation>&quot;%1&quot;의 링크를 가져올 수 없습니다: %2</translation>
    </message>
    <message>
        <location line="-19"/>
        <location line="+26"/>
        <source>&quot;%1&quot; holds no links.</source>
        <translation>&quot;%1&quot;에 링크가 없습니다.</translation>
    </message>
    <message numerus="yes">
        <location line="+9"/>
        <source>Added %n file(s) of &quot;%1&quot; to the Hoster tab.</source>
        <translation>
            <numerusform>&quot;%1&quot;의 파일 %n개를 호스터 탭에 추가했습니다.</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>Added %n link(s) of &quot;%1&quot; to the Hoster tab.</source>
        <translation type="vanished">
            <numerusform>&quot;%1&quot;의 링크 %n개를 호스터 탭에 추가했습니다.</numerusform>
        </translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Could not add the links of &quot;%1&quot;: downloads from hosters are off.</source>
        <translation>&quot;%1&quot;의 링크를 추가할 수 없습니다: 호스터 다운로드가 꺼져 있습니다.</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Could not add &quot;%1&quot;: the torrent is empty.</source>
        <translation>&quot;%1&quot;을(를) 추가할 수 없습니다: 토렌트가 비어 있습니다.</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Login cancelled: the file was not fetched.</source>
        <translation>로그인이 취소되었습니다: 파일을 가져오지 않았습니다.</translation>
    </message>
    <message>
        <location line="+70"/>
        <source>Search</source>
        <translation>검색</translation>
    </message>
</context>
<context>
    <name>eMule::MetaResultDetailDialog</name>
    <message>
        <location filename="../src/gui/dialogs/MetaResultDetailDialog.cpp" line="+65"/>
        <source>Details: %1</source>
        <translation>상세: %1</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Fetching the torrent...</source>
        <translation>토렌트를 가져오는 중...</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Fetching the NZB...</source>
        <translation>NZB를 가져오는 중...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>General</source>
        <translation>일반</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+2"/>
        <location line="+106"/>
        <source>Name</source>
        <translation>이름</translation>
    </message>
    <message>
        <location line="-108"/>
        <location line="+2"/>
        <location line="+110"/>
        <source>Size</source>
        <translation>크기</translation>
    </message>
    <message>
        <location line="-110"/>
        <location line="+143"/>
        <source>Articles</source>
        <translation>기사</translation>
    </message>
    <message>
        <location line="-143"/>
        <location line="+107"/>
        <source>Type</source>
        <translation>유형</translation>
    </message>
    <message>
        <location line="-100"/>
        <source>Unchecked files are not downloaded. The volumes of one archive are checked together, and PAR2 files are fetched only when a repair needs them.</source>
        <translation>선택을 해제한 파일은 다운로드되지 않습니다. 한 압축 파일의 볼륨은 함께 선택되며, PAR2 파일은 복구가 필요할 때만 가져옵니다.</translation>
    </message>
    <message>
        <location line="+10"/>
        <location line="+2"/>
        <location line="+95"/>
        <source>Files</source>
        <translation>파일</translation>
    </message>
    <message>
        <location line="-93"/>
        <source>URL</source>
        <translation>URL</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Tier</source>
        <translation>티어</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Trackers</source>
        <translation>트래커</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Download</source>
        <translation>다운로드</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>The file could not be read.</source>
        <translation>파일을 읽을 수 없습니다.</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Keep at least one file of &quot;%1&quot;.</source>
        <translation>&quot;%1&quot;의 파일은 최소 한 개를 남겨 두세요.</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Torrent</source>
        <translation>토렌트</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Usenet (NZB)</source>
        <translation>Usenet (NZB)</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Info hash v1</source>
        <translation>정보 해시 v1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Info hash v2</source>
        <translation>정보 해시 v2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Pieces</source>
        <translation>조각</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>%1 × %2</source>
        <translation>%1 × %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Private</source>
        <translation>비공개</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Yes</source>
        <translation>예</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>No</source>
        <translation>아니오</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Created by</source>
        <translation>만든 프로그램</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Created on</source>
        <translation>생성일</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Comment</source>
        <translation>댓글</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Poster</source>
        <translation>게시자</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Groups</source>
        <translation>그룹</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Posted</source>
        <translation>게시일</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>PAR2 files</source>
        <translation>PAR2 파일</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Password</source>
        <translation>비밀번호</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Included in the NZB</source>
        <translation>NZB에 포함됨</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Source</source>
        <translation>소스</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Category</source>
        <translation>카테고리</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Seeders / peers</source>
        <translation>시더 / 피어</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Age</source>
        <translation>경과</translation>
    </message>
    <message numerus="yes">
        <location line="+0"/>
        <source>%n day(s)</source>
        <translation>
            <numerusform>%n일</numerusform>
        </translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+52"/>
        <source>Files (%1)</source>
        <translation>파일 (%1)</translation>
    </message>
    <message>
        <location line="-19"/>
        <source>%1 (%2 missing)</source>
        <translation>%1 (%2개 누락)</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>PAR2</source>
        <translation>PAR2</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>PAR2 files are fetched when a repair needs them.</source>
        <translation>PAR2 파일은 복구가 필요할 때 가져옵니다.</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Web seed</source>
        <translation>웹 시드</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Trackers (%1)</source>
        <translation>트래커 (%1)</translation>
    </message>
</context>
<context>
    <name>eMule::MetadataPage</name>
    <message>
        <location filename="../src/gui/dialogs/MetadataPage.cpp" line="+108"/>
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
    <message>
        <location line="+37"/>
        <source>Copy</source>
        <translation>복사</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Select All</source>
        <translation>모두 선택</translation>
    </message>
</context>
<context>
    <name>eMule::MiniMuleWidget</name>
    <message>
        <location filename="../src/gui/app/MiniMuleWidget.cpp" line="+85"/>
        <source>Yes</source>
        <translation>예</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>No</source>
        <translation>아니오</translation>
    </message>
    <message>
        <location line="+116"/>
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
        <location filename="../src/gui/dialogs/NetworkInfoDialog.cpp" line="+57"/>
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
        <location line="+10"/>
        <source>&lt;b&gt;Not connected to daemon.&lt;/b&gt;</source>
        <translation>&lt;b&gt;데몬에 연결되지 않았습니다.&lt;/b&gt;</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Client</source>
        <translation>클라이언트</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Nick:</source>
        <translation>닉네임:</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+247"/>
        <source>Hash:</source>
        <translation>해시:</translation>
    </message>
    <message>
        <location line="-245"/>
        <source>TCP Port:</source>
        <translation>TCP 포트:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>UDP Port:</source>
        <translation>UDP 포트:</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>eD2K Network</source>
        <translation>eD2K 네트워크</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+188"/>
        <source>Connected</source>
        <translation>연결됨</translation>
    </message>
    <message>
        <location line="-186"/>
        <location line="+145"/>
        <location line="+39"/>
        <source>Connecting</source>
        <translation>연결 중</translation>
    </message>
    <message>
        <location line="-182"/>
        <location line="+145"/>
        <source>Disconnected</source>
        <translation>연결 해제됨</translation>
    </message>
    <message>
        <location line="-144"/>
        <location line="+145"/>
        <location line="+114"/>
        <source>Status:</source>
        <translation>상태:</translation>
    </message>
    <message>
        <location line="-253"/>
        <location line="+65"/>
        <location line="+156"/>
        <source>Users:</source>
        <translation>사용자:</translation>
    </message>
    <message>
        <location line="-220"/>
        <location line="+66"/>
        <location line="+156"/>
        <source>Files:</source>
        <translation>파일:</translation>
    </message>
    <message>
        <location line="-215"/>
        <source>Unknown</source>
        <translation>알 수 없음</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+2"/>
        <location line="+50"/>
        <location line="+98"/>
        <source>IP:Port:</source>
        <translation>IP:포트:</translation>
    </message>
    <message>
        <location line="-143"/>
        <location line="+147"/>
        <source>ID:</source>
        <translation>ID:</translation>
    </message>
    <message>
        <location line="-145"/>
        <source>Low ID</source>
        <translation>Low ID</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>High ID</source>
        <translation>High ID</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>server verified</source>
        <translation>서버 확인됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>inbound peer</source>
        <translation>수신 피어</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>port mapping</source>
        <translation>포트 매핑</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>assumed</source>
        <translation>가정됨</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Not verified, not advertised to peers</source>
        <translation>확인되지 않아 피어에게 알리지 않음</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>IPv6:</source>
        <translation>IPv6:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>IPv6 Status:</source>
        <translation>IPv6 상태:</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>eD2K Server</source>
        <translation>eD2K 서버</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Name:</source>
        <translation>이름:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Description:</source>
        <translation>설명:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Version:</source>
        <translation>버전:</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Connections:</source>
        <translation>연결 수:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Obfuscated</source>
        <translation>난독화됨</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Normal</source>
        <translation>보통</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Low ID:</source>
        <translation>Low ID:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Ping:</source>
        <translation>핑:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1 ms</source>
        <translation>%1 ms</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>eD2K Server Features</source>
        <translation>eD2K 서버 기능</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Soft/Hard File Limits:</source>
        <translation>소프트/하드 파일 제한:</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>TCP compression</source>
        <translation>TCP 압축</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Short tags</source>
        <translation>짧은 태그</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Integer type tags</source>
        <translation>정수형 태그</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+1"/>
        <source>Extended UDP protocol for source requests</source>
        <translation>소스 요청용 확장 UDP 프로토콜</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Extended UDP protocol for file requests</source>
        <translation>파일 요청용 확장 UDP 프로토콜</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Support for large files</source>
        <translation>대용량 파일 지원</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+1"/>
        <source>Protocol Obfuscation</source>
        <translation>프로토콜 난독화</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>LowID hole punching</source>
        <translation>LowID 홀 펀칭</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>NAT callback (Lugdunum)</source>
        <translation>NAT 콜백 (Lugdunum)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hole punching extension</source>
        <translation>홀 펀칭 확장</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>NAT rendezvous</source>
        <translation>NAT 랑데부</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>NAT wake-up</source>
        <translation>NAT 깨우기</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Callback for clients on other servers</source>
        <translation>다른 서버 클라이언트용 콜백</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Yes</source>
        <translation>예</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>No</source>
        <translation>아니오</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Kad Network</source>
        <translation>Kad 네트워크</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>LAN Mode</source>
        <translation>LAN 모드</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>UDP Status:</source>
        <translation>UDP 상태:</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Extern UDP Port:</source>
        <translation>외부 UDP 포트:</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Buddy:</source>
        <translation>버디:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Buddies served:</source>
        <translation>중계 중인 버디:</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>%1 (Experimental: %2)</source>
        <translation>%1 (실험적: %2)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Indexed:</source>
        <translation>색인됨:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Source:</source>
        <translation>소스:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Keyword:</source>
        <translation>키워드:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Notes:</source>
        <translation>노트:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Load:</source>
        <translation>부하:</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Web Interface</source>
        <translation>웹 인터페이스</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Enabled</source>
        <translation>활성화됨</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>not running</source>
        <translation>실행 중 아님</translation>
    </message>
    <message numerus="yes">
        <location line="+4"/>
        <source>%n active session(s)</source>
        <translation>
            <numerusform>활성 세션 %n개</numerusform>
        </translation>
    </message>
    <message>
        <location line="+10"/>
        <source>URL:</source>
        <translation>URL:</translation>
    </message>
    <message>
        <location line="-228"/>
        <location line="+94"/>
        <location line="+15"/>
        <source>Firewalled</source>
        <translation>방화벽</translation>
    </message>
    <message>
        <location line="-112"/>
        <location line="+1"/>
        <location line="+96"/>
        <location line="+17"/>
        <source>Open</source>
        <translation>개방</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>unverified</source>
        <translation>미확인</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>None</source>
        <translation>없음</translation>
    </message>
    <message>
        <location line="+63"/>
        <source>Disabled</source>
        <translation>비활성화됨</translation>
    </message>
</context>
<context>
    <name>eMule::NzbFileChooserDialog</name>
    <message>
        <location filename="../src/gui/dialogs/NzbFileChooserDialog.cpp" line="+42"/>
        <source>Choose Files</source>
        <translation>파일 선택</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Unchecked files are not downloaded. The volumes of one archive are checked together, and PAR2 files are fetched only when a repair needs them.</source>
        <translation>선택을 해제한 파일은 다운로드되지 않습니다. 한 압축 파일의 볼륨은 함께 선택되며, PAR2 파일은 복구가 필요할 때만 가져옵니다.</translation>
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
        <location line="+19"/>
        <source>Reading…</source>
        <translation>읽는 중…</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>PAR2 files are fetched when a repair needs them.</source>
        <translation>PAR2 파일은 복구가 필요할 때 가져옵니다.</translation>
    </message>
    <message>
        <location line="+20"/>
        <location line="+20"/>
        <source>Not connected to the eMule core.</source>
        <translation>eMule 코어에 연결되지 않았습니다.</translation>
    </message>
    <message>
        <location line="-14"/>
        <source>Cannot read %1.</source>
        <translation>%1을(를) 읽을 수 없습니다.</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>This file could not be read.</source>
        <translation>이 파일을 읽을 수 없습니다.</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Keep at least one file of &quot;%1&quot;.</source>
        <translation>&quot;%1&quot;의 파일은 최소 한 개를 남겨 두세요.</translation>
    </message>
</context>
<context>
    <name>eMule::OptionsDialog</name>
    <message>
        <location filename="../src/gui/dialogs/OptionsDialog.cpp" line="-3307"/>
        <source>Options</source>
        <translation>옵션</translation>
    </message>
    <message>
        <location line="+61"/>
        <location line="+2217"/>
        <source>OK</source>
        <translation>확인</translation>
    </message>
    <message>
        <location line="-2216"/>
        <location line="+2217"/>
        <source>Cancel</source>
        <translation>취소</translation>
    </message>
    <message>
        <location line="-2216"/>
        <location line="+6237"/>
        <source>Apply</source>
        <translation>적용</translation>
    </message>
    <message>
        <location line="-6236"/>
        <source>Help</source>
        <translation>도움말</translation>
    </message>
    <message>
        <location line="+272"/>
        <location line="+2212"/>
        <location line="+59"/>
        <location line="+6"/>
        <location line="+4"/>
        <location line="+21"/>
        <location line="+5"/>
        <location line="+9"/>
        <location line="+11"/>
        <source>IP Filter</source>
        <translation>IP 필터</translation>
    </message>
    <message>
        <location line="-2326"/>
        <source>IP filter reloaded: %1 entries.</source>
        <translation>IP 필터 다시 로드됨: %1개 항목.</translation>
    </message>
    <message>
        <location line="+90"/>
        <source>Options -&gt; %1 -&gt; %2</source>
        <translation>옵션 -&gt; %1 -&gt; %2</translation>
    </message>
    <message>
        <location line="+5216"/>
        <source>Bandwidth</source>
        <translation>대역폭</translation>
    </message>
    <message>
        <location line="-5008"/>
        <source>General options</source>
        <translation>일반 옵션</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Advanced options</source>
        <translation>고급 옵션</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Usenet</source>
        <translation>Usenet</translation>
    </message>
    <message>
        <location line="+113"/>
        <source>User Name</source>
        <translation>사용자 이름</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+6092"/>
        <source>Language</source>
        <translation>언어</translation>
    </message>
    <message>
        <location line="-6089"/>
        <source>System Default</source>
        <translation>시스템 기본값</translation>
    </message>
    <message>
        <location line="+14"/>
        <location line="+802"/>
        <location line="+384"/>
        <location line="+475"/>
        <location line="+298"/>
        <source>Miscellaneous</source>
        <translation>기타</translation>
    </message>
    <message>
        <location line="-1956"/>
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
        <location line="+3"/>
        <source>Web Services</source>
        <translation>웹 서비스</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>webservices.dat was not found in the config folder.</source>
        <translation>설정 폴더에서 webservices.dat를 찾을 수 없습니다.</translation>
    </message>
    <message>
        <source>Open eD2K links with eMule Qt</source>
        <translation type="vanished">eD2K 링크를 eMule Qt로 열기</translation>
    </message>
    <message>
        <source>Makes eMule Qt the default app for ed2k:// links. Turning it off hands them to another app that opens them, if there is one.</source>
        <translation type="vanished">eMule Qt를 ed2k:// 링크의 기본 앱으로 지정합니다. 끄면 이를 열 수 있는 다른 앱이 있을 경우 그 앱에 넘깁니다.</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Handle eD2K Links</source>
        <translation>eD2K 링크 처리</translation>
    </message>
    <message>
        <location line="+13"/>
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
        <location line="+18"/>
        <location line="+6004"/>
        <source>Core</source>
        <translation>코어</translation>
    </message>
    <message>
        <location line="-6017"/>
        <source>Address:</source>
        <translation>주소:</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+1377"/>
        <location line="+774"/>
        <location line="+427"/>
        <source>Port:</source>
        <translation>포트:</translation>
    </message>
    <message>
        <location line="-2575"/>
        <source>authentication token</source>
        <translation>인증 토큰</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Token:</source>
        <translation>토큰:</translation>
    </message>
    <message>
        <location line="+30"/>
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
        <location line="+3"/>
        <source>flat</source>
        <translation>평면</translation>
    </message>
    <message>
        <location line="+7"/>
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
        <source>Show downloaded data of unfinished parts and part boundaries</source>
        <translation>미완료 파트의 다운로드된 데이터와 파트 경계 표시</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Colours the data already downloaded in incomplete parts (green while being requested, grey otherwise) and marks every part boundary on the progress line.</source>
        <translation>미완료 파트에서 이미 다운로드된 데이터를 색으로 표시하고(요청 중이면 녹색, 그 외에는 회색) 진행 표시줄에 모든 파트 경계를 표시합니다.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Show active downloads in bold</source>
        <translation>활성 다운로드를 굵게 표시</translation>
    </message>
    <message>
        <source>Downloads that are receiving data, and the sources sending it, are drawn in bold. A source listed under another file (A4AF) is bold too while it transfers.</source>
        <translation type="vanished">데이터를 받고 있는 다운로드와 이를 보내는 소스를 굵게 표시합니다. 다른 파일 아래에 표시된 소스(A4AF)도 전송 중에는 굵게 표시됩니다.</translation>
    </message>
    <message>
        <location line="+7"/>
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
        <location line="+11"/>
        <source>Use original eMule icons</source>
        <translation>원본 eMule 아이콘 사용</translation>
    </message>
    <message>
        <location line="+31"/>
        <source>A database ships with each release. An optional free &lt;a href=&quot;https://www.maxmind.com/en/geolite2/signup&quot;&gt;MaxMind GeoLite2&lt;/a&gt; account keeps it current in between. This product includes GeoLite2 data created by MaxMind.</source>
        <translation>각 릴리스에는 데이터베이스가 포함되어 있습니다. 선택 사항인 무료 &lt;a href=&quot;https://www.maxmind.com/en/geolite2/signup&quot;&gt;MaxMind GeoLite2&lt;/a&gt; 계정을 사용하면 릴리스 사이에도 최신 상태로 유지됩니다. 이 제품에는 MaxMind에서 만든 GeoLite2 데이터가 포함되어 있습니다.</translation>
    </message>
    <message>
        <location line="+15"/>
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
        <location line="+25"/>
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
        <location line="+1864"/>
        <location line="+133"/>
        <location line="+296"/>
        <location line="+1032"/>
        <location line="+152"/>
        <location line="+1325"/>
        <location line="+284"/>
        <location line="+29"/>
        <source>Enabled</source>
        <translation>활성화됨</translation>
    </message>
    <message>
        <location line="-5113"/>
        <source>Reset</source>
        <translation>초기화</translation>
    </message>
    <message>
        <location line="+27"/>
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
        <location line="+4505"/>
        <location line="+334"/>
        <source> KB/s</source>
        <translation> KB/s</translation>
    </message>
    <message>
        <location line="-4842"/>
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
        <location line="+11"/>
        <source>Target rate per upload slot</source>
        <translation>업로드 슬롯당 목표 속도</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>A higher rate opens fewer upload slots: the upload limit divided by this rate, never fewer than two. Unchecked, the rate grows with the number of slots.</source>
        <translation>속도를 높이면 업로드 슬롯 수가 줄어듭니다. 슬롯 수는 업로드 제한을 이 속도로 나눈 값이며 2개 미만이 되지 않습니다. 선택하지 않으면 속도가 슬롯 수에 따라 커집니다.</translation>
    </message>
    <message>
        <location line="+23"/>
        <location line="+4345"/>
        <source>Client Port</source>
        <translation>클라이언트 포트</translation>
    </message>
    <message>
        <location line="-4342"/>
        <location line="+7788"/>
        <source>TCP</source>
        <translation>TCP</translation>
    </message>
    <message>
        <location line="-7782"/>
        <source>UDP</source>
        <translation>UDP</translation>
    </message>
    <message>
        <source>Disable</source>
        <translation type="vanished">비활성화</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+4345"/>
        <source>Test Ports</source>
        <translation>포트 테스트</translation>
    </message>
    <message>
        <location line="-4341"/>
        <location line="+4349"/>
        <source>Open Router</source>
        <translation>라우터 열기</translation>
    </message>
    <message>
        <location line="-4348"/>
        <location line="+4349"/>
        <source>Opens the web page of this computer&apos;s router, where ports are forwarded</source>
        <translation>이 컴퓨터의 라우터 웹 페이지를 엽니다. 여기서 포트를 포워딩합니다</translation>
    </message>
    <message>
        <location line="-4345"/>
        <source>Use UPnP to Setup Ports</source>
        <translation>UPnP로 포트 설정</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Randomize ports at startup, range</source>
        <translation>시작 시 포트 무작위 지정, 범위</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Picks new free ports from this range at every start: TCP, UDP unless it is disabled, and the BitTorrent port when BitTorrent is enabled. Use it with UPnP, or forward the whole range in your router.</source>
        <translation>시작할 때마다 이 범위에서 새 빈 포트를 고릅니다: TCP, 비활성화되지 않았다면 UDP, BitTorrent가 켜져 있으면 BitTorrent 포트. UPnP와 함께 쓰거나 라우터에서 범위 전체를 포워딩하세요.</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Port forwarding: unknown</source>
        <translation>포트 포워딩: 알 수 없음</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Network interface</source>
        <translation>네트워크 인터페이스</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+1733"/>
        <location line="+5616"/>
        <source>Any</source>
        <translation>모든</translation>
    </message>
    <message>
        <location line="-7347"/>
        <source>Use only this network interface, for example a VPN tunnel. Pick one from the list, or type an IP address or a subnet such as 10.64.0.0/10 (the interface holding an address in it). Connections through a proxy are not bound.</source>
        <translation>이 네트워크 인터페이스(예: VPN 터널)만 사용합니다. 목록에서 선택하거나 IP 주소 또는 10.64.0.0/10 같은 서브넷(해당 범위의 주소를 가진 인터페이스)을 입력하십시오. 프록시를 통한 연결은 바인딩되지 않습니다.</translation>
    </message>
    <message>
        <location line="+7244"/>
        <source>While the selected interface is not available, all P2P, Usenet and update connections stay closed.</source>
        <translation>선택한 인터페이스를 사용할 수 없는 동안에는 모든 P2P, Usenet 및 업데이트 연결이 닫힌 상태로 유지됩니다.</translation>
    </message>
    <message>
        <location line="-7230"/>
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
        <location line="+4367"/>
        <source>Connection Limits</source>
        <translation>연결 제한</translation>
    </message>
    <message>
        <location line="-4365"/>
        <source>Max. connections</source>
        <translation>최대 연결 수</translation>
    </message>
    <message>
        <location line="+14"/>
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
        <location line="+15"/>
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
        <location line="+4"/>
        <source>Use IPv6 privacy address</source>
        <translation>IPv6 개인 정보 보호 주소 사용</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Connect to IPv6 peers and servers from the rotating temporary (RFC 4941) address. Off: always use the stable address, so peers can reach the address we advertise.</source>
        <translation>IPv6 피어와 서버에 주기적으로 바뀌는 임시 주소(RFC 4941)로 연결합니다. 끄기: 항상 고정 주소를 사용하여 피어가 알린 주소로 접속할 수 있게 합니다.</translation>
    </message>
    <message>
        <location line="+57"/>
        <location line="+1558"/>
        <source>General</source>
        <translation>일반</translation>
    </message>
    <message>
        <location line="-1555"/>
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
        <location line="+21"/>
        <location line="+782"/>
        <source>Authentication</source>
        <translation>인증</translation>
    </message>
    <message>
        <location line="-779"/>
        <source>Enable authentication</source>
        <translation>인증 활성화</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+1940"/>
        <location line="+1035"/>
        <location line="+152"/>
        <source>Name:</source>
        <translation>이름:</translation>
    </message>
    <message>
        <location line="-3123"/>
        <location line="+783"/>
        <location line="+837"/>
        <location line="+19"/>
        <location line="+325"/>
        <source>Password:</source>
        <translation>비밀번호:</translation>
    </message>
    <message>
        <location line="-1930"/>
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
        <location line="+11"/>
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
        <location line="+9"/>
        <source>Safe Connect</source>
        <translation>안전 연결</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Lets two Low ID clients find each other&apos;s address through a server that supports it or a Kad buddy, then connect over IPv6 or QUIC</source>
        <translation>이를 지원하는 서버나 Kad 버디를 통해 두 Low ID 클라이언트가 서로의 주소를 찾은 뒤 IPv6 또는 QUIC로 연결합니다</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Always use a source&apos;s server for NAT callback</source>
        <translation>NAT 콜백에 항상 소스의 서버 사용</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Also asks a source&apos;s server that is not in your server list. It is pinged once first and is not added to the list</source>
        <translation>서버 목록에 없는 소스의 서버에도 요청합니다. 먼저 한 번 핑을 보내며 목록에는 추가하지 않습니다</translation>
    </message>
    <message>
        <location line="+8"/>
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
        <location line="+184"/>
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
        <location line="+12"/>
        <source>Select Temporary Directory</source>
        <translation>임시 디렉터리 선택</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Shared Directories (Ctrl+Click includes subdirectories)</source>
        <translation>공유 디렉터리(Ctrl+클릭으로 하위 디렉터리 포함)</translation>
    </message>
    <message>
        <location line="+40"/>
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
        <location line="+16"/>
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
        <location line="+613"/>
        <source>Edit...</source>
        <translation>편집...</translation>
    </message>
    <message>
        <source>Filename Cleanup Rules</source>
        <translation type="vanished">파일명 정리 규칙</translation>
    </message>
    <message>
        <source>Define patterns to automatically clean up filenames of new downloads.
Each rule replaces a regex pattern with a replacement string.</source>
        <translation type="vanished">새 다운로드의 파일명을 자동으로 정리하는 패턴을 정의합니다.
각 규칙은 정규식 패턴을 대체 문자열로 바꿉니다.</translation>
    </message>
    <message>
        <source>Pattern</source>
        <translation type="vanished">패턴</translation>
    </message>
    <message>
        <source>Replacement</source>
        <translation type="vanished">대체</translation>
    </message>
    <message>
        <location line="-602"/>
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
        <location line="+37"/>
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
        <source>Create backup to preview</source>
        <translation type="vanished">미리보기용 백업 생성</translation>
    </message>
    <message>
        <location line="+21"/>
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
        <location line="+13"/>
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
        <location line="+8"/>
        <source>Server:</source>
        <translation>서버:</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+14"/>
        <location line="+1233"/>
        <source>None</source>
        <translation>없음</translation>
    </message>
    <message>
        <source>Plain</source>
        <translation type="vanished">평문</translation>
    </message>
    <message>
        <source>Authentication:</source>
        <translation type="vanished">인증:</translation>
    </message>
    <message>
        <source>Use TLS/STARTTLS</source>
        <translation type="vanished">TLS/STARTTLS 사용</translation>
    </message>
    <message>
        <location line="-1228"/>
        <source>Username:</source>
        <translation>사용자 이름:</translation>
    </message>
    <message>
        <location line="+111"/>
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
        <location line="+982"/>
        <location line="+1043"/>
        <location line="+154"/>
        <source>Name</source>
        <translation>이름</translation>
    </message>
    <message>
        <location line="-2177"/>
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
        <location line="-111"/>
        <source>Connect to help channel</source>
        <translation>도움말 채널에 연결</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Load server channel list on connect</source>
        <translation>연결 시 서버 채널 목록 로드</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Add timestamp to messages</source>
        <translation>메시지에 타임스탬프 추가</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Ignore info messages</source>
        <translation>정보 메시지 무시</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ignore misc. info messages</source>
        <translation>기타 정보 메시지 무시</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Ignore Join info messages</source>
        <translation>Join 메시지 무시</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Ignore Part info messages</source>
        <translation>Part 메시지 무시</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Ignore Quit info messages</source>
        <translation>Quit 메시지 무시</translation>
    </message>
    <message>
        <location line="+161"/>
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
        <location line="-159"/>
        <location line="+164"/>
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
        <location line="+490"/>
        <source>Reload</source>
        <translation>다시 로드</translation>
    </message>
    <message>
        <source>http://example.com/ipfilter.dat</source>
        <translation type="vanished">http://example.com/ipfilter.dat</translation>
    </message>
    <message>
        <location line="-465"/>
        <location line="+16"/>
        <location line="+30"/>
        <source>Load</source>
        <translation>불러오기</translation>
    </message>
    <message>
        <location line="-41"/>
        <source>Loading...</source>
        <translation>로딩 중...</translation>
    </message>
    <message>
        <location line="+14"/>
        <location line="+6"/>
        <location line="+25"/>
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
        <location line="+23"/>
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
        <location line="+17"/>
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
        <source>Warn when opening untrusted files</source>
        <translation type="vanished">신뢰할 수 없는 파일 열 때 경고</translation>
    </message>
    <message>
        <location line="+50"/>
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
        <location line="+158"/>
        <source>Update delay: %1 sec</source>
        <translation>업데이트 지연: %1초</translation>
    </message>
    <message>
        <location line="-157"/>
        <location line="+158"/>
        <source>Update delay: disabled</source>
        <translation>업데이트 지연: 비활성화</translation>
    </message>
    <message>
        <location line="-151"/>
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
        <location line="+8"/>
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
        <location line="+7"/>
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
        <location line="-6"/>
        <source>Upload Friend Slots</source>
        <translation>업로드 친구 슬롯</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Upload Slots (no overhead)</source>
        <translation>업로드 슬롯(오버헤드 없음)</translation>
    </message>
    <message>
        <location line="-1810"/>
        <source>Country flags (IP2Country)</source>
        <translation>국기 (IP2Country)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Show country flags</source>
        <translation>국기 표시</translation>
    </message>
    <message>
        <source>Country column:</source>
        <translation type="vanished">국가 열:</translation>
    </message>
    <message>
        <source>Hidden</source>
        <translation type="vanished">숨김</translation>
    </message>
    <message>
        <source>Short name</source>
        <translation type="vanished">짧은 이름</translation>
    </message>
    <message>
        <source>Long name</source>
        <translation type="vanished">전체 이름</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>MaxMind account ID:</source>
        <translation>MaxMind 계정 ID:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>License key:</source>
        <translation>라이선스 키:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Update the database weekly</source>
        <translation>데이터베이스를 매주 업데이트</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Update now</source>
        <translation>지금 업데이트</translation>
    </message>
    <message>
        <source>Needs a free &lt;a href=&quot;https://www.maxmind.com/en/geolite2/signup&quot;&gt;MaxMind GeoLite2&lt;/a&gt; account. This product includes GeoLite2 data created by MaxMind.</source>
        <translation type="vanished">무료 &lt;a href=&quot;https://www.maxmind.com/en/geolite2/signup&quot;&gt;MaxMind GeoLite2&lt;/a&gt; 계정이 필요합니다. 이 제품에는 MaxMind가 만든 GeoLite2 데이터가 포함되어 있습니다.</translation>
    </message>
    <message>
        <location line="+413"/>
        <source>Use for news servers</source>
        <translation>뉴스 서버에도 사용</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Route Usenet downloads, availability checks and the news server Test button through this proxy too. News servers switch over as soon as you press OK.

Every Usenet connection then passes through the proxy, so its speed caps the download, and many HTTP proxies only allow connections to port 443.</source>
        <translation>Usenet 다운로드, 가용성 확인, 뉴스 서버 테스트 단추도 이 프록시를 거치게 합니다. 확인을 누르는 즉시 뉴스 서버가 전환됩니다.

그러면 모든 Usenet 연결이 프록시를 지나가므로 프록시 속도가 다운로드 속도를 좌우하며, 많은 HTTP 프록시는 443 포트로의 연결만 허용합니다.</translation>
    </message>
    <message>
        <location line="+115"/>
        <source>Prefer IPv6 when IPv4 gives a Low ID</source>
        <translation>IPv4에서 LowID를 받으면 IPv6 우선 사용</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Reconnect to a server over its IPv6 address when the IPv4 connection got a Low ID</source>
        <translation>IPv4 연결이 LowID를 받으면 서버의 IPv6 주소로 다시 연결합니다</translation>
    </message>
    <message>
        <location line="+430"/>
        <source>Remember files seen in search results</source>
        <translation>검색 결과에 나온 파일 기억</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Keeps a local record of the files that turned up in your searches and the names they went by, so later searches can show what you have seen before.</source>
        <translation>검색에 나타난 파일과 그때의 이름을 로컬에 기록하여, 이후 검색에서 이전에 본 항목을 표시할 수 있게 합니다.</translation>
    </message>
    <message>
        <location line="-1433"/>
        <location line="+5"/>
        <location line="+1648"/>
        <source>Connection</source>
        <translation>연결</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Security:</source>
        <translation>보안:</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Authentication method:</source>
        <translation>인증 방식:</translation>
    </message>
    <message>
        <location line="+63"/>
        <source>Ignore Ping? Pong! messages</source>
        <translation>Ping? Pong! 메시지 무시</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Ignore eMule protocol messages</source>
        <translation>eMule 프로토콜 메시지 무시</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Ignore eMule add friend protocol messages</source>
        <translation>eMule 친구 추가 프로토콜 메시지 무시</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Ignore eMule send link protocol messages</source>
        <translation>eMule 링크 전송 프로토콜 메시지 무시</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Allow others to add you as a friend</source>
        <translation>다른 사용자가 나를 친구로 추가하도록 허용</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Accept eD2K links in IRC (Use only with caution!)</source>
        <translation>IRC에서 eD2K 링크 수락 (주의해서 사용하십시오!)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>From friends only</source>
        <translation>친구에게서만</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Play sound events</source>
        <translation>소리 이벤트 재생</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Enable UTF-8</source>
        <translation>UTF-8 사용</translation>
    </message>
    <message>
        <location line="+400"/>
        <source>Anti-Leecher</source>
        <translation>안티 리처</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Ban leechers with DLP rules:</source>
        <translation>DLP 규칙으로 리처 차단:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Lite</source>
        <translation>Lite</translation>
    </message>
    <message>
        <source>All VeryCD</source>
        <translation type="vanished">모든 VeryCD</translation>
    </message>
    <message>
        <source>easyMule</source>
        <translation type="vanished">easyMule</translation>
    </message>
    <message>
        <source>VeryCD nickname</source>
        <translation type="vanished">VeryCD 닉네임</translation>
    </message>
    <message>
        <source>VeryCD tag</source>
        <translation type="vanished">VeryCD 태그</translation>
    </message>
    <message>
        <source>Only reduce the score of soft leechers instead of banning them</source>
        <translation type="vanished">경미한 리처는 차단하지 않고 점수만 낮추기</translation>
    </message>
    <message>
        <location line="+84"/>
        <source>Download Usenet</source>
        <translation>Usenet 다운로드</translation>
    </message>
    <message>
        <location line="+24"/>
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
        <location line="+1"/>
        <source>Serves /api/v1 with its description and a playground at /api/v1/docs. Calls need the API key.</source>
        <translation>/api/v1과 그 설명, /api/v1/docs의 테스트 페이지를 제공합니다. 호출에는 API 키가 필요합니다.</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Enable MCP server for AI assistants</source>
        <translation>AI 어시스턴트용 MCP 서버 사용</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Serves the Model Context Protocol at /mcp on the same port, with the same API key. Independent of the REST API.</source>
        <translation>같은 포트의 /mcp에서 같은 API 키로 Model Context Protocol을 제공합니다. REST API와는 별개입니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Read-only</source>
        <translation>읽기 전용</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Assistants can look at everything but change nothing.</source>
        <translation>어시스턴트는 모든 것을 볼 수 있지만 아무것도 변경할 수 없습니다.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Gzip compression</source>
        <translation>Gzip 압축</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Include port into UPnP setup</source>
        <translation>UPnP 설정에 포트 포함</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Listen address:</source>
        <translation>수신 주소:</translation>
    </message>
    <message>
        <source>all interfaces</source>
        <translation type="vanished">모든 인터페이스</translation>
    </message>
    <message>
        <source>Address the web server listens on. Empty: every interface, reachable from other hosts. 127.0.0.1: this computer only.</source>
        <translation type="vanished">웹 서버가 수신하는 주소입니다. 비워 두면 모든 인터페이스에서 수신하며 다른 호스트에서 접근할 수 있습니다. 127.0.0.1: 이 컴퓨터만.</translation>
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
        <location line="+55"/>
        <source>Web template reloaded</source>
        <translation>웹 템플릿을 다시 로드했습니다</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Web template reload failed</source>
        <translation>웹 템플릿 다시 로드 실패</translation>
    </message>
    <message>
        <location line="+7"/>
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
        <location line="+132"/>
        <source>Enable Usenet downloads</source>
        <translation>Usenet 다운로드 활성화</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Gates automatic activity only. Adding a download by hand always works.</source>
        <translation>자동 동작만 제어합니다. 직접 다운로드를 추가하는 것은 언제나 가능합니다.</translation>
    </message>
    <message>
        <location line="+26"/>
        <location line="+38"/>
        <source>Account</source>
        <translation>계정</translation>
    </message>
    <message>
        <location line="-36"/>
        <source>Advanced</source>
        <translation>고급</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+644"/>
        <location line="+274"/>
        <location line="+15"/>
        <source>News servers</source>
        <translation>뉴스 서버</translation>
    </message>
    <message>
        <location line="-927"/>
        <source>Host</source>
        <translation>호스트</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Port</source>
        <translation>포트</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Priority</source>
        <translation>우선순위</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Connections</source>
        <translation>연결 수</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Used</source>
        <translation>사용량</translation>
    </message>
    <message>
        <location line="+41"/>
        <source>Display name (optional)</source>
        <translation>표시 이름 (선택 사항)</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Host:</source>
        <translation>호스트:</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Encryption:</source>
        <translation>암호화:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>None (119)</source>
        <translation>없음 (119)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>SSL/TLS (563)</source>
        <translation>SSL/TLS (563)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>STARTTLS</source>
        <translation>STARTTLS</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>User:</source>
        <translation>사용자:</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Never set this above what your provider allows — exceeding the limit gets the account throttled, not queued.</source>
        <translation>제공업체가 허용하는 수보다 높게 설정하지 마세요 — 한도를 넘으면 연결이 대기되는 것이 아니라 계정 속도가 제한됩니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Connections:</source>
        <translation>연결 수:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Lower is tried first. A higher level is only used for articles that every server below reported as missing — that is what makes a block or fill account worth having.</source>
        <translation>낮은 값이 먼저 시도됩니다. 더 높은 단계는 그 아래의 모든 서버가 없다고 답한 기사에만 사용되며, 바로 그 점이 블록 계정이나 보충 계정을 쓸 만하게 만듭니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Priority level:</source>
        <translation>우선순위 단계:</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Account options</source>
        <translation>계정 옵션</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Unknown</source>
        <translation>알 수 없음</translation>
    </message>
    <message>
        <location line="-813"/>
        <location line="+815"/>
        <location line="+1020"/>
        <location line="+165"/>
        <source> days</source>
        <translation> 일</translation>
    </message>
    <message>
        <location line="-1184"/>
        <source>Retention:</source>
        <translation>보존 기간:</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Accounts sharing a group number count as one for connection limits — use it when the same provider is reached through two host names, so the two entries cannot open twice what the plan allows.</source>
        <translation>같은 그룹 번호를 쓰는 계정은 연결 제한에서 하나로 계산됩니다 — 같은 제공업체를 두 개의 호스트 이름으로 이용할 때 사용하면, 두 항목이 요금제가 허용하는 연결 수의 두 배를 열지 못합니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Connection group:</source>
        <translation>연결 그룹:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>None — accept any certificate</source>
        <translation>없음 — 모든 인증서 수락</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Minimal — allow a host name mismatch</source>
        <translation>최소 — 호스트 이름 불일치 허용</translation>
    </message>
    <message>
        <location line="-751"/>
        <location line="+752"/>
        <source>Strict</source>
        <translation>엄격</translation>
    </message>
    <message>
        <location line="-2872"/>
        <location line="+8417"/>
        <source>BitTorrent</source>
        <translation>BitTorrent</translation>
    </message>
    <message>
        <location line="-8032"/>
        <source>Show download progress on the Dock icon</source>
        <translation>Dock 아이콘에 다운로드 진행률 표시</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Show download progress on the taskbar button</source>
        <translation>작업 표시줄 버튼에 다운로드 진행률 표시</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Shows advanced entries in context menus and extra details in lists.</source>
        <translation>컨텍스트 메뉴에 고급 항목을, 목록에 추가 정보를 표시합니다.</translation>
    </message>
    <message>
        <location line="+50"/>
        <source>Appearance</source>
        <translation>모양</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Theme:</source>
        <translation>테마:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>System default</source>
        <translation>시스템 기본값</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Light</source>
        <translation>밝게</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Dark</source>
        <translation>어둡게</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Font size:</source>
        <translation>글꼴 크기:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>%1 %</source>
        <translation>%1%</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Text size of lists, menus and dialogs. Takes effect after a restart.</source>
        <translation>목록, 메뉴, 대화 상자의 글자 크기입니다. 다시 시작한 후 적용됩니다.</translation>
    </message>
    <message>
        <location line="+497"/>
        <source>Connect Low ID to Low ID through server or Kad buddy (NAT traversal)</source>
        <translation>서버 또는 Kad 버디를 통해 Low ID끼리 연결 (NAT 통과)</translation>
    </message>
    <message>
        <source>Lets two Low ID clients find each other&apos;s address through a server that supports it or a Kad buddy, then connect over IPv6</source>
        <translation type="vanished">이를 지원하는 서버나 Kad 버디를 통해 두 Low ID 클라이언트가 서로의 주소를 찾은 뒤 IPv6로 연결합니다</translation>
    </message>
    <message>
        <location line="+1130"/>
        <source>Never send unobfuscated NAT traversal (QUIC) packets</source>
        <translation>난독화되지 않은 NAT 통과(QUIC) 패킷을 보내지 않음</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Low ID to Low ID transfers over QUIC are always obfuscated when possible. If the other client cannot read them, eMule falls back to unobfuscated packets after 2 seconds. Check this to fail such transfers instead, e.g. on networks that block QUIC.</source>
        <translation>QUIC를 통한 Low ID 간 전송은 가능하면 항상 난독화됩니다. 상대 클라이언트가 읽을 수 없으면 eMule은 2초 후 난독화되지 않은 패킷으로 전환합니다. QUIC를 차단하는 네트워크 등에서 대신 해당 전송을 실패시키려면 선택하십시오.</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Strict + Chinese</source>
        <translation>엄격 + 중국계</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bans hard leechers, soft leechers only lose queue score</source>
        <translation>심각한 리처는 차단하고, 경미한 리처는 대기열 점수만 깎음</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Bans hard and soft leechers</source>
        <translation>심각한 리처와 경미한 리처를 모두 차단</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Strict, and also bans VeryCD and easyMule mods</source>
        <translation>엄격 모드에 더해 VeryCD 및 easyMule 모드도 차단</translation>
    </message>
    <message>
        <location line="+81"/>
        <source>Download BitTorrent</source>
        <translation>BitTorrent 다운로드</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Download ed3k</source>
        <translation>다운로드 ed3k</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Upload BitTorrent</source>
        <translation>BitTorrent 업로드</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload ed3k</source>
        <translation>업로드 ed3k</translation>
    </message>
    <message>
        <location line="+183"/>
        <source>Address the web server listens on. Any: every interface, reachable from other hosts. 127.0.0.1: this computer only. Pick one from the list or type an IP address.</source>
        <translation>웹 서버가 수신하는 주소입니다. 모든: 모든 인터페이스에서 수신하며 다른 호스트에서 접근할 수 있습니다. 127.0.0.1: 이 컴퓨터만. 목록에서 선택하거나 IP 주소를 입력하세요.</translation>
    </message>
    <message>
        <location line="+478"/>
        <source>Certificate check:</source>
        <translation>인증서 검사:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Optional — never fail a download on its own</source>
        <translation>선택 사항 — 이것만으로 다운로드가 실패하지는 않습니다</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Send GROUP before fetching (only needed by a few old servers)</source>
        <translation>가져오기 전에 GROUP 전송 (일부 오래된 서버에만 필요)</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Unmetered</source>
        <translation>사용량 무제한</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Monthly allowance</source>
        <translation>월 허용량</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Block account (prepaid)</source>
        <translation>블록 계정 (선불)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Allowance:</source>
        <translation>허용량:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source> GB</source>
        <translation> GB</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+1974"/>
        <location line="+9"/>
        <location line="+159"/>
        <source>No limit</source>
        <translation>제한 없음</translation>
    </message>
    <message>
        <location line="-2134"/>
        <source>Allowance size:</source>
        <translation>허용량 크기:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Your billing day — providers reset on the day you signed up, not on the 1st. A month shorter than this rolls over on its last day.</source>
        <translation>결제일입니다 — 제공업체는 1일이 아니라 가입한 날에 사용량을 초기화합니다. 이 날짜보다 짧은 달은 그 달의 마지막 날에 넘어갑니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Resets on day:</source>
        <translation>초기화 일자:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>When the allowance is spent, use the next priority level</source>
        <translation>허용량을 모두 쓰면 다음 우선순위 단계 사용</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Off by default: block credit usually costs more per GB than the plan it would be covering, and spending it without being asked is the one thing a limit exists to prevent. Left off, downloads wait for the allowance instead — they are never failed and no article is ever given up on.</source>
        <translation>기본적으로 꺼져 있습니다. 블록 크레딧은 대개 대신 쓰려던 요금제보다 GB당 비용이 비싸며, 묻지도 않고 그것을 쓰는 일이야말로 한도가 막으려는 바로 그 일입니다. 꺼 두면 다운로드는 대신 허용량이 회복되기를 기다립니다 — 실패로 처리되지 않으며 어떤 기사도 포기하지 않습니다.</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Used:</source>
        <translation>사용량:</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Correct…</source>
        <translation>수정…</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Downloading</source>
        <translation>다운로드 중</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Retry a failed server after:</source>
        <translation>실패한 서버 재시도 간격:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Never back off</source>
        <translation>대기 없음</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Applies to every account: how long a server that refused or dropped a connection is passed over before it is tried again.</source>
        <translation>모든 계정에 적용됩니다. 연결을 거부하거나 끊은 서버를 다시 시도하기 전까지 얼마나 건너뛸지 정합니다.</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+2076"/>
        <source>Share of the download limit:</source>
        <translation>다운로드 제한 중 할당 비율:</translation>
    </message>
    <message>
        <location line="-2073"/>
        <location line="+42"/>
        <location line="+2023"/>
        <source> %</source>
        <translation> %</translation>
    </message>
    <message>
        <location line="-2063"/>
        <source>How much of the global download limit Usenet may take while eD2K is also downloading. Whichever engine is idle lends its whole share to the other, so this only applies when both are busy.</source>
        <translation>eD2K도 다운로드 중일 때 Usenet이 전체 다운로드 제한 중 얼마나 차지할 수 있는지 정합니다. 유휴 상태인 엔진은 자신의 몫을 모두 다른 쪽에 넘겨주므로, 이 값은 양쪽이 모두 바쁠 때만 적용됩니다.</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>When adding</source>
        <translation>추가할 때</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Check availability:</source>
        <translation>가용성 확인:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Do not check</source>
        <translation>확인하지 않음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Sample one article per file</source>
        <translation>파일당 기사 하나만 표본 확인</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Check every article</source>
        <translation>모든 기사 확인</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Before downloading anything, ask your providers whether they still hold the release. It costs one small request per article asked about and no payload at all.

Sampling asks about the first article of each file, which is usually enough: providers expire whole posts by date, so a file is almost always present or absent as a unit. Checking every article is certain but can mean tens of thousands of requests for a large release.

The answer is never a verdict. Nothing here can stop an article being fetched — an article your providers deny may still arrive, and a release this pauses downloads normally when you resume it.</source>
        <translation>무엇이든 내려받기 전에 제공업체가 해당 릴리스를 아직 보관하고 있는지 물어봅니다. 문의한 기사마다 작은 요청 하나만 들 뿐 본문은 전혀 주고받지 않습니다.

표본 확인은 각 파일의 첫 기사만 물어보며 대개 그것으로 충분합니다. 제공업체는 게시물 전체를 날짜 기준으로 만료시키므로, 파일은 거의 언제나 통째로 있거나 통째로 없습니다. 모든 기사를 확인하면 확실하지만, 큰 릴리스에서는 수만 건의 요청이 될 수 있습니다.

그 답은 결코 최종 판정이 아닙니다. 여기서 정하는 어떤 것도 기사 가져오기를 막지 못합니다 — 제공업체가 없다고 한 기사가 그래도 도착할 수 있으며, 이 때문에 일시 정지된 릴리스도 재개하면 정상적으로 다운로드됩니다.</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Pause below:</source>
        <translation>이 값 미만이면 일시 정지:</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+1316"/>
        <source>never</source>
        <translation>안 함</translation>
    </message>
    <message>
        <location line="-1313"/>
        <source>A release that looks emptier than this is added paused, with the reason shown, so you decide rather than the guess. It is never failed and never refused.

A shortfall the release&apos;s own PAR2 recovery volumes can cover does not pause it, however low the figure goes.</source>
        <translation>이 값보다 더 비어 보이는 릴리스는 이유와 함께 일시 정지 상태로 추가되므로, 추측이 아니라 사용자가 결정하게 됩니다. 실패로 처리되거나 거부되는 일은 없습니다.

릴리스 자체의 PAR2 복구 볼륨으로 메울 수 있는 부족분이라면, 수치가 아무리 낮아도 일시 정지되지 않습니다.</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Start automatic downloads paused</source>
        <translation>자동 다운로드를 일시 정지 상태로 시작</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Applies to anything queued without you asking for it directly: the watch folder below, and feeds.

With this on, an automatic download waits for you to press Resume, so a feed proposes rather than decides. Anything you add yourself starts normally either way.</source>
        <translation>직접 요청하지 않았는데도 대기열에 추가되는 모든 항목, 즉 아래의 감시 폴더와 피드에 적용됩니다.

켜 두면 자동 다운로드는 재개를 누를 때까지 기다리므로, 피드는 결정하는 대신 제안하게 됩니다. 직접 추가한 항목은 어느 쪽이든 평소대로 시작됩니다.</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Download only the selected file of an NZB search result</source>
        <translation>NZB 검색 결과에서 선택한 파일만 다운로드</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Search results list an NZB once as a whole and once per file in it.

With this on, downloading one file&apos;s row fetches only that file (with the rest of its archive set) and skips the others. Picking another file of the same NZB later adds it to the queued download.

The Details window lets you choose the files either way.</source>
        <translation>검색 결과에는 NZB가 전체로 한 번, 포함된 파일마다 한 번씩 표시됩니다.

이 옵션을 켜면 파일 행을 다운로드할 때 해당 파일(과 같은 압축 세트의 나머지)만 가져오고 나머지는 건너뜁니다. 나중에 같은 NZB의 다른 파일을 고르면 대기 중인 다운로드에 추가됩니다.

세부 정보 창에서는 어느 경우든 파일을 선택할 수 있습니다.</translation>
    </message>
    <message>
        <location line="+10"/>
        <location line="+55"/>
        <source>Watch folder</source>
        <translation>감시 폴더</translation>
    </message>
    <message>
        <location line="-51"/>
        <source>Any .nzb file left in this folder is queued and then moved into a _processed subfolder — or _failed, if it could not be read.</source>
        <translation>이 폴더에 남겨 둔 .nzb 파일은 대기열에 추가된 뒤 _processed 하위 폴더로 옮겨집니다 — 읽을 수 없었다면 _failed로 옮겨집니다.</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>No folder is being watched</source>
        <translation>감시 중인 폴더 없음</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>A file is only read once it has stopped changing, so a large .nzb still being copied in is left alone until it is complete.

It cannot be inside your temp, incoming or configuration folders: the daemon writes there itself.</source>
        <translation>파일은 더 이상 변하지 않을 때에만 읽으므로, 복사 중인 큰 .nzb는 완전히 복사될 때까지 건드리지 않습니다.

임시, 수신, 설정 폴더 안에는 지정할 수 없습니다. 데몬이 직접 그 폴더에 쓰기 때문입니다.</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+1792"/>
        <source>Browse…</source>
        <translation>찾아보기…</translation>
    </message>
    <message>
        <location line="-1784"/>
        <location line="+1827"/>
        <source>Desktop</source>
        <translation>바탕 화면</translation>
    </message>
    <message>
        <location line="-1823"/>
        <source>Open .nzb files with eMule Qt</source>
        <translation>.nzb 파일을 eMule Qt로 열기</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Makes eMule Qt the default app for .nzb files, so double-clicking one queues it. Turning it off hands them to another app that opens them, if there is one.</source>
        <translation>eMule Qt를 .nzb 파일의 기본 앱으로 지정해 두 번 클릭하면 대기열에 추가됩니다. 끄면 이를 열 수 있는 다른 앱이 있을 경우 그 앱에 넘깁니다.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Claim .nzb files for this copy of eMule Qt, so double-clicking one queues it. The setting is for you alone and needs no administrator; it is re-applied at every start, so another program taking the association does not keep it.</source>
        <translation>.nzb 파일을 이 eMule Qt에 연결하여 두 번 클릭하면 대기열에 추가되도록 합니다. 이 설정은 현재 사용자에게만 적용되며 관리자 권한이 필요 없습니다. 시작할 때마다 다시 적용되므로, 다른 프로그램이 연결을 가져가더라도 계속 유지하지는 못합니다.</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>After downloading</source>
        <translation>다운로드 후</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Verify and repair with PAR2</source>
        <translation>PAR2로 검증 및 복구</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Check the finished files against the release&apos;s PAR2 set and repair any damage from its recovery volumes. The recovery volumes are only downloaded when something actually needs repairing.

With this off, a release with missing articles fails instead of being shared, because there is no way to tell whether it is intact.</source>
        <translation>완료된 파일을 릴리스의 PAR2 세트와 대조해 검사하고, 손상된 부분은 복구 볼륨으로 복구합니다. 복구 볼륨은 실제로 복구가 필요할 때만 다운로드됩니다.

이 옵션을 끄면 기사가 누락된 릴리스는 공유되지 않고 실패로 처리됩니다. 파일이 온전한지 확인할 방법이 없기 때문입니다.</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Restore filenames from PAR2</source>
        <translation>PAR2에서 파일 이름 복원</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Obfuscated releases are posted under meaningless filenames. The PAR2 metadata carries the real ones, and without them the archives cannot be identified for unpacking either.</source>
        <translation>난독화된 릴리스는 의미 없는 파일 이름으로 게시됩니다. 실제 이름은 PAR2 메타데이터에 들어 있으며, 이것이 없으면 압축을 풀기 위해 압축 파일을 식별할 수도 없습니다.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Verify with SFV when there is no PAR2</source>
        <translation>PAR2가 없으면 SFV로 검증</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>A release posted without a PAR2 set often comes with an .sfv file instead. Its checksums cannot repair anything, but a release they call damaged is not published.</source>
        <translation>PAR2 세트 없이 게시된 릴리스에는 대신 .sfv 파일이 함께 오는 경우가 많습니다. 그 체크섬으로는 아무것도 복구할 수 없지만, 손상되었다고 판정된 릴리스는 공유되지 않습니다.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Unpack archives</source>
        <translation>압축 파일 풀기</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Extract RAR, 7z and ZIP volume sets once they have been verified.

Password-protected archives need 7-Zip or unrar installed — eMule&apos;s own archive reader can only decrypt ZIP.</source>
        <translation>RAR, 7z, ZIP 볼륨 세트를 검증이 끝난 뒤 풉니다.

비밀번호로 보호된 압축 파일에는 7-Zip이나 unrar가 설치되어 있어야 합니다 — eMule 자체 압축 파일 판독기는 ZIP만 복호화할 수 있습니다.</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Unpack while downloading</source>
        <translation>다운로드 중에 압축 풀기</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Extract each archive volume as soon as it finishes instead of waiting for the whole release, so the content is ready the moment the download is.

It is the same extraction, moved earlier, so it costs no extra disk space. If the release turns out to need repairing, the result is discarded and it is unpacked again afterwards.</source>
        <translation>릴리스 전체를 기다리지 않고 각 압축 볼륨이 완료되는 즉시 풀어, 다운로드가 끝나는 순간 내용이 준비되도록 합니다.

같은 압축 해제를 앞당기는 것일 뿐이므로 디스크 공간을 추가로 쓰지 않습니다. 릴리스에 복구가 필요한 것으로 드러나면 그 결과는 버려지고 나중에 다시 압축을 풉니다.</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Preview password-protected releases while downloading</source>
        <translation>비밀번호로 보호된 릴리스를 다운로드 중에 미리보기</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>An encrypted archive cannot be read a piece at a time, so previewing one means decrypting it again from the first volume every time more of it arrives.

Nothing runs unless a preview is actually open, and only RAR releases can do it at all — an incomplete 7z set decodes to nothing.</source>
        <translation>암호화된 압축 파일은 조각 단위로 읽을 수 없으므로, 미리보기를 하려면 내용이 더 도착할 때마다 첫 볼륨부터 다시 복호화해야 합니다.

미리보기가 실제로 열려 있을 때만 동작하며, 그것이 가능한 것은 RAR 릴리스뿐입니다 — 완성되지 않은 7z 세트는 아무것도 디코딩되지 않습니다.</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Unpacker:</source>
        <translation>압축 해제 프로그램:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>automatic (7zz, 7z, unrar)</source>
        <translation>자동 (7zz, 7z, unrar)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Path to a 7-Zip or unrar binary, for password-protected archives.

Leave this empty to search the usual locations. Set it when eMule runs as a background service, whose search path is often much shorter than the one a terminal has.</source>
        <translation>비밀번호로 보호된 압축 파일에 사용할 7-Zip 또는 unrar 실행 파일의 경로입니다.

비워 두면 일반적인 위치에서 찾습니다. eMule을 백그라운드 서비스로 실행할 때 지정하세요. 서비스의 검색 경로는 터미널의 경로보다 훨씬 짧은 경우가 많습니다.</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Delete archives and PAR2 files after unpacking</source>
        <translation>압축을 푼 뒤 압축 파일과 PAR2 파일 삭제</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Keep only the unpacked content. Turning this off roughly doubles the disk space a release uses and shares the archive volumes and recovery files with eD2K peers, who have no use for them.</source>
        <translation>압축을 푼 내용만 남깁니다. 이 옵션을 끄면 릴리스가 쓰는 디스크 공간이 대략 두 배가 되고, 압축 볼륨과 복구 파일까지 eD2K 피어에게 공유되는데 그들에게는 쓸모가 없습니다.</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Keep downloading</source>
        <translation>계속 다운로드</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+11"/>
        <source>Pause it</source>
        <translation>일시 정지</translation>
    </message>
    <message>
        <location line="-10"/>
        <location line="+11"/>
        <source>Fail it</source>
        <translation>실패로 처리</translation>
    </message>
    <message>
        <location line="-9"/>
        <source>A release that has lost more than its recovery files could ever repair stops here instead of using up your allowance until the final check. The estimate only ever errs towards downloading.

Resume downloads it anyway.</source>
        <translation>복구 파일로도 도저히 복구할 수 없을 만큼 손실된 릴리스는 최종 검사까지 허용량을 소모하는 대신 여기서 멈춥니다. 이 추정은 언제나 다운로드하는 쪽으로만 치우칩니다.

재개를 누르면 그래도 다운로드합니다.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>When a download cannot be repaired:</source>
        <translation>다운로드를 복구할 수 없을 때:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Publish anyway</source>
        <translation>그래도 게시</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>A movie or episode whose download contains programs or shortcuts is almost always a fake. Only releases with video or audio in them are checked, so software downloads are not affected.

Resume publishes it anyway.</source>
        <translation>다운로드에 프로그램이나 바로 가기가 들어 있는 영화나 에피소드는 거의 언제나 가짜입니다. 비디오나 오디오가 들어 있는 릴리스만 검사하므로 소프트웨어 다운로드는 영향을 받지 않습니다.

재개를 누르면 그래도 게시합니다.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>When a media release has unwanted files:</source>
        <translation>미디어 릴리스에 원치 않는 파일이 있을 때:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>File extensions, separated by commas. A video file that is not really a video counts as well. Leave this empty to turn the check off.</source>
        <translation>쉼표로 구분한 파일 확장자입니다. 실제로는 비디오가 아닌 비디오 파일도 여기에 해당합니다. 비워 두면 이 검사를 끕니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Unwanted file types:</source>
        <translation>원치 않는 파일 유형:</translation>
    </message>
    <message>
        <location line="+191"/>
        <source>The news server list could not be saved.</source>
        <translation>뉴스 서버 목록을 저장하지 못했습니다.</translation>
    </message>
    <message>
        <location line="+146"/>
        <source>(unchanged)</source>
        <translation>(변경 없음)</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>(none set)</source>
        <translation>(설정되지 않음)</translation>
    </message>
    <message>
        <location line="+60"/>
        <source>not measured — the Usenet engine is stopped</source>
        <translation>측정되지 않음 — Usenet 엔진이 중지되었습니다</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>%1 used</source>
        <translation>%1 사용됨</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1 of %2</source>
        <translation>%1 / %2</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>, resets %1</source>
        <translation>, %1에 초기화</translation>
    </message>
    <message>
        <location line="+5"/>
        <source> — spent</source>
        <translation> — 소진됨</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Measured here, not reported by the provider — NNTP has no command that asks. Expect a few percent below your provider&apos;s own figure.</source>
        <translation>제공업체가 알려 준 값이 아니라 여기에서 측정한 값입니다 — NNTP에는 이를 물어보는 명령이 없습니다. 제공업체의 수치보다 몇 퍼센트 낮게 나올 수 있습니다.</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Correct usage</source>
        <translation>사용량 수정</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Used this period, in GB.

Enter what your provider&apos;s control panel says, or 0 to start again.</source>
        <translation>이번 기간에 사용한 양(GB)입니다.

제공업체 제어판에 표시된 값을 입력하거나, 처음부터 다시 세려면 0을 입력하세요.</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>The usage counter could not be changed.</source>
        <translation>사용량 카운터를 변경하지 못했습니다.</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>At most %1 news servers can be configured.</source>
        <translation>뉴스 서버는 최대 %1개까지 설정할 수 있습니다.</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>New server</source>
        <translation>새 서버</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Enter a host name first.</source>
        <translation>먼저 호스트 이름을 입력하세요.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Connecting…</source>
        <translation>연결 중…</translation>
    </message>
    <message>
        <location line="+51"/>
        <source>Search indexers answer keyword searches and hand back an NZB. They are separate from your news servers: on Usenet the provider you download from and the service you search are different businesses.</source>
        <translation>검색 인덱서는 키워드 검색에 답하고 NZB를 돌려줍니다. 뉴스 서버와는 별개입니다. Usenet에서는 다운로드하는 제공업체와 검색하는 서비스가 서로 다른 사업자입니다.</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+384"/>
        <location line="+541"/>
        <source>Indexers</source>
        <translation>인덱서</translation>
    </message>
    <message>
        <location line="-920"/>
        <source>URL</source>
        <translation>URL</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Type</source>
        <translation>유형</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>API key</source>
        <translation>API 키</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Indexer</source>
        <translation>인덱서</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+152"/>
        <source>Display name</source>
        <translation>표시 이름</translation>
    </message>
    <message>
        <location line="-150"/>
        <source>Also the identity of this account: it names the cached capabilities and appears in the Indexer column of the results.</source>
        <translation>이 계정의 식별자이기도 합니다. 캐시된 기능 정보의 이름이 되며 결과의 인덱서 열에 표시됩니다.</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>The API base URL. A bare host gets &quot;/api&quot; added; a URL that already has a path is used exactly as typed, which is what Jackett and NZBHydra2 endpoints need.</source>
        <translation>API 기본 URL입니다. 호스트만 적으면 &quot;/api&quot;가 붙고, 이미 경로가 있는 URL은 입력한 그대로 사용되는데 Jackett과 NZBHydra2 엔드포인트에는 이 방식이 필요합니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>API URL:</source>
        <translation>API URL:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>API key:</source>
        <translation>API 키:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Newznab (Usenet)</source>
        <translation>Newznab (Usenet)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Torznab (BitTorrent)</source>
        <translation>Torznab (BitTorrent)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Both — Prowlarr, NZBHydra2</source>
        <translation>둘 다 — Prowlarr, NZBHydra2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type:</source>
        <translation>유형:</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Searching</source>
        <translation>검색</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Rows to ask for per request. An indexer that allows fewer silently returns fewer, so this is an upper bound rather than a promise.</source>
        <translation>요청 한 번에 요구할 행 수입니다. 더 적게 허용하는 인덱서는 조용히 더 적게 돌려주므로, 이 값은 약속이 아니라 상한입니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Results per request:</source>
        <translation>요청당 결과 수:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>How many pages one search may fetch from each indexer.

Every page is an API call against the allowance your account has, so this is a spending limit, not a speed setting.</source>
        <translation>한 번의 검색이 각 인덱서에서 가져올 수 있는 페이지 수입니다.

페이지마다 계정 허용량을 소모하는 API 호출이 발생하므로, 이는 속도 설정이 아니라 지출 한도입니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Pages per search:</source>
        <translation>검색당 페이지 수:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Request timeout:</source>
        <translation>요청 시간 초과:</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>How often to re-read what each indexer supports. A stale answer never blocks a search — it only means a query field stays greyed out that the indexer has since started accepting.</source>
        <translation>각 인덱서가 지원하는 기능을 얼마나 자주 다시 읽을지 정합니다. 오래된 정보가 검색을 막는 일은 없습니다 — 인덱서가 그사이 받아들이기 시작한 조회 항목이 계속 비활성으로 남을 뿐입니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Refresh capabilities every:</source>
        <translation>기능 정보 갱신 주기:</translation>
    </message>
    <message>
        <location line="+51"/>
        <source>A feed is a search that runs on its own and queues what it finds. Its first check adds nothing — it only records what the indexer already lists, because otherwise a new feed would download everything still on the server.</source>
        <translation>피드는 스스로 실행되어 찾아낸 것을 대기열에 추가하는 검색입니다. 첫 확인에서는 아무것도 추가하지 않고 인덱서에 이미 올라와 있는 항목을 기록만 합니다 — 그렇게 하지 않으면 새 피드가 서버에 남아 있는 모든 것을 내려받게 되기 때문입니다.</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+324"/>
        <location line="+277"/>
        <source>Feeds</source>
        <translation>피드</translation>
    </message>
    <message>
        <location line="-596"/>
        <source>Search</source>
        <translation>검색</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Every</source>
        <translation>주기</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Last checked</source>
        <translation>마지막 확인</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Feed</source>
        <translation>피드</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Also the identity of this feed: it names the file that remembers what the feed has already seen.</source>
        <translation>이 피드의 식별자이기도 합니다. 피드가 이미 본 항목을 기억하는 파일의 이름이 됩니다.</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Search my indexers</source>
        <translation>내 인덱서 검색</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>An RSS address I paste</source>
        <translation>직접 붙여넣는 RSS 주소</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Source:</source>
        <translation>소스:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Keywords. Leave it empty to take everything new in the categories below.</source>
        <translation>키워드입니다. 비워 두면 아래 카테고리의 새 항목을 모두 가져옵니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Search for:</source>
        <translation>검색어:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>e.g. 2000, 5000</source>
        <translation>예: 2000, 5000</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Newznab category numbers, separated by commas. Empty means every category.</source>
        <translation>쉼표로 구분한 Newznab 카테고리 번호입니다. 비워 두면 모든 카테고리를 뜻합니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Categories:</source>
        <translation>카테고리:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Which indexers to ask, by name and separated by commas. Empty means all of them.

Adding one later does not fetch its back catalogue: a new indexer gets its own first check, which adds nothing.</source>
        <translation>어느 인덱서에 물어볼지 이름을 쉼표로 구분해 적습니다. 비워 두면 전부를 뜻합니다.

나중에 인덱서를 추가해도 지난 목록을 가져오지는 않습니다. 새 인덱서는 자체적으로 첫 확인을 거치며, 그때는 아무것도 추가되지 않습니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Indexers:</source>
        <translation>인덱서:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>The RSS address from your indexer&apos;s website. It contains your API key, so it is stored encrypted and is only ever shown back to you with the key hidden.</source>
        <translation>인덱서 웹사이트에서 가져온 RSS 주소입니다. API 키가 들어 있으므로 암호화하여 저장하며, 다시 보여 줄 때는 언제나 키를 가린 채로 표시합니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Feed URL:</source>
        <translation>피드 URL:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Only queue releases whose name matches this pattern. Empty accepts everything.</source>
        <translation>이름이 이 패턴과 일치하는 릴리스만 대기열에 추가합니다. 비워 두면 모두 받아들입니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Must match:</source>
        <translation>일치해야 함:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Never queue a release whose name matches this pattern. It wins over the one above.</source>
        <translation>이름이 이 패턴과 일치하는 릴리스는 대기열에 추가하지 않습니다. 위 항목보다 우선합니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Must not match:</source>
        <translation>일치하면 안 됨:</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+7"/>
        <source> MB</source>
        <translation> MB</translation>
    </message>
    <message>
        <location line="-6"/>
        <source>no minimum</source>
        <translation>최소 없음</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Smallest:</source>
        <translation>최소 크기:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>no maximum</source>
        <translation>최대 없음</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Largest:</source>
        <translation>최대 크기:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>any age</source>
        <translation>기간 무관</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Posted within:</source>
        <translation>게시 기간 이내:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source> minutes</source>
        <translation> 분</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>How often to check. Fifteen minutes is the floor: most indexers ask for no more than that, and checking harder gets an account suspended.</source>
        <translation>얼마나 자주 확인할지 정합니다. 15분이 하한입니다. 대부분의 인덱서가 그보다 자주 확인하지 말 것을 요구하며, 더 자주 확인하면 계정이 정지됩니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Check every:</source>
        <translation>확인 주기:</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+136"/>
        <source>No category</source>
        <translation>카테고리 없음</translation>
    </message>
    <message>
        <location line="-134"/>
        <source>Which download category this feed&apos;s matches go into. The category decides the folder they finish in, and it is resolved when a release completes — so repointing the category moves what is still running with it.</source>
        <translation>이 피드에 일치한 항목이 들어갈 다운로드 카테고리입니다. 카테고리가 완료될 폴더를 정하며, 그 값은 릴리스가 완료될 때 확정됩니다 — 따라서 카테고리를 다른 곳으로 바꾸면 아직 진행 중인 항목도 함께 옮겨집니다.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Download category:</source>
        <translation>다운로드 카테고리:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Queue what it already lists</source>
        <translation>이미 올라와 있는 항목도 대기열에 추가</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Normally a feed&apos;s first check only takes note of what is there and queues nothing, because everything an indexer still holds is new to a feed that has never run. Turn this on to take the back catalogue as well — it can be a great deal of it.</source>
        <translation>보통 피드의 첫 확인은 현재 올라와 있는 항목을 기록만 하고 아무것도 대기열에 넣지 않습니다. 한 번도 실행된 적 없는 피드에게는 인덱서에 남아 있는 모든 것이 새 항목이기 때문입니다. 지난 목록까지 가져오려면 이 옵션을 켜세요 — 그 양이 아주 많을 수 있습니다.</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Check now</source>
        <translation>지금 확인</translation>
    </message>
    <message>
        <location line="+97"/>
        <source>The indexer list could not be saved.</source>
        <translation>인덱서 목록을 저장하지 못했습니다.</translation>
    </message>
    <message>
        <location line="+94"/>
        <source>The feed list could not be saved.</source>
        <translation>피드 목록을 저장하지 못했습니다.</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>%1 min</source>
        <translation>%1분</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>checking…</source>
        <translation>확인 중…</translation>
    </message>
    <message>
        <location line="+113"/>
        <source>%1 queued on the last check; %2 releases remembered.</source>
        <translation>마지막 확인에서 %1개를 대기열에 추가했습니다. 기억 중인 릴리스 %2개.</translation>
    </message>
    <message>
        <location line="+71"/>
        <source>New feed</source>
        <translation>새 피드</translation>
    </message>
    <message>
        <location line="+50"/>
        <source>&quot;%1&quot; could not be checked.</source>
        <translation>&quot;%1&quot;을(를) 확인하지 못했습니다.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Checking &quot;%1&quot;…</source>
        <translation>&quot;%1&quot; 확인 중…</translation>
    </message>
    <message>
        <location line="+53"/>
        <source>Newznab</source>
        <translation>Newznab</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Torznab</source>
        <translation>Torznab</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Both</source>
        <translation>둘 다</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>yes</source>
        <translation>예</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>no</source>
        <translation>아니오</translation>
    </message>
    <message>
        <location line="+68"/>
        <source>(a key is stored — leave empty to keep it)</source>
        <translation>(키가 저장되어 있습니다 — 비워 두면 그대로 유지됩니다)</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>At most %1 indexers can be configured.</source>
        <translation>인덱서는 최대 %1개까지 설정할 수 있습니다.</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>New indexer</source>
        <translation>새 인덱서</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Enter an API URL first.</source>
        <translation>먼저 API URL을 입력하세요.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Contacting the indexer…</source>
        <translation>인덱서에 연결하는 중…</translation>
    </message>
    <message>
        <location line="+73"/>
        <source>Enable BitTorrent</source>
        <translation>BitTorrent 사용</translation>
    </message>
    <message>
        <location line="+3073"/>
        <source>This interface has no default route (split-tunnel VPN?): servers and peers outside its subnets are unreachable. Route all traffic through it, or choose another interface.</source>
        <translation>이 인터페이스에는 기본 경로가 없습니다(스플릿 터널 VPN?). 서브넷 밖의 서버와 피어에 연결할 수 없습니다. 모든 트래픽을 이 인터페이스로 보내거나 다른 인터페이스를 선택하세요.</translation>
    </message>
    <message>
        <location line="+475"/>
        <source>TCP/UDP</source>
        <translation>TCP/UDP</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>One port for eD2K TCP and UDP, Kad and ed3k</source>
        <translation>eD2K TCP와 UDP, Kad, ed3k 공용 포트 하나</translation>
    </message>
    <message>
        <location line="+62"/>
        <source>%1 — not available</source>
        <translation>%1 — 사용할 수 없음</translation>
    </message>
    <message>
        <source>Starts the BitTorrent engine with the daemon. Off, no torrent is downloaded or seeded.</source>
        <translation type="vanished">데몬과 함께 BitTorrent 엔진을 시작합니다. 끄면 토렌트를 다운로드하거나 시딩하지 않습니다.</translation>
    </message>
    <message>
        <location line="-49"/>
        <source>unlimited</source>
        <translation>무제한</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Download: %1, upload: %2</source>
        <translation>다운로드: %1, 업로드: %2</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>The BitTorrent port is the eD2K TCP port.</source>
        <translation>BitTorrent 포트가 eD2K TCP 포트와 같습니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The BitTorrent port is the eD2K UDP port.</source>
        <translation>BitTorrent 포트가 eD2K UDP 포트와 같습니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The BitTorrent port is the web server port.</source>
        <translation>BitTorrent 포트가 웹 서버 포트와 같습니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The BitTorrent port is the core port.</source>
        <translation>BitTorrent 포트가 코어 포트와 같습니다.</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Please choose another port.</source>
        <translation>다른 포트를 선택하세요.</translation>
    </message>
    <message>
        <source>Used for both TCP and UDP. Must differ from the eD2K ports.</source>
        <translation type="vanished">TCP와 UDP 모두에 사용됩니다. eD2K 포트와 달라야 합니다.</translation>
    </message>
    <message>
        <location line="-3458"/>
        <source>Listen port (TCP/UDP):</source>
        <translation>수신 포트 (TCP/UDP):</translation>
    </message>
    <message>
        <source>Forced</source>
        <translation type="vanished">강제</translation>
    </message>
    <message>
        <source>Enabled prefers encrypted peers and still accepts plain ones. Forced refuses unencrypted peers, which leaves fewer to download from.</source>
        <translation type="vanished">활성화는 암호화된 피어를 우선하지만 일반 피어도 허용합니다. 강제는 암호화되지 않은 피어를 거부하므로 다운로드할 피어가 줄어듭니다.</translation>
    </message>
    <message>
        <location line="+34"/>
        <source>Find peers through the DHT</source>
        <translation>DHT로 피어 찾기</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Find peers on the local network (LSD)</source>
        <translation>로컬 네트워크에서 피어 찾기 (LSD)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Exchange peers with other clients (PEX)</source>
        <translation>다른 클라이언트와 피어 교환 (PEX)</translation>
    </message>
    <message>
        <source>Unlimited</source>
        <translation type="vanished">무제한</translation>
    </message>
    <message>
        <source>Download:</source>
        <translation type="vanished">다운로드:</translation>
    </message>
    <message>
        <source>Upload:</source>
        <translation type="vanished">업로드:</translation>
    </message>
    <message>
        <location line="-144"/>
        <source>Active downloads:</source>
        <translation>활성 다운로드:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Active seeds:</source>
        <translation>활성 시드:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Seeding</source>
        <translation>시딩 중</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Stop seeding a torrent once it has uploaded this many times its size.</source>
        <translation>토렌트 크기의 이 배수만큼 업로드하면 시딩을 중지합니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Stop at share ratio:</source>
        <translation>공유 비율 도달 시 중지:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source> min</source>
        <translation> 분</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Stop after seeding for:</source>
        <translation>시딩 시간 경과 후 중지:</translation>
    </message>
    <message>
        <location line="-43"/>
        <source>Download folder</source>
        <translation>다운로드 폴더</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Incoming folder</source>
        <translation>수신 폴더</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Leave empty to save torrents to the incoming folder.</source>
        <translation>비워 두면 토렌트를 수신 폴더에 저장합니다.</translation>
    </message>
    <message>
        <source>Open .torrent files, magnet and ed3k links with eMule Qt</source>
        <translation type="vanished">.torrent 파일, magnet 및 ed3k 링크를 eMule Qt로 열기</translation>
    </message>
    <message>
        <source>Makes eMule Qt the default app for .torrent files, magnet: and ed3k: links. Turning it off hands them to another app that opens them, if there is one.</source>
        <translation type="vanished">eMule Qt를 .torrent 파일, magnet: 및 ed3k: 링크의 기본 앱으로 지정합니다. 끄면 이를 열 수 있는 다른 앱이 있을 경우 그 앱에 넘깁니다.</translation>
    </message>
    <message>
        <source>Claim .torrent files, magnet: and ed3k: links for this copy of eMule Qt. The setting is for you alone and needs no administrator; it is re-applied at every start, so another program taking the association does not keep it.</source>
        <translation type="vanished">.torrent 파일, magnet: 및 ed3k: 링크를 이 eMule Qt에 연결합니다. 이 설정은 사용자 본인에게만 적용되며 관리자 권한이 필요 없습니다. 시작할 때마다 다시 적용되므로 다른 프로그램이 연결을 가져가도 유지되지 않습니다.</translation>
    </message>
    <message>
        <location line="+73"/>
        <source>BitTorrent download folder</source>
        <translation>BitTorrent 다운로드 폴더</translation>
    </message>
    <message>
        <location line="+190"/>
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
        <location line="+194"/>
        <source>Log fake-file header warnings for shared files too</source>
        <translation>공유 파일의 가짜 파일 헤더 경고도 기록</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Downloads are always checked and logged. Shared files are checked again at every start.</source>
        <translation>다운로드는 항상 검사되고 기록됩니다. 공유 파일은 시작할 때마다 다시 검사됩니다.</translation>
    </message>
    <message>
        <location line="+1870"/>
        <location line="+20"/>
        <source>Could not update the file associations: %1</source>
        <translation>파일 연결을 업데이트할 수 없습니다: %1</translation>
    </message>
    <message>
        <location line="+983"/>
        <location line="+16"/>
        <location line="+6"/>
        <location line="+3"/>
        <location line="+3"/>
        <source>Rehash All Files</source>
        <translation>모든 파일 다시 해시</translation>
    </message>
    <message>
        <location line="-27"/>
        <source>This reads every shared file from disk again and rebuilds known.met. It takes a long time and should only be used after hard disk errors.

Sharing goes on with the current known.met until the rehash is done.

Rehash all shared files now?</source>
        <translation>모든 공유 파일을 디스크에서 다시 읽고 known.met을 다시 만듭니다. 시간이 오래 걸리므로 하드 디스크 오류 후에만 사용하십시오.

다시 해시가 끝날 때까지 현재 known.met으로 계속 공유합니다.

지금 모든 공유 파일을 다시 해시하시겠습니까?</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>The core did not start the rehash.</source>
        <translation>코어가 다시 해시를 시작하지 않았습니다.</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>A rehash of all files is already running.</source>
        <translation>모든 파일 다시 해시가 이미 실행 중입니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>There are no shared files to rehash.</source>
        <translation>다시 해시할 공유 파일이 없습니다.</translation>
    </message>
    <message numerus="yes">
        <location line="+3"/>
        <source>Rehashing %n shared file(s) in the background. known.met is replaced when all are done; the log shows the result.</source>
        <translation>
            <numerusform>공유 파일 %n개를 백그라운드에서 다시 해시하는 중입니다. 모두 끝나면 known.met이 교체되며, 결과는 로그에 표시됩니다.</numerusform>
        </translation>
    </message>
    <message>
        <location line="+114"/>
        <source>UDP is disabled in the config file (udpPort: 0)</source>
        <translation>설정 파일에서 UDP가 비활성화되어 있습니다 (udpPort: 0)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Kad needs UDP, which is disabled in the config file</source>
        <translation>Kad에는 UDP가 필요하지만 설정 파일에서 비활성화되어 있습니다</translation>
    </message>
    <message>
        <source>A VPN seems to be active (%1). Incoming connections then arrive through the VPN, so port forwarding on your router does not help. Forward the ports at your VPN provider instead, if it offers port forwarding.</source>
        <translation type="vanished">VPN이 활성화된 것 같습니다 (%1). 들어오는 연결이 VPN을 통해 도착하므로 라우터의 포트 포워딩은 도움이 되지 않습니다. VPN 제공업체가 포트 포워딩을 지원한다면 그곳에서 포트를 포워딩하세요.</translation>
    </message>
    <message>
        <source>Could not find your router&apos;s address.

Look up the &quot;default gateway&quot; in this computer&apos;s network settings and open that address in a web browser.</source>
        <translation type="vanished">라우터 주소를 찾을 수 없습니다.

이 컴퓨터의 네트워크 설정에서 &quot;기본 게이트웨이&quot;를 확인한 후 웹 브라우저에서 그 주소를 여세요.</translation>
    </message>
    <message>
        <source>Could not open %1 in the default browser.</source>
        <translation type="vanished">기본 브라우저에서 %1을(를) 열 수 없습니다.</translation>
    </message>
    <message>
        <source>Your router is at %1.</source>
        <translation type="vanished">라우터 주소는 %1입니다.</translation>
    </message>
    <message>
        <source>Open Router Page</source>
        <translation type="vanished">라우터 페이지 열기</translation>
    </message>
    <message>
        <location line="-70"/>
        <location line="+52"/>
        <source>Directories</source>
        <translation>폴더</translation>
    </message>
    <message>
        <location line="-36"/>
        <source>Please enter the incoming folder.</source>
        <translation>받은 파일 폴더를 입력하세요.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The incoming folder must be a full path:
%1</source>
        <translation>받은 파일 폴더는 전체 경로여야 합니다:
%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The incoming folder does not exist:
%1

Create it?</source>
        <translation>받은 파일 폴더가 없습니다:
%1

만드시겠습니까?</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Please enter the temporary folder.</source>
        <translation>임시 폴더를 입력하세요.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The temporary folder must be a full path:
%1</source>
        <translation>임시 폴더는 전체 경로여야 합니다:
%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The temporary folder does not exist:
%1

Create it?</source>
        <translation>임시 폴더가 없습니다:
%1

만드시겠습니까?</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>The temporary folder can&apos;t be the same as the incoming folder.</source>
        <translation>임시 폴더는 받은 파일 폴더와 같을 수 없습니다.</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>This is a file, not a folder:
%1</source>
        <translation>폴더가 아니라 파일입니다:
%1</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Could not create the folder:
%1</source>
        <translation>폴더를 만들 수 없습니다:
%1</translation>
    </message>
    <message>
        <location line="-3198"/>
        <source>Disabled</source>
        <translation>비활성화됨</translation>
    </message>
    <message>
        <location line="+1145"/>
        <source>Theme</source>
        <translation>테마</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The theme change will take effect after restarting the application.</source>
        <translation>테마 변경은 응용 프로그램을 다시 시작한 후 적용됩니다.</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Font size</source>
        <translation>글꼴 크기</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The font size change will take effect after restarting the application.</source>
        <translation>글꼴 크기 변경은 응용 프로그램을 다시 시작한 후 적용됩니다.</translation>
    </message>
    <message>
        <location line="+1135"/>
        <source>specialdlp loaded, DLP v%1</source>
        <translation>specialdlp 로드됨, DLP v%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>specialdlp library not found, checks are inactive</source>
        <translation>specialdlp 라이브러리를 찾을 수 없어 검사가 비활성 상태입니다</translation>
    </message>
    <message>
        <location line="+816"/>
        <source>Automatic</source>
        <translation>자동</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>about %1 slots</source>
        <translation>약 %1개 슬롯</translation>
    </message>
    <message>
        <source>Autotake eD2K links only during runtime</source>
        <translation type="vanished">실행 중에만 eD2K 링크 자동 수락</translation>
    </message>
    <message>
        <location line="-3090"/>
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
        <source>Open/close ports on WinXP firewall when starting/exiting eMule</source>
        <translation type="vanished">eMule 시작/종료 시 WinXP 방화벽 포트 열기/닫기</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Filter server and client LAN IPs</source>
        <translation>서버 및 클라이언트 LAN IP 필터</translation>
    </message>
    <message>
        <location line="-4788"/>
        <source>Show more controls (advanced mode controls)</source>
        <translation>더 많은 컨트롤 표시(고급 모드)</translation>
    </message>
    <message>
        <location line="-444"/>
        <location line="+53"/>
        <source>ed3k</source>
        <translation>ed3k</translation>
    </message>
    <message>
        <location line="-49"/>
        <source>Credits</source>
        <translation>크레딧</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Names</source>
        <translation>이름</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Hoster</source>
        <translation>호스터</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Filters</source>
        <translation>필터</translation>
    </message>
    <message>
        <location line="+211"/>
        <source>Open eD2K and ed3k links with eMule Qt</source>
        <translation>eD2K 및 ed3k 링크를 eMule Qt로 열기</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Makes eMule Qt the default app for ed2k:// and ed3k: links. Turning it off hands them to another app that opens them, if there is one.</source>
        <translation>eMule Qt를 ed2k:// 및 ed3k: 링크의 기본 앱으로 지정합니다. 끄면 이를 열 수 있는 다른 앱이 있을 경우 그 앱에 넘깁니다.</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Makes eMule Qt the handler for ed2k:// and ed3k: links.</source>
        <translation>eMule Qt를 ed2k:// 및 ed3k: 링크 처리기로 지정합니다.</translation>
    </message>
    <message>
        <location line="+65"/>
        <source>Certificate (SHA-256)</source>
        <translation>인증서 (SHA-256)</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>First Seen</source>
        <translation>처음 확인</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Trusted cores:</source>
        <translation>신뢰하는 코어:</translation>
    </message>
    <message>
        <location line="+1121"/>
        <source>Rehash All Files...</source>
        <translation>모든 파일 다시 해시...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Reads every shared file again and rebuilds known.met from the result. Only needed after hard disk errors.</source>
        <translation>모든 공유 파일을 다시 읽고 그 결과로 known.met을 다시 만듭니다. 하드 디스크 오류 후에만 필요합니다.</translation>
    </message>
    <message>
        <location line="+3445"/>
        <source>Runs the BitTorrent engine in the core. Off, the engine stops: no torrent is downloaded or seeded and the BitTorrent port is closed. Torrents stay in the list and continue when it is switched on again.</source>
        <translation>코어에서 BitTorrent 엔진을 실행합니다. 끄면 엔진이 멈추고 토렌트를 다운로드하거나 시딩하지 않으며 BitTorrent 포트가 닫힙니다. 토렌트는 목록에 남아 있다가 다시 켜면 계속됩니다.</translation>
    </message>
    <message>
        <location line="+58"/>
        <source>Open .torrent files and magnet links with eMule Qt</source>
        <translation>.torrent 파일과 마그넷 링크를 eMule Qt로 열기</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Makes eMule Qt the default app for .torrent files and magnet: links. Turning it off hands them to another app that opens them, if there is one.</source>
        <translation>eMule Qt를 .torrent 파일 및 magnet: 링크의 기본 앱으로 지정합니다. 끄면 이를 열 수 있는 다른 앱이 있을 경우 그 앱에 넘깁니다.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Claim .torrent files and magnet: links for this copy of eMule Qt. The setting is for you alone and needs no administrator; it is re-applied at every start, so another program taking the association does not keep it.</source>
        <translation>.torrent 파일과 magnet: 링크를 이 eMule Qt에 연결합니다. 이 설정은 현재 사용자에게만 적용되며 관리자 권한이 필요 없습니다. 시작할 때마다 다시 적용되므로, 다른 프로그램이 연결을 가져가더라도 계속 유지하지는 못합니다.</translation>
    </message>
    <message>
        <location line="+52"/>
        <source>Asks emule-qt.org whether the port the core uses now is reachable over TCP and UDP. Apply a changed port first.</source>
        <translation>코어가 현재 사용하는 포트에 TCP와 UDP로 접근 가능한지 emule-qt.org에 확인합니다. 포트를 변경했다면 먼저 적용하세요.</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Protocol Encryption</source>
        <translation>프로토콜 암호화</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Enable protocol encryption</source>
        <translation>프로토콜 암호화 사용</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Prefers encrypted peers and still accepts unencrypted ones.</source>
        <translation>암호화된 피어를 우선하며 암호화되지 않은 피어도 허용합니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Allow encrypted connections only (not recommended)</source>
        <translation>암호화된 연결만 허용 (권장하지 않음)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Refuses unencrypted peers, which leaves fewer to download from.</source>
        <translation>암호화되지 않은 피어를 거부하므로 다운로드할 수 있는 피어가 줄어듭니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Disable protocol encryption</source>
        <translation>프로토콜 암호화 사용 안 함</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Connects without encryption only. Peers that require it cannot connect.</source>
        <translation>암호화 없이만 연결합니다. 암호화를 요구하는 피어는 연결할 수 없습니다.</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Global limits:</source>
        <translation>전체 제한:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>eD2K and Usenet come first. BitTorrent keeps its share while they are busy and uses all the bandwidth they leave idle.</source>
        <translation>eD2K와 Usenet이 우선합니다. BitTorrent는 이들이 사용 중일 때 자신의 몫을 유지하고, 남는 대역폭은 모두 사용합니다.</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Max. download:</source>
        <translation>최대 다운로드:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Max. upload:</source>
        <translation>최대 업로드:</translation>
    </message>
    <message>
        <location line="+137"/>
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
        <location line="+5"/>
        <source>Allocate full file size for non-sparse part files</source>
        <translation>비-sparse part 파일에 전체 파일 크기 할당</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Check disk space</source>
        <translation>디스크 공간 확인</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Min. free disk space [MB]:</source>
        <translation>최소 여유 디스크 공간 [MB]:</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Max. disk load while hashing:</source>
        <translation>해싱 중 최대 디스크 부하:</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Hashing pauses between reads so the disk stays responsive. 100 % means no limit.</source>
        <translation>디스크 응답성을 유지하도록 해싱은 읽기 사이에 잠시 멈춥니다. 100 %는 제한 없음을 의미합니다.</translation>
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
        <source>Log level:</source>
        <translation type="vanished">로그 수준:</translation>
    </message>
    <message>
        <location line="+9"/>
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
        <location line="+16"/>
        <source>Show Usenet log tab</source>
        <translation>Usenet 로그 탭 표시</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Upload SpeedSense (not recommended)</source>
        <translation>업로드 속도 감지(권장하지 않음)</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Find best upload limit automatically</source>
        <translation>최적의 업로드 제한을 자동으로 찾기</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Lowest allowed upload speed:</source>
        <translation>허용되는 최저 업로드 속도:</translation>
    </message>
    <message>
        <location line="+10"/>
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
        <location line="+23"/>
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
        <location line="+22"/>
        <source>Queue size: %1</source>
        <translation>대기열 크기: %1</translation>
    </message>
    <message>
        <location line="+296"/>
        <source>Enter the datarate limit: (%1, KB/s)</source>
        <translation>데이터 전송률 제한을 입력하십시오: (%1, KB/s)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Please enter the new value: (%1)</source>
        <translation>새 값을 입력하십시오: (%1)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Configure Action</source>
        <translation>동작 구성</translation>
    </message>
    <message>
        <location line="+40"/>
        <source>Select category</source>
        <translation>카테고리 선택</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>All uncategorized</source>
        <translation>분류되지 않은 모든 항목</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>All</source>
        <translation>전체</translation>
    </message>
    <message>
        <location line="+299"/>
        <source>Proxy settings will only apply to new connections.
Restart eMule for all connections to use the new proxy settings.

News server connections switch over immediately.</source>
        <translation>프록시 설정은 새 연결에만 적용됩니다.
모든 연결에서 새 프록시 설정을 사용하려면 eMule을 재시작하세요.

뉴스 서버 연결은 즉시 전환됩니다.</translation>
    </message>
    <message>
        <location line="+509"/>
        <source>Web Interface</source>
        <translation>웹 인터페이스</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&quot;%1&quot; is not an IP address. The listen address was not changed.</source>
        <translation>&quot;%1&quot;은(는) IP 주소가 아닙니다. 수신 주소는 변경되지 않았습니다.</translation>
    </message>
    <message>
        <location line="+518"/>
        <location line="+20"/>
        <source>File types</source>
        <translation>파일 유형</translation>
    </message>
    <message>
        <source>Could not update the .nzb file association: %1</source>
        <translation type="vanished">.nzb 파일 연결을 업데이트하지 못했습니다: %1</translation>
    </message>
    <message>
        <location line="+709"/>
        <source>Port forwarding: %1</source>
        <translation>포트 전달: %1</translation>
    </message>
    <message>
        <location line="+105"/>
        <source>Database: not connected to the core</source>
        <translation>데이터베이스: 코어에 연결되지 않음</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Downloading the GeoLite2 country database...</source>
        <translation>GeoLite2 국가 데이터베이스 다운로드 중...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Update failed: the core did not answer</source>
        <translation>업데이트 실패: 코어가 응답하지 않음</translation>
    </message>
    <message>
        <location line="+53"/>
        <source>Database: GeoLite2-Country from %1</source>
        <translation>데이터베이스: GeoLite2-Country (%1)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Database: none — enter your MaxMind account ID and license key</source>
        <translation>데이터베이스: 없음 — MaxMind 계정 ID와 라이선스 키를 입력하세요</translation>
    </message>
    <message>
        <location line="+3"/>
        <source> (update running)</source>
        <translation> (업데이트 중)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Last update failed: %1</source>
        <translation>마지막 업데이트 실패: %1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Filename Cleanup</source>
        <translation>파일 이름 정리</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Text to remove from the names of new downloads. Case is ignored.</source>
        <translation>새 다운로드의 이름에서 제거할 텍스트입니다. 대소문자는 구분하지 않습니다.</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Reset to Default</source>
        <translation>기본값으로 재설정</translation>
    </message>
    <message>
        <location line="-7932"/>
        <location line="+2520"/>
        <location line="+1035"/>
        <location line="+154"/>
        <location line="+1627"/>
        <location line="+316"/>
        <location line="+2278"/>
        <source>Remove</source>
        <translation>제거</translation>
    </message>
    <message>
        <location line="-6645"/>
        <source>New eMule Qt version detected</source>
        <translation>새 eMule Qt 버전이 감지되었습니다</translation>
    </message>
    <message>
        <location line="+412"/>
        <source>Update from URL: (filter.dat- or PeerGuardian-format, .gz/.zip accepted)</source>
        <translation>URL에서 업데이트: (filter.dat 또는 PeerGuardian 형식, .gz/.zip 허용)</translation>
    </message>
    <message>
        <location line="+3338"/>
        <source>Write eMule core logs to disk</source>
        <translation>eMule 코어 로그를 디스크에 기록</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Write eMule GUI logs to disk</source>
        <translation>eMule GUI 로그를 디스크에 기록</translation>
    </message>
    <message>
        <location line="+19"/>
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
        <location line="+13"/>
        <source>Enable IPC log tab</source>
        <translation>IPC 로그 탭 활성화</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Start core with console (debug)</source>
        <translation>콘솔과 함께 코어 시작(디버그)</translation>
    </message>
    <message>
        <location line="+101"/>
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
        <location line="-2525"/>
        <location line="+929"/>
        <location line="+1600"/>
        <source> s</source>
        <translation> 초</translation>
    </message>
    <message>
        <location line="-5119"/>
        <source>Downloads that are receiving data, and the sources sending it, are drawn in bold. A source listed under another file (A4AF) is bold too while it transfers. The connected server and the selected category tab are bold as well.</source>
        <translation>데이터를 받는 다운로드와 이를 보내는 소스를 굵게 표시합니다. 다른 파일 아래에 있는 소스(A4AF)도 전송 중에는 굵게 표시됩니다. 연결된 서버와 선택된 카테고리 탭도 굵게 표시됩니다.</translation>
    </message>
    <message>
        <location line="+1012"/>
        <source>Rename downloads to the name most sources use</source>
        <translation>다운로드 이름을 대부분의 소스가 쓰는 이름으로 변경</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Renames a download once when at least 5 sources and 60% of them use the same name. Never after you renamed it yourself, and not when the file looks fake.</source>
        <translation>최소 5개 소스와 그중 60%가 같은 이름을 쓰면 다운로드 이름을 한 번 변경합니다. 직접 이름을 바꾼 뒤나 가짜로 보이는 파일에는 적용하지 않습니다.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Watch shared folders for new files</source>
        <translation>공유 폴더에서 새 파일 감시</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Shares files added to a shared folder as they appear. When off, new files are only picked up on restart or when you reload the shared files.</source>
        <translation>공유 폴더에 추가된 파일을 나타나는 즉시 공유합니다. 끄면 새 파일은 다시 시작하거나 공유 파일을 다시 불러올 때만 인식됩니다.</translation>
    </message>
    <message>
        <location line="+597"/>
        <source>the core did not answer</source>
        <translation>코어가 응답하지 않았습니다</translation>
    </message>
    <message numerus="yes">
        <location line="+10"/>
        <source>IP filter updated and reloaded (%n range(s)).</source>
        <translation>
            <numerusform>IP 필터를 업데이트하고 다시 불러왔습니다(%n개 범위).</numerusform>
        </translation>
    </message>
    <message>
        <location line="+56"/>
        <source>Update automatically every</source>
        <translation>자동 업데이트 주기</translation>
    </message>
    <message>
        <location line="+864"/>
        <source>Decimal GB, because that is what an invoice says — the 1024-based GB used elsewhere in eMule would put a 1000 GB plan 7% over.

Set it slightly under your plan. The figure is measured here, so it reads a few percent below your provider&apos;s, and articles already in flight when the limit is reached still finish.</source>
        <translation>청구서에 표시되는 단위인 10진수 GB입니다. eMule의 다른 곳에서 쓰는 1024 기반 GB로는 1000 GB 요금제를 7% 초과하게 됩니다.

요금제보다 약간 낮게 설정하세요. 이 값은 여기에서 측정되므로 제공업체의 수치보다 몇 퍼센트 낮게 표시되며, 한도에 도달했을 때 이미 전송 중인 기사는 그대로 완료됩니다.</translation>
    </message>
    <message>
        <location line="+1950"/>
        <source>Queue</source>
        <translation>대기열</translation>
    </message>
    <message>
        <location line="+91"/>
        <source>Used for both TCP and UDP. Must differ from the eD2K ports, the web server port and the core port.</source>
        <translation>TCP와 UDP 모두에 사용됩니다. eD2K 포트, 웹 서버 포트, 코어 포트와 달라야 합니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Random</source>
        <translation>무작위</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Picks a port that is free for TCP and UDP on this computer.</source>
        <translation>이 컴퓨터에서 TCP와 UDP 모두 사용 가능한 포트를 고릅니다.</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Map port with UPnP/NAT-PMP</source>
        <translation>UPnP/NAT-PMP로 포트 매핑</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Forwards the BitTorrent port on the router. Needs &quot;Use UPnP to Setup Ports&quot; on the Connection page.</source>
        <translation>라우터에서 BitTorrent 포트를 포워딩합니다. &quot;연결&quot; 페이지의 &quot;UPnP로 포트 설정&quot;이(가) 필요합니다.</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Peers</source>
        <translation>피어</translation>
    </message>
    <message>
        <location line="+68"/>
        <source>Peer connections of all torrents together.</source>
        <translation>모든 토렌트를 합친 피어 연결 수.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Max. connections:</source>
        <translation>최대 연결 수:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Max. connections per torrent:</source>
        <translation>토렌트당 최대 연결 수:</translation>
    </message>
    <message>
        <source>Global limits</source>
        <translation type="vanished">전체 제한</translation>
    </message>
    <message>
        <location line="-57"/>
        <source>Set on the Connection page.</source>
        <translation>&quot;연결&quot; 페이지에서 설정합니다.</translation>
    </message>
    <message>
        <source>eD2K and Usenet come first. BitTorrent keeps the share below while they are busy and uses all the bandwidth they leave idle.</source>
        <translation type="vanished">eD2K와 Usenet이 우선입니다. BitTorrent는 이들이 사용 중일 때 아래 비율을 유지하고, 이들이 쓰지 않는 대역폭은 모두 사용합니다.</translation>
    </message>
    <message>
        <source>Sharing</source>
        <translation type="vanished">분배</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>How much of the global download limit BitTorrent keeps while eD2K or Usenet is also downloading. When they are idle, BitTorrent may use all of it.</source>
        <translation>eD2K 또는 Usenet도 다운로드 중일 때 BitTorrent가 유지하는 전체 다운로드 제한의 비율입니다. 이들이 쉬고 있으면 BitTorrent가 전부 사용할 수 있습니다.</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Share of the upload limit:</source>
        <translation>업로드 제한 중 할당 비율:</translation>
    </message>
    <message>
        <location line="-2"/>
        <source>How much of the global upload limit BitTorrent keeps while eD2K is also uploading. When it is idle, BitTorrent may use all of it.</source>
        <translation>eD2K도 업로드 중일 때 BitTorrent가 유지하는 전체 업로드 제한의 비율입니다. eD2K가 쉬고 있으면 BitTorrent가 전부 사용할 수 있습니다.</translation>
    </message>
    <message>
        <source>Hard limits</source>
        <translation type="vanished">고정 제한</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>BitTorrent never goes above this, even with bandwidth to spare. At most the global limit.</source>
        <translation>대역폭이 남아도 BitTorrent는 이 값을 넘지 않습니다. 최대 전체 제한까지입니다.</translation>
    </message>
    <message>
        <location line="+116"/>
        <source>Max. firewalled Kad nodes served as buddy:</source>
        <translation>버디로 중계할 방화벽 뒤 Kad 노드 최대 수:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>While your ports are open, eMule relays callback requests for this many firewalled Kad nodes. Each one uses a connection, so at most a tenth of &quot;Max. connections&quot; is served.</source>
        <translation>포트가 열려 있는 동안 eMule은 이 수만큼의 방화벽 뒤 Kad 노드를 위해 콜백 요청을 중계합니다. 노드마다 연결 하나를 사용하므로 &quot;최대 연결 수&quot;의 10분의 1까지만 중계합니다.</translation>
    </message>
    <message>
        <location line="+423"/>
        <source>New</source>
        <translation>새로 만들기</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+19"/>
        <source>Title</source>
        <translation>제목</translation>
    </message>
    <message>
        <location line="-19"/>
        <source>Days</source>
        <translation>일</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Start Time</source>
        <translation>시작 시간</translation>
    </message>
    <message>
        <location line="+10"/>
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
        <location line="+113"/>
        <source>Daily</source>
        <translation>매일</translation>
    </message>
    <message>
        <location line="-113"/>
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
        <location line="+5"/>
        <source>Action</source>
        <translation>동작</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Value</source>
        <translation>값</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>New Schedule</source>
        <translation>새 일정</translation>
    </message>
    <message>
        <location line="-2910"/>
        <location line="+1035"/>
        <location line="+154"/>
        <location line="+1900"/>
        <location line="+2316"/>
        <source>Add</source>
        <translation>추가</translation>
    </message>
    <message>
        <source>Action Value</source>
        <translation type="vanished">동작 값</translation>
    </message>
    <message>
        <source>Enter value:</source>
        <translation type="vanished">값 입력:</translation>
    </message>
    <message>
        <location line="-2279"/>
        <source>Edit Value</source>
        <translation>값 편집</translation>
    </message>
    <message>
        <location line="+99"/>
        <source>The %1 settings page is not yet implemented.</source>
        <translation>설정 페이지 %1은(는) 아직 구현되지 않았습니다.</translation>
    </message>
    <message>
        <location line="+192"/>
        <source>Proxy</source>
        <translation>프록시</translation>
    </message>
    <message>
        <source>Proxy settings will only apply to new connections.
Restart eMule for all connections to use the new proxy settings.</source>
        <translation type="vanished">프록시 설정은 새 연결에만 적용됩니다.
모든 연결에서 새 프록시 설정을 사용하려면 eMule을 재시작하세요.</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>The language change will take effect after restarting the application.</source>
        <translation>언어 변경은 애플리케이션을 재시작한 후 적용됩니다.</translation>
    </message>
    <message>
        <location line="+34"/>
        <source>Core connection settings will take effect after restarting the application.</source>
        <translation>코어 연결 설정은 애플리케이션을 재시작한 후 적용됩니다.</translation>
    </message>
    <message>
        <location line="+37"/>
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
        <location filename="../src/gui/dialogs/PasteLinksDialog.cpp" line="+16"/>
        <source>Paste eD2K Links</source>
        <translation>eD2K 링크 붙여넣기</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>eD2K Links:</source>
        <translation>eD2K 링크:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paste one or more ed2k:// links here, one per line...</source>
        <translation>여기에 ed2k:// 링크를 한 줄에 하나씩 붙여넣으세요...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Download</source>
        <translation>다운로드</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation type="vanished">취소</translation>
    </message>
    <message>
        <location line="+19"/>
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
    <name>eMule::PasteTextDialog</name>
    <message>
        <location filename="../src/gui/dialogs/PasteTextDialog.cpp" line="+44"/>
        <source>optional</source>
        <translation>선택 사항</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Category:</source>
        <translation>카테고리:</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Priority:</source>
        <translation>우선순위:</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+25"/>
        <source>Start paused</source>
        <translation>일시 정지 상태로 시작</translation>
    </message>
    <message>
        <location line="-14"/>
        <source>default download folder</source>
        <translation>기본 다운로드 폴더</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Browse…</source>
        <translation>찾아보기…</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Save to</source>
        <translation>저장 위치</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Cancel</source>
        <translation>취소</translation>
    </message>
    <message>
        <location line="+91"/>
        <source>Working…</source>
        <translation>처리 중…</translation>
    </message>
</context>
<context>
    <name>eMule::PeerPreviewDialog</name>
    <message>
        <location filename="../src/gui/dialogs/PeerPreviewDialog.cpp" line="+17"/>
        <source>Preview: %1</source>
        <translation>미리 보기: %1</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Prior</source>
        <translation>이전</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Next</source>
        <translation>다음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Close</source>
        <translation>닫기</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>No images</source>
        <translation>이미지 없음</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Image %1 of %2</source>
        <translation>이미지 %1 / %2</translation>
    </message>
</context>
<context>
    <name>eMule::RouterPage</name>
    <message>
        <location filename="../src/gui/dialogs/RouterPage.h" line="+30"/>
        <source>Open Router</source>
        <translation>라우터 열기</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>A VPN seems to be active (%1). Incoming connections then arrive through the VPN, so port forwarding on your router does not help. Forward the ports at your VPN provider instead, if it offers port forwarding.</source>
        <translation>VPN이 활성화된 것 같습니다 (%1). 들어오는 연결이 VPN을 통해 도착하므로 라우터의 포트 포워딩은 도움이 되지 않습니다. VPN 제공업체가 포트 포워딩을 지원한다면 그곳에서 포트를 포워딩하세요.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Could not find your router&apos;s address.

Look up the &quot;default gateway&quot; in this computer&apos;s network settings and open that address in a web browser.</source>
        <translation>라우터 주소를 찾을 수 없습니다.

이 컴퓨터의 네트워크 설정에서 &quot;기본 게이트웨이&quot;를 확인한 후 웹 브라우저에서 그 주소를 여세요.</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Could not open %1 in the default browser.</source>
        <translation>기본 브라우저에서 %1을(를) 열 수 없습니다.</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Your router is at %1.</source>
        <translation>라우터 주소는 %1입니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Open Router Page</source>
        <translation>라우터 페이지 열기</translation>
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
    <name>eMule::SearchExprParser</name>
    <message>
        <location filename="../src/core/search/SearchExprParser.cpp" line="+123"/>
        <location line="+678"/>
        <source>Failed to parse search expression</source>
        <translation>검색식 분석 실패</translation>
    </message>
    <message>
        <location line="-668"/>
        <source>Expression too complex</source>
        <translation>검색식이 너무 복잡함</translation>
    </message>
    <message>
        <location line="+79"/>
        <location line="+6"/>
        <location line="+48"/>
        <source>Unterminated string</source>
        <translation>닫히지 않은 문자열</translation>
    </message>
    <message>
        <location line="+49"/>
        <location line="+18"/>
        <source>Unknown search attribute &apos;%1&apos;</source>
        <translation>알 수 없는 검색 속성: &apos;%1&apos;</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Invalid eD2K file link</source>
        <translation>잘못된 eD2K 파일 링크</translation>
    </message>
    <message>
        <location line="+233"/>
        <source>Syntax error in &apos;%1&apos; search attribute</source>
        <translation>검색 속성 &apos;%1&apos; 중 문법 오류</translation>
    </message>
    <message>
        <location line="+50"/>
        <source>Missing right side for AND operator</source>
        <translation>AND 연산자의 우측 항이 없음</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Missing right side for OR operator</source>
        <translation>OR 연산자의 우측 항이 없음</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Missing right side for NOT operator</source>
        <translation>NOT 연산자의 우측 항이 없음</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Missing expression for &apos;(&apos; &apos;)&apos;</source>
        <translation>&apos;(&apos; &apos;)&apos; 안에 식이 없음</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Missing closing &apos;)&apos;</source>
        <translation>닫는 &apos;)&apos; 없음</translation>
    </message>
    <message>
        <location line="+66"/>
        <source>Missing left side for NOT operator</source>
        <translation>NOT 연산자의 좌측 항이 없음</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Missing left side for AND operator</source>
        <translation>AND 연산자의 좌측 항이 없음</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Missing left side for OR operator</source>
        <translation>OR 연산자의 좌측 항이 없음</translation>
    </message>
    <message>
        <location line="+174"/>
        <source>You cannot use the first keyword with an OR operator when searching on Kad!

For example &quot;Keyword1 OR Keyword2&quot; is not possible. Try &quot;Keyword1 AND (Keyword2 OR Keyword3)&quot; or do multiple searches instead.</source>
        <translation>Kad에서 검색할 때는 첫 번째 키워드를 OR 연산자와 함께 사용할 수 없습니다!

예를 들어 &quot;Keyword1 OR Keyword2&quot;는 사용할 수 없습니다. &quot;Keyword1 AND (Keyword2 OR Keyword3)&quot;를 사용하거나 여러 번 나누어 검색하세요.</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Keyword too short.

The keyword(s) used in a Kad search expression must have a minimum length of 3 characters.</source>
        <translation>검색어가 너무 짧습니다.

Kad 검색식 검색어는 최소 3자 이상이어야 합니다.</translation>
    </message>
</context>
<context>
    <name>eMule::SearchPanel</name>
    <message>
        <location filename="../src/gui/panels/SearchPanel.cpp" line="+333"/>
        <location line="+1027"/>
        <location line="+13"/>
        <location line="+607"/>
        <source>Download</source>
        <translation>다운로드</translation>
    </message>
    <message>
        <location line="-1627"/>
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
        <location line="+5"/>
        <source>Usenet (Indexer)</source>
        <translation>Usenet (인덱서)</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Usenet (Server)</source>
        <translation>Usenet (서버)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Torrent (Server)</source>
        <translation>Torrent (서버)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Web (Server)</source>
        <translation>웹 (서버)</translation>
    </message>
    <message>
        <location line="-227"/>
        <source>ed3k</source>
        <translation>ed3k</translation>
    </message>
    <message>
        <location line="+231"/>
        <source>Reset</source>
        <translation>초기화</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Web results</source>
        <translation>웹 결과</translation>
    </message>
    <message>
        <location line="+12"/>
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
        <location line="+2735"/>
        <source>ed3k download added: %1</source>
        <translation>ed3k 다운로드 추가됨: %1</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>The ed3k download could not be added.</source>
        <translation>ed3k 다운로드를 추가할 수 없습니다.</translation>
    </message>
    <message>
        <source>Min. Bitrate [kbps]:</source>
        <translation type="vanished">최소 비트레이트 [kbps]:</translation>
    </message>
    <message>
        <location line="-2728"/>
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
        <location line="-79"/>
        <source>Show results the server found on this network</source>
        <translation>서버가 이 네트워크에서 찾은 결과 표시</translation>
    </message>
    <message>
        <location line="-214"/>
        <source>Related</source>
        <translation>관련</translation>
    </message>
    <message>
        <location line="+219"/>
        <source>Usenet results</source>
        <translation>Usenet 결과</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Kad results</source>
        <translation>Kad 결과</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Torrent results</source>
        <translation>토렌트 결과</translation>
    </message>
    <message>
        <location line="+48"/>
        <source>Min. Bitrate [Kbit/s]:</source>
        <translation>최소 비트레이트 [Kbit/s]:</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Start</source>
        <translation>시작</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>More</source>
        <translation>더 보기</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Ask the server for further results of this search</source>
        <translation>이 검색의 추가 결과를 서버에 요청</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Cancel</source>
        <translation>취소</translation>
    </message>
    <message>
        <location line="+34"/>
        <location line="+33"/>
        <location line="+204"/>
        <source>Not connected to daemon — search cannot be started.</source>
        <translation>데몬에 연결되어 있지 않습니다 — 검색을 시작할 수 없습니다.</translation>
    </message>
    <message>
        <location line="-224"/>
        <location line="+53"/>
        <source>Search</source>
        <translation>검색</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Kad: &quot;%1&quot; is already being searched — using &quot;%2&quot; as the search target.</source>
        <translation>Kad: &quot;%1&quot;은(는) 이미 검색 중입니다 — &quot;%2&quot;을(를) 검색 대상으로 사용합니다.</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Queued — waiting for a server connection</source>
        <translation>대기 중 — 서버 연결을 기다리는 중</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Queued — waiting for Kad to connect</source>
        <translation>대기 중 — Kad 연결을 기다리는 중</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Queued — waiting for a server or Kad connection</source>
        <translation>대기 중 — 서버 또는 Kad 연결을 기다리는 중</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Queued — waiting for the previous search to finish</source>
        <translation>대기 중 — 이전 검색이 끝나기를 기다리는 중</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Queued — waiting for the server to say what it offers</source>
        <translation>대기 중 — 서버가 제공 항목을 알려 주기를 기다리는 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Queued</source>
        <translation>대기 중</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Search failed: %1</source>
        <translation>검색 실패: %1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>%1 results — scroll down for more</source>
        <translation>결과 %1개 — 아래로 스크롤하면 더 표시</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>%1 of %2 results</source>
        <translation>결과 %2개 중 %1개</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Search &quot;%1&quot; failed: %2</source>
        <translation>검색 &quot;%1&quot; 실패: %2</translation>
    </message>
    <message>
        <location line="+85"/>
        <location line="+184"/>
        <source>Usenet search</source>
        <translation>Usenet 검색</translation>
    </message>
    <message>
        <location line="-91"/>
        <source>%1 results — %2 of %3 indexers</source>
        <translation>결과 %1개 — 인덱서 %3개 중 %2개</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Usenet search: %1</source>
        <translation>Usenet 검색: %1</translation>
    </message>
    <message>
        <location line="-228"/>
        <location line="+232"/>
        <source>No results</source>
        <translation>결과 없음</translation>
    </message>
    <message>
        <location line="+70"/>
        <source>Could not queue &quot;%1&quot;: %2</source>
        <translation>&quot;%1&quot;을(를) 대기열에 추가하지 못했습니다: %2</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Queued &quot;%1&quot; for download from Usenet.</source>
        <translation>&quot;%1&quot;을(를) Usenet 다운로드 대기열에 추가했습니다.</translation>
    </message>
    <message>
        <location line="+97"/>
        <source>Restore Search Parameters</source>
        <translation>검색 매개변수 복원</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Close</source>
        <translation>닫기</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Close Other Tabs</source>
        <translation>다른 탭 닫기</translation>
    </message>
    <message>
        <location line="+98"/>
        <source>&amp;Download</source>
        <translation>다운로드(&amp;D)</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Download &amp;To</source>
        <translation>다음 위치로 다운로드(&amp;T)</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Copy &amp;Name</source>
        <translation>이름 복사(&amp;N)</translation>
    </message>
    <message>
        <location line="+92"/>
        <source>%1 (%2)</source>
        <translation>%1 (%2)</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Paused</source>
        <translation>일시정지됨</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Download NZB File...</source>
        <translation>NZB 파일 다운로드...</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Download Torrent</source>
        <translation>토렌트 다운로드</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Copy Magnet Link</source>
        <translation>마그넷 링크 복사</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Show Links...</source>
        <translation>링크 보기...</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Download Container...</source>
        <translation>컨테이너 다운로드...</translation>
    </message>
    <message>
        <location line="-165"/>
        <location line="+177"/>
        <source>Details...</source>
        <translation>상세...</translation>
    </message>
    <message>
        <location line="-411"/>
        <source>Added &quot;%1&quot; to the Torrents tab.</source>
        <translation>&quot;%1&quot;을(를) Torrents 탭에 추가했습니다.</translation>
    </message>
    <message>
        <location line="+423"/>
        <source>Comments...</source>
        <translation>댓글...</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Copy eD2K Links</source>
        <translation>eD2K 링크 복사</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Copy eD2K Links (HTML)</source>
        <translation>eD2K 링크 복사 (HTML)</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Copy Magnet Links</source>
        <translation>마그넷 링크 복사</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Mark as not Spam</source>
        <translation>스팸 아님으로 표시</translation>
    </message>
    <message>
        <location line="+69"/>
        <location line="+7"/>
        <location line="+1055"/>
        <location line="+43"/>
        <source>Preview</source>
        <translation>미리보기</translation>
    </message>
    <message>
        <location line="-703"/>
        <source>You have already downloaded the following file(s). Download them again?

%1</source>
        <translation>다음 파일은 이미 다운로드했습니다. 다시 다운로드할까요?

%1</translation>
    </message>
    <message>
        <location line="+661"/>
        <source>Preview requested - please wait</source>
        <translation>미리 보기를 요청했습니다 - 잠시 기다려 주세요</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>%1 sent no preview</source>
        <translation>%1 님이 미리 보기를 보내지 않았습니다</translation>
    </message>
    <message>
        <location line="+344"/>
        <source>Asking servers: %1 / %2</source>
        <translation>서버 조회 중: %1 / %2</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>All</source>
        <translation>전체</translation>
    </message>
    <message>
        <location line="+217"/>
        <source>ed3k Filter</source>
        <translation>ed3k 필터</translation>
    </message>
    <message>
        <location line="+30"/>
        <location line="+4"/>
        <location line="+2"/>
        <source>Mark File As</source>
        <translation>파일 표시</translation>
    </message>
    <message>
        <location line="-4"/>
        <source>Fake</source>
        <translation>가짜</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+7"/>
        <source>Spam</source>
        <translation>스팸</translation>
    </message>
    <message>
        <location line="-7"/>
        <source>Harmful</source>
        <translation>유해</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Good</source>
        <translation>양호</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+5"/>
        <source>Clear</source>
        <translation>지우기</translation>
    </message>
    <message>
        <location line="-4"/>
        <location line="+2"/>
        <location line="+2"/>
        <source>Mark Publisher As</source>
        <translation>게시자 표시</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Hide Again</source>
        <translation>다시 숨기기</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+2"/>
        <source>Show Anyway</source>
        <translation>그래도 표시</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Show Rows Hidden by Filter Lists</source>
        <translation>필터 목록으로 숨겨진 행 표시</translation>
    </message>
    <message>
        <location line="-1806"/>
        <location line="+15"/>
        <location line="+10"/>
        <source>Mark as Spam</source>
        <translation>스팸으로 표시</translation>
    </message>
    <message>
        <location line="-140"/>
        <source>Save Torrent File...</source>
        <translation>토렌트 파일 저장...</translation>
    </message>
    <message>
        <location line="+156"/>
        <source>Remove</source>
        <translation>제거</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Close Search Results</source>
        <translation>검색 결과 닫기</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Close All Search Results</source>
        <translation>모든 검색 결과 닫기</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Find...</source>
        <translation>찾기...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Search Related Files</source>
        <translation>관련 파일 검색</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Web Services</source>
        <translation>웹 서비스</translation>
    </message>
    <message>
        <location line="+1060"/>
        <source>Preview not available — web server is not running or stream token not received.</source>
        <translation>미리보기를 사용할 수 없습니다 — 웹 서버가 실행 중이 아니거나 스트림 토큰을 받지 못했습니다.</translation>
    </message>
</context>
<context>
    <name>eMule::SearchResultsModel</name>
    <message>
        <location filename="../src/gui/controls/SearchResultsModel.cpp" line="+41"/>
        <source>Fake</source>
        <translation>가짜</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Harmful</source>
        <translation>유해</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Spam</source>
        <translation>스팸</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Good</source>
        <translation>양호</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>own</source>
        <translation>내 판정</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Hidden: %1</source>
        <translation>숨김: %1</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>your own verdict</source>
        <translation>내 판정</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>ed3k filter list: %1, by %2</source>
        <translation>ed3k 필터 목록: %1, 작성자 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Reason: %1</source>
        <translation>원인: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Lists disagree on this file: marked only</source>
        <translation>이 파일에 대해 목록의 판정이 다릅니다: 표시만</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Hidden by the list&apos;s mode; &quot;Show anyway&quot; in the menu shows it</source>
        <translation>목록 모드에 의해 숨겨짐. 메뉴의 &quot;그래도 표시&quot;로 표시됩니다</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Shown anyway (your choice)</source>
        <translation>그래도 표시됨 (사용자 선택)</translation>
    </message>
    <message>
        <location line="+51"/>
        <source>Yes</source>
        <translation>예</translation>
    </message>
    <message>
        <location line="+270"/>
        <source>Stream</source>
        <translation>스트림</translation>
    </message>
    <message numerus="yes">
        <location line="+106"/>
        <source>%1 · %n name(s)</source>
        <translation>
            <numerusform>%1 · 이름 %n개</numerusform>
        </translation>
    </message>
    <message>
        <location line="+9"/>
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
    <message>
        <location line="+1"/>
        <source>Seen</source>
        <translation>본 적 있음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Confidence</source>
        <translation>신뢰도</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>File ID</source>
        <translation>파일 ID</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Folder</source>
        <translation>폴더</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>AICH Hash</source>
        <translation>AICH 해시</translation>
    </message>
</context>
<context>
    <name>eMule::ServerListModel</name>
    <message>
        <location filename="../src/gui/controls/ServerListModel.cpp" line="+78"/>
        <source>%1 (disabled)</source>
        <translation>%1 (비활성화됨)</translation>
    </message>
    <message>
        <location line="+2"/>
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
        <location line="+100"/>
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
        <source>Files</source>
        <translation>파일</translation>
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
        <source>Soft File Limit</source>
        <translation>소프트 파일 제한</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Country</source>
        <translation>국가</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>IPv6</source>
        <translation>IPv6</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hard File Limit</source>
        <translation>하드 파일 제한</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Version</source>
        <translation>버전</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>High</source>
        <translation>높음</translation>
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
        <source>Soft Files</source>
        <translation type="vanished">소프트 파일</translation>
    </message>
    <message>
        <location line="-42"/>
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
        <location filename="../src/gui/panels/ServerPanel.cpp" line="+267"/>
        <source>Disconnect</source>
        <translation>연결 해제</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Cancel</source>
        <translation>취소</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+435"/>
        <source>Connect</source>
        <translation>연결</translation>
    </message>
    <message>
        <location line="-497"/>
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
        <location line="+623"/>
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
        <location line="-652"/>
        <source>Connect To</source>
        <translation>연결 대상</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Priority</source>
        <translation>우선순위</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Low</source>
        <translation>낮음</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+546"/>
        <source>Normal</source>
        <translation>보통</translation>
    </message>
    <message>
        <location line="-545"/>
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
        <location line="+23"/>
        <source>Enable</source>
        <translation>사용</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Copy eD2K Links</source>
        <translation>eD2K 링크 복사</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Paste eD2K Links</source>
        <translation>eD2K 링크 붙여넣기</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Remove</source>
        <translation>제거</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Remove All</source>
        <translation>모두 제거</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>eNode Account...</source>
        <translation>eNode 계정...</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Find...</source>
        <translation>찾기...</translation>
    </message>
    <message>
        <location line="+55"/>
        <source>▸ Servers (0)</source>
        <translation>▸ 서버 (0)</translation>
    </message>
    <message>
        <location line="+80"/>
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
        <location line="+150"/>
        <source>Name:</source>
        <translation>이름:</translation>
    </message>
    <message>
        <location line="-144"/>
        <source>Add to list</source>
        <translation>목록에 추가</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Update server.met from URL</source>
        <translation>URL에서 server.met 업데이트</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Update server.met from URL:</source>
        <translation>URL에서 server.met 업데이트:</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Update</source>
        <translation>업데이트</translation>
    </message>
    <message>
        <location line="+68"/>
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
    <message numerus="yes">
        <location line="+621"/>
        <source>%n server(s) not added — LAN addresses are filtered</source>
        <translation>
            <numerusform>서버 %n개를 추가하지 않았습니다 — LAN 주소가 필터링됩니다</numerusform>
        </translation>
    </message>
    <message>
        <location line="-667"/>
        <source>Low ID</source>
        <translation>Low ID</translation>
    </message>
    <message>
        <location line="+334"/>
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
        <location line="+49"/>
        <source>server.met processed: %1 servers added, %2 skipped (duplicates/invalid).</source>
        <translation>server.met 처리 완료: %1개 서버 추가, %2개 건너뜀 (중복/잘못됨).</translation>
    </message>
</context>
<context>
    <name>eMule::SharedFilesModel</name>
    <message>
        <location filename="../src/gui/controls/SharedFilesModel.cpp" line="+46"/>
        <source>Yes</source>
        <translation>예</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>No</source>
        <translation>아니오</translation>
    </message>
    <message>
        <location line="+111"/>
        <source>%1 Kbit/s</source>
        <translation>%1 Kbit/s</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>File Name:	%1
ED2K Hash:	%2
Type:	%3
Priority:	%4
Requests:	%5 (%6)
Accepted:	%7 (%8)
Transferred:	%9 (%10)
Complete Sources:	%11
Shared eD2K|Kad:	%12
Folder:	%13</source>
        <translation>파일 이름:	%1
ED2K 해시:	%2
유형:	%3
우선순위:	%4
요청:	%5 (%6)
수락:	%7 (%8)
전송됨:	%9 (%10)
완전한 소스:	%11
공유 eD2K|Kad:	%12
폴더:	%13</translation>
    </message>
    <message>
        <location line="+139"/>
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
        <source>Shared eD2K|Kad</source>
        <translation>공유 eD2K|Kad</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>File ID</source>
        <translation>파일 ID</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Accepted Requests</source>
        <translation>수락된 요청</translation>
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
        <source>Ratio</source>
        <translation>비율</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Session Ratio</source>
        <translation>세션 비율</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Last Request</source>
        <translation>마지막 요청</translation>
    </message>
    <message>
        <source>Shared eD2K/Kad</source>
        <translation type="vanished">공유 eD2K/Kad</translation>
    </message>
    <message>
        <location line="-11"/>
        <source>Folder</source>
        <translation>폴더</translation>
    </message>
</context>
<context>
    <name>eMule::SharedFilesPanel</name>
    <message>
        <location filename="../src/gui/panels/SharedFilesPanel.cpp" line="+192"/>
        <location line="+16"/>
        <location line="+513"/>
        <location line="+647"/>
        <source>Shared Files (0)</source>
        <translation>공유 파일 (0)</translation>
    </message>
    <message>
        <location line="-948"/>
        <source>Open File</source>
        <translation>파일 열기</translation>
    </message>
    <message>
        <location line="+13"/>
        <location line="+1508"/>
        <location line="+504"/>
        <source>Open Folder</source>
        <translation>폴더 열기</translation>
    </message>
    <message>
        <location line="-2000"/>
        <source>Rename...</source>
        <translation>이름 바꾸기...</translation>
    </message>
    <message>
        <location line="+182"/>
        <location line="+1860"/>
        <source>Copy eD2K Links</source>
        <translation>eD2K 링크 복사</translation>
    </message>
    <message>
        <source>Rename File</source>
        <translation type="vanished">파일 이름 변경</translation>
    </message>
    <message>
        <source>New file name:</source>
        <translation type="vanished">새 파일 이름:</translation>
    </message>
    <message>
        <location line="-2022"/>
        <location line="+1984"/>
        <source>Delete From Disk</source>
        <translation>디스크에서 삭제</translation>
    </message>
    <message>
        <location line="-984"/>
        <source>Delete File</source>
        <translation>파일 삭제</translation>
    </message>
    <message>
        <location line="-6"/>
        <source>Are you sure you want to permanently delete &quot;%1&quot; from disk?</source>
        <translation>&quot;%1&quot;을(를) 디스크에서 영구적으로 삭제하시겠습니까?</translation>
    </message>
    <message>
        <location line="-979"/>
        <source>Unshare</source>
        <translation>공유 해제</translation>
    </message>
    <message>
        <location line="+1012"/>
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
        <location line="-995"/>
        <location line="+1964"/>
        <source>Priority (Upload)</source>
        <translation>우선순위(업로드)</translation>
    </message>
    <message>
        <location line="-1949"/>
        <location line="+1966"/>
        <source>Very Low</source>
        <translation>매우 낮음</translation>
    </message>
    <message>
        <location line="-1965"/>
        <location line="+1966"/>
        <source>Low</source>
        <translation>낮음</translation>
    </message>
    <message>
        <location line="-1965"/>
        <location line="+1966"/>
        <source>Normal</source>
        <translation>보통</translation>
    </message>
    <message>
        <location line="-1965"/>
        <location line="+1966"/>
        <source>High</source>
        <translation>높음</translation>
    </message>
    <message>
        <source>Very High</source>
        <translation type="vanished">매우 높음</translation>
    </message>
    <message>
        <location line="-1963"/>
        <location line="+1965"/>
        <source>Auto</source>
        <translation>자동</translation>
    </message>
    <message>
        <location line="-1951"/>
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
        <location line="+19"/>
        <location line="+1871"/>
        <source>Details...</source>
        <translation>상세...</translation>
    </message>
    <message>
        <location line="-1864"/>
        <location line="+1867"/>
        <source>Comments...</source>
        <translation>댓글...</translation>
    </message>
    <message>
        <location line="-1852"/>
        <location line="+1858"/>
        <source>eD2K Links...</source>
        <translation>eD2K 링크...</translation>
    </message>
    <message>
        <location line="-1840"/>
        <source>Find...</source>
        <translation>찾기...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Web Services</source>
        <translation>웹 서비스</translation>
    </message>
    <message>
        <location line="+71"/>
        <source>Reload</source>
        <translation>다시 로드</translation>
    </message>
    <message>
        <source>File Name</source>
        <translation type="vanished">파일 이름</translation>
    </message>
    <message>
        <location line="+33"/>
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
        <location line="+239"/>
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
        <location line="+41"/>
        <source>Include Media Info</source>
        <translation>미디어 정보 포함</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add a line with resolution, codecs, length and bitrate under the HTML link of a complete file</source>
        <translation>완료된 파일의 HTML 링크 아래에 해상도, 코덱, 길이, 비트레이트 줄을 추가합니다</translation>
    </message>
    <message>
        <location line="+259"/>
        <source>%1 (%2 of %3 shared)</source>
        <translation>%1 (%3개 중 %2개 공유됨)</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Could not share that file</source>
        <translation>해당 파일을 공유할 수 없습니다</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Could not unshare that file</source>
        <translation>해당 파일의 공유를 해제할 수 없습니다</translation>
    </message>
    <message>
        <location line="+526"/>
        <source>Share Directory</source>
        <translation>디렉터리 공유</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Share with Subdirectories</source>
        <translation>하위 디렉터리 포함 공유</translation>
    </message>
    <message>
        <location line="+14"/>
        <location line="+516"/>
        <source>Unshare Directory</source>
        <translation>디렉터리 공유 해제</translation>
    </message>
    <message>
        <location line="-507"/>
        <location line="+516"/>
        <source>Unshare with Subdirectories</source>
        <translation>하위 디렉터리 포함 공유 해제</translation>
    </message>
    <message>
        <location line="-420"/>
        <source>Open File not available — web server is not running or stream token not received.</source>
        <translation>파일 열기를 사용할 수 없습니다 — 웹 서버가 실행 중이 아니거나 스트림 토큰을 받지 못했습니다.</translation>
    </message>
    <message>
        <location line="-973"/>
        <source>Content</source>
        <translation>콘텐츠</translation>
    </message>
    <message>
        <location line="+67"/>
        <source>eD2K Links</source>
        <translation>eD2K 링크</translation>
    </message>
    <message>
        <location line="-20"/>
        <source>Copy</source>
        <translation>복사</translation>
    </message>
    <message>
        <location line="-837"/>
        <source>All</source>
        <translation>전체</translation>
    </message>
    <message>
        <location line="+130"/>
        <source>Mass Rename...</source>
        <translation>일괄 이름 변경...</translation>
    </message>
    <message>
        <location line="+58"/>
        <location line="+1966"/>
        <source>Release</source>
        <translation>릴리스</translation>
    </message>
    <message>
        <location line="-1833"/>
        <source>Add To IRC Clipboard</source>
        <translation>IRC 클립보드에 추가</translation>
    </message>
    <message>
        <location line="+94"/>
        <source>Rescan the shared directories. Hold %1 to re-read the media information of all shared files instead.</source>
        <translation>공유 디렉터리를 다시 검색합니다. %1 키를 누른 채 클릭하면 대신 모든 공유 파일의 미디어 정보를 다시 읽습니다.</translation>
    </message>
    <message>
        <location line="+389"/>
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
        <location line="+33"/>
        <location line="+474"/>
        <source>Requires a hostname configured in Preferences, or a public IPv6</source>
        <translation>환경 설정에 구성된 호스트 이름 또는 공용 IPv6가 필요합니다</translation>
    </message>
    <message>
        <location line="-301"/>
        <source>Shared Files (%1)</source>
        <translation>공유 파일 (%1)</translation>
    </message>
    <message numerus="yes">
        <location line="+113"/>
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
        <location line="+156"/>
        <source>Add your hostname or public IPv6 as a source</source>
        <translation>자신의 호스트 이름 또는 공용 IPv6를 소스로 추가</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Showing eD2K links for the first %1 of %2 selected files.</source>
        <translation>선택한 %2개 파일 중 처음 %1개의 eD2K 링크를 표시합니다.</translation>
    </message>
</context>
<context>
    <name>eMule::SimpleCleanupDialog</name>
    <message>
        <location filename="../src/gui/dialogs/SimpleCleanupDialog.cpp" line="+50"/>
        <source>Simple cleanup</source>
        <translation>간단 정리</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Convert &quot;.&quot; to &quot; &quot;</source>
        <translation>&quot;.&quot;를 &quot; &quot;로 변환</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Convert &quot;_&quot; to &quot; &quot;</source>
        <translation>&quot;_&quot;를 &quot; &quot;로 변환</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Convert wrong apostrophes &quot;´&quot;, &quot;`&quot; to standard apostrophes &quot; &apos; &quot;</source>
        <translation>잘못된 아포스트로피 &quot;´&quot;, &quot;`&quot;를 표준 아포스트로피 &quot; &apos; &quot;로 변환</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Convert hexadecimal numbers &quot;%xx&quot; back to characters</source>
        <translation>16진수 &quot;%xx&quot;를 문자로 되돌리기</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Search and Replace:</source>
        <translation>찾기 및 바꾸기:</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Search for:</source>
        <translation>검색어:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Replace by:</source>
        <translation>바꿀 내용:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Case sensitive search</source>
        <translation>대소문자 구분</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Replace characters:</source>
        <translation>문자 바꾸기:</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Replace</source>
        <translation>바꾸기</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>by</source>
        <translation>→</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Search for</source>
        <translation>찾을 내용</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Replace by</source>
        <translation>바꿀 내용</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>&amp;Add</source>
        <translation>추가(&amp;A)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Edit</source>
        <translation>편집(&amp;E)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Delete</source>
        <translation>삭제(&amp;D)</translation>
    </message>
</context>
<context>
    <name>eMule::StatisticsPanel</name>
    <message>
        <location filename="../src/gui/panels/StatisticsPanel.cpp" line="-59"/>
        <location line="+11"/>
        <source>Session average</source>
        <translation>세션 평균</translation>
    </message>
    <message>
        <source>Average (3 min)</source>
        <translation type="vanished">평균 (3분)</translation>
    </message>
    <message>
        <location line="-8"/>
        <location line="+10"/>
        <source>Current</source>
        <translation>현재</translation>
    </message>
    <message>
        <source>KB/s</source>
        <translation type="vanished">KB/s</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Current (excl. overhead)</source>
        <translation>현재(오버헤드 제외)</translation>
    </message>
    <message>
        <source>Friend slots</source>
        <translation type="vanished">친구 슬롯</translation>
    </message>
    <message>
        <source>Active connections</source>
        <translation type="vanished">활성 연결</translation>
    </message>
    <message>
        <location line="+10"/>
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
        <location line="+49"/>
        <source>Transfer</source>
        <translation>전송</translation>
    </message>
    <message>
        <source>Session UL:DL Ratio: -</source>
        <translation type="vanished">세션 UL:DL 비율: -</translation>
    </message>
    <message>
        <source>Friend Session UL:DL Ratio: -</source>
        <translation type="vanished">친구 세션 UL:DL 비율: -</translation>
    </message>
    <message>
        <source>Cumulative UL:DL Ratio: -</source>
        <translation type="vanished">누적 UL:DL 비율: -</translation>
    </message>
    <message>
        <location line="+10"/>
        <location line="+165"/>
        <location line="+25"/>
        <location line="+398"/>
        <location line="+2040"/>
        <source>Uploads</source>
        <translation>업로드</translation>
    </message>
    <message>
        <location line="-2624"/>
        <location line="+67"/>
        <location line="+83"/>
        <location line="+56"/>
        <location line="+43"/>
        <location line="+1782"/>
        <location line="+90"/>
        <location line="+76"/>
        <location line="+129"/>
        <location line="+166"/>
        <location line="+121"/>
        <source>Session</source>
        <translation>세션</translation>
    </message>
    <message>
        <location line="-2610"/>
        <location line="+35"/>
        <source>Uploaded Data: 0 Bytes</source>
        <translation>업로드된 데이터: 0 Bytes</translation>
    </message>
    <message>
        <location line="-22"/>
        <source>Uploaded Data to Friends: 0 Bytes</source>
        <translation>친구에게 업로드된 데이터: 0 Bytes</translation>
    </message>
    <message>
        <source>Active Uploads: 0</source>
        <translation type="vanished">활성 업로드: 0</translation>
    </message>
    <message>
        <source>Waiting Uploads: 0</source>
        <translation type="vanished">대기 중 업로드: 0</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+28"/>
        <location line="+542"/>
        <location line="+141"/>
        <location line="+45"/>
        <source>Upload Sessions</source>
        <translation>업로드 세션</translation>
    </message>
    <message>
        <source>Successful: 0</source>
        <translation type="vanished">성공: 0</translation>
    </message>
    <message>
        <source>Failed: 0</source>
        <translation type="vanished">실패: 0</translation>
    </message>
    <message>
        <location line="-751"/>
        <location line="+28"/>
        <source>Average Upload Per Session: 0 Bytes</source>
        <translation>세션당 평균 업로드: 0 Bytes</translation>
    </message>
    <message>
        <location line="-26"/>
        <location line="+28"/>
        <source>Average Upload Time: 0:00:00</source>
        <translation>평균 업로드 시간: 0:00:00</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+105"/>
        <location line="+24"/>
        <location line="+407"/>
        <location line="+1485"/>
        <location line="+164"/>
        <location line="+380"/>
        <source>Downloads</source>
        <translation>다운로드</translation>
    </message>
    <message>
        <location line="-2558"/>
        <location line="+43"/>
        <source>Downloaded Data: 0 Bytes</source>
        <translation>다운로드된 데이터: 0 Bytes</translation>
    </message>
    <message>
        <source>Active Downloads: 0</source>
        <translation type="vanished">활성 다운로드: 0</translation>
    </message>
    <message>
        <location line="-32"/>
        <source>Found Sources: 0</source>
        <translation>발견된 소스: 0</translation>
    </message>
    <message>
        <location line="+65"/>
        <source>Connection</source>
        <translation>연결</translation>
    </message>
    <message>
        <source>Active Connections: 0</source>
        <translation type="vanished">활성 연결: 0</translation>
    </message>
    <message>
        <source>Peak Connections: 0</source>
        <translation type="vanished">최대 연결: 0</translation>
    </message>
    <message>
        <source>Max Connections Limit Reached: 0</source>
        <translation type="vanished">최대 연결 제한 도달: 0</translation>
    </message>
    <message>
        <source>Reconnects: 0</source>
        <translation type="vanished">재연결: 0</translation>
    </message>
    <message>
        <location line="+53"/>
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
        <location line="+11"/>
        <source>Transfer Time: 0:00:00</source>
        <translation>전송 시간: 0:00:00</translation>
    </message>
    <message>
        <location line="-10"/>
        <location line="+11"/>
        <source>Upload Time: 0:00:00</source>
        <translation>업로드 시간: 0:00:00</translation>
    </message>
    <message>
        <location line="-10"/>
        <location line="+11"/>
        <source>Download Time: 0:00:00</source>
        <translation>다운로드 시간: 0:00:00</translation>
    </message>
    <message>
        <source>Server Duration: 0:00:00</source>
        <translation type="vanished">서버 접속 시간: 0:00:00</translation>
    </message>
    <message>
        <location line="-218"/>
        <location line="+35"/>
        <location line="+32"/>
        <location line="+43"/>
        <location line="+114"/>
        <location line="+346"/>
        <source>Clients</source>
        <translation>클라이언트</translation>
    </message>
    <message>
        <location line="-567"/>
        <location line="+35"/>
        <location line="+32"/>
        <location line="+43"/>
        <source>%1: 0 Bytes</source>
        <translation>%1: 0 Bytes</translation>
    </message>
    <message>
        <location line="-109"/>
        <location line="+35"/>
        <location line="+32"/>
        <location line="+43"/>
        <location line="+119"/>
        <location line="+341"/>
        <source>Port</source>
        <translation>포트</translation>
    </message>
    <message>
        <location line="-569"/>
        <location line="+35"/>
        <location line="+32"/>
        <location line="+43"/>
        <source>Default Port 4662: 0 Bytes</source>
        <translation>기본 포트 4662: 0 Bytes</translation>
    </message>
    <message>
        <location line="-109"/>
        <location line="+35"/>
        <location line="+32"/>
        <location line="+43"/>
        <source>Other Ports: 0 Bytes</source>
        <translation>기타 포트: 0 Bytes</translation>
    </message>
    <message>
        <location line="-109"/>
        <location line="+35"/>
        <location line="+543"/>
        <source>Data Source</source>
        <translation>데이터 소스</translation>
    </message>
    <message>
        <location line="-577"/>
        <location line="+35"/>
        <source>Complete File: 0 Bytes</source>
        <translation>완전한 파일: 0 Bytes</translation>
    </message>
    <message>
        <location line="-34"/>
        <location line="+35"/>
        <source>Part File: 0 Bytes</source>
        <translation>부분 파일: 0 Bytes</translation>
    </message>
    <message>
        <location line="-13"/>
        <location line="+75"/>
        <location line="+66"/>
        <location line="+41"/>
        <location line="+32"/>
        <location line="+1787"/>
        <location line="+90"/>
        <location line="+76"/>
        <location line="+129"/>
        <location line="+162"/>
        <location line="+125"/>
        <source>Cumulative</source>
        <translation>누적</translation>
    </message>
    <message>
        <location line="-2539"/>
        <location line="+43"/>
        <source>Completed Downloads: 0</source>
        <translation>완료된 다운로드: 0</translation>
    </message>
    <message>
        <location line="-32"/>
        <location line="+34"/>
        <location line="+481"/>
        <location line="+270"/>
        <location line="+10"/>
        <source>Download Sessions</source>
        <translation>다운로드 세션</translation>
    </message>
    <message>
        <location line="-790"/>
        <location line="+34"/>
        <source>Average Download Per Session: 0 Bytes</source>
        <translation>세션당 평균 다운로드: 0 Bytes</translation>
    </message>
    <message>
        <location line="-32"/>
        <location line="+34"/>
        <source>Average Download Time: 0:00:00</source>
        <translation>평균 다운로드 시간: 0:00:00</translation>
    </message>
    <message>
        <location line="-31"/>
        <location line="+34"/>
        <source>Gain Due To Compression: 0 Bytes (0.0%)</source>
        <translation>압축으로 인한 이득: 0 Bytes (0.0%)</translation>
    </message>
    <message>
        <location line="-32"/>
        <location line="+34"/>
        <source>Lost Due To Corruption: 0 Bytes (0.0%)</source>
        <translation>손상으로 인한 손실: 0 Bytes (0.0%)</translation>
    </message>
    <message>
        <location line="-32"/>
        <location line="+34"/>
        <source>Parts Saved Due To ICH: 0</source>
        <translation>ICH로 복구된 파트: 0</translation>
    </message>
    <message>
        <location line="+15"/>
        <location line="+26"/>
        <location line="+1796"/>
        <location line="+87"/>
        <location line="+86"/>
        <location line="+79"/>
        <source>General</source>
        <translation>일반</translation>
    </message>
    <message>
        <source>Average Connections: 0.0</source>
        <translation type="vanished">평균 연결 수: 0.0</translation>
    </message>
    <message>
        <location line="-2064"/>
        <source>Upload Speed: 0 KB/s</source>
        <translation>업로드 속도: 0 KB/s</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+24"/>
        <source>Max Upload Rate: 0 KB/s</source>
        <translation>최대 업로드 속도: 0 KB/s</translation>
    </message>
    <message>
        <location line="-23"/>
        <location line="+24"/>
        <source>Max Average Upload Rate: 0 KB/s</source>
        <translation>최대 평균 업로드 속도: 0 KB/s</translation>
    </message>
    <message>
        <location line="-20"/>
        <source>Download Speed: 0 KB/s</source>
        <translation>다운로드 속도: 0 KB/s</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+23"/>
        <source>Max Download Rate: 0 KB/s</source>
        <translation>최대 다운로드 속도: 0 KB/s</translation>
    </message>
    <message>
        <location line="-22"/>
        <location line="+23"/>
        <source>Max Average Download Rate: 0 KB/s</source>
        <translation>최대 평균 다운로드 속도: 0 KB/s</translation>
    </message>
    <message>
        <source>Server Reconnects: 0</source>
        <translation type="vanished">서버 재연결: 0</translation>
    </message>
    <message>
        <source>Connection Limit Reached: 0</source>
        <translation type="vanished">연결 한도 도달: 0</translation>
    </message>
    <message>
        <location line="-8"/>
        <source>Average Upload Rate: 0 KB/s</source>
        <translation>평균 업로드 속도: 0 KB/s</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Average Download Rate: 0 KB/s</source>
        <translation>평균 다운로드 속도: 0 KB/s</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+805"/>
        <location line="+5"/>
        <source>Statistics Last Reset: %1</source>
        <translation>통계 마지막 초기화: %1</translation>
    </message>
    <message>
        <location line="-1126"/>
        <location line="+316"/>
        <location line="+636"/>
        <location line="+167"/>
        <location line="+8"/>
        <location line="+67"/>
        <location line="+69"/>
        <location line="+284"/>
        <location line="+1104"/>
        <source>Unknown</source>
        <translation>알 수 없음</translation>
    </message>
    <message>
        <location line="-2658"/>
        <source>Statistics Tree</source>
        <translation>통계 트리</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+1130"/>
        <source>Statistics last reset: %1</source>
        <translation>통계 마지막 초기화: %1</translation>
    </message>
    <message>
        <location line="-1101"/>
        <location line="+2408"/>
        <source>Usenet</source>
        <translation>Usenet</translation>
    </message>
    <message>
        <location line="-2334"/>
        <location line="+747"/>
        <location line="+45"/>
        <location line="+1168"/>
        <source>Waiting...</source>
        <translation>대기 중...</translation>
    </message>
    <message>
        <location line="-1957"/>
        <location line="+682"/>
        <source>Session UL:DL Ratio (Friends UL excluded): %1</source>
        <translation>세션 UL:DL 비율 (친구 업로드 제외): %1</translation>
    </message>
    <message>
        <location line="-587"/>
        <source>UDP File Re-asks: 0, Failed: 0 (0.0%)</source>
        <translation>UDP 파일 재요청: 0, 실패: 0 (0.0%)</translation>
    </message>
    <message>
        <location line="+2158"/>
        <source>Corrupt (Failed yEnc Check): %1</source>
        <translation>손상됨 (yEnc 검사 실패): %1</translation>
    </message>
    <message>
        <location line="+363"/>
        <source>HTTP Cache</source>
        <translation>HTTP 캐시</translation>
    </message>
    <message>
        <source>Published: 0 Bytes</source>
        <translation type="vanished">게시됨: 0 Bytes</translation>
    </message>
    <message>
        <source>Fetched: 0 Bytes</source>
        <translation type="vanished">가져옴: 0 Bytes</translation>
    </message>
    <message>
        <source>Upload Saved: 0 Bytes</source>
        <translation type="vanished">절약한 업로드: 0 Bytes</translation>
    </message>
    <message>
        <source>Chunks Published: 0</source>
        <translation type="vanished">게시한 청크: 0</translation>
    </message>
    <message>
        <source>Chunks Fetched: 0</source>
        <translation type="vanished">가져온 청크: 0</translation>
    </message>
    <message>
        <location line="-2389"/>
        <source>Run Time: 0:00:00</source>
        <translation>실행 시간: 0:00:00</translation>
    </message>
    <message>
        <location line="-4"/>
        <location line="+8"/>
        <source>Total Server Duration: 0:00:00</source>
        <translation>총 서버 지속 시간: 0:00:00</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Known Clients: 0</source>
        <translation>알려진 클라이언트: 0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Client Software</source>
        <translation>클라이언트 소프트웨어</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Low ID: 0 (0.0%)</source>
        <translation>Low ID: 0 (0.0%)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Banned Clients: 0</source>
        <translation>차단된 클라이언트: 0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Filtered Clients: 0</source>
        <translation>필터된 클라이언트: 0</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Servers</source>
        <translation>서버</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Working Servers: 0</source>
        <translation>작동 중인 서버: 0</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Failed Servers: 0</source>
        <translation>실패한 서버: 0</translation>
    </message>
    <message>
        <location line="+2"/>
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
        <source>Low ID Users: 0</source>
        <translation type="vanished">Low ID 사용자: 0</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+14"/>
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
        <source>Number of Shared Files: 0</source>
        <translation type="vanished">공유 파일 수: 0</translation>
    </message>
    <message>
        <source>Total Size: 0 Bytes</source>
        <translation type="vanished">총 크기: 0 Bytes</translation>
    </message>
    <message>
        <source>Average File Size: 0 Bytes</source>
        <translation type="vanished">평균 파일 크기: 0 Bytes</translation>
    </message>
    <message>
        <source>Largest Shared File: 0 Bytes</source>
        <translation type="vanished">최대 공유 파일: 0 Bytes</translation>
    </message>
    <message>
        <source>Most Files Shared: 0</source>
        <translation type="vanished">최다 공유 파일: 0</translation>
    </message>
    <message>
        <source>Largest Share Size: 0 Bytes</source>
        <translation type="vanished">최대 공유 크기: 0 Bytes</translation>
    </message>
    <message>
        <source>Largest Average File Size: 0 Bytes</source>
        <translation type="vanished">최대 평균 파일 크기: 0 Bytes</translation>
    </message>
    <message>
        <source>Largest File Size: 0 Bytes</source>
        <translation type="vanished">최대 파일 크기: 0 Bytes</translation>
    </message>
    <message>
        <source>Total Downloads</source>
        <translation type="vanished">전체 다운로드</translation>
    </message>
    <message>
        <source>Number of Downloads: 0</source>
        <translation type="vanished">다운로드 수: 0</translation>
    </message>
    <message>
        <source>Total Size of Downloads: 0 Bytes</source>
        <translation type="vanished">다운로드 총 크기: 0 Bytes</translation>
    </message>
    <message>
        <source>Total Size Downloaded: 0 Bytes</source>
        <translation type="vanished">다운로드된 총 크기: 0 Bytes</translation>
    </message>
    <message>
        <source>Total Size Left to Download: 0 Bytes</source>
        <translation type="vanished">남은 다운로드 크기: 0 Bytes</translation>
    </message>
    <message>
        <source>Free Space on Drive: 0 Bytes</source>
        <translation type="vanished">드라이브 여유 공간: 0 Bytes</translation>
    </message>
    <message>
        <location line="-297"/>
        <location line="+682"/>
        <source>Session UL:DL Ratio: %1</source>
        <translation>세션 UL:DL 비율: %1</translation>
    </message>
    <message>
        <source>Friend Session UL:DL Ratio: %1</source>
        <translation type="vanished">친구 세션 UL:DL 비율: %1</translation>
    </message>
    <message>
        <location line="-679"/>
        <location line="+683"/>
        <source>Cumulative UL:DL Ratio: %1</source>
        <translation>누적 UL:DL 비율: %1</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+74"/>
        <location line="+1237"/>
        <source>Uploaded Data: %1</source>
        <translation>업로드된 데이터: %1</translation>
    </message>
    <message>
        <location line="-1300"/>
        <location line="+73"/>
        <location line="+57"/>
        <location line="+99"/>
        <source>Default Port 4662: %1 %2</source>
        <translation>기본 포트 4662: %1 %2</translation>
    </message>
    <message>
        <location line="-226"/>
        <location line="+73"/>
        <location line="+57"/>
        <location line="+99"/>
        <source>Other Ports: %1 %2</source>
        <translation>기타 포트: %1 %2</translation>
    </message>
    <message>
        <location line="-226"/>
        <location line="+73"/>
        <source>Complete File: %1 %2</source>
        <translation>완전한 파일: %1 %2</translation>
    </message>
    <message>
        <location line="-70"/>
        <location line="+73"/>
        <source>Part File: %1 %2</source>
        <translation>부분 파일: %1 %2</translation>
    </message>
    <message>
        <location line="-68"/>
        <source>Uploaded Data to Friends: %1</source>
        <translation>친구에게 업로드된 데이터: %1</translation>
    </message>
    <message>
        <source>Active Uploads: %1</source>
        <translation type="vanished">활성 업로드: %1</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Waiting Uploads: %1</source>
        <translation>대기 중 업로드: %1</translation>
    </message>
    <message>
        <source>Successful: %1%2</source>
        <translation type="vanished">성공: %1%2</translation>
    </message>
    <message>
        <location line="+1550"/>
        <location line="+49"/>
        <source>Failed: %1</source>
        <translation>실패: %1</translation>
    </message>
    <message>
        <location line="-1575"/>
        <location line="+45"/>
        <source>Average Upload Per Session: %1</source>
        <translation>세션당 평균 업로드: %1</translation>
    </message>
    <message>
        <location line="-42"/>
        <location line="+45"/>
        <source>Average Upload Time: %1</source>
        <translation>평균 업로드 시간: %1</translation>
    </message>
    <message>
        <location line="-41"/>
        <source>%1: %2</source>
        <translation>%1: %2</translation>
    </message>
    <message>
        <location line="-186"/>
        <location line="+190"/>
        <location line="+57"/>
        <location line="+99"/>
        <location line="+36"/>
        <source>Total Overhead (Packets)</source>
        <translation>전체 오버헤드 (패킷)</translation>
    </message>
    <message>
        <location line="-381"/>
        <location line="+190"/>
        <location line="+57"/>
        <location line="+99"/>
        <location line="+36"/>
        <source>File Request Overhead (Packets)</source>
        <translation>파일 요청 오버헤드 (패킷)</translation>
    </message>
    <message>
        <location line="-381"/>
        <location line="+190"/>
        <location line="+57"/>
        <location line="+99"/>
        <location line="+36"/>
        <source>Source Exchange Overhead (Packets)</source>
        <translation>소스 교환 오버헤드 (패킷)</translation>
    </message>
    <message>
        <location line="-381"/>
        <location line="+190"/>
        <location line="+57"/>
        <location line="+99"/>
        <location line="+36"/>
        <source>Server Overhead (Packets)</source>
        <translation>서버 오버헤드 (패킷)</translation>
    </message>
    <message>
        <location line="-381"/>
        <location line="+190"/>
        <location line="+57"/>
        <location line="+99"/>
        <location line="+36"/>
        <source>Kad Overhead (Packets)</source>
        <translation>Kad 오버헤드 (패킷)</translation>
    </message>
    <message>
        <source>Published</source>
        <translation type="vanished">게시됨</translation>
    </message>
    <message>
        <source>Fetched</source>
        <translation type="vanished">가져옴</translation>
    </message>
    <message>
        <source>Upload Saved</source>
        <translation type="vanished">절약한 업로드</translation>
    </message>
    <message>
        <source>Chunks Published</source>
        <translation type="vanished">게시한 청크</translation>
    </message>
    <message>
        <source>Chunks Fetched</source>
        <translation type="vanished">가져온 청크</translation>
    </message>
    <message>
        <location line="-132"/>
        <location line="+99"/>
        <location line="+1080"/>
        <location line="+80"/>
        <location line="+166"/>
        <source>Downloaded Data: %1</source>
        <translation>다운로드된 데이터: %1</translation>
    </message>
    <message>
        <source>Active Downloads: %1</source>
        <translation type="vanished">활성 다운로드: %1</translation>
    </message>
    <message>
        <location line="-1404"/>
        <source>Found Sources: %1</source>
        <translation>발견된 소스: %1</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>UDP File Re-asks: %1, Failed: %2 %3</source>
        <translation>UDP 파일 재요청: %1, 실패: %2 %3</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+61"/>
        <location line="+1074"/>
        <location line="+84"/>
        <source>Completed Downloads: %1</source>
        <translation>완료된 다운로드: %1</translation>
    </message>
    <message>
        <location line="-1189"/>
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
        <location line="+1297"/>
        <source>Active Connections: %1</source>
        <translation>활성 연결: %1</translation>
    </message>
    <message>
        <location line="-75"/>
        <location line="+76"/>
        <source>Peak Connections: %1</source>
        <translation>최대 연결: %1</translation>
    </message>
    <message>
        <source>Max Connections Limit Reached: %1</source>
        <translation type="vanished">최대 연결 제한 도달: %1</translation>
    </message>
    <message>
        <location line="-2079"/>
        <location line="+26"/>
        <location line="+770"/>
        <location line="+33"/>
        <source>Reconnects: %1</source>
        <translation>재연결: %1</translation>
    </message>
    <message>
        <source>Average Connections: %1</source>
        <translation type="vanished">평균 연결: %1</translation>
    </message>
    <message>
        <location line="-9"/>
        <location line="+1002"/>
        <location line="+175"/>
        <source>Upload Speed: %1</source>
        <translation>업로드 속도: %1</translation>
    </message>
    <message>
        <location line="-1176"/>
        <location line="+17"/>
        <location line="+988"/>
        <location line="+173"/>
        <source>Max Upload Rate: %1</source>
        <translation>최대 업로드 속도: %1</translation>
    </message>
    <message>
        <location line="-1177"/>
        <location line="+17"/>
        <source>Max Average Upload Rate: %1</source>
        <translation>최대 평균 업로드 속도: %1</translation>
    </message>
    <message>
        <location line="-16"/>
        <location line="+998"/>
        <location line="+87"/>
        <location line="+86"/>
        <location line="+79"/>
        <source>Download Speed: %1</source>
        <translation>다운로드 속도: %1</translation>
    </message>
    <message>
        <location line="-1249"/>
        <location line="+17"/>
        <location line="+983"/>
        <location line="+86"/>
        <location line="+86"/>
        <location line="+79"/>
        <source>Max Download Rate: %1</source>
        <translation>최대 다운로드 속도: %1</translation>
    </message>
    <message>
        <location line="-1250"/>
        <location line="+17"/>
        <source>Max Average Download Rate: %1</source>
        <translation>최대 평균 다운로드 속도: %1</translation>
    </message>
    <message>
        <source>Server Reconnects: %1</source>
        <translation type="vanished">서버 재연결: %1</translation>
    </message>
    <message>
        <source>Connection Limit Reached: %1</source>
        <translation type="vanished">연결 한도 도달: %1</translation>
    </message>
    <message>
        <location line="-5"/>
        <location line="+988"/>
        <location line="+173"/>
        <source>Average Upload Rate: %1</source>
        <translation>평균 업로드 속도: %1</translation>
    </message>
    <message>
        <location line="-1158"/>
        <location line="+983"/>
        <location line="+86"/>
        <location line="+86"/>
        <location line="+79"/>
        <source>Average Download Rate: %1</source>
        <translation>평균 다운로드 속도: %1</translation>
    </message>
    <message>
        <location line="-1221"/>
        <location line="+5"/>
        <source>Time Since Last Reset: %1</source>
        <translation>마지막 초기화 이후 시간: %1</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Runtime: %1</source>
        <translation>실행 시간: %1</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+20"/>
        <source>Transfer Time: %1 %2</source>
        <translation>전송 시간: %1 %2</translation>
    </message>
    <message>
        <location line="-18"/>
        <location line="+20"/>
        <location line="+930"/>
        <location line="+173"/>
        <source>Upload Time: %1 %2</source>
        <translation>업로드 시간: %1 %2</translation>
    </message>
    <message>
        <location line="-1121"/>
        <location line="+20"/>
        <location line="+927"/>
        <location line="+84"/>
        <location line="+89"/>
        <location line="+76"/>
        <source>Download Time: %1 %2</source>
        <translation>다운로드 시간: %1 %2</translation>
    </message>
    <message>
        <source>Server Duration: %1 %2</source>
        <translation type="vanished">서버 접속 시간: %1 %2</translation>
    </message>
    <message>
        <location line="-1182"/>
        <source>Run Time: %1</source>
        <translation>실행 시간: %1</translation>
    </message>
    <message>
        <location line="-9"/>
        <location line="+17"/>
        <source>Total Server Duration: %1 %2</source>
        <translation>총 서버 지속 시간: %1 %2</translation>
    </message>
    <message>
        <location line="-843"/>
        <source>Current Server Duration: 0:00:00</source>
        <translation>현재 서버 접속 시간: 0:00:00</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Different clients, counted by user hash. An estimate, accurate to about 2%.</source>
        <translation>사용자 해시로 센 서로 다른 클라이언트. 오차 약 2%의 추정치입니다.</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Clients that said hello on a connection.</source>
        <translation>연결에서 hello를 보낸 클라이언트.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Clients that proved their user hash with Secure Identification.</source>
        <translation>보안 식별로 사용자 해시를 증명한 클라이언트.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Clients by the country of the address they connected from.</source>
        <translation>접속한 주소의 국가별 클라이언트.</translation>
    </message>
    <message>
        <location line="+616"/>
        <location line="+10"/>
        <source>Average Download Per Session: %1</source>
        <translation>세션당 평균 다운로드: %1</translation>
    </message>
    <message>
        <location line="-8"/>
        <location line="+10"/>
        <source>Average Download Time: %1</source>
        <translation>평균 다운로드 시간: %1</translation>
    </message>
    <message>
        <location line="+148"/>
        <source>Current Server Duration: %1 %2</source>
        <translation>현재 서버 접속 시간: %1 %2</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Known Clients: %1</source>
        <translation>알려진 클라이언트: %1</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Low ID: %1 %2</source>
        <translation>Low ID: %1 %2</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>Banned Clients: %1</source>
        <translation>차단된 클라이언트: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Filtered Clients: %1</source>
        <translation>필터된 클라이언트: %1</translation>
    </message>
    <message>
        <location line="+127"/>
        <source>Working Servers: %1</source>
        <translation>작동 중인 서버: %1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Failed Servers: %1</source>
        <translation>실패한 서버: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Total: %1</source>
        <translation>합계: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Users: %1</source>
        <translation>총 사용자: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Files: %1</source>
        <translation>총 파일: %1</translation>
    </message>
    <message>
        <source>Low ID Users: %1</source>
        <translation type="vanished">Low ID 사용자: %1</translation>
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
        <location line="-962"/>
        <location line="+970"/>
        <source>Number of Shared Files: %1</source>
        <translation>공유 파일 수: %1</translation>
    </message>
    <message>
        <source>Total Size: %1</source>
        <translation type="vanished">총 크기: %1</translation>
    </message>
    <message>
        <source>Average File Size: %1</source>
        <translation type="vanished">평균 파일 크기: %1</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+9"/>
        <source>Largest Shared File: %1</source>
        <translation>최대 공유 파일: %1</translation>
    </message>
    <message>
        <source>Most Files Shared: %1</source>
        <translation type="vanished">최다 공유 파일: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Largest Share Size: %1</source>
        <translation>최대 공유 크기: %1</translation>
    </message>
    <message>
        <location line="-4"/>
        <source>Largest Average File Size: %1</source>
        <translation>최대 평균 파일 크기: %1</translation>
    </message>
    <message>
        <source>Largest File Size: %1</source>
        <translation type="vanished">최대 파일 크기: %1</translation>
    </message>
    <message>
        <location line="+14"/>
        <location line="+1016"/>
        <source>Number of Downloads: %1</source>
        <translation>다운로드 수: %1</translation>
    </message>
    <message>
        <location line="-1015"/>
        <location line="+729"/>
        <location line="+90"/>
        <location line="+204"/>
        <source>Total Size of Downloads: %1</source>
        <translation>다운로드 총 크기: %1</translation>
    </message>
    <message>
        <location line="-293"/>
        <location line="+90"/>
        <location line="+204"/>
        <source>Total Size Downloaded: %1</source>
        <translation>다운로드된 총 크기: %1</translation>
    </message>
    <message>
        <location line="-293"/>
        <location line="+90"/>
        <location line="+204"/>
        <source>Total Size Left to Download: %1</source>
        <translation>남은 다운로드 크기: %1</translation>
    </message>
    <message>
        <source>Free Space on Drive: %1</source>
        <translation type="vanished">드라이브 여유 공간: %1</translation>
    </message>
    <message>
        <location line="-801"/>
        <source>Clients Seen: ≈%1</source>
        <translation>확인한 클라이언트: ≈%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Identified: ≈%1 %2</source>
        <translation>식별됨: ≈%1 %2</translation>
    </message>
    <message>
        <location line="+51"/>
        <source>Not running</source>
        <translation>실행 중 아님</translation>
    </message>
    <message>
        <location line="-730"/>
        <location line="+732"/>
        <source>Connecting</source>
        <translation>연결 중</translation>
    </message>
    <message>
        <location line="-1767"/>
        <location line="+111"/>
        <source>Average (%1 mins)</source>
        <translation>평균 (%1분)</translation>
    </message>
    <message>
        <location line="-86"/>
        <location line="+107"/>
        <source>Active connections (1:%1)</source>
        <translation>활성 연결 (1:%1)</translation>
    </message>
    <message>
        <location line="-15"/>
        <source>Download Speed</source>
        <translation>다운로드 속도</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Friend upload</source>
        <translation>친구 업로드</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Upload Speed</source>
        <translation>업로드 속도</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Connections</source>
        <translation>연결 수</translation>
    </message>
    <message>
        <location line="+364"/>
        <source>Disk Space</source>
        <translation>디스크 공간</translation>
    </message>
    <message>
        <location line="+231"/>
        <source>Projected Averages</source>
        <translation>예상 평균</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Daily</source>
        <translation>매일</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Monthly</source>
        <translation>월간</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Yearly</source>
        <translation>연간</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Default Port 4662</source>
        <translation>기본 포트 4662</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Other Ports</source>
        <translation>기타 포트</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Uploaded Data</source>
        <translation>업로드한 데이터</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Complete File</source>
        <translation>완전한 파일</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Part File</source>
        <translation>부분 파일</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+13"/>
        <source>Successful</source>
        <translation>성공</translation>
    </message>
    <message>
        <location line="-12"/>
        <location line="+13"/>
        <source>Failed</source>
        <translation>실패</translation>
    </message>
    <message>
        <location line="-6"/>
        <source>Downloaded Data</source>
        <translation>다운로드한 데이터</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Completed Downloads</source>
        <translation>완료된 다운로드</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Gained Due To Compression</source>
        <translation>압축으로 절약</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Lost Due To Corruption</source>
        <translation>손상으로 손실</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Parts Saved Due To ICH</source>
        <translation>ICH로 복구한 파트</translation>
    </message>
    <message>
        <location line="+96"/>
        <source>Active Uploads/Needed to fill Bandwidth: %1</source>
        <translation>활성 업로드/대역폭을 채우는 데 필요한 수: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Total Uploads: %1</source>
        <translation>전체 업로드: %1</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Total successful upload sessions</source>
        <translation>성공한 업로드 세션 합계</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total failed upload sessions</source>
        <translation>실패한 업로드 세션 합계</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Successful Download Sessions</source>
        <translation>성공한 다운로드 세션</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Failed Download Sessions</source>
        <translation>실패한 다운로드 세션</translation>
    </message>
    <message>
        <location line="+99"/>
        <source>Active Downloads (chunks): %1</source>
        <translation>활성 다운로드 (청크): %1</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>On Queue</source>
        <translation>대기 중</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Queue Full</source>
        <translation>대기열 가득 참</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>No needed parts</source>
        <translation>필요한 파트 없음</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Asking</source>
        <translation>요청 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Receiving hashset</source>
        <translation>해시셋 수신 중</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Connecting via server</source>
        <translation>서버를 통해 연결 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Too many connections</source>
        <translation>연결이 너무 많음</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Cannot connect LowID to LowID</source>
        <translation>LowID끼리 연결할 수 없음</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+261"/>
        <source>Problematic</source>
        <translation>문제 있음</translation>
    </message>
    <message>
        <location line="-261"/>
        <source>Banned</source>
        <translation>차단됨</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Asked for another file</source>
        <translation>다른 파일 요청함</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>via eD2K Server</source>
        <translation>eD2K 서버 경유</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>via Kad</source>
        <translation>Kad 경유</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>via Source Exchange</source>
        <translation>소스 교환 경유</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>via Passive</source>
        <translation>패시브 경유</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Dead Sources: %1 (%2 + %3)</source>
        <translation>죽은 소스: %1 (%2 + %3)</translation>
    </message>
    <message>
        <location line="+100"/>
        <source>Active Connections (estimate): %1 (Half:%2 | Compl:%3 | Other:%4)</source>
        <translation>활성 연결 (추정): %1 (하프:%2 | 완료:%3 | 기타:%4)</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+30"/>
        <source>Average Connections (estimate): %1</source>
        <translation>평균 연결 수 (추정): %1</translation>
    </message>
    <message>
        <location line="-28"/>
        <location line="+30"/>
        <source>Peak Connections (estimate): %1</source>
        <translation>최대 연결 수 (추정): %1</translation>
    </message>
    <message>
        <location line="-24"/>
        <source>Max Connection Limit Reached: %1 : %2</source>
        <translation>최대 연결 제한 도달: %1 : %2</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+22"/>
        <source>Max Connection Limit Reached: %1</source>
        <translation>최대 연결 제한 도달: %1</translation>
    </message>
    <message>
        <location line="-19"/>
        <source>Average Uploadrate: %1</source>
        <translation>평균 업로드 속도: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Average Downloadrate: %1</source>
        <translation>평균 다운로드 속도: %1</translation>
    </message>
    <message>
        <location line="+116"/>
        <source>Default</source>
        <translation>기본값</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Other</source>
        <translation>기타</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Secure Ident (OK : Failed )</source>
        <translation>보안 식별 (성공 : 실패 )</translation>
    </message>
    <message>
        <location line="+93"/>
        <source>Minor</source>
        <translation>기타 버전</translation>
    </message>
    <message>
        <location line="+44"/>
        <source>Users on Working Servers: %1; Low ID: %2 (%3%)</source>
        <translation>작동 중인 서버의 사용자: %1; Low ID: %2 (%3%)</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Files on Working Servers: %1</source>
        <translation>작동 중인 서버의 파일: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server Occupation: %1%</source>
        <translation>서버 점유율: %1%</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Deleted Servers: %1</source>
        <translation>삭제된 서버: %1</translation>
    </message>
    <message>
        <location line="+20"/>
        <source> (%1 hashing)</source>
        <translation> (%1개 해싱 중)</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Average file size: %1</source>
        <translation>평균 파일 크기: %1</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Total size of Shared Files: %1</source>
        <translation>공유 파일의 전체 크기: %1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Max. Files Ever Shared: %1</source>
        <translation>역대 최대 공유 파일 수: %1</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Total Completed Size: %1 (%2%)</source>
        <translation>완료된 전체 크기: %1 (%2%)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Total Size Left to Transfer: %1</source>
        <translation>전송할 남은 전체 크기: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Free Space on Tempdrive: %1</source>
        <translation>임시 드라이브의 여유 공간: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source> (you need to free %1!)</source>
        <translation> (%1을(를) 확보해야 합니다!)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Additional Space Needed for Downloads: %1</source>
        <translation>다운로드에 필요한 추가 공간: %1</translation>
    </message>
    <message>
        <location line="+125"/>
        <source>Status: Running on UDP port %1</source>
        <translation>상태: UDP 포트 %1에서 실행 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status: Not running</source>
        <translation>상태: 실행 중 아님</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Node ID: %1</source>
        <translation>노드 ID: %1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Open</source>
        <translation>개방</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Cone NAT</source>
        <translation>콘 NAT</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Symmetric NAT</source>
        <translation>대칭형 NAT</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Address Class: IPv4 %1, IPv6 %2</source>
        <translation>주소 클래스: IPv4 %1, IPv6 %2</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Router: %1</source>
        <translation>라우터: %1</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Yes</source>
        <translation>예</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>No</source>
        <translation>아니오</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>%1.%2</source>
        <translation>%1.%2</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>In use</source>
        <translation>사용 중</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Not configured</source>
        <translation>설정되지 않음</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Off</source>
        <translation>꺼짐</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Credit Address: %1</source>
        <translation>크레딧 주소: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Balance: %1</source>
        <translation>잔액: %1</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>%1 credits</source>
        <translation>%1 크레딧</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Account: %1</source>
        <translation>계정: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Overdrawn</source>
        <translation>초과 인출</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Mature</source>
        <translation>성숙됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not mature</source>
        <translation>성숙되지 않음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Receipts Signed: %1 (%2)</source>
        <translation>서명한 영수증: %1 (%2)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Receipts Received: %1 (%2), priority turns given: %3</source>
        <translation>받은 영수증: %1 (%2), 제공한 우선 차례: %3</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Stake: %1, claims waiting: %2</source>
        <translation>스테이크: %1, 대기 중인 청구: %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1 E3S per credit</source>
        <translation>크레딧당 %1 E3S</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>burning off</source>
        <translation>소각 꺼짐</translation>
    </message>
    <message>
        <location line="+75"/>
        <source>Connected (LAN mode)</source>
        <translation>연결됨 (LAN 모드)</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Connected, TCP and UDP firewalled</source>
        <translation>연결됨, TCP 및 UDP 방화벽 뒤</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Connected, TCP firewalled</source>
        <translation>연결됨, TCP 방화벽 뒤</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Connected, UDP firewalled</source>
        <translation>연결됨, UDP 방화벽 뒤</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Connected, open</source>
        <translation>연결됨, 열림</translation>
    </message>
    <message>
        <location line="-95"/>
        <location line="+97"/>
        <source>Status: %1</source>
        <translation>상태: %1</translation>
    </message>
    <message>
        <location line="+25"/>
        <location line="+49"/>
        <source>Reset Statistics</source>
        <translation>통계 초기화</translation>
    </message>
    <message>
        <location line="-48"/>
        <location line="+68"/>
        <source>Restore Statistics</source>
        <translation>통계 복원</translation>
    </message>
    <message>
        <location line="-60"/>
        <source>Expand Main Sections</source>
        <translation>주요 섹션 펼치기</translation>
    </message>
    <message>
        <location line="+1"/>
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
        <location line="+9"/>
        <source>Copy Branch</source>
        <translation>분기 복사</translation>
    </message>
    <message>
        <location line="-8"/>
        <location line="+9"/>
        <source>Copy All Visible</source>
        <translation>보이는 모든 항목 복사</translation>
    </message>
    <message>
        <location line="-8"/>
        <location line="+9"/>
        <source>Copy All Statistics</source>
        <translation>모든 통계 복사</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>HTML Features</source>
        <translation>HTML 기능</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+80"/>
        <location line="+7"/>
        <source>Export Statistics...</source>
        <translation>통계 내보내기...</translation>
    </message>
    <message>
        <location line="-68"/>
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
    <message>
        <location line="+43"/>
        <source>HTML Files (*.html);;All Files (*)</source>
        <translation>HTML 파일 (*.html);;모든 파일 (*)</translation>
    </message>
    <message>
        <location line="+53"/>
        <location line="+49"/>
        <source>Statistics</source>
        <translation>통계</translation>
    </message>
    <message>
        <location line="-7"/>
        <source>Name:</source>
        <translation>이름:</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>eMule Qt v%1 %2 [%3]</source>
        <translation>eMule Qt v%1 %2 [%3]</translation>
    </message>
    <message>
        <location line="+166"/>
        <source>Connected Peers: %1</source>
        <translation>연결된 피어: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Peak Peers: %1</source>
        <translation>최대 피어 수: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>DHT Nodes: %1</source>
        <translation>DHT 노드: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+80"/>
        <location line="+96"/>
        <source>Traffic</source>
        <translation>트래픽</translation>
    </message>
    <message>
        <location line="-173"/>
        <location line="+174"/>
        <source>Network Traffic Received: %1</source>
        <translation>수신 네트워크 트래픽: %1</translation>
    </message>
    <message>
        <location line="-172"/>
        <location line="+175"/>
        <source>Network Traffic Sent: %1</source>
        <translation>송신 네트워크 트래픽: %1</translation>
    </message>
    <message>
        <location line="-173"/>
        <source>Redundant Data: %1</source>
        <translation>중복 데이터: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Corrupted Data: %1</source>
        <translation>손상된 데이터: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Pieces Failed Hash Check: %1</source>
        <translation>해시 검사에 실패한 조각: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+42"/>
        <source>Torrents</source>
        <translation>토렌트</translation>
    </message>
    <message>
        <location line="-41"/>
        <location line="+74"/>
        <source>Added: %1</source>
        <translation>추가됨: %1</translation>
    </message>
    <message>
        <location line="-63"/>
        <source>Number of Torrents: %1</source>
        <translation>토렌트 수: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Seeding: %1</source>
        <translation>시딩 중: %1</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+90"/>
        <source>Error: %1</source>
        <translation>오류: %1</translation>
    </message>
    <message>
        <location line="-2191"/>
        <location line="+11"/>
        <location line="+2101"/>
        <source>BitTorrent</source>
        <translation>BitTorrent</translation>
    </message>
    <message>
        <location line="+204"/>
        <location line="+507"/>
        <source>Open Connections: %1</source>
        <translation>열린 연결: %1</translation>
    </message>
    <message>
        <location line="-503"/>
        <source>Network Traffic: %1</source>
        <translation>네트워크 트래픽: %1</translation>
    </message>
    <message>
        <location line="-244"/>
        <location line="+2"/>
        <location line="+243"/>
        <source>Overhead: %1 %2</source>
        <translation>오버헤드: %1 %2</translation>
    </message>
    <message>
        <location line="-60"/>
        <source>Most Records Published: %1</source>
        <translation>최대 게시 레코드: %1</translation>
    </message>
    <message>
        <location line="+62"/>
        <source>Articles</source>
        <translation>기사</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloaded: %1</source>
        <translation>다운로드됨: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not Found on a Server: %1</source>
        <translation>서버에서 찾지 못함: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Missing on All Servers: %1</source>
        <translation>모든 서버에서 누락: %1</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+502"/>
        <source>Connection Errors: %1</source>
        <translation>연결 오류: %1</translation>
    </message>
    <message>
        <location line="-499"/>
        <source>Completed Downloads: %1 %2</source>
        <translation>완료된 다운로드: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Completed Data: %1</source>
        <translation>완료된 데이터: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Failed Downloads: %1 %2</source>
        <translation>실패한 다운로드: %1 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Post-Processing</source>
        <translation>후처리</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>PAR2 Verified: %1</source>
        <translation>PAR2 검증됨: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Repaired: %1 %2</source>
        <translation>복구됨: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Repair Failed: %1 %2</source>
        <translation>복구 실패: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Blocks Repaired: %1</source>
        <translation>복구된 블록: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Recovery Volumes Fetched: %1</source>
        <translation>가져온 복구 볼륨: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Recovery Data: %1</source>
        <translation>복구 데이터: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unpacked: %1</source>
        <translation>압축 해제됨: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Password Required: %1</source>
        <translation>비밀번호 필요: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Sets Unpacked While Downloading: %1</source>
        <translation>다운로드 중 압축 해제한 세트: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Time Spent: %1</source>
        <translation>소요 시간: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Verifying: %1 %2</source>
        <translation>검증 중: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Repairing: %1 %2</source>
        <translation>복구 중: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unpacking: %1 %2</source>
        <translation>압축 해제 중: %1 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Health Checks</source>
        <translation>상태 검사</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Checks Run: %1</source>
        <translation>실행한 검사: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Passed: %1 %2</source>
        <translation>통과: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paused as Incomplete: %1 %2</source>
        <translation>불완전으로 일시 정지: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Inconclusive: %1 %2</source>
        <translation>판정 불가: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Articles Probed: %1</source>
        <translation>조사한 기사: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Intake</source>
        <translation>유입</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>NZBs Added: %1</source>
        <translation>추가된 NZB: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Files: %1 %2</source>
        <translation>파일: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>URLs: %1 %2</source>
        <translation>URL: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Watch Folder: %1 %2</source>
        <translation>감시 폴더: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Feeds: %1 %2</source>
        <translation>피드: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Indexer Searches: %1 %2</source>
        <translation>인덱서 검색: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Duplicates: %1</source>
        <translation>중복: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Already Downloaded: %1</source>
        <translation>이미 다운로드됨: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Invalid NZBs: %1</source>
        <translation>잘못된 NZB: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Indexers</source>
        <translation>인덱서</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+138"/>
        <source>Searches: %1</source>
        <translation>검색: %1</translation>
    </message>
    <message>
        <location line="-137"/>
        <source>API Requests: %1</source>
        <translation>API 요청: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Errors: %1 %2</source>
        <translation>오류: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>NZBs Fetched: %1</source>
        <translation>가져온 NZB: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+298"/>
        <source>Failed: %1 %2</source>
        <translation>실패: %1 %2</translation>
    </message>
    <message>
        <location line="-297"/>
        <source>Feed Polls: %1</source>
        <translation>피드 확인: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Feed Matches: %1</source>
        <translation>피드 일치: %1</translation>
    </message>
    <message>
        <location line="-282"/>
        <location line="+88"/>
        <location line="+205"/>
        <source>Downloading: %1</source>
        <translation>다운로드 중: %1</translation>
    </message>
    <message>
        <location line="-291"/>
        <location line="+87"/>
        <location line="+205"/>
        <source>Queued: %1</source>
        <translation>대기 중: %1</translation>
    </message>
    <message>
        <location line="-291"/>
        <location line="+89"/>
        <location line="+203"/>
        <source>Paused: %1</source>
        <translation>일시정지됨: %1</translation>
    </message>
    <message>
        <location line="-291"/>
        <location line="+292"/>
        <source>Checking: %1</source>
        <translation>확인 중: %1</translation>
    </message>
    <message>
        <location line="-151"/>
        <source>Connections: %1</source>
        <translation>연결: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Kad Contacts: %1</source>
        <translation>Kad 연락처: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Banned Nodes: %1</source>
        <translation>차단된 노드: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Banned Hosts: %1</source>
        <translation>차단된 호스트: %1</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+3"/>
        <source>File Data: %1 %2</source>
        <translation>파일 데이터: %1 %2</translation>
    </message>
    <message>
        <location line="-2"/>
        <location line="+3"/>
        <source>Other: %1 %2</source>
        <translation>기타: %1 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Kad</source>
        <translation>Kad</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Lookups Running: %1</source>
        <translation>실행 중인 조회: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Records Stored: %1</source>
        <translation>저장된 레코드: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Size of the Records: %1</source>
        <translation>레코드 크기: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Records Published: %1</source>
        <translation>게시한 레코드: %1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Upload Queue</source>
        <translation>업로드 대기열</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload Slots: %1</source>
        <translation>업로드 슬롯: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Uploading: %1</source>
        <translation>업로드 중: %1</translation>
    </message>
    <message>
        <location line="-79"/>
        <location line="+80"/>
        <source>Waiting: %1</source>
        <translation>대기 중: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Queued Requests: %1</source>
        <translation>대기열의 요청: %1</translation>
    </message>
    <message>
        <location line="-2266"/>
        <location line="+11"/>
        <location line="+2262"/>
        <source>ed3k</source>
        <translation>ed3k</translation>
    </message>
    <message>
        <location line="-120"/>
        <source>From Hosters: %1 %2</source>
        <translation>호스터에서: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>From ed3k Peers: %1 %2</source>
        <translation>ed3k 피어에서: %1 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+58"/>
        <source>Links</source>
        <translation>링크</translation>
    </message>
    <message>
        <location line="-56"/>
        <source>Folders Expanded: %1</source>
        <translation>펼친 폴더: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Links From Peers Used: %1</source>
        <translation>사용한 피어 링크: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Obstacles</source>
        <translation>장애물</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Puzzles Asked: %1</source>
        <translation>요청된 캡차: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Puzzles Answered: %1</source>
        <translation>응답한 캡차: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hoster Waits: %1</source>
        <translation>호스터 대기: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Streams Reopened: %1</source>
        <translation>다시 연 스트림: %1</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Verified in ed3k: %1 %2</source>
        <translation>ed3k에서 검증됨: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Size of Completed Downloads: %1</source>
        <translation>완료된 다운로드 크기: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Failed Downloads: %1</source>
        <translation>실패한 다운로드: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Removed by the Hoster: %1</source>
        <translation>호스터가 삭제함: %1</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Number of Links: %1</source>
        <translation>링크 수: %1</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Puzzle: %1</source>
        <translation>캡차: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Needs You: %1</source>
        <translation>사용자 필요: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Complete: %1</source>
        <translation>완료: %1</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Hoster</source>
        <translation>호스터</translation>
    </message>
    <message>
        <location line="+89"/>
        <source>Credits</source>
        <translation>크레딧</translation>
    </message>
    <message>
        <location line="+102"/>
        <source>Post-Processing: %1</source>
        <translation>후처리: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Completed: %1</source>
        <translation>완료됨: %1</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>News Servers</source>
        <translation>뉴스 서버</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Queue</source>
        <translation>대기열</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Contacts: %1</source>
        <translation>연락처: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+8"/>
        <source>IP Verified: %1 %2</source>
        <translation>IP 확인됨: %1 %2</translation>
    </message>
    <message>
        <location line="-7"/>
        <source>Type 0, Alive over 2 Hours: %1 %2</source>
        <translation>유형 0, 2시간 이상 활성: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type 1, Alive 1 to 2 Hours: %1 %2</source>
        <translation>유형 1, 1~2시간 활성: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type 2, Alive under 1 Hour: %1 %2</source>
        <translation>유형 2, 1시간 미만 활성: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type 3, New or Not Answering: %1 %2</source>
        <translation>유형 3, 신규 또는 무응답: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type 4, Dead: %1 %2</source>
        <translation>유형 4, 응답 없음: %1 %2</translation>
    </message>
    <message>
        <location line="-206"/>
        <location line="+207"/>
        <source>Most Contacts: %1</source>
        <translation>최대 연락처: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Contacts Added: %1</source>
        <translation>추가된 연락처: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Replaced a Weaker Contact: %1 %2</source>
        <translation>약한 연락처 대체: %1 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Contacts Expired: %1</source>
        <translation>만료된 연락처: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Contacts Banned: %1</source>
        <translation>차단된 연락처: %1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Nodes Seen: ≈%1</source>
        <translation>확인한 노드: ≈%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nodes Heard Of: ≈%1</source>
        <translation>전해 들은 노드: ≈%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Firewalled (Kad)</source>
        <translation>방화벽 뒤 (Kad)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>UDP: %1 %2</source>
        <translation>UDP: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>TCP: %1 %2</source>
        <translation>TCP: %1 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Source Publishes Received: %1</source>
        <translation>받은 소스 게시: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Open: %1 %2</source>
        <translation>개방: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Firewalled, with Buddy: %1 %2</source>
        <translation>방화벽 뒤, 버디 있음: %1 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Firewalled, Direct Callback: %1 %2</source>
        <translation>방화벽 뒤, 직접 콜백: %1 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Firewalled, without Buddy: %1 %2</source>
        <translation>방화벽 뒤, 버디 없음: %1 %2</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Estimated Users: %1</source>
        <translation>예상 사용자: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Estimated Files: %1</source>
        <translation>예상 파일: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Indexed Keywords: %1</source>
        <translation>색인된 키워드: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Indexed Sources: %1</source>
        <translation>색인된 소스: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Indexed Notes: %1</source>
        <translation>색인된 노트: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Active Searches: %1</source>
        <translation>활성 검색: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Banned Addresses: %1</source>
        <translation>차단된 주소: %1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Time Connected: %1 %2</source>
        <translation>연결 시간: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hellos Sent: %1</source>
        <translation>보낸 Hello: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hellos Answered: %1 %2</source>
        <translation>응답받은 Hello: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Lookup Answers: %1</source>
        <translation>조회 응답: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bootstrap Answers: %1</source>
        <translation>부트스트랩 응답: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Node Lookups: %1 %2</source>
        <translation>노드 조회: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Keyword Searches: %1 %2</source>
        <translation>키워드 검색: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Source Searches: %1 %2</source>
        <translation>소스 검색: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Notes Searches: %1 %2</source>
        <translation>노트 검색: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Publishes: %1</source>
        <translation>게시: %1</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Kademlia</source>
        <translation>Kademlia</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Different nodes, counted by node ID. An estimate, accurate to about 2%.</source>
        <translation>노드 ID로 센 서로 다른 노드 수. 약 2% 오차의 추정치입니다.</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Routing Table</source>
        <translation>라우팅 테이블</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>By Version</source>
        <translation>버전별</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Nodes</source>
        <translation>노드</translation>
    </message>
    <message>
        <location line="-2200"/>
        <location line="+2203"/>
        <source>By Country</source>
        <translation>국가별</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nodes that sent us a packet, by the country of the address it came from.</source>
        <translation>우리에게 패킷을 보낸 노드를 보낸 주소의 국가별로 표시합니다.</translation>
    </message>
    <message>
        <location line="-2230"/>
        <location line="+2234"/>
        <source>Network</source>
        <translation>네트워크</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Activity</source>
        <translation>활동</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Nodes that sent us a packet themselves.</source>
        <translation>직접 우리에게 패킷을 보낸 노드.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Nodes that other nodes named in their answers; most are never contacted.</source>
        <translation>다른 노드가 응답에서 알려 준 노드. 대부분은 접속하지 않습니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Counted per publish, not per node. Firewalled nodes that cannot publish are missing.</source>
        <translation>노드가 아니라 게시 단위로 셉니다. 게시할 수 없는 방화벽 뒤 노드는 포함되지 않습니다.</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>Version %1 (eMule %2)</source>
        <translation>버전 %1 (eMule %2)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Version %1</source>
        <translation>버전 %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1: %2 %3</source>
        <translation>%1: %2 %3</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>%1: ≈%2 %3</source>
        <translation>%1: ≈%2 %3</translation>
    </message>
    <message>
        <location line="+38"/>
        <source>Published: %1</source>
        <translation>게시됨: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Chunks Published: %1</source>
        <translation>게시한 청크: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload Saved: %1</source>
        <translation>절약한 업로드: %1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Fetched: %1</source>
        <translation>가져옴: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Chunks Fetched: %1 %2</source>
        <translation>가져온 청크: %1 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Failed Hash Check: %1</source>
        <translation>해시 검사 실패: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Resumed: %1</source>
        <translation>재개됨: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Offers Received: %1</source>
        <translation>받은 제안: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Declined: %1 %2</source>
        <translation>거절됨: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Chunks Found in Kad: %1</source>
        <translation>Kad에서 찾은 청크: %1</translation>
    </message>
    <message>
        <location line="-1168"/>
        <location line="+56"/>
        <location line="+1"/>
        <location line="+1"/>
        <location line="+10"/>
        <location line="+1189"/>
        <source>n/a</source>
        <translation>데이터 없음</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Measured here, not reported by the provider, in decimal GB as providers bill. Expect a few percent below the provider&apos;s own figure.</source>
        <translation>제공업체가 알려 준 값이 아니라 여기에서 측정한 값이며, 제공업체가 청구하는 방식과 같은 10진수 GB 단위입니다. 제공업체의 수치보다 몇 퍼센트 낮게 나올 수 있습니다.</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>%1 (disabled)</source>
        <translation>%1 (비활성화됨)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Session Traffic: %1 %2</source>
        <translation>세션 트래픽: %1 %2</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Articles Downloaded: %1</source>
        <translation>다운로드한 기사: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not Found: %1</source>
        <translation>찾지 못함: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Corrupt: %1</source>
        <translation>손상됨: %1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>%1 of %2 %3</source>
        <translation>%1 / %2 %3</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>, resets %1</source>
        <translation>, %1에 초기화</translation>
    </message>
    <message>
        <location line="+3"/>
        <source> — spent</source>
        <translation> — 소진됨</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Block: %1</source>
        <translation>블록: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>This Period: %1</source>
        <translation>이번 기간: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>All Time: %1</source>
        <translation>전체 기간: %1</translation>
    </message>
</context>
<context>
    <name>eMule::StatsGraph</name>
    <message>
        <location filename="../src/gui/controls/StatsGraph.cpp" line="+42"/>
        <source>Stopped</source>
        <translation>중지됨</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>%1: %2 @ %3 (%4 ago)</source>
        <translation>%1: %2 @ %3 (%4 전)</translation>
    </message>
</context>
<context>
    <name>eMule::ToolbarCustomizeDialog</name>
    <message>
        <location filename="../src/gui/dialogs/ToolbarCustomizeDialog.cpp" line="+26"/>
        <source>── Separator ──</source>
        <translation>── 구분선 ──</translation>
    </message>
    <message>
        <location line="+12"/>
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
    <name>eMule::TorrentDetailsDialog</name>
    <message>
        <source>Skip</source>
        <translation type="vanished">건너뛰기</translation>
    </message>
    <message>
        <source>Low</source>
        <translation type="vanished">낮음</translation>
    </message>
    <message>
        <source>Normal</source>
        <translation type="vanished">보통</translation>
    </message>
    <message>
        <source>High</source>
        <translation type="vanished">높음</translation>
    </message>
    <message>
        <source>Maximum</source>
        <translation type="vanished">최대</translation>
    </message>
    <message>
        <source>Torrent Details</source>
        <translation type="vanished">토렌트 세부 정보</translation>
    </message>
    <message>
        <source>File</source>
        <translation type="vanished">파일</translation>
    </message>
    <message>
        <location filename="../src/gui/dialogs/TorrentDetailsDialog.cpp" line="+52"/>
        <source>N/A</source>
        <translation>N/A</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Use global settings</source>
        <translation>전역 설정 사용</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Seed regardless</source>
        <translation>제한 없이 시드</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Stop seeding at:</source>
        <translation>시드 중지 기준:</translation>
    </message>
    <message>
        <location line="+46"/>
        <source>Info</source>
        <translation>정보</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Activity</source>
        <translation>활동</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Options</source>
        <translation>옵션</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+2"/>
        <source>%1 — Torrent Inspector</source>
        <translation>%1 — 토렌트 검사기</translation>
    </message>
    <message numerus="yes">
        <location line="+56"/>
        <source>%n file(s), %1</source>
        <translation>
            <numerusform>파일 %n개, %1</numerusform>
        </translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Getting metadata…</source>
        <translation>메타데이터 가져오는 중…</translation>
    </message>
    <message numerus="yes">
        <location line="+4"/>
        <source>%1 (%n piece(s) @ %2)</source>
        <translation>
            <numerusform>%1 (조각 %n개 @ %2)</numerusform>
        </translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Private to this tracker — DHT and PEX disabled</source>
        <translation>이 트래커 전용 — DHT 및 PEX 비활성화됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Public torrent</source>
        <translation>공개 토렌트</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Created by %1 on %2</source>
        <translation>%2에 %1(으)로 생성됨</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Created by %1</source>
        <translation>%1(으)로 생성됨</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Created on %1</source>
        <translation>%1에 생성됨</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>%1 (%2 selected)</source>
        <translation>%1 (%2 선택됨)</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Active now</source>
        <translation>현재 활성</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1 ago</source>
        <translation>%1 전</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Unknown</source>
        <translation>알 수 없음</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Details</source>
        <translation>상세</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Size</source>
        <translation>크기</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Location</source>
        <translation>위치</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hash v1</source>
        <translation>해시 v1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hash v2</source>
        <translation>해시 v2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Privacy</source>
        <translation>공개 여부</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Origin</source>
        <translation>출처</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Copy Magnet Link</source>
        <translation>마그넷 링크 복사</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Magnet</source>
        <translation>마그넷</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Comment</source>
        <translation>댓글</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Transfer</source>
        <translation>전송</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>State</source>
        <translation>상태</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Have</source>
        <translation>보유</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Availability</source>
        <translation>가용성</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Failed DL</source>
        <translation>실패한 DL</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ratio</source>
        <translation>비율</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Error</source>
        <translation>오류</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>Available</source>
        <translation>사용 가능</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Share of each part that is downloaded</source>
        <translation>각 부분의 다운로드된 비율</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Peers that have each part; red: nobody connected has it</source>
        <translation>각 부분을 가진 피어 수; 빨간색: 연결된 피어 중 아무도 없음</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Dates</source>
        <translation>날짜</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Added</source>
        <translation>추가됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Completed</source>
        <translation>완료됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Last Activity</source>
        <translation>마지막 활동</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Time Elapsed</source>
        <translation>경과 시간</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Downloading</source>
        <translation>다운로드 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Seeding</source>
        <translation>시딩 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remaining</source>
        <translation>남은 시간</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Speed</source>
        <translation>속도</translation>
    </message>
    <message>
        <location line="+7"/>
        <source> KiB/s</source>
        <translation> KiB/s</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Also limited by the global and BitTorrent limits.</source>
        <translation>전체 제한과 BitTorrent 제한도 적용됩니다.</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Limit download speed:</source>
        <translation>다운로드 속도 제한:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Limit upload speed:</source>
        <translation>업로드 속도 제한:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Seeding Limits</source>
        <translation>시드 제한</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Ratio:</source>
        <translation>비율:</translation>
    </message>
    <message>
        <location line="+7"/>
        <source> min</source>
        <translation> 분</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Seed time:</source>
        <translation>시드 시간:</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Peer Connections</source>
        <translation>피어 연결</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Default</source>
        <translation>기본값</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Maximum peers:</source>
        <translation>최대 피어 수:</translation>
    </message>
    <message>
        <location line="+89"/>
        <source>Could not change the torrent options.</source>
        <translation>토렌트 옵션을 변경할 수 없습니다.</translation>
    </message>
    <message numerus="yes">
        <location line="+17"/>
        <source>Copied %n magnet link(s).</source>
        <translation>
            <numerusform>마그넷 링크 %n개를 복사했습니다.</numerusform>
        </translation>
    </message>
    <message>
        <location line="-232"/>
        <location line="+37"/>
        <source>Progress</source>
        <translation>진행률</translation>
    </message>
    <message>
        <source>Priority</source>
        <translation type="vanished">우선순위</translation>
    </message>
    <message>
        <source>Files</source>
        <translation type="vanished">파일</translation>
    </message>
    <message>
        <source>Address</source>
        <translation type="vanished">주소</translation>
    </message>
    <message>
        <source>Client</source>
        <translation type="vanished">클라이언트</translation>
    </message>
    <message>
        <source>Down Speed</source>
        <translation type="vanished">다운로드 속도</translation>
    </message>
    <message>
        <source>Up Speed</source>
        <translation type="vanished">업로드 속도</translation>
    </message>
    <message>
        <location line="-34"/>
        <source>Downloaded</source>
        <translation>다운로드됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Uploaded</source>
        <translation>업로드됨</translation>
    </message>
    <message>
        <source>Flags</source>
        <translation type="vanished">플래그</translation>
    </message>
    <message>
        <source>Source</source>
        <translation type="vanished">소스</translation>
    </message>
    <message>
        <source>Peers</source>
        <translation type="vanished">피어</translation>
    </message>
    <message>
        <source>URL</source>
        <translation type="vanished">URL</translation>
    </message>
    <message>
        <source>Tier</source>
        <translation type="vanished">계층</translation>
    </message>
    <message>
        <source>Status</source>
        <translation type="vanished">상태</translation>
    </message>
    <message>
        <source>Seeds</source>
        <translation type="vanished">시드</translation>
    </message>
    <message>
        <source>Trackers</source>
        <translation type="vanished">트래커</translation>
    </message>
    <message>
        <location line="+141"/>
        <source>This torrent is no longer in the list.</source>
        <translation>이 토렌트는 더 이상 목록에 없습니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>BitTorrent is not running.</source>
        <translation>BitTorrent가 실행 중이 아닙니다.</translation>
    </message>
    <message>
        <source>Download</source>
        <translation type="vanished">다운로드</translation>
    </message>
    <message>
        <source>Do Not Download</source>
        <translation type="vanished">다운로드 안 함</translation>
    </message>
    <message>
        <source>Could not change the file priorities.</source>
        <translation type="vanished">파일 우선순위를 변경할 수 없습니다.</translation>
    </message>
    <message>
        <source>Files (%1)</source>
        <translation type="vanished">파일 (%1)</translation>
    </message>
    <message>
        <source>Seed</source>
        <translation type="vanished">시드</translation>
    </message>
    <message>
        <source>Enc</source>
        <translation type="vanished">암호</translation>
    </message>
    <message>
        <source>Peers (%1)</source>
        <translation type="vanished">피어 (%1)</translation>
    </message>
    <message>
        <source>Working</source>
        <translation type="vanished">작동 중</translation>
    </message>
    <message>
        <source>Not working</source>
        <translation type="vanished">작동 안 함</translation>
    </message>
    <message>
        <source>Trackers (%1)</source>
        <translation type="vanished">트래커 (%1)</translation>
    </message>
</context>
<context>
    <name>eMule::TorrentFilterProxy</name>
    <message>
        <location filename="../src/gui/controls/TorrentFilterProxy.cpp" line="+70"/>
        <source>All</source>
        <translation>전체</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloading</source>
        <translation>다운로드 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Seeding</source>
        <translation>시딩 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paused</source>
        <translation>일시정지됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Active</source>
        <translation>활성</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Error</source>
        <translation>오류</translation>
    </message>
</context>
<context>
    <name>eMule::TorrentListModel</name>
    <message>
        <location filename="../src/gui/controls/TorrentListModel.cpp" line="+54"/>
        <source>Queued</source>
        <translation>대기 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Checking files</source>
        <translation>파일 확인 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Getting metadata</source>
        <translation>메타데이터 가져오는 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloading</source>
        <translation>다운로드 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+242"/>
        <source>Completed</source>
        <translation>완료됨</translation>
    </message>
    <message>
        <location line="-241"/>
        <source>Seeding</source>
        <translation>시딩 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Checking resume data</source>
        <translation>재개 데이터 확인 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paused</source>
        <translation>일시정지됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Error</source>
        <translation>오류</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Moving</source>
        <translation>이동 중</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Unknown</source>
        <translation>알 수 없음</translation>
    </message>
    <message>
        <location line="+211"/>
        <source>Private torrent (no DHT, PEX or LSD)</source>
        <translation>비공개 토렌트 (DHT, PEX, LSD 없음)</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>%1% — %2 of %3 pieces</source>
        <translation>%1% — %3조각 중 %2</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Name</source>
        <translation>이름</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Size</source>
        <translation>크기</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Progress</source>
        <translation>진행률</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status</source>
        <translation>상태</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Down Speed</source>
        <translation>다운로드 속도</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Up Speed</source>
        <translation>업로드 속도</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Seeds</source>
        <translation>시드</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Peers</source>
        <translation>피어</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ratio</source>
        <translation>비율</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remaining Time</source>
        <translation>남은 시간</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Added</source>
        <translation>추가됨</translation>
    </message>
    <message>
        <location line="+316"/>
        <source>%1 (#%2)</source>
        <translation>%1 (#%2)</translation>
    </message>
</context>
<context>
    <name>eMule::TorrentListsPane</name>
    <message>
        <location filename="../src/gui/controls/TorrentListsPane.cpp" line="+86"/>
        <source>Tracker</source>
        <translation>트래커</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>DHT</source>
        <translation>DHT</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>PEX</source>
        <translation>PEX</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Local discovery</source>
        <translation>로컬 검색</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Resume data</source>
        <translation>재개 데이터</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Incoming</source>
        <translation>받은 파일</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/TorrentListModel.cpp" line="-539"/>
        <source>Skip</source>
        <translation>건너뛰기</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Low</source>
        <translation>낮음</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Normal</source>
        <translation>보통</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>High</source>
        <translation>높음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Maximum</source>
        <translation>최고</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/TorrentListsPane.cpp" line="+33"/>
        <source>Peer is connected over µTP</source>
        <translation>피어가 µTP로 연결됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Optimistic unchoke</source>
        <translation>낙관적 언초크</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloading from this peer</source>
        <translation>이 피어에서 다운로드 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>We would download from this peer if they would let us</source>
        <translation>허용되면 이 피어에서 다운로드함</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Uploading to peer</source>
        <translation>피어에게 업로드 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>We would upload to this peer if we unchoked it</source>
        <translation>언초크하면 이 피어에게 업로드함</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Peer has unchoked us, but we&apos;re not interested</source>
        <translation>피어가 언초크했지만 관심 없음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>We unchoked this peer, but they&apos;re not interested</source>
        <translation>이 피어를 언초크했지만 상대가 관심 없음</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+102"/>
        <location line="+141"/>
        <source>Encrypted connection</source>
        <translation>암호화된 연결</translation>
    </message>
    <message>
        <location line="-242"/>
        <source>Connected through a holepunch</source>
        <translation>홀펀칭으로 연결됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Peer was found through DHT</source>
        <translation>DHT로 찾은 피어</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Peer was found through Peer Exchange (PEX)</source>
        <translation>피어 교환(PEX)으로 찾은 피어</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Peer is an incoming connection</source>
        <translation>들어오는 연결의 피어</translation>
    </message>
    <message>
        <location line="+40"/>
        <location line="+95"/>
        <source>Select a torrent to see its peers, trackers and files.</source>
        <translation>피어, 트래커, 파일을 보려면 토렌트를 선택하세요.</translation>
    </message>
    <message>
        <location line="-40"/>
        <source>Up</source>
        <translation>업로드</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Down</source>
        <translation>다운로드</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>%</source>
        <translation>%</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+53"/>
        <source>Status</source>
        <translation>상태</translation>
    </message>
    <message>
        <location line="-53"/>
        <source>Address</source>
        <translation>주소</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Client</source>
        <translation>클라이언트</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+53"/>
        <source>Downloaded</source>
        <translation>다운로드됨</translation>
    </message>
    <message>
        <location line="-53"/>
        <source>Uploaded</source>
        <translation>업로드됨</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Source</source>
        <translation>소스</translation>
    </message>
    <message>
        <location line="+13"/>
        <location line="+40"/>
        <source>Peers</source>
        <translation>피어</translation>
    </message>
    <message>
        <location line="-38"/>
        <source>Trackers</source>
        <translation>트래커</translation>
    </message>
    <message>
        <location line="+5"/>
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
        <source>Progress</source>
        <translation>진행률</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+335"/>
        <source>Download</source>
        <translation>다운로드</translation>
    </message>
    <message>
        <location line="-335"/>
        <location line="+326"/>
        <source>Priority</source>
        <translation>우선순위</translation>
    </message>
    <message>
        <location line="-315"/>
        <source>Files</source>
        <translation>파일</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>URL</source>
        <translation>URL</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Tier</source>
        <translation>티어</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Next Announce</source>
        <translation>다음 알림</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Seeds</source>
        <translation>시드</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Add…</source>
        <translation>추가…</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Remove</source>
        <translation>제거</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Reannounce</source>
        <translation>다시 알림</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show backup trackers</source>
        <translation>백업 트래커 표시</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>This torrent is no longer in the list.</source>
        <translation>이 토렌트는 더 이상 목록에 없습니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>BitTorrent is not running.</source>
        <translation>BitTorrent가 실행 중이 아닙니다.</translation>
    </message>
    <message>
        <location line="+51"/>
        <source>Peers (%1)</source>
        <translation>피어 (%1)</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>Announcing…</source>
        <translation>알리는 중…</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Error: %1</source>
        <translation>오류: %1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Working</source>
        <translation>작동 중</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Working — %1</source>
        <translation>작동 중 — %1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Not contacted yet</source>
        <translation>아직 연결 안 됨</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Trackers (%1)</source>
        <translation>트래커 (%1)</translation>
    </message>
    <message>
        <location line="+94"/>
        <source>Files (%1)</source>
        <translation>파일 (%1)</translation>
    </message>
    <message>
        <location line="+53"/>
        <source>Do Not Download</source>
        <translation>다운로드 안 함</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Expand All</source>
        <translation>모두 펼치기</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Collapse All</source>
        <translation>모두 접기</translation>
    </message>
    <message>
        <location line="+42"/>
        <source>Could not change the file priorities.</source>
        <translation>파일 우선순위를 변경할 수 없습니다.</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Add Trackers</source>
        <translation>트래커 추가</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Tracker URLs, one per line:</source>
        <translation>트래커 URL (한 줄에 하나씩):</translation>
    </message>
    <message>
        <location line="+41"/>
        <source>Could not change the trackers.</source>
        <translation>트래커를 변경할 수 없습니다.</translation>
    </message>
</context>
<context>
    <name>eMule::TorrentPanel</name>
    <message>
        <location filename="../src/gui/panels/TorrentPanel.cpp" line="+119"/>
        <source>Torrent download completed: %1</source>
        <translation>토렌트 다운로드 완료: %1</translation>
    </message>
    <message>
        <location line="+14"/>
        <location line="+6"/>
        <location line="+12"/>
        <location line="+13"/>
        <source>Add Torrent</source>
        <translation>토렌트 추가</translation>
    </message>
    <message>
        <location line="-31"/>
        <location line="+18"/>
        <location line="+13"/>
        <location line="+15"/>
        <location line="+11"/>
        <location line="+568"/>
        <source>Not connected to the eMule core.</source>
        <translation>eMule 코어에 연결되지 않았습니다.</translation>
    </message>
    <message>
        <location line="-618"/>
        <source>Cannot read %1.</source>
        <translation>%1을(를) 읽을 수 없습니다.</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Add Magnet Links</source>
        <translation>마그넷 링크 추가</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+4"/>
        <source>Add Torrent Files</source>
        <translation>토렌트 파일 추가</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Torrent files (*.torrent);;All files (*)</source>
        <translation>토렌트 파일 (*.torrent);;모든 파일 (*)</translation>
    </message>
    <message>
        <location line="+70"/>
        <source>Add Torrent…</source>
        <translation>토렌트 추가…</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Add Magnet Link…</source>
        <translation>마그넷 링크 추가…</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Pause</source>
        <translation>일시정지</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Resume</source>
        <translation>재개</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Remove…</source>
        <translation>제거…</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Details…</source>
        <translation>세부 정보…</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Force Recheck</source>
        <translation>강제 재확인</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Force Reannounce</source>
        <translation>강제 재알림</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Open Folder</source>
        <translation>폴더 열기</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Copy Magnet Link</source>
        <translation>마그넷 링크 복사</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Paste Magnet Links</source>
        <translation>마그넷 링크 붙여넣기</translation>
    </message>
    <message>
        <location line="+95"/>
        <source>BitTorrent Options…</source>
        <translation>BitTorrent 옵션…</translation>
    </message>
    <message>
        <location line="+95"/>
        <source>Priority</source>
        <translation>우선순위</translation>
    </message>
    <message>
        <location line="+45"/>
        <source>BitTorrent: %1</source>
        <translation>BitTorrent: %1</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Remove Torrents</source>
        <translation>토렌트 제거</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Remove &quot;%1&quot; from the list?</source>
        <translation>&quot;%1&quot;을(를) 목록에서 제거하시겠습니까?</translation>
    </message>
    <message numerus="yes">
        <location line="+1"/>
        <source>Remove %n torrent(s) from the list?</source>
        <translation>
            <numerusform>토렌트 %n개를 목록에서 제거하시겠습니까?</numerusform>
        </translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Also delete the downloaded files</source>
        <translation>다운로드한 파일도 삭제</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Saved on the core&apos;s machine: %1</source>
        <translation>코어 컴퓨터에 저장됨: %1</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>No magnet link: v2-only torrents have no v1 info hash.</source>
        <translation>마그넷 링크 없음: v2 전용 토렌트에는 v1 info 해시가 없습니다.</translation>
    </message>
    <message numerus="yes">
        <location line="+4"/>
        <source>Copied %n magnet link(s).</source>
        <translation>
            <numerusform>마그넷 링크 %n개를 복사했습니다.</numerusform>
        </translation>
    </message>
    <message>
        <location line="+164"/>
        <source>Asking the core for the BitTorrent state…</source>
        <translation>코어에 BitTorrent 상태 요청 중…</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>This eMule core was built without BitTorrent support.</source>
        <translation>이 eMule 코어는 BitTorrent 지원 없이 빌드되었습니다.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>BitTorrent is off — enable it in Options.</source>
        <translation>BitTorrent가 꺼져 있습니다 — 옵션에서 활성화하세요.</translation>
    </message>
    <message numerus="yes">
        <location line="+20"/>
        <source>%n torrent(s), %1 active — Down: %2, Up: %3 — DHT: %4 nodes — Port %5 %6</source>
        <translation>
            <numerusform>토렌트 %n개, 활성 %1개 — 다운: %2, 업: %3 — DHT: 노드 %4개 — 포트 %5 %6</numerusform>
        </translation>
    </message>
    <message>
        <location line="+6"/>
        <source>(listening)</source>
        <translation>(수신 대기 중)</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>(not listening)</source>
        <translation>(수신 대기 안 함)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>No torrents. Use &quot;Add Torrent…&quot; or &quot;Add Magnet Link…&quot; to start one.</source>
        <translation>토렌트가 없습니다. &quot;토렌트 추가…&quot; 또는 &quot;마그넷 링크 추가…&quot;로 시작하세요.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>No torrents match this filter.</source>
        <translation>이 필터와 일치하는 토렌트가 없습니다.</translation>
    </message>
    <message>
        <location line="+50"/>
        <source>Could not change the file priorities.</source>
        <translation>파일 우선순위를 변경할 수 없습니다.</translation>
    </message>
</context>
<context>
    <name>eMule::TransferPanel</name>
    <message>
        <location filename="../src/gui/panels/TransferPanel.cpp" line="+802"/>
        <source>Downloads</source>
        <translation>다운로드</translation>
    </message>
    <message>
        <location line="-421"/>
        <location line="+341"/>
        <source>Priority (Download)</source>
        <translation>우선순위(다운로드)</translation>
    </message>
    <message>
        <location line="-312"/>
        <location line="+2152"/>
        <location line="+177"/>
        <source>Low</source>
        <translation>낮음</translation>
    </message>
    <message>
        <location line="-2328"/>
        <location line="+2151"/>
        <location line="+178"/>
        <source>Normal</source>
        <translation>보통</translation>
    </message>
    <message>
        <location line="-2328"/>
        <location line="+2150"/>
        <location line="+179"/>
        <source>High</source>
        <translation>높음</translation>
    </message>
    <message>
        <location line="-2325"/>
        <location line="+2329"/>
        <source>Very Low</source>
        <translation>매우 낮음</translation>
    </message>
    <message>
        <location line="-2327"/>
        <location line="+2329"/>
        <source>Very High</source>
        <translation>매우 높음</translation>
    </message>
    <message>
        <location line="-2327"/>
        <location line="+2330"/>
        <source>Auto</source>
        <translation>자동</translation>
    </message>
    <message>
        <location line="-2319"/>
        <location line="+291"/>
        <location line="+457"/>
        <location line="+1397"/>
        <source>Pause</source>
        <translation>일시정지</translation>
    </message>
    <message>
        <location line="-2136"/>
        <location line="+282"/>
        <location line="+463"/>
        <location line="+1393"/>
        <source>Stop</source>
        <translation>중지</translation>
    </message>
    <message>
        <location line="-2129"/>
        <location line="+273"/>
        <location line="+469"/>
        <location line="+1389"/>
        <source>Resume</source>
        <translation>재개</translation>
    </message>
    <message>
        <location line="-2118"/>
        <location line="+260"/>
        <location line="+475"/>
        <location line="+1385"/>
        <location line="+4"/>
        <source>Cancel</source>
        <translation>취소</translation>
    </message>
    <message>
        <location line="-2091"/>
        <source>Preview with</source>
        <translation>다음으로 미리보기</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Pause when preview is possible</source>
        <translation>미리보기가 가능해지면 일시정지</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Increase priority for preview parts</source>
        <translation>미리보기 파트의 우선순위 높이기</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Stop Import parts to file</source>
        <translation>파일로 파트 가져오기 중지</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+1437"/>
        <location line="+1"/>
        <location line="+10"/>
        <source>Import parts to file...</source>
        <translation>파일로 파트 가져오기...</translation>
    </message>
    <message>
        <location line="-1443"/>
        <source>Mass Rename...</source>
        <translation>일괄 이름 변경...</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Source Handling</source>
        <translation>소스 처리</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add Sources...</source>
        <translation>소스 추가...</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+1344"/>
        <source>Source Limit...</source>
        <translation>소스 제한...</translation>
    </message>
    <message>
        <location line="-1341"/>
        <source>Swap All A4AF Sources to This File</source>
        <translation>모든 A4AF 소스를 이 파일로 전환</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>cannot swap sources.</source>
        <translation>소스를 전환할 수 없습니다.</translation>
    </message>
    <message numerus="yes">
        <location line="+6"/>
        <source>%n A4AF source(s) swapped to this file.</source>
        <translation>
            <numerusform>A4AF 소스 %n개를 이 파일로 전환했습니다.</numerusform>
        </translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Copy eD2K Links</source>
        <translation>eD2K 링크 복사</translation>
    </message>
    <message>
        <location line="+26"/>
        <location line="+71"/>
        <source>ed3k Sets...</source>
        <translation>ed3k 세트...</translation>
    </message>
    <message>
        <location line="-31"/>
        <source>New...</source>
        <translation>새로 만들기...</translation>
    </message>
    <message>
        <location line="+22"/>
        <location line="+9"/>
        <source>Show Toolbar</source>
        <translation>도구 모음 표시</translation>
    </message>
    <message>
        <location line="+439"/>
        <source>Close Toolbar</source>
        <translation>도구 모음 닫기</translation>
    </message>
    <message>
        <location line="+736"/>
        <source>Failed to execute: %1 %2</source>
        <translation>실행 실패: %1 %2</translation>
    </message>
    <message>
        <location line="+46"/>
        <source>Please enter the new source limit for the selected file(s)!
(set to 0 to use global setting)</source>
        <translation>선택한 파일의 새 소스 제한을 입력하십시오!
(0으로 설정하면 전역 설정 사용)</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>cannot add the source.</source>
        <translation>소스를 추가할 수 없습니다.</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>The source %1 was not accepted.</source>
        <translation>소스 %1이(가) 수락되지 않았습니다.</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>cannot import parts.</source>
        <translation>파트를 가져올 수 없습니다.</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Path of the file on the core&apos;s machine:</source>
        <translation>코어 컴퓨터의 파일 경로:</translation>
    </message>
    <message>
        <location line="+608"/>
        <source>Download In Alphabetical Order</source>
        <translation>알파벳순으로 다운로드</translation>
    </message>
    <message>
        <location line="+182"/>
        <source>Copy ed3k Node ID</source>
        <translation>ed3k Node ID 복사</translation>
    </message>
    <message>
        <location line="+70"/>
        <location line="+103"/>
        <source>cannot view shared files.</source>
        <translation>공유 파일을 볼 수 없습니다.</translation>
    </message>
    <message>
        <location line="-90"/>
        <source>Unban</source>
        <translation>차단 해제</translation>
    </message>
    <message>
        <location line="+124"/>
        <source>Bootstrap</source>
        <translation>부트스트랩</translation>
    </message>
    <message>
        <location line="+168"/>
        <source>Cancel Download</source>
        <translation>다운로드 취소</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+31"/>
        <source>Cancel download &quot;%1&quot;?</source>
        <translation>&quot;%1&quot; 다운로드를 취소하시겠습니까?</translation>
    </message>
    <message>
        <location line="-27"/>
        <location line="+29"/>
        <source>Cancel Downloads</source>
        <translation>다운로드 취소</translation>
    </message>
    <message>
        <location line="-28"/>
        <location line="+27"/>
        <source>Cancel %1 selected downloads?</source>
        <translation>선택한 다운로드 %1개를 취소하시겠습니까?</translation>
    </message>
    <message>
        <location line="+21"/>
        <location line="+18"/>
        <location line="+34"/>
        <source>Rename</source>
        <translation>이름 바꾸기</translation>
    </message>
    <message>
        <location line="-51"/>
        <source>Do you want to cleanup the file names of the selected files?</source>
        <translation>선택한 파일의 이름을 정리하시겠습니까?</translation>
    </message>
    <message>
        <source>File name:</source>
        <translation type="vanished">파일 이름:</translation>
    </message>
    <message>
        <location line="-2737"/>
        <location line="+245"/>
        <location line="+481"/>
        <location line="+637"/>
        <source>Open File</source>
        <translation>파일 열기</translation>
    </message>
    <message>
        <location line="-1354"/>
        <location line="+2"/>
        <location line="+729"/>
        <location line="+680"/>
        <source>Preview</source>
        <translation>미리보기</translation>
    </message>
    <message>
        <location line="-1372"/>
        <location line="+2272"/>
        <location line="+118"/>
        <source>Details...</source>
        <translation>상세...</translation>
    </message>
    <message>
        <location line="-2381"/>
        <source>Comments...</source>
        <translation>댓글...</translation>
    </message>
    <message>
        <location line="+53"/>
        <location line="+135"/>
        <location line="+546"/>
        <source>Clear Completed</source>
        <translation>완료됨 삭제</translation>
    </message>
    <message>
        <location line="-636"/>
        <source>eD2K Links...</source>
        <translation>eD2K 링크...</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+85"/>
        <source>Paste eD2K Links</source>
        <translation>eD2K 링크 붙여넣기</translation>
    </message>
    <message>
        <location line="-64"/>
        <location line="+64"/>
        <location line="+2063"/>
        <location line="+113"/>
        <location line="+72"/>
        <source>Find...</source>
        <translation>찾기...</translation>
    </message>
    <message>
        <location line="-2304"/>
        <source>Search Related Files</source>
        <translation>관련 파일 검색</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Web Services</source>
        <translation>웹 서비스</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+35"/>
        <location line="+527"/>
        <source>Assign To Category</source>
        <translation>카테고리에 할당</translation>
    </message>
    <message>
        <source>(All)</source>
        <translation type="vanished">(전체)</translation>
    </message>
    <message>
        <source>All</source>
        <translation type="vanished">모두</translation>
    </message>
    <message>
        <location line="+1240"/>
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
        <location line="-1356"/>
        <source>Clients on queue:   0</source>
        <translation>대기열의 클라이언트:   0</translation>
    </message>
    <message>
        <location line="-440"/>
        <location line="+560"/>
        <source>(Unassign)</source>
        <translation>(할당 해제)</translation>
    </message>
    <message>
        <location line="-83"/>
        <location line="+1386"/>
        <source>Priority</source>
        <translation>우선순위</translation>
    </message>
    <message>
        <location line="-1349"/>
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
        <location line="+35"/>
        <source>Search Related</source>
        <translation>관련 검색</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Find</source>
        <translation>찾기</translation>
    </message>
    <message>
        <location line="+593"/>
        <location line="+23"/>
        <source>Preview not available — web server is not running or stream token not received.</source>
        <translation>미리보기를 사용할 수 없습니다 — 웹 서버가 실행 중이 아니거나 스트림 토큰을 받지 못했습니다.</translation>
    </message>
    <message>
        <location line="+151"/>
        <source>Open File not available — web server is not running or stream token not received.</source>
        <translation>파일 열기를 사용할 수 없습니다 — 웹 서버가 실행 중이 아니거나 스트림 토큰을 받지 못했습니다.</translation>
    </message>
    <message>
        <location line="+464"/>
        <location line="+7"/>
        <source>Downloads (%1)</source>
        <translation>다운로드 (%1)</translation>
    </message>
    <message>
        <location line="-23"/>
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
        <location line="+28"/>
        <source>Clients on queue:   %1</source>
        <translation>대기열의 클라이언트:   %1</translation>
    </message>
    <message>
        <source>Cat %1</source>
        <translation type="vanished">카테고리 %1</translation>
    </message>
    <message>
        <source>Category</source>
        <translation type="vanished">카테고리</translation>
    </message>
    <message>
        <location line="+50"/>
        <source>Are you sure you want to cancel every download in &quot;%1&quot;?</source>
        <translation>&quot;%1&quot;의 모든 다운로드를 취소하시겠습니까?</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Resume next file</source>
        <translation>다음 파일 재개</translation>
    </message>
    <message>
        <source>Open Incoming Folder</source>
        <translation type="vanished">수신 폴더 열기</translation>
    </message>
    <message>
        <location line="+217"/>
        <location line="+112"/>
        <source>Add To Friends</source>
        <translation>친구에 추가</translation>
    </message>
    <message>
        <location line="-88"/>
        <location line="+113"/>
        <source>Send Message</source>
        <translation>메시지 보내기</translation>
    </message>
    <message>
        <source>Message:</source>
        <translation type="vanished">메시지:</translation>
    </message>
    <message>
        <location line="-102"/>
        <location line="+109"/>
        <source>View Shared Files</source>
        <translation>공유 파일 보기</translation>
    </message>
</context>
<context>
    <name>eMule::TrayMenuManager</name>
    <message>
        <location filename="../src/gui/app/TrayMenuManager.cpp" line="+86"/>
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
        <location line="+20"/>
        <source>Pause Usenet</source>
        <translation>Usenet 일시정지</translation>
    </message>
    <message>
        <location line="+14"/>
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
    <name>eMule::UsenetArchiveEntryDialog</name>
    <message>
        <location filename="../src/gui/dialogs/UsenetArchiveEntryDialog.cpp" line="+59"/>
        <source>Preview File</source>
        <translation>미리 볼 파일</translation>
    </message>
    <message>
        <location line="+42"/>
        <source>Playable</source>
        <translation>재생 가능</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>Nothing has arrived for this download yet.</source>
        <translation>이 다운로드는 아직 아무것도 도착하지 않았습니다.</translation>
    </message>
    <message numerus="yes">
        <location line="+3"/>
        <source>Reading the archive… %n file(s) found so far</source>
        <translation>
            <numerusform>압축 파일 읽는 중… 지금까지 파일 %n개 발견</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="+6"/>
        <source>%n file(s) in the archive.</source>
        <translation>
            <numerusform>압축 파일에 파일 %n개가 있습니다.</numerusform>
        </translation>
    </message>
    <message>
        <location line="+34"/>
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
        <source>Status</source>
        <translation>상태</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Reading the archive…</source>
        <translation>압축 파일 읽는 중…</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Preview</source>
        <translation>미리보기</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Keep Scanning</source>
        <translation>계속 검사</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>The connection to the core was lost.</source>
        <translation>코어와의 연결이 끊어졌습니다.</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>The archive is still being read — the files listed so far are all that is known.</source>
        <translation>압축 파일을 아직 읽는 중입니다 — 지금까지 표시된 파일이 현재 확인된 전부입니다.</translation>
    </message>
</context>
<context>
    <name>eMule::UsenetDetailsDialog</name>
    <message>
        <location filename="../src/gui/dialogs/UsenetDetailsDialog.cpp" line="+15"/>
        <location line="+18"/>
        <source>Release Details</source>
        <translation>릴리스 상세</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Not checked.</source>
        <translation>확인하지 않음.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>%1% of this release looks obtainable.</source>
        <translation>이 릴리스의 %1%를 받을 수 있는 것으로 보입니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1% by the NZB&apos;s own article counts. No server was asked.</source>
        <translation>NZB 자체의 기사 수 기준 %1%. 서버에는 문의하지 않았습니다.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>%1 of %2</source>
        <translation>%1 / %2</translation>
    </message>
    <message numerus="yes">
        <location line="+99"/>
        <source>%n article(s) were never listed in the NZB</source>
        <translation>
            <numerusform>기사 %n개가 NZB에 없었습니다</numerusform>
        </translation>
    </message>
    <message>
        <location line="+58"/>
        <source>&quot;%1&quot; has not been published on its own.</source>
        <translation>&quot;%1&quot;은(는) 개별적으로 게시되지 않았습니다.</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>&quot;%1&quot; is outside the core&apos;s Incoming folder and cannot be opened from here.</source>
        <translation>&quot;%1&quot;은(는) 코어의 수신 폴더 밖에 있어 여기에서 열 수 없습니다.</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Total Size:</source>
        <translation>전체 크기:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Date:</source>
        <translation>날짜:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status:</source>
        <translation>상태:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Health:</source>
        <translation>상태:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Poster:</source>
        <translation>게시자:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Articles:</source>
        <translation>기사:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Newsgroups:</source>
        <translation>뉴스그룹:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Files:</source>
        <translation>파일:</translation>
    </message>
    <message>
        <location line="+9"/>
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
        <source>Progress</source>
        <translation>진행률</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Articles</source>
        <translation>기사</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Missing</source>
        <translation>누락</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Status</source>
        <translation>상태</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Open Folder</source>
        <translation>폴더 열기</translation>
    </message>
</context>
<context>
    <name>eMule::UsenetFileCheckList</name>
    <message>
        <location filename="../src/gui/controls/UsenetFileCheckList.cpp" line="+53"/>
        <source>Select All</source>
        <translation>모두 선택</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Select None</source>
        <translation>모두 선택 해제</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Invert Selection</source>
        <translation>선택 반전</translation>
    </message>
    <message>
        <location line="+73"/>
        <source>Could not change which files download.</source>
        <translation>다운로드할 파일을 변경하지 못했습니다.</translation>
    </message>
</context>
<context>
    <name>eMule::UsenetPanel</name>
    <message>
        <location filename="../src/gui/panels/UsenetPanel.cpp" line="+166"/>
        <location line="+7"/>
        <location line="+44"/>
        <location line="+511"/>
        <source>Add NZB</source>
        <translation>NZB 추가</translation>
    </message>
    <message>
        <location line="-561"/>
        <location line="+50"/>
        <location line="+62"/>
        <source>Not connected to the eMule core.</source>
        <translation>eMule 코어에 연결되지 않았습니다.</translation>
    </message>
    <message>
        <location line="-105"/>
        <source>Cannot read %1.</source>
        <translation>%1을(를) 읽을 수 없습니다.</translation>
    </message>
    <message>
        <location line="+104"/>
        <source>Add NZB from URL</source>
        <translation>URL에서 NZB 추가</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Add NZB…</source>
        <translation>NZB 추가…</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+977"/>
        <source>Pause</source>
        <translation>일시정지</translation>
    </message>
    <message>
        <location line="-973"/>
        <location line="+976"/>
        <source>Resume</source>
        <translation>재개</translation>
    </message>
    <message>
        <location line="-974"/>
        <source>Remove</source>
        <translation>제거</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Clear Completed</source>
        <translation>완료됨 삭제</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+925"/>
        <source>Pause All</source>
        <translation>모두 일시정지</translation>
    </message>
    <message>
        <location line="-918"/>
        <source>Add NZB from URL…</source>
        <translation>URL에서 NZB 추가…</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Preview</source>
        <translation>미리보기</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Check Availability</source>
        <translation>가용성 확인</translation>
    </message>
    <message>
        <location line="+282"/>
        <source>Priority</source>
        <translation>우선순위</translation>
    </message>
    <message>
        <source>High</source>
        <translation type="vanished">높음</translation>
    </message>
    <message>
        <source>Normal</source>
        <translation type="vanished">보통</translation>
    </message>
    <message>
        <source>Low</source>
        <translation type="vanished">낮음</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Assign To Category</source>
        <translation>카테고리에 할당</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>No category</source>
        <translation>카테고리 없음</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Set Password…</source>
        <translation>비밀번호 설정…</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Download Selected Files</source>
        <translation>선택한 파일 다운로드</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Skip Selected Files</source>
        <translation>선택한 파일 건너뛰기</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Open File</source>
        <translation>파일 열기</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Open Folder</source>
        <translation>폴더 열기</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Details…</source>
        <translation>세부 정보…</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Remove and Delete Files</source>
        <translation>제거 및 파일 삭제</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>NZB files (*.nzb);;All files (*)</source>
        <translation>NZB 파일 (*.nzb);;모든 파일 (*)</translation>
    </message>
    <message>
        <location line="+34"/>
        <location line="+15"/>
        <source>Set Password</source>
        <translation>비밀번호 설정</translation>
    </message>
    <message>
        <location line="-14"/>
        <source>Archive password for &quot;%1&quot;:</source>
        <translation>&quot;%1&quot;의 압축 파일 비밀번호:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>this download</source>
        <translation>이 다운로드</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Remove the stored password for this download?</source>
        <translation>이 다운로드에 저장된 비밀번호를 삭제할까요?</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Could not set the password.</source>
        <translation>비밀번호를 설정하지 못했습니다.</translation>
    </message>
    <message>
        <location line="-349"/>
        <location line="+392"/>
        <source>Remove Downloads</source>
        <translation>다운로드 제거</translation>
    </message>
    <message numerus="yes">
        <location line="-392"/>
        <source>Remove %n download(s) from the list?</source>
        <translation>
            <numerusform>다운로드 %n개를 목록에서 제거하시겠습니까?</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="+393"/>
        <source>Remove %n download(s) and delete the files already fetched?</source>
        <translation>
            <numerusform>다운로드 %n개를 제거하고 이미 받은 파일을 삭제할까요?</numerusform>
        </translation>
    </message>
    <message>
        <location line="+63"/>
        <location line="+82"/>
        <source>Nothing has completed yet for &quot;%1&quot;.</source>
        <translation>&quot;%1&quot;은(는) 아직 완료된 것이 없습니다.</translation>
    </message>
    <message>
        <location line="+100"/>
        <source>Nothing here can be previewed yet.</source>
        <translation>아직 미리 볼 수 있는 항목이 없습니다.</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Preview is unavailable — the daemon&apos;s web server is not running.</source>
        <translation>미리보기를 사용할 수 없습니다 — 데몬의 웹 서버가 실행 중이 아닙니다.</translation>
    </message>
    <message>
        <location line="+58"/>
        <source>No Usenet downloads. Use &quot;Add NZB…&quot; to queue one.</source>
        <translation>Usenet 다운로드가 없습니다. &quot;NZB 추가…&quot;로 대기열에 추가하세요.</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>%1 download(s), %2 active — %3% complete</source>
        <translation>다운로드 %1개, 활성 %2개 — %3% 완료</translation>
    </message>
    <message>
        <location line="+9"/>
        <source> — %1</source>
        <translation> — %1</translation>
    </message>
    <message>
        <location line="+7"/>
        <source> — limited to %1 KB/s while eD2K downloads</source>
        <translation> — eD2K 다운로드 중에는 %1 KB/s로 제한</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Download limit %1 KB/s: Usenet up to %2 KB/s, eD2K up to %3 KB/s.
Whichever network is idle lends its share to the other.</source>
        <translation>다운로드 제한 %1 KB/s: Usenet 최대 %2 KB/s, eD2K 최대 %3 KB/s.
유휴 상태인 네트워크가 자신의 몫을 다른 쪽에 넘겨줍니다.</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Usenet: %1</source>
        <translation>Usenet: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Usenet: downloading again.</source>
        <translation>Usenet: 다시 다운로드합니다.</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Resume All</source>
        <translation>모두 재개</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Let every Usenet download continue</source>
        <translation>모든 Usenet 다운로드를 계속 진행합니다</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Stop starting new Usenet articles. Nothing is removed, and each release keeps its own state.</source>
        <translation>새 Usenet 기사를 더 이상 시작하지 않습니다. 아무것도 제거되지 않으며 각 릴리스는 자신의 상태를 그대로 유지합니다.</translation>
    </message>
    <message>
        <location line="+42"/>
        <location line="+4"/>
        <source>Cancel</source>
        <translation>취소</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove every Usenet download in &quot;%1&quot; and delete its files?</source>
        <translation>&quot;%1&quot;의 모든 Usenet 다운로드를 제거하고 파일을 삭제할까요?</translation>
    </message>
    <message>
        <location line="+40"/>
        <source>Could not apply that to the category: %1</source>
        <translation>카테고리에 적용할 수 없습니다: %1</translation>
    </message>
</context>
<context>
    <name>eMule::UsenetQueueModel</name>
    <message numerus="yes">
        <source>%n article(s) missing</source>
        <translation type="vanished">
            <numerusform>기사 %n개 누락</numerusform>
        </translation>
    </message>
    <message>
        <source>Complete</source>
        <translation type="vanished">완료</translation>
    </message>
    <message numerus="yes">
        <location filename="../src/gui/controls/UsenetQueueModel.cpp" line="+120"/>
        <source>%1% — %n article(s) missing</source>
        <translation>
            <numerusform>%1% — 기사 %n개 누락</numerusform>
        </translation>
    </message>
    <message>
        <location line="+41"/>
        <source>Skipped — tick it to download this file</source>
        <translation>건너뜀 — 이 파일을 다운로드하려면 체크하세요</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>%1% — %2 of %3 articles</source>
        <translation>%1% — 기사 %3개 중 %2개</translation>
    </message>
    <message numerus="yes">
        <location line="+4"/>
        <source>, %n missing</source>
        <translation>
            <numerusform>, %n개 누락</numerusform>
        </translation>
    </message>
    <message>
        <location line="+84"/>
        <source>Not checked.</source>
        <translation>확인하지 않음.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1% of this release looks obtainable.</source>
        <translation>이 릴리스의 %1%를 받을 수 있는 것으로 보입니다.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1% by the NZB&apos;s own article counts. No server was asked.</source>
        <translation>NZB 자체의 기사 수 기준 %1%. 서버에는 문의하지 않았습니다.</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>The PAR2 recovery volumes should cover the shortfall.</source>
        <translation>PAR2 복구 볼륨으로 부족한 부분을 보완할 수 있습니다.</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>%1
The password for this release did not work. Right-click to set a different one.</source>
        <translation>%1
이 릴리스의 비밀번호가 맞지 않습니다. 마우스 오른쪽 버튼을 눌러 다른 비밀번호를 설정하세요.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1
This release is password-protected. Right-click to set its password.</source>
        <translation>%1
이 릴리스는 비밀번호로 보호되어 있습니다. 마우스 오른쪽 버튼을 눌러 비밀번호를 설정하세요.</translation>
    </message>
    <message numerus="yes">
        <location line="+4"/>
        <source>%1
%n article(s) could not be found on any server</source>
        <translation>
            <numerusform>%1
기사 %n개를 어느 서버에서도 찾을 수 없습니다</numerusform>
        </translation>
    </message>
    <message>
        <location line="+4"/>
        <source>%1
A password is set for this release.</source>
        <translation>%1
이 릴리스에는 비밀번호가 설정되어 있습니다.</translation>
    </message>
    <message>
        <location line="+90"/>
        <source>Name</source>
        <translation>이름</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Size</source>
        <translation>크기</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Progress</source>
        <translation>진행률</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status</source>
        <translation>상태</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Speed</source>
        <translation>속도</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remaining</source>
        <translation>남은 시간</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority</source>
        <translation>우선순위</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Health</source>
        <translation>상태</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Category</source>
        <translation>카테고리</translation>
    </message>
</context>
<context>
    <name>eMule::VersionChecker</name>
    <message>
        <source>the version manifest is not a JSON object</source>
        <translation type="vanished">버전 매니페스트가 JSON 객체가 아닙니다</translation>
    </message>
    <message>
        <source>invalid JSON response: %1</source>
        <translation type="vanished">잘못된 JSON 응답: %1</translation>
    </message>
    <message>
        <source>the version manifest has no &apos;latest&apos; field</source>
        <translation type="vanished">버전 매니페스트에 &apos;latest&apos; 필드가 없습니다</translation>
    </message>
</context>
<context>
    <name>eMule::WebLinksDialog</name>
    <message>
        <location filename="../src/gui/dialogs/WebLinksDialog.cpp" line="+122"/>
        <source>Links: %1</source>
        <translation>링크: %1</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>The page says these files are password protected.</source>
        <translation>페이지에 따르면 이 파일들은 암호로 보호되어 있습니다.</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>File</source>
        <translation>파일</translation>
    </message>
    <message>
        <location line="+0"/>
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
        <source>Hoster</source>
        <translation>호스터</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Link</source>
        <translation>링크</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Status</source>
        <translation>상태</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+213"/>
        <source>Account</source>
        <translation>계정</translation>
    </message>
    <message>
        <location line="-192"/>
        <source>Links to .onion addresses are Tor hidden services. Copy them into Tor Browser: other browsers cannot open them.</source>
        <translation>.onion 주소 링크는 Tor 히든 서비스입니다. Tor Browser에 복사하세요. 다른 브라우저로는 열 수 없습니다.</translation>
    </message>
    <message>
        <location line="+15"/>
        <location line="+104"/>
        <location line="+13"/>
        <source>Download</source>
        <translation>다운로드</translation>
    </message>
    <message>
        <location line="-110"/>
        <source>Copy Links</source>
        <translation>링크 복사</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Open in Browser</source>
        <translation>브라우저에서 열기</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Save List...</source>
        <translation>목록 저장...</translation>
    </message>
    <message>
        <location line="+75"/>
        <source>Could not add the links of &quot;%1&quot;: downloads from hosters are off.</source>
        <translation>&quot;%1&quot;의 링크를 추가할 수 없습니다: 호스터 다운로드가 꺼져 있습니다.</translation>
    </message>
    <message numerus="yes">
        <location line="+3"/>
        <source>Added %n file(s) of &quot;%1&quot; to the Hoster tab.</source>
        <translation>
            <numerusform>&quot;%1&quot;의 파일 %n개를 호스터 탭에 추가했습니다.</numerusform>
        </translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Download (%1)</source>
        <translation>다운로드 (%1)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Download (all)</source>
        <translation>다운로드 (전체)</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Deselect All</source>
        <translation>모두 선택 해제</translation>
    </message>
    <message>
        <location line="+46"/>
        <source>Checking...</source>
        <translation>확인 중...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Online</source>
        <translation>온라인</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Offline</source>
        <translation>오프라인</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unknown</source>
        <translation>알 수 없음</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Premium account</source>
        <translation>프리미엄 계정</translation>
    </message>
    <message>
        <source>%1 · Premium account</source>
        <translation type="vanished">%1 · 프리미엄 계정</translation>
    </message>
    <message>
        <source>%1 · Account</source>
        <translation type="vanished">%1 · 계정</translation>
    </message>
    <message numerus="yes">
        <source>Added %n link(s) of &quot;%1&quot; to the Hoster tab.</source>
        <translation type="vanished">
            <numerusform>&quot;%1&quot;의 링크 %n개를 호스터 탭에 추가했습니다.</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="+19"/>
        <source>Copied %n link(s) to the clipboard.</source>
        <translation>
            <numerusform>링크 %n개를 클립보드에 복사했습니다.</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="+7"/>
        <source>Open %n links in the browser?</source>
        <translation>
            <numerusform>링크 %n개를 브라우저에서 열까요?</numerusform>
        </translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Save Link List</source>
        <translation>링크 목록 저장</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Link lists (*.txt);;All files (*)</source>
        <translation>링크 목록 (*.txt);;모든 파일 (*)</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Could not write %1: %2</source>
        <translation>%1을(를) 쓸 수 없습니다: %2</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Saved %1</source>
        <translation>%1 저장됨</translation>
    </message>
</context>
<context>
    <name>eMule::WebServer</name>
    <message>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+15"/>
        <source>(still scanning)</source>
        <translation>(검사 중)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Accepted</source>
        <translation>수락</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add</source>
        <translation>추가</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add NZB from URL…</source>
        <translation>URL에서 NZB 추가…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add NZB…</source>
        <translation>NZB 추가…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add paused</source>
        <translation>일시 정지 상태로 추가</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add to Static</source>
        <translation>고정 서버에 추가</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Adding %1 NZB(s)…</source>
        <translation>NZB %1개 추가 중…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Address</source>
        <translation>주소</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebServer.cpp" line="-3417"/>
        <location line="+3306"/>
        <location line="+49"/>
        <source>Session expired — log in again</source>
        <translation>세션이 만료되었습니다 — 다시 로그인하세요</translation>
    </message>
    <message>
        <location line="-3351"/>
        <source>Guests cannot add downloads</source>
        <translation>게스트는 다운로드를 추가할 수 없습니다</translation>
    </message>
    <message>
        <location line="+609"/>
        <source>Looking for comments on Kad</source>
        <translation>Kad에서 코멘트 검색 중</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Rating: %1</source>
        <translation>평가: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Has comments</source>
        <translation>코멘트 있음</translation>
    </message>
    <message>
        <location line="+404"/>
        <location line="+1"/>
        <source>Incoming</source>
        <translation>받은 파일</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Nothing has finished downloading yet.</source>
        <translation>아직 완료된 다운로드가 없습니다.</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Modified</source>
        <translation>수정한 날짜</translation>
    </message>
    <message>
        <location line="+40"/>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+36"/>
        <source>Download</source>
        <translation>다운로드</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Play</source>
        <translation>재생</translation>
    </message>
    <message>
        <location line="+94"/>
        <source>Open this URL in VLC or another player:</source>
        <translation>이 URL을 VLC 또는 다른 플레이어에서 여세요:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>This file is named %1 but its contents are %2. The name is wrong — common for files off the ed2k network — so a player that trusts it finds no %3 and sits at 0:00. It is being served as its real type, so it may still play above.</source>
        <translation>이 파일의 이름은 %1이지만 내용은 %2입니다. 이름이 잘못되었습니다(ed2k 네트워크의 파일에서 흔합니다). 그래서 이름을 믿는 플레이어는 %3을(를) 찾지 못하고 0:00에 멈춥니다. 실제 형식으로 제공되므로 위에서 재생될 수도 있습니다.</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>This file is named %1 but does not start with the %2 signature every one of them has, and its contents match no media container we recognise. It is very likely a fake or a corrupt download — no player will get anything out of it.</source>
        <translation>이 파일의 이름은 %1이지만 이 형식이라면 모두 가지는 %2 서명으로 시작하지 않으며, 내용도 알려진 미디어 컨테이너와 일치하지 않습니다. 가짜이거나 손상된 다운로드일 가능성이 매우 높으며, 어떤 플레이어로도 재생할 수 없습니다.</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>The raw URL, if you want to look for yourself:</source>
        <translation>직접 확인하려면 원본 URL:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Your browser probably cannot decode %1.</source>
        <translation>브라우저가 %1을(를) 디코딩하지 못할 수 있습니다.</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Copy</source>
        <translation>복사</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Copied</source>
        <translation>복사됨</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Download this file</source>
        <translation>이 파일 다운로드</translation>
    </message>
    <message>
        <location line="+62"/>
        <source>Access denied — no password configured. Set a password in Options → Web Interface.</source>
        <translation>접근 거부 — 비밀번호가 설정되지 않았습니다. 옵션 → 웹 인터페이스에서 비밀번호를 설정하세요.</translation>
    </message>
    <message numerus="yes">
        <location line="+11"/>
        <source>Too many failed logins. Try again in %n second(s).</source>
        <translation>
            <numerusform>로그인 실패가 너무 많습니다. %n초 후에 다시 시도하세요.</numerusform>
        </translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Login failed</source>
        <translation>로그인 실패</translation>
    </message>
    <message numerus="yes">
        <location line="+236"/>
        <source>%n download(s) added</source>
        <translation>
            <numerusform>다운로드 %n개를 추가했습니다</numerusform>
        </translation>
    </message>
    <message>
        <source>Not connected — the search could not be sent.</source>
        <translation type="vanished">연결되지 않음 — 검색을 보낼 수 없습니다.</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Not sent yet — the search is waiting (%1) and starts by itself.</source>
        <translation>아직 전송되지 않음 — 검색이 대기 중이며(%1) 자동으로 시작됩니다.</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Nothing was saved: a value is missing or out of range.</source>
        <translation>저장되지 않았습니다: 값이 없거나 범위를 벗어났습니다.</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Not saved: %1</source>
        <translation>저장되지 않음: %1</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Saved. Now listening on TCP %1, UDP %2.</source>
        <translation>저장되었습니다. 이제 TCP %1, UDP %2에서 수신 대기 중입니다.</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+27"/>
        <source>Saved. The new ports are used after a restart.</source>
        <translation>저장했습니다. 새 포트는 다시 시작한 후에 사용됩니다.</translation>
    </message>
    <message>
        <location line="-23"/>
        <source>Saved, but a new port is in use. Still listening on TCP %1, UDP %2.</source>
        <translation>저장되었지만 새 포트가 사용 중입니다. 계속 TCP %1, UDP %2에서 수신 대기 중입니다.</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+19"/>
        <source>Saved.</source>
        <translation>저장했습니다.</translation>
    </message>
    <message>
        <location line="+13"/>
        <location line="+1973"/>
        <source>Web Control Panel</source>
        <translation>웹 제어판</translation>
    </message>
    <message>
        <location line="-1965"/>
        <source>Not connected</source>
        <translation>연결되지 않음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Blocked: network interface not available</source>
        <translation>차단됨: 네트워크 인터페이스를 사용할 수 없음</translation>
    </message>
    <message>
        <location line="+299"/>
        <source>Connected</source>
        <translation>연결됨</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+335"/>
        <source>Disconnected</source>
        <translation>연결 해제됨</translation>
    </message>
    <message numerus="yes">
        <location line="-282"/>
        <source>&quot;%1&quot;: %n result(s)</source>
        <translation>
            <numerusform>&quot;%1&quot;: 결과 %n개</numerusform>
        </translation>
    </message>
    <message>
        <location line="+195"/>
        <source>Active Connections</source>
        <translation>활성 연결</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Time</source>
        <translation>시간</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Connected to: %1 (%2:%3)</source>
        <translation>연결됨: %1 (%2:%3)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Client ID: %1 (%2)</source>
        <translation>클라이언트 ID: %1 (%2)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>LowID</source>
        <translation>LowID</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>HighID</source>
        <translation>HighID</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Users: %1 | Files: %2</source>
        <translation>사용자: %1 | 파일: %2</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Description: %1</source>
        <translation>설명: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Ping: %1 ms</source>
        <translation>핑: %1 ms</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Connecting...</source>
        <translation>연결 중...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not connected to any server</source>
        <translation>어떤 서버에도 연결되지 않음</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Running</source>
        <translation>실행 중</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Not available</source>
        <translation>사용할 수 없음</translation>
    </message>
    <message>
        <location line="+149"/>
        <location line="+1128"/>
        <source>Queued</source>
        <translation>대기 중</translation>
    </message>
    <message>
        <location line="-1127"/>
        <location line="+1126"/>
        <source>Downloading</source>
        <translation>다운로드 중</translation>
    </message>
    <message>
        <location line="-1125"/>
        <source>Paused</source>
        <translation>일시정지됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+960"/>
        <location line="+163"/>
        <source>Complete</source>
        <translation>완료</translation>
    </message>
    <message>
        <location line="-1122"/>
        <source>Failed</source>
        <translation>실패</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Verifying</source>
        <translation>검증 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Repairing</source>
        <translation>복구 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unpacking</source>
        <translation>압축 해제 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Checking</source>
        <translation>확인 중</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unknown</source>
        <translation>알 수 없음</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>Not checked.</source>
        <translation>확인하지 않음.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1% of this release looks obtainable.</source>
        <translation>이 릴리스의 %1%를 받을 수 있는 것으로 보입니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1% by the NZB&apos;s own article counts. No server was asked.</source>
        <translation>NZB 자체의 기사 수 기준 %1%. 서버에는 문의하지 않았습니다.</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>The PAR2 recovery volumes should cover the shortfall.</source>
        <translation>PAR2 복구 볼륨으로 부족한 부분을 보완할 수 있습니다.</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>%1
The password for this release did not work. Right-click to set a different one.</source>
        <translation>%1
이 릴리스의 비밀번호가 맞지 않습니다. 마우스 오른쪽 버튼을 눌러 다른 비밀번호를 설정하세요.</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1
This release is password-protected. Right-click to set its password.</source>
        <translation>%1
이 릴리스는 비밀번호로 보호되어 있습니다. 마우스 오른쪽 버튼을 눌러 비밀번호를 설정하세요.</translation>
    </message>
    <message numerus="yes">
        <location line="+4"/>
        <source>%1
%n article(s) could not be found on any server</source>
        <translation>
            <numerusform>%1
기사 %n개를 어느 서버에서도 찾을 수 없습니다</numerusform>
        </translation>
    </message>
    <message>
        <location line="+4"/>
        <source>%1
A password is set for this release.</source>
        <translation>%1
이 릴리스에는 비밀번호가 설정되어 있습니다.</translation>
    </message>
    <message>
        <location line="+126"/>
        <source>%1 (and %2 other(s))</source>
        <translation>%1 (외 %2개)</translation>
    </message>
    <message>
        <location line="+60"/>
        <source>Usenet item not found</source>
        <translation>Usenet 항목을 찾을 수 없음</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Very high</source>
        <translation>매우 높음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>High</source>
        <translation>높음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Low</source>
        <translation>낮음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Very low</source>
        <translation>매우 낮음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Normal</source>
        <translation>보통</translation>
    </message>
    <message>
        <location line="+188"/>
        <source>Only a queued or downloading release can be paused</source>
        <translation>대기 중이거나 다운로드 중인 릴리스만 일시 정지할 수 있습니다</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Only a paused or failed release can be resumed</source>
        <translation>일시 정지되었거나 실패한 릴리스만 재개할 수 있습니다</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+110"/>
        <location line="+174"/>
        <source>priority must be a number from -2 to 2</source>
        <translation>우선순위는 -2에서 2 사이의 숫자여야 합니다</translation>
    </message>
    <message>
        <location line="-278"/>
        <location line="+110"/>
        <location line="+71"/>
        <location line="+90"/>
        <source>Unknown category</source>
        <translation>알 수 없는 카테고리</translation>
    </message>
    <message>
        <location line="-259"/>
        <location line="+4"/>
        <source>files must be a list of file numbers</source>
        <translation>files는 파일 번호 목록이어야 합니다</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Unknown action</source>
        <translation>알 수 없는 작업</translation>
    </message>
    <message>
        <location line="+228"/>
        <source>Post the .nzb as the request body, or give a url</source>
        <translation>.nzb를 요청 본문으로 보내거나 URL을 지정하세요</translation>
    </message>
    <message>
        <location line="+57"/>
        <source>Guests cannot change downloads</source>
        <translation>게스트는 다운로드를 변경할 수 없습니다</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Nothing selected</source>
        <translation>선택된 항목 없음</translation>
    </message>
    <message>
        <location line="+49"/>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="-35"/>
        <source>All</source>
        <translation>전체</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>No category</source>
        <translation>카테고리 없음</translation>
    </message>
    <message numerus="yes">
        <location line="+114"/>
        <location line="+163"/>
        <source>%n article(s) missing</source>
        <translation>
            <numerusform>기사 %n개 누락</numerusform>
        </translation>
    </message>
    <message>
        <location line="-99"/>
        <source>No Usenet downloads. Use &quot;Add NZB…&quot; to queue one.</source>
        <translation>Usenet 다운로드가 없습니다. &quot;NZB 추가…&quot;로 대기열에 추가하세요.</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>%1 download(s), %2 active — %3% complete</source>
        <translation>다운로드 %1개, 활성 %2개 — %3% 완료</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+2"/>
        <source> — %1</source>
        <translation> — %1</translation>
    </message>
    <message>
        <location line="+7"/>
        <source> — limited to %1 KB/s while eD2K downloads</source>
        <translation> — eD2K 다운로드 중에는 %1 KB/s로 제한</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Download limit %1 KB/s: Usenet up to %2 KB/s, eD2K up to %3 KB/s.
Whichever network is idle lends its share to the other.</source>
        <translation>다운로드 제한 %1 KB/s: Usenet 최대 %2 KB/s, eD2K 최대 %3 KB/s.
유휴 상태인 네트워크가 자신의 몫을 다른 쪽에 넘겨줍니다.</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>No Usenet downloads here.</source>
        <translation>여기에는 Usenet 다운로드가 없습니다.</translation>
    </message>
    <message numerus="yes">
        <location line="+48"/>
        <source>%n article(s) were never listed in the NZB</source>
        <translation>
            <numerusform>기사 %n개가 NZB에 없었습니다</numerusform>
        </translation>
    </message>
    <message>
        <location line="+40"/>
        <source>%1 of %2</source>
        <translation>%1 / %2</translation>
    </message>
    <message>
        <location line="+60"/>
        <source>App language (%1)</source>
        <translation>앱 언어 (%1)</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>Any</source>
        <translation>모든</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Apply</source>
        <translation>적용</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Archive (.zip .rar ...)</source>
        <translation>압축 파일 (.zip .rar ...)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Archive password for &quot;%1&quot;:</source>
        <translation>&quot;%1&quot;의 압축 파일 비밀번호:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Articles</source>
        <translation>기사</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Articles:</source>
        <translation>기사:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Audio (.mp3 .ogg ...)</source>
        <translation>오디오 (.mp3 .ogg ...)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Back to the queue</source>
        <translation>대기열로 돌아가기</translation>
    </message>
    <message>
        <source>CD Image (.iso .bin ...)</source>
        <translation type="vanished">CD 이미지 (.iso .bin ...)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Cancel</source>
        <translation>취소</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Cancel this download?</source>
        <translation>이 다운로드를 취소할까요?</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Category</source>
        <translation>카테고리</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Category:</source>
        <translation>카테고리:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Category: %1</source>
        <translation>카테고리: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Check Availability</source>
        <translation>가용성 확인</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Choose .nzb files or paste links first.</source>
        <translation>먼저 .nzb 파일을 선택하거나 링크를 붙여 넣으세요.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Clear Completed</source>
        <translation>완료됨 삭제</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Client</source>
        <translation>클라이언트</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Comment</source>
        <translation>댓글</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Comments</source>
        <translation>댓글</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Completed</source>
        <translation>완료됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Confidence</source>
        <translation>신뢰도</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Connect</source>
        <translation>연결</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Copy ED2K Link</source>
        <translation>ED2K 링크 복사</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Could not apply that to the category: %1</source>
        <translation>카테고리에 적용할 수 없습니다: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Could not reach the eMule core.</source>
        <translation>eMule 코어에 연결할 수 없습니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Date:</source>
        <translation>날짜:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Debug</source>
        <translation>디버그</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Debug Log</source>
        <translation>디버그 로그</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Description</source>
        <translation>설명</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Details…</source>
        <translation>세부 정보…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Disconnect</source>
        <translation>연결 해제</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Document (.doc .pdf ...)</source>
        <translation>문서 (.doc .pdf ...)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Download Speed</source>
        <translation>다운로드 속도</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebServer.cpp" line="-1453"/>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>Downloads</source>
        <translation>다운로드</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>File</source>
        <translation>파일</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>File Name</source>
        <translation>파일 이름</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Files</source>
        <translation>파일</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Files:</source>
        <translation>파일:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>General</source>
        <translation>일반</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Global</source>
        <translation>전역</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Graphs</source>
        <translation>그래프</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hash</source>
        <translation>해시</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Health</source>
        <translation>상태</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Health:</source>
        <translation>상태:</translation>
    </message>
    <message>
        <source>Image (.jpg .png ...)</source>
        <translation type="vanished">이미지 (.jpg .png ...)</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebServer.cpp" line="+4"/>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>KB/s</source>
        <translation>KB/s</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>Kad</source>
        <translation>Kad</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Kad Network</source>
        <translation>Kad 네트워크</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Kademlia</source>
        <translation>Kademlia</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Keep Scanning</source>
        <translation>계속 검사</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Language</source>
        <translation>언어</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Log</source>
        <translation>로그</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Login</source>
        <translation>로그인</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Logout</source>
        <translation>로그아웃</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Max Download (KB/s)</source>
        <translation>최대 다운로드 (KB/s)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Max Download Speed</source>
        <translation>최대 다운로드 속도</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Max Upload (KB/s)</source>
        <translation>최대 업로드 (KB/s)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Max Upload Speed</source>
        <translation>최대 업로드 속도</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Method</source>
        <translation>방식</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Missing</source>
        <translation>누락</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>My Info</source>
        <translation>내 정보</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>NZB URLs:</source>
        <translation>NZB URL:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>NZB files:</source>
        <translation>NZB 파일:</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebServer.cpp" line="-1215"/>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>Name</source>
        <translation>이름</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="-57"/>
        <source>CD-Image (.iso .bin ...)</source>
        <translation>CD 이미지 (.iso .bin ...)</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Collection</source>
        <translation>컬렉션</translation>
    </message>
    <message>
        <location line="+48"/>
        <source>Newsgroups:</source>
        <translation>뉴스그룹:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nickname</source>
        <translation>닉네임</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nickname:</source>
        <translation>닉네임:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nothing has completed yet for &quot;%1&quot;.</source>
        <translation>&quot;%1&quot;은(는) 아직 완료된 것이 없습니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nothing here can be previewed yet.</source>
        <translation>아직 미리 볼 수 있는 항목이 없습니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Open File</source>
        <translation>파일 열기</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Open Folder</source>
        <translation>폴더 열기</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Password</source>
        <translation>비밀번호</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Password:</source>
        <translation>비밀번호:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paste one or more http(s) links to .nzb files here, one per line...</source>
        <translation>.nzb 파일에 대한 http(s) 링크를 한 줄에 하나씩 여기에 붙여 넣으세요...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Pause</source>
        <translation>일시정지</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Pause All</source>
        <translation>모두 일시정지</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Picture (.jpg .png ...)</source>
        <translation>그림 (.jpg .png ...)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ping</source>
        <translation>Ping</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Playable</source>
        <translation>재생 가능</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Please add at most %1 links at a time.</source>
        <translation>한 번에 최대 %1개의 링크만 추가하세요.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Port</source>
        <translation>포트</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ports</source>
        <translation>포트</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Poster:</source>
        <translation>게시자:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Preferences</source>
        <translation>환경설정</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Preview</source>
        <translation>미리보기</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Preview File</source>
        <translation>미리 볼 파일</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority</source>
        <translation>우선순위</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority:</source>
        <translation>우선순위:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority: %1</source>
        <translation>우선순위: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority: Auto</source>
        <translation>우선순위: 자동</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority: High</source>
        <translation>우선순위: 높음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority: Low</source>
        <translation>우선순위: 낮음</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority: Normal</source>
        <translation>우선순위: 보통</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Program (.exe ...)</source>
        <translation>프로그램 (.exe ...)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Progress</source>
        <translation>진행률</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Queued %1 NZB(s).</source>
        <translation>NZB %1개를 대기열에 추가했습니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Rating</source>
        <translation>평가</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Recheck Firewall</source>
        <translation>방화벽 재확인</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Reconnects</source>
        <translation>재연결</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Refresh</source>
        <translation>새로 고침</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remaining</source>
        <translation>남은 시간</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove</source>
        <translation>제거</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove %1 download(s) and delete the files already fetched?</source>
        <translation>다운로드 %1개를 제거하고 이미 받은 파일을 삭제할까요?</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove and Delete Files</source>
        <translation>제거 및 파일 삭제</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove every Usenet download in &quot;%1&quot; and delete its files?</source>
        <translation>&quot;%1&quot;의 모든 Usenet 다운로드를 제거하고 파일을 삭제할까요?</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove from Static</source>
        <translation>고정 서버에서 제거</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove the stored password for this download?</source>
        <translation>이 다운로드에 저장된 비밀번호를 삭제할까요?</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Requests</source>
        <translation>요청</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Resume</source>
        <translation>재개</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Resume All</source>
        <translation>모두 재개</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Search</source>
        <translation>검색</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server</source>
        <translation>서버</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server Info</source>
        <translation>서버 정보</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server List</source>
        <translation>서버 목록</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server Name</source>
        <translation>서버 이름</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Session Received</source>
        <translation>받음 (세션)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Session Sent</source>
        <translation>보냄 (세션)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Session Statistics</source>
        <translation>세션 통계</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Set Password…</source>
        <translation>비밀번호 설정…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Shared</source>
        <translation>공유됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Shared Files</source>
        <translation>공유 파일</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebServer.cpp" line="+0"/>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>Size</source>
        <translation>크기</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>Sources</source>
        <translation>소스</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Speed</source>
        <translation>속도</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Speed Limits</source>
        <translation>속도 제한</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Statistics</source>
        <translation>통계</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status</source>
        <translation>상태</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status:</source>
        <translation>상태:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Stop</source>
        <translation>중지</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>TCP Port</source>
        <translation>TCP 포트</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>That did not work.</source>
        <translation>작업에 실패했습니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>These could not be added: %1</source>
        <translation>다음 항목을 추가할 수 없습니다: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>This archive cannot be previewed.</source>
        <translation>이 압축 파일은 미리 볼 수 없습니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>This download is no longer in the queue.</source>
        <translation>이 다운로드는 더 이상 대기열에 없습니다.</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total</source>
        <translation>합계</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Size:</source>
        <translation>전체 크기:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Transfer</source>
        <translation>전송</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Transferred</source>
        <translation>전송됨</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type</source>
        <translation>유형</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>UDP Port</source>
        <translation>UDP 포트</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload Speed</source>
        <translation>업로드 속도</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebServer.cpp" line="+1212"/>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>Uploads</source>
        <translation>업로드</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>Uptime</source>
        <translation>가동 시간</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Usenet</source>
        <translation>Usenet</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebServer.cpp" line="+486"/>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>Usenet engine unavailable</source>
        <translation>Usenet 엔진을 사용할 수 없음</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>User</source>
        <translation>사용자</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>User Information</source>
        <translation>사용자 정보</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>User Name</source>
        <translation>사용자 이름</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Users</source>
        <translation>사용자</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Video (.avi .mkv ...)</source>
        <translation>동영상 (.avi .mkv ...)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>You have already downloaded %1 of these. Download them again?</source>
        <translation>이 중 %1개는 이미 다운로드했습니다. 다시 다운로드할까요?</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>this download</source>
        <translation>이 다운로드</translation>
    </message>
</context>
</TS>
