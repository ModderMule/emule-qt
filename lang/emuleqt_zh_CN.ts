<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_CN">
<context>
    <name>ContainerSniffer</name>
    <message>
        <location filename="../src/core/media/ContainerSniffer.cpp" line="+210"/>
        <source>Named .%1 but matches no media container we recognise — very likely a fake.</source>
        <translation>文件名为 .%1，但与我们能识别的任何媒体容器都不匹配 — 极可能是假文件。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Named .%1 but the contents are %2.</source>
        <translation>文件名为 .%1，但内容实际是 %2。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Only the extension is wrong — most players still open it.</source>
        <translation>只是扩展名不对——大多数播放器仍能打开。</translation>
    </message>
</context>
<context>
    <name>Ed2kLinkImporter</name>
    <message>
        <location filename="../src/gui/utils/Ed2kLinkImporter.cpp" line="+211"/>
        <source>already shared</source>
        <translation>已共享</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>already downloading</source>
        <translation>已在下载中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>already downloaded</source>
        <translation>已下载</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>previously cancelled</source>
        <translation>先前已取消</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>You already have the file &quot;%1&quot;.</source>
        <translation>您已拥有文件 &quot;%1&quot;。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>You are already trying to download the file &quot;%1&quot;.</source>
        <translation>您已经在尝试下载文件 &quot;%1&quot;。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>You previously cancelled the download of &quot;%1&quot;.</source>
        <translation>您先前取消了 &quot;%1&quot; 的下载。</translation>
    </message>
    <message numerus="yes">
        <location line="+172"/>
        <source>%n further HTTP Cache link(s) ignored — apply one at a time.</source>
        <translation>
            <numerusform>已忽略另外 %n 个 HTTP 缓存链接 — 请一次应用一个。</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="+104"/>
        <source>%n eD2K link(s) not added — already known</source>
        <translation>
            <numerusform>%n 个 eD2K 链接未添加 — 已知</numerusform>
        </translation>
    </message>
    <message>
        <location line="+9"/>
        <source>eD2K Link</source>
        <translation>eD2K 链接</translation>
    </message>
</context>
<context>
    <name>HttpCacheLinkImporter</name>
    <message>
        <location filename="../src/gui/utils/HttpCacheLinkImporter.cpp" line="+44"/>
        <source>The core did not answer.</source>
        <translation>核心没有响应。</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>Server: %1</source>
        <translation>服务器：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Key: %1</source>
        <translation>密钥：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Version %1%2</source>
        <translation>版本 %1%2</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>This server also accepts uploads without a key.</source>
        <translation>此服务器也接受不带密钥的上传。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>This replaces the entry already stored for %1.</source>
        <translation>这将替换已为 %1 保存的条目。</translation>
    </message>
    <message numerus="yes">
        <location line="+3"/>
        <source>Uploads are shared across your cache servers; this makes %n of them.</source>
        <translation>
            <numerusform>上传会分散到各个缓存服务器；这样将共有 %n 个。</numerusform>
        </translation>
    </message>
    <message>
        <location line="+10"/>
        <source>

This link uses plain HTTP. The key and every chunk address will cross the network unencrypted.</source>
        <translation>

此链接使用明文 HTTP。密钥和每个块的地址都将以未加密的方式在网络上传输。</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Update your HTTP Cache settings for &quot;%1&quot;?</source>
        <translation>要更新 &quot;%1&quot; 的 HTTP 缓存设置吗？</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Add &quot;%1&quot; as an HTTP Cache server?</source>
        <translation>要将 &quot;%1&quot; 添加为 HTTP 缓存服务器吗？</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Use &quot;%1&quot; as your HTTP Cache server?</source>
        <translation>要将 &quot;%1&quot; 用作您的 HTTP 缓存服务器吗？</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+26"/>
        <source>HTTP Cache</source>
        <translation>HTTP 缓存</translation>
    </message>
    <message>
        <location line="-23"/>
        <source>

HTTP Cache will be enabled and this key stored for uploads.</source>
        <translation>

将启用 HTTP 缓存，并保存此密钥用于上传。</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>HTTP Cache link refused: %1</source>
        <translation>已拒绝 HTTP 缓存链接：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+29"/>
        <source>HTTP Cache link refused</source>
        <translation>已拒绝 HTTP 缓存链接</translation>
    </message>
    <message>
        <location line="-16"/>
        <location line="+2"/>
        <source>HTTP Cache is already configured for %1.</source>
        <translation>%1 的 HTTP 缓存已配置。</translation>
    </message>
    <message numerus="yes">
        <location line="+10"/>
        <source>You already have %n HTTP Cache server(s) configured. Remove one from preferences.yml before adding another.</source>
        <translation>
            <numerusform>您已配置 %n 个 HTTP 缓存服务器。请先从 preferences.yml 中移除一个，再添加新的服务器。</numerusform>
        </translation>
    </message>
    <message>
        <location line="+17"/>
        <source>HTTP Cache configuration for %1 was not applied.</source>
        <translation>未应用 %1 的 HTTP 缓存配置。</translation>
    </message>
    <message>
        <location line="+22"/>
        <location line="+2"/>
        <source>HTTP Cache configured for %1.</source>
        <translation>已为 %1 配置 HTTP 缓存。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>HTTP Cache configuration failed: %1</source>
        <translation>HTTP 缓存配置失败：%1</translation>
    </message>
</context>
<context>
    <name>IpcFeedback</name>
    <message>
        <location filename="../src/gui/utils/IpcFeedback.cpp" line="+28"/>
        <source>The request was rejected by eMule.</source>
        <translation>请求被 eMule 拒绝。</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Not connected to daemon — %1</source>
        <translation>未连接到守护进程 — %1</translation>
    </message>
</context>
<context>
    <name>MassRenameAction</name>
    <message>
        <location filename="../src/gui/utils/MassRenameAction.cpp" line="+22"/>
        <location line="+19"/>
        <location line="+12"/>
        <source>Mass Rename</source>
        <translation>批量重命名</translation>
    </message>
    <message numerus="yes">
        <location line="+1"/>
        <source>%n file(s) could not be renamed:</source>
        <translation>
            <numerusform>%n 个文件无法重命名:</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>Notifier</name>
    <message>
        <location filename="../src/gui/utils/NotifierText.h" line="+30"/>
        <source>Downloaded:</source>
        <translation>已下载：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Connection lost</source>
        <translation>连接已断开</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Out of disk space</source>
        <translation>磁盘空间不足</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>You have insufficient disk space to download &quot;%1&quot;!</source>
        <translation>磁盘空间不足,无法下载“%1”!</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Port not available</source>
        <translation>端口不可用</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Fatal Error: Unable to create socket on port %1</source>
        <translation>严重错误:无法在端口 %1 上创建套接字</translation>
    </message>
</context>
<context>
    <name>PortChange</name>
    <message>
        <location filename="../src/gui/dialogs/PortChangeNotice.h" line="+42"/>
        <source>Now listening on TCP port %1 and UDP port %2.</source>
        <translation>现在监听 TCP 端口 %1 和 UDP 端口 %2。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The new ports take effect after restarting eMule, because the core is connected to a network or to other clients.

Until then it keeps listening on TCP port %1 and UDP port %2.</source>
        <translation>由于核心已连接到网络或其他客户端，新端口将在重新启动 eMule 后生效。

在此之前，它继续监听 TCP 端口 %1 和 UDP 端口 %2。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>The new port could not be opened. It may be in use by another program.

The core keeps listening on TCP port %1 and UDP port %2.</source>
        <translation>无法打开新端口。它可能正被其他程序使用。

核心继续监听 TCP 端口 %1 和 UDP 端口 %2。</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Ports</source>
        <translation>端口</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Restart Now</source>
        <translation>立即重启</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Later</source>
        <translation>稍后</translation>
    </message>
</context>
<context>
    <name>PortMapStatus</name>
    <message>
        <location filename="../src/gui/dialogs/PortMapStatusText.h" line="+37"/>
        <source>Ports forwarded via %1.</source>
        <translation>已通过 %1 转发端口。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ports forwarded via %1 (external address %2).</source>
        <translation>已通过 %1 转发端口（外部地址 %2）。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>The router granted the ports, but they are not reachable from the Internet.</source>
        <translation>路由器已授予端口，但无法从互联网访问。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The router granted the ports, but its address %1 is not public, so they stay unreachable over IPv4.</source>
        <translation>路由器已授予端口，但其地址 %1 不是公网地址，因此通过 IPv4 仍无法访问。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>No router answered PCP, NAT-PMP or UPnP. Forward the ports manually.</source>
        <translation>没有路由器响应 PCP、NAT-PMP 或 UPnP。请手动转发端口。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>The router refused the port forwarding.</source>
        <translation>路由器拒绝了端口转发。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Automatic port forwarding is switched off.</source>
        <translation>自动端口转发已关闭。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Asking the router...</source>
        <translation>正在询问路由器...</translation>
    </message>
</context>
<context>
    <name>PortTest</name>
    <message>
        <location filename="../src/gui/dialogs/PortTest.h" line="+82"/>
        <source>reachable</source>
        <translation>可访问</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>not reachable: closed, nothing is listening on this port</source>
        <translation>无法访问：已关闭，此端口上没有任何程序在监听</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>not reachable: rejected by a firewall</source>
        <translation>无法访问：被防火墙拒绝</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>not reachable: no answer (firewall or missing port forwarding)</source>
        <translation>无法访问：无响应（防火墙或缺少端口转发）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>not tested: the test server may not send to this port</source>
        <translation>未测试：测试服务器不能向此端口发送</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>not tested: the test server cannot reach this IPv6 address</source>
        <translation>未测试：测试服务器无法访问此 IPv6 地址</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>not tested: the test packet could not be sent</source>
        <translation>未测试：无法发送测试数据包</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>not tested: the address is not public</source>
        <translation>未测试：该地址不是公网地址</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>not tested: too many tests, try again in a minute</source>
        <translation>未测试：测试次数过多，请一分钟后重试</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>not reachable (%1)</source>
        <translation>无法访问（%1）</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>BitTorrent is not running, so its port cannot be tested.</source>
        <translation>BitTorrent 未运行，因此无法测试其端口。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The ed3k node is not running, so its port cannot be tested.</source>
        <translation>ed3k 节点未运行，因此无法测试其端口。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The ed3k node could not make a test packet.</source>
        <translation>ed3k 节点无法生成测试数据包。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The test server emule-qt.org did not answer.</source>
        <translation>测试服务器 emule-qt.org 没有响应。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The test server refused the test.</source>
        <translation>测试服务器拒绝了测试。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The test server cannot test this port yet. Please try again later.</source>
        <translation>测试服务器暂时无法测试此端口。请稍后重试。</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+90"/>
        <source>The core did not run the test.</source>
        <translation>核心没有执行测试。</translation>
    </message>
    <message>
        <location line="-59"/>
        <source>TCP: %1</source>
        <translation>TCP：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>UDP: %1</source>
        <translation>UDP：%1</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>BitTorrent port %1. TCP and UDP must both be reachable from the internet.</source>
        <translation>BitTorrent 端口 %1。TCP 和 UDP 都必须可从互联网访问。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Both TCP and UDP are reachable.</source>
        <translation>TCP 和 UDP 均可访问。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>TCP and UDP are both reachable over one address family only.</source>
        <translation>TCP 和 UDP 仅能通过一种地址族访问。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>TCP and UDP are not both reachable. Other peers can still be reached, but fewer can connect to you.</source>
        <translation>TCP 和 UDP 并非都可访问。仍可连接其他节点，但能连接到您的节点会减少。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>ed3k UDP port %1. Only UDP must be reachable: the ed3k node uses no TCP port.</source>
        <translation>ed3k UDP 端口 %1。只需 UDP 可访问：ed3k 节点不使用 TCP 端口。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>UDP is reachable.</source>
        <translation>UDP 可访问。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>UDP is reachable over one address family only.</source>
        <translation>UDP 仅能通过一种地址族访问。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>UDP is not reachable. The node then finds fewer peers and relies on hole punching.</source>
        <translation>UDP 无法访问。节点会找到较少的节点，并依赖打洞。</translation>
    </message>
    <message>
        <location line="+10"/>
        <location line="+19"/>
        <location line="+4"/>
        <location line="+5"/>
        <source>Test Ports</source>
        <translation>测试端口</translation>
    </message>
    <message>
        <location line="-28"/>
        <source>Not connected to the core.</source>
        <translation>未连接到核心。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Testing...</source>
        <translation>正在测试...</translation>
    </message>
</context>
<context>
    <name>PreviewLauncher</name>
    <message>
        <location filename="../src/gui/utils/PreviewLauncher.cpp" line="+319"/>
        <source>Not connected to the core.</source>
        <translation>未连接到核心。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The core has not sent its stream token yet. It arrives with the next status update — try again in a moment.</source>
        <translation>核心尚未发送流令牌。它会随下一次状态更新到达 — 请稍后重试。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>The core runs on another machine and its web server only listens on localhost.

Enable Web Interface or REST API under Options → Web Interface.</source>
        <translation>核心运行在另一台机器上，其 Web 服务器仅监听 localhost。

请在“选项 → Web 界面”中启用 Web 界面或 REST API。</translation>
    </message>
</context>
<context>
    <name>Priority</name>
    <message>
        <location filename="../src/gui/utils/PriorityText.cpp" line="+20"/>
        <source>Very Low</source>
        <translation>非常低</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Auto [Lo]</source>
        <translation>自动 [低]</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Low</source>
        <translation>低</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Auto [Hi]</source>
        <translation>自动 [高]</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>High</source>
        <translation>高</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Release</source>
        <translation>发布</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Very High</source>
        <translation>非常高</translation>
    </message>
    <message>
        <location line="-24"/>
        <source>Auto [No]</source>
        <translation>自动 [普]</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Normal</source>
        <translation>普通</translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <location filename="../src/gui/utils/Ed2kLinkImporter.cpp" line="-375"/>
        <location line="+20"/>
        <source>eD2K Link</source>
        <translation>eD2K 链接</translation>
    </message>
    <message>
        <location line="-19"/>
        <source>Do you want to download the following file(s)?

%1</source>
        <translation>是否要下载以下文件？

%1</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>You have already downloaded the following file(s). Download them again?

%1</source>
        <translation>您已经下载过以下文件。要重新下载吗？

%1</translation>
    </message>
    <message>
        <location filename="../src/gui/app/main.cpp" line="+487"/>
        <source>eMule Qt</source>
        <translation>eMule Qt</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>eMule Qt is already running with this configuration but does not respond.

Start it with --new-instance or --config &lt;dir&gt; to open another window.</source>
        <translation>eMule Qt 已使用此配置运行，但没有响应。

使用 --new-instance 或 --config &lt;目录&gt; 启动以打开另一个窗口。</translation>
    </message>
    <message>
        <location line="+202"/>
        <source>Core Certificate Changed</source>
        <translation>核心证书已更改</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The core at %1 shows a different certificate than the one trusted before. This happens when its ipc-cert.pem was regenerated, but can also mean someone is intercepting the connection.

Trusted: %2
Presented: %3

Trust the new certificate?</source>
        <translation>位于 %1 的核心提供的证书与之前信任的证书不同。重新生成其 ipc-cert.pem 时会出现这种情况,但也可能意味着有人正在拦截连接。

已信任: %2
当前提供: %3

信任新证书吗?</translation>
    </message>
    <message>
        <location line="+299"/>
        <source>Download Added</source>
        <translation>下载已添加</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>A new download has been added.</source>
        <translation>已添加新的下载。</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Message from %1</source>
        <translation>来自 %1 的消息</translation>
    </message>
    <message>
        <source>Chat Message from %1</source>
        <translation type="vanished">来自 %1 的聊天消息</translation>
    </message>
    <message>
        <source>Log Entry</source>
        <translation type="vanished">日志条目</translation>
    </message>
    <message>
        <source>Connection Lost</source>
        <translation type="vanished">连接丢失</translation>
    </message>
    <message>
        <source>Server connection has been lost.</source>
        <translation type="vanished">服务器连接已丢失。</translation>
    </message>
    <message>
        <source>Very Low</source>
        <translation type="vanished">非常低</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/UsenetQueueModel.cpp" line="+201"/>
        <source>Skipped</source>
        <translation>已跳过</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Held back — fetched if a repair needs it</source>
        <translation>暂缓获取 — 仅在修复需要时才下载</translation>
    </message>
    <message numerus="yes">
        <location line="+2"/>
        <location line="+4"/>
        <source>%n article(s) missing</source>
        <translation>
            <numerusform>缺少 %n 篇文章</numerusform>
        </translation>
    </message>
    <message>
        <location line="-3"/>
        <source>Complete</source>
        <translation>已完成</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Queued</source>
        <translation>排队中</translation>
    </message>
    <message>
        <location line="+430"/>
        <source>Very high</source>
        <translation>非常高</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Low</source>
        <translation>低</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Very low</source>
        <translation>非常低</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Normal</source>
        <translation>普通</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>High</source>
        <translation>高</translation>
    </message>
    <message>
        <source>Very High</source>
        <translation type="vanished">非常高</translation>
    </message>
    <message>
        <source>Auto [%1]</source>
        <translation type="vanished">自动 [%1]</translation>
    </message>
    <message>
        <location filename="../src/gui/utils/RatingIcons.cpp" line="+59"/>
        <source>Fake</source>
        <translation>伪造</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Poor</source>
        <translation>差</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Fair</source>
        <translation>一般</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Good</source>
        <translation>良好</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Excellent</source>
        <translation>优秀</translation>
    </message>
    <message>
        <location line="+113"/>
        <source>
Rating:	%1</source>
        <translation>
评分：	%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>
Has comments</source>
        <translation>
有评论</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/ClientListModel.cpp" line="+56"/>
        <source>Server</source>
        <translation>服务器</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Kad</source>
        <translation>Kad</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Source Exch.</source>
        <translation>来源交换</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/gui/controls/DownloadListModel.cpp" line="+78"/>
        <source>Passive</source>
        <translation>被动</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/gui/controls/DownloadListModel.cpp" line="+1"/>
        <source>Link</source>
        <translation>链接</translation>
    </message>
    <message>
        <location line="+2"/>
        <location filename="../src/gui/controls/DownloadListModel.cpp" line="+2"/>
        <source>HTTP Cache</source>
        <translation>HTTP 缓存</translation>
    </message>
    <message>
        <source>Yes</source>
        <translation type="vanished">是</translation>
    </message>
    <message>
        <source>Never</source>
        <translation type="vanished">从不</translation>
    </message>
    <message>
        <source>Archive</source>
        <translation type="vanished">压缩包</translation>
    </message>
    <message>
        <source>Audio</source>
        <translation type="vanished">音频</translation>
    </message>
    <message>
        <source>Video</source>
        <translation type="vanished">视频</translation>
    </message>
    <message>
        <source>Image</source>
        <translation type="vanished">图片</translation>
    </message>
    <message>
        <source>Program</source>
        <translation type="vanished">程序</translation>
    </message>
    <message>
        <source>Document</source>
        <translation type="vanished">文档</translation>
    </message>
    <message>
        <source>CD-Image</source>
        <translation type="vanished">CD 镜像</translation>
    </message>
    <message>
        <source>eMule Collection</source>
        <translation type="vanished">eMule 合集</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/DownloadListModel.cpp" line="-6"/>
        <source>eD2K Server</source>
        <translation>eD2K服务器</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Kademlia</source>
        <translation>Kademlia</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Source Exchange</source>
        <translation>来源交换</translation>
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
        <translation>未知</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/KnownTypeStyle.h" line="+24"/>
        <source>Shared</source>
        <translation>已共享</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/gui/controls/UsenetQueueModel.cpp" line="-433"/>
        <source>Downloading</source>
        <translation>正在下载</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloaded</source>
        <translation>已下载</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Cancelled</source>
        <translation>已取消</translation>
    </message>
    <message>
        <location filename="../src/gui/panels/StatisticsPanel.cpp" line="+351"/>
        <source>Total Overhead (Packets): 0 Bytes (0)</source>
        <translation>总开销（数据包）：0 Bytes (0)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>File Request Overhead (Packets): 0 Bytes (0)</source>
        <translation>文件请求开销（数据包）：0 Bytes (0)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Source Exchange Overhead (Packets): 0 Bytes (0)</source>
        <translation>来源交换开销（数据包）：0 Bytes (0)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server Overhead (Packets): 0 Bytes (0)</source>
        <translation>服务器开销（数据包）：0 Bytes (0)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Kad Overhead (Packets): 0 Bytes (0)</source>
        <translation>Kad 开销（数据包）：0 Bytes (0)</translation>
    </message>
    <message>
        <source>%1 Bytes</source>
        <translation type="vanished">%1 Bytes</translation>
    </message>
    <message>
        <location filename="../src/gui/dialogs/OptionsDialog.cpp" line="+3407"/>
        <source>Test</source>
        <translation>测试</translation>
    </message>
    <message>
        <location filename="../src/gui/utils/FileAssociation.cpp" line="+480"/>
        <source>Could not write the file association to the registry.</source>
        <translation>无法将文件关联写入注册表。</translation>
    </message>
    <message>
        <source>Could not remove the file association from the registry.</source>
        <translation type="vanished">无法从注册表中移除文件关联。</translation>
    </message>
    <message>
        <location line="-75"/>
        <source>Not started from the app bundle.</source>
        <translation>未从应用程序包启动。</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>LaunchServices refused %1.</source>
        <translation>LaunchServices 拒绝了 %1。</translation>
    </message>
    <message>
        <location line="+85"/>
        <source>No writable data directory.</source>
        <translation>没有可写的数据目录。</translation>
    </message>
    <message>
        <location line="+34"/>
        <source>Could not create %1.</source>
        <translation>无法创建 %1。</translation>
    </message>
    <message>
        <location filename="../src/gui/utils/NzbAdd.cpp" line="+25"/>
        <location filename="../src/gui/utils/TorrentAdd.cpp" line="+37"/>
        <source>Could not add &quot;%1&quot;.</source>
        <translation>无法添加“%1”。</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+42"/>
        <location line="+10"/>
        <source>Add NZB</source>
        <translation>添加 NZB</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1

Download it again?</source>
        <translation>%1

要重新下载吗？</translation>
    </message>
    <message>
        <location filename="../src/gui/utils/TorrentAdd.cpp" line="-13"/>
        <source>Torrent &quot;%1&quot; added.</source>
        <translation>已添加种子“%1”。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>&quot;%1&quot; is already in the torrent list.</source>
        <translation>“%1”已在种子列表中。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>&quot;%1&quot; is not a valid torrent or magnet link.</source>
        <translation>“%1”不是有效的种子或磁力链接。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&quot;%1&quot; is not a valid torrent or magnet link.

%2</source>
        <translation>“%1”不是有效的种子或磁力链接。

%2</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>BitTorrent is off — enable it in Options to add &quot;%1&quot;.</source>
        <translation>BitTorrent 已关闭 — 请在选项中启用以添加“%1”。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Could not add &quot;%1&quot;.

%2</source>
        <translation>无法添加“%1”。

%2</translation>
    </message>
    <message>
        <location line="+73"/>
        <source>Add Torrent</source>
        <translation>添加种子</translation>
    </message>
</context>
<context>
    <name>Rating</name>
    <message>
        <location filename="../src/core/utils/OtherFunctions.cpp" line="+528"/>
        <source>Not rated</source>
        <translation>未评分</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Invalid / Corrupt / Fake</source>
        <translation>无效 / 损坏 / 假文件</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Poor</source>
        <translation>差</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Fair</source>
        <translation>一般</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Good</source>
        <translation>良好</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Excellent</source>
        <translation>优秀</translation>
    </message>
</context>
<context>
    <name>RenameFileDialog</name>
    <message>
        <location filename="../src/gui/dialogs/RenameFileDialog.h" line="+23"/>
        <source>Rename</source>
        <translation>重命名</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>File name:</source>
        <translation>文件名：</translation>
    </message>
</context>
<context>
    <name>Units</name>
    <message>
        <location filename="../src/core/utils/StringUtils.cpp" line="+72"/>
        <location filename="../src/core/webserver/WebServer.cpp" line="+4083"/>
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
        <translation>秒</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>mins</source>
        <translation>分</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>h</source>
        <translation>时</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>d</source>
        <translation>天</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Seconds</source>
        <translation>秒</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Minutes</source>
        <translation>分钟</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hours</source>
        <translation>小时</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Days</source>
        <translation>天</translation>
    </message>
</context>
<context>
    <name>UsenetDetailsDialog</name>
    <message numerus="yes">
        <location filename="../src/gui/dialogs/UsenetDetailsDialog.cpp" line="+66"/>
        <source>%1 (and %n other(s))</source>
        <translation>
            <numerusform>%1（另有 %n 个）</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n article(s) missing</source>
        <translation type="vanished">
            <numerusform>缺少 %n 篇文章</numerusform>
        </translation>
    </message>
    <message>
        <source>Complete</source>
        <translation type="vanished">已完成</translation>
    </message>
    <message>
        <source>Downloading</source>
        <translation type="vanished">正在下载</translation>
    </message>
    <message>
        <source>Queued</source>
        <translation type="vanished">排队中</translation>
    </message>
</context>
<context>
    <name>eMule</name>
    <message>
        <source>Unknown</source>
        <translation type="vanished">未知</translation>
    </message>
    <message>
        <source>Server</source>
        <translation type="vanished">服务器</translation>
    </message>
    <message>
        <source>Rating</source>
        <translation type="vanished">评分</translation>
    </message>
    <message>
        <source>Asked for another file</source>
        <translation type="vanished">已请求其他文件</translation>
    </message>
    <message>
        <source>Country: %1</source>
        <translation type="obsolete">国家: %1</translation>
    </message>
    <message>
        <source>Downloaded:</source>
        <translation type="vanished">已下载：</translation>
    </message>
</context>
<context>
    <name>eMule::AddFriendDialog</name>
    <message>
        <location filename="../src/gui/dialogs/AddFriendDialog.cpp" line="+28"/>
        <source>Add...</source>
        <translation>添加...</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Required Information</source>
        <translation>必填信息</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>IP Address:</source>
        <translation>IP 地址：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Port:</source>
        <translation>端口：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Additional Information</source>
        <translation>附加信息</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Name:</source>
        <translation>名称：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Hash:</source>
        <translation>哈希：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Unknown</source>
        <translation>未知</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>KadID:</source>
        <translation>Kad ID：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>ed3k</source>
        <translation>ed3k</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>ed3k link:</source>
        <translation>ed3k 链接：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Request text (optional)</source>
        <translation>请求文本（可选）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Message:</source>
        <translation>消息：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Last Seen:</source>
        <translation>最后出现：</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Add</source>
        <translation>添加</translation>
    </message>
    <message>
        <location line="+102"/>
        <source>Details</source>
        <translation>详情</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>User ID:</source>
        <translation>用户 ID:</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>You have to enter a valid IP and port!</source>
        <translation>必须输入有效的 IP 和端口!</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Friend not added.

There is already a friend with same IP address and port available.</source>
        <translation>未添加好友。

已存在具有相同 IP 地址和端口的好友。</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>This is not an ed3k friend link.</source>
        <translation>这不是 ed3k 好友链接。</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Friend not added.

%1</source>
        <translation>未添加好友。

%1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>The ed3k node is not running.</source>
        <translation>ed3k 节点未运行。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The ed3k link is invalid.</source>
        <translation>ed3k 链接无效。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The message is too long (at most 96 bytes).</source>
        <translation>消息过长（最多 96 字节）。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>This is your own ed3k link.</source>
        <translation>这是你自己的 ed3k 链接。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Too many ed3k friends (at most 64).</source>
        <translation>ed3k 好友过多（最多 64 个）。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>ed3k friends are switched off.</source>
        <translation>ed3k 好友功能已关闭。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>ed3k error %1.</source>
        <translation>ed3k 错误 %1。</translation>
    </message>
    <message>
        <source>Friend Details</source>
        <translation type="vanished">好友详情</translation>
    </message>
    <message>
        <location line="-87"/>
        <source>Never</source>
        <translation>从不</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Close</source>
        <translation>关闭</translation>
    </message>
    <message>
        <location line="+19"/>
        <location line="+24"/>
        <location line="+13"/>
        <location line="+13"/>
        <source>Add Friend</source>
        <translation>添加好友</translation>
    </message>
    <message>
        <source>Please enter an IP address.</source>
        <translation type="vanished">请输入 IP 地址。</translation>
    </message>
    <message>
        <source>Please enter a valid port (1-65535).</source>
        <translation type="vanished">请输入有效端口（1-65535）。</translation>
    </message>
</context>
<context>
    <name>eMule::AddNzbFilesDialog</name>
    <message>
        <location filename="../src/gui/dialogs/AddNzbFilesDialog.cpp" line="+15"/>
        <source>Add NZB</source>
        <translation>添加 NZB</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>NZB files:</source>
        <translation>NZB 文件：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Add</source>
        <translation>添加</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Password:</source>
        <translation>密码：</translation>
    </message>
    <message>
        <location line="+18"/>
        <location line="+31"/>
        <source>Choose Files…</source>
        <translation>选择文件…</translation>
    </message>
    <message numerus="yes">
        <location line="-1"/>
        <source>Choose Files… (%n skipped)</source>
        <translation>
            <numerusform>选择文件…（已跳过 %n 个）</numerusform>
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
        <translation>从 URL 添加 NZB</translation>
    </message>
    <message>
        <location line="-130"/>
        <source>NZB URLs:</source>
        <translation>NZB 链接：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paste one or more http(s) links to .nzb files here, one per line...</source>
        <translation>在此粘贴一个或多个指向 .nzb 文件的 http(s) 链接，每行一个...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Download</source>
        <translation>下载</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Password:</source>
        <translation>密码：</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Not Connected</source>
        <translation>未连接</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not connected to the eMule core.</source>
        <translation>未连接到 eMule 核心。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Please add at most %1 links at a time.</source>
        <translation>每次最多添加 %1 个链接。</translation>
    </message>
    <message numerus="yes">
        <location line="+23"/>
        <source>Queued %n NZB(s) from URL.</source>
        <translation>
            <numerusform>已从 URL 将 %n 个 NZB 加入队列。</numerusform>
        </translation>
    </message>
    <message>
        <location line="+8"/>
        <source>These links could not be added:

%1</source>
        <translation>以下链接无法添加：

%1</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Could not reach the eMule core.</source>
        <translation>无法连接到 eMule 核心。</translation>
    </message>
    <message numerus="yes">
        <location line="+42"/>
        <source>You have already downloaded %n of these. Download them again?

%1</source>
        <translation>
            <numerusform>其中 %n 个您已下载过。要重新下载吗？

%1</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>eMule::AddSourceDialog</name>
    <message>
        <location filename="../src/gui/dialogs/AddSourceDialog.cpp" line="+23"/>
        <source>Add Sources</source>
        <translation>添加来源</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add Sources (%1)</source>
        <translation>添加来源 (%1)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Source Type</source>
        <translation>来源类型</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Source</source>
        <translation>来源</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>User IP</source>
        <translation>用户 IP</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Port</source>
        <translation>端口</translation>
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
        <translation>添加</translation>
    </message>
</context>
<context>
    <name>eMule::AddTorrentDialog</name>
    <message>
        <location filename="../src/gui/dialogs/AddTorrentDialog.cpp" line="+29"/>
        <source>Add Magnet Links</source>
        <translation>添加磁力链接</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add Torrent Files</source>
        <translation>添加种子文件</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Magnet links:</source>
        <translation>磁力链接：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Torrent files:</source>
        <translation>种子文件：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Paste one or more magnet:?xt=urn:btih:… links here, one per line...</source>
        <translation>在此粘贴一个或多个 magnet:?xt=urn:btih:… 链接，每行一个...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Download</source>
        <translation>下载</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Save to:</source>
        <translation>保存到：</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>Not Connected</source>
        <translation>未连接</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Not connected to the eMule core.</source>
        <translation>未连接到 eMule 核心。</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>These lines are not BitTorrent magnet links:

%1</source>
        <translation>以下各行不是 BitTorrent 磁力链接：

%1</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Cannot read:

%1</source>
        <translation>无法读取：

%1</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Please add at most %1 torrents at a time.</source>
        <translation>每次最多添加 %1 个种子。</translation>
    </message>
    <message numerus="yes">
        <location line="+39"/>
        <source>Added %n torrent(s).</source>
        <translation>
            <numerusform>已添加 %n 个种子。</numerusform>
        </translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Could not reach the eMule core.</source>
        <translation>无法连接到 eMule 核心。</translation>
    </message>
</context>
<context>
    <name>eMule::ArchivePreviewPanel</name>
    <message>
        <source>Scanning...</source>
        <translation type="vanished">正在扫描...</translation>
    </message>
    <message>
        <location filename="../src/gui/dialogs/ArchivePreviewPanel.cpp" line="+179"/>
        <location line="+76"/>
        <source>Archive type: --</source>
        <translation>压缩包类型：--</translation>
    </message>
    <message>
        <location line="-75"/>
        <location line="+79"/>
        <source>Ready</source>
        <translation>就绪</translation>
    </message>
    <message>
        <location line="-23"/>
        <source>Archive type: %1</source>
        <translation>压缩包类型：%1</translation>
    </message>
    <message>
        <source>No entries found or unsupported format</source>
        <translation type="vanished">未找到条目或格式不支持</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Files: %1</source>
        <translation>文件：%1</translation>
    </message>
    <message>
        <source>Create Preview Copy</source>
        <translation type="vanished">创建预览副本</translation>
    </message>
    <message>
        <location line="-160"/>
        <source>Scanning archive...</source>
        <translation>正在扫描压缩包...</translation>
    </message>
    <message>
        <location line="+39"/>
        <location line="+35"/>
        <location line="+17"/>
        <source>Password protection</source>
        <translation>密码保护</translation>
    </message>
    <message>
        <location line="-53"/>
        <source>bootable</source>
        <translation>可启动的</translation>
    </message>
    <message>
        <location line="+16"/>
        <location line="+38"/>
        <source>Archive scanned.</source>
        <translation>压缩包扫描完成。</translation>
    </message>
    <message>
        <location line="-37"/>
        <location line="+37"/>
        <source>File list may be incomplete.</source>
        <translation>文件列表可能不完整。</translation>
    </message>
    <message>
        <location line="-36"/>
        <location line="+15"/>
        <source>Headers encrypted - unable to read archive.</source>
        <translation>文件头已加密 - 无法读取压缩包。</translation>
    </message>
    <message>
        <location line="-14"/>
        <location line="+6"/>
        <source>Insufficient data available.</source>
        <translation>可用数据不足。</translation>
    </message>
    <message>
        <location line="-5"/>
        <location line="+14"/>
        <source>Table of contents not found.</source>
        <translation>未找到目录。</translation>
    </message>
    <message>
        <location line="+85"/>
        <source>(unknown/unsupported)</source>
        <translation>(未知/不支持)</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Create preview file</source>
        <translation>创建预览文件</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Update</source>
        <translation>更新</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>CRC</source>
        <translation>CRC</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Attributes</source>
        <translation>属性</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Last Modified</source>
        <translation>最后修改</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Info:</source>
        <translation>信息:</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Archive Preview</source>
        <translation>压缩包预览</translation>
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
        <translation>文件属性可包含:

P	文件受密码保护
D	是目录
C	注释
Lx	压缩级别 x
M	压缩数据尚未完全下载
&lt;	文件接续自上一分卷
&gt;	文件在下一分卷继续
</translation>
    </message>
    <message>
        <location line="-202"/>
        <location line="+161"/>
        <source>Comment</source>
        <translation>评论</translation>
    </message>
</context>
<context>
    <name>eMule::BugReportDialog</name>
    <message>
        <location filename="../src/gui/dialogs/BugReportDialog.cpp" line="+55"/>
        <location line="+139"/>
        <location line="+128"/>
        <source>Submit Bug Report</source>
        <translation>提交错误报告</translation>
    </message>
    <message>
        <location line="-262"/>
        <source>Report Details</source>
        <translation>报告详情</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Bug Report</source>
        <translation>错误报告</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Feature Request</source>
        <translation>功能请求</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type:</source>
        <translation>类型：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Brief summary of the issue</source>
        <translation>问题简述</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Title:</source>
        <translation>标题：</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+4"/>
        <source>(optional)</source>
        <translation>（可选）</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>Name:</source>
        <translation>名称：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Email:</source>
        <translation>电子邮件：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Description</source>
        <translation>描述</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Describe the issue in detail...</source>
        <translation>详细描述问题...</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Attachments</source>
        <translation>附件</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Screenshots:</source>
        <translation>屏幕截图：</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Add...</source>
        <translation>添加...</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+14"/>
        <source>Remove</source>
        <translation>删除</translation>
    </message>
    <message>
        <location line="-6"/>
        <source>Crash Dump:</source>
        <translation>崩溃转储：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Browse...</source>
        <translation>浏览...</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Alternatively, you can submit bug reports at &lt;a href=&quot;%1&quot;&gt;emule-qt.org/submit-bug-report&lt;/a&gt;</source>
        <translation>您也可以在 &lt;a href=&quot;%1&quot;&gt;emule-qt.org/submit-bug-report&lt;/a&gt; 提交错误报告</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Submit</source>
        <translation>提交</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Please fill in both the title and description fields.</source>
        <translation>请填写标题和描述两项。</translation>
    </message>
    <message>
        <location line="+123"/>
        <source>Bug report submitted successfully.</source>
        <translation>错误报告已成功提交。</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Error: %1</source>
        <translation>错误：%1</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Select Screenshots</source>
        <translation>选择屏幕截图</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp)</source>
        <translation>图片 (*.png *.jpg *.jpeg *.bmp *.gif *.webp)</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Invalid Files</source>
        <translation>无效文件</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The following files are not valid images and were skipped:
%1</source>
        <translation>以下文件不是有效的图片，已跳过：
%1</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Select Crash Dump</source>
        <translation>选择崩溃转储</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Dump Files (*.dmp *.crash *.txt);;All Files (*)</source>
        <translation>转储文件 (*.dmp *.crash *.txt);;所有文件 (*)</translation>
    </message>
</context>
<context>
    <name>eMule::CategoryDialog</name>
    <message>
        <location filename="../src/gui/dialogs/CategoryDialog.cpp" line="+36"/>
        <source>Edit Category-Properties</source>
        <translation>编辑分类属性</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Title</source>
        <translation>标题</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Comments</source>
        <translation>评论</translation>
    </message>
    <message>
        <location line="+13"/>
        <location line="+79"/>
        <source>Choose a folder for incoming files</source>
        <translation>选择接收文件的文件夹</translation>
    </message>
    <message>
        <location line="-75"/>
        <source>Incoming Files  (Folder will be shared!)</source>
        <translation>接收文件  （文件夹将被共享！）</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Low</source>
        <translation>低</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Normal</source>
        <translation>普通</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>High</source>
        <translation>高</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Priority for this category</source>
        <translation>此分类的优先级</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>More Colors...</source>
        <translation>更多颜色...</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+59"/>
        <source>Color</source>
        <translation>颜色</translation>
    </message>
    <message>
        <location line="-56"/>
        <source>Auto cat. assignment (separate patterns with |)</source>
        <translation>自动分类分配（用 | 分隔多个模式）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>As Regular Expression</source>
        <translation>作为正则表达式</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Shows only the files whose whole name matches. Clearing it turns the filter off again.</source>
        <translation>仅显示整个名称匹配的文件。清空后过滤器将再次关闭。</translation>
    </message>
    <message>
        <source>Used when the tab&apos;s view filter is set to &quot;Regular Expression&quot;; it must match the whole file name.</source>
        <translation type="vanished">当标签页的视图过滤器设为 &quot;正则表达式&quot; 时使用；必须匹配整个文件名。</translation>
    </message>
    <message>
        <source>Stored for compatibility — the per-category view filter is not implemented yet.</source>
        <translation type="vanished">仅为兼容性保存 — 按分类的视图过滤器尚未实现。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Regular expression for view filter:</source>
        <translation>视图过滤器的正则表达式：</translation>
    </message>
    <message>
        <location line="+55"/>
        <source>A category needs a title.</source>
        <translation>分类需要一个标题。</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Invalid folder. Folder can not be created. Please check name and location.</source>
        <translation>无效的文件夹。无法创建该文件夹。请检查名称和位置。</translation>
    </message>
    <message>
        <location line="+10"/>
        <location line="+6"/>
        <source>Bad regular expression</source>
        <translation>无效的正则表达式</translation>
    </message>
    <message>
        <location line="-110"/>
        <location line="+151"/>
        <source>Default</source>
        <translation>默认</translation>
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
        <translation>全部</translation>
    </message>
    <message>
        <location line="-113"/>
        <source>Cat %1</source>
        <translation>分类 %1</translation>
    </message>
    <message>
        <location line="+77"/>
        <source>Uncategorized</source>
        <translation>未分类</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Incomplete</source>
        <translation>未完成</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Completed</source>
        <translation>已完成</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Waiting</source>
        <translation>等待中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloading</source>
        <translation>正在下载</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Erroneous</source>
        <translation>出错</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paused</source>
        <translation>已暂停</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ever Seen Complete</source>
        <translation>曾见过完整</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Video</source>
        <translation>视频</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Audio</source>
        <translation>音频</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Archive</source>
        <translation>压缩包</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>CD-Image</source>
        <translation>CD 镜像</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Document</source>
        <translation>文档</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Picture</source>
        <translation>图片</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Program</source>
        <translation>程序</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Regular Expression</source>
        <translation>正则表达式</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Collection</source>
        <translation>合集</translation>
    </message>
    <message>
        <location line="+69"/>
        <source>Select View Filter</source>
        <translation>选择视图过滤器</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Evaluate All Files</source>
        <translation>评估所有文件</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Negate Filter</source>
        <translation>反转过滤器</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Category</source>
        <translation>分类</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Category (%1)</source>
        <translation>分类 (%1)</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Open Incoming Folder</source>
        <translation>打开接收文件夹</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Add Category...</source>
        <translation>添加分类...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Edit Category...</source>
        <translation>编辑分类...</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+164"/>
        <source>Remove Category</source>
        <translation>移除分类</translation>
    </message>
    <message>
        <location line="-61"/>
        <source>Could not save categories: %1</source>
        <translation>无法保存分类：%1</translation>
    </message>
    <message>
        <location line="+62"/>
        <source>Remove the category &quot;%1&quot;?

Its downloads keep their files and move to All.</source>
        <translation>移除分类“%1”？

其下载会保留文件并移至“全部”。</translation>
    </message>
</context>
<context>
    <name>eMule::ClientDetailDialog</name>
    <message>
        <location filename="../src/gui/dialogs/ClientDetailDialog.cpp" line="+125"/>
        <location line="+30"/>
        <source>Client Details: %1</source>
        <translation>客户端详情：%1</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>General</source>
        <translation>常规</translation>
    </message>
    <message>
        <source>User Name</source>
        <translation type="vanished">用户名</translation>
    </message>
    <message>
        <source>User Hash</source>
        <translation type="vanished">用户哈希</translation>
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
        <translation type="vanished">客户端软件</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Country</source>
        <translation>国家</translation>
    </message>
    <message>
        <source>Server</source>
        <translation type="vanished">服务器</translation>
    </message>
    <message>
        <location line="-18"/>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Hash</source>
        <translation>哈希</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Software</source>
        <translation>软件</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>Server IP</source>
        <translation>服务器 IP</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server Name</source>
        <translation>服务器名称</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Identification</source>
        <translation>识别</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Successful</source>
        <translation>成功</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Invalid</source>
        <translation>无效</translation>
    </message>
    <message>
        <location line="-23"/>
        <location line="+24"/>
        <source>Not supported or disabled</source>
        <translation>不支持或已禁用</translation>
    </message>
    <message>
        <location line="-27"/>
        <source>Obfuscation</source>
        <translation>混淆</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Enabled</source>
        <translation>已启用</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Supported</source>
        <translation>支持</translation>
    </message>
    <message>
        <location line="+31"/>
        <location line="+2"/>
        <source>Connected (%1)</source>
        <translation>已连接 (%1)</translation>
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
        <translation>IPv6 备用</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Not supported</source>
        <translation>不支持</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Supported (%1)</source>
        <translation>支持 (%1)</translation>
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
        <translation>已连接</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Disconnected</source>
        <translation>已断开</translation>
    </message>
    <message>
        <location line="+66"/>
        <source>Currently downloading</source>
        <translation>当前正在下载</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Requested to upload</source>
        <translation>请求上传</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Downloaded (this session)</source>
        <translation>已下载 (本次会话)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Uploaded (this session)</source>
        <translation>已上传 (本次会话)</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Average Downloadrate</source>
        <translation>平均下载速度</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Average Upload rate</source>
        <translation>平均上传速度</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Downloaded total</source>
        <translation>总下载量</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Uploaded total</source>
        <translation>总上传量</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Rating (total)</source>
        <translation>评分 (总计)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>(Friend Slot)</source>
        <translation>(好友位)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Uploadqueue Score</source>
        <translation>上传队列得分</translation>
    </message>
    <message>
        <source>Not connected</source>
        <translation type="vanished">未连接</translation>
    </message>
    <message>
        <location line="-66"/>
        <source>Transfer</source>
        <translation>传输</translation>
    </message>
    <message>
        <source>Currently Downloading</source>
        <translation type="vanished">当前正在下载</translation>
    </message>
    <message>
        <source>Currently Uploading</source>
        <translation type="vanished">当前正在上传</translation>
    </message>
    <message>
        <source>Downloaded (Session)</source>
        <translation type="vanished">已下载（会话）</translation>
    </message>
    <message>
        <source>Uploaded (Session)</source>
        <translation type="vanished">已上传（会话）</translation>
    </message>
    <message>
        <source>Download Rate</source>
        <translation type="vanished">下载速率</translation>
    </message>
    <message>
        <source>Downloaded (Total)</source>
        <translation type="vanished">已下载（总计）</translation>
    </message>
    <message>
        <source>Uploaded (Total)</source>
        <translation type="vanished">已上传（总计）</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Scores</source>
        <translation>评分</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>DL/UP Modifier</source>
        <translation>下载/上传修正系数</translation>
    </message>
    <message>
        <source>Rating (Total)</source>
        <translation type="vanished">评分（总计）</translation>
    </message>
    <message>
        <source>Upload Queue Score</source>
        <translation type="vanished">上传队列评分</translation>
    </message>
    <message>
        <source>Friend Slot</source>
        <translation type="vanished">好友位</translation>
    </message>
    <message>
        <source>Yes</source>
        <translation type="vanished">是</translation>
    </message>
</context>
<context>
    <name>eMule::ClientListModel</name>
    <message>
        <location filename="../src/gui/controls/ClientListModel.cpp" line="+327"/>
        <location line="+13"/>
        <location line="+13"/>
        <location line="+15"/>
        <source>User Name</source>
        <translation>用户名</translation>
    </message>
    <message>
        <location line="-40"/>
        <location line="+14"/>
        <location line="+12"/>
        <source>File</source>
        <translation>文件</translation>
    </message>
    <message>
        <location line="-25"/>
        <location line="+14"/>
        <source>Speed</source>
        <translation>速度</translation>
    </message>
    <message>
        <location line="-13"/>
        <source>Transferred</source>
        <translation>已传输</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Waited</source>
        <translation>已等待</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload Time</source>
        <translation>上传时间</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status</source>
        <translation>状态</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+28"/>
        <source>Obtained Parts</source>
        <translation>已获取部分</translation>
    </message>
    <message>
        <location line="-21"/>
        <location line="+32"/>
        <source>Software</source>
        <translation>软件</translation>
    </message>
    <message>
        <location line="-29"/>
        <source>Available Parts</source>
        <translation>可用部分</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+24"/>
        <source>Transferred Up</source>
        <translation>已上传</translation>
    </message>
    <message>
        <location line="-23"/>
        <source>Source Type</source>
        <translation>来源类型</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>File Priority</source>
        <translation>文件优先级</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Rating</source>
        <translation>评分</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Score</source>
        <translation>得分</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Asked</source>
        <translation>已请求</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Last Seen</source>
        <translation>最后出现</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Entered Queue</source>
        <translation>进入队列</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Banned</source>
        <translation>已封禁</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Upload Status</source>
        <translation>上传状态</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Download Status</source>
        <translation>下载状态</translation>
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
        <translation>已下载</translation>
    </message>
    <message>
        <location line="-305"/>
        <source>%1 (ed3k)</source>
        <translation>%1 (ed3k)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>place %1</source>
        <translation>第 %1 位</translation>
    </message>
    <message>
        <location line="+48"/>
        <source>Country</source>
        <translation>国家</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Shared List</source>
        <translation>共享列表</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Uploaded</source>
        <translation>已上传</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloaded</source>
        <translation>已下载</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ratio</source>
        <translation>分享率</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Session Ratio</source>
        <translation>本次会话分享率</translation>
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
        <location line="-316"/>
        <location line="+159"/>
        <location line="+15"/>
        <source>Yes</source>
        <translation>是</translation>
    </message>
    <message>
        <location line="-174"/>
        <location line="+159"/>
        <location line="+15"/>
        <source>No</source>
        <translation>否</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Unknown</source>
        <translation>未知</translation>
    </message>
    <message>
        <location line="+125"/>
        <source>Connected</source>
        <translation>已连接</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hash</source>
        <translation>哈希</translation>
    </message>
</context>
<context>
    <name>eMule::ClientSharedFilesDialog</name>
    <message>
        <source>Shared Files — %1</source>
        <translation type="vanished">共享文件 — %1</translation>
    </message>
    <message>
        <source>File Name</source>
        <translation type="vanished">文件名</translation>
    </message>
    <message>
        <source>Size</source>
        <translation type="vanished">大小</translation>
    </message>
    <message>
        <source>Hash</source>
        <translation type="vanished">哈希</translation>
    </message>
    <message>
        <source>Download Selected</source>
        <translation type="vanished">下载所选</translation>
    </message>
    <message>
        <source>Close</source>
        <translation type="vanished">关闭</translation>
    </message>
</context>
<context>
    <name>eMule::ClientState</name>
    <message>
        <location filename="../src/gui/utils/ClientStateText.h" line="+68"/>
        <source>Unknown</source>
        <translation>未知</translation>
    </message>
</context>
<context>
    <name>eMule::CollectionCreateDialog</name>
    <message>
        <location filename="../src/gui/dialogs/CollectionCreateDialog.cpp" line="+57"/>
        <source>Modify Collection...</source>
        <translation>修改合集...</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Create Collection...</source>
        <translation>创建合集...</translation>
    </message>
    <message>
        <source>Shared (0)</source>
        <translation type="vanished">已共享 (0)</translation>
    </message>
    <message>
        <location line="+20"/>
        <location line="+36"/>
        <source>File Name</source>
        <translation>文件名</translation>
    </message>
    <message>
        <location line="-36"/>
        <location line="+36"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location line="-36"/>
        <location line="+36"/>
        <source>Hash</source>
        <translation>哈希</translation>
    </message>
    <message>
        <location line="-20"/>
        <source>Add to collection</source>
        <translation>添加到合集</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Remove from collection</source>
        <translation>从合集中移除</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Collection List (0)</source>
        <translation>合集列表 (0)</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Basic Options</source>
        <translation>基本选项</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Name:</source>
        <translation>名称：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Advanced Options</source>
        <translation>高级选项</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Save collection in plain text format</source>
        <translation>以纯文本格式保存合集</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Sign collection with name and key</source>
        <translation>使用名称和密钥签名合集</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Save</source>
        <translation>保存</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="-95"/>
        <location line="+197"/>
        <source>Shared (%1)</source>
        <translation>已共享 (%1)</translation>
    </message>
    <message>
        <location line="-195"/>
        <source>Switch between the shared files and every file known</source>
        <translation>在共享文件和所有已知文件之间切换</translation>
    </message>
    <message>
        <location line="+195"/>
        <source>Known (%1)</source>
        <translation>已知 (%1)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Collection List (%1)</source>
        <translation>合集列表 (%1)</translation>
    </message>
    <message>
        <location line="+26"/>
        <location line="+6"/>
        <location line="+42"/>
        <location line="+9"/>
        <source>Collection</source>
        <translation>合集</translation>
    </message>
    <message>
        <location line="-57"/>
        <source>Please enter a collection name.</source>
        <translation>请输入合集名称。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Collection is empty. Add files first.</source>
        <translation>合集为空。请先添加文件。</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Do you want to replace existing file?</source>
        <translation>是否要替换已存在的文件？</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Failed to save collection: %1</source>
        <translation>保存合集失败：%1</translation>
    </message>
</context>
<context>
    <name>eMule::CollectionViewDialog</name>
    <message>
        <location filename="../src/gui/dialogs/CollectionViewDialog.cpp" line="+43"/>
        <source>Collection: %1</source>
        <translation>合集：%1</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Collection List (%1)</source>
        <translation>合集列表 (%1)</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>File Name</source>
        <translation>文件名</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Hash</source>
        <translation>哈希</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Details</source>
        <translation>详情</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Author:</source>
        <translation>作者：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Author Key:</source>
        <translation>作者密钥：</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Options</source>
        <translation>选项</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Add new downloads into the collection category</source>
        <translation>将新下载添加到集合的分类中</translation>
    </message>
    <message>
        <source>Add to new category</source>
        <translation type="vanished">添加到新分类</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Download</source>
        <translation>下载</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Close</source>
        <translation>关闭</translation>
    </message>
</context>
<context>
    <name>eMule::CommentEditPanel</name>
    <message>
        <location filename="../src/gui/dialogs/CommentEditPanel.cpp" line="+70"/>
        <source>Comment This File! (This text will be shown to all users.)</source>
        <translation>评论此文件！（此文本将显示给所有用户。）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>For a film, you can say its length, its story, the language... And if it is a fake, you can inform other eMule users...</source>
        <translation>对于影片，您可以说明其时长、剧情、语言... 如果它是假文件，您可以告知其他 eMule 用户...</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>File Quality</source>
        <translation>文件质量</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Choose the file rating or advice users if the file is invalid!</source>
        <translation>选择文件评分，或在文件无效时提醒其他用户！</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Reset</source>
        <translation>重置</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Apply</source>
        <translation>应用</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Comment</source>
        <translation>评论</translation>
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
        <translation>（Kad 搜索中...）</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+55"/>
        <source>Search Kad</source>
        <translation>搜索 Kad</translation>
    </message>
    <message>
        <location line="-37"/>
        <source>Rating</source>
        <translation>评分</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Comment</source>
        <translation>评论</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>File Name</source>
        <translation>文件名</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>User Name</source>
        <translation>用户名</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Network</source>
        <translation>网络</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>No comments or ratings available for this file.</source>
        <translation>此文件没有可用的评论或评分。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Copy</source>
        <translation>复制</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Edit spam filter...</source>
        <translation>编辑垃圾信息过滤器...</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Edit spam filter for comments</source>
        <translation>编辑评论垃圾信息过滤器</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ignore comments containing: (Separator | )</source>
        <translation>忽略包含以下内容的评论：（分隔符 | ）</translation>
    </message>
</context>
<context>
    <name>eMule::ConfidenceText</name>
    <message>
        <location filename="../src/core/search/ConfidenceText.h" line="+27"/>
        <source>Spam</source>
        <translation>垃圾</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Likely fake</source>
        <translation>很可能是假文件</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Suspect</source>
        <translation>可疑</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Caution: %1%</source>
        <translation>注意：%1%</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Looks good</source>
        <translation>看起来正常</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Genuine</source>
        <translation>可信</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Shared under names that describe different content</source>
        <translation>以描述不同内容的多个名称共享</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Shared as different kinds of file (e.g. video and archive)</source>
        <translation>以不同类型的文件共享（例如视频和压缩包）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>A name matches a rule in FakeFileFilter.dat</source>
        <translation>某个名称符合 FakeFileFilter.dat 中的规则</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>A comment matches a rule in FakeFileFilter.dat</source>
        <translation>某条评论符合 FakeFileFilter.dat 中的规则</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The first bytes are not what the extension claims</source>
        <translation>文件开头的字节与扩展名不符</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>It is a program named like a media file</source>
        <translation>这是一个伪装成媒体文件名的程序</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>It is an archive named like a media file</source>
        <translation>这是一个伪装成媒体文件名的压缩包</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The published type does not fit the extension</source>
        <translation>发布的类型与扩展名不符</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The spam filter rates it high</source>
        <translation>垃圾过滤器评分较高</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Counted as spam</source>
        <translation>被视为垃圾</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Rated poor, or a Kad note calls it a fake</source>
        <translation>评分较差，或有 Kad 备注称其为假文件</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Rated as fake by users</source>
        <translation>被用户评为假文件</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Sources disagree on the AICH hash</source>
        <translation>各来源的 AICH 哈希不一致</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The length does not fit a file of this size</source>
        <translation>时长与此大小的文件不相称</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The bitrate is not plausible</source>
        <translation>比特率不合理</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Length and bitrate do not add up to the size</source>
        <translation>时长与比特率和文件大小对不上</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Artist, album and title appear in none of the names</source>
        <translation>艺术家、专辑和标题均未出现在任何名称中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>A name uses words that advertise child abuse material</source>
        <translation>名称中含有宣传儿童虐待内容的词语</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The release is password protected</source>
        <translation>该发布受密码保护</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>%1 (fake score %2 of 100)</source>
        <translation>%1（假文件评分 %2 / 100）</translation>
    </message>
</context>
<context>
    <name>eMule::ContactsGraph</name>
    <message>
        <location filename="../src/gui/controls/ContactsGraph.cpp" line="+88"/>
        <source>Contacts</source>
        <translation>联系人</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Time</source>
        <translation>时间</translation>
    </message>
</context>
<context>
    <name>eMule::CoreConnectDialog</name>
    <message>
        <location filename="../src/gui/dialogs/CoreConnectDialog.cpp" line="+23"/>
        <source>Connect to Core</source>
        <translation>连接到核心</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Could not find a local eMule core.
Enter the address and authentication token of a remote core.</source>
        <translation>找不到本地 eMule 核心。
请输入远程核心的地址和认证令牌。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Remote Core</source>
        <translation>远程核心</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Address:</source>
        <translation>地址：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Port:</source>
        <translation>端口：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>paste token here</source>
        <translation>在此粘贴令牌</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Token:</source>
        <translation>令牌：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Save token</source>
        <translation>保存令牌</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Connect</source>
        <translation>连接</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Exit</source>
        <translation>退出</translation>
    </message>
</context>
<context>
    <name>eMule::DetailDialog</name>
    <message>
        <location filename="../src/gui/dialogs/DetailDialog.cpp" line="+202"/>
        <source>Rename</source>
        <translation>重命名</translation>
    </message>
    <message>
        <location line="+16"/>
        <location line="+7"/>
        <source>Archive Preview</source>
        <translation>压缩包预览</translation>
    </message>
    <message>
        <location line="-6"/>
        <source>A preview file can only be opened when the core runs on this computer.</source>
        <translation>只有当核心在本机运行时才能打开预览文件。</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Search Kad</source>
        <translation>搜索 Kad</translation>
    </message>
    <message>
        <location line="+126"/>
        <source>Comments</source>
        <translation>评论</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>Previous</source>
        <translation>上一个</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Next</source>
        <translation>下一个</translation>
    </message>
</context>
<context>
    <name>eMule::DownloadListModel</name>
    <message>
        <location filename="../src/gui/controls/DownloadListModel.cpp" line="+809"/>
        <source>Downloading</source>
        <translation>正在下载</translation>
    </message>
    <message>
        <source>Auto [%1]</source>
        <translation type="vanished">自动 [%1]</translation>
    </message>
    <message>
        <location line="-531"/>
        <source>Queue Full</source>
        <translation>队列已满</translation>
    </message>
    <message>
        <source>Available parts: %1 / %2</source>
        <translation type="vanished">可用部分：%1 / %2</translation>
    </message>
    <message>
        <location line="-145"/>
        <source>Country: %1</source>
        <translation>国家: %1</translation>
    </message>
    <message>
        <location line="+268"/>
        <location line="+2"/>
        <source>Never</source>
        <translation>从不</translation>
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
        <translation type="vanished">文件名：	%1
ED2K 哈希：	%2
大小：	%3
已完成：	%4 (%5%)
类型：	%6
状态：	%7
优先级：	%8
来源：	%9
请求：	%10
已接受请求：	%11
已传输数据：	%12</translation>
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
        <translation>文件名：	%1
ED2K 哈希：	%2
大小：	%3
已完成：	%4 (%5%)
类型：	%6
状态：	%7
优先级：	%8
来源：	%9
请求：	%10
已接受请求：	%11
已上传：	%12</translation>
    </message>
    <message>
        <location line="+100"/>
        <source>File Name</source>
        <translation>文件名</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Completed</source>
        <translation>已完成</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Speed</source>
        <translation>速度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Progress</source>
        <translation>进度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Sources</source>
        <translation>来源</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority</source>
        <translation>优先级</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status</source>
        <translation>状态</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remaining</source>
        <translation>剩余</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Seen Complete</source>
        <translation>已见完整</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Country</source>
        <translation>国家</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Last reception</source>
        <translation>最后接收</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Category</source>
        <translation>分类</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Added On</source>
        <translation>添加于</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Confidence</source>
        <translation>可信度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Transferred</source>
        <translation>已传输</translation>
    </message>
    <message>
        <location line="+226"/>
        <source>Importing part</source>
        <translation>正在导入分块</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+5"/>
        <source>Hashing</source>
        <translation>正在计算哈希</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+1"/>
        <location line="+1"/>
        <source>Completing (%1)</source>
        <translation>正在完成（%1）</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Copying</source>
        <translation>正在复制</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Uncompressing</source>
        <translation>正在解压</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Completing</source>
        <translation>正在完成</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Complete</source>
        <translation>已完成</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Stopped</source>
        <translation>已停止</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Paused</source>
        <translation>已暂停</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+3"/>
        <source>Insufficient disk space</source>
        <translation>磁盘空间不足</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Error</source>
        <translation>错误</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Waiting</source>
        <translation>等待中</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>HTTP Cache</source>
        <translation>HTTP 缓存</translation>
    </message>
    <message>
        <location line="-716"/>
        <location line="+718"/>
        <source>Asked for another file</source>
        <translation>已请求其他文件</translation>
    </message>
    <message>
        <location line="-740"/>
        <source>Available parts: %1</source>
        <translation>可用分块:%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>User Name: %1</source>
        <translation>用户名:%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unknown</source>
        <translation>未知</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server</source>
        <translation>服务器</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Next re-ask</source>
        <translation>下次重新询问</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Asked: %1 times; Available parts: %2</source>
        <translation>已询问:%1 次;可用分块:%2</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Client&apos;s file name:</source>
        <translation>客户端的文件名:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>File comment:</source>
        <translation>文件评论:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Rating</source>
        <translation>评分</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>A4AF files</source>
        <translation>A4AF 文件</translation>
    </message>
    <message>
        <location line="+260"/>
        <source>%1 (ed3k)</source>
        <translation>%1 (ed3k)</translation>
    </message>
    <message>
        <location line="+458"/>
        <source>Swap blocked</source>
        <translation>切换被阻止</translation>
    </message>
</context>
<context>
    <name>eMule::Ed3kFilterPage</name>
    <message>
        <location filename="../src/gui/dialogs/Ed3kFilterPage.cpp" line="+74"/>
        <location line="+80"/>
        <source>Changes on this page take effect at once.</source>
        <translation>此页面上的更改立即生效。</translation>
    </message>
    <message>
        <location line="-75"/>
        <source>Followed filter lists</source>
        <translation>已关注的过滤列表</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>A list marks fake, harmful or spam files in search results; whether it also hides them or refuses their download is set per list.</source>
        <translation>列表会在搜索结果中标记伪造、有害或垃圾文件；是否同时隐藏它们或拒绝下载，按每个列表设置。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Label</source>
        <translation>标签</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+218"/>
        <source>Mode</source>
        <translation>模式</translation>
    </message>
    <message>
        <location line="-218"/>
        <source>Version</source>
        <translation>版本</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Files</source>
        <translation>文件</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Entries</source>
        <translation>条目</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Waiting</source>
        <translation>等待中</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Follow…</source>
        <translation>关注…</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+176"/>
        <source>Unfollow</source>
        <translation>取消关注</translation>
    </message>
    <message>
        <location line="-175"/>
        <source>Open list file…</source>
        <translation>打开列表文件…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Uses a list file (.ed3kflt) by hand: it has no author and gets no updates.</source>
        <translation>手动使用列表文件 (.ed3kflt)：它没有作者，也不会更新。</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Mode of the selected list:</source>
        <translation>所选列表的模式：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Own verdicts (they overrule every list)</source>
        <translation>自己的判定（优先于所有列表）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Entry</source>
        <translation>条目</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>ID</source>
        <translation>ID</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Verdict</source>
        <translation>判定</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Reason</source>
        <translation>原因</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Add…</source>
        <translation>添加…</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+254"/>
        <source>Remove</source>
        <translation>删除</translation>
    </message>
    <message>
        <location line="-249"/>
        <source>Mode:</source>
        <translation>模式：</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>The ed3k node is not running: filter lists can be changed while it runs.</source>
        <translation>ed3k 节点未运行：过滤列表只能在其运行时更改。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>%1 (opened by hand: no author, no updates)</source>
        <translation>%1（手动打开：无作者，无更新）</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>none yet</source>
        <translation>暂无</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Fake</source>
        <translation>伪造</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Harmful</source>
        <translation>有害</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Spam</source>
        <translation>垃圾</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Good</source>
        <translation>良好</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>None</source>
        <translation>无</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>File (ed3k root)</source>
        <translation>文件（ed3k 根）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>File (eD2K hash)</source>
        <translation>文件（eD2K 哈希）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Author (node ID)</source>
        <translation>作者（Node ID）</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Hide and refuse downloads</source>
        <translation>隐藏并拒绝下载</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Hide</source>
        <translation>隐藏</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Mark only</source>
        <translation>仅标记</translation>
    </message>
    <message>
        <location line="+22"/>
        <location line="+4"/>
        <location line="+8"/>
        <source>Follow filter list</source>
        <translation>关注过滤列表</translation>
    </message>
    <message>
        <location line="-12"/>
        <source>Link of the filter list (ed3k:set?…):</source>
        <translation>过滤列表的链接 (ed3k:set?…)：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Label:</source>
        <translation>标签：</translation>
    </message>
    <message>
        <location line="+29"/>
        <location line="+7"/>
        <location line="+8"/>
        <source>Open filter list file</source>
        <translation>打开过滤列表文件</translation>
    </message>
    <message>
        <location line="-14"/>
        <source>Filter lists (*.ed3kflt);;All files (*)</source>
        <translation>过滤列表 (*.ed3kflt);;所有文件 (*)</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Could not read %1 (at most 4 MiB).</source>
        <translation>无法读取 %1（最大 4 MiB）。</translation>
    </message>
    <message>
        <location line="+32"/>
        <location line="+28"/>
        <location line="+11"/>
        <source>Own verdict</source>
        <translation>自己的判定</translation>
    </message>
    <message>
        <location line="-34"/>
        <source>Entry:</source>
        <translation>条目：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>hexadecimal: a root (64 digits), an eD2K hash or a node ID (32 digits)</source>
        <translation>十六进制：根（64 位）、eD2K 哈希或 Node ID（32 位）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>ID:</source>
        <translation>ID：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Verdict:</source>
        <translation>判定：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Reason:</source>
        <translation>原因：</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>The ID is 32 hexadecimal digits (a root: 64).</source>
        <translation>ID 为 32 位十六进制数（根：64 位）。</translation>
    </message>
    <message>
        <location line="+38"/>
        <source>the ed3k node is not running</source>
        <translation>ed3k 节点未运行</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>error %1</source>
        <translation>错误 %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>no answer from the core</source>
        <translation>核心无应答</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>ed3k</source>
        <translation>ed3k</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>%1 failed: %2</source>
        <translation>%1 失败：%2</translation>
    </message>
</context>
<context>
    <name>eMule::Ed3kNameOpener</name>
    <message>
        <location filename="../src/gui/app/Ed3kNameOpener.cpp" line="+78"/>
        <source>yours</source>
        <translation>你的</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>pinned on %1</source>
        <translation>固定于 %1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>a name list</source>
        <translation>一个名称列表</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>the list &quot;%1&quot;</source>
        <translation>列表“%1”</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>%1 of %2 friends</source>
        <translation>%2 位好友中的 %1 位</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>network, %1 sites</source>
        <translation>网络，%1 个站点</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>domain-verified</source>
        <translation>已经域名验证</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>DNS, unverified</source>
        <translation>DNS，未验证</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>key %1</source>
        <translation>密钥 %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>unknown</source>
        <translation>未知</translation>
    </message>
    <message>
        <location line="+34"/>
        <source>The name does not exist in its zone.</source>
        <translation>该名称在其区域中不存在。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>No source names a zone for it.</source>
        <translation>没有来源为它指定区域。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>It passes too many zones.</source>
        <translation>它经过的区域过多。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Neither DNS nor the web server names a usable link.</source>
        <translation>DNS 和 Web 服务器都没有给出可用的链接。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>It could not be resolved.</source>
        <translation>无法解析它。</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+30"/>
        <source>Sources tie between the zones %1.</source>
        <translation>来源在区域 %1 之间不分上下。</translation>
    </message>
    <message>
        <location line="-28"/>
        <location line="+35"/>
        <source>ed3k name</source>
        <translation>ed3k 名称</translation>
    </message>
    <message>
        <location line="-35"/>
        <source>%1 could not be opened.

%2</source>
        <translation>无法打开 %1。

%2</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>%1 is the zone %2 (%3).</source>
        <translation>%1 是区域 %2（%3）。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Only the network knows this name: no friend and no list you follow knows it.</source>
        <translation>只有网络知道这个名称：没有好友，也没有你关注的列表知道它。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>It comes from DNS only and is not verified by the domain.</source>
        <translation>它仅来自 DNS，未经域名验证。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The domain&apos;s DNS record and its ed3k.json name different keys; ed3k.json decides.</source>
        <translation>该域名的 DNS 记录和其 ed3k.json 指定了不同的密钥；以 ed3k.json 为准。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Warning: %1 names another zone, %2, than %3.</source>
        <translation>警告：%1 指定的区域 %2 与 %3 不同。</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Your friends learn the names you open.</source>
        <translation>你的好友会得知你打开的名称。</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Open it?</source>
        <translation>要打开吗？</translation>
    </message>
    <message>
        <location line="+49"/>
        <source>ed3k name list</source>
        <translation>ed3k 名称列表</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Your ed3k friend %1 publishes a name list. Follow it?

Its names are then resolved after the lists you already follow.</source>
        <translation>你的 ed3k 好友 %1 发布了一个名称列表。要关注它吗？

其名称将在你已关注的列表之后解析。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Friend %1</source>
        <translation>好友 %1</translation>
    </message>
</context>
<context>
    <name>eMule::Ed3kOptionsPages</name>
    <message>
        <source>The ed3k node uses the nickname of the General page: %1</source>
        <translation type="vanished">ed3k 节点使用“常规”页面中的昵称：%1</translation>
    </message>
    <message>
        <location filename="../src/gui/dialogs/Ed3kOptionsPages.cpp" line="+301"/>
        <location line="+11"/>
        <location line="+470"/>
        <location line="+11"/>
        <location line="+23"/>
        <location line="+19"/>
        <location line="+3"/>
        <location line="+21"/>
        <location line="+24"/>
        <source>ed3k credits</source>
        <translation>ed3k 信用</translation>
    </message>
    <message>
        <location line="-581"/>
        <source>The ledger contract is an address: 0x and 40 hexadecimal digits.</source>
        <translation>账本合约是一个地址：0x 加 40 位十六进制数字。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>The chain ID is a whole number.</source>
        <translation>chain ID 是一个整数。</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Enable the ed3k node</source>
        <translation>启用 ed3k 节点</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Runs the ed3k node (packmule) in the core, on a UDP port of its own. It finds other nodes over Kad and serves the shared files.</source>
        <translation>在核心中运行 ed3k 节点（packmule），使用其独立的 UDP 端口。它通过 Kad 查找其他节点并提供共享文件。</translation>
    </message>
    <message>
        <source>Identity</source>
        <translation type="vanished">身份</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Sharing</source>
        <translation>共享</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Share the shared files over ed3k too</source>
        <translation>同时通过 ed3k 共享共享文件</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Hold Usenet posts back</source>
        <translation>保留 Usenet 帖子</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Share the files of Usenet downloads, but not their article lists, post sets or nzb records.</source>
        <translation>共享 Usenet 下载的文件，但不共享其文章列表、帖子集或 nzb 记录。</translation>
    </message>
    <message>
        <source>Extensions</source>
        <translation type="vanished">扩展</translation>
    </message>
    <message>
        <source>Optional parts of the ed3k protocol. A change restarts the node.</source>
        <translation type="vanished">ed3k 协议的可选部分。更改会重启节点。</translation>
    </message>
    <message>
        <location line="+41"/>
        <source>Ports</source>
        <translation>端口</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+19"/>
        <source>Random</source>
        <translation>随机</translation>
    </message>
    <message>
        <location line="-18"/>
        <source>UDP port of the ed3k node, not the eD2K UDP port. A change restarts the node.</source>
        <translation>ed3k 节点的 UDP 端口，不是 eD2K 的 UDP 端口。更改会重启节点。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Test Ports</source>
        <translation>测试端口</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Asks emule-qt.org whether the UDP port the node uses now is reachable. ed3k needs only UDP. Apply a changed port first.</source>
        <translation>询问 emule-qt.org 节点当前使用的 UDP 端口是否可访问。ed3k 只需要 UDP。如已更改端口，请先应用。</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>UDP port:</source>
        <translation>UDP 端口：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>TCP port of the local website server (this computer only). It is part of every site&apos;s address: a change loses what sites stored in the browser.</source>
        <translation>本地网站服务器的 TCP 端口（仅限本机）。它是每个站点地址的一部分：更改后会丢失站点在浏览器中存储的内容。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Website server port:</source>
        <translation>网站服务器端口：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Finding nodes</source>
        <translation>查找节点</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Find ed3k nodes in the local network (restarts the node)</source>
        <translation>在局域网中查找 ed3k 节点（会重启节点）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Node lists for the first contact, one ed3k:nodes link per line:</source>
        <translation>首次联系用的节点列表，每行一个 ed3k:nodes 链接：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Chaintube</source>
        <translation>Chaintube</translation>
    </message>
    <message>
        <source>JSON-RPC endpoints of the chaintube chain, one URL per line (https, or http to a node on this computer). The ledger of the credits is on the Credits page.</source>
        <translation type="vanished">chaintube 链的 JSON-RPC 端点，每行一个 URL（https，或指向本机节点的 http）。信用账本在“信用”页面中。</translation>
    </message>
    <message>
        <location line="+63"/>
        <source>Use credits (off by default)</source>
        <translation>使用信用（默认关闭）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Earn credits for uploading, and spend them on priority upload slots at others.</source>
        <translation>上传可赚取信用，并可用其在他人处换取优先上传位。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Ledger of the network</source>
        <translation>网络账本</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>assigned at deployment</source>
        <translation>部署时分配</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>There is no default: take it from the network you are in (credit.md §7.1).</source>
        <translation>没有默认值：请从你所在的网络获取（credit.md §7.1）。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Chain ID:</source>
        <translation>Chain ID：</translation>
    </message>
    <message>
        <location line="-382"/>
        <source>Not valid</source>
        <translation>无效</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Premium until %1</source>
        <translation>高级会员至 %1</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Free until %1</source>
        <translation>免费至 %1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Premium</source>
        <translation>高级会员</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Free</source>
        <translation>免费</translation>
    </message>
    <message>
        <source>A contract is an address: 0x and 40 hexadecimal digits.</source>
        <translation type="vanished">合约是一个地址：0x 加 40 位十六进制数。</translation>
    </message>
    <message>
        <source>The content contract of the network. No default.</source>
        <translation type="vanished">网络的内容合约。无默认值。</translation>
    </message>
    <message>
        <source>The comment contract of the network. No default.</source>
        <translation type="vanished">网络的评论合约。无默认值。</translation>
    </message>
    <message>
        <source>The vote contract of the network. No default.</source>
        <translation type="vanished">网络的投票合约。无默认值。</translation>
    </message>
    <message>
        <source>The forwarder of requests a site sends; empty: none.</source>
        <translation type="vanished">站点所发请求的转发器；留空：无。</translation>
    </message>
    <message>
        <source>Content contract:</source>
        <translation type="vanished">内容合约：</translation>
    </message>
    <message>
        <source>Comment contract:</source>
        <translation type="vanished">评论合约：</translation>
    </message>
    <message>
        <source>Vote contract:</source>
        <translation type="vanished">投票合约：</translation>
    </message>
    <message>
        <source>Forwarder:</source>
        <translation type="vanished">转发器：</translation>
    </message>
    <message>
        <location line="+382"/>
        <source>The ledger contract of the network: 0x and 40 hexadecimal digits. No default.</source>
        <translation>网络的账本合约：0x 加 40 位十六进制数字。没有默认值。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ledger contract:</source>
        <translation>账本合约：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>JSON-RPC endpoints of the ledger, one URL per line, best of different operators. They go through the proxy you configured. Without one, receipts are signed and kept, nothing is sold or settled.</source>
        <translation>账本的 JSON-RPC 端点，每行一个 URL，最好来自不同的运营者。它们经由你配置的代理。若没有端点，收据会被签名并保存，但不会出售或结算任何东西。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>RPC endpoints:</source>
        <translation>RPC 端点：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>How many endpoints must agree on a read the node acts on (credit.md §7.3: two).</source>
        <translation>节点据以行动的读取需有多少个端点一致（credit.md §7.3：两个）。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Endpoints that must agree:</source>
        <translation>需一致的端点数：</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+10"/>
        <source> credits/MiB</source>
        <translation> 信用/MiB</translation>
    </message>
    <message>
        <location line="-9"/>
        <source>Never pay (receipts only)</source>
        <translation>从不支付（仅收据）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The most paid for a priority slot per MiB, for every ed3k download.</source>
        <translation>每个 ed3k 下载为优先上传位每 MiB 支付的最高金额。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bid at most:</source>
        <translation>最高出价：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Sell priority upload slots</source>
        <translation>出售优先上传位</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Lowest bid taken:</source>
        <translation>接受的最低出价：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Those of the upload queue</source>
        <translation>上传队列的上传位</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Upload slots the priority lane counts; at most two thirds go to bidders.</source>
        <translation>优先通道计入的上传位；最多三分之二分配给出价者。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload slots:</source>
        <translation>上传位：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Credit key</source>
        <translation>信用密钥</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>New key…</source>
        <translation>新密钥…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Import…</source>
        <translation>导入…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Export…</source>
        <translation>导出…</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Only from a GUI on the computer of the core: the key is a secret.</source>
        <translation>只能在核心所在计算机上的 GUI 中操作：密钥是机密。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Account</source>
        <translation>账户</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+151"/>
        <source>Not known while credits are off.</source>
        <translation>信用关闭时未知。</translation>
    </message>
    <message>
        <location line="-143"/>
        <source>What switching credits on means</source>
        <translation>开启信用意味着什么</translation>
    </message>
    <message>
        <location line="+50"/>
        <location line="+259"/>
        <source>Changes on this page take effect at once.</source>
        <translation>此页面上的更改立即生效。</translation>
    </message>
    <message>
        <location line="-243"/>
        <source>Pinned names</source>
        <translation>固定的名称</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+20"/>
        <source>Key</source>
        <translation>键</translation>
    </message>
    <message>
        <location line="-20"/>
        <source>How</source>
        <translation>方式</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Vouched</source>
        <translation>已担保</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+7"/>
        <source>Unpin</source>
        <translation>取消固定</translation>
    </message>
    <message>
        <location line="-6"/>
        <source>Vouch / Withdraw</source>
        <translation>担保 / 撤回</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Vouching publishes a claim for the name and answers friends with it.</source>
        <translation>担保会为该名称发布一个声明，并以此回复好友。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Vouch</source>
        <translation>担保</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Petnames</source>
        <translation>昵名</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Petname</source>
        <translation>昵名</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Shared with friends</source>
        <translation>与好友共享</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Add…</source>
        <translation>添加…</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+6"/>
        <location line="+473"/>
        <source>Remove</source>
        <translation>删除</translation>
    </message>
    <message>
        <location line="-469"/>
        <source>Followed name lists (the first one wins)</source>
        <translation>已关注的名称列表（第一个优先）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Label</source>
        <translation>标签</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Entries</source>
        <translation>条目</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Refused</source>
        <translation>已拒绝</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Link</source>
        <translation>链接</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Follow…</source>
        <translation>关注…</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+10"/>
        <source>Unfollow</source>
        <translation>取消关注</translation>
    </message>
    <message>
        <location line="-9"/>
        <source>Move up</source>
        <translation>上移</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Move down</source>
        <translation>下移</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>This core was built without ed3k: the settings are kept, but have no effect.</source>
        <translation>此核心在构建时未包含 ed3k：设置会保留，但不起作用。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The node is running on UDP port %1.</source>
        <translation>节点正在 UDP 端口 %1 上运行。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The node is not running.</source>
        <translation>节点未运行。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>No credit key yet: one is made when credits are switched on.</source>
        <translation>尚无信用密钥：开启信用时会生成一个。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Credit address: %1</source>
        <translation>信用地址：%1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Not in use: %1</source>
        <translation>未使用：%1</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Balance: %1</source>
        <translation>余额：%1</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>not read yet</source>
        <translation>尚未读取</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The account is overdrawn: a new credit key is the way on.</source>
        <translation>账户已透支：唯一的出路是新的信用密钥。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Mature: the receipts you sign mint credits.</source>
        <translation>已成熟：你签名的收据会铸造信用。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not mature yet: the receipts you sign mint nothing.</source>
        <translation>尚未成熟：你签名的收据不会铸造任何东西。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Receipts signed: %1, received: %2</source>
        <translation>已签名收据：%1，已收到：%2</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Stake: a credit buys %1 E3S</source>
        <translation>质押：一个信用可购买 %1 E3S</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Stake: burning is off</source>
        <translation>质押：销毁已关闭</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Stake claims waiting: %1</source>
        <translation>等待中的质押领取：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Some endpoints are on another chain and are not used.</source>
        <translation>部分端点位于另一条链上，不会被使用。</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Switch credits on?</source>
        <translation>开启信用？</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>A new credit key gives a new credit address: no balance, and not mature for 30 days. The old address keeps its credits; they move only by a transfer, which is public and costs a part of them. Export the old key first if you may need it.

Make a new key?</source>
        <translation>新的信用密钥会产生新的信用地址：没有余额，且 30 天内不成熟。旧地址保留其信用；信用只能通过转账转移，转账是公开的，并会耗费其中一部分。如果你可能还需要旧密钥，请先将其导出。

生成新密钥？</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>New key</source>
        <translation>新密钥</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Import credit key</source>
        <translation>导入信用密钥</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+55"/>
        <source>Credit keys (*.key *.txt);;All files (*)</source>
        <translation>信用密钥 (*.key *.txt);;所有文件 (*)</translation>
    </message>
    <message>
        <location line="-50"/>
        <source>Could not read %1.</source>
        <translation>无法读取 %1。</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>This is not a credit key: 32 bytes or 64 hexadecimal digits.</source>
        <translation>这不是信用密钥：应为 32 字节或 64 位十六进制数字。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>The imported key replaces the current credit key. Export the current one first if you may need it.

Import?</source>
        <translation>导入的密钥将替换当前的信用密钥。如果你可能还需要当前密钥，请先将其导出。

导入？</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Import</source>
        <translation>导入</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>The file will hold the secret credit key: whoever has it can spend your credits and sign as you. Keep it as private as a password.

Export?</source>
        <translation>该文件将保存机密的信用密钥：任何拥有它的人都可以花费你的信用并以你的身份签名。请像密码一样妥善保密。

导出？</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Export credit key</source>
        <translation>导出信用密钥</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Export</source>
        <translation>导出</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Could not write %1.</source>
        <translation>无法写入 %1。</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>The ed3k node is not running: names can be changed while it runs.</source>
        <translation>ed3k 节点未运行：只有在其运行时才能更改名称。</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+10"/>
        <source>Yes</source>
        <translation>是</translation>
    </message>
    <message>
        <location line="-10"/>
        <location line="+10"/>
        <source>No</source>
        <translation>否</translation>
    </message>
    <message>
        <location line="+19"/>
        <location line="+3"/>
        <location line="+4"/>
        <location line="+3"/>
        <source>Add petname</source>
        <translation>添加昵名</translation>
    </message>
    <message>
        <location line="-10"/>
        <source>Petname:</source>
        <translation>昵名：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Key of the zone (hex or base32):</source>
        <translation>区域的密钥（hex 或 base32）：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Share this petname with your ed3k friends?</source>
        <translation>要与你的 ed3k 好友共享此昵名吗？</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+4"/>
        <location line="+3"/>
        <source>Follow name list</source>
        <translation>关注名称列表</translation>
    </message>
    <message>
        <location line="-7"/>
        <source>Link of the name list (ed3k:set?…):</source>
        <translation>名称列表的链接（ed3k:set?…）：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Label:</source>
        <translation>标签：</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Move</source>
        <translation>移动</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>the ed3k node is not running</source>
        <translation>ed3k 节点未运行</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>error %1</source>
        <translation>错误 %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>no answer from the core</source>
        <translation>核心无应答</translation>
    </message>
    <message>
        <location line="+110"/>
        <source>The resolver runs JDownloader&apos;s hoster plugins in a Java program of its own.</source>
        <translation>解析器在独立的 Java 程序中运行 JDownloader 的网盘插件。</translation>
    </message>
    <message>
        <location line="-751"/>
        <location line="+643"/>
        <source>ed3k</source>
        <translation>ed3k</translation>
    </message>
    <message>
        <location line="-642"/>
        <source>This network is early alpha, experimental and not for live use yet. Please come back later.</source>
        <translation>该网络处于早期 Alpha 实验阶段，尚不能用于实际使用。请稍后再来。</translation>
    </message>
    <message>
        <location line="+68"/>
        <source>Open Router</source>
        <translation>打开路由器</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Opens the web page of this computer&apos;s router, where ports are forwarded</source>
        <translation>打开此计算机路由器的网页，可在其中设置端口转发</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>JSON-RPC endpoints of the chaintube chain, one URL per line (https, or http to a node on this computer).</source>
        <translation>chaintube 链的 JSON-RPC 端点，每行一个 URL（https，或指向本机节点的 http）。</translation>
    </message>
    <message>
        <location line="+547"/>
        <source>%1 failed: %2</source>
        <translation>%1 失败：%2</translation>
    </message>
    <message>
        <location line="+49"/>
        <source>&lt;p&gt;&lt;b&gt;The ledger is public and stays so.&lt;/b&gt; For every pair of credit addresses it shows how much one downloaded from the other, on which day, and what was paid; no file, host or node ID. Your node tells its credit address to every peer that opens a file stream to it, so others can link your node and host address to your credit address and read your whole trade history. A bid (a hold) is public at once. A burn for stake tokens is public too: it ties your credit address to the address it pays, and through the bridge to an address on the C-Chain, where an exchange may know your name.&lt;/p&gt;&lt;p&gt;&lt;b&gt;Hidden (anonymous) use&lt;/b&gt; needs a credit address of its own that is never used outside the anonymous path; never move credits or gas coin between that address and this one, and never burn for it: a transfer is a public link for good.&lt;/p&gt;&lt;p&gt;A remote ledger endpoint sees your IP address and every credit address it is asked about; the calls go through the proxy you configured.&lt;/p&gt;&lt;p&gt;&lt;b&gt;Not checked by a lawyer:&lt;/b&gt; credits buy E3S stake tokens, which are traded on exchanges. Whether credits and E3S are crypto-assets under the law of your country, and what being paid in them means for you, was not checked by a lawyer.&lt;/p&gt;</source>
        <translation>&lt;p&gt;&lt;b&gt;账本是公开的，并将一直公开。&lt;/b&gt;对于每一对信用地址，它都会显示一方从另一方下载了多少、在哪一天、支付了多少；不包含文件、主机或节点 ID。你的节点会把它的信用地址告诉每个向它打开文件流的对等端，因此他人可以把你的节点和主机地址与你的信用地址关联起来，并读取你的全部交易历史。出价（冻结）会立即公开。为获取质押代币而进行的销毁同样公开：它把你的信用地址与其付款的地址关联起来，并经由跨链桥关联到 C-Chain 上的一个地址，而交易所可能知道该地址对应你的姓名。&lt;/p&gt;&lt;p&gt;&lt;b&gt;隐藏（匿名）使用&lt;/b&gt;需要一个专用的信用地址，且该地址从不在匿名路径之外使用；切勿在该地址与本地址之间转移信用或 gas 币，也切勿为其进行销毁：一次转账就是永久的公开关联。&lt;/p&gt;&lt;p&gt;远程账本端点能看到你的 IP 地址以及每个被查询的信用地址；这些调用经由你配置的代理。&lt;/p&gt;&lt;p&gt;&lt;b&gt;未经律师审核：&lt;/b&gt;信用可购买 E3S 质押代币，而 E3S 在交易所上交易。信用和 E3S 在你所在国家的法律下是否属于加密资产，以及以它们获得支付对你意味着什么，均未经律师审核。&lt;/p&gt;</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>%1.%2 credits</source>
        <translation>%1.%2 信用</translation>
    </message>
    <message>
        <location line="+13"/>
        <location line="+211"/>
        <source>Downloads from file hosters</source>
        <translation>从文件网盘下载</translation>
    </message>
    <message>
        <location line="-209"/>
        <source>Allow downloads from file hosters</source>
        <translation>允许从文件网盘下载</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Lets the resolver open hoster pages, wait out their timers, show their puzzles and use the accounts below. Off by default.</source>
        <translation>允许解析器打开网盘页面、等待其计时、显示验证码并使用下方的账户。默认关闭。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Hoster downloads at once. Never more than one per hoster and account.</source>
        <translation>同时进行的网盘下载数。每个网盘和账户最多一个。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloads at once:</source>
        <translation>同时下载数:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Show image puzzles (captchas)</source>
        <translation>显示图片验证码</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Off: a link that asks for a puzzle waits instead.</source>
        <translation>关闭:需要验证码的链接将等待。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Use hoster links that other ed3k nodes name</source>
        <translation>使用其他 ed3k 节点提供的网盘链接</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Java runtime</source>
        <translation>Java 运行时</translation>
    </message>
    <message>
        <source>The resolver runs JDownloader&apos;s hoster plugins in a Java program of its own. eMuleQt looks for Java 17 or newer and can download one into its folder.</source>
        <translation type="vanished">解析器在其自己的 Java 程序中运行 JDownloader 的网盘插件。eMuleQt 会查找 Java 17 或更高版本,也可将其下载到自己的文件夹中。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Find automatically</source>
        <translation>自动查找</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Java program:</source>
        <translation>Java 程序:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Browse...</source>
        <translation>浏览...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Java program</source>
        <translation>Java 程序</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Download a Java runtime when none is found</source>
        <translation>未找到时下载 Java 运行时</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Eclipse Temurin from adoptium.net, checked by its SHA-256, into the eMuleQt folder.</source>
        <translation>从 adoptium.net 获取 Eclipse Temurin,经 SHA-256 校验后放入 eMuleQt 文件夹。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Look again</source>
        <translation>重新查找</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Download Java</source>
        <translation>下载 Java</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Sharing links</source>
        <translation>链接共享</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+153"/>
        <source>Publish hoster links</source>
        <translation>发布托管站链接</translation>
    </message>
    <message>
        <location line="-151"/>
        <source>A downloaded file is shared either way. Published, its link is found in ed3k and handed to other nodes: a link is often all that protects a file at a hoster.</source>
        <translation>下载的文件无论如何都会共享。发布后，其链接可在 ed3k 中找到并交给其他节点：托管站上的文件往往只靠链接保护。</translation>
    </message>
    <message>
        <location line="+147"/>
        <source>&lt;p&gt;Sharing hoster links means: once a file from a link is downloaded and verified, its link is published in ed3k and handed to other nodes that download the same file.&lt;/p&gt;&lt;p&gt;A link is often all that keeps a file at a hoster private. Whoever knows the link can find the file in ed3k, and anyone can find the link of a file you share.&lt;/p&gt;&lt;p&gt;Publish hoster links?&lt;/p&gt;</source>
        <translation>&lt;p&gt;共享网盘链接是指:链接中的文件下载并校验后,其链接会在 ed3k 中发布,并交给下载同一文件的其他节点。&lt;/p&gt;&lt;p&gt;链接往往是让网盘上的文件保持私密的唯一手段。知道链接的人可以在 ed3k 中找到该文件,任何人也都能找到您共享文件的链接。&lt;/p&gt;&lt;p&gt;发布托管站链接?&lt;/p&gt;</translation>
    </message>
    <message>
        <source>Hold hoster links back</source>
        <translation type="vanished">保留网盘链接不公开</translation>
    </message>
    <message>
        <source>A downloaded file is shared either way. Held back, its link is not published and not handed to other nodes: a link is often all that protects a file at a hoster.</source>
        <translation type="vanished">下载的文件无论如何都会共享。保留时,其链接不会发布,也不会交给其他节点:链接往往是保护网盘上文件的唯一手段。</translation>
    </message>
    <message>
        <location line="-142"/>
        <source>Accounts</source>
        <translation>账户</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Logins at hosters. They stay on this computer: never sent to another node, never logged.</source>
        <translation>网盘登录信息。仅保存在本机:绝不会发送给其他节点,也绝不会写入日志。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Hoster</source>
        <translation>网盘</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>User</source>
        <translation>用户</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Status</source>
        <translation>状态</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Add...</source>
        <translation>添加...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Edit...</source>
        <translation>编辑...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Check</source>
        <translation>检查</translation>
    </message>
    <message>
        <location line="+56"/>
        <source>This build has no hoster resolver: hoster links are stored and passed on, but not downloaded.</source>
        <translation>此版本没有网盘解析器:网盘链接会被保存和转发,但不会下载。</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>The resolver is ready.</source>
        <translation>解析器已就绪。</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Resolver: %1</source>
        <translation>解析器:%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The resolver is not ready: %1</source>
        <translation>解析器未就绪:%1</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Looking for Java...</source>
        <translation>正在查找 Java...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Java %1 (%2): %3</source>
        <translation>Java %1 (%2):%3</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>No Java 17 or newer found.</source>
        <translation>未找到 Java 17 或更高版本。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloading Java... %1%</source>
        <translation>正在下载 Java... %1%</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Installing Java...</source>
        <translation>正在安装 Java...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Java: %1</source>
        <translation>Java:%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Java is looked for when downloads from hosters are allowed.</source>
        <translation>允许从网盘下载后将查找 Java。</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Allow downloads from file hosters and apply first.</source>
        <translation>请先允许从文件托管站下载并应用。</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>&lt;p&gt;Downloads from file hosters run JDownloader&apos;s hoster plugins on this computer, in a program of their own.&lt;/p&gt;&lt;ul&gt;&lt;li&gt;The hoster sees your address and which file you fetch; with an account, also who you are to it.&lt;/li&gt;&lt;li&gt;Their pages may show puzzles and timers; eMuleQt shows image puzzles and waits out timers for you.&lt;/li&gt;&lt;li&gt;Fetching by program is against the terms of many hosters. That is between you and the hoster.&lt;/li&gt;&lt;li&gt;Every block that ed3k knows is verified; a file ed3k does not know is checked as a whole when complete.&lt;/li&gt;&lt;/ul&gt;&lt;p&gt;Allow downloads from file hosters?&lt;/p&gt;</source>
        <translation>&lt;p&gt;从文件网盘下载会在本机以独立程序运行 JDownloader 的网盘插件。&lt;/p&gt;&lt;ul&gt;&lt;li&gt;网盘能看到您的地址以及您获取的文件;使用账户时,还能知道您是谁。&lt;/li&gt;&lt;li&gt;其页面可能显示验证码和等待计时;eMuleQt 会为您显示图片验证码并等待计时结束。&lt;/li&gt;&lt;li&gt;以程序方式获取文件违反许多网盘的服务条款。这是您与网盘之间的事。&lt;/li&gt;&lt;li&gt;ed3k 已知的每个块都会校验;ed3k 未知的文件会在完成后整体校验。&lt;/li&gt;&lt;/ul&gt;&lt;p&gt;允许从文件网盘下载吗?&lt;/p&gt;</translation>
    </message>
    <message>
        <source>&lt;p&gt;Sharing hoster links means: once a file from a link is downloaded and verified, its link is published in ed3k and handed to other nodes that download the same file.&lt;/p&gt;&lt;p&gt;A link is often all that keeps a file at a hoster private. Whoever knows the link can find the file in ed3k, and anyone can find the link of a file you share.&lt;/p&gt;&lt;p&gt;Share hoster links?&lt;/p&gt;</source>
        <translation type="vanished">&lt;p&gt;共享网盘链接是指:链接中的文件下载并校验后,其链接会在 ed3k 中发布,并交给下载同一文件的其他节点。&lt;/p&gt;&lt;p&gt;链接往往是让网盘上的文件保持私密的唯一手段。知道链接的人可以在 ed3k 中找到该文件,任何人也都能找到您共享文件的链接。&lt;/p&gt;&lt;p&gt;共享网盘链接吗?&lt;/p&gt;</translation>
    </message>
    <message>
        <source>Share hoster links</source>
        <translation type="vanished">共享网盘链接</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>Add hoster account</source>
        <translation>添加网盘账户</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Edit hoster account</source>
        <translation>编辑网盘账户</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>unchanged</source>
        <translation>未更改</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hoster:</source>
        <translation>网盘:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>User:</source>
        <translation>用户：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Password:</source>
        <translation>密码：</translation>
    </message>
    <message>
        <location line="+38"/>
        <source>Check hoster account</source>
        <translation>检查网盘账户</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Password of %1 at %2:</source>
        <translation>%1 在 %2 的密码:</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Checking...</source>
        <translation>正在检查...</translation>
    </message>
</context>
<context>
    <name>eMule::Ed3kRoomList</name>
    <message>
        <location filename="../src/gui/panels/Ed3kRoomList.cpp" line="+28"/>
        <location line="+98"/>
        <source>Join by link...</source>
        <translation>通过链接加入...</translation>
    </message>
    <message>
        <location line="-97"/>
        <location line="+98"/>
        <source>Create...</source>
        <translation>创建...</translation>
    </message>
    <message>
        <location line="-88"/>
        <source>Room</source>
        <translation>房间</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Nick</source>
        <translation>昵称</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Members</source>
        <translation>成员</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Kind</source>
        <translation>类型</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>State</source>
        <translation>状态</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>Closed</source>
        <translation>封闭</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Private</source>
        <translation>私密</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Public</source>
        <translation>公开</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>moderated</source>
        <translation>受管理</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>owner</source>
        <translation>所有者</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>moderator</source>
        <translation>管理员</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Waiting for the room key</source>
        <translation>正在等待房间密钥</translation>
    </message>
    <message numerus="yes">
        <location line="+2"/>
        <source>Joined (%n neighbour(s))</source>
        <translation>
            <numerusform>已加入（%n 个邻居）</numerusform>
        </translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Looking for members</source>
        <translation>正在寻找成员</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Open</source>
        <translation>开放</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Leave</source>
        <translation>离开</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Copy link</source>
        <translation>复制链接</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Change nick...</source>
        <translation>更改昵称...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Copy my member key</source>
        <translation>复制我的成员密钥</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Import admit...</source>
        <translation>导入准入...</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Admit a member key...</source>
        <translation>准入成员密钥...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>New generation</source>
        <translation>新一代</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Room info...</source>
        <translation>房间信息...</translation>
    </message>
</context>
<context>
    <name>eMule::Ed3kRoomTab</name>
    <message>
        <location filename="../src/gui/panels/Ed3kRoomTab.cpp" line="+57"/>
        <source>ed3k room %1</source>
        <translation>ed3k 房间 %1</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>%1 (ed3k)</source>
        <translation>%1 (ed3k)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>closed</source>
        <translation>封闭</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>private</source>
        <translation>私密</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>public</source>
        <translation>公开</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>moderated</source>
        <translation>受管理</translation>
    </message>
    <message>
        <location line="+124"/>
        <source>a post not held</source>
        <translation>未保存的帖子</translation>
    </message>
</context>
<context>
    <name>eMule::Ed3kSetsDialog</name>
    <message>
        <location filename="../src/gui/dialogs/Ed3kSetsDialog.cpp" line="+89"/>
        <location line="+497"/>
        <source>ed3k Sets</source>
        <translation>ed3k 集合</translation>
    </message>
    <message>
        <location line="-487"/>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Author</source>
        <translation>作者</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Version</source>
        <translation>版本</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Files</source>
        <translation>文件</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+6"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location line="-6"/>
        <source>New Versions</source>
        <translation>新版本</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+6"/>
        <location line="+41"/>
        <source>Status</source>
        <translation>状态</translation>
    </message>
    <message>
        <location line="-41"/>
        <source>File</source>
        <translation>文件</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Follow Link...</source>
        <translation>关注链接...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unfollow</source>
        <translation>取消关注</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>New versions:</source>
        <translation>新版本：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Download Selected</source>
        <translation>下载所选</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Download Parts</source>
        <translation>下载部分</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Sets</source>
        <translation>集合</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Channel</source>
        <translation>频道</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Peers</source>
        <translation>下载数</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Segments</source>
        <translation>分段</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Received</source>
        <translation>已接收</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Players</source>
        <translation>播放器</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Watch Link...</source>
        <translation>观看链接...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Open</source>
        <translation>开放</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Stop</source>
        <translation>停止</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Live Streams</source>
        <translation>直播流</translation>
    </message>
    <message>
        <location line="+42"/>
        <source>(hidden)</source>
        <translation>（隐藏）</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Error</source>
        <translation>错误</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>New version</source>
        <translation>新版本</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Looking for it</source>
        <translation>正在查找</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Waiting for ed3k</source>
        <translation>正在等待 ed3k</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+57"/>
        <source>Complete</source>
        <translation>已完成</translation>
    </message>
    <message>
        <location line="-57"/>
        <source>Downloading (%1 of %2)</source>
        <translation>正在下载（%1 / %2）</translation>
    </message>
    <message>
        <location line="+48"/>
        <source>Download</source>
        <translation>下载</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Ask</source>
        <translation>询问</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Not selected</source>
        <translation>未选择</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Waiting</source>
        <translation>等待中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloading</source>
        <translation>正在下载</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Failed</source>
        <translation>失败</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paused</source>
        <translation>已暂停</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Joining</source>
        <translation>正在加入</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Playing</source>
        <translation>正在播放</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Interrupted</source>
        <translation>已中断</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>No peers</source>
        <translation>无对等节点</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ended</source>
        <translation>已结束</translation>
    </message>
    <message>
        <location line="+50"/>
        <source>Folder: %1</source>
        <translation>文件夹：%1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Version %1 is waiting: %2 added, %3 changed, %4 removed.</source>
        <translation>版本 %1 正在等待：新增 %2，更改 %3，删除 %4。</translation>
    </message>
    <message numerus="yes">
        <location line="+7"/>
        <source>%n open part(s) by other authors.</source>
        <translation>
            <numerusform>其他作者的 %n 个开放部分。</numerusform>
        </translation>
    </message>
    <message>
        <location line="+96"/>
        <source>Follow ed3k Set</source>
        <translation>关注 ed3k 集合</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>ed3k:set link:</source>
        <translation>ed3k:set 链接：</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>The set could not be followed.</source>
        <translation>无法关注该集合。</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Unfollow ed3k Set</source>
        <translation>取消关注 ed3k 集合</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Stop following &quot;%1&quot;? The files downloaded stay where they are.</source>
        <translation>取消关注“%1”？已下载的文件保留在原处。</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>The selection could not be set.</source>
        <translation>无法设置所选内容。</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>The parts could not be downloaded.</source>
        <translation>无法下载这些部分。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Watch ed3k Live Stream</source>
        <translation>观看 ed3k 直播流</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>ed3k:live link:</source>
        <translation>ed3k:live 链接：</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>The stream could not be watched.</source>
        <translation>无法观看该直播流。</translation>
    </message>
</context>
<context>
    <name>eMule::Ed3kStakeGroup</name>
    <message>
        <location filename="../src/gui/dialogs/Ed3kStakeGroup.cpp" line="+77"/>
        <source>Stake tokens (E3S)</source>
        <translation>质押代币 (E3S)</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+11"/>
        <location line="+207"/>
        <source>Claim</source>
        <translation>领取</translation>
    </message>
    <message>
        <location line="-218"/>
        <source>Amount (E3S)</source>
        <translation>数量 (E3S)</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>To</source>
        <translation>目标</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+72"/>
        <source>Ready</source>
        <translation>就绪</translation>
    </message>
    <message>
        <location line="-72"/>
        <source>State</source>
        <translation>状态</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Burn for stake…</source>
        <translation>销毁换取质押…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Burns credits of this credit address for stake tokens. It cannot be undone and is public.</source>
        <translation>销毁此信用地址的信用以换取质押代币。此操作不可撤销且公开。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Mints a ready claim to its address.</source>
        <translation>将已就绪的领取铸造到其地址。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Refresh</source>
        <translation>刷新</translation>
    </message>
    <message>
        <location line="+34"/>
        <source>Stake tokens need credits in use with ledger endpoints.</source>
        <translation>质押代币需要已在账本端点上使用的信用。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The stake rate is not read yet.</source>
        <translation>尚未读取质押汇率。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Burning for stake is off on this ledger.</source>
        <translation>此账本已关闭销毁换取质押。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>A credit buys %1 E3S, claimable %2 day(s) after the burn.</source>
        <translation>一个信用可购买 %1 E3S，销毁 %2 天后可领取。</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Claiming…</source>
        <translation>正在领取…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Waiting</source>
        <translation>等待中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paid</source>
        <translation>已支付</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>&lt;p&gt;&lt;b&gt;A burn cannot be undone.&lt;/b&gt; The credits are gone; nothing turns stake tokens back into credits. The claim can be minted to the address once its delay has passed.&lt;/p&gt;&lt;p&gt;&lt;b&gt;A burn is public.&lt;/b&gt; The ledger shows it for good, and it ties your credit address to the address it pays, and through the bridge to an address on the C-Chain, where an exchange may know your name. To avoid that link, burn to a new address and move nothing back to the credit address.&lt;/p&gt;&lt;p&gt;Only your node&apos;s own credit address burns, never an anonymous one.&lt;/p&gt;</source>
        <translation>&lt;p&gt;&lt;b&gt;销毁不可撤销。&lt;/b&gt;信用将消失；没有任何方式能把质押代币换回信用。延迟期过后，可将领取铸造到该地址。&lt;/p&gt;&lt;p&gt;&lt;b&gt;销毁是公开的。&lt;/b&gt;账本会永久显示它，并将你的信用地址与其支付的地址关联，再通过跨链桥关联到 C-Chain 上的地址，而交易所可能知道你的姓名。为避免这种关联，请销毁到新地址，并且不要把任何东西转回信用地址。&lt;/p&gt;&lt;p&gt;只有本节点自己的信用地址会进行销毁，绝不会使用匿名地址。&lt;/p&gt;</translation>
    </message>
    <message>
        <location line="+28"/>
        <location line="+42"/>
        <location line="+5"/>
        <location line="+17"/>
        <source>Burn for stake</source>
        <translation>销毁换取质押</translation>
    </message>
    <message>
        <location line="-58"/>
        <source> credits</source>
        <translation> 信用</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Burn:</source>
        <translation>销毁：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>To my credit address (%1)</source>
        <translation>到我的信用地址（%1）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>To another address:</source>
        <translation>到其他地址：</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Burn…</source>
        <translation>销毁…</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>The address is 0x and 40 hexadecimal digits, not zero.</source>
        <translation>地址为 0x 加 40 位十六进制数，且不能为零。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Burn %1 credits for stake tokens to %2?</source>
        <translation>销毁 %1 信用以将质押代币发送到 %2？</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>This cannot be undone, and it is public.</source>
        <translation>此操作不可撤销，且是公开的。</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>The burn was sent; its claim shows here once it is mined.</source>
        <translation>销毁已发送；打包上链后其领取将显示在此处。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The burn was refused (error %1).</source>
        <translation>销毁被拒绝（错误 %1）。</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>The claim was refused (error %1).</source>
        <translation>领取被拒绝（错误 %1）。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>The burn did not go through (revert %1).</source>
        <translation>销毁未成功（revert %1）。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The claim did not go through (revert %1).</source>
        <translation>领取未成功（revert %1）。</translation>
    </message>
</context>
<context>
    <name>eMule::ExternalLinkHandler</name>
    <message>
        <location filename="../src/gui/app/ExternalLinkHandler.cpp" line="+227"/>
        <source>ed3k link</source>
        <translation>ed3k 链接</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>The ed3k link could not be added.</source>
        <translation>无法添加 ed3k 链接。</translation>
    </message>
    <message>
        <location line="+115"/>
        <source>ed3k node list</source>
        <translation>ed3k 节点列表</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>A link wants to add a source of ed3k node lists:

%1

Node lists are used only to find the first ed3k nodes. Add this source?</source>
        <translation>一个链接想要添加 ed3k 节点列表的来源：

%1

节点列表仅用于查找最初的 ed3k 节点。添加此来源？</translation>
    </message>
    <message>
        <location line="+21"/>
        <location line="+34"/>
        <location line="+46"/>
        <source>ed3k set</source>
        <translation>ed3k 集合</translation>
    </message>
    <message>
        <location line="-79"/>
        <source>This ed3k set is no website. Follow it and download its files?</source>
        <translation>此 ed3k 集合不是网站。关注它并下载其文件？</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Follow this ed3k set%1?

Its files go to the incoming folder, below a folder of the set&apos;s own; new versions are downloaded as they come.</source>
        <translation>关注此 ed3k 集合%1？

其文件将保存到下载完成目录下该集合自己的文件夹中；新版本发布后即自动下载。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Follow and Download</source>
        <translation>关注并下载</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Choose Files...</source>
        <translation>选择文件...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Open as Website</source>
        <translation>作为网站打开</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>The set could not be followed.</source>
        <translation>无法关注该集合。</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>ed3k live stream</source>
        <translation>ed3k 直播流</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>The stream could not be watched.</source>
        <translation>无法观看该直播流。</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Version %1 of the ed3k set &quot;%2&quot; is here: %3 new and %4 changed file(s).</source>
        <translation>ed3k 集合“%2”的版本 %1 已到达：%3 个新文件，%4 个已更改文件。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Show</source>
        <translation>显示</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Later</source>
        <translation>稍后</translation>
    </message>
</context>
<context>
    <name>eMule::FileDetailDialog</name>
    <message>
        <location filename="../src/gui/dialogs/FileDetailDialog.cpp" line="+150"/>
        <source>File Details: %1</source>
        <translation>文件详情：%1</translation>
    </message>
    <message>
        <location line="-38"/>
        <location line="+49"/>
        <location line="+7"/>
        <source>General</source>
        <translation>常规</translation>
    </message>
    <message>
        <location line="-6"/>
        <location line="+7"/>
        <source>File Names</source>
        <translation>文件名</translation>
    </message>
    <message>
        <location line="-56"/>
        <location line="+50"/>
        <location line="+7"/>
        <source>Comments</source>
        <translation>评论</translation>
    </message>
    <message>
        <location line="-56"/>
        <location line="+50"/>
        <location line="+7"/>
        <source>Media Info</source>
        <translation>媒体信息</translation>
    </message>
    <message>
        <location line="-6"/>
        <location line="+7"/>
        <source>Metadata</source>
        <translation>元数据</translation>
    </message>
    <message>
        <location line="-57"/>
        <location line="+51"/>
        <location line="+7"/>
        <source>ED2K Link</source>
        <translation>ED2K 链接</translation>
    </message>
    <message>
        <location line="+10"/>
        <location line="+2"/>
        <source>Archive Preview</source>
        <translation>压缩包预览</translation>
    </message>
    <message>
        <location line="+29"/>
        <location line="+20"/>
        <location line="+56"/>
        <source>File Name</source>
        <translation>文件名</translation>
    </message>
    <message>
        <location line="-75"/>
        <location line="+30"/>
        <source>Hash (MD4)</source>
        <translation>哈希 (MD4)</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>AICH Hash</source>
        <translation>AICH 哈希</translation>
    </message>
    <message>
        <location line="-33"/>
        <location line="+37"/>
        <source>File Size</source>
        <translation>文件大小</translation>
    </message>
    <message>
        <location line="-37"/>
        <location line="+37"/>
        <source>%1 (%2 Bytes)</source>
        <translation>%1 (%2 Bytes)</translation>
    </message>
    <message>
        <location line="-36"/>
        <location line="+41"/>
        <source>Completed</source>
        <translation>已完成</translation>
    </message>
    <message>
        <location line="-37"/>
        <location line="+43"/>
        <source>Status</source>
        <translation>状态</translation>
    </message>
    <message>
        <location line="-42"/>
        <location line="+43"/>
        <source>Priority</source>
        <translation>优先级</translation>
    </message>
    <message>
        <location line="-42"/>
        <location line="+47"/>
        <location line="+20"/>
        <source>Sources</source>
        <translation>来源</translation>
    </message>
    <message>
        <location line="-47"/>
        <source>File Path</source>
        <translation>文件路径</translation>
    </message>
    <message>
        <location line="-16"/>
        <location line="+46"/>
        <source>Created</source>
        <translation>创建时间</translation>
    </message>
    <message numerus="yes">
        <location line="-175"/>
        <source>File Details: %n files</source>
        <translation>
            <numerusform>文件详情：%n 个文件</numerusform>
        </translation>
    </message>
    <message>
        <location line="+122"/>
        <location line="+43"/>
        <source>Transferred</source>
        <translation>已传输</translation>
    </message>
    <message>
        <location line="-35"/>
        <location line="+46"/>
        <source>Last Seen Complete</source>
        <translation>最后一次见到完整文件</translation>
    </message>
    <message>
        <location line="-45"/>
        <location line="+46"/>
        <source>Last Reception</source>
        <translation>最后接收</translation>
    </message>
    <message>
        <source>Part.met File</source>
        <translation type="vanished">part.met 文件</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>No alternative file names reported by sources. Use “Search Kad” to look them up.</source>
        <translation>来源未报告备用文件名。使用“搜索 Kad”进行查找。</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>&amp;Take over</source>
        <translation>采用(&amp;T)</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>&amp;Cleanup</source>
        <translation>清理(&amp;C)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Rename</source>
        <translation>重命名</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Take over</source>
        <translation>采用</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Copy</source>
        <translation>复制</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Search Kad</source>
        <translation>搜索 Kad</translation>
    </message>
    <message>
        <location line="+92"/>
        <source>No media information available.</source>
        <translation>没有可用的媒体信息。</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Title</source>
        <translation>标题</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Artist</source>
        <translation>艺术家</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Album</source>
        <translation>专辑</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Codec</source>
        <translation>编解码器</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Bitrate</source>
        <translation>比特率</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Length</source>
        <translation>时长</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Link Options</source>
        <translation>链接选项</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Include Hashset</source>
        <translation>包含哈希集</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Include Hostname</source>
        <translation>包含主机名</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add your hostname or public IPv6 as a source</source>
        <translation>将您的主机名或公网 IPv6 添加为来源</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>HTML Format</source>
        <translation>HTML 格式</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Include Media Info</source>
        <translation>包含媒体信息</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add a line with resolution, codecs, length and bitrate under the HTML link of a complete file</source>
        <translation>在完整文件的 HTML 链接下添加一行，显示分辨率、编解码器、时长和比特率</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Copy to Clipboard</source>
        <translation>复制到剪贴板</translation>
    </message>
</context>
<context>
    <name>eMule::FindInListDialog</name>
    <message>
        <location filename="../src/gui/dialogs/FindInListDialog.cpp" line="+66"/>
        <location line="+55"/>
        <source>Search</source>
        <translation>搜索</translation>
    </message>
    <message>
        <location line="-54"/>
        <source>No matching entry found.</source>
        <translation>未找到匹配的项目。</translation>
    </message>
    <message>
        <location line="+68"/>
        <source>Search for:</source>
        <translation>搜索：</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Search in column:</source>
        <translation>在列中搜索：</translation>
    </message>
</context>
<context>
    <name>eMule::FirstStartWizard</name>
    <message>
        <location filename="../src/gui/dialogs/FirstStartWizard.cpp" line="+62"/>
        <source>eMule First Runtime Wizard</source>
        <translation>eMule 首次运行向导</translation>
    </message>
    <message>
        <location line="+214"/>
        <source>Show more controls (advanced mode controls)</source>
        <translation>显示更多控件（高级模式控件）</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Shows advanced entries in context menus and extra details in lists.</source>
        <translation>在右键菜单中显示高级项目，并在列表中显示更多详细信息。</translation>
    </message>
    <message>
        <location line="+147"/>
        <source>Ports and Connection</source>
        <translation>端口和连接</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Connection</source>
        <translation>连接</translation>
    </message>
    <message>
        <location line="-273"/>
        <source>eMule uses two ports for communication with servers and clients. These ports must be free and available for remote clients. The TCP port must be available to ensure the main functionality of eMule. The UDP port is used for Kad (serverless network) and to reduce network usage (Overhead).</source>
        <translation>eMule 使用两个端口与服务器和客户端通信。这些端口必须空闲并对远程客户端可用。TCP 端口必须可用以确保 eMule 的主要功能。UDP 端口用于 Kad（无服务器网络）和减少网络使用（开销）。</translation>
    </message>
    <message>
        <location line="-101"/>
        <location line="+308"/>
        <source>Unlimited</source>
        <translation>无限制</translation>
    </message>
    <message>
        <source>This wizard will guide you through the first steps in configuring eMule.</source>
        <translation type="vanished">此向导将引导您完成 eMule 的初步配置。</translation>
    </message>
    <message>
        <source>To continue, click Next.</source>
        <translation type="vanished">要继续,请单击“下一步”。</translation>
    </message>
    <message>
        <source>Please enter your user name:</source>
        <translation type="vanished">请输入您的用户名:</translation>
    </message>
    <message>
        <source>Start eMule when the computer starts.</source>
        <translation type="vanished">计算机启动时启动 eMule。</translation>
    </message>
    <message>
        <source>Enable this option if you want eMule to connect at startup.</source>
        <translation type="vanished">如果希望 eMule 在启动时连接,请启用此选项。</translation>
    </message>
    <message>
        <source>Enable this option if you want eMule to manage your download priorities.

Turning this on will allow eMule to make sure downloads with a lot of sources do not interfere with downloads that have few sources. This option will only affect future downloads.</source>
        <translation type="vanished">如果希望 eMule 管理下载优先级,请启用此选项。

启用后,eMule 会确保来源多的下载不会妨碍来源少的下载。此选项只影响以后的下载。</translation>
    </message>
    <message>
        <source>Enable this option if you want eMule to manage your upload priorities.

Turning this on will allow eMule to boost rare files meaning popular files will be harder for other people to get. Turning this off will allow eMule to upload popular files more often meaning rare files will be harder for other people to get. This option will only affect future shared files.</source>
        <translation type="vanished">如果希望 eMule 管理上传优先级,请启用此选项。

启用后,eMule 会优先稀有文件,这意味着其他人更难获得热门文件。关闭后,eMule 会更频繁地上传热门文件,这意味着其他人更难获得稀有文件。此选项只影响以后共享的文件。</translation>
    </message>
    <message>
        <source>Enable this option if you want to use protocol obfuscation

If your ISP tries throttle or block eMule, enabling obfuscation will help to circumvent such restrictions.</source>
        <translation type="vanished">如果要使用协议混淆,请启用此选项

如果您的网络服务商试图限速或屏蔽 eMule,启用混淆有助于绕过此类限制。</translation>
    </message>
    <message>
        <location line="-194"/>
        <source>You can change the ports here while no network activities have started.</source>
        <translation>您可以在尚未开始网络活动时在此更改端口。</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>TCP:</source>
        <translation>TCP：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>UDP:</source>
        <translation>UDP：</translation>
    </message>
    <message>
        <source>Disable</source>
        <translation type="vanished">禁用</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Use UPnP to Setup Ports</source>
        <translation>使用 UPnP 设置端口</translation>
    </message>
    <message>
        <location line="+45"/>
        <source>Here you can test, if your TCP and UDP port can be connected to from remote. This success of this test is required for servers and clients to connect you. The TCP port have to succeed!</source>
        <translation>您可以在此测试 TCP 和 UDP 端口能否从外部连接。服务器和客户端要连接您,此测试必须成功。TCP 端口必须通过!</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Test Ports</source>
        <translation>测试端口</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Open Router</source>
        <translation>打开路由器</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Opens the web page of this computer&apos;s router, where ports are forwarded</source>
        <translation>打开此计算机路由器的网页，可在其中设置端口转发</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Choose which Network(s) you want to use</source>
        <translation>选择要使用的网络</translation>
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
        <translation type="vanished">安全连接</translation>
    </message>
    <message>
        <source>Turning this feature off allows eMule to connect to servers a little faster, but can cause you to get more false LowID connects.</source>
        <translation type="vanished">关闭此功能可让 eMule 稍快地连接服务器,但可能导致更多错误的 LowID 连接。</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>Select your internet connection. eMule derives its download and upload limits from it, leaving room for your other applications. You can change the limits at any time in the Options.</source>
        <translation>请选择您的互联网连接。eMule 会据此确定下载和上传限速，并为其他应用程序留出余量。您可以随时在选项中更改限速。</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Type</source>
        <translation>类型</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Down</source>
        <translation>下载</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Up</source>
        <translation>上传</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Keep current settings</source>
        <translation>保留当前设置</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unknown (recommended defaults)</source>
        <translation>未知（推荐默认值）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Custom</source>
        <translation>自定义</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+1"/>
        <source>(enter below)</source>
        <translation>（在下方输入）</translation>
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
        <translation>下载：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload:</source>
        <translation>上传：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source> KB/s</source>
        <translation> KB/s</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Download limit:</source>
        <translation>下载限速：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload limit:</source>
        <translation>上传限速：</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>&lt; Back</source>
        <translation>&lt; 上一步</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+28"/>
        <source>Next &gt;</source>
        <translation>下一步 &gt;</translation>
    </message>
    <message>
        <location line="+34"/>
        <source>UDP is disabled in the config file (udpPort: 0)</source>
        <translation>UDP 已在配置文件中禁用 (udpPort: 0)</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Kad needs UDP, which is disabled in the config file</source>
        <translation>Kad 需要 UDP，但它已在配置文件中禁用</translation>
    </message>
    <message>
        <source>Welcome to eMule</source>
        <translation type="vanished">欢迎使用 eMule</translation>
    </message>
    <message>
        <source>General</source>
        <translation type="vanished">常规</translation>
    </message>
    <message>
        <source>User Name</source>
        <translation type="vanished">用户名</translation>
    </message>
    <message>
        <source>Download</source>
        <translation type="vanished">下载</translation>
    </message>
    <message>
        <source>Upload</source>
        <translation type="vanished">上传</translation>
    </message>
    <message>
        <source>Priority</source>
        <translation type="vanished">优先级</translation>
    </message>
    <message>
        <source>Security</source>
        <translation type="vanished">安全</translation>
    </message>
    <message>
        <source>Obfuscation</source>
        <translation type="vanished">混淆</translation>
    </message>
    <message>
        <location line="-43"/>
        <source>Connection Speed</source>
        <translation>连接速度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bandwidth</source>
        <translation>带宽</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Finish</source>
        <translation>完成</translation>
    </message>
    <message>
        <location line="-25"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Help</source>
        <translation>帮助</translation>
    </message>
    <message>
        <source>Download limit: %1    Upload limit: %2</source>
        <translation type="vanished">下载限速：%1    上传限速：%2</translation>
    </message>
    <message>
        <location line="+204"/>
        <source>The ports are forwarded when the core starts.</source>
        <translation>端口将在核心启动时转发。</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>The core did not accept the port settings.</source>
        <translation>核心未接受端口设置。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>No answer from the core about port forwarding.</source>
        <translation>核心未就端口转发作出答复。</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Network</source>
        <translation>网络</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>You must enable at least one network (Kad or eD2K).</source>
        <translation>您必须启用至少一个网络（Kad 或 eD2K）。</translation>
    </message>
    <message>
        <source>UPnP</source>
        <translation type="vanished">UPnP</translation>
    </message>
    <message>
        <source>UPnP port mapping timed out. Your router may not support UPnP, or it may be disabled. You can set up port forwarding manually.</source>
        <translation type="vanished">UPnP 端口映射超时。您的路由器可能不支持 UPnP，或者已被禁用。您可以手动设置端口转发。</translation>
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
        <translation>ed3k 好友，请求待处理</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>ed3k friend</source>
        <translation>ed3k 好友</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>ed3k friend, not confirmed by the other side</source>
        <translation>ed3k 好友，未经对方确认</translation>
    </message>
</context>
<context>
    <name>eMule::HosterPanel</name>
    <message>
        <location filename="../src/gui/panels/HosterPanel.cpp" line="+51"/>
        <source>Finding a resolver</source>
        <translation>正在查找解析器</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Looking up in ed3k</source>
        <translation>正在 ed3k 中查找</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Checking</source>
        <translation>正在检查</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Queued</source>
        <translation>排队中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Waiting</source>
        <translation>等待中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloading</source>
        <translation>正在下载</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Puzzle</source>
        <translation>验证码</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paused</source>
        <translation>已暂停</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Complete</source>
        <translation>已完成</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>File removed</source>
        <translation>文件已删除</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Needs you</source>
        <translation>需要操作</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>No resolver</source>
        <translation>无解析器</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Failed</source>
        <translation>失败</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Waiting for the ed3k node</source>
        <translation>正在等待 ed3k 节点</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>This build has no hoster resolver</source>
        <translation>此版本没有网盘解析器</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The resolver is not ready: %1</source>
        <translation>解析器未就绪:%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>No resolver plugin for %1</source>
        <translation>没有适用于 %1 的解析器插件</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloads from hosters are off (Options, ed3k, Hoster)</source>
        <translation>网盘下载已关闭(选项 &gt; ed3k &gt; 网盘)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The link was refused (%1)</source>
        <translation>链接被拒绝(%1)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Busy</source>
        <translation>忙碌</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hoster timer</source>
        <translation>网盘等待计时</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hoster quota</source>
        <translation>网盘配额</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hoster busy</source>
        <translation>网盘忙碌</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The hoster wants a login (Options, ed3k, Hoster)</source>
        <translation>网盘要求登录(选项 &gt; ed3k &gt; 网盘)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The hoster wants a puzzle this resolver cannot show</source>
        <translation>网盘要求此解析器无法显示的验证码</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>A puzzle, a login or a quota stands in the way</source>
        <translation>验证码、登录或配额限制阻碍了下载</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The hoster ended the transfer early</source>
        <translation>网盘提前结束了传输</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The hoster says the file was removed</source>
        <translation>网盘称文件已被删除</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The hoster&apos;s file is not the one ed3k knows</source>
        <translation>网盘上的文件与 ed3k 已知的文件不符</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The hoster&apos;s file has another size</source>
        <translation>网盘上的文件大小不同</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Transfer failed</source>
        <translation>传输失败</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Resolver error: %1</source>
        <translation>解析器错误:%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Cannot write the file: %1</source>
        <translation>无法写入文件:%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Cannot move the file to %1</source>
        <translation>无法将文件移动到 %1</translation>
    </message>
    <message numerus="yes">
        <location line="+1"/>
        <source>Folder: %n link(s)</source>
        <translation>
            <numerusform>文件夹:%n 个链接</numerusform>
        </translation>
    </message>
    <message>
        <location line="+1"/>
        <source>From a link a peer named (%1)</source>
        <translation>来自对等方提供的链接 (%1)</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Hoster downloads</source>
        <translation>网盘下载</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+177"/>
        <source>Add links...</source>
        <translation>添加链接...</translation>
    </message>
    <message>
        <location line="-176"/>
        <source>Options...</source>
        <translation>选项...</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>File Name</source>
        <translation>文件名</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Progress</source>
        <translation>进度</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Speed</source>
        <translation>速度</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Hoster</source>
        <translation>网盘</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Status</source>
        <translation>状态</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Source</source>
        <translation>来源</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Add hoster links...</source>
        <translation>添加网盘链接...</translation>
    </message>
    <message>
        <location line="+37"/>
        <location line="+25"/>
        <source>Hoster links</source>
        <translation>网盘链接</translation>
    </message>
    <message>
        <location line="-18"/>
        <source>Add hoster links</source>
        <translation>添加网盘链接</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Links to files at hosters, one per line:</source>
        <translation>网盘文件链接,每行一个:</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Not connected to the eMule core.</source>
        <translation>未连接到 eMule 核心。</translation>
    </message>
    <message>
        <location line="+75"/>
        <source>ed3k + link</source>
        <translation>ed3k + 链接</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>link</source>
        <translation>链接</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Known in ed3k: every block is verified, peers help</source>
        <translation>ed3k 已知:每个块均经校验,有节点协助</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unknown in ed3k: checked as a whole when complete</source>
        <translation>ed3k 未知:完成后整体校验</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Pause</source>
        <translation>暂停</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Resume</source>
        <translation>恢复</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Remove</source>
        <translation>删除</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+22"/>
        <source>Remove and delete file</source>
        <translation>移除并删除文件</translation>
    </message>
    <message>
        <location line="-20"/>
        <source>Copy link</source>
        <translation>复制链接</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Open folder</source>
        <translation>打开文件夹</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Delete the selected files from disk?</source>
        <translation>要从磁盘删除所选文件吗?</translation>
    </message>
    <message>
        <location line="+56"/>
        <source>Links of a folder</source>
        <translation>文件夹中的链接</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The link names a folder. Add which of its files?</source>
        <translation>此链接指向一个文件夹。要添加其中哪些文件?</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>Downloads from hosters are off (Options, ed3k, Hoster).</source>
        <translation>网盘下载已关闭(选项 &gt; ed3k &gt; 网盘)。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Resolver not ready: %1</source>
        <translation>解析器未就绪:%1</translation>
    </message>
</context>
<context>
    <name>eMule::HosterPuzzleDialog</name>
    <message>
        <location filename="../src/gui/dialogs/HosterPuzzleDialog.cpp" line="+22"/>
        <source>Puzzle from %1</source>
        <translation>来自 %1 的验证码</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>For %1</source>
        <translation>用于 %1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>The puzzle image could not be shown.</source>
        <translation>无法显示验证码图片。</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Type what the image shows</source>
        <translation>输入图片中显示的内容</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Skip</source>
        <translation>跳过</translation>
    </message>
    <message numerus="yes">
        <location line="+24"/>
        <source>%n second(s) left</source>
        <translation>
            <numerusform>剩余 %n 秒</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>eMule::ImportDownloadsDialog</name>
    <message>
        <location filename="../src/gui/dialogs/ImportDownloadsDialog.cpp" line="+40"/>
        <source>Convert Part Files</source>
        <translation>转换 Part 文件</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Current Job</source>
        <translation>当前任务</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+224"/>
        <source>Idle</source>
        <translation>空闲</translation>
    </message>
    <message>
        <location line="-213"/>
        <source>Job Queue</source>
        <translation>任务队列</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Filename</source>
        <translation>文件名</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Status</source>
        <translation>状态</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>File Hash</source>
        <translation>文件哈希</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Add Imports...</source>
        <translation>添加导入...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Retry Selected</source>
        <translation>重试所选</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove Selected</source>
        <translation>删除所选</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Close</source>
        <translation>关闭</translation>
    </message>
    <message>
        <location line="+57"/>
        <location line="+13"/>
        <source>Import Downloads</source>
        <translation>导入下载</translation>
    </message>
    <message>
        <location line="-12"/>
        <source>Import Downloads is only available for local connections.</source>
        <translation>导入下载仅适用于本地连接。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Select folder to scan for importable downloads</source>
        <translation>选择要扫描可导入下载的文件夹</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Do you want to delete the source files after a successful import?</source>
        <translation>导入成功后是否删除源文件？</translation>
    </message>
    <message>
        <location line="+118"/>
        <source>Converting...</source>
        <translation>正在转换...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Done</source>
        <translation>完成</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>OK</source>
        <translation>确定</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Queued</source>
        <translation>排队中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>In Progress</source>
        <translation>进行中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Out of Disk Space</source>
        <translation>磁盘空间不足</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>.part.met Not Found</source>
        <translation>未找到 .part.met</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>I/O Error</source>
        <translation>I/O 错误</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Failed</source>
        <translation>失败</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bad Format</source>
        <translation>格式错误</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Already Exists</source>
        <translation>已存在</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unknown</source>
        <translation>未知</translation>
    </message>
</context>
<context>
    <name>eMule::IndexerResultsModel</name>
    <message>
        <location filename="../src/gui/controls/IndexerResultsModel.cpp" line="+26"/>
        <source>today</source>
        <translation>今天</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>1 day</source>
        <translation>1 天</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1 days</source>
        <translation>%1 天</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1 months</source>
        <translation>%1 个月</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1 years</source>
        <translation>%1 年</translation>
    </message>
    <message>
        <location line="+109"/>
        <source>Posted: %1</source>
        <translation>发布于：%1</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>%1 files</source>
        <translation>%1 个文件</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Password protected</source>
        <translation>受密码保护</translation>
    </message>
    <message>
        <location line="+31"/>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Age</source>
        <translation>发布时长</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Category</source>
        <translation>分类</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Grabs</source>
        <translation>抓取数</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Indexer</source>
        <translation>索引器</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Seeders</source>
        <translation>做种数</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Peers</source>
        <translation>下载数</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Known</source>
        <translation>已知</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Confidence</source>
        <translation>可信度</translation>
    </message>
</context>
<context>
    <name>eMule::IpcClient</name>
    <message>
        <location filename="../src/gui/app/IpcClient.cpp" line="+963"/>
        <source>The core&apos;s certificate changed</source>
        <translation>核心的证书已更改</translation>
    </message>
</context>
<context>
    <name>eMule::IrcPanel</name>
    <message>
        <location filename="../src/gui/panels/IrcPanel.cpp" line="+146"/>
        <source>Select an IRC nick.</source>
        <translation>选择一个 IRC 昵称。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Should be no longer than 25 characters: letters, digits or symbols [_-{}]\.
Nick can be changed again in Options-&gt;IRC.</source>
        <translation>不超过 25 个字符：字母、数字或符号 [_-{}]\。
昵称可在选项-&gt;IRC 中再次更改。</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Connecting</source>
        <translation>连接中</translation>
    </message>
    <message>
        <location line="+73"/>
        <source>Connected</source>
        <translation>已连接</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Disconnect</source>
        <translation>断开连接</translation>
    </message>
    <message>
        <location line="+27"/>
        <location line="+493"/>
        <source>Connect</source>
        <translation>连接</translation>
    </message>
    <message>
        <location line="-482"/>
        <source>* Disconnected</source>
        <translation>* 已断开连接</translation>
    </message>
    <message>
        <location line="+153"/>
        <source>* %1 has joined %2</source>
        <translation>* %1 加入了 %2</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>* %1 has parted %2 (%3)</source>
        <translation>* %1 离开了 %2 (%3)</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>* %1 has quit (%2)</source>
        <translation>* %1 已退出 (%2)</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>* %1 was kicked by %2 (%3)</source>
        <translation>* %1 被 %2 踢出 (%3)</translation>
    </message>
    <message>
        <location line="+30"/>
        <location filename="../src/gui/panels/IrcPanelEd3k.cpp" line="+302"/>
        <source>* %1 is now known as %2</source>
        <translation>* %1 现在改名为 %2</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>* %1 sets mode: %2 %3</source>
        <translation>* %1 设置模式:%2 %3</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>* Channel Topic: %1</source>
        <translation>* 频道主题:%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>* %1 changes topic to &apos;%2&apos;</source>
        <translation>* %1 将主题改为 &apos;%2&apos;</translation>
    </message>
    <message>
        <location line="+68"/>
        <source>Nick in use</source>
        <translation>昵称已被使用</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The nick &quot;%1&quot; is already in use.
Please choose another:</source>
        <translation>昵称 &quot;%1&quot; 已被使用。
请选择另一个：</translation>
    </message>
    <message>
        <location line="+66"/>
        <location line="+615"/>
        <source>Nick</source>
        <translation>昵称</translation>
    </message>
    <message>
        <location line="-545"/>
        <source>Status</source>
        <translation>状态</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Close</source>
        <translation>关闭</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Smileys</source>
        <translation>表情</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Bold</source>
        <translation>粗体</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Italic</source>
        <translation>斜体</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Underline</source>
        <translation>下划线</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Color</source>
        <translation>颜色</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Reset Formatting</source>
        <translation>重置格式</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Type a message...</source>
        <translation>输入消息...</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Send</source>
        <translation>发送</translation>
    </message>
    <message>
        <location line="+76"/>
        <source>Channel</source>
        <translation>频道</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Users</source>
        <translation>用户</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Topic</source>
        <translation>主题</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Join</source>
        <translation>加入</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Channels</source>
        <translation>频道</translation>
    </message>
    <message>
        <location line="+325"/>
        <source>Nick (%1)</source>
        <translation>昵称 (%1)</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>* Private chat session started</source>
        <translation>* 私聊会话已开始</translation>
    </message>
    <message>
        <location line="+33"/>
        <location filename="../src/gui/panels/IrcPanelEd3k.cpp" line="+220"/>
        <source>Nick : %1</source>
        <translation>昵称: %1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Private Message</source>
        <translation>私信</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Add to friends list</source>
        <translation>添加到好友列表</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Send this to friend: </source>
        <translation>发送给好友: </translation>
    </message>
    <message>
        <location line="+1"/>
        <source>No Shared File Selected</source>
        <translation>未选择共享文件</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Kick</source>
        <translation>踢出</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Ban</source>
        <translation>封禁</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Slap</source>
        <translation>拍打</translation>
    </message>
    <message>
        <location line="+95"/>
        <source>%1 attempted to send you a file. If you wanted to accept the files from this person, enable Receive files in the IRC Preferences.</source>
        <translation>%1 试图向您发送文件。如果想接受此人的文件，请在 IRC 首选项中启用接收文件。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>%1 sent (%2) to be auto started.</source>
        <translation>%1 发送了 (%2)，将自动开始。</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>%1 attempted to send you a file. If you wanted to accept the files from this person, add him as a friend or change your IRC Preferences.</source>
        <translation>%1 试图向您发送文件。如果想接受此人的文件，请将其添加为好友或更改 IRC 首选项。</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>%1 added you as friend!</source>
        <translation>%1 已将您添加为好友！</translation>
    </message>
    <message>
        <location filename="../src/gui/panels/IrcPanelEd3k.cpp" line="-446"/>
        <location line="+20"/>
        <source>Public: anyone with the link</source>
        <translation>公开：任何拥有链接的人</translation>
    </message>
    <message>
        <location line="-19"/>
        <location line="+14"/>
        <source>Private: posts sealed, anyone with the link</source>
        <translation>私密：帖子加密，任何拥有链接的人</translation>
    </message>
    <message>
        <location line="-13"/>
        <location line="+14"/>
        <source>Closed: only admitted members</source>
        <translation>封闭：仅限已准入成员</translation>
    </message>
    <message>
        <location line="-13"/>
        <source>Moderated: only admitted members post</source>
        <translation>受管理：仅已准入成员可发帖</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Title:</source>
        <translation>标题：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>About:</source>
        <translation>简介：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Your nick:</source>
        <translation>你的昵称：</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+9"/>
        <source>Kind:</source>
        <translation>类型：</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>the ed3k node is not running</source>
        <translation>ed3k 节点未运行</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>not a valid room link or admit</source>
        <translation>不是有效的房间链接或准入</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>no key of the current generation yet</source>
        <translation>尚无当前一代的密钥</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>too many rooms (at most 16)</source>
        <translation>房间过多（最多 16 个）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>not allowed (not admitted, banned or no moderator)</source>
        <translation>不允许（未准入、已封禁或非管理员）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>unknown room</source>
        <translation>未知房间</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>a key that cannot be used here</source>
        <translation>无法在此使用的密钥</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>the room keys could not be stored</source>
        <translation>无法保存房间密钥</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>error %1</source>
        <translation>错误 %1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>room %1</source>
        <translation>房间 %1</translation>
    </message>
    <message>
        <location line="+14"/>
        <location line="+227"/>
        <location line="+47"/>
        <location line="+286"/>
        <source>ed3k room</source>
        <translation>ed3k 房间</translation>
    </message>
    <message>
        <location line="-559"/>
        <source>Join the ed3k room &quot;%1&quot;?

Other members see your nick and posts; your node takes part in passing the room&apos;s posts on.</source>
        <translation>加入 ed3k 房间“%1”？

其他成员可以看到你的昵称和帖子；你的节点会参与转发房间的帖子。</translation>
    </message>
    <message>
        <location line="+58"/>
        <source>Rooms (ed3k)</source>
        <translation>房间 (ed3k)</translation>
    </message>
    <message>
        <location line="+87"/>
        <source>* %1 has left the room</source>
        <translation>* %1 离开了房间</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>* %1 has joined the room</source>
        <translation>* %1 加入了房间</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>* Now in the room</source>
        <translation>* 已在房间中</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>* %1 was banned</source>
        <translation>* %1 已被封禁</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>* Room key of generation %1 taken</source>
        <translation>* 已获得第 %1 代房间密钥</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>* The room key of generation %1 is missing: posting waits for a moderator</source>
        <translation>* 缺少第 %1 代房间密钥：发帖需等待管理员</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>* A moderator&apos;s room key (generation %1) does not open</source>
        <translation>* 管理员的房间密钥（第 %1 代）无法打开</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>* A member refused us: not admitted. Send a moderator your member key, then import the admit (Rooms (ed3k) tab)</source>
        <translation>* 有成员拒绝了我们：未准入。请将你的成员密钥发给管理员，然后导入准入（房间 (ed3k) 标签页）</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>* A member refused us: banned in this room</source>
        <translation>* 有成员拒绝了我们：在此房间中被封禁</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>* A member sent a bad item</source>
        <translation>* 有成员发送了无效条目</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Leave this room? You own it: its room key stays on this computer, so joining by its link makes you the owner again.</source>
        <translation>离开此房间？你是所有者：房间密钥保留在此计算机上，因此通过链接加入后你将再次成为所有者。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Leave this room? To come back you need its link.</source>
        <translation>离开此房间？要回来需要它的链接。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Leaving the room failed</source>
        <translation>离开房间失败</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>* Room link copied. It holds the room&apos;s secret: give it to members only</source>
        <translation>* 已复制房间链接。它包含房间的秘密：只发给成员</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>* Room link copied</source>
        <translation>* 已复制房间链接</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>* Your member key of this room was copied: send it to a moderator to be admitted</source>
        <translation>* 已复制你在此房间的成员密钥：发给管理员以获得准入</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Import admit</source>
        <translation>导入准入</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paste the admit text a moderator gave you:</source>
        <translation>粘贴管理员给你的准入文本：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>The admit was not taken</source>
        <translation>准入未被接受</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>* Admit taken in: you can join the room&apos;s members now</source>
        <translation>* 已接受准入：你现在可以加入房间的成员了</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Your nick in this room:</source>
        <translation>你在此房间的昵称：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>The nick was not changed</source>
        <translation>昵称未更改</translation>
    </message>
    <message>
        <location line="+78"/>
        <source>* Not posted: %1</source>
        <translation>* 未发送：%1</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Copy member key</source>
        <translation>复制成员密钥</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Import admit...</source>
        <translation>导入准入...</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+125"/>
        <source>Admit</source>
        <translation>准入</translation>
    </message>
    <message>
        <location line="-124"/>
        <source>Ban (30 days)</source>
        <translation>封禁（30 天）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Lift ban</source>
        <translation>解除封禁</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Remove moderator</source>
        <translation>移除管理员</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Make moderator</source>
        <translation>设为管理员</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Admit a member key...</source>
        <translation>准入成员密钥...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>New generation</source>
        <translation>新一代</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Create an ed3k room</source>
        <translation>创建 ed3k 房间</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>The room was not created</source>
        <translation>房间未创建</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>* ed3k room created. Copy its link from the Rooms (ed3k) tab to invite members</source>
        <translation>* ed3k 房间已创建。从“房间 (ed3k)”标签页复制其链接以邀请成员</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Join an ed3k room</source>
        <translation>加入 ed3k 房间</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Room link (ed3k:room?...):</source>
        <translation>房间链接 (ed3k:room?...)：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>* Not an ed3k room link</source>
        <translation>* 不是 ed3k 房间链接</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>The room was not joined</source>
        <translation>未加入房间</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Room info</source>
        <translation>房间信息</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>The room info was not changed</source>
        <translation>房间信息未更改</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Admit a member</source>
        <translation>准入成员</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The newcomer&apos;s member key (52 characters):</source>
        <translation>新成员的成员密钥（52 个字符）：</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Not admitted</source>
        <translation>未准入</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>The admit is in the clipboard. Hand it to the newcomer, who imports it (Rooms (ed3k) tab, Import admit).</source>
        <translation>准入已在剪贴板中。请交给新成员，由其导入（“房间 (ed3k)”标签页，导入准入）。</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>The moderation failed</source>
        <translation>管理操作失败</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>* New generation %1 started</source>
        <translation>* 已开始第 %1 代</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>* Ban lifted</source>
        <translation>* 已解除封禁</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>The moderators were not changed</source>
        <translation>管理员未更改</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>no core connection</source>
        <translation>无核心连接</translation>
    </message>
</context>
<context>
    <name>eMule::KadContactHistogram</name>
    <message>
        <location filename="../src/gui/controls/KadContactHistogram.cpp" line="+194"/>
        <source>Contacts</source>
        <translation>联系人</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Kademlia Network</source>
        <translation>Kademlia 网络</translation>
    </message>
</context>
<context>
    <name>eMule::KadContactsModel</name>
    <message>
        <source>Status</source>
        <translation type="vanished">状态</translation>
    </message>
    <message>
        <source>Client ID</source>
        <translation type="vanished">客户端 ID</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/KadContactsModel.cpp" line="+79"/>
        <source>Type</source>
        <translation>类型</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>ID</source>
        <translation>ID</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Distance</source>
        <translation>距离</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Country</source>
        <translation>国家</translation>
    </message>
</context>
<context>
    <name>eMule::KadLookupGraph</name>
    <message>
        <source>No search selected</source>
        <translation type="vanished">未选择搜索</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/KadLookupGraph.cpp" line="+160"/>
        <location line="+274"/>
        <source>Distance</source>
        <translation>距离</translation>
    </message>
    <message>
        <location line="-272"/>
        <source>Time</source>
        <translation>时间</translation>
    </message>
    <message>
        <location line="+104"/>
        <source>Automatically show active searches</source>
        <translation>自动显示活动中的搜索</translation>
    </message>
    <message>
        <location line="+136"/>
        <source>Routing Table</source>
        <translation>路由表</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1 Node(s)</source>
        <translation>%1 节点(s)</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>No Response</source>
        <translation>无回应</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+14"/>
        <source>Asking</source>
        <translation>正在询问</translation>
    </message>
    <message>
        <location line="-6"/>
        <location line="+10"/>
        <source>Not Asked</source>
        <translation>未请求</translation>
    </message>
    <message>
        <location line="-4"/>
        <source>None</source>
        <translation>无</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Protocol Version</source>
        <translation>协议版本</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Discovered by</source>
        <translation>发现于</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Found Nodes (useful)</source>
        <translation>找到节点(有效的)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Search Results</source>
        <translation>搜索结果</translation>
    </message>
    <message>
        <source>Our node (search initiator)</source>
        <translation type="vanished">我们的节点（搜索发起者）</translation>
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
        <translation>▸ 联系人 (0)</translation>
    </message>
    <message>
        <location line="-484"/>
        <location line="+12"/>
        <location line="+419"/>
        <location line="+99"/>
        <source>▸ Current Searches (0)</source>
        <translation>▸ 当前搜索 (0)</translation>
    </message>
    <message>
        <location line="-456"/>
        <source>The URL is invalid.</source>
        <translation>URL 无效。</translation>
    </message>
    <message>
        <location line="+153"/>
        <location line="+2"/>
        <location line="+489"/>
        <source>▸ Search Details</source>
        <translation>▸ 搜索详情</translation>
    </message>
    <message>
        <location line="-394"/>
        <source>Recheck Firewall</source>
        <translation>重新检查防火墙</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+256"/>
        <source>Connect</source>
        <translation>连接</translation>
    </message>
    <message>
        <location line="-238"/>
        <source>IP or Address:</source>
        <translation>IP 或地址:</translation>
    </message>
    <message>
        <location line="-254"/>
        <location line="+251"/>
        <location line="+40"/>
        <source>Bootstrap</source>
        <translation>引导</translation>
    </message>
    <message>
        <location line="-300"/>
        <source>Downloading...</source>
        <translation>正在下载...</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+77"/>
        <source>Kademlia</source>
        <translation>Kademlia</translation>
    </message>
    <message>
        <source>Failed to download nodes.dat: %1</source>
        <translation type="vanished">下载nodes.dat失败: %1</translation>
    </message>
    <message>
        <source>Downloaded nodes.dat is empty.</source>
        <translation type="vanished">下载的nodes.dat为空。</translation>
    </message>
    <message>
        <source>Failed to save nodes.dat: %1</source>
        <translation type="vanished">保存nodes.dat失败: %1</translation>
    </message>
    <message>
        <source>IP Address:</source>
        <translation type="vanished">IP 地址：</translation>
    </message>
    <message>
        <location line="+179"/>
        <source>Port:</source>
        <translation>端口：</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Nodes.dat from URL:</source>
        <translation>从 URL 获取 Nodes.dat：</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>From known clients</source>
        <translation>从已知客户端</translation>
    </message>
    <message>
        <location line="+144"/>
        <location line="+3"/>
        <source>▸ Contacts (%1)</source>
        <translation>▸ 联系人 (%1)</translation>
    </message>
    <message>
        <location line="+41"/>
        <source>▸ Current Searches (%1)</source>
        <translation>▸ 当前搜索 (%1)</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Disconnect</source>
        <translation>断开连接</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="+138"/>
        <source>▸ Search Details (%1)</source>
        <translation>▸ 搜索详情 (%1)</translation>
    </message>
</context>
<context>
    <name>eMule::KadSearchesModel</name>
    <message>
        <source>No.</source>
        <translation type="vanished">编号</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/KadSearchesModel.cpp" line="+36"/>
        <source>Stopping</source>
        <translation>正在停止</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Active</source>
        <translation>活动</translation>
    </message>
    <message>
        <location line="+42"/>
        <source>Number</source>
        <translation>编号</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Key</source>
        <translation>键</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type</source>
        <translation>类型</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status</source>
        <translation>状态</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Load</source>
        <translation>加载</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Packets Sent</source>
        <translation>已发送数据包</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Responses</source>
        <translation>响应</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Search Sources</source>
        <translation>搜索来源</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Search Keywords</source>
        <translation>搜索关键词</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Node Lookup</source>
        <translation>节点查找</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Store File</source>
        <translation>存储文件</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Store Keyword</source>
        <translation>存储关键词</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Find Buddy</source>
        <translation>查找伙伴</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Store Notes</source>
        <translation>存储备注</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Notes</source>
        <translation>备注</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unknown</source>
        <translation>未知</translation>
    </message>
</context>
<context>
    <name>eMule::LogTextView</name>
    <message>
        <location filename="../src/gui/controls/LogTextView.cpp" line="+86"/>
        <source>Autoscroll</source>
        <translation>自动滚动</translation>
    </message>
</context>
<context>
    <name>eMule::LogWidget</name>
    <message>
        <location filename="../src/gui/controls/LogWidget.cpp" line="+87"/>
        <location line="+2"/>
        <source>Server Info</source>
        <translation>服务器信息</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+2"/>
        <source>Log</source>
        <translation>日志</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+2"/>
        <source>Verbose</source>
        <translation>详细</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Leecher</source>
        <translation>吸血客户端</translation>
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
        <translation>ed3k 详细</translation>
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
        <translation>点击此处检查是否有新版本</translation>
    </message>
</context>
<context>
    <name>eMule::MainWindow</name>
    <message>
        <location filename="../src/gui/app/MainWindow.cpp" line="+107"/>
        <location line="+582"/>
        <source>eMule Qt v%1</source>
        <translation>eMule Qt v%1</translation>
    </message>
    <message>
        <location line="-442"/>
        <location line="+9"/>
        <source>New Version Available</source>
        <translation>有新版本可用</translation>
    </message>
    <message>
        <location line="+238"/>
        <source>eD2K: Connected (LowID)</source>
        <translation>eD2K：已连接 (LowID)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>eD2K: Connected</source>
        <translation>eD2K：已连接</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>eD2K: Connecting...</source>
        <translation>eD2K：连接中...</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+1415"/>
        <source>eD2K: Disconnected</source>
        <translation>eD2K：已断开</translation>
    </message>
    <message>
        <location line="-1357"/>
        <source>Kad: Connected</source>
        <translation>Kad：已连接</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Kad: Connected (Firewalled)</source>
        <translation>Kad：已连接（防火墙）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Kad: Connecting...</source>
        <translation>Kad：连接中...</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+1349"/>
        <source>Kad: Disconnected</source>
        <translation>Kad：已断开</translation>
    </message>
    <message>
        <location line="-1297"/>
        <source>Users: %1 | Files: %2</source>
        <translation>用户：%1 | 文件：%2</translation>
    </message>
    <message>
        <location line="+472"/>
        <source>Open Incoming Folder...</source>
        <translation>打开接收文件夹...</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Import Downloads (eM,eD,ON)...</source>
        <translation>导入下载 (eM,eD,ON)...</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>eMule First Runtime Wizard...</source>
        <translation>eMule 首次运行向导...</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>IP Filter...</source>
        <translation>IP 过滤器...</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Paste eD2K Links...</source>
        <translation>粘贴 eD2K 链接...</translation>
    </message>
    <message>
        <location line="+34"/>
        <source>Links</source>
        <translation>链接</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>eMule Homepage</source>
        <translation>eMule 主页</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Features</source>
        <translation>功能</translation>
    </message>
    <message>
        <location line="-902"/>
        <location line="+7"/>
        <location line="+898"/>
        <source>Version Check</source>
        <translation>版本检查</translation>
    </message>
    <message>
        <location line="-966"/>
        <source>Quit eMule Qt</source>
        <translation>退出 eMule Qt</translation>
    </message>
    <message>
        <location line="+41"/>
        <source>eMule Qt %1 has been released.</source>
        <translation>eMule Qt %1 已发布。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Version %1 of eMule Qt was released on %2.</source>
        <translation>eMule Qt 版本 %1 已于 %2 发布。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Version %1 of eMule Qt is available.</source>
        <translation>eMule Qt 版本 %1 现已可用。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>%1

You are running %2. Open the eMule Qt website?</source>
        <translation>%1

您正在运行 %2。是否打开 eMule Qt 网站？</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>You are running the latest version of eMule Qt (v%1).</source>
        <translation>您正在运行最新版本的 eMule Qt (v%1)。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Could not check for a new version:

%1</source>
        <translation>无法检查新版本：

%1</translation>
    </message>
    <message>
        <location line="+67"/>
        <source>Cannot Connect</source>
        <translation>无法连接</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Both the eD2K and Kad networks are disabled.

Enable at least one under Options → Connection to connect.</source>
        <translation>eD2K 和 Kad 网络均已禁用。

请在“选项 → 连接”中至少启用一个后再连接。</translation>
    </message>
    <message>
        <location line="+78"/>
        <source>eMule Version %1 ready</source>
        <translation>eMule 版本 %1 已就绪</translation>
    </message>
    <message>
        <location line="+84"/>
        <source>Blocked: network interface not available</source>
        <translation>已阻止：网络接口不可用</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>eD2K Server: %1  (%2 Users)</source>
        <translation>eD2K 服务器: %1  (%2 位用户)</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Preparing...</source>
        <translation>正在准备...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Waiting...</source>
        <translation>等待...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Error!</source>
        <translation>错误！</translation>
    </message>
    <message>
        <location line="+60"/>
        <source>ed3k: Connected</source>
        <translation>ed3k：已连接</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>ed3k: Connected (Firewalled)</source>
        <translation>ed3k：已连接（防火墙）</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>ed3k: Connecting...</source>
        <translation>ed3k：连接中...</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>open</source>
        <translation>开放</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>cone NAT</source>
        <translation>锥形 NAT</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>symmetric NAT</source>
        <translation>对称 NAT</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>unknown</source>
        <translation>未知</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>ed3k node %1
UDP port: %2
Contacts: %3
IPv4: %4
IPv6: %5
Router: %6</source>
        <translation>ed3k 节点 %1
UDP 端口：%2
联系人：%3
IPv4：%4
IPv6：%5
路由器：%6</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>yes</source>
        <translation>是</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>no</source>
        <translation>否</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>Up: %1(%2)</source>
        <translation>上传:%1(%2)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Down: %1(%2)</source>
        <translation>下载:%1(%2)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Up: %1</source>
        <translation>上传:%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Down: %1</source>
        <translation>下载:%1</translation>
    </message>
    <message>
        <location line="+38"/>
        <source>Connected</source>
        <translation>已连接</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Disconnected</source>
        <translation>已断开</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>eMule Qt v%1 (%2)
Up: %3 | Down: %4</source>
        <translation>eMule Qt v%1 (%2)
上传: %3 | 下载: %4</translation>
    </message>
    <message>
        <location line="+80"/>
        <source>Confirm Exit</source>
        <translation>确认退出</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Are you sure you want to exit eMule?</source>
        <translation>确定要退出 eMule 吗？</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>The eMule core stopped right after it was started.</source>
        <translation>eMule 核心在启动后立即停止了。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>It left no reason, so it may have crashed. Crash reports go to %1.</source>
        <translation>未留下原因，可能已崩溃。崩溃报告保存在 %1。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Reason: %1</source>
        <translation>原因：%1</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>The eMule core at %1 accepts the connection but does not answer. It may be busy or stuck.</source>
        <translation>位于 %1 的 eMule 核心接受了连接但没有应答。它可能正忙或已卡住。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>No eMule core is reachable at %1.</source>
        <translation>在 %1 无法连接到任何 eMule 核心。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The eMule core at %1 refused the connection: %2</source>
        <translation>位于 %1 的 eMule 核心拒绝了连接：%2</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>eMule Core</source>
        <translation>eMule 核心</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Click Retry to start it again.</source>
        <translation>点击“重试”重新启动它。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>eMule keeps trying in the background.</source>
        <translation>eMule 会在后台继续尝试。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Retry</source>
        <translation>重试</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Open Log</source>
        <translation>打开日志</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Quit</source>
        <translation>退出</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Log Entry</source>
        <translation>日志条目</translation>
    </message>
    <message>
        <location line="+141"/>
        <source>eMule Log</source>
        <translation>eMule 日志</translation>
    </message>
    <message>
        <location line="+87"/>
        <source>Open Downloads Folder in Browser</source>
        <translation>在浏览器中打开下载文件夹</translation>
    </message>
    <message>
        <location line="+42"/>
        <source>Open WebUI</source>
        <translation>打开 Web 界面</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Submit Bug Report...</source>
        <translation>提交错误报告...</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Edit Web Services...</source>
        <translation>编辑 Web 服务...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Web Services</source>
        <translation>Web 服务</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>webservices.dat was not found in the config folder.</source>
        <translation>在配置文件夹中未找到 webservices.dat。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Scheduler</source>
        <translation>计划任务</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Disable Scheduler</source>
        <translation>禁用计划任务</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Enable Scheduler</source>
        <translation>启用计划任务</translation>
    </message>
    <message>
        <location line="+41"/>
        <source>Cannot Open Downloads Folder</source>
        <translation>无法打开下载文件夹</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Cannot Open Web Interface</source>
        <translation>无法打开 Web 界面</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not connected to the core.</source>
        <translation>未连接到核心。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Web Interface Disabled</source>
        <translation>Web 界面已禁用</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The web interface is disabled.

Enable it under Options → Web Interface, then try again.</source>
        <translation>Web 界面已禁用。

请在“选项 → Web 界面”中启用，然后重试。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Open Options</source>
        <translation>打开选项</translation>
    </message>
    <message>
        <location line="+196"/>
        <source>Main</source>
        <translation>主菜单</translation>
    </message>
    <message>
        <location line="+162"/>
        <source>Toolbar Skins</source>
        <translation>工具栏皮肤</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Select Toolbar Bitmap...</source>
        <translation>选择工具栏位图...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Select Toolbar Bitmap</source>
        <translation>选择工具栏位图</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Images (*.bmp *.png *.jpg);;All Files (*)</source>
        <translation>图片 (*.bmp *.png *.jpg);;所有文件 (*)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Select Toolbar Bitmap Directory...</source>
        <translation>选择工具栏位图目录...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Select Toolbar Bitmap Directory</source>
        <translation>选择工具栏位图目录</translation>
    </message>
    <message>
        <location line="+17"/>
        <location line="+62"/>
        <source>Default</source>
        <translation>默认</translation>
    </message>
    <message>
        <location line="-26"/>
        <source>Skin Profiles</source>
        <translation>皮肤配置</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Select Skin File...</source>
        <translation>选择皮肤文件...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Select Skin Profile</source>
        <translation>选择皮肤配置</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Skin Files (*.eMuleSkin.ini);;All Files (*)</source>
        <translation>皮肤文件 (*.eMuleSkin.ini);;所有文件 (*)</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Select Skin Directory...</source>
        <translation>选择皮肤目录...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Select Skin Directory</source>
        <translation>选择皮肤目录</translation>
    </message>
    <message>
        <location line="+40"/>
        <source>Text Label Options</source>
        <translation>文字标签选项</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Customize Toolbar...</source>
        <translation>自定义工具栏...</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Disconnect</source>
        <translation>断开连接</translation>
    </message>
    <message>
        <location line="+5"/>
        <location filename="../src/gui/app/ToolbarButtons.h" line="+50"/>
        <source>Connect</source>
        <translation>连接</translation>
    </message>
    <message>
        <location line="+71"/>
        <source>Ready</source>
        <translation>就绪</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Users: 0 | Files: 0</source>
        <translation>用户：0 | 文件：0</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Up: 0.0</source>
        <translation>上传：0.0</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Down: 0.0</source>
        <translation>下载：0.0</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Double-click for Network Information</source>
        <translation>双击查看网络信息</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>New message — double-click to read</source>
        <translation>新消息 — 双击阅读</translation>
    </message>
    <message>
        <location line="+338"/>
        <source>eD2K Link</source>
        <translation>eD2K 链接</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Do you want to download and use the Kad nodes file from
%1 ?</source>
        <translation>是否从以下地址下载并使用 Kad 节点文件？
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
        <translation>未连接到核心</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Starting the core…</source>
        <translation>正在启动核心…</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Connecting to the core… (attempt %1)</source>
        <translation>正在连接核心…（第 %1 次尝试）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Connecting to the core…</source>
        <translation>正在连接核心…</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Waiting for the core to answer…</source>
        <translation>正在等待核心应答…</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Connected to the core</source>
        <translation>已连接到核心</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The core is not responding — retrying (attempt %1)</source>
        <translation>核心没有响应 — 正在重试（第 %1 次尝试）</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The core refused the connection: %1</source>
        <translation>核心拒绝了连接：%1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>The core is still loading its files…</source>
        <translation>核心仍在加载文件…</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>The core is loading: client credits…</source>
        <translation>核心正在加载：客户端积分…</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The core is loading: known files…</source>
        <translation>核心正在加载：已知文件…</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The core is loading: shared files…</source>
        <translation>核心正在加载：共享文件…</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The core is loading: downloads…</source>
        <translation>核心正在加载：下载…</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The core is starting its networks…</source>
        <translation>核心正在启动网络…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The core is loading its files…</source>
        <translation>核心正在加载文件…</translation>
    </message>
    <message>
        <location line="+80"/>
        <source>Low ID</source>
        <translation>Low ID</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Other clients cannot connect to you: eD2K gives you a Low ID and Kad reports you as firewalled.

You get fewer sources and slower downloads, and two Low ID clients can rarely exchange data. Usually your router or firewall does not forward your ports. UPnP or a port forwarding rule in your router fixes it in most cases.</source>
        <translation>其他客户端无法连接到你：eD2K 给你分配了 Low ID，Kad 报告你处于防火墙后。

你会获得更少的来源、更慢的下载，并且两个 Low ID 客户端几乎无法交换数据。通常是路由器或防火墙没有转发你的端口。大多数情况下，开启 UPnP 或在路由器中设置端口转发即可解决。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Open Connection Settings</source>
        <translation>打开连接设置</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>More Info</source>
        <translation>更多信息</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Don&apos;t show this again</source>
        <translation>不再显示</translation>
    </message>
    <message>
        <location filename="../src/gui/app/ToolbarButtons.h" line="+1"/>
        <source>Kad</source>
        <translation>Kad</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Servers</source>
        <translation>服务器</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Transfers</source>
        <translation>传输</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Search</source>
        <translation>搜索</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Shared Files</source>
        <translation>共享文件</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Messages</source>
        <translation>消息</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>IRC</source>
        <translation>IRC</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Statistics</source>
        <translation>统计</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Usenet</source>
        <translation>Usenet</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Torrents</source>
        <translation>种子</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Hoster</source>
        <translation>网盘</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Downloads Folder</source>
        <translation>下载文件夹</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Options</source>
        <translation>选项</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Tools</source>
        <translation>工具</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Help</source>
        <translation>帮助</translation>
    </message>
</context>
<context>
    <name>eMule::MassRenameDialog</name>
    <message>
        <location filename="../src/gui/dialogs/MassRenameDialog.cpp" line="+106"/>
        <source>Mass-Rename</source>
        <translation>批量重命名</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Filename mask (changes here will be done to all filenames below):</source>
        <translation>文件名掩码 (此处的更改将应用于下方所有文件名):</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>&amp;Reset</source>
        <translation>重置(&amp;R)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Filenames left-justified</source>
        <translation>文件名左对齐</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Filenames right-justified</source>
        <translation>文件名右对齐</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>New filenames:</source>
        <translation>新文件名:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Original filenames:</source>
        <translation>原文件名:</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>&amp;Cleanup</source>
        <translation>清理(&amp;C)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Simple cleanup</source>
        <translation>简单清理(&amp;S)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Insert text column from clipboard</source>
        <translation>从剪贴板插入文本列(&amp;I)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>OK</source>
        <translation>确定</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="+63"/>
        <source>Not enough filenames in the list of new filenames. Rename not possible.</source>
        <translation>新文件名列表中的文件名不足。无法重命名。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Invalid filename in line %1. Rename not possible.</source>
        <translation>第 %1 行的文件名无效。无法重命名。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Two or more equal filenames within the same directory are not allowed (Line %1). Rename not possible.</source>
        <translation>同一目录中不允许有两个或更多相同的文件名 (第 %1 行)。无法重命名。</translation>
    </message>
</context>
<context>
    <name>eMule::MediaInfoPanel</name>
    <message>
        <location filename="../src/gui/dialogs/MediaInfoPanel.cpp" line="+44"/>
        <source>Scanning...</source>
        <translation>正在扫描...</translation>
    </message>
    <message>
        <location line="+17"/>
        <location line="+178"/>
        <source>No media information available.</source>
        <translation>没有可用的媒体信息。</translation>
    </message>
    <message>
        <location line="-146"/>
        <source>estimated</source>
        <translation>估算</translation>
    </message>
    <message>
        <location line="+164"/>
        <source>General</source>
        <translation>常规</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Format:</source>
        <translation>格式：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Length:</source>
        <translation>时长：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Video</source>
        <translation>视频</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+17"/>
        <source>Codec:</source>
        <translation>编解码器：</translation>
    </message>
    <message>
        <location line="-16"/>
        <location line="+17"/>
        <source>Bitrate:</source>
        <translation>比特率：</translation>
    </message>
    <message>
        <location line="-16"/>
        <source>Resolution:</source>
        <translation>分辨率：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Aspect Ratio:</source>
        <translation>宽高比：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>FPS:</source>
        <translation>FPS：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Audio</source>
        <translation>音频</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Channels:</source>
        <translation>声道：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Sample Rate:</source>
        <translation>采样率：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Language:</source>
        <translation>语言：</translation>
    </message>
</context>
<context>
    <name>eMule::MessagesPanel</name>
    <message>
        <location filename="../src/gui/panels/MessagesPanel.cpp" line="+168"/>
        <location line="+108"/>
        <location line="+529"/>
        <source> ...failed</source>
        <translation> ...失败</translation>
    </message>
    <message>
        <location line="-635"/>
        <source>Me</source>
        <translation>我</translation>
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
        <translation> ...失败：对方不支持聊天</translation>
    </message>
    <message>
        <location line="+2"/>
        <source> ...failed: refused, try again in %1 s</source>
        <translation> ...失败：被拒绝，请在 %1 秒后重试</translation>
    </message>
    <message>
        <location line="+1"/>
        <source> ...failed: refused</source>
        <translation> ...失败：被拒绝</translation>
    </message>
    <message>
        <location line="+1"/>
        <source> ...failed: not reachable</source>
        <translation> ...失败：无法连接</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Friends (0)</source>
        <translation>好友 (0)</translation>
    </message>
    <message>
        <location line="+41"/>
        <source>Info</source>
        <translation>信息</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Name:</source>
        <translation>名称：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hash:</source>
        <translation>哈希：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Software:</source>
        <translation>软件：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Identification:</source>
        <translation>身份验证：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Uploaded:</source>
        <translation>已上传：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloaded:</source>
        <translation>已下载：</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Messages</source>
        <translation>消息</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>Smileys</source>
        <translation>表情</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Type a message...</source>
        <translation>输入消息...</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Send</source>
        <translation>发送</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+288"/>
        <source>Close</source>
        <translation>关闭</translation>
    </message>
    <message>
        <location line="-264"/>
        <location line="+228"/>
        <location line="+3"/>
        <source>&amp;Details...</source>
        <translation>详情(&amp;D)...</translation>
    </message>
    <message>
        <location line="-227"/>
        <source>Add...</source>
        <translation>添加...</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+245"/>
        <source>Remove</source>
        <translation>删除</translation>
    </message>
    <message>
        <location line="-242"/>
        <source>Send Message</source>
        <translation>发送消息</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>View Shared Files</source>
        <translation>查看共享文件</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>cannot view shared files.</source>
        <translation>无法查看共享文件。</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Establish Friend Slot</source>
        <translation>建立好友位</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Find...</source>
        <translation>查找...</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Copy my ed3k friend link</source>
        <translation>复制我的 ed3k 好友链接</translation>
    </message>
    <message>
        <location line="+35"/>
        <location line="+7"/>
        <source>Friends (%1)</source>
        <translation>好友 (%1)</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Request pending</source>
        <translation>请求待处理</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+46"/>
        <source>Successful</source>
        <translation>成功</translation>
    </message>
    <message>
        <location line="-45"/>
        <source>Not confirmed</source>
        <translation>未确认</translation>
    </message>
    <message>
        <location line="+46"/>
        <source>Invalid</source>
        <translation>无效</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not supported or disabled</source>
        <translation>不支持或已禁用</translation>
    </message>
    <message>
        <location line="+52"/>
        <source>Add to friends list</source>
        <translation>添加到好友列表</translation>
    </message>
    <message>
        <location line="+109"/>
        <source>*** Connecting</source>
        <translation>*** 连接中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>*** Authenticating friend</source>
        <translation>*** 正在验证好友</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>*** Searching friend in Kad</source>
        <translation>*** 正在 Kad 中搜索好友</translation>
    </message>
    <message>
        <location line="+1"/>
        <source> ...found</source>
        <translation> ...已找到</translation>
    </message>
    <message>
        <location line="+1"/>
        <source> ...OK</source>
        <translation> ...OK</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>*** In order to avoid spam messages, this user requires you to solve a captcha before you can send him a message. Please enter the letters you see on this image as response:</source>
        <translation>*** 为了避免垃圾消息，对方要求在发送消息前进行验证。请输入这幅图片中的字母作为回复：</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>*** You have passed the captcha check and the user has received your message.</source>
        <translation>*** 你输入的验证码正确，对方已经收到你的消息。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>*** Your response to the captcha was wrong and your message has been ignored. You can request a new captcha by sending a new message.</source>
        <translation>*** 你输入的验证码不正确，消息已被对方忽略。你可以通过再次发送消息来获取新的验证码。</translation>
    </message>
    <message>
        <location line="+335"/>
        <source>ed3k Friend Request</source>
        <translation>ed3k 好友请求</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The ed3k node %1 asks to become your friend.</source>
        <translation>ed3k 节点 %1 请求成为你的好友。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The ed3k node %1 asks to become your friend:

%2</source>
        <translation>ed3k 节点 %1 请求成为你的好友：

%2</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Accept</source>
        <translation>接受</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Decline</source>
        <translation>拒绝</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Later</source>
        <translation>稍后</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>cannot read the ed3k friend link.</source>
        <translation>无法读取 ed3k 好友链接。</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>ed3k</source>
        <translation>ed3k</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>The ed3k node is not running.</source>
        <translation>ed3k 节点未运行。</translation>
    </message>
    <message>
        <source>Find Friend</source>
        <translation type="vanished">查找好友</translation>
    </message>
</context>
<context>
    <name>eMule::MetaAccountDialog</name>
    <message>
        <location filename="../src/gui/dialogs/MetaAccountDialog.cpp" line="+64"/>
        <source>User name:</source>
        <translation>用户名：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Password:</source>
        <translation>密码：</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Log In</source>
        <translation>登录</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Check Again</source>
        <translation>重新检查</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Log Out</source>
        <translation>退出登录</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>eNode Account — %1</source>
        <translation>eNode 账户 — %1</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>&lt;b&gt;%1&lt;/b&gt; needs an account to download torrent and Usenet search results. Log in, or register an account on the server&apos;s website.</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; 需要账户才能下载种子和 Usenet 搜索结果。请登录，或在服务器网站上注册账户。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Log in to your account on &lt;b&gt;%1&lt;/b&gt;.</source>
        <translation>登录您在 &lt;b&gt;%1&lt;/b&gt; 上的账户。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>is not active yet</source>
        <translation>尚未激活</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>has expired</source>
        <translation>已过期</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>has been disabled by the operator</source>
        <translation>已被运营者停用</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Your account &lt;b&gt;%1&lt;/b&gt; on %2 %3. Complete the steps below on the server&apos;s website, then choose Check Again.</source>
        <translation>您在 %2 上的账户 &lt;b&gt;%1&lt;/b&gt; %3。请在服务器网站上完成以下步骤，然后选择“重新检查”。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>no expiry</source>
        <translation>无期限</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Logged in to &lt;b&gt;%1&lt;/b&gt; as &lt;b&gt;%2&lt;/b&gt;.&lt;br&gt;Access until: %3</source>
        <translation>已以 &lt;b&gt;%2&lt;/b&gt; 身份登录 &lt;b&gt;%1&lt;/b&gt;。&lt;br&gt;访问期限：%3</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>&lt;b&gt;%1&lt;/b&gt; needs no account — its torrent and Usenet results are free to download.</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; 无需账户 — 其种子和 Usenet 结果可自由下载。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Checking the account on &lt;b&gt;%1&lt;/b&gt;...</source>
        <translation>正在检查 &lt;b&gt;%1&lt;/b&gt; 上的账户...</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Open steps:</source>
        <translation>待完成步骤：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Register an account</source>
        <translation>注册账户</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Open account page</source>
        <translation>打开账户页面</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Close</source>
        <translation>关闭</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Not connected to the eMule core.</source>
        <translation>未连接到 eMule 核心。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Enter a user name and a password.</source>
        <translation>请输入用户名和密码。</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Logging in...</source>
        <translation>正在登录...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>The connection to the eMule core was lost.</source>
        <translation>与 eMule 核心的连接已断开。</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>Checking...</source>
        <translation>正在检查...</translation>
    </message>
</context>
<context>
    <name>eMule::MetaResultActions</name>
    <message>
        <location filename="../src/gui/utils/MetaResultActions.cpp" line="+63"/>
        <source>NZB files (*.nzb)</source>
        <translation>NZB 文件 (*.nzb)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Link containers (*.dlc *.ccf *.rsdf)</source>
        <translation>链接容器 (*.dlc *.ccf *.rsdf)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Torrent files (*.torrent)</source>
        <translation>种子文件 (*.torrent)</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Save NZB File</source>
        <translation>保存 NZB 文件</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Save Link Container</source>
        <translation>保存链接容器</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Save Torrent File</source>
        <translation>保存种子文件</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>Save Files To</source>
        <translation>文件保存到</translation>
    </message>
    <message numerus="yes">
        <location line="+23"/>
        <source>Copied %n magnet link(s) to the clipboard.</source>
        <translation>
            <numerusform>已将 %n 个磁力链接复制到剪贴板。</numerusform>
        </translation>
    </message>
    <message>
        <location line="+59"/>
        <source>Queued &quot;%1&quot; for Usenet download.</source>
        <translation>已将“%1”加入 Usenet 下载队列。</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Download</source>
        <translation>下载</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>%1

Download it again?</source>
        <translation>%1

要重新下载吗？</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Could not queue &quot;%1&quot;: %2</source>
        <translation>无法将“%1”加入队列：%2</translation>
    </message>
    <message>
        <location line="+37"/>
        <location line="+59"/>
        <source>Could not download &quot;%1&quot;: %2</source>
        <translation>无法下载“%1”：%2</translation>
    </message>
    <message>
        <location line="-39"/>
        <source>Could not write %1: %2</source>
        <translation>无法写入 %1：%2</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Saved %1</source>
        <translation>已保存 %1</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Could not fetch the links of &quot;%1&quot;: %2</source>
        <translation>无法获取“%1”的链接：%2</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>&quot;%1&quot; holds no links.</source>
        <translation>“%1”不包含任何链接。</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Could not add &quot;%1&quot;: the torrent is empty.</source>
        <translation>无法添加“%1”：种子为空。</translation>
    </message>
    <message>
        <location line="+45"/>
        <source>Search</source>
        <translation>搜索</translation>
    </message>
</context>
<context>
    <name>eMule::MetadataPage</name>
    <message>
        <location filename="../src/gui/dialogs/MetadataPage.cpp" line="+108"/>
        <source>No metadata tags available.</source>
        <translation>没有可用的元数据标签。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Tag Name</source>
        <translation>标签名称</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type</source>
        <translation>类型</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Value</source>
        <translation>值</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Unknown</source>
        <translation>未知</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>Copy</source>
        <translation>复制</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Select All</source>
        <translation>全选</translation>
    </message>
</context>
<context>
    <name>eMule::MiniMuleWidget</name>
    <message>
        <location filename="../src/gui/app/MiniMuleWidget.cpp" line="+85"/>
        <source>Yes</source>
        <translation>是</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>No</source>
        <translation>否</translation>
    </message>
    <message>
        <location line="+116"/>
        <source>Connected</source>
        <translation>已连接</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Upload</source>
        <translation>上传</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Download</source>
        <translation>下载</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Completed</source>
        <translation>已完成</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Free Space</source>
        <translation>可用空间</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Restore Window</source>
        <translation>还原窗口</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Open Incoming Folder</source>
        <translation>打开接收文件夹</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Options</source>
        <translation>选项</translation>
    </message>
</context>
<context>
    <name>eMule::NetworkInfoDialog</name>
    <message>
        <location filename="../src/gui/dialogs/NetworkInfoDialog.cpp" line="+57"/>
        <source>Network Information</source>
        <translation>网络信息</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>&lt;i&gt;Loading...&lt;/i&gt;</source>
        <translation>&lt;i&gt;加载中...&lt;/i&gt;</translation>
    </message>
    <message>
        <location line="+15"/>
        <location line="+10"/>
        <source>&lt;b&gt;Not connected to daemon.&lt;/b&gt;</source>
        <translation>&lt;b&gt;未连接到守护进程。&lt;/b&gt;</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Client</source>
        <translation>客户端</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Nick:</source>
        <translation>昵称:</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+247"/>
        <source>Hash:</source>
        <translation>哈希：</translation>
    </message>
    <message>
        <location line="-245"/>
        <source>TCP Port:</source>
        <translation>TCP 端口:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>UDP Port:</source>
        <translation>UDP 端口:</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>eD2K Network</source>
        <translation>eD2K 网络</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+188"/>
        <source>Connected</source>
        <translation>已连接</translation>
    </message>
    <message>
        <location line="-186"/>
        <location line="+145"/>
        <location line="+39"/>
        <source>Connecting</source>
        <translation>连接中</translation>
    </message>
    <message>
        <location line="-182"/>
        <location line="+145"/>
        <source>Disconnected</source>
        <translation>已断开</translation>
    </message>
    <message>
        <location line="-144"/>
        <location line="+145"/>
        <location line="+114"/>
        <source>Status:</source>
        <translation>状态：</translation>
    </message>
    <message>
        <location line="-253"/>
        <location line="+65"/>
        <location line="+156"/>
        <source>Users:</source>
        <translation>用户数：</translation>
    </message>
    <message>
        <location line="-220"/>
        <location line="+66"/>
        <location line="+156"/>
        <source>Files:</source>
        <translation>文件数：</translation>
    </message>
    <message>
        <location line="-215"/>
        <source>Unknown</source>
        <translation>未知</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+2"/>
        <location line="+50"/>
        <location line="+98"/>
        <source>IP:Port:</source>
        <translation>IP:端口：</translation>
    </message>
    <message>
        <location line="-143"/>
        <location line="+147"/>
        <source>ID:</source>
        <translation>ID：</translation>
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
        <translation>服务器已验证</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>inbound peer</source>
        <translation>入站节点</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>port mapping</source>
        <translation>端口映射</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>assumed</source>
        <translation>假定</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Not verified, not advertised to peers</source>
        <translation>未验证,不向节点公布</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>IPv6:</source>
        <translation>IPv6:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>IPv6 Status:</source>
        <translation>IPv6 状态:</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>eD2K Server</source>
        <translation>eD2K服务器</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Name:</source>
        <translation>名称：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Description:</source>
        <translation>描述：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Version:</source>
        <translation>版本：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Connections:</source>
        <translation>连接数：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Obfuscated</source>
        <translation>已混淆</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Normal</source>
        <translation>普通</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Low ID:</source>
        <translation>低 ID:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Ping:</source>
        <translation>Ping:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1 ms</source>
        <translation>%1 毫秒</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>eD2K Server Features</source>
        <translation>eD2K 服务器功能</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Soft/Hard File Limits:</source>
        <translation>软/硬文件限制:</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>TCP compression</source>
        <translation>TCP 压缩</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Short tags</source>
        <translation>短标签</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Integer type tags</source>
        <translation>整数类型标签</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+1"/>
        <source>Extended UDP protocol for source requests</source>
        <translation>用于来源请求的扩展 UDP 协议</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Extended UDP protocol for file requests</source>
        <translation>用于文件请求的扩展 UDP 协议</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Support for large files</source>
        <translation>支持大文件</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+1"/>
        <source>Protocol Obfuscation</source>
        <translation>协议混淆</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>LowID hole punching</source>
        <translation>LowID 打洞</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>NAT callback (Lugdunum)</source>
        <translation>NAT 回调 (Lugdunum)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hole punching extension</source>
        <translation>打洞扩展</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>NAT rendezvous</source>
        <translation>NAT 会合</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>NAT wake-up</source>
        <translation>NAT 唤醒</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Callback for clients on other servers</source>
        <translation>其他服务器客户端的回调</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Yes</source>
        <translation>是</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>No</source>
        <translation>否</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Kad Network</source>
        <translation>Kad 网络</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>LAN Mode</source>
        <translation>局域网模式</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>UDP Status:</source>
        <translation>UDP 状态：</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Extern UDP Port:</source>
        <translation>外部 UDP 端口：</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Buddy:</source>
        <translation>伙伴:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Buddies served:</source>
        <translation>服务的伙伴:</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>%1 (Experimental: %2)</source>
        <translation>%1(实验性:%2)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Indexed:</source>
        <translation>已索引:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Source:</source>
        <translation>来源：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Keyword:</source>
        <translation>关键字:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Notes:</source>
        <translation>备注:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Load:</source>
        <translation>负载:</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Web Interface</source>
        <translation>Web 界面</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Enabled</source>
        <translation>已启用</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>not running</source>
        <translation>未运行</translation>
    </message>
    <message numerus="yes">
        <location line="+4"/>
        <source>%n active session(s)</source>
        <translation>
            <numerusform>%n 个活动会话</numerusform>
        </translation>
    </message>
    <message>
        <location line="+10"/>
        <source>URL:</source>
        <translation>网址:</translation>
    </message>
    <message>
        <location line="-228"/>
        <location line="+94"/>
        <location line="+15"/>
        <source>Firewalled</source>
        <translation>防火墙</translation>
    </message>
    <message>
        <location line="-112"/>
        <location line="+1"/>
        <location line="+96"/>
        <location line="+17"/>
        <source>Open</source>
        <translation>开放</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>unverified</source>
        <translation>未验证</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>None</source>
        <translation>无</translation>
    </message>
    <message>
        <location line="+63"/>
        <source>Disabled</source>
        <translation>已禁用</translation>
    </message>
</context>
<context>
    <name>eMule::NzbFileChooserDialog</name>
    <message>
        <location filename="../src/gui/dialogs/NzbFileChooserDialog.cpp" line="+42"/>
        <source>Choose Files</source>
        <translation>选择文件</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Unchecked files are not downloaded. The volumes of one archive are checked together, and PAR2 files are fetched only when a repair needs them.</source>
        <translation>未勾选的文件不会被下载。同一压缩包的各个分卷会一起勾选，PAR2 文件只在修复需要时才获取。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Reading…</source>
        <translation>正在读取…</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>PAR2 files are fetched when a repair needs them.</source>
        <translation>PAR2 文件在修复需要时才获取。</translation>
    </message>
    <message>
        <location line="+20"/>
        <location line="+20"/>
        <source>Not connected to the eMule core.</source>
        <translation>未连接到 eMule 核心。</translation>
    </message>
    <message>
        <location line="-14"/>
        <source>Cannot read %1.</source>
        <translation>无法读取 %1。</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>This file could not be read.</source>
        <translation>无法读取此文件。</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Keep at least one file of &quot;%1&quot;.</source>
        <translation>请为“%1”至少保留一个文件。</translation>
    </message>
</context>
<context>
    <name>eMule::OptionsDialog</name>
    <message>
        <location filename="../src/gui/dialogs/OptionsDialog.cpp" line="-3294"/>
        <source>Options</source>
        <translation>选项</translation>
    </message>
    <message>
        <location line="+61"/>
        <location line="+2204"/>
        <source>OK</source>
        <translation>确定</translation>
    </message>
    <message>
        <location line="-2203"/>
        <location line="+2204"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="-2203"/>
        <location line="+6213"/>
        <source>Apply</source>
        <translation>应用</translation>
    </message>
    <message>
        <location line="-6212"/>
        <source>Help</source>
        <translation>帮助</translation>
    </message>
    <message>
        <location line="+270"/>
        <location line="+2201"/>
        <location line="+59"/>
        <location line="+6"/>
        <location line="+4"/>
        <location line="+21"/>
        <location line="+5"/>
        <location line="+9"/>
        <location line="+11"/>
        <source>IP Filter</source>
        <translation>IP 过滤器</translation>
    </message>
    <message>
        <location line="-2315"/>
        <source>IP filter reloaded: %1 entries.</source>
        <translation>IP 过滤器已重新加载：%1 条目。</translation>
    </message>
    <message>
        <location line="+90"/>
        <source>Options -&gt; %1 -&gt; %2</source>
        <translation>选项 -&gt; %1 -&gt; %2</translation>
    </message>
    <message>
        <location line="+5194"/>
        <source>Bandwidth</source>
        <translation>带宽</translation>
    </message>
    <message>
        <location line="-4987"/>
        <source>General options</source>
        <translation>常规选项</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Advanced options</source>
        <translation>高级选项</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Usenet</source>
        <translation>Usenet</translation>
    </message>
    <message>
        <location line="+105"/>
        <source>User Name</source>
        <translation>用户名</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+6075"/>
        <source>Language</source>
        <translation>语言</translation>
    </message>
    <message>
        <location line="-6072"/>
        <source>System Default</source>
        <translation>系统默认</translation>
    </message>
    <message>
        <location line="+14"/>
        <location line="+800"/>
        <location line="+384"/>
        <location line="+475"/>
        <location line="+298"/>
        <source>Miscellaneous</source>
        <translation>其他</translation>
    </message>
    <message>
        <location line="-1954"/>
        <source>Bring to front on link click</source>
        <translation>点击链接时置前</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Prompt on exit</source>
        <translation>退出时提示</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Enable online signature</source>
        <translation>启用在线签名</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Enable MiniMule</source>
        <translation>启用 MiniMule</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Prevent standby mode while running</source>
        <translation>运行时防止待机</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Edit Web Services...</source>
        <translation>编辑 Web 服务...</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Web Services</source>
        <translation>Web 服务</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>webservices.dat was not found in the config folder.</source>
        <translation>在配置文件夹中未找到 webservices.dat。</translation>
    </message>
    <message>
        <source>Open eD2K links with eMule Qt</source>
        <translation type="vanished">使用 eMule Qt 打开 eD2K 链接</translation>
    </message>
    <message>
        <source>Makes eMule Qt the default app for ed2k:// links. Turning it off hands them to another app that opens them, if there is one.</source>
        <translation type="vanished">将 eMule Qt 设为 ed2k:// 链接的默认应用。关闭后，如有其他可打开它们的应用，则交给该应用。</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Handle eD2K Links</source>
        <translation>处理 eD2K 链接</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Startup</source>
        <translation>启动</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Check for new version</source>
        <translation>检查新版本</translation>
    </message>
    <message>
        <location line="+4"/>
        <source> Days</source>
        <translation> 天</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Show splash screen</source>
        <translation>显示启动画面</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Start minimized</source>
        <translation>启动时最小化</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Start with macOS</source>
        <translation>随 macOS 启动</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Start with Windows</source>
        <translation>随 Windows 启动</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Start with system</source>
        <translation>随系统启动</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+18"/>
        <location line="+5987"/>
        <source>Core</source>
        <translation>核心</translation>
    </message>
    <message>
        <location line="-6000"/>
        <source>Address:</source>
        <translation>地址：</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+1375"/>
        <location line="+774"/>
        <location line="+427"/>
        <source>Port:</source>
        <translation>端口：</translation>
    </message>
    <message>
        <location line="-2573"/>
        <source>authentication token</source>
        <translation>认证令牌</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Token:</source>
        <translation>令牌：</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>Changes require a restart to take effect.</source>
        <translation>更改需要重启才能生效。</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+9"/>
        <source>Shutdown eMule Core</source>
        <translation>关闭 eMule 核心</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>This will shut down both the eMule Core and the GUI.

Are you sure you want to continue?</source>
        <translation>这将同时关闭 eMule 核心和图形界面。

确定要继续吗？</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>Progressbar style</source>
        <translation>进度条样式</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>flat</source>
        <translation>扁平</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>round</source>
        <translation>圆角</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Tooltip delay time [sec.]</source>
        <translation>提示延迟时间 [秒]</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Minimize to system tray</source>
        <translation>最小化到系统托盘</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Download list double-click to expand</source>
        <translation>双击展开下载列表</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show percentage of download completion in progressbar</source>
        <translation>在进度条中显示下载完成百分比</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show downloaded data of unfinished parts and part boundaries</source>
        <translation>显示未完成部分的已下载数据和部分边界</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Colours the data already downloaded in incomplete parts (green while being requested, grey otherwise) and marks every part boundary on the progress line.</source>
        <translation>为未完成部分中已下载的数据着色（请求中为绿色，否则为灰色），并在进度线上标记每个部分边界。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Show active downloads in bold</source>
        <translation>以粗体显示活动下载</translation>
    </message>
    <message>
        <source>Downloads that are receiving data, and the sources sending it, are drawn in bold. A source listed under another file (A4AF) is bold too while it transfers.</source>
        <translation type="vanished">正在接收数据的下载及发送数据的来源以粗体显示。列在其他文件下的来源 (A4AF) 在传输时也以粗体显示。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Show transfer rates on title</source>
        <translation>在标题中显示传输速率</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show download info on category tabs</source>
        <translation>在分类标签上显示下载信息</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Auto clear completed downloads</source>
        <translation>自动清除已完成的下载</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show additional toolbar on Transfers window</source>
        <translation>在传输窗口显示额外工具栏</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show speed graph in toolbar</source>
        <translation>在工具栏中显示速度图表</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Speed graph time range (minutes):</source>
        <translation>速度图表时间范围（分钟）：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Remember open searches between restarts</source>
        <translation>在重启之间记住打开的搜索</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Use original eMule icons</source>
        <translation>使用原版 eMule 图标</translation>
    </message>
    <message>
        <location line="+31"/>
        <source>A database ships with each release. An optional free &lt;a href=&quot;https://www.maxmind.com/en/geolite2/signup&quot;&gt;MaxMind GeoLite2&lt;/a&gt; account keeps it current in between. This product includes GeoLite2 data created by MaxMind.</source>
        <translation>每个版本都附带一个数据库。可选的免费 &lt;a href=&quot;https://www.maxmind.com/en/geolite2/signup&quot;&gt;MaxMind GeoLite2&lt;/a&gt; 帐户可在版本之间保持其最新。本产品包含由 MaxMind 创建的 GeoLite2 数据。</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Save CPU &amp;&amp; Memory Usage</source>
        <translation>节省 CPU &amp;&amp; 内存</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Disable Known Clients list</source>
        <translation>禁用已知客户端列表</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Disable Queue list</source>
        <translation>禁用队列列表</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Font for Server-, Message- and IRC-Window</source>
        <translation>服务器、消息和 IRC 窗口的字体</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Select Font...</source>
        <translation>选择字体...</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Select Font</source>
        <translation>选择字体</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Auto completion (history function)</source>
        <translation>自动完成（历史功能）</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+1862"/>
        <location line="+133"/>
        <location line="+296"/>
        <location line="+1021"/>
        <location line="+152"/>
        <location line="+1325"/>
        <location line="+284"/>
        <location line="+29"/>
        <source>Enabled</source>
        <translation>已启用</translation>
    </message>
    <message>
        <location line="-5100"/>
        <source>Reset</source>
        <translation>重置</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Capacities</source>
        <translation>容量</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Download</source>
        <translation>下载</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+6"/>
        <location line="+4492"/>
        <location line="+334"/>
        <source> KB/s</source>
        <translation> KB/s</translation>
    </message>
    <message>
        <location line="-4829"/>
        <source>Upload</source>
        <translation>上传</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Limits</source>
        <translation>限制</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Download limit</source>
        <translation>下载限制</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Upload limit</source>
        <translation>上传限制</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Target rate per upload slot</source>
        <translation>每个上传位的目标速率</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>A higher rate opens fewer upload slots: the upload limit divided by this rate, never fewer than two. Unchecked, the rate grows with the number of slots.</source>
        <translation>速率越高，打开的上传位越少：上传位数为上传限制除以此速率，且不少于两个。未勾选时，速率随上传位数增长。</translation>
    </message>
    <message>
        <location line="+23"/>
        <location line="+4332"/>
        <source>Client Port</source>
        <translation>客户端端口</translation>
    </message>
    <message>
        <location line="-4329"/>
        <source>TCP</source>
        <translation>TCP</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>UDP</source>
        <translation>UDP</translation>
    </message>
    <message>
        <source>Disable</source>
        <translation type="vanished">禁用</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+4334"/>
        <source>Test Ports</source>
        <translation>测试端口</translation>
    </message>
    <message>
        <location line="-4330"/>
        <location line="+4338"/>
        <source>Open Router</source>
        <translation>打开路由器</translation>
    </message>
    <message>
        <location line="-4337"/>
        <location line="+4338"/>
        <source>Opens the web page of this computer&apos;s router, where ports are forwarded</source>
        <translation>打开此计算机路由器的网页，可在其中设置端口转发</translation>
    </message>
    <message>
        <location line="-4334"/>
        <source>Use UPnP to Setup Ports</source>
        <translation>使用 UPnP 设置端口</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Randomize ports at startup, range</source>
        <translation>启动时随机选择端口，范围</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Picks new free ports from this range at every start: TCP, UDP unless it is disabled, and the BitTorrent port when BitTorrent is enabled. Use it with UPnP, or forward the whole range in your router.</source>
        <translation>每次启动时从此范围中选择新的空闲端口：TCP、UDP（除非已禁用），以及启用 BitTorrent 时的 BitTorrent 端口。请配合 UPnP 使用，或在路由器中转发整个范围。</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Port forwarding: unknown</source>
        <translation>端口转发：未知</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Network interface</source>
        <translation>网络接口</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+1735"/>
        <location line="+5532"/>
        <source>Any</source>
        <translation>任意</translation>
    </message>
    <message>
        <location line="-7265"/>
        <source>Use only this network interface, for example a VPN tunnel. Pick one from the list, or type an IP address or a subnet such as 10.64.0.0/10 (the interface holding an address in it). Connections through a proxy are not bound.</source>
        <translation>仅使用此网络接口，例如 VPN 隧道。可从列表中选择，或输入 IP 地址或子网（如 10.64.0.0/10，即拥有该范围内地址的接口）。通过代理的连接不会被绑定。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>While the selected interface is not available, all P2P, Usenet and update connections stay closed.</source>
        <translation>所选接口不可用期间，所有 P2P、Usenet 和更新连接都保持关闭。</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Max. Sources/File</source>
        <translation>最大来源/文件</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Hard limit</source>
        <translation>硬限制</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+4356"/>
        <source>Connection Limits</source>
        <translation>连接限制</translation>
    </message>
    <message>
        <location line="-4354"/>
        <source>Max. connections</source>
        <translation>最大连接数</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Autoconnect on startup</source>
        <translation>启动时自动连接</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Reconnect on loss</source>
        <translation>断线重连</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Show overhead bandwidth</source>
        <translation>显示开销带宽</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Wizard...</source>
        <translation>向导...</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Network</source>
        <translation>网络</translation>
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
        <translation>独立的 IPv6 队列</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Alternate freed upload slots between IPv4 and IPv6 clients when both are waiting, so IPv6 peers are not outbid on score alone. When only one family is waiting, no slot is held back.</source>
        <translation>当 IPv4 和 IPv6 客户端同时等待时，在两者之间轮流分配释放的上传位，以免 IPv6 对等端仅因评分而落选。若只有一种协议在等待，则不会保留任何上传位。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Use IPv6 privacy address</source>
        <translation>使用 IPv6 隐私地址</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Connect to IPv6 peers and servers from the rotating temporary (RFC 4941) address. Off: always use the stable address, so peers can reach the address we advertise.</source>
        <translation>使用轮换的临时地址 (RFC 4941) 连接 IPv6 节点和服务器。关闭：始终使用固定地址，使节点能够访问我们公布的地址。</translation>
    </message>
    <message>
        <location line="+57"/>
        <location line="+1558"/>
        <source>General</source>
        <translation>常规</translation>
    </message>
    <message>
        <location line="-1555"/>
        <source>Enable proxy</source>
        <translation>启用代理</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Proxy type:</source>
        <translation>代理类型：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>No Proxy</source>
        <translation>无代理</translation>
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
        <translation>代理主机：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Proxy port:</source>
        <translation>代理端口：</translation>
    </message>
    <message>
        <location line="+21"/>
        <location line="+782"/>
        <source>Authentication</source>
        <translation>认证</translation>
    </message>
    <message>
        <location line="-779"/>
        <source>Enable authentication</source>
        <translation>启用认证</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+1940"/>
        <location line="+1024"/>
        <location line="+152"/>
        <source>Name:</source>
        <translation>名称：</translation>
    </message>
    <message>
        <location line="-3112"/>
        <location line="+783"/>
        <location line="+837"/>
        <location line="+19"/>
        <location line="+325"/>
        <source>Password:</source>
        <translation>密码：</translation>
    </message>
    <message>
        <location line="-1930"/>
        <source>Update</source>
        <translation>更新</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Remove dead servers after</source>
        <translation>删除失效服务器，超过</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>retries</source>
        <translation>次重试</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Auto-update server list at startup</source>
        <translation>启动时自动更新服务器列表</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>List...</source>
        <translation>列表...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Server List URL</source>
        <translation>服务器列表 URL</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Enter the URL for server.met download:</source>
        <translation>输入 server.met 下载 URL：</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Update server list when connecting to a server</source>
        <translation>连接服务器时更新服务器列表</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Update server list when a client connects</source>
        <translation>客户端连接时更新服务器列表</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Use smart LowID check on connect</source>
        <translation>连接时使用智能 LowID 检查</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Safe Connect</source>
        <translation>安全连接</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Lets two Low ID clients find each other&apos;s address through a server that supports it or a Kad buddy, then connect over IPv6 or QUIC</source>
        <translation>让两个 Low ID 客户端通过支持此功能的服务器或 Kad 伙伴找到对方地址，然后通过 IPv6 或 QUIC 连接</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Always use a source&apos;s server for NAT callback</source>
        <translation>始终使用来源的服务器进行 NAT 回调</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Also asks a source&apos;s server that is not in your server list. It is pinged once first and is not added to the list</source>
        <translation>也会询问不在服务器列表中的来源服务器。先 ping 一次，且不会加入列表</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Autoconnect to servers in static list only</source>
        <translation>仅自动连接静态列表中的服务器</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Use priority system</source>
        <translation>使用优先级系统</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Use the manual server order (drag/Move Up-Down)</source>
        <translation>使用手动服务器排序（拖动/上移-下移）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Set manually added servers to high priority</source>
        <translation>将手动添加的服务器设为高优先级</translation>
    </message>
    <message>
        <location line="+184"/>
        <source>Incoming Files</source>
        <translation>接收文件</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Select Incoming Directory</source>
        <translation>选择接收目录</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Temporary Files</source>
        <translation>临时文件</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Select Temporary Directory</source>
        <translation>选择临时目录</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Shared Directories (Ctrl+Click includes subdirectories)</source>
        <translation>共享目录（Ctrl+点击包含子目录）</translation>
    </message>
    <message>
        <location line="+40"/>
        <source>Add UNC share</source>
        <translation>添加 UNC 共享</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Add UNC Share</source>
        <translation>添加 UNC 共享</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Enter UNC path (e.g., \\server\share):</source>
        <translation>输入 UNC 路径（例如 \\server\share）：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Invalid Path</source>
        <translation>无效路径</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>A UNC path must start with \\.</source>
        <translation>UNC 路径必须以 \\ 开头。</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>UNC shares are only supported on Windows</source>
        <translation>UNC 共享仅在 Windows 上受支持</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Initializations</source>
        <translation>初始化</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Add files to download in paused mode</source>
        <translation>以暂停模式添加下载文件</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Add new shared files with auto priority</source>
        <translation>以自动优先级添加新共享文件</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Add new downloads with auto priority</source>
        <translation>以自动优先级添加新下载</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Remember download sources between restarts</source>
        <translation>在重启之间记住下载来源</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Stores each download&apos;s best sources in the temp folder and reconnects to them on the next start, so a rare file does not have to find its peers again.</source>
        <translation>将每个下载的最佳来源保存在临时文件夹中，并在下次启动时重新连接，这样稀有文件就不必重新寻找来源。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Auto cleanup file names of new downloads</source>
        <translation>自动清理新下载的文件名</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+613"/>
        <source>Edit...</source>
        <translation>编辑...</translation>
    </message>
    <message>
        <source>Filename Cleanup Rules</source>
        <translation type="vanished">文件名清理规则</translation>
    </message>
    <message>
        <source>Define patterns to automatically clean up filenames of new downloads.
Each rule replaces a regex pattern with a replacement string.</source>
        <translation type="vanished">定义模式以自动清理新下载的文件名。
每条规则将正则表达式模式替换为替换字符串。</translation>
    </message>
    <message>
        <source>Pattern</source>
        <translation type="vanished">模式</translation>
    </message>
    <message>
        <source>Replacement</source>
        <translation type="vanished">替换</translation>
    </message>
    <message>
        <location line="-602"/>
        <source>Try to transfer full chunks to all uploads</source>
        <translation>尝试向所有上传传输完整块</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Try to download preview chunks first</source>
        <translation>优先下载预览块</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Watch clipboard for eD2K links</source>
        <translation>监视剪贴板中的 eD2K 文件链接</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Use advanced calculation method for remaining time</source>
        <translation>使用高级方法计算剩余时间</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Start next paused file when a file completes</source>
        <translation>文件完成时开始下一个暂停的文件</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Prefer same category</source>
        <translation>优先相同分类</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Only in same category</source>
        <translation>仅在相同分类</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Remember downloaded files</source>
        <translation>记住已下载的文件</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Remember cancelled files</source>
        <translation>记住已取消的文件</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>Video Player</source>
        <translation>视频播放器</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Command</source>
        <translation>命令</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Select Video Player</source>
        <translation>选择视频播放器</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Arguments</source>
        <translation>参数</translation>
    </message>
    <message>
        <source>Create backup to preview</source>
        <translation type="vanished">创建备份以预览</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Pop-up Message</source>
        <translation>弹出消息</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>No sound</source>
        <translation>无声音</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Test</source>
        <translation>测试</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Play sound</source>
        <translation>播放声音</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Speak notification message</source>
        <translation>朗读通知消息</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Select Sound File</source>
        <translation>选择声音文件</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Sound Files (*.wav *.mp3 *.ogg);;All Files (*)</source>
        <translation>声音文件 (*.wav *.mp3 *.ogg);;所有文件 (*)</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Pop-up when</source>
        <translation>弹出提示当</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Log entry added</source>
        <translation>日志条目已添加</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Chat session started</source>
        <translation>聊天会话已开始</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Chat message received</source>
        <translation>收到聊天消息</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Download added</source>
        <translation>下载已添加</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Download finished (*)</source>
        <translation>下载完成 (*)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Urgent: out of disk space, server connection lost (*)</source>
        <translation>紧急：磁盘空间不足，服务器连接丢失 (*)</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>(*) Email Notifications</source>
        <translation>(*) 邮件通知</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Enable email notifications</source>
        <translation>启用邮件通知</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>SMTP server...</source>
        <translation>SMTP 服务器...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Recipient address:</source>
        <translation>收件人地址：</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Sender address:</source>
        <translation>发件人地址：</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>SMTP Server Settings</source>
        <translation>SMTP 服务器设置</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Server:</source>
        <translation>服务器：</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+14"/>
        <location line="+1233"/>
        <source>None</source>
        <translation>无</translation>
    </message>
    <message>
        <source>Plain</source>
        <translation type="vanished">明文</translation>
    </message>
    <message>
        <source>Authentication:</source>
        <translation type="vanished">认证：</translation>
    </message>
    <message>
        <source>Use TLS/STARTTLS</source>
        <translation type="vanished">使用 TLS/STARTTLS</translation>
    </message>
    <message>
        <location line="-1228"/>
        <source>Username:</source>
        <translation>用户名：</translation>
    </message>
    <message>
        <location line="+111"/>
        <source>Server</source>
        <translation>服务器</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Nick</source>
        <translation>昵称</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Channels</source>
        <translation>频道</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Use channel list filter</source>
        <translation>使用频道列表过滤器</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+982"/>
        <location line="+1032"/>
        <location line="+154"/>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <location line="-2166"/>
        <source>Users</source>
        <translation>用户</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Perform</source>
        <translation>执行</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Use perform string on connect</source>
        <translation>连接时使用执行字符串</translation>
    </message>
    <message>
        <location line="-111"/>
        <source>Connect to help channel</source>
        <translation>连接到帮助频道</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Load server channel list on connect</source>
        <translation>连接时加载服务器频道列表</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Add timestamp to messages</source>
        <translation>为消息添加时间戳</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Ignore info messages</source>
        <translation>忽略信息消息</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ignore misc. info messages</source>
        <translation>忽略杂项信息消息</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Ignore Join info messages</source>
        <translation>忽略加入信息消息</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Ignore Part info messages</source>
        <translation>忽略离开信息消息</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Ignore Quit info messages</source>
        <translation>忽略退出信息消息</translation>
    </message>
    <message>
        <location line="+161"/>
        <source>Messages</source>
        <translation>消息</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Filter messages containing: (Separator | )</source>
        <translation>过滤包含以下内容的消息：（分隔符 | ）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Accept from friends only</source>
        <translation>仅接受好友消息</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Advanced spam filter</source>
        <translation>高级垃圾信息过滤器</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Require captcha authentication</source>
        <translation>要求验证码认证</translation>
    </message>
    <message>
        <location line="-159"/>
        <location line="+164"/>
        <source>Show smileys</source>
        <translation>显示表情</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Comments</source>
        <translation>评论</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Ignore comments containing: (Separator | )</source>
        <translation>忽略包含以下内容的评论：（分隔符 | ）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Indicate downloads with comments/rating by icon</source>
        <translation>用图标标示有评论/评分的下载</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Filter servers too</source>
        <translation>同时过滤服务器</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Filter level:   &lt;</source>
        <translation>过滤级别：   &lt;</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+490"/>
        <source>Reload</source>
        <translation>重新加载</translation>
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
        <translation>加载</translation>
    </message>
    <message>
        <location line="-41"/>
        <source>Loading...</source>
        <translation>加载中...</translation>
    </message>
    <message>
        <location line="+14"/>
        <location line="+6"/>
        <location line="+25"/>
        <source>Failed to download IP filter: %1</source>
        <translation>下载IP过滤器失败: %1</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Downloaded IP filter is empty.</source>
        <translation>下载的 IP 过滤器为空。</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Failed to save IP filter: %1</source>
        <translation>保存 IP 过滤器失败：%1</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>IP filter updated and reloaded.</source>
        <translation>IP过滤器已更新并重新加载。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>IP filter updated and reloaded (unpacked &quot;%1&quot;).</source>
        <translation>IP 过滤器已更新并重新加载（已解压 &quot;%1&quot;）。</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>See My Shared Files/Directories</source>
        <translation>查看我的共享文件/目录</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Everybody</source>
        <translation>所有人</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Friends only</source>
        <translation>仅好友</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nobody</source>
        <translation>无人</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Protocol Obfuscation</source>
        <translation>协议混淆</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Enable protocol obfuscation</source>
        <translation>启用协议混淆</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Allow obfuscated connections only (not recommended)</source>
        <translation>仅允许混淆连接（不推荐）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Disable support for obfuscated connections</source>
        <translation>禁用混淆连接支持</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Use secure identification</source>
        <translation>使用安全身份验证</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Run eMule as unprivileged user</source>
        <translation>以非特权用户运行 eMule</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Enable spam filter for search results</source>
        <translation>为搜索结果启用垃圾信息过滤</translation>
    </message>
    <message>
        <source>Warn when opening untrusted files</source>
        <translation type="vanished">打开不受信任的文件时警告</translation>
    </message>
    <message>
        <location line="+50"/>
        <source>Graphs</source>
        <translation>图表</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Update delay: 3 sec</source>
        <translation>更新延迟：3 秒</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+158"/>
        <source>Update delay: %1 sec</source>
        <translation>更新延迟：%1 秒</translation>
    </message>
    <message>
        <location line="-157"/>
        <location line="+158"/>
        <source>Update delay: disabled</source>
        <translation>更新延迟：已禁用</translation>
    </message>
    <message>
        <location line="-151"/>
        <source>Time for average graph: 5 mins</source>
        <translation>平均图表时间：5 分钟</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Time for average graph: %1 mins</source>
        <translation>平均图表时间：%1 分钟</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Colors</source>
        <translation>颜色</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Background</source>
        <translation>背景</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Grid</source>
        <translation>网格</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Download Current</source>
        <translation>当前下载</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Download Average</source>
        <translation>平均下载</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Download Session</source>
        <translation>会话下载</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Upload Current</source>
        <translation>当前上传</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Upload Average</source>
        <translation>平均上传</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Upload Session</source>
        <translation>会话上传</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Active Connections</source>
        <translation>活跃连接</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Total Uploads</source>
        <translation>总上传</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Active Uploads</source>
        <translation>活跃上传</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Icon Bar</source>
        <translation>图标栏</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Active Downloads</source>
        <translation>活跃下载</translation>
    </message>
    <message>
        <location line="-6"/>
        <source>Upload Friend Slots</source>
        <translation>上传好友位</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Upload Slots (no overhead)</source>
        <translation>上传位（无开销）</translation>
    </message>
    <message>
        <location line="-1808"/>
        <source>Country flags (IP2Country)</source>
        <translation>国旗 (IP2Country)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Show country flags</source>
        <translation>显示国旗</translation>
    </message>
    <message>
        <source>Country column:</source>
        <translation type="vanished">国家列:</translation>
    </message>
    <message>
        <source>Hidden</source>
        <translation type="vanished">隐藏</translation>
    </message>
    <message>
        <source>Short name</source>
        <translation type="vanished">简称</translation>
    </message>
    <message>
        <source>Long name</source>
        <translation type="vanished">全称</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>MaxMind account ID:</source>
        <translation>MaxMind 账户 ID:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>License key:</source>
        <translation>许可证密钥:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Update the database weekly</source>
        <translation>每周更新数据库</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Update now</source>
        <translation>立即更新</translation>
    </message>
    <message>
        <source>Needs a free &lt;a href=&quot;https://www.maxmind.com/en/geolite2/signup&quot;&gt;MaxMind GeoLite2&lt;/a&gt; account. This product includes GeoLite2 data created by MaxMind.</source>
        <translation type="vanished">需要免费的 &lt;a href=&quot;https://www.maxmind.com/en/geolite2/signup&quot;&gt;MaxMind GeoLite2&lt;/a&gt; 账户。本产品包含由 MaxMind 创建的 GeoLite2 数据。</translation>
    </message>
    <message>
        <location line="+411"/>
        <source>Use for news servers</source>
        <translation>同时用于新闻服务器</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Route Usenet downloads, availability checks and the news server Test button through this proxy too. News servers switch over as soon as you press OK.

Every Usenet connection then passes through the proxy, so its speed caps the download, and many HTTP proxies only allow connections to port 443.</source>
        <translation>同时将 Usenet 下载、可用性检查和新闻服务器的“测试”按钮通过此代理转发。按下“确定”后，新闻服务器会立即切换。

此后每个 Usenet 连接都会经过代理，因此代理的速度决定下载速度，而且许多 HTTP 代理只允许连接到 443 端口。</translation>
    </message>
    <message>
        <location line="+115"/>
        <source>Prefer IPv6 when IPv4 gives a Low ID</source>
        <translation>IPv4 获得 LowID 时优先使用 IPv6</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Reconnect to a server over its IPv6 address when the IPv4 connection got a Low ID</source>
        <translation>当 IPv4 连接获得 LowID 时，通过服务器的 IPv6 地址重新连接</translation>
    </message>
    <message>
        <location line="+430"/>
        <source>Remember files seen in search results</source>
        <translation>记住搜索结果中出现过的文件</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Keeps a local record of the files that turned up in your searches and the names they went by, so later searches can show what you have seen before.</source>
        <translation>在本地记录搜索中出现过的文件及其当时的名称，以便之后的搜索能显示您以前见过的内容。</translation>
    </message>
    <message>
        <location line="-1422"/>
        <location line="+5"/>
        <location line="+1637"/>
        <source>Connection</source>
        <translation>连接</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Security:</source>
        <translation>安全:</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Authentication method:</source>
        <translation>认证方式:</translation>
    </message>
    <message>
        <location line="+63"/>
        <source>Ignore Ping? Pong! messages</source>
        <translation>忽略 Ping? Pong! 消息</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Ignore eMule protocol messages</source>
        <translation>忽略 eMule 协议消息</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Ignore eMule add friend protocol messages</source>
        <translation>忽略 eMule 添加好友协议消息</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Ignore eMule send link protocol messages</source>
        <translation>忽略 eMule 发送链接协议消息</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Allow others to add you as a friend</source>
        <translation>允许他人将您添加为好友</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Accept eD2K links in IRC (Use only with caution!)</source>
        <translation>在 IRC 中接受 eD2K 链接 (请谨慎使用！)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>From friends only</source>
        <translation>仅限好友</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Play sound events</source>
        <translation>播放声音事件</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Enable UTF-8</source>
        <translation>启用 UTF-8</translation>
    </message>
    <message>
        <location line="+400"/>
        <source>Anti-Leecher</source>
        <translation>反吸血</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Ban leechers with DLP rules:</source>
        <translation>使用 DLP 规则封禁吸血客户端：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Lite</source>
        <translation>Lite</translation>
    </message>
    <message>
        <source>All VeryCD</source>
        <translation type="vanished">全部 VeryCD</translation>
    </message>
    <message>
        <source>easyMule</source>
        <translation type="vanished">easyMule</translation>
    </message>
    <message>
        <source>VeryCD nickname</source>
        <translation type="vanished">VeryCD 昵称</translation>
    </message>
    <message>
        <source>VeryCD tag</source>
        <translation type="vanished">VeryCD 标签</translation>
    </message>
    <message>
        <source>Only reduce the score of soft leechers instead of banning them</source>
        <translation type="vanished">对轻度吸血客户端仅降低评分而不封禁</translation>
    </message>
    <message>
        <location line="+84"/>
        <source>Download Usenet</source>
        <translation>Usenet 下载</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Default</source>
        <translation>默认</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Restore this colour to the eMule default</source>
        <translation>将此颜色恢复为 eMule 默认值</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Auto</source>
        <translation>自动</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Select Color</source>
        <translation>选择颜色</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Draw filled graphs</source>
        <translation>绘制填充图表</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Connections statistics Y-axis scale:</source>
        <translation>连接统计 Y 轴比例：</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Active connections ratio:</source>
        <translation>活跃连接比率：</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Statistics Tree</source>
        <translation>统计树</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Update delay: 5 sec</source>
        <translation>更新延迟：5 秒</translation>
    </message>
    <message>
        <location line="+40"/>
        <source>Enable REST API</source>
        <translation>启用 REST API</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Serves /api/v1 with its description and a playground at /api/v1/docs. Calls need the API key.</source>
        <translation>提供 /api/v1 及其说明，并在 /api/v1/docs 提供测试页面。调用需要 API 密钥。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Enable MCP server for AI assistants</source>
        <translation>为 AI 助手启用 MCP 服务器</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Serves the Model Context Protocol at /mcp on the same port, with the same API key. Independent of the REST API.</source>
        <translation>在同一端口的 /mcp 上以相同的 API 密钥提供 Model Context Protocol。与 REST API 相互独立。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Read-only</source>
        <translation>只读</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Assistants can look at everything but change nothing.</source>
        <translation>助手可以查看所有内容，但不能更改任何内容。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Gzip compression</source>
        <translation>Gzip 压缩</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Include port into UPnP setup</source>
        <translation>将端口加入 UPnP 设置</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Listen address:</source>
        <translation>监听地址:</translation>
    </message>
    <message>
        <source>all interfaces</source>
        <translation type="vanished">所有接口</translation>
    </message>
    <message>
        <source>Address the web server listens on. Empty: every interface, reachable from other hosts. 127.0.0.1: this computer only.</source>
        <translation type="vanished">Web 服务器监听的地址。留空:所有接口,其他主机可访问。127.0.0.1:仅限本机。</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Template:</source>
        <translation>模板：</translation>
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
        <translation>会话超时：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>minutes</source>
        <translation>分钟</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Use HTTPS</source>
        <translation>使用 HTTPS</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Create new certificate</source>
        <translation>创建新证书</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Certificate:</source>
        <translation>证书：</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Key:</source>
        <translation>密钥：</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>REST API Key:</source>
        <translation>REST API 密钥：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Administrator</source>
        <translation>管理员</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Allow exit eMule, reboot and shutdown</source>
        <translation>允许退出 eMule、重启和关机</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Guest</source>
        <translation>访客</translation>
    </message>
    <message>
        <location line="+55"/>
        <source>Web template reloaded</source>
        <translation>Web 模板已重新加载</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Web template reload failed</source>
        <translation>Web 模板重新加载失败</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Select Template File</source>
        <translation>选择模板文件</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Template files (*.tmpl);;All files (*)</source>
        <translation>模板文件 (*.tmpl);;所有文件 (*)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Select Certificate File</source>
        <translation>选择证书文件</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>PEM files (*.pem *.crt);;All files (*)</source>
        <translation>PEM 文件 (*.pem *.crt);;所有文件 (*)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Select Key File</source>
        <translation>选择密钥文件</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>PEM files (*.pem *.key);;All files (*)</source>
        <translation>PEM 文件 (*.pem *.key);;所有文件 (*)</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Save Certificate</source>
        <translation>保存证书</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>PEM files (*.pem)</source>
        <translation>PEM 文件 (*.pem)</translation>
    </message>
    <message>
        <location line="+132"/>
        <source>Enable Usenet downloads</source>
        <translation>启用 Usenet 下载</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Gates automatic activity only. Adding a download by hand always works.</source>
        <translation>仅限制自动活动。手动添加下载始终有效。</translation>
    </message>
    <message>
        <location line="+26"/>
        <location line="+38"/>
        <source>Account</source>
        <translation>账户</translation>
    </message>
    <message>
        <location line="-36"/>
        <source>Advanced</source>
        <translation>高级</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+633"/>
        <location line="+274"/>
        <location line="+15"/>
        <source>News servers</source>
        <translation>新闻服务器</translation>
    </message>
    <message>
        <location line="-916"/>
        <source>Host</source>
        <translation>主机</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Port</source>
        <translation>端口</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Priority</source>
        <translation>优先级</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Connections</source>
        <translation>连接数</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Used</source>
        <translation>已用</translation>
    </message>
    <message>
        <location line="+41"/>
        <source>Display name (optional)</source>
        <translation>显示名称（可选）</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Host:</source>
        <translation>主机：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Encryption:</source>
        <translation>加密：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>None (119)</source>
        <translation>无 (119)</translation>
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
        <translation>用户：</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Never set this above what your provider allows — exceeding the limit gets the account throttled, not queued.</source>
        <translation>切勿将此值设得高于提供商允许的数量 — 超出限制会导致账户被限速，而不是排队等待。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Connections:</source>
        <translation>连接数：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Lower is tried first. A higher level is only used for articles that every server below reported as missing — that is what makes a block or fill account worth having.</source>
        <translation>级别越低越先尝试。只有当所有更低级别的服务器都报告文章缺失时，才会使用更高的级别 — 这正是块账户或补充账户的价值所在。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Priority level:</source>
        <translation>优先级别：</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Account options</source>
        <translation>账户选项</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Unknown</source>
        <translation>未知</translation>
    </message>
    <message>
        <location line="-813"/>
        <location line="+815"/>
        <location line="+1009"/>
        <location line="+165"/>
        <source> days</source>
        <translation> 天</translation>
    </message>
    <message>
        <location line="-1173"/>
        <source>Retention:</source>
        <translation>保留期：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Accounts sharing a group number count as one for connection limits — use it when the same provider is reached through two host names, so the two entries cannot open twice what the plan allows.</source>
        <translation>共用同一组号的账户在连接数限制上算作一个 — 当同一提供商通过两个主机名接入时使用它，这样两个条目就不会打开超出套餐允许数量两倍的连接。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Connection group:</source>
        <translation>连接组：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>None — accept any certificate</source>
        <translation>不检查 — 接受任何证书</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Minimal — allow a host name mismatch</source>
        <translation>最低 — 允许主机名不匹配</translation>
    </message>
    <message>
        <location line="-751"/>
        <location line="+752"/>
        <source>Strict</source>
        <translation>严格</translation>
    </message>
    <message>
        <location line="-2862"/>
        <location line="+8303"/>
        <source>BitTorrent</source>
        <translation>BitTorrent</translation>
    </message>
    <message>
        <location line="-7926"/>
        <source>Show download progress on the Dock icon</source>
        <translation>在程序坞图标上显示下载进度</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Show download progress on the taskbar button</source>
        <translation>在任务栏按钮上显示下载进度</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Shows advanced entries in context menus and extra details in lists.</source>
        <translation>在右键菜单中显示高级项目，并在列表中显示更多详细信息。</translation>
    </message>
    <message>
        <location line="+50"/>
        <source>Appearance</source>
        <translation>外观</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Theme:</source>
        <translation>主题：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>System default</source>
        <translation>系统默认</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Light</source>
        <translation>浅色</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Dark</source>
        <translation>深色</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Font size:</source>
        <translation>字体大小：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>%1 %</source>
        <translation>%1%</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Text size of lists, menus and dialogs. Takes effect after a restart.</source>
        <translation>列表、菜单和对话框的文字大小。重启后生效。</translation>
    </message>
    <message>
        <location line="+495"/>
        <source>Connect Low ID to Low ID through server or Kad buddy (NAT traversal)</source>
        <translation>通过服务器或 Kad 伙伴连接 Low ID 与 Low ID (NAT 穿透)</translation>
    </message>
    <message>
        <source>Lets two Low ID clients find each other&apos;s address through a server that supports it or a Kad buddy, then connect over IPv6</source>
        <translation type="vanished">让两个 Low ID 客户端通过支持此功能的服务器或 Kad 伙伴找到对方地址，然后通过 IPv6 连接</translation>
    </message>
    <message>
        <location line="+1130"/>
        <source>Never send unobfuscated NAT traversal (QUIC) packets</source>
        <translation>从不发送未混淆的 NAT 穿透 (QUIC) 数据包</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Low ID to Low ID transfers over QUIC are always obfuscated when possible. If the other client cannot read them, eMule falls back to unobfuscated packets after 2 seconds. Check this to fail such transfers instead, e.g. on networks that block QUIC.</source>
        <translation>通过 QUIC 的低 ID 到低 ID 传输会尽可能始终混淆。如果对方客户端无法读取，eMule 会在 2 秒后改用未混淆的数据包。勾选此项可改为让此类传输失败，例如在屏蔽 QUIC 的网络中。</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Strict + Chinese</source>
        <translation>严格 + 中国</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bans hard leechers, soft leechers only lose queue score</source>
        <translation>封禁严重吸血者，轻度吸血者只降低队列评分</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Bans hard and soft leechers</source>
        <translation>封禁严重和轻度吸血者</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Strict, and also bans VeryCD and easyMule mods</source>
        <translation>严格，并封禁 VeryCD 和 easyMule Mod</translation>
    </message>
    <message>
        <location line="+81"/>
        <source>Download BitTorrent</source>
        <translation>BitTorrent 下载</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Download ed3k</source>
        <translation>下载 ed3k</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Upload BitTorrent</source>
        <translation>BitTorrent 上传</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload ed3k</source>
        <translation>上传 ed3k</translation>
    </message>
    <message>
        <location line="+183"/>
        <source>Address the web server listens on. Any: every interface, reachable from other hosts. 127.0.0.1: this computer only. Pick one from the list or type an IP address.</source>
        <translation>Web 服务器监听的地址。任意:所有接口,其他主机可访问。127.0.0.1:仅限本机。从列表中选择或输入 IP 地址。</translation>
    </message>
    <message>
        <location line="+478"/>
        <source>Certificate check:</source>
        <translation>证书检查：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Optional — never fail a download on its own</source>
        <translation>可选 — 绝不会仅因此而导致下载失败</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Send GROUP before fetching (only needed by a few old servers)</source>
        <translation>获取前先发送 GROUP（仅少数旧服务器需要）</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Unmetered</source>
        <translation>不限流量</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Monthly allowance</source>
        <translation>每月流量额度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Block account (prepaid)</source>
        <translation>块账户（预付费）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Allowance:</source>
        <translation>额度：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source> GB</source>
        <translation> GB</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+1963"/>
        <location line="+9"/>
        <location line="+159"/>
        <source>No limit</source>
        <translation>无限制</translation>
    </message>
    <message>
        <location line="-2123"/>
        <source>Allowance size:</source>
        <translation>额度大小：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Your billing day — providers reset on the day you signed up, not on the 1st. A month shorter than this rolls over on its last day.</source>
        <translation>您的账单日 — 提供商在您注册的那一天重置额度，而不是每月 1 日。若某月天数少于此日期，则在该月最后一天结转。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Resets on day:</source>
        <translation>重置日：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>When the allowance is spent, use the next priority level</source>
        <translation>额度用尽时，使用下一个优先级别</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Off by default: block credit usually costs more per GB than the plan it would be covering, and spending it without being asked is the one thing a limit exists to prevent. Left off, downloads wait for the allowance instead — they are never failed and no article is ever given up on.</source>
        <translation>默认关闭：块账户的每 GB 费用通常高于它所补充的套餐，而未经询问就把它花掉，正是设置限额要防止的事情。保持关闭时，下载会改为等待额度恢复 — 它们绝不会失败，也不会放弃任何一篇文章。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Used:</source>
        <translation>已用：</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Correct…</source>
        <translation>修正…</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Downloading</source>
        <translation>正在下载</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Retry a failed server after:</source>
        <translation>失败服务器的重试间隔：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Never back off</source>
        <translation>从不退避</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Applies to every account: how long a server that refused or dropped a connection is passed over before it is tried again.</source>
        <translation>适用于所有账户：拒绝或断开连接的服务器会被跳过多久，然后才再次尝试。</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+2065"/>
        <source>Share of the download limit:</source>
        <translation>占下载限速的份额：</translation>
    </message>
    <message>
        <location line="-2062"/>
        <location line="+42"/>
        <location line="+2012"/>
        <source> %</source>
        <translation> %</translation>
    </message>
    <message>
        <location line="-2052"/>
        <source>How much of the global download limit Usenet may take while eD2K is also downloading. Whichever engine is idle lends its whole share to the other, so this only applies when both are busy.</source>
        <translation>当 eD2K 也在下载时，Usenet 可占用全局下载限速的比例。空闲的一方会把自己的全部份额让给另一方，因此此设置仅在两者都繁忙时生效。</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>When adding</source>
        <translation>添加时</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Check availability:</source>
        <translation>检查可用性：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Do not check</source>
        <translation>不检查</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Sample one article per file</source>
        <translation>每个文件抽查一篇文章</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Check every article</source>
        <translation>检查每一篇文章</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Before downloading anything, ask your providers whether they still hold the release. It costs one small request per article asked about and no payload at all.

Sampling asks about the first article of each file, which is usually enough: providers expire whole posts by date, so a file is almost always present or absent as a unit. Checking every article is certain but can mean tens of thousands of requests for a large release.

The answer is never a verdict. Nothing here can stop an article being fetched — an article your providers deny may still arrive, and a release this pauses downloads normally when you resume it.</source>
        <translation>在下载任何内容之前，先询问提供商是否仍然保存着该发布内容。每查询一篇文章只需一个很小的请求，完全不传输正文数据。

抽查只询问每个文件的第一篇文章，这通常已经足够：提供商按日期整体过期帖子，因此一个文件几乎总是整体存在或整体缺失。检查每一篇文章更为确切，但对大型发布内容可能意味着数万次请求。

这个答案从来都不是最终判决。这里的任何设置都无法阻止文章被获取 — 提供商声称没有的文章仍可能到达，而被它暂停的发布内容在您恢复后会照常下载。</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Pause below:</source>
        <translation>低于此值时暂停：</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+1305"/>
        <source>never</source>
        <translation>从不</translation>
    </message>
    <message>
        <location line="-1302"/>
        <source>A release that looks emptier than this is added paused, with the reason shown, so you decide rather than the guess. It is never failed and never refused.

A shortfall the release&apos;s own PAR2 recovery volumes can cover does not pause it, however low the figure goes.</source>
        <translation>看起来比此值更不完整的发布内容会以暂停状态添加，并显示原因，由您而不是由猜测来决定。它绝不会被判为失败，也绝不会被拒绝。

如果缺失的部分能由发布内容自带的 PAR2 恢复卷补足，则无论数值多低都不会暂停。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Start automatic downloads paused</source>
        <translation>自动添加的下载以暂停状态开始</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Applies to anything queued without you asking for it directly: the watch folder below, and feeds.

With this on, an automatic download waits for you to press Resume, so a feed proposes rather than decides. Anything you add yourself starts normally either way.</source>
        <translation>适用于所有并非由您直接要求而加入队列的内容：下面的监视文件夹以及订阅源。

开启后，自动下载会等待您按下“恢复”，这样订阅源只是提议而不是替您决定。无论此项是否开启，您自己添加的内容都会正常开始。</translation>
    </message>
    <message>
        <location line="+10"/>
        <location line="+54"/>
        <source>Watch folder</source>
        <translation>监视文件夹</translation>
    </message>
    <message>
        <location line="-50"/>
        <source>Any .nzb file left in this folder is queued and then moved into a _processed subfolder — or _failed, if it could not be read.</source>
        <translation>放入此文件夹的任何 .nzb 文件都会被加入队列，然后移动到 _processed 子文件夹 — 如果无法读取，则移动到 _failed。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>No folder is being watched</source>
        <translation>未监视任何文件夹</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>A file is only read once it has stopped changing, so a large .nzb still being copied in is left alone until it is complete.

It cannot be inside your temp, incoming or configuration folders: the daemon writes there itself.</source>
        <translation>文件只有在停止变化后才会被读取，因此正在复制中的大型 .nzb 会被暂时搁置，直到复制完成。

它不能位于您的临时、接收或配置文件夹内：守护进程本身要向这些位置写入。</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+1791"/>
        <source>Browse…</source>
        <translation>浏览…</translation>
    </message>
    <message>
        <location line="-1783"/>
        <location line="+1826"/>
        <source>Desktop</source>
        <translation>桌面</translation>
    </message>
    <message>
        <location line="-1822"/>
        <source>Open .nzb files with eMule Qt</source>
        <translation>使用 eMule Qt 打开 .nzb 文件</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Makes eMule Qt the default app for .nzb files, so double-clicking one queues it. Turning it off hands them to another app that opens them, if there is one.</source>
        <translation>将 eMule Qt 设为 .nzb 文件的默认应用，双击即可加入队列。关闭后，如有其他可打开它们的应用，则交给该应用。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Claim .nzb files for this copy of eMule Qt, so double-clicking one queues it. The setting is for you alone and needs no administrator; it is re-applied at every start, so another program taking the association does not keep it.</source>
        <translation>将 .nzb 文件关联到此副本的 eMule Qt，这样双击即可将其加入队列。该设置仅对您自己生效，无需管理员权限；每次启动都会重新应用，因此其他程序抢走关联也无法一直占着。</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>After downloading</source>
        <translation>下载完成后</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Verify and repair with PAR2</source>
        <translation>使用 PAR2 校验并修复</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Check the finished files against the release&apos;s PAR2 set and repair any damage from its recovery volumes. The recovery volumes are only downloaded when something actually needs repairing.

With this off, a release with missing articles fails instead of being shared, because there is no way to tell whether it is intact.</source>
        <translation>用发布内容的 PAR2 集校验已完成的文件，并用其恢复卷修复任何损坏。只有确实需要修复时才会下载恢复卷。

关闭此项时，缺失文章的发布内容会失败而不会被共享，因为无法判断它是否完整。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Restore filenames from PAR2</source>
        <translation>从 PAR2 还原文件名</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Obfuscated releases are posted under meaningless filenames. The PAR2 metadata carries the real ones, and without them the archives cannot be identified for unpacking either.</source>
        <translation>混淆发布的内容会以毫无意义的文件名发帖。PAR2 元数据中带有真实文件名，没有它们也无法识别压缩包以进行解压。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Verify with SFV when there is no PAR2</source>
        <translation>没有 PAR2 时使用 SFV 校验</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>A release posted without a PAR2 set often comes with an .sfv file instead. Its checksums cannot repair anything, but a release they call damaged is not published.</source>
        <translation>没有 PAR2 集的发布内容通常会附带一个 .sfv 文件。它的校验和无法修复任何东西，但被它判定为损坏的内容不会被发布。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Unpack archives</source>
        <translation>解压压缩包</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Extract RAR, 7z and ZIP volume sets once they have been verified.

Password-protected archives need 7-Zip or unrar installed — eMule&apos;s own archive reader can only decrypt ZIP.</source>
        <translation>在校验通过后解压 RAR、7z 和 ZIP 分卷集。

受密码保护的压缩包需要安装 7-Zip 或 unrar — eMule 自带的压缩包读取器只能解密 ZIP。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Unpack while downloading</source>
        <translation>边下载边解压</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Extract each archive volume as soon as it finishes instead of waiting for the whole release, so the content is ready the moment the download is.

It is the same extraction, moved earlier, so it costs no extra disk space. If the release turns out to need repairing, the result is discarded and it is unpacked again afterwards.</source>
        <translation>每个压缩分卷一下载完就解压，而不是等待整个发布内容完成，这样下载结束的同时内容也就绪了。

这只是把同一次解压提前，不会占用额外的磁盘空间。如果发布内容最终需要修复，解压结果会被丢弃，之后重新解压。</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Preview password-protected releases while downloading</source>
        <translation>下载过程中预览受密码保护的发布内容</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>An encrypted archive cannot be read a piece at a time, so previewing one means decrypting it again from the first volume every time more of it arrives.

Nothing runs unless a preview is actually open, and only RAR releases can do it at all — an incomplete 7z set decodes to nothing.</source>
        <translation>加密的压缩包无法逐段读取，因此每当有更多数据到达时，预览都意味着从第一个分卷开始重新解密。

只有确实打开了预览时才会执行，而且只有 RAR 发布内容才能做到 — 不完整的 7z 集根本解不出任何内容。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Unpacker:</source>
        <translation>解压程序：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>automatic (7zz, 7z, unrar)</source>
        <translation>自动 (7zz, 7z, unrar)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Path to a 7-Zip or unrar binary, for password-protected archives.

Leave this empty to search the usual locations. Set it when eMule runs as a background service, whose search path is often much shorter than the one a terminal has.</source>
        <translation>7-Zip 或 unrar 可执行文件的路径，用于受密码保护的压缩包。

留空则搜索常见位置。当 eMule 作为后台服务运行时请设置此项，因为服务的搜索路径通常比终端的短得多。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Delete archives and PAR2 files after unpacking</source>
        <translation>解压后删除压缩包和 PAR2 文件</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Keep only the unpacked content. Turning this off roughly doubles the disk space a release uses and shares the archive volumes and recovery files with eD2K peers, who have no use for them.</source>
        <translation>只保留解压后的内容。关闭此项会使发布内容占用的磁盘空间大约翻倍，并把压缩分卷和恢复文件共享给 eD2K 节点，而它们对这些文件毫无用处。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Keep downloading</source>
        <translation>继续下载</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+11"/>
        <source>Pause it</source>
        <translation>暂停它</translation>
    </message>
    <message>
        <location line="-10"/>
        <location line="+11"/>
        <source>Fail it</source>
        <translation>判为失败</translation>
    </message>
    <message>
        <location line="-9"/>
        <source>A release that has lost more than its recovery files could ever repair stops here instead of using up your allowance until the final check. The estimate only ever errs towards downloading.

Resume downloads it anyway.</source>
        <translation>当发布内容缺失的部分已超出其恢复文件所能修复的范围时，会在此停止，而不是继续消耗额度直到最终检查。这个估算只会偏向于继续下载。

“恢复”仍会照常下载它。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>When a download cannot be repaired:</source>
        <translation>当下载无法修复时：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Publish anyway</source>
        <translation>仍然发布</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>A movie or episode whose download contains programs or shortcuts is almost always a fake. Only releases with video or audio in them are checked, so software downloads are not affected.

Resume publishes it anyway.</source>
        <translation>如果一部电影或剧集的下载中含有程序或快捷方式，那几乎一定是假文件。只有包含视频或音频的发布内容才会被检查，因此软件类下载不受影响。

“恢复”仍会照常发布它。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>When a media release has unwanted files:</source>
        <translation>当媒体发布内容含有不需要的文件时：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>File extensions, separated by commas. A video file that is not really a video counts as well. Leave this empty to turn the check off.</source>
        <translation>文件扩展名，用逗号分隔。名为视频但实际并非视频的文件同样算在内。留空则关闭此检查。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Unwanted file types:</source>
        <translation>不需要的文件类型：</translation>
    </message>
    <message>
        <location line="+191"/>
        <source>The news server list could not be saved.</source>
        <translation>无法保存新闻服务器列表。</translation>
    </message>
    <message>
        <location line="+146"/>
        <source>(unchanged)</source>
        <translation>（未更改）</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>(none set)</source>
        <translation>（未设置）</translation>
    </message>
    <message>
        <location line="+60"/>
        <source>not measured — the Usenet engine is stopped</source>
        <translation>未统计 — Usenet 引擎已停止</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>%1 used</source>
        <translation>已用 %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1 of %2</source>
        <translation>%1 / %2</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>, resets %1</source>
        <translation>，%1 重置</translation>
    </message>
    <message>
        <location line="+5"/>
        <source> — spent</source>
        <translation> — 已用尽</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Measured here, not reported by the provider — NNTP has no command that asks. Expect a few percent below your provider&apos;s own figure.</source>
        <translation>此数值由本地统计，并非提供商报告 — NNTP 没有可用于查询的命令。预计会比提供商自己的数字低几个百分点。</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Correct usage</source>
        <translation>修正用量</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Used this period, in GB.

Enter what your provider&apos;s control panel says, or 0 to start again.</source>
        <translation>本周期已用量，单位 GB。

请输入提供商控制面板显示的数值，或输入 0 重新开始计数。</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>The usage counter could not be changed.</source>
        <translation>无法更改用量计数器。</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>At most %1 news servers can be configured.</source>
        <translation>最多可配置 %1 个新闻服务器。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>New server</source>
        <translation>新服务器</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Enter a host name first.</source>
        <translation>请先输入主机名。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Connecting…</source>
        <translation>正在连接…</translation>
    </message>
    <message>
        <location line="+51"/>
        <source>Search indexers answer keyword searches and hand back an NZB. They are separate from your news servers: on Usenet the provider you download from and the service you search are different businesses.</source>
        <translation>搜索索引器负责响应关键词搜索并返回 NZB。它们与您的新闻服务器是分开的：在 Usenet 上，您下载所用的提供商和您搜索所用的服务是两类不同的生意。</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+384"/>
        <location line="+541"/>
        <source>Indexers</source>
        <translation>索引器</translation>
    </message>
    <message>
        <location line="-920"/>
        <source>URL</source>
        <translation>URL</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Type</source>
        <translation>类型</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>API key</source>
        <translation>API 密钥</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Indexer</source>
        <translation>索引器</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+152"/>
        <source>Display name</source>
        <translation>显示名称</translation>
    </message>
    <message>
        <location line="-150"/>
        <source>Also the identity of this account: it names the cached capabilities and appears in the Indexer column of the results.</source>
        <translation>它同时也是此账户的标识：缓存的功能信息以它命名，并显示在结果的“索引器”列中。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>The API base URL. A bare host gets &quot;/api&quot; added; a URL that already has a path is used exactly as typed, which is what Jackett and NZBHydra2 endpoints need.</source>
        <translation>API 基础 URL。只填主机名时会自动追加 “/api”；已带路径的 URL 会原样使用，Jackett 和 NZBHydra2 的端点正需要这样。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>API URL:</source>
        <translation>API URL：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>API key:</source>
        <translation>API 密钥：</translation>
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
        <translation>两者 — Prowlarr、NZBHydra2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type:</source>
        <translation>类型：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Searching</source>
        <translation>搜索</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Rows to ask for per request. An indexer that allows fewer silently returns fewer, so this is an upper bound rather than a promise.</source>
        <translation>每次请求索取的结果条数。允许条数更少的索引器会直接返回更少的结果，因此这是一个上限而不是承诺。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Results per request:</source>
        <translation>每次请求的结果数：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>How many pages one search may fetch from each indexer.

Every page is an API call against the allowance your account has, so this is a spending limit, not a speed setting.</source>
        <translation>一次搜索最多可从每个索引器获取多少页。

每一页都是一次 API 调用，会消耗您账户的配额，因此这是一个花费上限，而不是速度设置。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Pages per search:</source>
        <translation>每次搜索的页数：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Request timeout:</source>
        <translation>请求超时：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>How often to re-read what each indexer supports. A stale answer never blocks a search — it only means a query field stays greyed out that the indexer has since started accepting.</source>
        <translation>多久重新读取一次各索引器支持的功能。过时的信息绝不会阻止搜索 — 只是某个索引器后来已经支持的查询字段仍显示为灰色。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Refresh capabilities every:</source>
        <translation>刷新功能信息间隔：</translation>
    </message>
    <message>
        <location line="+51"/>
        <source>A feed is a search that runs on its own and queues what it finds. Its first check adds nothing — it only records what the indexer already lists, because otherwise a new feed would download everything still on the server.</source>
        <translation>订阅源是一个自动运行并把结果加入队列的搜索。它的首次检查不会添加任何内容 — 只是记录索引器当前列出的条目，否则新建的订阅源会把服务器上仍保留的一切都下载下来。</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+324"/>
        <location line="+277"/>
        <source>Feeds</source>
        <translation>订阅源</translation>
    </message>
    <message>
        <location line="-596"/>
        <source>Search</source>
        <translation>搜索</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Every</source>
        <translation>每隔</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Last checked</source>
        <translation>上次检查</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Feed</source>
        <translation>订阅源</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Also the identity of this feed: it names the file that remembers what the feed has already seen.</source>
        <translation>它同时也是此订阅源的标识：记录该订阅源已见过哪些条目的文件以它命名。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Search my indexers</source>
        <translation>搜索我的索引器</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>An RSS address I paste</source>
        <translation>我粘贴的 RSS 地址</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Source:</source>
        <translation>来源：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Keywords. Leave it empty to take everything new in the categories below.</source>
        <translation>关键词。留空则接收下面所选分类中的所有新条目。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Search for:</source>
        <translation>搜索：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>e.g. 2000, 5000</source>
        <translation>例如 2000, 5000</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Newznab category numbers, separated by commas. Empty means every category.</source>
        <translation>Newznab 分类编号，用逗号分隔。留空表示所有分类。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Categories:</source>
        <translation>分类：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Which indexers to ask, by name and separated by commas. Empty means all of them.

Adding one later does not fetch its back catalogue: a new indexer gets its own first check, which adds nothing.</source>
        <translation>要询问哪些索引器，按名称填写并用逗号分隔。留空表示全部。

之后新增的索引器不会抓取历史条目：新索引器会进行自己的首次检查，而首次检查不添加任何内容。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Indexers:</source>
        <translation>索引器：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>The RSS address from your indexer&apos;s website. It contains your API key, so it is stored encrypted and is only ever shown back to you with the key hidden.</source>
        <translation>来自索引器网站的 RSS 地址。它包含您的 API 密钥，因此会加密保存，显示给您时也始终隐藏密钥。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Feed URL:</source>
        <translation>订阅源 URL：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Only queue releases whose name matches this pattern. Empty accepts everything.</source>
        <translation>只将名称匹配此模式的发布内容加入队列。留空表示接受全部。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Must match:</source>
        <translation>必须匹配：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Never queue a release whose name matches this pattern. It wins over the one above.</source>
        <translation>绝不将名称匹配此模式的发布内容加入队列。它优先于上面的规则。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Must not match:</source>
        <translation>必须不匹配：</translation>
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
        <translation>无下限</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Smallest:</source>
        <translation>最小：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>no maximum</source>
        <translation>无上限</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Largest:</source>
        <translation>最大：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>any age</source>
        <translation>不限时间</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Posted within:</source>
        <translation>发布时间不早于：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source> minutes</source>
        <translation> 分钟</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>How often to check. Fifteen minutes is the floor: most indexers ask for no more than that, and checking harder gets an account suspended.</source>
        <translation>检查频率。十五分钟是下限：大多数索引器要求不高于此频率，检查过于频繁会导致账户被封停。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Check every:</source>
        <translation>检查间隔：</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+136"/>
        <source>No category</source>
        <translation>无分类</translation>
    </message>
    <message>
        <location line="-134"/>
        <source>Which download category this feed&apos;s matches go into. The category decides the folder they finish in, and it is resolved when a release completes — so repointing the category moves what is still running with it.</source>
        <translation>此订阅源匹配到的内容归入哪个下载分类。分类决定它们完成后所在的文件夹，并在发布内容完成时才确定 — 因此改变分类的指向，正在进行的下载也会随之改变。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Download category:</source>
        <translation>下载分类：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Queue what it already lists</source>
        <translation>将已列出的条目加入队列</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Normally a feed&apos;s first check only takes note of what is there and queues nothing, because everything an indexer still holds is new to a feed that has never run. Turn this on to take the back catalogue as well — it can be a great deal of it.</source>
        <translation>通常订阅源的首次检查只记录当前有哪些条目而不加入队列，因为对于从未运行过的订阅源来说，索引器保留的一切都是新内容。开启此项也会把历史条目一并接收 — 数量可能非常庞大。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Check now</source>
        <translation>立即检查</translation>
    </message>
    <message>
        <location line="+97"/>
        <source>The indexer list could not be saved.</source>
        <translation>无法保存索引器列表。</translation>
    </message>
    <message>
        <location line="+94"/>
        <source>The feed list could not be saved.</source>
        <translation>无法保存订阅源列表。</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>%1 min</source>
        <translation>%1 分钟</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>checking…</source>
        <translation>正在检查…</translation>
    </message>
    <message>
        <location line="+113"/>
        <source>%1 queued on the last check; %2 releases remembered.</source>
        <translation>上次检查加入队列 %1 个；已记住 %2 个发布内容。</translation>
    </message>
    <message>
        <location line="+71"/>
        <source>New feed</source>
        <translation>新订阅源</translation>
    </message>
    <message>
        <location line="+50"/>
        <source>&quot;%1&quot; could not be checked.</source>
        <translation>无法检查“%1”。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Checking &quot;%1&quot;…</source>
        <translation>正在检查“%1”…</translation>
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
        <translation>两者</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>yes</source>
        <translation>是</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>no</source>
        <translation>否</translation>
    </message>
    <message>
        <location line="+68"/>
        <source>(a key is stored — leave empty to keep it)</source>
        <translation>（已保存密钥 — 留空则继续使用）</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>At most %1 indexers can be configured.</source>
        <translation>最多可配置 %1 个索引器。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>New indexer</source>
        <translation>新索引器</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Enter an API URL first.</source>
        <translation>请先输入 API URL。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Contacting the indexer…</source>
        <translation>正在联系索引器…</translation>
    </message>
    <message>
        <location line="+73"/>
        <source>Enable BitTorrent</source>
        <translation>启用 BitTorrent</translation>
    </message>
    <message>
        <source>Starts the BitTorrent engine with the daemon. Off, no torrent is downloaded or seeded.</source>
        <translation type="vanished">随守护进程启动 BitTorrent 引擎。关闭时不下载也不做种任何种子。</translation>
    </message>
    <message>
        <location line="+3469"/>
        <source>unlimited</source>
        <translation>无限制</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Download: %1, upload: %2</source>
        <translation>下载：%1，上传：%2</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>The BitTorrent port is the eD2K TCP port.</source>
        <translation>BitTorrent 端口与 eD2K TCP 端口相同。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The BitTorrent port is the eD2K UDP port.</source>
        <translation>BitTorrent 端口与 eD2K UDP 端口相同。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The BitTorrent port is the web server port.</source>
        <translation>BitTorrent 端口与 Web 服务器端口相同。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The BitTorrent port is the core port.</source>
        <translation>BitTorrent 端口与核心端口相同。</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Please choose another port.</source>
        <translation>请选择其他端口。</translation>
    </message>
    <message>
        <source>Used for both TCP and UDP. Must differ from the eD2K ports.</source>
        <translation type="vanished">同时用于 TCP 和 UDP。必须与 eD2K 端口不同。</translation>
    </message>
    <message>
        <location line="-3365"/>
        <source>Listen port (TCP/UDP):</source>
        <translation>监听端口 (TCP/UDP)：</translation>
    </message>
    <message>
        <source>Forced</source>
        <translation type="vanished">强制</translation>
    </message>
    <message>
        <source>Enabled prefers encrypted peers and still accepts plain ones. Forced refuses unencrypted peers, which leaves fewer to download from.</source>
        <translation type="vanished">“已启用”优先使用加密的 Peer，但仍接受未加密的 Peer。“强制”拒绝未加密的 Peer，可供下载的来源会更少。</translation>
    </message>
    <message>
        <location line="+34"/>
        <source>Find peers through the DHT</source>
        <translation>通过 DHT 查找 Peer</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Find peers on the local network (LSD)</source>
        <translation>在本地网络中查找 Peer (LSD)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Exchange peers with other clients (PEX)</source>
        <translation>与其他客户端交换 Peer (PEX)</translation>
    </message>
    <message>
        <source>Unlimited</source>
        <translation type="vanished">无限制</translation>
    </message>
    <message>
        <source>Download:</source>
        <translation type="vanished">下载：</translation>
    </message>
    <message>
        <source>Upload:</source>
        <translation type="vanished">上传：</translation>
    </message>
    <message>
        <location line="-144"/>
        <source>Active downloads:</source>
        <translation>活跃下载：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Active seeds:</source>
        <translation>活跃做种：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Seeding</source>
        <translation>做种中</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Stop seeding a torrent once it has uploaded this many times its size.</source>
        <translation>种子上传量达到其大小的此倍数后停止做种。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Stop at share ratio:</source>
        <translation>达到分享率时停止：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source> min</source>
        <translation> 分钟</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Stop after seeding for:</source>
        <translation>做种时长达到后停止：</translation>
    </message>
    <message>
        <location line="-43"/>
        <source>Download folder</source>
        <translation>下载文件夹</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Incoming folder</source>
        <translation>下载完成目录</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Leave empty to save torrents to the incoming folder.</source>
        <translation>留空则将种子保存到下载完成目录。</translation>
    </message>
    <message>
        <source>Open .torrent files, magnet and ed3k links with eMule Qt</source>
        <translation type="vanished">使用 eMule Qt 打开 .torrent 文件、magnet 和 ed3k 链接</translation>
    </message>
    <message>
        <source>Makes eMule Qt the default app for .torrent files, magnet: and ed3k: links. Turning it off hands them to another app that opens them, if there is one.</source>
        <translation type="vanished">将 eMule Qt 设为 .torrent 文件以及 magnet: 和 ed3k: 链接的默认应用。关闭后，如有其他可打开它们的应用，则交给该应用。</translation>
    </message>
    <message>
        <source>Claim .torrent files, magnet: and ed3k: links for this copy of eMule Qt. The setting is for you alone and needs no administrator; it is re-applied at every start, so another program taking the association does not keep it.</source>
        <translation type="vanished">为此 eMule Qt 副本关联 .torrent 文件以及 magnet: 和 ed3k: 链接。此设置仅对您生效，无需管理员权限；每次启动时都会重新应用，因此其他程序抢走关联也无法保留。</translation>
    </message>
    <message>
        <location line="+73"/>
        <source>BitTorrent download folder</source>
        <translation>BitTorrent 下载文件夹</translation>
    </message>
    <message>
        <location line="+190"/>
        <source>Warning: Do not change these settings unless you know what you are doing. Otherwise you can easily make things worse for yourself. eMule will run fine without adjusting any of these settings.</source>
        <translation>警告：除非您知道自己在做什么，否则请勿更改这些设置。否则可能会导致问题。eMule 无需调整这些设置即可正常运行。</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>TCP/IP connections</source>
        <translation>TCP/IP 连接</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Max. new connections / 5 secs.:</source>
        <translation>每 5 秒最大新连接数：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Max. half-open connections:</source>
        <translation>最大半开连接数：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Server connection refresh interval [min.]:</source>
        <translation>服务器连接刷新间隔 [分钟]：</translation>
    </message>
    <message>
        <location line="+194"/>
        <source>Log fake-file header warnings for shared files too</source>
        <translation>同时记录共享文件的伪造文件头警告</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Downloads are always checked and logged. Shared files are checked again at every start.</source>
        <translation>下载始终会被检查并记录。共享文件在每次启动时都会重新检查。</translation>
    </message>
    <message>
        <location line="+1849"/>
        <location line="+20"/>
        <source>Could not update the file associations: %1</source>
        <translation>无法更新文件关联：%1</translation>
    </message>
    <message>
        <location line="+922"/>
        <location line="+16"/>
        <location line="+6"/>
        <location line="+3"/>
        <location line="+3"/>
        <source>Rehash All Files</source>
        <translation>重新计算所有文件的哈希</translation>
    </message>
    <message>
        <location line="-27"/>
        <source>This reads every shared file from disk again and rebuilds known.met. It takes a long time and should only be used after hard disk errors.

Sharing goes on with the current known.met until the rehash is done.

Rehash all shared files now?</source>
        <translation>这将从磁盘重新读取每个共享文件并重建 known.met。此操作耗时较长，仅应在硬盘出错后使用。

在重新计算完成之前，将继续使用当前的 known.met 进行共享。

现在重新计算所有共享文件的哈希吗？</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>The core did not start the rehash.</source>
        <translation>核心未启动重新计算哈希。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>A rehash of all files is already running.</source>
        <translation>所有文件的哈希重新计算已在进行中。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>There are no shared files to rehash.</source>
        <translation>没有可重新计算哈希的共享文件。</translation>
    </message>
    <message numerus="yes">
        <location line="+3"/>
        <source>Rehashing %n shared file(s) in the background. known.met is replaced when all are done; the log shows the result.</source>
        <translation>
            <numerusform>正在后台重新计算 %n 个共享文件的哈希。全部完成后将替换 known.met，结果显示在日志中。</numerusform>
        </translation>
    </message>
    <message>
        <location line="+114"/>
        <source>UDP is disabled in the config file (udpPort: 0)</source>
        <translation>UDP 已在配置文件中禁用 (udpPort: 0)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Kad needs UDP, which is disabled in the config file</source>
        <translation>Kad 需要 UDP，但它已在配置文件中禁用</translation>
    </message>
    <message>
        <source>A VPN seems to be active (%1). Incoming connections then arrive through the VPN, so port forwarding on your router does not help. Forward the ports at your VPN provider instead, if it offers port forwarding.</source>
        <translation type="vanished">VPN 似乎已启用（%1）。传入连接会经由 VPN 到达，因此在路由器上转发端口没有作用。如果 VPN 提供商支持端口转发，请改在那里转发端口。</translation>
    </message>
    <message>
        <source>Could not find your router&apos;s address.

Look up the &quot;default gateway&quot; in this computer&apos;s network settings and open that address in a web browser.</source>
        <translation type="vanished">找不到路由器的地址。

请在此计算机的网络设置中查找“默认网关”，然后在网页浏览器中打开该地址。</translation>
    </message>
    <message>
        <source>Could not open %1 in the default browser.</source>
        <translation type="vanished">无法在默认浏览器中打开 %1。</translation>
    </message>
    <message>
        <source>Your router is at %1.</source>
        <translation type="vanished">路由器地址为 %1。</translation>
    </message>
    <message>
        <source>Open Router Page</source>
        <translation type="vanished">打开路由器页面</translation>
    </message>
    <message>
        <location line="-70"/>
        <location line="+52"/>
        <source>Directories</source>
        <translation>目录</translation>
    </message>
    <message>
        <location line="-36"/>
        <source>Please enter the incoming folder.</source>
        <translation>请输入下载完成文件夹。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The incoming folder must be a full path:
%1</source>
        <translation>下载完成文件夹必须是完整路径：
%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The incoming folder does not exist:
%1

Create it?</source>
        <translation>下载完成文件夹不存在：
%1

是否创建？</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Please enter the temporary folder.</source>
        <translation>请输入临时文件夹。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The temporary folder must be a full path:
%1</source>
        <translation>临时文件夹必须是完整路径：
%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The temporary folder does not exist:
%1

Create it?</source>
        <translation>临时文件夹不存在：
%1

是否创建？</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>The temporary folder can&apos;t be the same as the incoming folder.</source>
        <translation>临时文件夹不能与下载完成文件夹相同。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>This is a file, not a folder:
%1</source>
        <translation>这是文件，不是文件夹：
%1</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Could not create the folder:
%1</source>
        <translation>无法创建文件夹：
%1</translation>
    </message>
    <message>
        <location line="-3116"/>
        <source>Disabled</source>
        <translation>已禁用</translation>
    </message>
    <message>
        <location line="+1141"/>
        <source>Theme</source>
        <translation>主题</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The theme change will take effect after restarting the application.</source>
        <translation>主题更改将在重启应用程序后生效。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Font size</source>
        <translation>字体大小</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The font size change will take effect after restarting the application.</source>
        <translation>字体大小更改将在重启应用程序后生效。</translation>
    </message>
    <message>
        <location line="+1117"/>
        <source>specialdlp loaded, DLP v%1</source>
        <translation>specialdlp 已加载，DLP v%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>specialdlp library not found, checks are inactive</source>
        <translation>未找到 specialdlp 库，检查未启用</translation>
    </message>
    <message>
        <location line="+756"/>
        <source>Automatic</source>
        <translation>自动</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>about %1 slots</source>
        <translation>约 %1 个上传位</translation>
    </message>
    <message>
        <source>Autotake eD2K links only during runtime</source>
        <translation type="vanished">仅在运行时自动接受 eD2K 链接</translation>
    </message>
    <message>
        <location line="-3008"/>
        <source>Use credit system (reward uploaders)</source>
        <translation>使用信用系统（奖励上传者）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Remember the upload queue between restarts</source>
        <translation>在重启之间记住上传队列</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Stores the longest-waiting clients in your upload queue and puts them back, with the places they had earned, when eMule starts again. They are not contacted on startup — they simply wait their turn as usual.</source>
        <translation>保存上传队列中等待时间最长的客户端，并在 eMule 再次启动时按它们已获得的排位放回队列。启动时不会主动联系它们 — 它们只是像往常一样排队等待。</translation>
    </message>
    <message>
        <source>Open/close ports on WinXP firewall when starting/exiting eMule</source>
        <translation type="vanished">启动/退出 eMule 时在 WinXP 防火墙上打开/关闭端口</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Filter server and client LAN IPs</source>
        <translation>过滤服务器和客户端局域网 IP</translation>
    </message>
    <message>
        <location line="-4775"/>
        <source>Show more controls (advanced mode controls)</source>
        <translation>显示更多控件（高级模式控件）</translation>
    </message>
    <message>
        <location line="-435"/>
        <location line="+52"/>
        <source>ed3k</source>
        <translation>ed3k</translation>
    </message>
    <message>
        <source>Credits</source>
        <translation type="vanished">信用</translation>
    </message>
    <message>
        <location line="-47"/>
        <source>Names</source>
        <translation>名称</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Hoster</source>
        <translation>网盘</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Filters</source>
        <translation>过滤器</translation>
    </message>
    <message>
        <location line="+203"/>
        <source>Open eD2K and ed3k links with eMule Qt</source>
        <translation>使用 eMule Qt 打开 eD2K 和 ed3k 链接</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Makes eMule Qt the default app for ed2k:// and ed3k: links. Turning it off hands them to another app that opens them, if there is one.</source>
        <translation>将 eMule Qt 设为 ed2k:// 和 ed3k: 链接的默认应用。关闭后，如有其他可打开它们的应用，则交给该应用。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Makes eMule Qt the handler for ed2k:// and ed3k: links.</source>
        <translation>将 eMule Qt 设为 ed2k:// 和 ed3k: 链接的处理程序。</translation>
    </message>
    <message>
        <location line="+65"/>
        <source>Certificate (SHA-256)</source>
        <translation>证书 (SHA-256)</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>First Seen</source>
        <translation>首次发现</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Trusted cores:</source>
        <translation>受信任的核心:</translation>
    </message>
    <message>
        <location line="+1119"/>
        <source>Rehash All Files...</source>
        <translation>重新计算所有文件的哈希...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Reads every shared file again and rebuilds known.met from the result. Only needed after hard disk errors.</source>
        <translation>重新读取每个共享文件，并根据结果重建 known.met。仅在硬盘出错后需要。</translation>
    </message>
    <message>
        <location line="+3434"/>
        <source>Runs the BitTorrent engine in the core. Off, the engine stops: no torrent is downloaded or seeded and the BitTorrent port is closed. Torrents stay in the list and continue when it is switched on again.</source>
        <translation>在核心中运行 BitTorrent 引擎。关闭时引擎停止：不下载也不做种任何种子，BitTorrent 端口关闭。种子保留在列表中，重新开启后继续。</translation>
    </message>
    <message>
        <location line="+58"/>
        <source>Open .torrent files and magnet links with eMule Qt</source>
        <translation>使用 eMule Qt 打开 .torrent 文件和磁力链接</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Makes eMule Qt the default app for .torrent files and magnet: links. Turning it off hands them to another app that opens them, if there is one.</source>
        <translation>将 eMule Qt 设为 .torrent 文件和 magnet: 链接的默认应用。关闭后，如有其他可打开它们的应用，则交给该应用。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Claim .torrent files and magnet: links for this copy of eMule Qt. The setting is for you alone and needs no administrator; it is re-applied at every start, so another program taking the association does not keep it.</source>
        <translation>将 .torrent 文件和 magnet: 链接关联到此副本的 eMule Qt。该设置仅对您自己生效，无需管理员权限；每次启动都会重新应用，因此其他程序抢走关联也无法一直占着。</translation>
    </message>
    <message>
        <location line="+52"/>
        <source>Asks emule-qt.org whether the port the core uses now is reachable over TCP and UDP. Apply a changed port first.</source>
        <translation>询问 emule-qt.org 核心当前使用的端口是否可通过 TCP 和 UDP 访问。如已更改端口，请先应用。</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Protocol Encryption</source>
        <translation>协议加密</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Enable protocol encryption</source>
        <translation>启用协议加密</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Prefers encrypted peers and still accepts unencrypted ones.</source>
        <translation>优先使用加密的节点，同时仍接受未加密的节点。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Allow encrypted connections only (not recommended)</source>
        <translation>仅允许加密连接（不推荐）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Refuses unencrypted peers, which leaves fewer to download from.</source>
        <translation>拒绝未加密的节点，可供下载的来源会更少。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Disable protocol encryption</source>
        <translation>禁用协议加密</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Connects without encryption only. Peers that require it cannot connect.</source>
        <translation>仅以不加密方式连接。要求加密的节点无法连接。</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Global limits:</source>
        <translation>全局限制:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>eD2K and Usenet come first. BitTorrent keeps its share while they are busy and uses all the bandwidth they leave idle.</source>
        <translation>eD2K 和 Usenet 优先。它们忙碌时 BitTorrent 保留自己的份额，并使用它们空闲出来的全部带宽。</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Max. download:</source>
        <translation>最大下载:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Max. upload:</source>
        <translation>最大上传:</translation>
    </message>
    <message>
        <location line="+137"/>
        <source>Disable A4AF checks to save CPU</source>
        <translation>禁用 A4AF 检查以节省 CPU</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Disable automatic archive preview start in file details</source>
        <translation>在文件详情中禁用自动压缩包预览</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Host name for own eD2K links:</source>
        <translation>自有 eD2K 链接的主机名：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>A DNS name or an IPv6 literal</source>
        <translation>DNS 名称或 IPv6 字面地址</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Add own IPv6 address to eD2K links</source>
        <translation>将自己的 IPv6 地址添加到 eD2K 链接</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Only when a public IPv6 address is confirmed. Legacy clients ignore it.</source>
        <translation>仅在确认拥有公网 IPv6 地址时生效。旧版客户端会忽略它。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Create new part files as &apos;sparse&apos; (NTFS only)</source>
        <translation>将新 part 文件创建为「稀疏」（仅 NTFS）</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Allocate full file size for non-sparse part files</source>
        <translation>为非稀疏 part 文件分配完整文件大小</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Check disk space</source>
        <translation>检查磁盘空间</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Min. free disk space [MB]:</source>
        <translation>最小空闲磁盘空间 [MB]：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Max. disk load while hashing:</source>
        <translation>哈希计算时的最大磁盘负载:</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Hashing pauses between reads so the disk stays responsive. 100 % means no limit.</source>
        <translation>哈希计算会在读取之间暂停，以保持磁盘响应。100 % 表示不限制。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Safe .met/.dat file writing</source>
        <translation>安全 .met/.dat 文件写入</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+15"/>
        <source>Never</source>
        <translation>从不</translation>
    </message>
    <message>
        <location line="-14"/>
        <source>On shutdown</source>
        <translation>关闭时</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Always</source>
        <translation>始终</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Extract meta data</source>
        <translation>提取元数据</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>MediaInfo Library</source>
        <translation>MediaInfo 库</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Resolve shell links in shared directories</source>
        <translation>解析共享目录中的快捷方式</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Verbose (additional program feedback)</source>
        <translation>详细（额外程序反馈）</translation>
    </message>
    <message>
        <source>Log level:</source>
        <translation type="vanished">日志级别：</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Log client source exchange and server source queries/answers</source>
        <translation>记录客户端来源交换和服务器来源查询/回复</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Log banned clients</source>
        <translation>记录已封禁客户端</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Log received file descriptions and ratings</source>
        <translation>记录收到的文件描述和评分</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Log secure ident</source>
        <translation>记录安全身份验证</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Log filtered and/or ignored IPs</source>
        <translation>记录已过滤和/或忽略的 IP</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Log file save actions</source>
        <translation>记录文件保存操作</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Log A4AF actions</source>
        <translation>记录 A4AF 操作</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Log upload/download events</source>
        <translation>记录上传/下载事件</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Log raw socket packets</source>
        <translation>记录原始套接字数据包</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Show Usenet log tab</source>
        <translation>显示 Usenet 日志标签页</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Upload SpeedSense (not recommended)</source>
        <translation>上传速度感应（不推荐）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Find best upload limit automatically</source>
        <translation>自动查找最佳上传限制</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Lowest allowed upload speed:</source>
        <translation>允许的最低上传速度:</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Ping tolerance (% of lowest ping):</source>
        <translation>Ping 容差（最低 ping 的 %）：</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Ping tolerance (ms):</source>
        <translation>Ping 容差（毫秒）：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source> ms</source>
        <translation> 毫秒</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Method for ping tolerance:</source>
        <translation>Ping 容差方法：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Percent (%)</source>
        <translation>百分比 (%)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Milliseconds (ms)</source>
        <translation>毫秒 (ms)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Going up slowness:</source>
        <translation>上升缓慢度：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Going down slowness:</source>
        <translation>下降缓慢度：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Max number of pings for average:</source>
        <translation>最大平均 ping 次数：</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>UPnP</source>
        <translation>UPnP</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Remove UPnP port forwarding on exit</source>
        <translation>退出时移除 UPnP 端口转发</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Sharing eMule with other computer users</source>
        <translation>与其他计算机用户共享 eMule</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Each user has its own configuration and downloads</source>
        <translation>每个用户有自己的配置和下载</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Everyone has the same configuration and downloads</source>
        <translation>所有人使用相同的配置和下载</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Store config and downloads in the program directory</source>
        <translation>将配置和下载存储在程序目录中</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>File buffer size: %1 MB</source>
        <translation>文件缓冲区大小：%1 MB</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Queue size: %1</source>
        <translation>队列大小：%1</translation>
    </message>
    <message>
        <location line="+296"/>
        <source>Enter the datarate limit: (%1, KB/s)</source>
        <translation>请输入速率限制:(%1,KB/s)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Please enter the new value: (%1)</source>
        <translation>请输入新值:(%1)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Configure Action</source>
        <translation>配置操作</translation>
    </message>
    <message>
        <location line="+40"/>
        <source>Select category</source>
        <translation>选择分类</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>All uncategorized</source>
        <translation>所有未分类</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>All</source>
        <translation>全部</translation>
    </message>
    <message>
        <location line="+299"/>
        <source>Proxy settings will only apply to new connections.
Restart eMule for all connections to use the new proxy settings.

News server connections switch over immediately.</source>
        <translation>代理设置仅应用于新连接。
重启 eMule 以使所有连接使用新的代理设置。

新闻服务器连接会立即切换。</translation>
    </message>
    <message>
        <location line="+503"/>
        <source>Web Interface</source>
        <translation>Web 界面</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&quot;%1&quot; is not an IP address. The listen address was not changed.</source>
        <translation>“%1”不是 IP 地址。监听地址未更改。</translation>
    </message>
    <message>
        <location line="+503"/>
        <location line="+20"/>
        <source>File types</source>
        <translation>文件类型</translation>
    </message>
    <message>
        <source>Could not update the .nzb file association: %1</source>
        <translation type="vanished">无法更新 .nzb 文件关联：%1</translation>
    </message>
    <message>
        <location line="+673"/>
        <source>Port forwarding: %1</source>
        <translation>端口转发：%1</translation>
    </message>
    <message>
        <location line="+80"/>
        <source>Database: not connected to the core</source>
        <translation>数据库：未连接到核心</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Downloading the GeoLite2 country database...</source>
        <translation>正在下载 GeoLite2 国家数据库...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Update failed: the core did not answer</source>
        <translation>更新失败：核心无响应</translation>
    </message>
    <message>
        <location line="+53"/>
        <source>Database: GeoLite2-Country from %1</source>
        <translation>数据库：GeoLite2-Country（%1）</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Database: none — enter your MaxMind account ID and license key</source>
        <translation>数据库：无 — 请输入 MaxMind 账户 ID 和许可证密钥</translation>
    </message>
    <message>
        <location line="+3"/>
        <source> (update running)</source>
        <translation> (正在更新)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Last update failed: %1</source>
        <translation>上次更新失败：%1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Filename Cleanup</source>
        <translation>文件名清理</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Text to remove from the names of new downloads. Case is ignored.</source>
        <translation>要从新下载的名称中移除的文本。不区分大小写。</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Reset to Default</source>
        <translation>恢复默认值</translation>
    </message>
    <message>
        <location line="-7837"/>
        <location line="+2518"/>
        <location line="+1024"/>
        <location line="+154"/>
        <location line="+1627"/>
        <location line="+316"/>
        <location line="+2196"/>
        <source>Remove</source>
        <translation>删除</translation>
    </message>
    <message>
        <location line="-6552"/>
        <source>New eMule Qt version detected</source>
        <translation>检测到新的 eMule Qt 版本</translation>
    </message>
    <message>
        <location line="+412"/>
        <source>Update from URL: (filter.dat- or PeerGuardian-format, .gz/.zip accepted)</source>
        <translation>从 URL 更新：（filter.dat 或 PeerGuardian 格式，支持 .gz/.zip）</translation>
    </message>
    <message>
        <location line="+3327"/>
        <source>Write eMule core logs to disk</source>
        <translation>将 eMule 核心日志写入磁盘</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Write eMule GUI logs to disk</source>
        <translation>将 eMule 图形界面日志写入磁盘</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Log server connection &amp;&amp; search details (TCP/UDP handshake)</source>
        <translation>记录服务器连接 &amp;&amp; 搜索详情（TCP/UDP 握手）</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Log web server requests</source>
        <translation>记录 Web 服务器请求</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Log public IP address on startup</source>
        <translation>启动时记录公网 IP 地址</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Enable IPC log tab</source>
        <translation>启用 IPC 日志标签</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Start core with console (debug)</source>
        <translation>以控制台启动核心（调试）</translation>
    </message>
    <message>
        <location line="+101"/>
        <source>PCP (RFC 6887) — preferred, supports IPv6</source>
        <translation>PCP (RFC 6887) — 首选，支持 IPv6</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>NAT-PMP (RFC 6886) — IPv4 only</source>
        <translation>NAT-PMP (RFC 6886) — 仅 IPv4</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>UPnP IGD — fallback</source>
        <translation>UPnP IGD — 回退方案</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Open IPv6 firewall pinholes</source>
        <translation>打开 IPv6 防火墙针孔</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Requested lease:</source>
        <translation>请求的租期：</translation>
    </message>
    <message>
        <location line="-2514"/>
        <location line="+918"/>
        <location line="+1600"/>
        <source> s</source>
        <translation> 秒</translation>
    </message>
    <message>
        <location line="-5106"/>
        <source>Downloads that are receiving data, and the sources sending it, are drawn in bold. A source listed under another file (A4AF) is bold too while it transfers. The connected server and the selected category tab are bold as well.</source>
        <translation>正在接收数据的下载及发送数据的来源以粗体显示。列在其他文件下的来源 (A4AF) 在传输时也以粗体显示。已连接的服务器和选中的分类标签页同样以粗体显示。</translation>
    </message>
    <message>
        <location line="+1010"/>
        <source>Rename downloads to the name most sources use</source>
        <translation>将下载重命名为大多数来源使用的名称</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Renames a download once when at least 5 sources and 60% of them use the same name. Never after you renamed it yourself, and not when the file looks fake.</source>
        <translation>当至少 5 个来源且其中 60% 使用相同名称时，将下载重命名一次。你自己重命名后不会再改，文件看起来是假的时也不会。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Watch shared folders for new files</source>
        <translation>监视共享文件夹中的新文件</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Shares files added to a shared folder as they appear. When off, new files are only picked up on restart or when you reload the shared files.</source>
        <translation>文件添加到共享文件夹后立即共享。关闭时，新文件只会在重启或重新加载共享文件时被发现。</translation>
    </message>
    <message>
        <location line="+597"/>
        <source>the core did not answer</source>
        <translation>核心没有响应</translation>
    </message>
    <message numerus="yes">
        <location line="+10"/>
        <source>IP filter updated and reloaded (%n range(s)).</source>
        <translation>
            <numerusform>IP 过滤器已更新并重新加载（%n 个范围）。</numerusform>
        </translation>
    </message>
    <message>
        <location line="+56"/>
        <source>Update automatically every</source>
        <translation>自动更新，间隔</translation>
    </message>
    <message>
        <location line="+864"/>
        <source>Decimal GB, because that is what an invoice says — the 1024-based GB used elsewhere in eMule would put a 1000 GB plan 7% over.

Set it slightly under your plan. The figure is measured here, so it reads a few percent below your provider&apos;s, and articles already in flight when the limit is reached still finish.</source>
        <translation>使用十进制 GB，因为账单上就是这样写的——若按 eMule 其他地方使用的 1024 进制 GB 计算，1000 GB 的套餐会超出 7%。

请设置得略低于您的套餐。此数值在本地测量，因此会比服务商的数值低几个百分点；达到上限时已在传输中的文章仍会完成。</translation>
    </message>
    <message>
        <location line="+1939"/>
        <source>Queue</source>
        <translation>队列</translation>
    </message>
    <message>
        <location line="+91"/>
        <source>Used for both TCP and UDP. Must differ from the eD2K ports, the web server port and the core port.</source>
        <translation>同时用于 TCP 和 UDP。必须与 eD2K 端口、Web 服务器端口和核心端口不同。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Random</source>
        <translation>随机</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Picks a port that is free for TCP and UDP on this computer.</source>
        <translation>选择本机上 TCP 和 UDP 都空闲的端口。</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Map port with UPnP/NAT-PMP</source>
        <translation>通过 UPnP/NAT-PMP 映射端口</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Forwards the BitTorrent port on the router. Needs &quot;Use UPnP to Setup Ports&quot; on the Connection page.</source>
        <translation>在路由器上转发 BitTorrent 端口。需要在“连接”页面启用“使用 UPnP 设置端口”。</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Peers</source>
        <translation>下载数</translation>
    </message>
    <message>
        <location line="+68"/>
        <source>Peer connections of all torrents together.</source>
        <translation>所有种子合计的节点连接数。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Max. connections:</source>
        <translation>最大连接数：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Max. connections per torrent:</source>
        <translation>每个种子的最大连接数：</translation>
    </message>
    <message>
        <source>Global limits</source>
        <translation type="vanished">全局限速</translation>
    </message>
    <message>
        <location line="-57"/>
        <source>Set on the Connection page.</source>
        <translation>在“连接”页面设置。</translation>
    </message>
    <message>
        <source>eD2K and Usenet come first. BitTorrent keeps the share below while they are busy and uses all the bandwidth they leave idle.</source>
        <translation type="vanished">eD2K 和 Usenet 优先。它们繁忙时 BitTorrent 保留下方的份额，并使用它们闲置的全部带宽。</translation>
    </message>
    <message>
        <source>Sharing</source>
        <translation type="vanished">分配</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>How much of the global download limit BitTorrent keeps while eD2K or Usenet is also downloading. When they are idle, BitTorrent may use all of it.</source>
        <translation>eD2K 或 Usenet 也在下载时，BitTorrent 保留的全局下载限速份额。它们空闲时，BitTorrent 可使用全部。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Share of the upload limit:</source>
        <translation>占上传限速的份额：</translation>
    </message>
    <message>
        <location line="-2"/>
        <source>How much of the global upload limit BitTorrent keeps while eD2K is also uploading. When it is idle, BitTorrent may use all of it.</source>
        <translation>eD2K 也在上传时，BitTorrent 保留的全局上传限速份额。eD2K 空闲时，BitTorrent 可使用全部。</translation>
    </message>
    <message>
        <source>Hard limits</source>
        <translation type="vanished">硬性限速</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>BitTorrent never goes above this, even with bandwidth to spare. At most the global limit.</source>
        <translation>即使有空闲带宽，BitTorrent 也不会超过此值。最多为全局限速。</translation>
    </message>
    <message>
        <location line="+116"/>
        <source>Max. firewalled Kad nodes served as buddy:</source>
        <translation>作为伙伴服务的防火墙内 Kad 节点最大数：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>While your ports are open, eMule relays callback requests for this many firewalled Kad nodes. Each one uses a connection, so at most a tenth of &quot;Max. connections&quot; is served.</source>
        <translation>端口开放时，eMule 会为这么多个防火墙内的 Kad 节点中继回调请求。每个节点占用一个连接，因此最多只为“最大连接数”的十分之一提供中继。</translation>
    </message>
    <message>
        <location line="+423"/>
        <source>New</source>
        <translation>新建</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+19"/>
        <source>Title</source>
        <translation>标题</translation>
    </message>
    <message>
        <location line="-19"/>
        <source>Days</source>
        <translation>天</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Start Time</source>
        <translation>开始时间</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Details</source>
        <translation>详情</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Time</source>
        <translation>时间</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+113"/>
        <source>Daily</source>
        <translation>每天</translation>
    </message>
    <message>
        <location line="-113"/>
        <source>Monday</source>
        <translation>周一</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Tuesday</source>
        <translation>周二</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Wednesday</source>
        <translation>周三</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Thursday</source>
        <translation>周四</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Friday</source>
        <translation>周五</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Saturday</source>
        <translation>周六</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Sunday</source>
        <translation>周日</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Mon-Fri</source>
        <translation>周一至周五</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Mon-Sat</source>
        <translation>周一至周六</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Sat-Sun</source>
        <translation>周六至周日</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>No end time</source>
        <translation>无结束时间</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+5"/>
        <source>Action</source>
        <translation>操作</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Value</source>
        <translation>值</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>New Schedule</source>
        <translation>新建计划</translation>
    </message>
    <message>
        <location line="-2899"/>
        <location line="+1024"/>
        <location line="+154"/>
        <location line="+1900"/>
        <location line="+2234"/>
        <source>Add</source>
        <translation>添加</translation>
    </message>
    <message>
        <source>Action Value</source>
        <translation type="vanished">操作值</translation>
    </message>
    <message>
        <source>Enter value:</source>
        <translation type="vanished">输入值：</translation>
    </message>
    <message>
        <location line="-2197"/>
        <source>Edit Value</source>
        <translation>编辑值</translation>
    </message>
    <message>
        <location line="+99"/>
        <source>The %1 settings page is not yet implemented.</source>
        <translation>设置页面 %1 尚未实现。</translation>
    </message>
    <message>
        <location line="+192"/>
        <source>Proxy</source>
        <translation>代理</translation>
    </message>
    <message>
        <source>Proxy settings will only apply to new connections.
Restart eMule for all connections to use the new proxy settings.</source>
        <translation type="vanished">代理设置仅应用于新连接。
重启 eMule 以使所有连接使用新的代理设置。</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>The language change will take effect after restarting the application.</source>
        <translation>语言更改将在重启应用程序后生效。</translation>
    </message>
    <message>
        <location line="+34"/>
        <source>Core connection settings will take effect after restarting the application.</source>
        <translation>核心连接设置将在重启应用程序后生效。</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>Icons</source>
        <translation>图标</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The icon change will take effect after restarting the application.</source>
        <translation>图标更改将在重启应用程序后生效。</translation>
    </message>
</context>
<context>
    <name>eMule::PasteLinksDialog</name>
    <message>
        <location filename="../src/gui/dialogs/PasteLinksDialog.cpp" line="+16"/>
        <source>Paste eD2K Links</source>
        <translation>粘贴 eD2K 链接</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>eD2K Links:</source>
        <translation>eD2K 链接：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paste one or more ed2k:// links here, one per line...</source>
        <translation>在此粘贴一个或多个 ed2k:// 链接，每行一个...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Download</source>
        <translation>下载</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation type="vanished">取消</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Not Connected</source>
        <translation>未连接</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not connected to the daemon.</source>
        <translation>未连接到守护进程。</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Invalid Links</source>
        <translation>无效链接</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The following links could not be parsed:

%1</source>
        <translation>以下链接无法解析：

%1</translation>
    </message>
</context>
<context>
    <name>eMule::PasteTextDialog</name>
    <message>
        <location filename="../src/gui/dialogs/PasteTextDialog.cpp" line="+44"/>
        <source>optional</source>
        <translation>可选</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Category:</source>
        <translation>分类：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Priority:</source>
        <translation>优先级：</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+25"/>
        <source>Start paused</source>
        <translation>以暂停状态开始</translation>
    </message>
    <message>
        <location line="-14"/>
        <source>default download folder</source>
        <translation>默认下载文件夹</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Browse…</source>
        <translation>浏览…</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Save to</source>
        <translation>保存到</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="+91"/>
        <source>Working…</source>
        <translation>正在处理…</translation>
    </message>
</context>
<context>
    <name>eMule::PeerPreviewDialog</name>
    <message>
        <location filename="../src/gui/dialogs/PeerPreviewDialog.cpp" line="+17"/>
        <source>Preview: %1</source>
        <translation>预览：%1</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Prior</source>
        <translation>上一张</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Next</source>
        <translation>下一个</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Close</source>
        <translation>关闭</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>No images</source>
        <translation>没有图像</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Image %1 of %2</source>
        <translation>第 %1 张，共 %2 张</translation>
    </message>
</context>
<context>
    <name>eMule::RouterPage</name>
    <message>
        <location filename="../src/gui/dialogs/RouterPage.h" line="+30"/>
        <source>Open Router</source>
        <translation>打开路由器</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>A VPN seems to be active (%1). Incoming connections then arrive through the VPN, so port forwarding on your router does not help. Forward the ports at your VPN provider instead, if it offers port forwarding.</source>
        <translation>VPN 似乎已启用（%1）。传入连接会经由 VPN 到达，因此在路由器上转发端口没有作用。如果 VPN 提供商支持端口转发，请改在那里转发端口。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Could not find your router&apos;s address.

Look up the &quot;default gateway&quot; in this computer&apos;s network settings and open that address in a web browser.</source>
        <translation>找不到路由器的地址。

请在此计算机的网络设置中查找“默认网关”，然后在网页浏览器中打开该地址。</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Could not open %1 in the default browser.</source>
        <translation>无法在默认浏览器中打开 %1。</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Your router is at %1.</source>
        <translation>路由器地址为 %1。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Open Router Page</source>
        <translation>打开路由器页面</translation>
    </message>
</context>
<context>
    <name>eMule::SearchDetailDialog</name>
    <message>
        <location filename="../src/gui/dialogs/SearchDetailDialog.cpp" line="+42"/>
        <source>Details: %1</source>
        <translation>详情：%1</translation>
    </message>
    <message>
        <location line="+19"/>
        <location line="+2"/>
        <source>Metadata</source>
        <translation>元数据</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+2"/>
        <source>Comments</source>
        <translation>评论</translation>
    </message>
</context>
<context>
    <name>eMule::SearchExprParser</name>
    <message>
        <location filename="../src/core/search/SearchExprParser.cpp" line="+123"/>
        <location line="+678"/>
        <source>Failed to parse search expression</source>
        <translation>解析搜索表达式失败</translation>
    </message>
    <message>
        <location line="-668"/>
        <source>Expression too complex</source>
        <translation>表达式太复杂</translation>
    </message>
    <message>
        <location line="+79"/>
        <location line="+6"/>
        <location line="+48"/>
        <source>Unterminated string</source>
        <translation>字符串没有终结</translation>
    </message>
    <message>
        <location line="+49"/>
        <location line="+18"/>
        <source>Unknown search attribute &apos;%1&apos;</source>
        <translation>未知的搜索属性 &apos;%1&apos;</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Invalid eD2K file link</source>
        <translation>无效的 eD2K 文件链接</translation>
    </message>
    <message>
        <location line="+233"/>
        <source>Syntax error in &apos;%1&apos; search attribute</source>
        <translation>搜索属性 &apos;%1&apos; 中有语法错误</translation>
    </message>
    <message>
        <location line="+50"/>
        <source>Missing right side for AND operator</source>
        <translation>AND 运算符缺少右边的操作数</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Missing right side for OR operator</source>
        <translation>OR 运算符缺少右边的操作数</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Missing right side for NOT operator</source>
        <translation>NOT 运算符缺少右边的操作数</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Missing expression for &apos;(&apos; &apos;)&apos;</source>
        <translation>&apos;(&apos; &apos;)&apos; 缺少表达式</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Missing closing &apos;)&apos;</source>
        <translation>缺少终止符 &apos;)&apos;</translation>
    </message>
    <message>
        <location line="+66"/>
        <source>Missing left side for NOT operator</source>
        <translation>NOT 运算符缺少左边的操作数</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Missing left side for AND operator</source>
        <translation>AND 运算符缺少左边的操作数</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Missing left side for OR operator</source>
        <translation>OR 运算符缺少左边的操作数</translation>
    </message>
    <message>
        <location line="+174"/>
        <source>You cannot use the first keyword with an OR operator when searching on Kad!

For example &quot;Keyword1 OR Keyword2&quot; is not possible. Try &quot;Keyword1 AND (Keyword2 OR Keyword3)&quot; or do multiple searches instead.</source>
        <translation>当使用 Kad 搜索时，第一个关键词不能和连接词 OR 同时使用！

例如 &quot;关键词1 OR 关键词2&quot; 是无法执行的。请尝试 &quot;关键词1 AND (关键词2 OR 关键词3)&quot;，或者使用多次搜索代替。</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Keyword too short.

The keyword(s) used in a Kad search expression must have a minimum length of 3 characters.</source>
        <translation>关键字太短。

在 Kad 的搜索表达式中，关键字的最小长度是 3 个字符。</translation>
    </message>
</context>
<context>
    <name>eMule::SearchPanel</name>
    <message>
        <location filename="../src/gui/panels/SearchPanel.cpp" line="+325"/>
        <location line="+996"/>
        <location line="+13"/>
        <location line="+527"/>
        <source>Download</source>
        <translation>下载</translation>
    </message>
    <message>
        <location line="-1516"/>
        <source>Close All Searches</source>
        <translation>关闭所有搜索</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Name:</source>
        <translation>名称：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Enter search keywords...</source>
        <translation>输入搜索关键词...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Type:</source>
        <translation>类型：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Any</source>
        <translation>任意</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Audio</source>
        <translation>音频</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Video</source>
        <translation>视频</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Image</source>
        <translation>图片</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Document</source>
        <translation>文档</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Program</source>
        <translation>程序</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Archive</source>
        <translation>压缩包</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>CD-Image</source>
        <translation>CD 镜像</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Collection</source>
        <translation>合集</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Method:</source>
        <translation>方法：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Automatic</source>
        <translation>自动</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Kad Network</source>
        <translation>Kad 网络</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ed2k Server</source>
        <translation>Ed2k 服务器</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ed2k Global</source>
        <translation>Ed2k 全局</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Usenet (Indexer)</source>
        <translation>Usenet（索引器）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Usenet (Server)</source>
        <translation>Usenet（服务器）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Torrent (Server)</source>
        <translation>Torrent（服务器）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Web (Server)</source>
        <translation>网页（服务器）</translation>
    </message>
    <message>
        <location line="-227"/>
        <source>ed3k</source>
        <translation>ed3k</translation>
    </message>
    <message>
        <location line="+231"/>
        <source>Reset</source>
        <translation>重置</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Web results</source>
        <translation>网页结果</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Min. Size [MB]:</source>
        <translation>最小大小 [MB]：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Max. Size [MB]:</source>
        <translation>最大大小 [MB]：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Availability:</source>
        <translation>可用性：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Complete Sources:</source>
        <translation>完整来源：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Extension:</source>
        <translation>扩展名：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Codec:</source>
        <translation>编解码器：</translation>
    </message>
    <message>
        <location line="+2615"/>
        <source>ed3k download added: %1</source>
        <translation>已添加 ed3k 下载：%1</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>The ed3k download could not be added.</source>
        <translation>无法添加 ed3k 下载。</translation>
    </message>
    <message>
        <source>Min. Bitrate [kbps]:</source>
        <translation type="vanished">最小比特率 [kbps]：</translation>
    </message>
    <message>
        <location line="-2608"/>
        <source>Min. Length [s]:</source>
        <translation>最小时长 [秒]：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Title:</source>
        <translation>标题：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Album:</source>
        <translation>专辑：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Artist:</source>
        <translation>艺术家：</translation>
    </message>
    <message>
        <location line="-79"/>
        <source>Show results the server found on this network</source>
        <translation>显示服务器在此网络上找到的结果</translation>
    </message>
    <message>
        <location line="-214"/>
        <source>Related</source>
        <translation>相关</translation>
    </message>
    <message>
        <location line="+219"/>
        <source>Usenet results</source>
        <translation>Usenet 结果</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Kad results</source>
        <translation>Kad 结果</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Torrent results</source>
        <translation>种子结果</translation>
    </message>
    <message>
        <location line="+48"/>
        <source>Min. Bitrate [Kbit/s]:</source>
        <translation>最小比特率 [Kbit/s]:</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Start</source>
        <translation>开始</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>More</source>
        <translation>更多</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Ask the server for further results of this search</source>
        <translation>向服务器请求此搜索的更多结果</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="+34"/>
        <location line="+33"/>
        <location line="+204"/>
        <source>Not connected to daemon — search cannot be started.</source>
        <translation>未连接到守护进程 — 无法开始搜索。</translation>
    </message>
    <message>
        <location line="-224"/>
        <location line="+53"/>
        <source>Search</source>
        <translation>搜索</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Kad: &quot;%1&quot; is already being searched — using &quot;%2&quot; as the search target.</source>
        <translation>Kad：正在搜索 &quot;%1&quot; — 使用 &quot;%2&quot; 作为搜索目标。</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Queued — waiting for a server connection</source>
        <translation>已排队 — 正在等待服务器连接</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Queued — waiting for Kad to connect</source>
        <translation>已排队 — 正在等待 Kad 连接</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Queued — waiting for a server or Kad connection</source>
        <translation>已排队 — 正在等待服务器或 Kad 连接</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Queued — waiting for the previous search to finish</source>
        <translation>已排队 — 正在等待上一个搜索完成</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Queued — waiting for the server to say what it offers</source>
        <translation>已排队 — 正在等待服务器告知其提供的内容</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Queued</source>
        <translation>排队中</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Search failed: %1</source>
        <translation>搜索失败：%1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>%1 results — scroll down for more</source>
        <translation>%1 条结果 — 向下滚动查看更多</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>%1 of %2 results</source>
        <translation>%1 / %2 条结果</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Search &quot;%1&quot; failed: %2</source>
        <translation>搜索“%1”失败：%2</translation>
    </message>
    <message>
        <location line="+85"/>
        <location line="+182"/>
        <source>Usenet search</source>
        <translation>Usenet 搜索</translation>
    </message>
    <message>
        <location line="-90"/>
        <source>%1 results — %2 of %3 indexers</source>
        <translation>%1 条结果 — %3 个索引器中的 %2 个</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Usenet search: %1</source>
        <translation>Usenet 搜索：%1</translation>
    </message>
    <message>
        <location line="-227"/>
        <location line="+231"/>
        <source>No results</source>
        <translation>无结果</translation>
    </message>
    <message>
        <location line="+69"/>
        <source>Could not queue &quot;%1&quot;: %2</source>
        <translation>无法将“%1”加入队列：%2</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Queued &quot;%1&quot; for download from Usenet.</source>
        <translation>已将“%1”加入队列，从 Usenet 下载。</translation>
    </message>
    <message>
        <location line="+97"/>
        <source>Restore Search Parameters</source>
        <translation>恢复搜索参数</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Close</source>
        <translation>关闭</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Close Other Tabs</source>
        <translation>关闭其他标签页</translation>
    </message>
    <message>
        <location line="+98"/>
        <source>&amp;Download</source>
        <translation>下载(&amp;D)</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Download &amp;To</source>
        <translation>下载到(&amp;T)</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Copy &amp;Name</source>
        <translation>复制名称(&amp;N)</translation>
    </message>
    <message>
        <location line="+86"/>
        <source>%1 (%2)</source>
        <translation>%1 (%2)</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Paused</source>
        <translation>已暂停</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Download NZB File...</source>
        <translation>下载 NZB 文件...</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Download Torrent</source>
        <translation>下载种子</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Copy Magnet Link</source>
        <translation>复制磁力链接</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Show Links...</source>
        <translation>显示链接...</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Download Container...</source>
        <translation>下载容器...</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Details...</source>
        <translation>详情...</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Comments...</source>
        <translation>评论...</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Copy eD2K Links</source>
        <translation>复制 eD2K 链接</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Copy eD2K Links (HTML)</source>
        <translation>复制 eD2K 链接 (HTML)</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Copy Magnet Links</source>
        <translation>复制磁力链接</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Mark as not Spam</source>
        <translation>标记为非垃圾</translation>
    </message>
    <message>
        <location line="+65"/>
        <location line="+7"/>
        <location line="+981"/>
        <location line="+43"/>
        <source>Preview</source>
        <translation>预览</translation>
    </message>
    <message>
        <location line="-701"/>
        <source>You have already downloaded the following file(s). Download them again?

%1</source>
        <translation>您已经下载过以下文件。要重新下载吗？

%1</translation>
    </message>
    <message>
        <location line="+659"/>
        <source>Preview requested - please wait</source>
        <translation>已请求预览 - 请稍候</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>%1 sent no preview</source>
        <translation>%1 未发送预览</translation>
    </message>
    <message>
        <location line="+338"/>
        <source>Asking servers: %1 / %2</source>
        <translation>正在询问服务器：%1 / %2</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>All</source>
        <translation>全部</translation>
    </message>
    <message>
        <location line="-1447"/>
        <location line="+15"/>
        <location line="+10"/>
        <source>Mark as Spam</source>
        <translation>标记为垃圾</translation>
    </message>
    <message>
        <location line="-136"/>
        <source>Save Torrent File...</source>
        <translation>保存种子文件...</translation>
    </message>
    <message>
        <location line="+148"/>
        <source>Remove</source>
        <translation>删除</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Close Search Results</source>
        <translation>关闭搜索结果</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Close All Search Results</source>
        <translation>关闭所有搜索结果</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Find...</source>
        <translation>查找...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Search Related Files</source>
        <translation>搜索相关文件</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Web Services</source>
        <translation>Web 服务</translation>
    </message>
    <message>
        <location line="+986"/>
        <source>Preview not available — web server is not running or stream token not received.</source>
        <translation>预览不可用 — Web 服务器未运行或未收到流令牌。</translation>
    </message>
</context>
<context>
    <name>eMule::SearchResultsModel</name>
    <message>
        <location filename="../src/gui/controls/SearchResultsModel.cpp" line="+84"/>
        <source>Yes</source>
        <translation>是</translation>
    </message>
    <message>
        <location line="+270"/>
        <source>Stream</source>
        <translation>流媒体</translation>
    </message>
    <message numerus="yes">
        <location line="+99"/>
        <source>%1 · %n name(s)</source>
        <translation>
            <numerusform>%1 · %n 个名称</numerusform>
        </translation>
    </message>
    <message>
        <location line="+9"/>
        <source>File Name</source>
        <translation>文件名</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Availability</source>
        <translation>可用性</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Complete Sources</source>
        <translation>完整来源</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type</source>
        <translation>类型</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Artist</source>
        <translation>艺术家</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Album</source>
        <translation>专辑</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Title</source>
        <translation>标题</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Length</source>
        <translation>时长</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bitrate</source>
        <translation>比特率</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Codec</source>
        <translation>编解码器</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Known</source>
        <translation>已知</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Seen</source>
        <translation>已见过</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Confidence</source>
        <translation>可信度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>File ID</source>
        <translation>文件 ID</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Folder</source>
        <translation>文件夹</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>AICH Hash</source>
        <translation>AICH 哈希</translation>
    </message>
</context>
<context>
    <name>eMule::ServerListModel</name>
    <message>
        <location filename="../src/gui/controls/ServerListModel.cpp" line="+78"/>
        <source>%1 (disabled)</source>
        <translation>%1（已禁用）</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+3"/>
        <source>Yes</source>
        <translation>是</translation>
    </message>
    <message>
        <location line="-3"/>
        <location line="+3"/>
        <source>No</source>
        <translation>否</translation>
    </message>
    <message>
        <location line="+100"/>
        <source>Server Name</source>
        <translation>服务器名称</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>IP</source>
        <translation>IP</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Description</source>
        <translation>描述</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ping</source>
        <translation>Ping</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Users</source>
        <translation>用户</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Max Users</source>
        <translation>最大用户数</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Files</source>
        <translation>文件</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Preference</source>
        <translation>偏好</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Failed</source>
        <translation>失败</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Static</source>
        <translation>静态</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Soft File Limit</source>
        <translation>软文件限制</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Country</source>
        <translation>国家</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>IPv6</source>
        <translation>IPv6</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hard File Limit</source>
        <translation>硬文件限制</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Version</source>
        <translation>版本</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>High</source>
        <translation>高</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Low</source>
        <translation>低</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Normal</source>
        <translation>普通</translation>
    </message>
    <message>
        <source>Soft Files</source>
        <translation type="vanished">软文件</translation>
    </message>
    <message>
        <location line="-42"/>
        <source>Low ID</source>
        <translation>Low ID</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Obfuscation</source>
        <translation>混淆</translation>
    </message>
</context>
<context>
    <name>eMule::ServerPanel</name>
    <message>
        <location filename="../src/gui/panels/ServerPanel.cpp" line="+267"/>
        <source>Disconnect</source>
        <translation>断开连接</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+435"/>
        <source>Connect</source>
        <translation>连接</translation>
    </message>
    <message>
        <location line="-497"/>
        <source>Invalid URL: %1</source>
        <translation>无效的URL: %1</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Downloading server.met from %1 ...</source>
        <translation>正在从%1下载server.met...</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Failed to download server.met: %1</source>
        <translation>下载server.met失败: %1</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Downloaded empty server.met file.</source>
        <translation>下载的server.met文件为空。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Downloaded server.met (%1 bytes). Parsing...</source>
        <translation>已下载server.met (%1字节)。正在解析...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloaded server.met, unpacked &quot;%1&quot; (%2 bytes). Parsing...</source>
        <translation>已下载 server.met，解压 &quot;%1&quot;（%2 字节）。正在解析...</translation>
    </message>
    <message>
        <location line="+623"/>
        <location line="+2"/>
        <location line="+24"/>
        <location line="+39"/>
        <source>IP:Port:</source>
        <translation>IP:端口：</translation>
    </message>
    <message>
        <location line="-65"/>
        <source>Unknown</source>
        <translation>未知</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+64"/>
        <source>ID:</source>
        <translation>ID：</translation>
    </message>
    <message>
        <location line="-48"/>
        <source>eD2K Server</source>
        <translation>eD2K服务器</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Description:</source>
        <translation>描述：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Version:</source>
        <translation>版本：</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+48"/>
        <source>Users:</source>
        <translation>用户数：</translation>
    </message>
    <message>
        <location line="-47"/>
        <location line="+49"/>
        <source>Files:</source>
        <translation>文件数：</translation>
    </message>
    <message>
        <location line="-48"/>
        <source>Connection:</source>
        <translation>连接：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Obfuscated</source>
        <translation>已混淆</translation>
    </message>
    <message>
        <location line="+18"/>
        <location line="+7"/>
        <source>Open</source>
        <translation>开放</translation>
    </message>
    <message>
        <location line="-3"/>
        <location line="+6"/>
        <source>UDP Status:</source>
        <translation>UDP 状态：</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>unverified</source>
        <translation>未验证</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Extern UDP Port:</source>
        <translation>外部 UDP 端口：</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Web Interface</source>
        <translation>Web 界面</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Enabled</source>
        <translation>已启用</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Disabled</source>
        <translation>已禁用</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>▸ Servers (%1)</source>
        <translation>▸ 服务器 (%1)</translation>
    </message>
    <message>
        <location line="-652"/>
        <source>Connect To</source>
        <translation>连接到</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Priority</source>
        <translation>优先级</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Low</source>
        <translation>低</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+546"/>
        <source>Normal</source>
        <translation>普通</translation>
    </message>
    <message>
        <location line="-545"/>
        <source>High</source>
        <translation>高</translation>
    </message>
    <message>
        <location line="+93"/>
        <source>Move Up</source>
        <translation>上移</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Move Down</source>
        <translation>下移</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Add To Static List</source>
        <translation>添加到静态列表</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Remove From Static List</source>
        <translation>从静态列表移除</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Enable</source>
        <translation>启用</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Copy eD2K Links</source>
        <translation>复制 eD2K 链接</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Paste eD2K Links</source>
        <translation>粘贴 eD2K 链接</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Remove</source>
        <translation>删除</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Remove All</source>
        <translation>全部删除</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>eNode Account...</source>
        <translation>eNode 账户...</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Find...</source>
        <translation>查找...</translation>
    </message>
    <message>
        <location line="+55"/>
        <source>▸ Servers (0)</source>
        <translation>▸ 服务器 (0)</translation>
    </message>
    <message>
        <location line="+80"/>
        <source>New Server</source>
        <translation>新服务器</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>IP Address:</source>
        <translation>IP 地址：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Port:</source>
        <translation>端口：</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+150"/>
        <source>Name:</source>
        <translation>名称：</translation>
    </message>
    <message>
        <location line="-144"/>
        <source>Add to list</source>
        <translation>添加到列表</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Update server.met from URL</source>
        <translation>从 URL 更新 server.met</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Update server.met from URL:</source>
        <translation>从 URL 更新 server.met：</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Update</source>
        <translation>更新</translation>
    </message>
    <message>
        <location line="+68"/>
        <source>My Info</source>
        <translation>我的信息</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>eD2K Network</source>
        <translation>eD2K 网络</translation>
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
        <translation>状态：</translation>
    </message>
    <message>
        <location line="-95"/>
        <source>Connected</source>
        <translation>已连接</translation>
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
        <translation>连接中...</translation>
    </message>
    <message>
        <location line="-45"/>
        <location line="+48"/>
        <source>Disconnected</source>
        <translation>已断开</translation>
    </message>
    <message>
        <location line="-44"/>
        <source>Kad Network</source>
        <translation>Kad 网络</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+8"/>
        <source>Firewalled</source>
        <translation>防火墙内</translation>
    </message>
    <message numerus="yes">
        <location line="+621"/>
        <source>%n server(s) not added — LAN addresses are filtered</source>
        <translation>
            <numerusform>未添加 %n 个服务器 — 已过滤局域网地址</numerusform>
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
        <translation>无效的server.met头: 0x%1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Server count too large: %1</source>
        <translation>服务器数量过多: %1</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Corrupt server.met: tag count %1 at server %2</source>
        <translation>损坏的server.met: 服务器%2的标签数%1</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Corrupt server.met: truncated tag name</source>
        <translation>损坏的server.met: 标签名被截断</translation>
    </message>
    <message>
        <location line="+52"/>
        <source>Corrupt server.met: truncated hash tag</source>
        <translation>server.met 已损坏：哈希标签被截断</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>Unknown tag type 0x%1 at server %2, stopping parse</source>
        <translation>服务器%2出现未知标签类型0x%1，停止解析</translation>
    </message>
    <message>
        <location line="+49"/>
        <source>server.met processed: %1 servers added, %2 skipped (duplicates/invalid).</source>
        <translation>server.met处理完成: 添加%1个服务器，跳过%2个（重复/无效）。</translation>
    </message>
</context>
<context>
    <name>eMule::SharedFilesModel</name>
    <message>
        <location filename="../src/gui/controls/SharedFilesModel.cpp" line="+46"/>
        <source>Yes</source>
        <translation>是</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>No</source>
        <translation>否</translation>
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
        <translation>文件名：	%1
ED2K 哈希：	%2
类型：	%3
优先级：	%4
请求：	%5 (%6)
已接受：	%7 (%8)
已传输：	%9 (%10)
完整来源：	%11
共享 eD2K|Kad：	%12
文件夹：	%13</translation>
    </message>
    <message>
        <location line="+139"/>
        <source>File Name</source>
        <translation>文件名</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type</source>
        <translation>类型</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority</source>
        <translation>优先级</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Requests</source>
        <translation>请求</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Transferred Data</source>
        <translation>已传输数据</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Shared parts</source>
        <translation>共享部分</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Complete Sources</source>
        <translation>完整来源</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Shared eD2K|Kad</source>
        <translation>共享 eD2K|Kad</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>File ID</source>
        <translation>文件 ID</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Accepted Requests</source>
        <translation>已接受的请求</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Artist</source>
        <translation>艺术家</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Album</source>
        <translation>专辑</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Title</source>
        <translation>标题</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Length</source>
        <translation>时长</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bitrate</source>
        <translation>比特率</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Codec</source>
        <translation>编解码器</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ratio</source>
        <translation>分享率</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Session Ratio</source>
        <translation>本次会话分享率</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Last Request</source>
        <translation>最后请求</translation>
    </message>
    <message>
        <source>Shared eD2K/Kad</source>
        <translation type="vanished">共享 eD2K/Kad</translation>
    </message>
    <message>
        <location line="-11"/>
        <source>Folder</source>
        <translation>文件夹</translation>
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
        <translation>共享文件 (0)</translation>
    </message>
    <message>
        <location line="-948"/>
        <source>Open File</source>
        <translation>打开文件</translation>
    </message>
    <message>
        <location line="+13"/>
        <location line="+1508"/>
        <location line="+504"/>
        <source>Open Folder</source>
        <translation>打开文件夹</translation>
    </message>
    <message>
        <location line="-2000"/>
        <source>Rename...</source>
        <translation>重命名...</translation>
    </message>
    <message>
        <location line="+182"/>
        <location line="+1860"/>
        <source>Copy eD2K Links</source>
        <translation>复制 eD2K 链接</translation>
    </message>
    <message>
        <source>Rename File</source>
        <translation type="vanished">重命名文件</translation>
    </message>
    <message>
        <source>New file name:</source>
        <translation type="vanished">新文件名:</translation>
    </message>
    <message>
        <location line="-2022"/>
        <location line="+1984"/>
        <source>Delete From Disk</source>
        <translation>从磁盘删除</translation>
    </message>
    <message>
        <location line="-984"/>
        <source>Delete File</source>
        <translation>删除文件</translation>
    </message>
    <message>
        <location line="-6"/>
        <source>Are you sure you want to permanently delete &quot;%1&quot; from disk?</source>
        <translation>确定要从磁盘永久删除&quot;%1&quot;吗？</translation>
    </message>
    <message>
        <location line="-979"/>
        <source>Unshare</source>
        <translation>取消共享</translation>
    </message>
    <message>
        <location line="+1012"/>
        <source>Unshare File</source>
        <translation>取消共享文件</translation>
    </message>
    <message>
        <location line="-6"/>
        <source>Remove &quot;%1&quot; from the shared files list?

The file will remain on disk.</source>
        <translation>从共享文件列表中移除&quot;%1&quot;？

文件将保留在磁盘上。</translation>
    </message>
    <message>
        <location line="-995"/>
        <location line="+1964"/>
        <source>Priority (Upload)</source>
        <translation>优先级（上传）</translation>
    </message>
    <message>
        <location line="-1949"/>
        <location line="+1966"/>
        <source>Very Low</source>
        <translation>非常低</translation>
    </message>
    <message>
        <location line="-1965"/>
        <location line="+1966"/>
        <source>Low</source>
        <translation>低</translation>
    </message>
    <message>
        <location line="-1965"/>
        <location line="+1966"/>
        <source>Normal</source>
        <translation>普通</translation>
    </message>
    <message>
        <location line="-1965"/>
        <location line="+1966"/>
        <source>High</source>
        <translation>高</translation>
    </message>
    <message>
        <source>Very High</source>
        <translation type="vanished">非常高</translation>
    </message>
    <message>
        <location line="-1963"/>
        <location line="+1965"/>
        <source>Auto</source>
        <translation>自动</translation>
    </message>
    <message>
        <location line="-1951"/>
        <source>Collection</source>
        <translation>合集</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Create Collection...</source>
        <translation>创建合集...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Modify Collection...</source>
        <translation>修改合集...</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>View Collection...</source>
        <translation>查看合集...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Search Author&apos;s Collections...</source>
        <translation>搜索作者的合集...</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+6"/>
        <source>Search Author&apos;s Collections</source>
        <translation>搜索作者的合集</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>This collection carries no author key, so its author&apos;s other collections cannot be looked up.</source>
        <translation>此合集不含作者密钥，因此无法查找该作者的其他合集。</translation>
    </message>
    <message>
        <location line="+19"/>
        <location line="+1871"/>
        <source>Details...</source>
        <translation>详情...</translation>
    </message>
    <message>
        <location line="-1864"/>
        <location line="+1867"/>
        <source>Comments...</source>
        <translation>评论...</translation>
    </message>
    <message>
        <location line="-1852"/>
        <location line="+1858"/>
        <source>eD2K Links...</source>
        <translation>eD2K 链接...</translation>
    </message>
    <message>
        <location line="-1840"/>
        <source>Find...</source>
        <translation>查找...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Web Services</source>
        <translation>Web 服务</translation>
    </message>
    <message>
        <location line="+71"/>
        <source>Reload</source>
        <translation>重新加载</translation>
    </message>
    <message>
        <source>File Name</source>
        <translation type="vanished">文件名</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>All Shared Files</source>
        <translation>所有共享文件</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Incoming Files</source>
        <translation>接收文件</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Incomplete Files</source>
        <translation>未完成的文件</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Shared Directories</source>
        <translation>共享目录</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>All Directories</source>
        <translation>所有目录</translation>
    </message>
    <message>
        <location line="+239"/>
        <source>Current Session</source>
        <translation>当前会话</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+45"/>
        <source>Popularity Rank:</source>
        <translation>流行度排名：</translation>
    </message>
    <message>
        <location line="-39"/>
        <location line="+45"/>
        <source>  Requests:</source>
        <translation>  请求数：</translation>
    </message>
    <message>
        <location line="-38"/>
        <source>On Queue:</source>
        <translation>排队中：</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+40"/>
        <source>  Accepted Uploads:</source>
        <translation>  已接受上传：</translation>
    </message>
    <message>
        <location line="-33"/>
        <source>Uploading:</source>
        <translation>上传中：</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+35"/>
        <source>  Transferred:</source>
        <translation>  已传输：</translation>
    </message>
    <message>
        <location line="-27"/>
        <source>Total</source>
        <translation>总计</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Statistics</source>
        <translation>统计</translation>
    </message>
    <message>
        <location line="+41"/>
        <source>Include Media Info</source>
        <translation>包含媒体信息</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add a line with resolution, codecs, length and bitrate under the HTML link of a complete file</source>
        <translation>在完整文件的 HTML 链接下添加一行，显示分辨率、编解码器、时长和比特率</translation>
    </message>
    <message>
        <location line="+259"/>
        <source>%1 (%2 of %3 shared)</source>
        <translation>%1（%3 个中已共享 %2 个）</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Could not share that file</source>
        <translation>无法共享该文件</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Could not unshare that file</source>
        <translation>无法取消共享该文件</translation>
    </message>
    <message>
        <location line="+526"/>
        <source>Share Directory</source>
        <translation>共享目录</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Share with Subdirectories</source>
        <translation>连同子目录一起共享</translation>
    </message>
    <message>
        <location line="+14"/>
        <location line="+516"/>
        <source>Unshare Directory</source>
        <translation>取消共享目录</translation>
    </message>
    <message>
        <location line="-507"/>
        <location line="+516"/>
        <source>Unshare with Subdirectories</source>
        <translation>连同子目录一起取消共享</translation>
    </message>
    <message>
        <location line="-420"/>
        <source>Open File not available — web server is not running or stream token not received.</source>
        <translation>打开文件不可用 — Web 服务器未运行或未收到流令牌。</translation>
    </message>
    <message>
        <location line="-973"/>
        <source>Content</source>
        <translation>内容</translation>
    </message>
    <message>
        <location line="+67"/>
        <source>eD2K Links</source>
        <translation>eD2K 链接</translation>
    </message>
    <message>
        <location line="-20"/>
        <source>Copy</source>
        <translation>复制</translation>
    </message>
    <message>
        <location line="-837"/>
        <source>All</source>
        <translation>全部</translation>
    </message>
    <message>
        <location line="+130"/>
        <source>Mass Rename...</source>
        <translation>批量重命名...</translation>
    </message>
    <message>
        <location line="+58"/>
        <location line="+1966"/>
        <source>Release</source>
        <translation>发布</translation>
    </message>
    <message>
        <location line="-1833"/>
        <source>Add To IRC Clipboard</source>
        <translation>添加到 IRC 剪贴板</translation>
    </message>
    <message>
        <location line="+94"/>
        <source>Rescan the shared directories. Hold %1 to re-read the media information of all shared files instead.</source>
        <translation>重新扫描共享目录。按住 %1 可改为重新读取所有共享文件的媒体信息。</translation>
    </message>
    <message>
        <location line="+389"/>
        <source>Basic Options</source>
        <translation>基本选项</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Add Source</source>
        <translation>添加来源</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Not available (requires public IP and open firewall)</source>
        <translation>不可用（需要公网 IP 和开放的防火墙）</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Advanced Options</source>
        <translation>高级选项</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Add HTML</source>
        <translation>添加 HTML</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add Hashset</source>
        <translation>添加哈希集</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hostname</source>
        <translation>主机名</translation>
    </message>
    <message>
        <location line="+33"/>
        <location line="+474"/>
        <source>Requires a hostname configured in Preferences, or a public IPv6</source>
        <translation>需要在首选项中配置主机名，或拥有公网 IPv6</translation>
    </message>
    <message>
        <location line="-301"/>
        <source>Shared Files (%1)</source>
        <translation>共享文件 (%1)</translation>
    </message>
    <message numerus="yes">
        <location line="+113"/>
        <source>Are you sure you want to permanently delete %n selected file(s) from disk?</source>
        <translation>
            <numerusform>确定要从磁盘永久删除选中的 %n 个文件吗？</numerusform>
        </translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Delete Files</source>
        <translation>删除文件</translation>
    </message>
    <message numerus="yes">
        <location line="+5"/>
        <source>Deleting %n shared file(s) from disk</source>
        <translation>
            <numerusform>正在从磁盘删除 %n 个共享文件</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="+18"/>
        <source>Remove %n selected file(s) from the shared files list?

The files will remain on disk.</source>
        <translation>
            <numerusform>从共享文件列表中移除选中的 %n 个文件？

这些文件仍会保留在磁盘上。</numerusform>
        </translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Unshare Files</source>
        <translation>取消共享文件</translation>
    </message>
    <message>
        <location line="+156"/>
        <source>Add your hostname or public IPv6 as a source</source>
        <translation>将您的主机名或公网 IPv6 添加为来源</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Showing eD2K links for the first %1 of %2 selected files.</source>
        <translation>正在显示所选 %2 个文件中前 %1 个的 eD2K 链接。</translation>
    </message>
</context>
<context>
    <name>eMule::SimpleCleanupDialog</name>
    <message>
        <location filename="../src/gui/dialogs/SimpleCleanupDialog.cpp" line="+50"/>
        <source>Simple cleanup</source>
        <translation>简单清理</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Convert &quot;.&quot; to &quot; &quot;</source>
        <translation>将 &quot;.&quot; 转换为 &quot; &quot;</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Convert &quot;_&quot; to &quot; &quot;</source>
        <translation>将 &quot;_&quot; 转换为 &quot; &quot;</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Convert wrong apostrophes &quot;´&quot;, &quot;`&quot; to standard apostrophes &quot; &apos; &quot;</source>
        <translation>将错误的撇号 &quot;´&quot;, &quot;`&quot; 转换为标准撇号 &quot; &apos; &quot;</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Convert hexadecimal numbers &quot;%xx&quot; back to characters</source>
        <translation>将十六进制数 &quot;%xx&quot; 转换回字符</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Search and Replace:</source>
        <translation>查找和替换:</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Search for:</source>
        <translation>搜索：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Replace by:</source>
        <translation>替换为:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Case sensitive search</source>
        <translation>区分大小写</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Replace characters:</source>
        <translation>替换字符:</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Replace</source>
        <translation>替换</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>by</source>
        <translation>为</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Search for</source>
        <translation>查找</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Replace by</source>
        <translation>替换为</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>&amp;Add</source>
        <translation>添加(&amp;A)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Edit</source>
        <translation>编辑(&amp;E)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&amp;Delete</source>
        <translation>删除(&amp;D)</translation>
    </message>
</context>
<context>
    <name>eMule::StatisticsPanel</name>
    <message>
        <location filename="../src/gui/panels/StatisticsPanel.cpp" line="-59"/>
        <location line="+11"/>
        <source>Session average</source>
        <translation>会话平均</translation>
    </message>
    <message>
        <source>Average (3 min)</source>
        <translation type="vanished">平均 (3 分钟)</translation>
    </message>
    <message>
        <location line="-8"/>
        <location line="+10"/>
        <source>Current</source>
        <translation>当前</translation>
    </message>
    <message>
        <source>KB/s</source>
        <translation type="vanished">KB/s</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Current (excl. overhead)</source>
        <translation>当前（不含开销）</translation>
    </message>
    <message>
        <source>Friend slots</source>
        <translation type="vanished">好友位</translation>
    </message>
    <message>
        <source>Active connections</source>
        <translation type="vanished">活跃连接</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Active uploads</source>
        <translation>活跃上传</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total uploads</source>
        <translation>总上传</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Active downloads</source>
        <translation>活跃下载</translation>
    </message>
    <message>
        <location line="+49"/>
        <source>Transfer</source>
        <translation>传输</translation>
    </message>
    <message>
        <source>Session UL:DL Ratio: -</source>
        <translation type="vanished">会话上传:下载比率：-</translation>
    </message>
    <message>
        <source>Friend Session UL:DL Ratio: -</source>
        <translation type="vanished">好友会话上传:下载比率：-</translation>
    </message>
    <message>
        <source>Cumulative UL:DL Ratio: -</source>
        <translation type="vanished">累计上传:下载比率：-</translation>
    </message>
    <message>
        <location line="+10"/>
        <location line="+165"/>
        <location line="+25"/>
        <location line="+384"/>
        <location line="+1895"/>
        <source>Uploads</source>
        <translation>上传</translation>
    </message>
    <message>
        <location line="-2465"/>
        <location line="+67"/>
        <location line="+83"/>
        <location line="+56"/>
        <location line="+43"/>
        <location line="+1728"/>
        <location line="+66"/>
        <location line="+124"/>
        <location line="+166"/>
        <location line="+121"/>
        <source>Session</source>
        <translation>会话</translation>
    </message>
    <message>
        <location line="-2451"/>
        <location line="+35"/>
        <source>Uploaded Data: 0 Bytes</source>
        <translation>已上传数据：0 Bytes</translation>
    </message>
    <message>
        <location line="-22"/>
        <source>Uploaded Data to Friends: 0 Bytes</source>
        <translation>上传给好友的数据：0 Bytes</translation>
    </message>
    <message>
        <source>Active Uploads: 0</source>
        <translation type="vanished">活跃上传：0</translation>
    </message>
    <message>
        <source>Waiting Uploads: 0</source>
        <translation type="vanished">等待上传：0</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+28"/>
        <location line="+528"/>
        <location line="+141"/>
        <location line="+45"/>
        <source>Upload Sessions</source>
        <translation>上传会话</translation>
    </message>
    <message>
        <source>Successful: 0</source>
        <translation type="vanished">成功：0</translation>
    </message>
    <message>
        <source>Failed: 0</source>
        <translation type="vanished">失败：0</translation>
    </message>
    <message>
        <location line="-737"/>
        <location line="+28"/>
        <source>Average Upload Per Session: 0 Bytes</source>
        <translation>每次会话平均上传：0 Bytes</translation>
    </message>
    <message>
        <location line="-26"/>
        <location line="+28"/>
        <source>Average Upload Time: 0:00:00</source>
        <translation>平均上传时间：0:00:00</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+105"/>
        <location line="+24"/>
        <location line="+393"/>
        <location line="+1504"/>
        <location line="+380"/>
        <source>Downloads</source>
        <translation>下载</translation>
    </message>
    <message>
        <location line="-2399"/>
        <location line="+43"/>
        <source>Downloaded Data: 0 Bytes</source>
        <translation>已下载数据：0 Bytes</translation>
    </message>
    <message>
        <source>Active Downloads: 0</source>
        <translation type="vanished">活跃下载：0</translation>
    </message>
    <message>
        <location line="-32"/>
        <source>Found Sources: 0</source>
        <translation>已找到来源：0</translation>
    </message>
    <message>
        <location line="+65"/>
        <source>Connection</source>
        <translation>连接</translation>
    </message>
    <message>
        <source>Active Connections: 0</source>
        <translation type="vanished">活跃连接：0</translation>
    </message>
    <message>
        <source>Peak Connections: 0</source>
        <translation type="vanished">峰值连接：0</translation>
    </message>
    <message>
        <source>Max Connections Limit Reached: 0</source>
        <translation type="vanished">已达到最大连接限制：0</translation>
    </message>
    <message>
        <source>Reconnects: 0</source>
        <translation type="vanished">重连次数：0</translation>
    </message>
    <message>
        <location line="+53"/>
        <source>Time Statistics</source>
        <translation>时间统计</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Time Since Last Reset: -</source>
        <translation>自上次重置以来的时间：-</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Runtime: 0:00:00</source>
        <translation>运行时间：0:00:00</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+11"/>
        <source>Transfer Time: 0:00:00</source>
        <translation>传输时间：0:00:00</translation>
    </message>
    <message>
        <location line="-10"/>
        <location line="+11"/>
        <source>Upload Time: 0:00:00</source>
        <translation>上传时间：0:00:00</translation>
    </message>
    <message>
        <location line="-10"/>
        <location line="+11"/>
        <source>Download Time: 0:00:00</source>
        <translation>下载时间：0:00:00</translation>
    </message>
    <message>
        <source>Server Duration: 0:00:00</source>
        <translation type="vanished">服务器持续时间：0:00:00</translation>
    </message>
    <message>
        <location line="-218"/>
        <location line="+35"/>
        <location line="+32"/>
        <location line="+43"/>
        <location line="+114"/>
        <location line="+332"/>
        <source>Clients</source>
        <translation>客户端</translation>
    </message>
    <message>
        <location line="-553"/>
        <location line="+35"/>
        <location line="+32"/>
        <location line="+43"/>
        <source>%1: 0 Bytes</source>
        <translation>%1：0 Bytes</translation>
    </message>
    <message>
        <location line="-109"/>
        <location line="+35"/>
        <location line="+32"/>
        <location line="+43"/>
        <location line="+119"/>
        <location line="+327"/>
        <source>Port</source>
        <translation>端口</translation>
    </message>
    <message>
        <location line="-555"/>
        <location line="+35"/>
        <location line="+32"/>
        <location line="+43"/>
        <source>Default Port 4662: 0 Bytes</source>
        <translation>默认端口 4662：0 Bytes</translation>
    </message>
    <message>
        <location line="-109"/>
        <location line="+35"/>
        <location line="+32"/>
        <location line="+43"/>
        <source>Other Ports: 0 Bytes</source>
        <translation>其他端口：0 Bytes</translation>
    </message>
    <message>
        <location line="-109"/>
        <location line="+35"/>
        <location line="+529"/>
        <source>Data Source</source>
        <translation>数据来源</translation>
    </message>
    <message>
        <location line="-563"/>
        <location line="+35"/>
        <source>Complete File: 0 Bytes</source>
        <translation>完整文件：0 Bytes</translation>
    </message>
    <message>
        <location line="-34"/>
        <location line="+35"/>
        <source>Part File: 0 Bytes</source>
        <translation>分块文件：0 Bytes</translation>
    </message>
    <message>
        <location line="-13"/>
        <location line="+75"/>
        <location line="+66"/>
        <location line="+41"/>
        <location line="+32"/>
        <location line="+1733"/>
        <location line="+190"/>
        <location line="+162"/>
        <location line="+125"/>
        <source>Cumulative</source>
        <translation>累计</translation>
    </message>
    <message>
        <location line="-2380"/>
        <location line="+43"/>
        <source>Completed Downloads: 0</source>
        <translation>已完成下载：0</translation>
    </message>
    <message>
        <location line="-32"/>
        <location line="+34"/>
        <location line="+467"/>
        <location line="+270"/>
        <location line="+10"/>
        <source>Download Sessions</source>
        <translation>下载会话</translation>
    </message>
    <message>
        <location line="-776"/>
        <location line="+34"/>
        <source>Average Download Per Session: 0 Bytes</source>
        <translation>每次会话平均下载：0 Bytes</translation>
    </message>
    <message>
        <location line="-32"/>
        <location line="+34"/>
        <source>Average Download Time: 0:00:00</source>
        <translation>平均下载时间：0:00:00</translation>
    </message>
    <message>
        <location line="-31"/>
        <location line="+34"/>
        <source>Gain Due To Compression: 0 Bytes (0.0%)</source>
        <translation>压缩带来的收益：0 Bytes (0.0%)</translation>
    </message>
    <message>
        <location line="-32"/>
        <location line="+34"/>
        <source>Lost Due To Corruption: 0 Bytes (0.0%)</source>
        <translation>损坏造成的损失：0 Bytes (0.0%)</translation>
    </message>
    <message>
        <location line="-32"/>
        <location line="+34"/>
        <source>Parts Saved Due To ICH: 0</source>
        <translation>通过 ICH 挽救的分块：0</translation>
    </message>
    <message>
        <location line="+15"/>
        <location line="+26"/>
        <location line="+1742"/>
        <location line="+82"/>
        <location line="+65"/>
        <source>General</source>
        <translation>常规</translation>
    </message>
    <message>
        <source>Average Connections: 0.0</source>
        <translation type="vanished">平均连接数：0.0</translation>
    </message>
    <message>
        <location line="-1905"/>
        <source>Upload Speed: 0 KB/s</source>
        <translation>上传速度：0 KB/s</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+24"/>
        <source>Max Upload Rate: 0 KB/s</source>
        <translation>最大上传速率：0 KB/s</translation>
    </message>
    <message>
        <location line="-23"/>
        <location line="+24"/>
        <source>Max Average Upload Rate: 0 KB/s</source>
        <translation>最大平均上传速率：0 KB/s</translation>
    </message>
    <message>
        <location line="-20"/>
        <source>Download Speed: 0 KB/s</source>
        <translation>下载速度：0 KB/s</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+23"/>
        <source>Max Download Rate: 0 KB/s</source>
        <translation>最大下载速率：0 KB/s</translation>
    </message>
    <message>
        <location line="-22"/>
        <location line="+23"/>
        <source>Max Average Download Rate: 0 KB/s</source>
        <translation>最大平均下载速率：0 KB/s</translation>
    </message>
    <message>
        <source>Server Reconnects: 0</source>
        <translation type="vanished">服务器重连次数：0</translation>
    </message>
    <message>
        <source>Connection Limit Reached: 0</source>
        <translation type="vanished">达到连接限制：0</translation>
    </message>
    <message>
        <location line="-8"/>
        <source>Average Upload Rate: 0 KB/s</source>
        <translation>平均上传速率：0 KB/s</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Average Download Rate: 0 KB/s</source>
        <translation>平均下载速率：0 KB/s</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+791"/>
        <location line="+5"/>
        <source>Statistics Last Reset: %1</source>
        <translation>统计上次重置: %1</translation>
    </message>
    <message>
        <location line="-1112"/>
        <location line="+316"/>
        <location line="+622"/>
        <location line="+167"/>
        <location line="+8"/>
        <location line="+67"/>
        <location line="+69"/>
        <location line="+261"/>
        <location line="+982"/>
        <source>Unknown</source>
        <translation>未知</translation>
    </message>
    <message>
        <location line="-2499"/>
        <source>Statistics Tree</source>
        <translation>统计树</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+1116"/>
        <source>Statistics last reset: %1</source>
        <translation>统计上次重置: %1</translation>
    </message>
    <message>
        <location line="-1087"/>
        <location line="+2249"/>
        <source>Usenet</source>
        <translation>Usenet</translation>
    </message>
    <message>
        <location line="-2175"/>
        <location line="+733"/>
        <location line="+45"/>
        <location line="+1128"/>
        <source>Waiting...</source>
        <translation>等待...</translation>
    </message>
    <message>
        <location line="-1903"/>
        <location line="+668"/>
        <source>Session UL:DL Ratio (Friends UL excluded): %1</source>
        <translation>会话上传:下载比率 (排除对好友的上传)：%1</translation>
    </message>
    <message>
        <location line="-573"/>
        <source>UDP File Re-asks: 0, Failed: 0 (0.0%)</source>
        <translation>UDP 文件重新请求：0，失败：0 (0.0%)</translation>
    </message>
    <message>
        <location line="+1999"/>
        <source>Corrupt (Failed yEnc Check): %1</source>
        <translation>损坏（yEnc 校验失败）：%1</translation>
    </message>
    <message>
        <location line="+363"/>
        <source>HTTP Cache</source>
        <translation>HTTP 缓存</translation>
    </message>
    <message>
        <source>Published: 0 Bytes</source>
        <translation type="vanished">已发布: 0 Bytes</translation>
    </message>
    <message>
        <source>Fetched: 0 Bytes</source>
        <translation type="vanished">已获取: 0 Bytes</translation>
    </message>
    <message>
        <source>Upload Saved: 0 Bytes</source>
        <translation type="vanished">节省的上传: 0 Bytes</translation>
    </message>
    <message>
        <source>Chunks Published: 0</source>
        <translation type="vanished">已发布块: 0</translation>
    </message>
    <message>
        <source>Chunks Fetched: 0</source>
        <translation type="vanished">已获取块: 0</translation>
    </message>
    <message>
        <location line="-2230"/>
        <source>Run Time: 0:00:00</source>
        <translation>运行时间：0:00:00</translation>
    </message>
    <message>
        <location line="-4"/>
        <location line="+8"/>
        <source>Total Server Duration: 0:00:00</source>
        <translation>服务器总时长：0:00:00</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Known Clients: 0</source>
        <translation>已知客户端：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Client Software</source>
        <translation>客户端软件</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Low ID: 0 (0.0%)</source>
        <translation>Low ID：0 (0.0%)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Banned Clients: 0</source>
        <translation>已封禁客户端：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Filtered Clients: 0</source>
        <translation>已过滤客户端：0</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Servers</source>
        <translation>服务器</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Working Servers: 0</source>
        <translation>工作中的服务器：0</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Failed Servers: 0</source>
        <translation>失败的服务器：0</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Total: 0</source>
        <translation>总计：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Users: 0</source>
        <translation>总用户：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Files: 0</source>
        <translation>总文件：0</translation>
    </message>
    <message>
        <source>Low ID Users: 0</source>
        <translation type="vanished">Low ID 用户：0</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+14"/>
        <source>Records</source>
        <translation>纪录</translation>
    </message>
    <message>
        <location line="-12"/>
        <source>Most Working Servers: 0</source>
        <translation>最多可用服务器：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Most Users Online: 0</source>
        <translation>最多在线用户：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Most Files Available: 0</source>
        <translation>最多可用文件：0</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Shared Files</source>
        <translation>共享文件</translation>
    </message>
    <message>
        <source>Number of Shared Files: 0</source>
        <translation type="vanished">共享文件数：0</translation>
    </message>
    <message>
        <source>Total Size: 0 Bytes</source>
        <translation type="vanished">总大小：0 Bytes</translation>
    </message>
    <message>
        <source>Average File Size: 0 Bytes</source>
        <translation type="vanished">平均文件大小：0 Bytes</translation>
    </message>
    <message>
        <source>Largest Shared File: 0 Bytes</source>
        <translation type="vanished">最大共享文件：0 Bytes</translation>
    </message>
    <message>
        <source>Most Files Shared: 0</source>
        <translation type="vanished">最多共享文件：0</translation>
    </message>
    <message>
        <source>Largest Share Size: 0 Bytes</source>
        <translation type="vanished">最大共享容量：0 Bytes</translation>
    </message>
    <message>
        <source>Largest Average File Size: 0 Bytes</source>
        <translation type="vanished">最大平均文件大小：0 Bytes</translation>
    </message>
    <message>
        <source>Largest File Size: 0 Bytes</source>
        <translation type="vanished">最大文件大小：0 Bytes</translation>
    </message>
    <message>
        <source>Total Downloads</source>
        <translation type="vanished">下载总计</translation>
    </message>
    <message>
        <source>Number of Downloads: 0</source>
        <translation type="vanished">下载数量：0</translation>
    </message>
    <message>
        <source>Total Size of Downloads: 0 Bytes</source>
        <translation type="vanished">下载总大小：0 Bytes</translation>
    </message>
    <message>
        <source>Total Size Downloaded: 0 Bytes</source>
        <translation type="vanished">已下载总大小：0 Bytes</translation>
    </message>
    <message>
        <source>Total Size Left to Download: 0 Bytes</source>
        <translation type="vanished">剩余下载大小：0 Bytes</translation>
    </message>
    <message>
        <source>Free Space on Drive: 0 Bytes</source>
        <translation type="vanished">磁盘可用空间：0 Bytes</translation>
    </message>
    <message>
        <location line="-297"/>
        <location line="+668"/>
        <source>Session UL:DL Ratio: %1</source>
        <translation>会话上传:下载比率：%1</translation>
    </message>
    <message>
        <source>Friend Session UL:DL Ratio: %1</source>
        <translation type="vanished">好友会话上传:下载比率：%1</translation>
    </message>
    <message>
        <location line="-665"/>
        <location line="+669"/>
        <source>Cumulative UL:DL Ratio: %1</source>
        <translation>累计上传:下载比率：%1</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+74"/>
        <location line="+1197"/>
        <source>Uploaded Data: %1</source>
        <translation>已上传数据：%1</translation>
    </message>
    <message>
        <location line="-1260"/>
        <location line="+73"/>
        <location line="+57"/>
        <location line="+99"/>
        <source>Default Port 4662: %1 %2</source>
        <translation>默认端口 4662：%1 %2</translation>
    </message>
    <message>
        <location line="-226"/>
        <location line="+73"/>
        <location line="+57"/>
        <location line="+99"/>
        <source>Other Ports: %1 %2</source>
        <translation>其他端口：%1 %2</translation>
    </message>
    <message>
        <location line="-226"/>
        <location line="+73"/>
        <source>Complete File: %1 %2</source>
        <translation>完整文件：%1 %2</translation>
    </message>
    <message>
        <location line="-70"/>
        <location line="+73"/>
        <source>Part File: %1 %2</source>
        <translation>分块文件：%1 %2</translation>
    </message>
    <message>
        <location line="-68"/>
        <source>Uploaded Data to Friends: %1</source>
        <translation>上传给好友的数据：%1</translation>
    </message>
    <message>
        <source>Active Uploads: %1</source>
        <translation type="vanished">活跃上传：%1</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Waiting Uploads: %1</source>
        <translation>等待上传：%1</translation>
    </message>
    <message>
        <source>Successful: %1%2</source>
        <translation type="vanished">成功：%1%2</translation>
    </message>
    <message>
        <location line="+1405"/>
        <location line="+49"/>
        <source>Failed: %1</source>
        <translation>失败：%1</translation>
    </message>
    <message>
        <location line="-1430"/>
        <location line="+45"/>
        <source>Average Upload Per Session: %1</source>
        <translation>每次会话平均上传：%1</translation>
    </message>
    <message>
        <location line="-42"/>
        <location line="+45"/>
        <source>Average Upload Time: %1</source>
        <translation>平均上传时间：%1</translation>
    </message>
    <message>
        <location line="-41"/>
        <source>%1: %2</source>
        <translation>%1：%2</translation>
    </message>
    <message>
        <location line="-186"/>
        <location line="+190"/>
        <location line="+57"/>
        <location line="+99"/>
        <location line="+36"/>
        <source>Total Overhead (Packets)</source>
        <translation>总开销（数据包）</translation>
    </message>
    <message>
        <location line="-381"/>
        <location line="+190"/>
        <location line="+57"/>
        <location line="+99"/>
        <location line="+36"/>
        <source>File Request Overhead (Packets)</source>
        <translation>文件请求开销（数据包）</translation>
    </message>
    <message>
        <location line="-381"/>
        <location line="+190"/>
        <location line="+57"/>
        <location line="+99"/>
        <location line="+36"/>
        <source>Source Exchange Overhead (Packets)</source>
        <translation>来源交换开销（数据包）</translation>
    </message>
    <message>
        <location line="-381"/>
        <location line="+190"/>
        <location line="+57"/>
        <location line="+99"/>
        <location line="+36"/>
        <source>Server Overhead (Packets)</source>
        <translation>服务器开销（数据包）</translation>
    </message>
    <message>
        <location line="-381"/>
        <location line="+190"/>
        <location line="+57"/>
        <location line="+99"/>
        <location line="+36"/>
        <source>Kad Overhead (Packets)</source>
        <translation>Kad 开销（数据包）</translation>
    </message>
    <message>
        <source>Published</source>
        <translation type="vanished">已发布</translation>
    </message>
    <message>
        <source>Fetched</source>
        <translation type="vanished">已获取</translation>
    </message>
    <message>
        <source>Upload Saved</source>
        <translation type="vanished">节省的上传</translation>
    </message>
    <message>
        <source>Chunks Published</source>
        <translation type="vanished">已发布块</translation>
    </message>
    <message>
        <source>Chunks Fetched</source>
        <translation type="vanished">已获取块</translation>
    </message>
    <message>
        <location line="-132"/>
        <location line="+99"/>
        <location line="+1040"/>
        <location line="+141"/>
        <source>Downloaded Data: %1</source>
        <translation>已下载数据：%1</translation>
    </message>
    <message>
        <source>Active Downloads: %1</source>
        <translation type="vanished">活跃下载：%1</translation>
    </message>
    <message>
        <location line="-1259"/>
        <source>Found Sources: %1</source>
        <translation>找到的来源: %1</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>UDP File Re-asks: %1, Failed: %2 %3</source>
        <translation>UDP 文件重新请求：%1，失败：%2 %3</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+61"/>
        <location line="+1034"/>
        <source>Completed Downloads: %1</source>
        <translation>已完成下载：%1</translation>
    </message>
    <message>
        <location line="-1065"/>
        <location line="+36"/>
        <source>Gain Due To Compression: %1 %2</source>
        <translation>压缩带来的收益：%1 %2</translation>
    </message>
    <message>
        <location line="-34"/>
        <location line="+36"/>
        <source>Lost Due To Corruption: %1 %2</source>
        <translation>损坏造成的损失：%1 %2</translation>
    </message>
    <message>
        <location line="-34"/>
        <location line="+36"/>
        <source>Parts Saved Due To ICH: %1</source>
        <translation>通过 ICH 挽救的分块：%1</translation>
    </message>
    <message>
        <location line="+1152"/>
        <source>Active Connections: %1</source>
        <translation>活跃连接：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Peak Connections: %1</source>
        <translation>峰值连接：%1</translation>
    </message>
    <message>
        <source>Max Connections Limit Reached: %1</source>
        <translation type="vanished">已达到最大连接限制：%1</translation>
    </message>
    <message>
        <location line="-1920"/>
        <location line="+26"/>
        <location line="+756"/>
        <location line="+33"/>
        <source>Reconnects: %1</source>
        <translation>重连次数：%1</translation>
    </message>
    <message>
        <source>Average Connections: %1</source>
        <translation type="vanished">平均连接：%1</translation>
    </message>
    <message>
        <location line="-9"/>
        <location line="+962"/>
        <location line="+82"/>
        <source>Upload Speed: %1</source>
        <translation>上传速度：%1</translation>
    </message>
    <message>
        <location line="-1043"/>
        <location line="+17"/>
        <location line="+948"/>
        <source>Max Upload Rate: %1</source>
        <translation>最大上传速率：%1</translation>
    </message>
    <message>
        <location line="-964"/>
        <location line="+17"/>
        <source>Max Average Upload Rate: %1</source>
        <translation>最大平均上传速率：%1</translation>
    </message>
    <message>
        <location line="-16"/>
        <location line="+958"/>
        <location line="+82"/>
        <location line="+65"/>
        <source>Download Speed: %1</source>
        <translation>下载速度：%1</translation>
    </message>
    <message>
        <location line="-1104"/>
        <location line="+17"/>
        <location line="+943"/>
        <location line="+146"/>
        <source>Max Download Rate: %1</source>
        <translation>最大下载速率：%1</translation>
    </message>
    <message>
        <location line="-1105"/>
        <location line="+17"/>
        <source>Max Average Download Rate: %1</source>
        <translation>最大平均下载速率：%1</translation>
    </message>
    <message>
        <source>Server Reconnects: %1</source>
        <translation type="vanished">服务器重连次数：%1</translation>
    </message>
    <message>
        <source>Connection Limit Reached: %1</source>
        <translation type="vanished">达到连接限制：%1</translation>
    </message>
    <message>
        <location line="-5"/>
        <location line="+948"/>
        <source>Average Upload Rate: %1</source>
        <translation>平均上传速率：%1</translation>
    </message>
    <message>
        <location line="-945"/>
        <location line="+943"/>
        <location line="+146"/>
        <source>Average Download Rate: %1</source>
        <translation>平均下载速率：%1</translation>
    </message>
    <message>
        <location line="-1076"/>
        <location line="+5"/>
        <source>Time Since Last Reset: %1</source>
        <translation>自上次重置以来的时间：%1</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Runtime: %1</source>
        <translation>运行时间：%1</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+20"/>
        <source>Transfer Time: %1 %2</source>
        <translation>传输时间：%1 %2</translation>
    </message>
    <message>
        <location line="-18"/>
        <location line="+20"/>
        <location line="+890"/>
        <source>Upload Time: %1 %2</source>
        <translation>上传时间：%1 %2</translation>
    </message>
    <message>
        <location line="-908"/>
        <location line="+20"/>
        <location line="+887"/>
        <location line="+144"/>
        <source>Download Time: %1 %2</source>
        <translation>下载时间：%1 %2</translation>
    </message>
    <message>
        <source>Server Duration: %1 %2</source>
        <translation type="vanished">服务器持续时间：%1 %2</translation>
    </message>
    <message>
        <location line="-1037"/>
        <source>Run Time: %1</source>
        <translation>运行时间：%1</translation>
    </message>
    <message>
        <location line="-9"/>
        <location line="+17"/>
        <source>Total Server Duration: %1 %2</source>
        <translation>服务器总时长：%1 %2</translation>
    </message>
    <message>
        <location line="-829"/>
        <source>Current Server Duration: 0:00:00</source>
        <translation>当前服务器持续时间：0:00:00</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Different clients, counted by user hash. An estimate, accurate to about 2%.</source>
        <translation>按用户哈希统计的不同客户端。估计值，误差约 2%。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Clients that said hello on a connection.</source>
        <translation>通过连接发来 hello 的客户端。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Clients that proved their user hash with Secure Identification.</source>
        <translation>通过安全身份验证证明了其用户哈希的客户端。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Clients by the country of the address they connected from.</source>
        <translation>按连接来源地址所属国家划分的客户端。</translation>
    </message>
    <message>
        <location line="+602"/>
        <location line="+10"/>
        <source>Average Download Per Session: %1</source>
        <translation>每次会话平均下载：%1</translation>
    </message>
    <message>
        <location line="-8"/>
        <location line="+10"/>
        <source>Average Download Time: %1</source>
        <translation>平均下载时间：%1</translation>
    </message>
    <message>
        <location line="+148"/>
        <source>Current Server Duration: %1 %2</source>
        <translation>当前服务器持续时间：%1 %2</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Known Clients: %1</source>
        <translation>已知客户端：%1</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Low ID: %1 %2</source>
        <translation>Low ID：%1 %2</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>Banned Clients: %1</source>
        <translation>已封禁客户端：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Filtered Clients: %1</source>
        <translation>已过滤客户端：%1</translation>
    </message>
    <message>
        <location line="+127"/>
        <source>Working Servers: %1</source>
        <translation>工作中的服务器：%1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Failed Servers: %1</source>
        <translation>失败的服务器：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Total: %1</source>
        <translation>总计：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Users: %1</source>
        <translation>总用户：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Files: %1</source>
        <translation>总文件：%1</translation>
    </message>
    <message>
        <source>Low ID Users: %1</source>
        <translation type="vanished">Low ID 用户：%1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Most Working Servers: %1</source>
        <translation>最多可用服务器：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Most Users Online: %1</source>
        <translation>最多在线用户：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Most Files Available: %1</source>
        <translation>最多可用文件：%1</translation>
    </message>
    <message>
        <location line="-948"/>
        <location line="+956"/>
        <source>Number of Shared Files: %1</source>
        <translation>共享文件数：%1</translation>
    </message>
    <message>
        <source>Total Size: %1</source>
        <translation type="vanished">总大小：%1</translation>
    </message>
    <message>
        <source>Average File Size: %1</source>
        <translation type="vanished">平均文件大小：%1</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+9"/>
        <source>Largest Shared File: %1</source>
        <translation>最大共享文件：%1</translation>
    </message>
    <message>
        <source>Most Files Shared: %1</source>
        <translation type="vanished">最多共享文件：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Largest Share Size: %1</source>
        <translation>最大共享容量：%1</translation>
    </message>
    <message>
        <location line="-4"/>
        <source>Largest Average File Size: %1</source>
        <translation>最大平均文件大小：%1</translation>
    </message>
    <message>
        <source>Largest File Size: %1</source>
        <translation type="vanished">最大文件大小：%1</translation>
    </message>
    <message>
        <location line="+14"/>
        <location line="+871"/>
        <source>Number of Downloads: %1</source>
        <translation>下载数量：%1</translation>
    </message>
    <message>
        <location line="-870"/>
        <location line="+689"/>
        <location line="+189"/>
        <source>Total Size of Downloads: %1</source>
        <translation>下载总大小：%1</translation>
    </message>
    <message>
        <location line="-188"/>
        <location line="+189"/>
        <source>Total Size Downloaded: %1</source>
        <translation>已下载总大小：%1</translation>
    </message>
    <message>
        <location line="-188"/>
        <location line="+189"/>
        <source>Total Size Left to Download: %1</source>
        <translation>剩余下载大小：%1</translation>
    </message>
    <message>
        <source>Free Space on Drive: %1</source>
        <translation type="vanished">磁盘可用空间：%1</translation>
    </message>
    <message>
        <location line="-696"/>
        <source>Clients Seen: ≈%1</source>
        <translation>见过的客户端: ≈%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Identified: ≈%1 %2</source>
        <translation>已识别: ≈%1 %2</translation>
    </message>
    <message>
        <location line="+51"/>
        <source>Not running</source>
        <translation>未运行</translation>
    </message>
    <message>
        <location line="-690"/>
        <location line="+692"/>
        <source>Connecting</source>
        <translation>连接中</translation>
    </message>
    <message>
        <location line="-1713"/>
        <location line="+111"/>
        <source>Average (%1 mins)</source>
        <translation>平均(%1 分钟)</translation>
    </message>
    <message>
        <location line="-86"/>
        <location line="+107"/>
        <source>Active connections (1:%1)</source>
        <translation>活跃连接 (1:%1)</translation>
    </message>
    <message>
        <location line="-15"/>
        <source>Download Speed</source>
        <translation>下载速度</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Friend upload</source>
        <translation>好友上传</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Upload Speed</source>
        <translation>上传速度</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Connections</source>
        <translation>连接数</translation>
    </message>
    <message>
        <location line="+364"/>
        <source>Disk Space</source>
        <translation>磁盘空间</translation>
    </message>
    <message>
        <location line="+217"/>
        <source>Projected Averages</source>
        <translation>预计平均值</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Daily</source>
        <translation>每天</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Monthly</source>
        <translation>每月</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Yearly</source>
        <translation>每年</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Default Port 4662</source>
        <translation>默认端口 4662</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Other Ports</source>
        <translation>其他端口</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Uploaded Data</source>
        <translation>已上传数据</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Complete File</source>
        <translation>完整文件</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Part File</source>
        <translation>部分文件</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+13"/>
        <source>Successful</source>
        <translation>成功</translation>
    </message>
    <message>
        <location line="-12"/>
        <location line="+13"/>
        <source>Failed</source>
        <translation>失败</translation>
    </message>
    <message>
        <location line="-6"/>
        <source>Downloaded Data</source>
        <translation>已下载数据</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Completed Downloads</source>
        <translation>已完成的下载</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Gained Due To Compression</source>
        <translation>压缩节省</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Lost Due To Corruption</source>
        <translation>损坏损失</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Parts Saved Due To ICH</source>
        <translation>ICH 挽救的分块</translation>
    </message>
    <message>
        <location line="+96"/>
        <source>Active Uploads/Needed to fill Bandwidth: %1</source>
        <translation>活动上传/填满带宽所需:%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Total Uploads: %1</source>
        <translation>上传总数:%1</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Total successful upload sessions</source>
        <translation>成功的上传会话总数</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total failed upload sessions</source>
        <translation>失败的上传会话总数</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Successful Download Sessions</source>
        <translation>成功的下载会话</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Failed Download Sessions</source>
        <translation>失败的下载会话</translation>
    </message>
    <message>
        <location line="+99"/>
        <source>Active Downloads (chunks): %1</source>
        <translation>活动下载(分块):%1</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>On Queue</source>
        <translation>排队中</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Queue Full</source>
        <translation>队列已满</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>No needed parts</source>
        <translation>没有需要的分块</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Asking</source>
        <translation>正在询问</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Receiving hashset</source>
        <translation>正在接收哈希集</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Connecting via server</source>
        <translation>正在通过服务器连接</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Too many connections</source>
        <translation>连接过多</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Cannot connect LowID to LowID</source>
        <translation>LowID 无法连接 LowID</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+261"/>
        <source>Problematic</source>
        <translation>有问题</translation>
    </message>
    <message>
        <location line="-261"/>
        <source>Banned</source>
        <translation>已封禁</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Asked for another file</source>
        <translation>已请求其他文件</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>via eD2K Server</source>
        <translation>通过 eD2K 服务器</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>via Kad</source>
        <translation>通过 Kad</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>via Source Exchange</source>
        <translation>通过来源交换</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>via Passive</source>
        <translation>通过被动方式</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Dead Sources: %1 (%2 + %3)</source>
        <translation>失效来源：%1 (%2 + %3)</translation>
    </message>
    <message>
        <location line="+100"/>
        <source>Active Connections (estimate): %1 (Half:%2 | Compl:%3 | Other:%4)</source>
        <translation>活动连接(估计):%1(半开:%2 | 完成:%3 | 其他:%4)</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+30"/>
        <source>Average Connections (estimate): %1</source>
        <translation>平均连接数(估计):%1</translation>
    </message>
    <message>
        <location line="-28"/>
        <location line="+30"/>
        <source>Peak Connections (estimate): %1</source>
        <translation>连接峰值(估计):%1</translation>
    </message>
    <message>
        <location line="-24"/>
        <source>Max Connection Limit Reached: %1 : %2</source>
        <translation>达到最大连接限制:%1 : %2</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+22"/>
        <source>Max Connection Limit Reached: %1</source>
        <translation>达到最大连接限制:%1</translation>
    </message>
    <message>
        <location line="-19"/>
        <source>Average Uploadrate: %1</source>
        <translation>平均上传速率:%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Average Downloadrate: %1</source>
        <translation>平均下载速率:%1</translation>
    </message>
    <message>
        <location line="+116"/>
        <source>Default</source>
        <translation>默认</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Other</source>
        <translation>其他</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Secure Ident (OK : Failed )</source>
        <translation>安全认证(成功 : 失败 )</translation>
    </message>
    <message>
        <location line="+93"/>
        <source>Minor</source>
        <translation>次要</translation>
    </message>
    <message>
        <location line="+44"/>
        <source>Users on Working Servers: %1; Low ID: %2 (%3%)</source>
        <translation>可用服务器上的用户:%1;低 ID:%2 (%3%)</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Files on Working Servers: %1</source>
        <translation>可用服务器上的文件:%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server Occupation: %1%</source>
        <translation>服务器占用率:%1%</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Deleted Servers: %1</source>
        <translation>已删除的服务器:%1</translation>
    </message>
    <message>
        <location line="+20"/>
        <source> (%1 hashing)</source>
        <translation>(%1 个正在哈希)</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Average file size: %1</source>
        <translation>平均文件大小:%1</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Total size of Shared Files: %1</source>
        <translation>共享文件总大小:%1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Max. Files Ever Shared: %1</source>
        <translation>历史最多共享文件数:%1</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Total Completed Size: %1 (%2%)</source>
        <translation>已完成总大小:%1 (%2%)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Total Size Left to Transfer: %1</source>
        <translation>剩余待传输总大小:%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Free Space on Tempdrive: %1</source>
        <translation>临时驱动器可用空间:%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source> (you need to free %1!)</source>
        <translation>(需要释放 %1!)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Additional Space Needed for Downloads: %1</source>
        <translation>下载所需的额外空间:%1</translation>
    </message>
    <message>
        <location line="+102"/>
        <source>Status: Running on UDP port %1</source>
        <translation>状态：正在 UDP 端口 %1 上运行</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status: Not running</source>
        <translation>状态：未运行</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Node ID: %1</source>
        <translation>节点 ID：%1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Open</source>
        <translation>开放</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Cone NAT</source>
        <translation>锥形 NAT</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Symmetric NAT</source>
        <translation>对称型 NAT</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Address Class: IPv4 %1, IPv6 %2</source>
        <translation>地址类型：IPv4 %1，IPv6 %2</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Router: %1</source>
        <translation>路由器：%1</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Yes</source>
        <translation>是</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>No</source>
        <translation>否</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>%1.%2</source>
        <translation>%1.%2</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>In use</source>
        <translation>使用中</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Not configured</source>
        <translation>未配置</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Off</source>
        <translation>关闭</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Credit Address: %1</source>
        <translation>信用地址：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Balance: %1</source>
        <translation>余额：%1</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>%1 credits</source>
        <translation>%1 信用</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Account: %1</source>
        <translation>账户：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Overdrawn</source>
        <translation>已透支</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Mature</source>
        <translation>已成熟</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not mature</source>
        <translation>未成熟</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Receipts Signed: %1 (%2)</source>
        <translation>已签名收据：%1（%2）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Receipts Received: %1 (%2), priority turns given: %3</source>
        <translation>已收到收据：%1（%2），给予的优先轮次：%3</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Stake: %1, claims waiting: %2</source>
        <translation>质押：%1，等待中的领取：%2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1 E3S per credit</source>
        <translation>每个信用 %1 E3S</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>burning off</source>
        <translation>销毁已关闭</translation>
    </message>
    <message>
        <location line="+75"/>
        <source>Connected (LAN mode)</source>
        <translation>已连接 (局域网模式)</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Connected, TCP and UDP firewalled</source>
        <translation>已连接，TCP 和 UDP 受防火墙限制</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Connected, TCP firewalled</source>
        <translation>已连接，TCP 受防火墙限制</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Connected, UDP firewalled</source>
        <translation>已连接，UDP 受防火墙限制</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Connected, open</source>
        <translation>已连接，开放</translation>
    </message>
    <message>
        <location line="-95"/>
        <location line="+97"/>
        <source>Status: %1</source>
        <translation>状态: %1</translation>
    </message>
    <message>
        <location line="+25"/>
        <location line="+49"/>
        <source>Reset Statistics</source>
        <translation>重置统计</translation>
    </message>
    <message>
        <location line="-48"/>
        <location line="+68"/>
        <source>Restore Statistics</source>
        <translation>恢复统计</translation>
    </message>
    <message>
        <location line="-60"/>
        <source>Expand Main Sections</source>
        <translation>展开主要部分</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Expand All Sections</source>
        <translation>展开所有部分</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Collapse All Sections</source>
        <translation>折叠所有部分</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+9"/>
        <source>Copy Branch</source>
        <translation>复制分支</translation>
    </message>
    <message>
        <location line="-8"/>
        <location line="+9"/>
        <source>Copy All Visible</source>
        <translation>复制所有可见</translation>
    </message>
    <message>
        <location line="-8"/>
        <location line="+9"/>
        <source>Copy All Statistics</source>
        <translation>复制所有统计</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>HTML Features</source>
        <translation>HTML 功能</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+80"/>
        <location line="+7"/>
        <source>Export Statistics...</source>
        <translation>导出统计...</translation>
    </message>
    <message>
        <location line="-68"/>
        <source>Are you sure you wish to reset your cumulative statistics?

If you change your mind, you can reverse this action by clicking the &apos;Restore Stats&apos; button.</source>
        <translation>确定要重置累计统计吗？

如果改变主意，可以单击“恢复统计”按钮撤销此操作。</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Are you sure you wish to restore your cumulative statistics from the backup file?

Clicking &apos;Restore Stats&apos; again will reload your current statistics.</source>
        <translation>确定要从备份文件恢复累计统计吗？

再次单击“恢复统计”将重新载入当前统计。</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>HTML Files (*.html);;All Files (*)</source>
        <translation>HTML 文件 (*.html);;所有文件 (*)</translation>
    </message>
    <message>
        <location line="+53"/>
        <location line="+49"/>
        <source>Statistics</source>
        <translation>统计</translation>
    </message>
    <message>
        <location line="-7"/>
        <source>Name:</source>
        <translation>名称：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>eMule Qt v%1 %2 [%3]</source>
        <translation>eMule Qt v%1 %2 [%3]</translation>
    </message>
    <message>
        <location line="+166"/>
        <source>Connected Peers: %1</source>
        <translation>已连接的节点: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Peak Peers: %1</source>
        <translation>节点峰值: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>DHT Nodes: %1</source>
        <translation>DHT 节点: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+77"/>
        <source>Traffic</source>
        <translation>流量</translation>
    </message>
    <message>
        <location line="-74"/>
        <location line="+75"/>
        <source>Network Traffic Received: %1</source>
        <translation>接收的网络流量: %1</translation>
    </message>
    <message>
        <location line="-73"/>
        <location line="+76"/>
        <source>Network Traffic Sent: %1</source>
        <translation>发送的网络流量: %1</translation>
    </message>
    <message>
        <location line="-74"/>
        <source>Redundant Data: %1</source>
        <translation>冗余数据: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Corrupted Data: %1</source>
        <translation>损坏的数据: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Pieces Failed Hash Check: %1</source>
        <translation>哈希校验失败的分块: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+42"/>
        <source>Torrents</source>
        <translation>种子</translation>
    </message>
    <message>
        <location line="-41"/>
        <source>Added: %1</source>
        <translation>已添加: %1</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Number of Torrents: %1</source>
        <translation>种子数: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Seeding: %1</source>
        <translation>做种中: %1</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Error: %1</source>
        <translation>错误：%1</translation>
    </message>
    <message>
        <location line="-2047"/>
        <location line="+11"/>
        <location line="+2047"/>
        <source>BitTorrent</source>
        <translation>BitTorrent</translation>
    </message>
    <message>
        <location line="+99"/>
        <location line="+507"/>
        <source>Open Connections: %1</source>
        <translation>打开的连接：%1</translation>
    </message>
    <message>
        <location line="-503"/>
        <source>Network Traffic: %1</source>
        <translation>网络流量：%1</translation>
    </message>
    <message>
        <location line="-139"/>
        <location line="+2"/>
        <location line="+138"/>
        <source>Overhead: %1 %2</source>
        <translation>开销：%1 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Articles</source>
        <translation>文章</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloaded: %1</source>
        <translation>已下载：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not Found on a Server: %1</source>
        <translation>在某个服务器上未找到：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Missing on All Servers: %1</source>
        <translation>在所有服务器上都缺失：%1</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+502"/>
        <source>Connection Errors: %1</source>
        <translation>连接错误：%1</translation>
    </message>
    <message>
        <location line="-499"/>
        <source>Completed Downloads: %1 %2</source>
        <translation>已完成下载：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Completed Data: %1</source>
        <translation>已完成数据：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Failed Downloads: %1 %2</source>
        <translation>失败的下载：%1 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Post-Processing</source>
        <translation>后期处理</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>PAR2 Verified: %1</source>
        <translation>PAR2 已校验：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Repaired: %1 %2</source>
        <translation>已修复：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Repair Failed: %1 %2</source>
        <translation>修复失败：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Blocks Repaired: %1</source>
        <translation>已修复块：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Recovery Volumes Fetched: %1</source>
        <translation>已获取恢复卷：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Recovery Data: %1</source>
        <translation>恢复数据：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unpacked: %1</source>
        <translation>已解压：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Password Required: %1</source>
        <translation>需要密码：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Sets Unpacked While Downloading: %1</source>
        <translation>边下载边解压的集数：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Time Spent: %1</source>
        <translation>耗时：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Verifying: %1 %2</source>
        <translation>正在校验：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Repairing: %1 %2</source>
        <translation>正在修复：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unpacking: %1 %2</source>
        <translation>正在解压：%1 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Health Checks</source>
        <translation>健康度检查</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Checks Run: %1</source>
        <translation>已执行检查：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Passed: %1 %2</source>
        <translation>通过：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paused as Incomplete: %1 %2</source>
        <translation>因不完整而暂停：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Inconclusive: %1 %2</source>
        <translation>无法确定：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Articles Probed: %1</source>
        <translation>已探测文章：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Intake</source>
        <translation>添加来源</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>NZBs Added: %1</source>
        <translation>已添加 NZB：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Files: %1 %2</source>
        <translation>文件：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>URLs: %1 %2</source>
        <translation>URL：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Watch Folder: %1 %2</source>
        <translation>监视文件夹：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Feeds: %1 %2</source>
        <translation>订阅源：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Indexer Searches: %1 %2</source>
        <translation>索引器搜索：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Duplicates: %1</source>
        <translation>重复项：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Already Downloaded: %1</source>
        <translation>已下载过：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Invalid NZBs: %1</source>
        <translation>无效 NZB：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Indexers</source>
        <translation>索引器</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+138"/>
        <source>Searches: %1</source>
        <translation>搜索次数：%1</translation>
    </message>
    <message>
        <location line="-137"/>
        <source>API Requests: %1</source>
        <translation>API 请求：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Errors: %1 %2</source>
        <translation>错误：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>NZBs Fetched: %1</source>
        <translation>已获取 NZB：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+298"/>
        <source>Failed: %1 %2</source>
        <translation>失败：%1 %2</translation>
    </message>
    <message>
        <location line="-297"/>
        <source>Feed Polls: %1</source>
        <translation>订阅源轮询：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Feed Matches: %1</source>
        <translation>订阅源匹配：%1</translation>
    </message>
    <message>
        <location line="-177"/>
        <location line="+188"/>
        <source>Downloading: %1</source>
        <translation>正在下载：%1</translation>
    </message>
    <message>
        <location line="-186"/>
        <location line="+187"/>
        <source>Queued: %1</source>
        <translation>排队中：%1</translation>
    </message>
    <message>
        <location line="-186"/>
        <location line="+187"/>
        <source>Paused: %1</source>
        <translation>已暂停：%1</translation>
    </message>
    <message>
        <location line="-186"/>
        <location line="+187"/>
        <source>Checking: %1</source>
        <translation>正在检查：%1</translation>
    </message>
    <message>
        <location line="-143"/>
        <source>Connections: %1</source>
        <translation>连接数：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Kad Contacts: %1</source>
        <translation>Kad 联系人：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Banned Nodes: %1</source>
        <translation>已封禁节点：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Banned Hosts: %1</source>
        <translation>已封禁主机：%1</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+3"/>
        <source>File Data: %1 %2</source>
        <translation>文件数据：%1 %2</translation>
    </message>
    <message>
        <location line="-2"/>
        <location line="+3"/>
        <source>Other: %1 %2</source>
        <translation>其他：%1 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Kad</source>
        <translation>Kad</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Lookups Running: %1</source>
        <translation>进行中的查找：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Records Stored: %1</source>
        <translation>已存储记录：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Size of the Records: %1</source>
        <translation>记录大小：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Records Published: %1</source>
        <translation>已发布记录：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Upload Queue</source>
        <translation>上传队列</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload Slots: %1</source>
        <translation>上传位：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Uploading: %1</source>
        <translation>上传中：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Waiting: %1</source>
        <translation>等待中：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Queued Requests: %1</source>
        <translation>排队的请求：%1</translation>
    </message>
    <message>
        <location line="-2112"/>
        <location line="+11"/>
        <location line="+2108"/>
        <source>ed3k</source>
        <translation>ed3k</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Credits</source>
        <translation>信用</translation>
    </message>
    <message>
        <location line="+102"/>
        <source>Post-Processing: %1</source>
        <translation>后期处理：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Completed: %1</source>
        <translation>已完成：%1</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>News Servers</source>
        <translation>新闻服务器</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Queue</source>
        <translation>队列</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Contacts: %1</source>
        <translation>联系人: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+8"/>
        <source>IP Verified: %1 %2</source>
        <translation>IP 已验证: %1 %2</translation>
    </message>
    <message>
        <location line="-7"/>
        <source>Type 0, Alive over 2 Hours: %1 %2</source>
        <translation>类型 0，活跃超过 2 小时: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type 1, Alive 1 to 2 Hours: %1 %2</source>
        <translation>类型 1，活跃 1 至 2 小时: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type 2, Alive under 1 Hour: %1 %2</source>
        <translation>类型 2，活跃不足 1 小时: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type 3, New or Not Answering: %1 %2</source>
        <translation>类型 3，新联系人或无应答: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type 4, Dead: %1 %2</source>
        <translation>类型 4，已失效: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Most Contacts: %1</source>
        <translation>最多联系人: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Contacts Added: %1</source>
        <translation>已添加的联系人: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Replaced a Weaker Contact: %1 %2</source>
        <translation>替换了较弱的联系人: %1 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Contacts Expired: %1</source>
        <translation>已过期的联系人: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Contacts Banned: %1</source>
        <translation>已封禁的联系人: %1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Nodes Seen: ≈%1</source>
        <translation>见过的节点: ≈%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nodes Heard Of: ≈%1</source>
        <translation>听说过的节点: ≈%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Firewalled (Kad)</source>
        <translation>受防火墙限制 (Kad)</translation>
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
        <translation>收到的来源发布: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Open: %1 %2</source>
        <translation>开放: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Firewalled, with Buddy: %1 %2</source>
        <translation>受防火墙限制，有伙伴: %1 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Firewalled, Direct Callback: %1 %2</source>
        <translation>受防火墙限制，直接回调: %1 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Firewalled, without Buddy: %1 %2</source>
        <translation>受防火墙限制，无伙伴: %1 %2</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Estimated Users: %1</source>
        <translation>估计用户数: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Estimated Files: %1</source>
        <translation>估计文件数: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Indexed Keywords: %1</source>
        <translation>已索引的关键词: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Indexed Sources: %1</source>
        <translation>已索引的来源: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Indexed Notes: %1</source>
        <translation>已索引的注释: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Active Searches: %1</source>
        <translation>活动搜索: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Banned Addresses: %1</source>
        <translation>已封禁的地址: %1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Time Connected: %1 %2</source>
        <translation>连接时间: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hellos Sent: %1</source>
        <translation>已发送的 Hello: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hellos Answered: %1 %2</source>
        <translation>得到应答的 Hello: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Lookup Answers: %1</source>
        <translation>查找应答: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bootstrap Answers: %1</source>
        <translation>引导应答: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Node Lookups: %1 %2</source>
        <translation>节点查找: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Keyword Searches: %1 %2</source>
        <translation>关键词搜索: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Source Searches: %1 %2</source>
        <translation>来源搜索: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Notes Searches: %1 %2</source>
        <translation>注释搜索: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Publishes: %1</source>
        <translation>发布: %1</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Kademlia</source>
        <translation>Kademlia</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Different nodes, counted by node ID. An estimate, accurate to about 2%.</source>
        <translation>按节点 ID 统计的不同节点数。估计值，误差约 2%。</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Routing Table</source>
        <translation>路由表</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>By Version</source>
        <translation>按版本</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Nodes</source>
        <translation>节点</translation>
    </message>
    <message>
        <location line="-2041"/>
        <location line="+2044"/>
        <source>By Country</source>
        <translation>按国家</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nodes that sent us a packet, by the country of the address it came from.</source>
        <translation>向我们发送过数据包的节点，按来源地址所在国家统计。</translation>
    </message>
    <message>
        <location line="-2071"/>
        <location line="+2075"/>
        <source>Network</source>
        <translation>网络</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Activity</source>
        <translation>活动</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Nodes that sent us a packet themselves.</source>
        <translation>亲自向我们发送过数据包的节点。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Nodes that other nodes named in their answers; most are never contacted.</source>
        <translation>其他节点在应答中提到的节点；其中大多数从未被联系。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Counted per publish, not per node. Firewalled nodes that cannot publish are missing.</source>
        <translation>按发布次数统计，而非按节点。无法发布的防火墙内节点不在其中。</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>Version %1 (eMule %2)</source>
        <translation>版本 %1 (eMule %2)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Version %1</source>
        <translation>版本 %1</translation>
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
        <translation>已发布：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Chunks Published: %1</source>
        <translation>已发布块：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload Saved: %1</source>
        <translation>节省的上传：%1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Fetched: %1</source>
        <translation>已获取：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Chunks Fetched: %1 %2</source>
        <translation>已获取块：%1 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Failed Hash Check: %1</source>
        <translation>哈希校验失败：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Resumed: %1</source>
        <translation>已恢复：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Offers Received: %1</source>
        <translation>收到的提议：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Declined: %1 %2</source>
        <translation>已拒绝：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Chunks Found in Kad: %1</source>
        <translation>在 Kad 中找到的块：%1</translation>
    </message>
    <message>
        <location line="-1046"/>
        <location line="+39"/>
        <location line="+1"/>
        <location line="+1"/>
        <location line="+10"/>
        <location line="+1084"/>
        <source>n/a</source>
        <translation>无数据</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Measured here, not reported by the provider, in decimal GB as providers bill. Expect a few percent below the provider&apos;s own figure.</source>
        <translation>此数值由本地统计，并非提供商报告，按提供商计费所用的十进制 GB 计算。预计会比提供商自己的数字低几个百分点。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>%1 (disabled)</source>
        <translation>%1（已禁用）</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Session Traffic: %1 %2</source>
        <translation>会话流量：%1 %2</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Articles Downloaded: %1</source>
        <translation>已下载文章：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not Found: %1</source>
        <translation>未找到：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Corrupt: %1</source>
        <translation>损坏：%1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>%1 of %2 %3</source>
        <translation>%1 / %2 %3</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>, resets %1</source>
        <translation>，%1 重置</translation>
    </message>
    <message>
        <location line="+3"/>
        <source> — spent</source>
        <translation> — 已用尽</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Block: %1</source>
        <translation>块账户：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>This Period: %1</source>
        <translation>本周期：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>All Time: %1</source>
        <translation>总计：%1</translation>
    </message>
</context>
<context>
    <name>eMule::StatsGraph</name>
    <message>
        <location filename="../src/gui/controls/StatsGraph.cpp" line="+42"/>
        <source>Stopped</source>
        <translation>已停止</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>%1: %2 @ %3 (%4 ago)</source>
        <translation>%1:%2 @ %3(%4 前)</translation>
    </message>
</context>
<context>
    <name>eMule::ToolbarCustomizeDialog</name>
    <message>
        <location filename="../src/gui/dialogs/ToolbarCustomizeDialog.cpp" line="+26"/>
        <source>── Separator ──</source>
        <translation>── 分隔符 ──</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Customize Toolbar</source>
        <translation>自定义工具栏</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Available toolbar buttons:</source>
        <translation>可用的工具栏按钮：</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Add -&gt;</source>
        <translation>添加 -&gt;</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&lt;- Remove</source>
        <translation>&lt;- 删除</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Current toolbar buttons:</source>
        <translation>当前的工具栏按钮：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Close</source>
        <translation>关闭</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Reset</source>
        <translation>重置</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Move Up</source>
        <translation>上移</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Move Down</source>
        <translation>下移</translation>
    </message>
</context>
<context>
    <name>eMule::TorrentDetailsDialog</name>
    <message>
        <source>Skip</source>
        <translation type="vanished">跳过</translation>
    </message>
    <message>
        <source>Low</source>
        <translation type="vanished">低</translation>
    </message>
    <message>
        <source>Normal</source>
        <translation type="vanished">普通</translation>
    </message>
    <message>
        <source>High</source>
        <translation type="vanished">高</translation>
    </message>
    <message>
        <source>Maximum</source>
        <translation type="vanished">最高</translation>
    </message>
    <message>
        <source>Torrent Details</source>
        <translation type="vanished">种子详情</translation>
    </message>
    <message>
        <source>File</source>
        <translation type="vanished">文件</translation>
    </message>
    <message>
        <location filename="../src/gui/dialogs/TorrentDetailsDialog.cpp" line="+52"/>
        <source>N/A</source>
        <translation>N/A</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Use global settings</source>
        <translation>使用全局设置</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Seed regardless</source>
        <translation>无限制做种</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Stop seeding at:</source>
        <translation>停止做种于：</translation>
    </message>
    <message>
        <location line="+46"/>
        <source>Info</source>
        <translation>信息</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Activity</source>
        <translation>活动</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Options</source>
        <translation>选项</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+2"/>
        <source>%1 — Torrent Inspector</source>
        <translation>%1 — Torrent 检查器</translation>
    </message>
    <message numerus="yes">
        <location line="+56"/>
        <source>%n file(s), %1</source>
        <translation>
            <numerusform>%n 个文件，%1</numerusform>
        </translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Getting metadata…</source>
        <translation>正在获取元数据…</translation>
    </message>
    <message numerus="yes">
        <location line="+4"/>
        <source>%1 (%n piece(s) @ %2)</source>
        <translation>
            <numerusform>%1（%n 个分块 @ %2）</numerusform>
        </translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Private to this tracker — DHT and PEX disabled</source>
        <translation>仅限此 Tracker — 已禁用 DHT 和 PEX</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Public torrent</source>
        <translation>公开 Torrent</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Created by %1 on %2</source>
        <translation>由 %1 创建于 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Created by %1</source>
        <translation>由 %1 创建</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Created on %1</source>
        <translation>创建于 %1</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>%1 (%2 selected)</source>
        <translation>%1（已选 %2）</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Active now</source>
        <translation>当前活动</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1 ago</source>
        <translation>%1 前</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Unknown</source>
        <translation>未知</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Details</source>
        <translation>详情</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Location</source>
        <translation>位置</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hash v1</source>
        <translation>哈希 v1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hash v2</source>
        <translation>哈希 v2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Privacy</source>
        <translation>私密性</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Origin</source>
        <translation>来源</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Copy Magnet Link</source>
        <translation>复制磁力链接</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Magnet</source>
        <translation>磁力链接</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Comment</source>
        <translation>评论</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Transfer</source>
        <translation>传输</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>State</source>
        <translation>状态</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Have</source>
        <translation>已有</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Availability</source>
        <translation>可用性</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Failed DL</source>
        <translation>下载失败</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ratio</source>
        <translation>分享率</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Error</source>
        <translation>错误</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>Available</source>
        <translation>可用</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Share of each part that is downloaded</source>
        <translation>每个分块已下载的比例</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Peers that have each part; red: nobody connected has it</source>
        <translation>拥有每个分块的用户数；红色：已连接用户均无此分块</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Dates</source>
        <translation>日期</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Added</source>
        <translation>添加于</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Completed</source>
        <translation>已完成</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Last Activity</source>
        <translation>最后活动</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Time Elapsed</source>
        <translation>已用时间</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Downloading</source>
        <translation>正在下载</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Seeding</source>
        <translation>做种中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remaining</source>
        <translation>剩余</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Speed</source>
        <translation>速度</translation>
    </message>
    <message>
        <location line="+7"/>
        <source> KiB/s</source>
        <translation> KiB/s</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Also limited by the global and BitTorrent limits.</source>
        <translation>同时受全局限速和 BitTorrent 限速约束。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Limit download speed:</source>
        <translation>限制下载速度：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Limit upload speed:</source>
        <translation>限制上传速度：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Seeding Limits</source>
        <translation>做种限制</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Ratio:</source>
        <translation>分享率：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source> min</source>
        <translation> 分钟</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Seed time:</source>
        <translation>做种时间：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Peer Connections</source>
        <translation>用户连接</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Default</source>
        <translation>默认</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Maximum peers:</source>
        <translation>最大用户数：</translation>
    </message>
    <message>
        <location line="+89"/>
        <source>Could not change the torrent options.</source>
        <translation>无法更改 Torrent 选项。</translation>
    </message>
    <message numerus="yes">
        <location line="+17"/>
        <source>Copied %n magnet link(s).</source>
        <translation>
            <numerusform>已复制 %n 个磁力链接。</numerusform>
        </translation>
    </message>
    <message>
        <location line="-232"/>
        <location line="+37"/>
        <source>Progress</source>
        <translation>进度</translation>
    </message>
    <message>
        <source>Priority</source>
        <translation type="vanished">优先级</translation>
    </message>
    <message>
        <source>Files</source>
        <translation type="vanished">文件</translation>
    </message>
    <message>
        <source>Address</source>
        <translation type="vanished">地址</translation>
    </message>
    <message>
        <source>Client</source>
        <translation type="vanished">客户端</translation>
    </message>
    <message>
        <source>Down Speed</source>
        <translation type="vanished">下载速度</translation>
    </message>
    <message>
        <source>Up Speed</source>
        <translation type="vanished">上传速度</translation>
    </message>
    <message>
        <location line="-34"/>
        <source>Downloaded</source>
        <translation>已下载</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Uploaded</source>
        <translation>已上传</translation>
    </message>
    <message>
        <source>Flags</source>
        <translation type="vanished">标志</translation>
    </message>
    <message>
        <source>Source</source>
        <translation type="vanished">来源</translation>
    </message>
    <message>
        <source>Peers</source>
        <translation type="vanished">下载数</translation>
    </message>
    <message>
        <source>URL</source>
        <translation type="vanished">URL</translation>
    </message>
    <message>
        <source>Tier</source>
        <translation type="vanished">层级</translation>
    </message>
    <message>
        <source>Status</source>
        <translation type="vanished">状态</translation>
    </message>
    <message>
        <source>Seeds</source>
        <translation type="vanished">做种数</translation>
    </message>
    <message>
        <source>Trackers</source>
        <translation type="vanished">Tracker</translation>
    </message>
    <message>
        <location line="+141"/>
        <source>This torrent is no longer in the list.</source>
        <translation>此种子已不在列表中。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>BitTorrent is not running.</source>
        <translation>BitTorrent 未运行。</translation>
    </message>
    <message>
        <source>Download</source>
        <translation type="vanished">下载</translation>
    </message>
    <message>
        <source>Do Not Download</source>
        <translation type="vanished">不下载</translation>
    </message>
    <message>
        <source>Could not change the file priorities.</source>
        <translation type="vanished">无法更改文件优先级。</translation>
    </message>
    <message>
        <source>Files (%1)</source>
        <translation type="vanished">文件 (%1)</translation>
    </message>
    <message>
        <source>Seed</source>
        <translation type="vanished">做种</translation>
    </message>
    <message>
        <source>Enc</source>
        <translation type="vanished">加密</translation>
    </message>
    <message>
        <source>Peers (%1)</source>
        <translation type="vanished">Peer (%1)</translation>
    </message>
    <message>
        <source>Working</source>
        <translation type="vanished">正常</translation>
    </message>
    <message>
        <source>Not working</source>
        <translation type="vanished">不工作</translation>
    </message>
    <message>
        <source>Trackers (%1)</source>
        <translation type="vanished">Tracker (%1)</translation>
    </message>
</context>
<context>
    <name>eMule::TorrentFilterProxy</name>
    <message>
        <location filename="../src/gui/controls/TorrentFilterProxy.cpp" line="+70"/>
        <source>All</source>
        <translation>全部</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloading</source>
        <translation>正在下载</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Seeding</source>
        <translation>做种中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paused</source>
        <translation>已暂停</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Active</source>
        <translation>活动</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Error</source>
        <translation>错误</translation>
    </message>
</context>
<context>
    <name>eMule::TorrentListModel</name>
    <message>
        <location filename="../src/gui/controls/TorrentListModel.cpp" line="+52"/>
        <source>Queued</source>
        <translation>排队中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Checking files</source>
        <translation>正在检查文件</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Getting metadata</source>
        <translation>正在获取元数据</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloading</source>
        <translation>正在下载</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+168"/>
        <source>Completed</source>
        <translation>已完成</translation>
    </message>
    <message>
        <location line="-167"/>
        <source>Seeding</source>
        <translation>做种中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Checking resume data</source>
        <translation>正在检查续传数据</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paused</source>
        <translation>已暂停</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Error</source>
        <translation>错误</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Moving</source>
        <translation>正在移动</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Unknown</source>
        <translation>未知</translation>
    </message>
    <message>
        <location line="+137"/>
        <source>Private torrent (no DHT, PEX or LSD)</source>
        <translation>私有种子（无 DHT、PEX 或 LSD）</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>%1% — %2 of %3 pieces</source>
        <translation>%1% — %2/%3 个分块</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Progress</source>
        <translation>进度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status</source>
        <translation>状态</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Down Speed</source>
        <translation>下载速度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Up Speed</source>
        <translation>上传速度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Seeds</source>
        <translation>做种数</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Peers</source>
        <translation>下载数</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ratio</source>
        <translation>分享率</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remaining Time</source>
        <translation>剩余时间</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Added</source>
        <translation>添加于</translation>
    </message>
    <message>
        <location line="+163"/>
        <source>%1 (#%2)</source>
        <translation>%1 (#%2)</translation>
    </message>
</context>
<context>
    <name>eMule::TorrentListsPane</name>
    <message>
        <location filename="../src/gui/controls/TorrentListsPane.cpp" line="+85"/>
        <source>Tracker</source>
        <translation>Tracker</translation>
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
        <translation>本地发现</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Resume data</source>
        <translation>恢复数据</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Incoming</source>
        <translation>下载完成目录</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>Skip</source>
        <translation>跳过</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Low</source>
        <translation>低</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Normal</source>
        <translation>普通</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>High</source>
        <translation>高</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Maximum</source>
        <translation>最高</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Peer is connected over µTP</source>
        <translation>用户通过 µTP 连接</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Optimistic unchoke</source>
        <translation>乐观解除阻塞</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloading from this peer</source>
        <translation>正在从此用户下载</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>We would download from this peer if they would let us</source>
        <translation>若对方允许，将从此用户下载</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Uploading to peer</source>
        <translation>正在上传给用户</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>We would upload to this peer if we unchoked it</source>
        <translation>若解除阻塞，将上传给此用户</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Peer has unchoked us, but we&apos;re not interested</source>
        <translation>用户已解除阻塞，但我们不感兴趣</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>We unchoked this peer, but they&apos;re not interested</source>
        <translation>我们已解除阻塞此用户，但对方不感兴趣</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+102"/>
        <location line="+141"/>
        <source>Encrypted connection</source>
        <translation>加密连接</translation>
    </message>
    <message>
        <location line="-242"/>
        <source>Connected through a holepunch</source>
        <translation>通过打洞连接</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Peer was found through DHT</source>
        <translation>通过 DHT 找到的用户</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Peer was found through Peer Exchange (PEX)</source>
        <translation>通过用户交换 (PEX) 找到的用户</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Peer is an incoming connection</source>
        <translation>用户为传入连接</translation>
    </message>
    <message>
        <location line="+40"/>
        <location line="+95"/>
        <source>Select a torrent to see its peers, trackers and files.</source>
        <translation>选择一个 Torrent 以查看其用户、Tracker 和文件。</translation>
    </message>
    <message>
        <location line="-40"/>
        <source>Up</source>
        <translation>上传</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Down</source>
        <translation>下载</translation>
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
        <translation>状态</translation>
    </message>
    <message>
        <location line="-53"/>
        <source>Address</source>
        <translation>地址</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Client</source>
        <translation>客户端</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+53"/>
        <source>Downloaded</source>
        <translation>已下载</translation>
    </message>
    <message>
        <location line="-53"/>
        <source>Uploaded</source>
        <translation>已上传</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Source</source>
        <translation>来源</translation>
    </message>
    <message>
        <location line="+13"/>
        <location line="+40"/>
        <source>Peers</source>
        <translation>下载数</translation>
    </message>
    <message>
        <location line="-38"/>
        <source>Trackers</source>
        <translation>Tracker</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Progress</source>
        <translation>进度</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+348"/>
        <source>Download</source>
        <translation>下载</translation>
    </message>
    <message>
        <location line="-348"/>
        <location line="+339"/>
        <source>Priority</source>
        <translation>优先级</translation>
    </message>
    <message>
        <location line="-328"/>
        <source>Files</source>
        <translation>文件</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>URL</source>
        <translation>URL</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Tier</source>
        <translation>层级</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Next Announce</source>
        <translation>下次汇报</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Seeds</source>
        <translation>做种数</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Add…</source>
        <translation>添加…</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Remove</source>
        <translation>删除</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Reannounce</source>
        <translation>重新汇报</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show backup trackers</source>
        <translation>显示备用 Tracker</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>This torrent is no longer in the list.</source>
        <translation>此种子已不在列表中。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>BitTorrent is not running.</source>
        <translation>BitTorrent 未运行。</translation>
    </message>
    <message>
        <location line="+51"/>
        <source>Peers (%1)</source>
        <translation>用户 (%1)</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>Announcing…</source>
        <translation>正在汇报…</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Error: %1</source>
        <translation>错误：%1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Working</source>
        <translation>工作中</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Working — %1</source>
        <translation>工作中 — %1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Not contacted yet</source>
        <translation>尚未联系</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Trackers (%1)</source>
        <translation>Tracker (%1)</translation>
    </message>
    <message>
        <location line="+97"/>
        <source>Files (%1)</source>
        <translation>文件 (%1)</translation>
    </message>
    <message>
        <location line="+63"/>
        <source>Do Not Download</source>
        <translation>不下载</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Expand All</source>
        <translation>全部展开</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Collapse All</source>
        <translation>全部折叠</translation>
    </message>
    <message>
        <location line="+42"/>
        <source>Could not change the file priorities.</source>
        <translation>无法更改文件优先级。</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Add Trackers</source>
        <translation>添加 Tracker</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Tracker URLs, one per line:</source>
        <translation>Tracker URL，每行一个：</translation>
    </message>
    <message>
        <location line="+41"/>
        <source>Could not change the trackers.</source>
        <translation>无法更改 Tracker。</translation>
    </message>
</context>
<context>
    <name>eMule::TorrentPanel</name>
    <message>
        <location filename="../src/gui/panels/TorrentPanel.cpp" line="+119"/>
        <source>Torrent download completed: %1</source>
        <translation>种子下载完成：%1</translation>
    </message>
    <message>
        <location line="+14"/>
        <location line="+6"/>
        <location line="+12"/>
        <location line="+13"/>
        <source>Add Torrent</source>
        <translation>添加种子</translation>
    </message>
    <message>
        <location line="-31"/>
        <location line="+18"/>
        <location line="+13"/>
        <location line="+15"/>
        <location line="+11"/>
        <location line="+525"/>
        <source>Not connected to the eMule core.</source>
        <translation>未连接到 eMule 核心。</translation>
    </message>
    <message>
        <location line="-575"/>
        <source>Cannot read %1.</source>
        <translation>无法读取 %1。</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Add Magnet Links</source>
        <translation>添加磁力链接</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+4"/>
        <source>Add Torrent Files</source>
        <translation>添加种子文件</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Torrent files (*.torrent);;All files (*)</source>
        <translation>种子文件 (*.torrent);;所有文件 (*)</translation>
    </message>
    <message>
        <location line="+70"/>
        <source>Add Torrent…</source>
        <translation>添加种子…</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Add Magnet Link…</source>
        <translation>添加磁力链接…</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Pause</source>
        <translation>暂停</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Resume</source>
        <translation>恢复</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Remove…</source>
        <translation>删除…</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Details…</source>
        <translation>详细信息…</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Force Recheck</source>
        <translation>强制重新校验</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Force Reannounce</source>
        <translation>强制重新汇报</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Open Folder</source>
        <translation>打开文件夹</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Copy Magnet Link</source>
        <translation>复制磁力链接</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Paste Magnet Links</source>
        <translation>粘贴磁力链接</translation>
    </message>
    <message>
        <location line="+78"/>
        <source>BitTorrent Options…</source>
        <translation>BitTorrent 选项…</translation>
    </message>
    <message>
        <location line="+114"/>
        <source>BitTorrent: %1</source>
        <translation>BitTorrent：%1</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Remove Torrents</source>
        <translation>删除种子</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Remove &quot;%1&quot; from the list?</source>
        <translation>从列表中删除“%1”？</translation>
    </message>
    <message numerus="yes">
        <location line="+1"/>
        <source>Remove %n torrent(s) from the list?</source>
        <translation>
            <numerusform>从列表中删除 %n 个种子？</numerusform>
        </translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Also delete the downloaded files</source>
        <translation>同时删除已下载的文件</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Saved on the core&apos;s machine: %1</source>
        <translation>保存在核心所在的计算机上：%1</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>No magnet link: v2-only torrents have no v1 info hash.</source>
        <translation>无磁力链接：仅 v2 的种子没有 v1 info hash。</translation>
    </message>
    <message numerus="yes">
        <location line="+4"/>
        <source>Copied %n magnet link(s).</source>
        <translation>
            <numerusform>已复制 %n 个磁力链接。</numerusform>
        </translation>
    </message>
    <message>
        <location line="+164"/>
        <source>Asking the core for the BitTorrent state…</source>
        <translation>正在向核心查询 BitTorrent 状态…</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>This eMule core was built without BitTorrent support.</source>
        <translation>此 eMule 核心编译时未包含 BitTorrent 支持。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>BitTorrent is off — enable it in Options.</source>
        <translation>BitTorrent 已关闭 — 请在选项中启用。</translation>
    </message>
    <message numerus="yes">
        <location line="+20"/>
        <source>%n torrent(s), %1 active — Down: %2, Up: %3 — DHT: %4 nodes — Port %5 %6</source>
        <translation>
            <numerusform>%n 个种子，%1 个活跃 — 下载：%2，上传：%3 — DHT：%4 个节点 — 端口 %5 %6</numerusform>
        </translation>
    </message>
    <message>
        <location line="+6"/>
        <source>(listening)</source>
        <translation>（监听中）</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>(not listening)</source>
        <translation>（未监听）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>No torrents. Use &quot;Add Torrent…&quot; or &quot;Add Magnet Link…&quot; to start one.</source>
        <translation>没有种子。使用“添加种子…”或“添加磁力链接…”开始。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>No torrents match this filter.</source>
        <translation>没有符合此筛选条件的 Torrent。</translation>
    </message>
</context>
<context>
    <name>eMule::TransferPanel</name>
    <message>
        <location filename="../src/gui/panels/TransferPanel.cpp" line="+800"/>
        <source>Downloads</source>
        <translation>下载</translation>
    </message>
    <message>
        <location line="-420"/>
        <location line="+340"/>
        <source>Priority (Download)</source>
        <translation>优先级（下载）</translation>
    </message>
    <message>
        <location line="-311"/>
        <location line="+2181"/>
        <location line="+177"/>
        <source>Low</source>
        <translation>低</translation>
    </message>
    <message>
        <location line="-2357"/>
        <location line="+2180"/>
        <location line="+178"/>
        <source>Normal</source>
        <translation>普通</translation>
    </message>
    <message>
        <location line="-2357"/>
        <location line="+2179"/>
        <location line="+179"/>
        <source>High</source>
        <translation>高</translation>
    </message>
    <message>
        <location line="-2354"/>
        <location line="+2358"/>
        <source>Very Low</source>
        <translation>非常低</translation>
    </message>
    <message>
        <location line="-2356"/>
        <location line="+2358"/>
        <source>Very High</source>
        <translation>非常高</translation>
    </message>
    <message>
        <location line="-2356"/>
        <location line="+2359"/>
        <source>Auto</source>
        <translation>自动</translation>
    </message>
    <message>
        <location line="-2348"/>
        <location line="+290"/>
        <location line="+455"/>
        <location line="+1429"/>
        <source>Pause</source>
        <translation>暂停</translation>
    </message>
    <message>
        <location line="-2165"/>
        <location line="+281"/>
        <location line="+461"/>
        <location line="+1425"/>
        <source>Stop</source>
        <translation>停止</translation>
    </message>
    <message>
        <location line="-2158"/>
        <location line="+272"/>
        <location line="+467"/>
        <location line="+1421"/>
        <source>Resume</source>
        <translation>恢复</translation>
    </message>
    <message>
        <location line="-2147"/>
        <location line="+259"/>
        <location line="+473"/>
        <location line="+1417"/>
        <location line="+4"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="-2120"/>
        <source>Preview with</source>
        <translation>预览方式</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Pause when preview is possible</source>
        <translation>可预览时暂停</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Increase priority for preview parts</source>
        <translation>提高预览分块的优先级</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Stop Import parts to file</source>
        <translation>停止向文件导入分块</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+1471"/>
        <location line="+1"/>
        <location line="+10"/>
        <source>Import parts to file...</source>
        <translation>向文件导入分块...</translation>
    </message>
    <message>
        <location line="-1477"/>
        <source>Mass Rename...</source>
        <translation>批量重命名...</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Source Handling</source>
        <translation>来源处理</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add Sources...</source>
        <translation>添加来源...</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+1378"/>
        <source>Source Limit...</source>
        <translation>来源限制...</translation>
    </message>
    <message>
        <location line="-1375"/>
        <source>Swap All A4AF Sources to This File</source>
        <translation>将所有 A4AF 来源切换到此文件</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>cannot swap sources.</source>
        <translation>无法切换来源。</translation>
    </message>
    <message numerus="yes">
        <location line="+6"/>
        <source>%n A4AF source(s) swapped to this file.</source>
        <translation>
            <numerusform>已将 %n 个 A4AF 来源切换到此文件。</numerusform>
        </translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Copy eD2K Links</source>
        <translation>复制 eD2K 链接</translation>
    </message>
    <message>
        <location line="+25"/>
        <location line="+71"/>
        <source>ed3k Sets...</source>
        <translation>ed3k 集合...</translation>
    </message>
    <message>
        <location line="-31"/>
        <source>New...</source>
        <translation>新建...</translation>
    </message>
    <message>
        <location line="+22"/>
        <location line="+9"/>
        <source>Show Toolbar</source>
        <translation>显示工具栏</translation>
    </message>
    <message>
        <location line="+437"/>
        <source>Close Toolbar</source>
        <translation>关闭工具栏</translation>
    </message>
    <message>
        <location line="+773"/>
        <source>Failed to execute: %1 %2</source>
        <translation>执行失败: %1 %2</translation>
    </message>
    <message>
        <location line="+46"/>
        <source>Please enter the new source limit for the selected file(s)!
(set to 0 to use global setting)</source>
        <translation>请输入所选文件的新来源限制！
(设为 0 则使用全局设置)</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>cannot add the source.</source>
        <translation>无法添加来源。</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>The source %1 was not accepted.</source>
        <translation>来源 %1 未被接受。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>cannot import parts.</source>
        <translation>无法导入分块。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Path of the file on the core&apos;s machine:</source>
        <translation>核心所在机器上的文件路径:</translation>
    </message>
    <message>
        <location line="+603"/>
        <source>Download In Alphabetical Order</source>
        <translation>按字母顺序下载</translation>
    </message>
    <message>
        <location line="+182"/>
        <source>Copy ed3k Node ID</source>
        <translation>复制 ed3k Node ID</translation>
    </message>
    <message>
        <location line="+70"/>
        <location line="+103"/>
        <source>cannot view shared files.</source>
        <translation>无法查看共享文件。</translation>
    </message>
    <message>
        <location line="-90"/>
        <source>Unban</source>
        <translation>解除封禁</translation>
    </message>
    <message>
        <location line="+124"/>
        <source>Bootstrap</source>
        <translation>引导</translation>
    </message>
    <message>
        <location line="+168"/>
        <source>Cancel Download</source>
        <translation>取消下载</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+31"/>
        <source>Cancel download &quot;%1&quot;?</source>
        <translation>取消下载 &quot;%1&quot;？</translation>
    </message>
    <message>
        <location line="-27"/>
        <location line="+29"/>
        <source>Cancel Downloads</source>
        <translation>取消多个下载</translation>
    </message>
    <message>
        <location line="-28"/>
        <location line="+27"/>
        <source>Cancel %1 selected downloads?</source>
        <translation>取消选中的 %1 个下载？</translation>
    </message>
    <message>
        <location line="+21"/>
        <location line="+18"/>
        <location line="+34"/>
        <source>Rename</source>
        <translation>重命名</translation>
    </message>
    <message>
        <location line="-51"/>
        <source>Do you want to cleanup the file names of the selected files?</source>
        <translation>要清理所选文件的文件名吗?</translation>
    </message>
    <message>
        <source>File name:</source>
        <translation type="vanished">文件名：</translation>
    </message>
    <message>
        <location line="-2766"/>
        <location line="+244"/>
        <location line="+479"/>
        <location line="+674"/>
        <source>Open File</source>
        <translation>打开文件</translation>
    </message>
    <message>
        <location line="-1388"/>
        <location line="+2"/>
        <location line="+726"/>
        <location line="+717"/>
        <source>Preview</source>
        <translation>预览</translation>
    </message>
    <message>
        <location line="-1406"/>
        <location line="+2301"/>
        <location line="+118"/>
        <source>Details...</source>
        <translation>详情...</translation>
    </message>
    <message>
        <location line="-2410"/>
        <source>Comments...</source>
        <translation>评论...</translation>
    </message>
    <message>
        <location line="+53"/>
        <location line="+134"/>
        <location line="+544"/>
        <source>Clear Completed</source>
        <translation>清除已完成</translation>
    </message>
    <message>
        <location line="-633"/>
        <source>eD2K Links...</source>
        <translation>eD2K 链接...</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+84"/>
        <source>Paste eD2K Links</source>
        <translation>粘贴 eD2K 链接</translation>
    </message>
    <message>
        <location line="-64"/>
        <location line="+64"/>
        <location line="+2093"/>
        <location line="+113"/>
        <location line="+72"/>
        <source>Find...</source>
        <translation>查找...</translation>
    </message>
    <message>
        <location line="-2334"/>
        <source>Search Related Files</source>
        <translation>搜索相关文件</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Web Services</source>
        <translation>Web 服务</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+35"/>
        <location line="+525"/>
        <source>Assign To Category</source>
        <translation>分配到分类</translation>
    </message>
    <message>
        <source>(All)</source>
        <translation type="vanished">(全部)</translation>
    </message>
    <message>
        <source>All</source>
        <translation type="vanished">全部</translation>
    </message>
    <message>
        <location line="+1272"/>
        <source>Uploading</source>
        <translation>上传中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloading</source>
        <translation>正在下载</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>On Queue</source>
        <translation>排队中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Known Clients</source>
        <translation>已知客户端</translation>
    </message>
    <message>
        <location line="-1388"/>
        <source>Clients on queue:   0</source>
        <translation>排队客户端：   0</translation>
    </message>
    <message>
        <location line="-438"/>
        <location line="+558"/>
        <source>(Unassign)</source>
        <translation>（取消分配）</translation>
    </message>
    <message>
        <location line="-83"/>
        <location line="+1418"/>
        <source>Priority</source>
        <translation>优先级</translation>
    </message>
    <message>
        <location line="-1381"/>
        <source>Open Folder</source>
        <translation>打开文件夹</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Details</source>
        <translation>详情</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Comments</source>
        <translation>评论</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>eD2K Links</source>
        <translation>eD2K 链接</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Search Related</source>
        <translation>相关搜索</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Find</source>
        <translation>查找</translation>
    </message>
    <message>
        <location line="+630"/>
        <location line="+23"/>
        <source>Preview not available — web server is not running or stream token not received.</source>
        <translation>预览不可用 — Web 服务器未运行或未收到流令牌。</translation>
    </message>
    <message>
        <location line="+151"/>
        <source>Open File not available — web server is not running or stream token not received.</source>
        <translation>打开文件不可用 — Web 服务器未运行或未收到流令牌。</translation>
    </message>
    <message>
        <location line="+459"/>
        <location line="+7"/>
        <source>Downloads (%1)</source>
        <translation>下载 (%1)</translation>
    </message>
    <message>
        <location line="-23"/>
        <source>Uploading (%1)</source>
        <translation>上传中 (%1)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloading (%1)</source>
        <translation>下载中 (%1)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>On Queue (%1)</source>
        <translation>排队中 (%1)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Known Clients (%1)</source>
        <translation>已知客户端 (%1)</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>Clients on queue:   %1</source>
        <translation>排队客户端：   %1</translation>
    </message>
    <message>
        <source>Cat %1</source>
        <translation type="vanished">分类 %1</translation>
    </message>
    <message>
        <source>Category</source>
        <translation type="obsolete">分类</translation>
    </message>
    <message>
        <location line="+50"/>
        <source>Are you sure you want to cancel every download in &quot;%1&quot;?</source>
        <translation>确定要取消“%1”中的所有下载吗？</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Resume next file</source>
        <translation>恢复下一个文件</translation>
    </message>
    <message>
        <source>Open Incoming Folder</source>
        <translation type="obsolete">打开接收文件夹</translation>
    </message>
    <message>
        <location line="+217"/>
        <location line="+112"/>
        <source>Add To Friends</source>
        <translation>添加到好友</translation>
    </message>
    <message>
        <location line="-88"/>
        <location line="+113"/>
        <source>Send Message</source>
        <translation>发送消息</translation>
    </message>
    <message>
        <source>Message:</source>
        <translation type="vanished">消息:</translation>
    </message>
    <message>
        <location line="-102"/>
        <location line="+109"/>
        <source>View Shared Files</source>
        <translation>查看共享文件</translation>
    </message>
</context>
<context>
    <name>eMule::TrayMenuManager</name>
    <message>
        <location filename="../src/gui/app/TrayMenuManager.cpp" line="+86"/>
        <source>eMule Speed</source>
        <translation>eMule 速度</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Download:</source>
        <translation>下载：</translation>
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
        <translation>无限制</translation>
    </message>
    <message>
        <location line="-5"/>
        <source>Upload:</source>
        <translation>上传：</translation>
    </message>
    <message>
        <location line="+34"/>
        <source>Set Full Up/Down-Speed</source>
        <translation>设置最大上传/下载速度</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Throttle Up/Down-Speed</source>
        <translation>限制上传/下载速度</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Pause Usenet</source>
        <translation>暂停 Usenet</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Connect</source>
        <translation>连接</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Disconnect</source>
        <translation>断开连接</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Options</source>
        <translation>选项</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Restore</source>
        <translation>还原</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Exit</source>
        <translation>退出</translation>
    </message>
</context>
<context>
    <name>eMule::UsenetArchiveEntryDialog</name>
    <message>
        <location filename="../src/gui/dialogs/UsenetArchiveEntryDialog.cpp" line="+59"/>
        <source>Preview File</source>
        <translation>预览文件</translation>
    </message>
    <message>
        <location line="+42"/>
        <source>Playable</source>
        <translation>可播放</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>Nothing has arrived for this download yet.</source>
        <translation>此下载尚未收到任何数据。</translation>
    </message>
    <message numerus="yes">
        <location line="+3"/>
        <source>Reading the archive… %n file(s) found so far</source>
        <translation>
            <numerusform>正在读取压缩包… 目前已找到 %n 个文件</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="+6"/>
        <source>%n file(s) in the archive.</source>
        <translation>
            <numerusform>压缩包中有 %n 个文件。</numerusform>
        </translation>
    </message>
    <message>
        <location line="+34"/>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Status</source>
        <translation>状态</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Reading the archive…</source>
        <translation>正在读取压缩包…</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Preview</source>
        <translation>预览</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Keep Scanning</source>
        <translation>继续扫描</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>The connection to the core was lost.</source>
        <translation>与核心的连接已断开。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>The archive is still being read — the files listed so far are all that is known.</source>
        <translation>压缩包仍在读取中 — 目前列出的文件就是已知的全部内容。</translation>
    </message>
</context>
<context>
    <name>eMule::UsenetDetailsDialog</name>
    <message>
        <location filename="../src/gui/dialogs/UsenetDetailsDialog.cpp" line="+15"/>
        <location line="+18"/>
        <source>Release Details</source>
        <translation>发布内容详情</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Not checked.</source>
        <translation>未检查。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>%1% of this release looks obtainable.</source>
        <translation>此发布内容约 %1% 看起来可以获取。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1% by the NZB&apos;s own article counts. No server was asked.</source>
        <translation>按 NZB 自身的文章数计算为 %1%。未询问任何服务器。</translation>
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
            <numerusform>有 %n 篇文章从未列在 NZB 中</numerusform>
        </translation>
    </message>
    <message>
        <location line="+58"/>
        <source>&quot;%1&quot; has not been published on its own.</source>
        <translation>“%1”尚未单独发布。</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>&quot;%1&quot; is outside the core&apos;s Incoming folder and cannot be opened from here.</source>
        <translation>“%1”不在核心的接收文件夹内，无法从这里打开。</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Total Size:</source>
        <translation>总大小：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Date:</source>
        <translation>日期：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status:</source>
        <translation>状态：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Health:</source>
        <translation>健康度：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Poster:</source>
        <translation>发布者：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Articles:</source>
        <translation>文章：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Newsgroups:</source>
        <translation>新闻组：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Files:</source>
        <translation>文件数：</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Progress</source>
        <translation>进度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Articles</source>
        <translation>文章</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Missing</source>
        <translation>缺失</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Status</source>
        <translation>状态</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Open Folder</source>
        <translation>打开文件夹</translation>
    </message>
</context>
<context>
    <name>eMule::UsenetFileCheckList</name>
    <message>
        <location filename="../src/gui/controls/UsenetFileCheckList.cpp" line="+53"/>
        <source>Select All</source>
        <translation>全选</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Select None</source>
        <translation>全不选</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Invert Selection</source>
        <translation>反选</translation>
    </message>
    <message>
        <location line="+73"/>
        <source>Could not change which files download.</source>
        <translation>无法更改要下载的文件。</translation>
    </message>
</context>
<context>
    <name>eMule::UsenetPanel</name>
    <message>
        <location filename="../src/gui/panels/UsenetPanel.cpp" line="+166"/>
        <location line="+7"/>
        <location line="+44"/>
        <location line="+498"/>
        <source>Add NZB</source>
        <translation>添加 NZB</translation>
    </message>
    <message>
        <location line="-548"/>
        <location line="+50"/>
        <location line="+62"/>
        <source>Not connected to the eMule core.</source>
        <translation>未连接到 eMule 核心。</translation>
    </message>
    <message>
        <location line="-105"/>
        <source>Cannot read %1.</source>
        <translation>无法读取 %1。</translation>
    </message>
    <message>
        <location line="+104"/>
        <source>Add NZB from URL</source>
        <translation>从 URL 添加 NZB</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Add NZB…</source>
        <translation>添加 NZB…</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+964"/>
        <source>Pause</source>
        <translation>暂停</translation>
    </message>
    <message>
        <location line="-960"/>
        <location line="+963"/>
        <source>Resume</source>
        <translation>恢复</translation>
    </message>
    <message>
        <location line="-961"/>
        <source>Remove</source>
        <translation>删除</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Clear Completed</source>
        <translation>清除已完成</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+912"/>
        <source>Pause All</source>
        <translation>全部暂停</translation>
    </message>
    <message>
        <location line="-905"/>
        <source>Add NZB from URL…</source>
        <translation>从 URL 添加 NZB…</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Preview</source>
        <translation>预览</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Check Availability</source>
        <translation>检查可用性</translation>
    </message>
    <message>
        <location line="+269"/>
        <source>Priority</source>
        <translation>优先级</translation>
    </message>
    <message>
        <source>High</source>
        <translation type="obsolete">高</translation>
    </message>
    <message>
        <source>Normal</source>
        <translation type="obsolete">普通</translation>
    </message>
    <message>
        <source>Low</source>
        <translation type="obsolete">低</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Assign To Category</source>
        <translation>分配到分类</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>No category</source>
        <translation>无分类</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Set Password…</source>
        <translation>设置密码…</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Download Selected Files</source>
        <translation>下载所选文件</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Skip Selected Files</source>
        <translation>跳过所选文件</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Open File</source>
        <translation>打开文件</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Open Folder</source>
        <translation>打开文件夹</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Details…</source>
        <translation>详细信息…</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Remove and Delete Files</source>
        <translation>移除并删除文件</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>NZB files (*.nzb);;All files (*)</source>
        <translation>NZB 文件 (*.nzb);;所有文件 (*)</translation>
    </message>
    <message>
        <location line="+34"/>
        <location line="+15"/>
        <source>Set Password</source>
        <translation>设置密码</translation>
    </message>
    <message>
        <location line="-14"/>
        <source>Archive password for &quot;%1&quot;:</source>
        <translation>“%1”的压缩包密码：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>this download</source>
        <translation>此下载</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Remove the stored password for this download?</source>
        <translation>移除此下载已保存的密码？</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Could not set the password.</source>
        <translation>无法设置密码。</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Remove Downloads</source>
        <translation>移除下载</translation>
    </message>
    <message numerus="yes">
        <location line="+1"/>
        <source>Remove %n download(s) and delete the files already fetched?</source>
        <translation>
            <numerusform>移除 %n 个下载并删除已获取的文件？</numerusform>
        </translation>
    </message>
    <message>
        <location line="+63"/>
        <location line="+82"/>
        <source>Nothing has completed yet for &quot;%1&quot;.</source>
        <translation>“%1”尚无已完成的内容。</translation>
    </message>
    <message>
        <location line="+100"/>
        <source>Nothing here can be previewed yet.</source>
        <translation>这里还没有可以预览的内容。</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Preview is unavailable — the daemon&apos;s web server is not running.</source>
        <translation>预览不可用 — 守护进程的 Web 服务器未运行。</translation>
    </message>
    <message>
        <location line="+58"/>
        <source>No Usenet downloads. Use &quot;Add NZB…&quot; to queue one.</source>
        <translation>没有 Usenet 下载。使用“添加 NZB…”将其加入队列。</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>%1 download(s), %2 active — %3% complete</source>
        <translation>%1 个下载，%2 个活动 — 已完成 %3%</translation>
    </message>
    <message>
        <location line="+9"/>
        <source> — %1</source>
        <translation> — %1</translation>
    </message>
    <message>
        <location line="+7"/>
        <source> — limited to %1 KB/s while eD2K downloads</source>
        <translation> — eD2K 下载时限速 %1 KB/s</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Download limit %1 KB/s: Usenet up to %2 KB/s, eD2K up to %3 KB/s.
Whichever network is idle lends its share to the other.</source>
        <translation>下载限速 %1 KB/s：Usenet 最高 %2 KB/s，eD2K 最高 %3 KB/s。
空闲的网络会把自己的份额让给另一方。</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Usenet: %1</source>
        <translation>Usenet：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Usenet: downloading again.</source>
        <translation>Usenet：已恢复下载。</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Resume All</source>
        <translation>全部恢复</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Let every Usenet download continue</source>
        <translation>让所有 Usenet 下载继续</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Stop starting new Usenet articles. Nothing is removed, and each release keeps its own state.</source>
        <translation>停止开始新的 Usenet 文章。不会移除任何内容，每个发布内容都保留自己的状态。</translation>
    </message>
    <message>
        <location line="+42"/>
        <location line="+4"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove every Usenet download in &quot;%1&quot; and delete its files?</source>
        <translation>移除“%1”中的所有 Usenet 下载并删除其文件？</translation>
    </message>
    <message>
        <location line="+40"/>
        <source>Could not apply that to the category: %1</source>
        <translation>无法应用到该分类：%1</translation>
    </message>
</context>
<context>
    <name>eMule::UsenetQueueModel</name>
    <message numerus="yes">
        <source>%n article(s) missing</source>
        <translation type="vanished">
            <numerusform>缺少 %n 篇文章</numerusform>
        </translation>
    </message>
    <message>
        <source>Complete</source>
        <translation type="vanished">已完成</translation>
    </message>
    <message numerus="yes">
        <location filename="../src/gui/controls/UsenetQueueModel.cpp" line="+120"/>
        <source>%1% — %n article(s) missing</source>
        <translation>
            <numerusform>%1% — 缺失 %n 篇文章</numerusform>
        </translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Skipped — tick it to download this file</source>
        <translation>已跳过 — 勾选即可下载此文件</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>%1% — %2 of %3 articles</source>
        <translation>%1% — %3 篇文章中的 %2 篇</translation>
    </message>
    <message numerus="yes">
        <location line="+4"/>
        <source>, %n missing</source>
        <translation>
            <numerusform>，缺失 %n 篇</numerusform>
        </translation>
    </message>
    <message>
        <location line="+84"/>
        <source>Not checked.</source>
        <translation>未检查。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1% of this release looks obtainable.</source>
        <translation>此发布内容约 %1% 看起来可以获取。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1% by the NZB&apos;s own article counts. No server was asked.</source>
        <translation>按 NZB 自身的文章数计算为 %1%。未询问任何服务器。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>The PAR2 recovery volumes should cover the shortfall.</source>
        <translation>PAR2 恢复卷应能补足缺失部分。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>%1
The password for this release did not work. Right-click to set a different one.</source>
        <translation>%1
此发布内容的密码无效。右键单击可设置其他密码。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1
This release is password-protected. Right-click to set its password.</source>
        <translation>%1
此发布内容受密码保护。右键单击可设置密码。</translation>
    </message>
    <message numerus="yes">
        <location line="+4"/>
        <source>%1
%n article(s) could not be found on any server</source>
        <translation>
            <numerusform>%1
有 %n 篇文章在所有服务器上都找不到</numerusform>
        </translation>
    </message>
    <message>
        <location line="+4"/>
        <source>%1
A password is set for this release.</source>
        <translation>%1
已为此发布内容设置密码。</translation>
    </message>
    <message>
        <location line="+77"/>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Progress</source>
        <translation>进度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status</source>
        <translation>状态</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Speed</source>
        <translation>速度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remaining</source>
        <translation>剩余</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority</source>
        <translation>优先级</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Health</source>
        <translation>健康度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Category</source>
        <translation>分类</translation>
    </message>
</context>
<context>
    <name>eMule::VersionChecker</name>
    <message>
        <location filename="../src/gui/app/VersionChecker.cpp" line="+100"/>
        <source>the version manifest is not a JSON object</source>
        <translation>版本清单不是 JSON 对象</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>invalid JSON response: %1</source>
        <translation>无效的 JSON 响应：%1</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>the version manifest has no &apos;latest&apos; field</source>
        <translation>版本清单中没有 &apos;latest&apos; 字段</translation>
    </message>
</context>
<context>
    <name>eMule::WebLinksDialog</name>
    <message>
        <location filename="../src/gui/dialogs/WebLinksDialog.cpp" line="+47"/>
        <source>Links: %1</source>
        <translation>链接：%1</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>The page says these files are password protected.</source>
        <translation>页面说明这些文件受密码保护。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>File</source>
        <translation>文件</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Hoster</source>
        <translation>网盘</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Link</source>
        <translation>链接</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Links to .onion addresses are Tor hidden services. Copy them into Tor Browser: other browsers cannot open them.</source>
        <translation>.onion 地址的链接是 Tor 隐藏服务。请将其复制到 Tor Browser 中：其他浏览器无法打开。</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Copy Links</source>
        <translation>复制链接</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Open in Browser</source>
        <translation>在浏览器中打开</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Save List...</source>
        <translation>保存列表...</translation>
    </message>
    <message numerus="yes">
        <location line="+42"/>
        <source>Copied %n link(s) to the clipboard.</source>
        <translation>
            <numerusform>已将 %n 个链接复制到剪贴板。</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="+7"/>
        <source>Open %n links in the browser?</source>
        <translation>
            <numerusform>要在浏览器中打开 %n 个链接吗？</numerusform>
        </translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Save Link List</source>
        <translation>保存链接列表</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Link lists (*.txt);;All files (*)</source>
        <translation>链接列表 (*.txt);;所有文件 (*)</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Could not write %1: %2</source>
        <translation>无法写入 %1：%2</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Saved %1</source>
        <translation>已保存 %1</translation>
    </message>
</context>
<context>
    <name>eMule::WebServer</name>
    <message>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+15"/>
        <source>(still scanning)</source>
        <translation>（仍在扫描）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Accepted</source>
        <translation>已接受</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add</source>
        <translation>添加</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add NZB from URL…</source>
        <translation>从 URL 添加 NZB…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add NZB…</source>
        <translation>添加 NZB…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add paused</source>
        <translation>以暂停状态添加</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add to Static</source>
        <translation>添加到静态列表</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Adding %1 NZB(s)…</source>
        <translation>正在添加 %1 个 NZB…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Address</source>
        <translation>地址</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebServer.cpp" line="-3417"/>
        <location line="+3306"/>
        <location line="+49"/>
        <source>Session expired — log in again</source>
        <translation>会话已过期 — 请重新登录</translation>
    </message>
    <message>
        <location line="-3351"/>
        <source>Guests cannot add downloads</source>
        <translation>访客无法添加下载</translation>
    </message>
    <message>
        <location line="+609"/>
        <source>Looking for comments on Kad</source>
        <translation>正在 Kad 上查找评论</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Rating: %1</source>
        <translation>评分：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Has comments</source>
        <translation>有评论</translation>
    </message>
    <message>
        <location line="+404"/>
        <location line="+1"/>
        <source>Incoming</source>
        <translation>下载完成目录</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Nothing has finished downloading yet.</source>
        <translation>还没有下载完成的文件。</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Modified</source>
        <translation>修改时间</translation>
    </message>
    <message>
        <location line="+40"/>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+36"/>
        <source>Download</source>
        <translation>下载</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Play</source>
        <translation>播放</translation>
    </message>
    <message>
        <location line="+94"/>
        <source>Open this URL in VLC or another player:</source>
        <translation>请在 VLC 或其他播放器中打开此 URL：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>This file is named %1 but its contents are %2. The name is wrong — common for files off the ed2k network — so a player that trusts it finds no %3 and sits at 0:00. It is being served as its real type, so it may still play above.</source>
        <translation>此文件名为 %1，但内容是 %2。文件名有误（ed2k 网络上的文件常有此情况），因此信任文件名的播放器找不到 %3，会停在 0:00。它正以真实类型提供，所以上方仍可能播放。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>This file is named %1 but does not start with the %2 signature every one of them has, and its contents match no media container we recognise. It is very likely a fake or a corrupt download — no player will get anything out of it.</source>
        <translation>此文件名为 %1，但并不以此类文件都有的 %2 签名开头，其内容也不符合任何已知的媒体容器。它很可能是假文件或已损坏的下载 — 任何播放器都无法播放。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>The raw URL, if you want to look for yourself:</source>
        <translation>原始 URL，如需自行查看：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Your browser probably cannot decode %1.</source>
        <translation>您的浏览器可能无法解码 %1。</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Copy</source>
        <translation>复制</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Copied</source>
        <translation>已复制</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Download this file</source>
        <translation>下载此文件</translation>
    </message>
    <message>
        <location line="+62"/>
        <source>Access denied — no password configured. Set a password in Options → Web Interface.</source>
        <translation>拒绝访问 — 未设置密码。请在“选项 → Web 界面”中设置密码。</translation>
    </message>
    <message numerus="yes">
        <location line="+11"/>
        <source>Too many failed logins. Try again in %n second(s).</source>
        <translation>
            <numerusform>登录失败次数过多。请在 %n 秒后重试。</numerusform>
        </translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Login failed</source>
        <translation>登录失败</translation>
    </message>
    <message numerus="yes">
        <location line="+236"/>
        <source>%n download(s) added</source>
        <translation>
            <numerusform>已添加 %n 个下载</numerusform>
        </translation>
    </message>
    <message>
        <source>Not connected — the search could not be sent.</source>
        <translation type="vanished">未连接 — 无法发送搜索。</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Not sent yet — the search is waiting (%1) and starts by itself.</source>
        <translation>尚未发送 — 搜索正在等待（%1），会自动开始。</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Nothing was saved: a value is missing or out of range.</source>
        <translation>未保存任何内容：某个值缺失或超出范围。</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Not saved: %1</source>
        <translation>未保存：%1</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Saved. Now listening on TCP %1, UDP %2.</source>
        <translation>已保存。现在监听 TCP %1、UDP %2。</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+27"/>
        <source>Saved. The new ports are used after a restart.</source>
        <translation>已保存。新端口将在重启后生效。</translation>
    </message>
    <message>
        <location line="-23"/>
        <source>Saved, but a new port is in use. Still listening on TCP %1, UDP %2.</source>
        <translation>已保存，但新端口已被占用。仍在监听 TCP %1、UDP %2。</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+19"/>
        <source>Saved.</source>
        <translation>已保存。</translation>
    </message>
    <message>
        <location line="+13"/>
        <location line="+1973"/>
        <source>Web Control Panel</source>
        <translation>Web 控制面板</translation>
    </message>
    <message>
        <location line="-1965"/>
        <source>Not connected</source>
        <translation>未连接</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Blocked: network interface not available</source>
        <translation>已阻止：网络接口不可用</translation>
    </message>
    <message>
        <location line="+299"/>
        <source>Connected</source>
        <translation>已连接</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+335"/>
        <source>Disconnected</source>
        <translation>已断开</translation>
    </message>
    <message numerus="yes">
        <location line="-282"/>
        <source>&quot;%1&quot;: %n result(s)</source>
        <translation>
            <numerusform>“%1”：%n 个结果</numerusform>
        </translation>
    </message>
    <message>
        <location line="+195"/>
        <source>Active Connections</source>
        <translation>活跃连接</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Time</source>
        <translation>时间</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Connected to: %1 (%2:%3)</source>
        <translation>已连接到：%1 (%2:%3)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Client ID: %1 (%2)</source>
        <translation>客户端 ID：%1 (%2)</translation>
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
        <translation>用户：%1 | 文件：%2</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Description: %1</source>
        <translation>描述：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Ping: %1 ms</source>
        <translation>Ping：%1 毫秒</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Connecting...</source>
        <translation>连接中...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not connected to any server</source>
        <translation>未连接到任何服务器</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Running</source>
        <translation>运行中</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Not available</source>
        <translation>不可用</translation>
    </message>
    <message>
        <location line="+149"/>
        <location line="+1128"/>
        <source>Queued</source>
        <translation>排队中</translation>
    </message>
    <message>
        <location line="-1127"/>
        <location line="+1126"/>
        <source>Downloading</source>
        <translation>正在下载</translation>
    </message>
    <message>
        <location line="-1125"/>
        <source>Paused</source>
        <translation>已暂停</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+960"/>
        <location line="+163"/>
        <source>Complete</source>
        <translation>已完成</translation>
    </message>
    <message>
        <location line="-1122"/>
        <source>Failed</source>
        <translation>失败</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Verifying</source>
        <translation>正在校验</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Repairing</source>
        <translation>正在修复</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unpacking</source>
        <translation>正在解压</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Checking</source>
        <translation>正在检查</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unknown</source>
        <translation>未知</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>Not checked.</source>
        <translation>未检查。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1% of this release looks obtainable.</source>
        <translation>此发布内容约 %1% 看起来可以获取。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1% by the NZB&apos;s own article counts. No server was asked.</source>
        <translation>按 NZB 自身的文章数计算为 %1%。未询问任何服务器。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>The PAR2 recovery volumes should cover the shortfall.</source>
        <translation>PAR2 恢复卷应能补足缺失部分。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>%1
The password for this release did not work. Right-click to set a different one.</source>
        <translation>%1
此发布内容的密码无效。右键单击可设置其他密码。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1
This release is password-protected. Right-click to set its password.</source>
        <translation>%1
此发布内容受密码保护。右键单击可设置密码。</translation>
    </message>
    <message numerus="yes">
        <location line="+4"/>
        <source>%1
%n article(s) could not be found on any server</source>
        <translation>
            <numerusform>%1
有 %n 篇文章在所有服务器上都找不到</numerusform>
        </translation>
    </message>
    <message>
        <location line="+4"/>
        <source>%1
A password is set for this release.</source>
        <translation>%1
已为此发布内容设置密码。</translation>
    </message>
    <message>
        <location line="+126"/>
        <source>%1 (and %2 other(s))</source>
        <translation>%1（及其他 %2 个）</translation>
    </message>
    <message>
        <location line="+60"/>
        <source>Usenet item not found</source>
        <translation>未找到 Usenet 项目</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Very high</source>
        <translation>非常高</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>High</source>
        <translation>高</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Low</source>
        <translation>低</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Very low</source>
        <translation>非常低</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Normal</source>
        <translation>普通</translation>
    </message>
    <message>
        <location line="+188"/>
        <source>Only a queued or downloading release can be paused</source>
        <translation>只能暂停排队中或下载中的发布内容</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Only a paused or failed release can be resumed</source>
        <translation>只能恢复已暂停或失败的发布内容</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+110"/>
        <location line="+174"/>
        <source>priority must be a number from -2 to 2</source>
        <translation>优先级必须是 -2 到 2 之间的数字</translation>
    </message>
    <message>
        <location line="-278"/>
        <location line="+110"/>
        <location line="+71"/>
        <location line="+90"/>
        <source>Unknown category</source>
        <translation>未知分类</translation>
    </message>
    <message>
        <location line="-259"/>
        <location line="+4"/>
        <source>files must be a list of file numbers</source>
        <translation>files 必须是文件编号的列表</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Unknown action</source>
        <translation>未知操作</translation>
    </message>
    <message>
        <location line="+228"/>
        <source>Post the .nzb as the request body, or give a url</source>
        <translation>请将 .nzb 作为请求正文发送，或提供 URL</translation>
    </message>
    <message>
        <location line="+57"/>
        <source>Guests cannot change downloads</source>
        <translation>访客无法更改下载</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Nothing selected</source>
        <translation>未选择任何项</translation>
    </message>
    <message>
        <location line="+49"/>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="-35"/>
        <source>All</source>
        <translation>全部</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>No category</source>
        <translation>无分类</translation>
    </message>
    <message numerus="yes">
        <location line="+114"/>
        <location line="+163"/>
        <source>%n article(s) missing</source>
        <translation>
            <numerusform>缺少 %n 篇文章</numerusform>
        </translation>
    </message>
    <message>
        <location line="-99"/>
        <source>No Usenet downloads. Use &quot;Add NZB…&quot; to queue one.</source>
        <translation>没有 Usenet 下载。使用“添加 NZB…”将其加入队列。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>%1 download(s), %2 active — %3% complete</source>
        <translation>%1 个下载，%2 个活动 — 已完成 %3%</translation>
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
        <translation> — eD2K 下载时限速 %1 KB/s</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Download limit %1 KB/s: Usenet up to %2 KB/s, eD2K up to %3 KB/s.
Whichever network is idle lends its share to the other.</source>
        <translation>下载限速 %1 KB/s：Usenet 最高 %2 KB/s，eD2K 最高 %3 KB/s。
空闲的网络会把自己的份额让给另一方。</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>No Usenet downloads here.</source>
        <translation>此处没有 Usenet 下载。</translation>
    </message>
    <message numerus="yes">
        <location line="+48"/>
        <source>%n article(s) were never listed in the NZB</source>
        <translation>
            <numerusform>有 %n 篇文章从未列在 NZB 中</numerusform>
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
        <translation>应用语言（%1）</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>Any</source>
        <translation>任意</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Apply</source>
        <translation>应用</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Archive (.zip .rar ...)</source>
        <translation>压缩包 (.zip .rar ...)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Archive password for &quot;%1&quot;:</source>
        <translation>“%1”的压缩包密码：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Articles</source>
        <translation>文章</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Articles:</source>
        <translation>文章：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Audio (.mp3 .ogg ...)</source>
        <translation>音频 (.mp3 .ogg ...)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Back to the queue</source>
        <translation>返回队列</translation>
    </message>
    <message>
        <source>CD Image (.iso .bin ...)</source>
        <translation type="vanished">光盘映像 (.iso .bin ...)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Cancel this download?</source>
        <translation>取消此下载？</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Category</source>
        <translation>分类</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Category:</source>
        <translation>分类：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Category: %1</source>
        <translation>分类：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Check Availability</source>
        <translation>检查可用性</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Choose .nzb files or paste links first.</source>
        <translation>请先选择 .nzb 文件或粘贴链接。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Clear Completed</source>
        <translation>清除已完成</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Client</source>
        <translation>客户端</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Comment</source>
        <translation>评论</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Comments</source>
        <translation>评论</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Completed</source>
        <translation>已完成</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Confidence</source>
        <translation>可信度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Connect</source>
        <translation>连接</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Copy ED2K Link</source>
        <translation>复制 ED2K 链接</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Could not apply that to the category: %1</source>
        <translation>无法应用到该分类：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Could not reach the eMule core.</source>
        <translation>无法连接到 eMule 核心。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Date:</source>
        <translation>日期：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Debug</source>
        <translation>调试</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Debug Log</source>
        <translation>调试日志</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Description</source>
        <translation>描述</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Details…</source>
        <translation>详细信息…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Disconnect</source>
        <translation>断开连接</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Document (.doc .pdf ...)</source>
        <translation>文档 (.doc .pdf ...)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Download Speed</source>
        <translation>下载速度</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebServer.cpp" line="-1453"/>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>Downloads</source>
        <translation>下载</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>File</source>
        <translation>文件</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>File Name</source>
        <translation>文件名</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Files</source>
        <translation>文件</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Files:</source>
        <translation>文件数：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>General</source>
        <translation>常规</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Global</source>
        <translation>全局</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Graphs</source>
        <translation>图表</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hash</source>
        <translation>哈希</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Health</source>
        <translation>健康度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Health:</source>
        <translation>健康度：</translation>
    </message>
    <message>
        <source>Image (.jpg .png ...)</source>
        <translation type="vanished">图片 (.jpg .png ...)</translation>
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
        <translation>Kad 网络</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Kademlia</source>
        <translation>Kademlia</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Keep Scanning</source>
        <translation>继续扫描</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Language</source>
        <translation>语言</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Log</source>
        <translation>日志</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Login</source>
        <translation>登录</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Logout</source>
        <translation>注销</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Max Download (KB/s)</source>
        <translation>最大下载 (KB/s)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Max Download Speed</source>
        <translation>最大下载速度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Max Upload (KB/s)</source>
        <translation>最大上传 (KB/s)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Max Upload Speed</source>
        <translation>最大上传速度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Method</source>
        <translation>方式</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Missing</source>
        <translation>缺失</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>My Info</source>
        <translation>我的信息</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>NZB URLs:</source>
        <translation>NZB 链接：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>NZB files:</source>
        <translation>NZB 文件：</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebServer.cpp" line="-1215"/>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="-57"/>
        <source>CD-Image (.iso .bin ...)</source>
        <translation>CD 映像 (.iso .bin ...)</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Collection</source>
        <translation>合集</translation>
    </message>
    <message>
        <location line="+48"/>
        <source>Newsgroups:</source>
        <translation>新闻组：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nickname</source>
        <translation>昵称</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nickname:</source>
        <translation>昵称：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nothing has completed yet for &quot;%1&quot;.</source>
        <translation>“%1”尚无已完成的内容。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nothing here can be previewed yet.</source>
        <translation>这里还没有可以预览的内容。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Open File</source>
        <translation>打开文件</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Open Folder</source>
        <translation>打开文件夹</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Password</source>
        <translation>密码</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Password:</source>
        <translation>密码：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paste one or more http(s) links to .nzb files here, one per line...</source>
        <translation>在此粘贴一个或多个指向 .nzb 文件的 http(s) 链接，每行一个...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Pause</source>
        <translation>暂停</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Pause All</source>
        <translation>全部暂停</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Picture (.jpg .png ...)</source>
        <translation>图片 (.jpg .png ...)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ping</source>
        <translation>Ping</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Playable</source>
        <translation>可播放</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Please add at most %1 links at a time.</source>
        <translation>每次最多添加 %1 个链接。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Port</source>
        <translation>端口</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ports</source>
        <translation>端口</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Poster:</source>
        <translation>发布者：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Preferences</source>
        <translation>首选项</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Preview</source>
        <translation>预览</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Preview File</source>
        <translation>预览文件</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority</source>
        <translation>优先级</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority:</source>
        <translation>优先级：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority: %1</source>
        <translation>优先级：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority: Auto</source>
        <translation>优先级：自动</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority: High</source>
        <translation>优先级：高</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority: Low</source>
        <translation>优先级：低</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority: Normal</source>
        <translation>优先级：普通</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Program (.exe ...)</source>
        <translation>程序 (.exe ...)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Progress</source>
        <translation>进度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Queued %1 NZB(s).</source>
        <translation>已将 %1 个 NZB 加入队列。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Rating</source>
        <translation>评分</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Recheck Firewall</source>
        <translation>重新检查防火墙</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Reconnects</source>
        <translation>重新连接</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Refresh</source>
        <translation>刷新</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remaining</source>
        <translation>剩余</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove</source>
        <translation>删除</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove %1 download(s) and delete the files already fetched?</source>
        <translation>移除 %1 个下载并删除已获取的文件？</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove and Delete Files</source>
        <translation>移除并删除文件</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove every Usenet download in &quot;%1&quot; and delete its files?</source>
        <translation>移除“%1”中的所有 Usenet 下载并删除其文件？</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove from Static</source>
        <translation>从静态列表中移除</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove the stored password for this download?</source>
        <translation>移除此下载已保存的密码？</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Requests</source>
        <translation>请求</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Resume</source>
        <translation>恢复</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Resume All</source>
        <translation>全部恢复</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Search</source>
        <translation>搜索</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server</source>
        <translation>服务器</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server Info</source>
        <translation>服务器信息</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server List</source>
        <translation>服务器列表</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server Name</source>
        <translation>服务器名称</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Session Received</source>
        <translation>本次会话接收</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Session Sent</source>
        <translation>本次会话发送</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Session Statistics</source>
        <translation>会话统计</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Set Password…</source>
        <translation>设置密码…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Shared</source>
        <translation>已共享</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Shared Files</source>
        <translation>共享文件</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebServer.cpp" line="+0"/>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>Sources</source>
        <translation>来源</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Speed</source>
        <translation>速度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Speed Limits</source>
        <translation>速度限制</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Statistics</source>
        <translation>统计</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status</source>
        <translation>状态</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status:</source>
        <translation>状态：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Stop</source>
        <translation>停止</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>TCP Port</source>
        <translation>TCP 端口</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>That did not work.</source>
        <translation>操作未成功。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>These could not be added: %1</source>
        <translation>以下项目无法添加：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>This archive cannot be previewed.</source>
        <translation>无法预览此压缩包。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>This download is no longer in the queue.</source>
        <translation>此下载已不在队列中。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total</source>
        <translation>总计</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Size:</source>
        <translation>总大小：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Transfer</source>
        <translation>传输</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Transferred</source>
        <translation>已传输</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type</source>
        <translation>类型</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>UDP Port</source>
        <translation>UDP 端口</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload Speed</source>
        <translation>上传速度</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebServer.cpp" line="+1212"/>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>Uploads</source>
        <translation>上传</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>Uptime</source>
        <translation>运行时间</translation>
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
        <translation>Usenet 引擎不可用</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>User</source>
        <translation>用户</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>User Information</source>
        <translation>用户信息</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>User Name</source>
        <translation>用户名</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Users</source>
        <translation>用户</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Video (.avi .mkv ...)</source>
        <translation>视频 (.avi .mkv ...)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>You have already downloaded %1 of these. Download them again?</source>
        <translation>其中 %1 个已下载过。要重新下载吗？</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>this download</source>
        <translation>此下载</translation>
    </message>
</context>
</TS>
