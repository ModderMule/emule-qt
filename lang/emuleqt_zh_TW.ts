<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_TW">
<context>
    <name>ContainerSniffer</name>
    <message>
        <location filename="../src/core/media/ContainerSniffer.cpp" line="+175"/>
        <source>Named .%1 but matches no media container we recognise — very likely a fake.</source>
        <translation>檔名為 .%1，但與我們能辨識的任何媒體容器都不符 — 極可能是假檔案。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Named .%1 but the contents are %2.</source>
        <translation>檔名為 .%1，但內容實際上是 %2。</translation>
    </message>
</context>
<context>
    <name>Ed2kLinkImporter</name>
    <message>
        <location filename="../src/gui/utils/Ed2kLinkImporter.cpp" line="+203"/>
        <source>already shared</source>
        <translation>已分享</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>already downloading</source>
        <translation>已在下載中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>already downloaded</source>
        <translation>已下載</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>previously cancelled</source>
        <translation>先前已取消</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>You already have the file &quot;%1&quot;.</source>
        <translation>您已擁有檔案 &quot;%1&quot;。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>You are already trying to download the file &quot;%1&quot;.</source>
        <translation>您已經在嘗試下載檔案 &quot;%1&quot;。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>You previously cancelled the download of &quot;%1&quot;.</source>
        <translation>您先前已取消 &quot;%1&quot; 的下載。</translation>
    </message>
    <message numerus="yes">
        <location line="+126"/>
        <source>%n further HTTP Cache link(s) ignored — apply one at a time.</source>
        <translation>
            <numerusform>已忽略另外 %n 個 HTTP 快取連結 — 請一次套用一個。</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="+104"/>
        <source>%n eD2K link(s) not added — already known</source>
        <translation>
            <numerusform>%n 個 eD2K 連結未加入 — 已知</numerusform>
        </translation>
    </message>
    <message>
        <location line="+9"/>
        <source>eD2K Link</source>
        <translation>eD2K 連結</translation>
    </message>
</context>
<context>
    <name>HttpCacheLinkImporter</name>
    <message>
        <location filename="../src/gui/utils/HttpCacheLinkImporter.cpp" line="+44"/>
        <source>The core did not answer.</source>
        <translation>核心沒有回應。</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>Server: %1</source>
        <translation>伺服器：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Key: %1</source>
        <translation>金鑰：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Version %1%2</source>
        <translation>版本 %1%2</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>This server also accepts uploads without a key.</source>
        <translation>此伺服器也接受不帶金鑰的上傳。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>This replaces the entry already stored for %1.</source>
        <translation>這將取代已為 %1 儲存的項目。</translation>
    </message>
    <message numerus="yes">
        <location line="+3"/>
        <source>Uploads are shared across your cache servers; this makes %n of them.</source>
        <translation>
            <numerusform>上傳會分散到各個快取伺服器；這樣將共有 %n 個。</numerusform>
        </translation>
    </message>
    <message>
        <location line="+10"/>
        <source>

This link uses plain HTTP. The key and every chunk address will cross the network unencrypted.</source>
        <translation>

此連結使用明文 HTTP。金鑰和每個區塊的位址都將以未加密的方式在網路上傳輸。</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Update your HTTP Cache settings for &quot;%1&quot;?</source>
        <translation>要更新 &quot;%1&quot; 的 HTTP 快取設定嗎？</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Add &quot;%1&quot; as an HTTP Cache server?</source>
        <translation>要將 &quot;%1&quot; 加入為 HTTP 快取伺服器嗎？</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Use &quot;%1&quot; as your HTTP Cache server?</source>
        <translation>要將 &quot;%1&quot; 用作您的 HTTP 快取伺服器嗎？</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+26"/>
        <source>HTTP Cache</source>
        <translation>HTTP 快取</translation>
    </message>
    <message>
        <location line="-23"/>
        <source>

HTTP Cache will be enabled and this key stored for uploads.</source>
        <translation>

將啟用 HTTP 快取，並儲存此金鑰用於上傳。</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>HTTP Cache link refused: %1</source>
        <translation>已拒絕 HTTP 快取連結：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+29"/>
        <source>HTTP Cache link refused</source>
        <translation>已拒絕 HTTP 快取連結</translation>
    </message>
    <message>
        <location line="-16"/>
        <location line="+2"/>
        <source>HTTP Cache is already configured for %1.</source>
        <translation>%1 的 HTTP 快取已設定。</translation>
    </message>
    <message numerus="yes">
        <location line="+10"/>
        <source>You already have %n HTTP Cache server(s) configured. Remove one from preferences.yml before adding another.</source>
        <translation>
            <numerusform>您已設定 %n 個 HTTP 快取伺服器。請先從 preferences.yml 中移除一個，再加入新的伺服器。</numerusform>
        </translation>
    </message>
    <message>
        <location line="+17"/>
        <source>HTTP Cache configuration for %1 was not applied.</source>
        <translation>未套用 %1 的 HTTP 快取設定。</translation>
    </message>
    <message>
        <location line="+22"/>
        <location line="+2"/>
        <source>HTTP Cache configured for %1.</source>
        <translation>已為 %1 設定 HTTP 快取。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>HTTP Cache configuration failed: %1</source>
        <translation>HTTP 快取設定失敗：%1</translation>
    </message>
</context>
<context>
    <name>IpcFeedback</name>
    <message>
        <location filename="../src/gui/utils/IpcFeedback.cpp" line="+28"/>
        <source>The request was rejected by eMule.</source>
        <translation>請求被 eMule 拒絕。</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Not connected to daemon — %1</source>
        <translation>未連線到背景服務 — %1</translation>
    </message>
</context>
<context>
    <name>Notifier</name>
    <message>
        <location filename="../src/gui/utils/NotifierText.h" line="+30"/>
        <source>Downloaded:</source>
        <translation>已下載：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Connection lost</source>
        <translation>連線已中斷</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Out of disk space</source>
        <translation>磁碟空間不足</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>You have insufficient disk space to download &quot;%1&quot;!</source>
        <translation>磁碟空間不足，無法下載「%1」！</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Port not available</source>
        <translation>連接埠無法使用</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Fatal Error: Unable to create socket on port %1</source>
        <translation>嚴重錯誤：無法在連接埠 %1 上建立通訊端</translation>
    </message>
</context>
<context>
    <name>PortChange</name>
    <message>
        <location filename="../src/gui/dialogs/PortChangeNotice.h" line="+42"/>
        <source>Now listening on TCP port %1 and UDP port %2.</source>
        <translation>現在監聽 TCP 連接埠 %1 和 UDP 連接埠 %2。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The new ports take effect after restarting eMule, because the core is connected to a network or to other clients.

Until then it keeps listening on TCP port %1 and UDP port %2.</source>
        <translation>由於核心已連線到網路或其他客戶端，新連接埠將在重新啟動 eMule 後生效。

在此之前，它會繼續監聽 TCP 連接埠 %1 和 UDP 連接埠 %2。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>The new port could not be opened. It may be in use by another program.

The core keeps listening on TCP port %1 and UDP port %2.</source>
        <translation>無法開啟新連接埠。它可能正被其他程式使用。

核心會繼續監聽 TCP 連接埠 %1 和 UDP 連接埠 %2。</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Ports</source>
        <translation>連接埠</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Restart Now</source>
        <translation>立即重新啟動</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Later</source>
        <translation>稍後</translation>
    </message>
</context>
<context>
    <name>PortMapStatus</name>
    <message>
        <location filename="../src/gui/dialogs/PortMapStatusText.h" line="+37"/>
        <source>Ports forwarded via %1.</source>
        <translation>已透過 %1 轉送連接埠。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ports forwarded via %1 (external address %2).</source>
        <translation>已透過 %1 轉送連接埠（外部位址 %2）。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>The router granted the ports, but they are not reachable from the Internet.</source>
        <translation>路由器已授予連接埠，但無法從網際網路存取。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The router granted the ports, but its address %1 is not public, so they stay unreachable over IPv4.</source>
        <translation>路由器已授予連接埠，但其位址 %1 不是公開位址，因此透過 IPv4 仍無法存取。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>No router answered PCP, NAT-PMP or UPnP. Forward the ports manually.</source>
        <translation>沒有路由器回應 PCP、NAT-PMP 或 UPnP。請手動轉送連接埠。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>The router refused the port forwarding.</source>
        <translation>路由器拒絕了連接埠轉送。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Automatic port forwarding is switched off.</source>
        <translation>自動連接埠轉送已關閉。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Asking the router...</source>
        <translation>正在詢問路由器...</translation>
    </message>
</context>
<context>
    <name>PreviewLauncher</name>
    <message>
        <location filename="../src/gui/utils/PreviewLauncher.cpp" line="+271"/>
        <source>Not connected to the core.</source>
        <translation>未連線到核心。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The core has not sent its stream token yet. It arrives with the next status update — try again in a moment.</source>
        <translation>核心尚未傳送串流權杖。它會隨下一次狀態更新送達 — 請稍後再試。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>The core runs on another machine and its web server only listens on localhost.

Enable Web Interface or REST API under Options → Web Interface.</source>
        <translation>核心執行於另一台電腦上，其 Web 伺服器僅監聽 localhost。

請在「選項 → Web 介面」中啟用 Web 介面或 REST API。</translation>
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
        <translation>自動 [低]</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Low</source>
        <translation>低</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Auto [Hi]</source>
        <translation>自動 [高]</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>High</source>
        <translation>高</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Auto [No]</source>
        <translation>自動 [普]</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Normal</source>
        <translation>普通</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Release</source>
        <translation>發佈</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Very High</source>
        <translation>非常高</translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <location filename="../src/gui/app/main.cpp" line="+336"/>
        <source>eMule Qt</source>
        <translation>eMule Qt</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>eMule Qt is already running with this configuration but does not respond.

Start it with --new-instance or --config &lt;dir&gt; to open another window.</source>
        <translation>eMule Qt 已使用此設定執行，但沒有回應。

使用 --new-instance 或 --config &lt;目錄&gt; 啟動以開啟另一個視窗。</translation>
    </message>
    <message>
        <location line="+466"/>
        <source>Download Added</source>
        <translation>已加入下載</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>A new download has been added.</source>
        <translation>已加入新的下載。</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Message from %1</source>
        <translation>來自 %1 的訊息</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/ClientListModel.cpp" line="+56"/>
        <source>Server</source>
        <translation>伺服器</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Kad</source>
        <translation>Kad</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Source Exch.</source>
        <translation>來源交換</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/gui/controls/DownloadListModel.cpp" line="+70"/>
        <source>Passive</source>
        <translation>被動</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/gui/controls/DownloadListModel.cpp" line="+1"/>
        <source>Link</source>
        <translation>連結</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/gui/controls/DownloadListModel.cpp" line="+1"/>
        <source>SLS</source>
        <translation>SLS</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/gui/controls/DownloadListModel.cpp" line="+1"/>
        <source>HTTP Cache</source>
        <translation>HTTP 快取</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/DownloadListModel.cpp" line="-6"/>
        <source>eD2K Server</source>
        <translation>eD2K 伺服器</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Kademlia</source>
        <translation>Kademlia</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Source Exchange</source>
        <translation>來源交換</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Unknown</source>
        <translation>未知</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/KnownTypeStyle.h" line="+24"/>
        <source>Shared</source>
        <translation>已分享</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/gui/controls/UsenetQueueModel.cpp" line="+209"/>
        <source>Downloading</source>
        <translation>下載中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloaded</source>
        <translation>已下載</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Cancelled</source>
        <translation>已取消</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/UsenetQueueModel.cpp" line="-8"/>
        <source>Skipped</source>
        <translation>已略過</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Held back — fetched if a repair needs it</source>
        <translation>暫緩取得 — 僅在修復需要時才下載</translation>
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
        <translation>佇列中</translation>
    </message>
    <message>
        <location line="+430"/>
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
        <location filename="../src/gui/dialogs/OptionsDialog.cpp" line="+3090"/>
        <source>Test</source>
        <translation>測試</translation>
    </message>
    <message>
        <location filename="../src/gui/panels/StatisticsPanel.cpp" line="+342"/>
        <source>Total Overhead (Packets): 0 Bytes (0)</source>
        <translation>總額外負擔 (封包)：0 Bytes (0)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>File Request Overhead (Packets): 0 Bytes (0)</source>
        <translation>檔案要求額外負擔 (封包)：0 Bytes (0)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Source Exchange Overhead (Packets): 0 Bytes (0)</source>
        <translation>來源交換額外負擔 (封包)：0 Bytes (0)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server Overhead (Packets): 0 Bytes (0)</source>
        <translation>伺服器額外負擔 (封包)：0 Bytes (0)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Kad Overhead (Packets): 0 Bytes (0)</source>
        <translation>Kad 額外負擔 (封包)：0 Bytes (0)</translation>
    </message>
    <message>
        <location filename="../src/gui/utils/Ed2kLinkImporter.cpp" line="-329"/>
        <location line="+20"/>
        <source>eD2K Link</source>
        <translation>eD2K 連結</translation>
    </message>
    <message>
        <location line="-19"/>
        <source>Do you want to download the following file(s)?

%1</source>
        <translation>是否要下載以下檔案？

%1</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>You have already downloaded the following file(s). Download them again?

%1</source>
        <translation>您已經下載過以下檔案。要重新下載嗎？

%1</translation>
    </message>
    <message>
        <location filename="../src/gui/utils/FileAssociation.cpp" line="+258"/>
        <source>Could not write the file association to the registry.</source>
        <translation>無法將檔案關聯寫入登錄檔。</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Could not remove the file association from the registry.</source>
        <translation>無法從登錄檔中移除檔案關聯。</translation>
    </message>
    <message>
        <location line="+15"/>
        <location line="+31"/>
        <source>No writable data directory.</source>
        <translation>沒有可寫入的資料目錄。</translation>
    </message>
    <message>
        <location line="-23"/>
        <source>Could not create %1.</source>
        <translation>無法建立 %1。</translation>
    </message>
    <message>
        <location filename="../src/gui/utils/NzbAdd.cpp" line="+25"/>
        <source>Could not add &quot;%1&quot;.</source>
        <translation>無法加入「%1」。</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+42"/>
        <location line="+10"/>
        <source>Add NZB</source>
        <translation>加入 NZB</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1

Download it again?</source>
        <translation>%1

要重新下載嗎？</translation>
    </message>
    <message>
        <location filename="../src/gui/utils/RatingIcons.cpp" line="+59"/>
        <source>Fake</source>
        <translation>假檔</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Poor</source>
        <translation>差</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Fair</source>
        <translation>普通</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Good</source>
        <translation>良好</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Excellent</source>
        <translation>優秀</translation>
    </message>
    <message>
        <location line="+110"/>
        <source>
Rating:	%1</source>
        <translation>
評分：	%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>
Has comments</source>
        <translation>
有評論</translation>
    </message>
</context>
<context>
    <name>Rating</name>
    <message>
        <location filename="../src/core/utils/OtherFunctions.cpp" line="+528"/>
        <source>Not rated</source>
        <translation>未評分</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Invalid / Corrupt / Fake</source>
        <translation>無效 / 損壞 / 假檔</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Poor</source>
        <translation>差</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Fair</source>
        <translation>普通</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Good</source>
        <translation>良好</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Excellent</source>
        <translation>優秀</translation>
    </message>
</context>
<context>
    <name>RenameFileDialog</name>
    <message>
        <location filename="../src/gui/dialogs/RenameFileDialog.h" line="+23"/>
        <source>Rename</source>
        <translation>重新命名</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>File name:</source>
        <translation>檔案名稱：</translation>
    </message>
</context>
<context>
    <name>Units</name>
    <message>
        <location filename="../src/core/utils/StringUtils.cpp" line="+72"/>
        <location filename="../src/core/webserver/WebServer.cpp" line="+4028"/>
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
        <translation>時</translation>
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
        <translation>分鐘</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hours</source>
        <translation>小時</translation>
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
            <numerusform>%1（另有 %n 個）</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>eMule::AddFriendDialog</name>
    <message>
        <location filename="../src/gui/dialogs/AddFriendDialog.cpp" line="+28"/>
        <source>Add...</source>
        <translation>加入...</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Required Information</source>
        <translation>必填資訊</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>IP Address:</source>
        <translation>IP 位址：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Port:</source>
        <translation>連接埠：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Additional Information</source>
        <translation>附加資訊</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Name:</source>
        <translation>名稱：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Hash:</source>
        <translation>切細值：</translation>
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
        <location line="+7"/>
        <source>Last Seen:</source>
        <translation>最後出現：</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Add</source>
        <translation>加入</translation>
    </message>
    <message>
        <location line="+86"/>
        <source>Details</source>
        <translation>詳細資料</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>User ID:</source>
        <translation>使用者 ID：</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Never</source>
        <translation>從不</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Close</source>
        <translation>關閉</translation>
    </message>
    <message>
        <location line="+14"/>
        <location line="+24"/>
        <source>Add Friend</source>
        <translation>加入好友</translation>
    </message>
    <message>
        <location line="-24"/>
        <source>You have to enter a valid IP and port!</source>
        <translation>必須輸入有效的 IP 和連接埠！</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Friend not added.

There is already a friend with same IP address and port available.</source>
        <translation>未加入好友。

已存在具有相同 IP 位址和連接埠的好友。</translation>
    </message>
</context>
<context>
    <name>eMule::AddNzbFilesDialog</name>
    <message>
        <location filename="../src/gui/dialogs/AddNzbFilesDialog.cpp" line="+15"/>
        <source>Add NZB</source>
        <translation>加入 NZB</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>NZB files:</source>
        <translation>NZB 檔案：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Add</source>
        <translation>加入</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Password:</source>
        <translation>密碼：</translation>
    </message>
    <message>
        <location line="+18"/>
        <location line="+31"/>
        <source>Choose Files…</source>
        <translation>選擇檔案…</translation>
    </message>
    <message numerus="yes">
        <location line="-1"/>
        <source>Choose Files… (%n skipped)</source>
        <translation>
            <numerusform>選擇檔案…（已略過 %n 個）</numerusform>
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
        <translation>從 URL 加入 NZB</translation>
    </message>
    <message>
        <location line="-130"/>
        <source>NZB URLs:</source>
        <translation>NZB 連結：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paste one or more http(s) links to .nzb files here, one per line...</source>
        <translation>在此貼上一個或多個指向 .nzb 檔案的 http(s) 連結，每行一個...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Download</source>
        <translation>下載</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Password:</source>
        <translation>密碼：</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Not Connected</source>
        <translation>未連線</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not connected to the eMule core.</source>
        <translation>未連線到 eMule 核心。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Please add at most %1 links at a time.</source>
        <translation>每次最多加入 %1 個連結。</translation>
    </message>
    <message numerus="yes">
        <location line="+23"/>
        <source>Queued %n NZB(s) from URL.</source>
        <translation>
            <numerusform>已從 URL 將 %n 個 NZB 加入佇列。</numerusform>
        </translation>
    </message>
    <message>
        <location line="+8"/>
        <source>These links could not be added:

%1</source>
        <translation>以下連結無法加入：

%1</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Could not reach the eMule core.</source>
        <translation>無法連線到 eMule 核心。</translation>
    </message>
    <message numerus="yes">
        <location line="+42"/>
        <source>You have already downloaded %n of these. Download them again?

%1</source>
        <translation>
            <numerusform>其中 %n 個您已下載過。要重新下載嗎？

%1</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>eMule::AddSourceDialog</name>
    <message>
        <location filename="../src/gui/dialogs/AddSourceDialog.cpp" line="+23"/>
        <source>Add Sources</source>
        <translation>加入來源</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add Sources (%1)</source>
        <translation>加入來源 (%1)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Source Type</source>
        <translation>來源類型</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Source</source>
        <translation>來源</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>User IP</source>
        <translation>使用者 IP</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Port</source>
        <translation>連接埠</translation>
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
        <translation>加入</translation>
    </message>
</context>
<context>
    <name>eMule::ArchivePreviewPanel</name>
    <message>
        <location filename="../src/gui/dialogs/ArchivePreviewPanel.cpp" line="+80"/>
        <source>Scanning archive...</source>
        <translation>正在掃描壓縮檔...</translation>
    </message>
    <message>
        <location line="+38"/>
        <source>bootable</source>
        <translation>可開機</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+35"/>
        <location line="+17"/>
        <source>Password protection</source>
        <translation>密碼保護</translation>
    </message>
    <message>
        <location line="-52"/>
        <location line="+161"/>
        <source>Comment</source>
        <translation>評論</translation>
    </message>
    <message>
        <location line="-146"/>
        <location line="+38"/>
        <source>Archive scanned.</source>
        <translation>壓縮檔掃描完成。</translation>
    </message>
    <message>
        <location line="-37"/>
        <location line="+37"/>
        <source>File list may be incomplete.</source>
        <translation>檔案清單可能不完整。</translation>
    </message>
    <message>
        <location line="-36"/>
        <location line="+15"/>
        <source>Headers encrypted - unable to read archive.</source>
        <translation>檔頭已加密 - 無法讀取壓縮檔。</translation>
    </message>
    <message>
        <location line="-14"/>
        <location line="+6"/>
        <source>Insufficient data available.</source>
        <translation>可用資料不足。</translation>
    </message>
    <message>
        <location line="-5"/>
        <location line="+14"/>
        <source>Table of contents not found.</source>
        <translation>找不到目錄。</translation>
    </message>
    <message>
        <location line="+27"/>
        <location line="+76"/>
        <source>Archive type: --</source>
        <translation>壓縮檔類型：--</translation>
    </message>
    <message>
        <location line="-75"/>
        <location line="+79"/>
        <source>Ready</source>
        <translation>就緒</translation>
    </message>
    <message>
        <location line="-23"/>
        <source>Archive type: %1</source>
        <translation>壓縮檔類型：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>(unknown/unsupported)</source>
        <translation>(未知/不支援)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Files: %1</source>
        <translation>檔案：%1</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Create preview file</source>
        <translation>建立預覽檔案</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Update</source>
        <translation>更新</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Name</source>
        <translation>名稱</translation>
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
        <translation>屬性</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Last Modified</source>
        <translation>最後修改</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Info:</source>
        <translation>資訊：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Archive Preview</source>
        <translation>壓縮檔預覽</translation>
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
        <translation>檔案屬性可包含：

P	檔案受密碼保護
D	是目錄
C	註解
Lx	壓縮等級 x
M	壓縮資料尚未完全下載
&lt;	檔案接續自上一分割卷
&gt;	檔案在下一分割卷繼續
</translation>
    </message>
</context>
<context>
    <name>eMule::BugReportDialog</name>
    <message>
        <location filename="../src/gui/dialogs/BugReportDialog.cpp" line="+55"/>
        <location line="+133"/>
        <location line="+128"/>
        <source>Submit Bug Report</source>
        <translation>提交錯誤報告</translation>
    </message>
    <message>
        <location line="-256"/>
        <source>Report Details</source>
        <translation>報告詳細資料</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Bug Report</source>
        <translation>錯誤報告</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Feature Request</source>
        <translation>功能請求</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type:</source>
        <translation>類型：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Brief summary of the issue</source>
        <translation>問題簡述</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Title:</source>
        <translation>標題：</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+4"/>
        <source>(optional)</source>
        <translation>（選填）</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>Name:</source>
        <translation>名稱：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Email:</source>
        <translation>電子郵件：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Description</source>
        <translation>描述</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Describe the issue in detail...</source>
        <translation>詳細描述問題...</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Attachments</source>
        <translation>附件</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Screenshots:</source>
        <translation>螢幕擷圖：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Add...</source>
        <translation>加入...</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+13"/>
        <source>Remove</source>
        <translation>移除</translation>
    </message>
    <message>
        <location line="-6"/>
        <source>Crash Dump:</source>
        <translation>當機傾印：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Browse...</source>
        <translation>瀏覽...</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Alternatively, you can submit bug reports at &lt;a href=&quot;%1&quot;&gt;emule-qt.org/submit-bug-report&lt;/a&gt;</source>
        <translation>您也可以在 &lt;a href=&quot;%1&quot;&gt;emule-qt.org/submit-bug-report&lt;/a&gt; 提交錯誤報告</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Submit</source>
        <translation>提交</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Please fill in both the title and description fields.</source>
        <translation>請填寫標題和描述兩個欄位。</translation>
    </message>
    <message>
        <location line="+123"/>
        <source>Bug report submitted successfully.</source>
        <translation>錯誤報告已成功提交。</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Error: %1</source>
        <translation>錯誤：%1</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Select Screenshots</source>
        <translation>選擇螢幕擷圖</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp)</source>
        <translation>圖片 (*.png *.jpg *.jpeg *.bmp *.gif *.webp)</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Invalid Files</source>
        <translation>無效的檔案</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The following files are not valid images and were skipped:
%1</source>
        <translation>以下檔案不是有效的圖片，已略過：
%1</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Select Crash Dump</source>
        <translation>選擇當機傾印</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Dump Files (*.dmp *.crash *.txt);;All Files (*)</source>
        <translation>傾印檔案 (*.dmp *.crash *.txt);;所有檔案 (*)</translation>
    </message>
</context>
<context>
    <name>eMule::CategoryDialog</name>
    <message>
        <location filename="../src/gui/dialogs/CategoryDialog.cpp" line="+36"/>
        <source>Edit Category-Properties</source>
        <translation>編輯分類屬性</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Title</source>
        <translation>標題</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Comments</source>
        <translation>評論</translation>
    </message>
    <message>
        <location line="+13"/>
        <location line="+79"/>
        <source>Choose a folder for incoming files</source>
        <translation>選擇新進檔案的資料夾</translation>
    </message>
    <message>
        <location line="-75"/>
        <source>Incoming Files  (Folder will be shared!)</source>
        <translation>新進檔案  (資料夾將被分享！)</translation>
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
        <translation>此分類的優先權</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+151"/>
        <source>Default</source>
        <translation>預設</translation>
    </message>
    <message>
        <location line="-147"/>
        <source>More Colors...</source>
        <translation>更多色彩...</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+59"/>
        <source>Color</source>
        <translation>色彩</translation>
    </message>
    <message>
        <location line="-56"/>
        <source>Auto cat. assignment (separate patterns with |)</source>
        <translation>自動分類指派 (以 | 分隔多個樣式)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>As Regular Expression</source>
        <translation>作為規則運算式</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Shows only the files whose whole name matches. Clearing it turns the filter off again.</source>
        <translation>僅顯示整個名稱相符的檔案。清空後將再次關閉過濾器。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Regular expression for view filter:</source>
        <translation>檢視過濾器的規則運算式：</translation>
    </message>
    <message>
        <location line="+55"/>
        <source>A category needs a title.</source>
        <translation>分類需要一個標題。</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Invalid folder. Folder can not be created. Please check name and location.</source>
        <translation>無效的資料夾。無法建立此資料夾。請檢查名稱和位置。</translation>
    </message>
    <message>
        <location line="+10"/>
        <location line="+6"/>
        <source>Bad regular expression</source>
        <translation>無效的規則運算式</translation>
    </message>
</context>
<context>
    <name>eMule::CategoryTabBar</name>
    <message>
        <location filename="../src/gui/controls/CategoryTabBar.cpp" line="+32"/>
        <location line="+47"/>
        <location line="+63"/>
        <location line="+20"/>
        <location line="+37"/>
        <source>All</source>
        <translation>全部</translation>
    </message>
    <message>
        <location line="-113"/>
        <source>Cat %1</source>
        <translation>分類 %1</translation>
    </message>
    <message>
        <location line="+77"/>
        <source>Uncategorized</source>
        <translation>未分類</translation>
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
        <translation>下載中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Erroneous</source>
        <translation>錯誤</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paused</source>
        <translation>已暫停</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ever Seen Complete</source>
        <translation>曾見過完整</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Video</source>
        <translation>影像</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Audio</source>
        <translation>音樂</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Archive</source>
        <translation>壓縮檔</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>CD-Image</source>
        <translation>光碟映像</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Document</source>
        <translation>文件</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Picture</source>
        <translation>圖片</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Program</source>
        <translation>程式</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Regular Expression</source>
        <translation>規則運算式</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Collection</source>
        <translation>收藏集</translation>
    </message>
    <message>
        <location line="+69"/>
        <source>Select View Filter</source>
        <translation>選擇檢視過濾器</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Evaluate All Files</source>
        <translation>評估所有檔案</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Negate Filter</source>
        <translation>反轉過濾器</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Category</source>
        <translation>分類</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Category (%1)</source>
        <translation>分類 (%1)</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Open Incoming Folder</source>
        <translation>開啟新進檔案資料夾</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Add Category...</source>
        <translation>加入分類...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Edit Category...</source>
        <translation>編輯分類...</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+159"/>
        <source>Remove Category</source>
        <translation>移除分類</translation>
    </message>
    <message>
        <location line="-61"/>
        <source>Could not save categories: %1</source>
        <translation>無法儲存分類：%1</translation>
    </message>
    <message>
        <location line="+62"/>
        <source>Remove the category &quot;%1&quot;?

Its downloads keep their files and move to All.</source>
        <translation>移除分類「%1」？

其下載會保留檔案並移至「全部」。</translation>
    </message>
</context>
<context>
    <name>eMule::ClientDetailDialog</name>
    <message>
        <location filename="../src/gui/dialogs/ClientDetailDialog.cpp" line="+65"/>
        <source>Client Details: %1</source>
        <translation>客戶端詳細資料：%1</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>General</source>
        <translation>一般</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Name</source>
        <translation>名稱</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hash</source>
        <translation>切細值</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>ID</source>
        <translation>ID</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Low ID</source>
        <translation>低-ID</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>High ID</source>
        <translation>高-ID</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Software</source>
        <translation>軟體</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Country</source>
        <translation>國家</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Server IP</source>
        <translation>伺服器 IP</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server Name</source>
        <translation>伺服器名稱</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Identification</source>
        <translation>識別</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Successful</source>
        <translation>成功</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Invalid</source>
        <translation>無效</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+5"/>
        <source>Not supported or disabled</source>
        <translation>不支援或已停用</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>Obfuscation</source>
        <translation>混淆</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Enabled</source>
        <translation>已啟用</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Supported</source>
        <translation>支援</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Kad</source>
        <translation>Kad</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Connected</source>
        <translation>已連線</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Disconnected</source>
        <translation>已中斷連線</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Transfer</source>
        <translation>傳輸</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Currently downloading</source>
        <translation>目前正在下載</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Requested to upload</source>
        <translation>要求上傳</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Downloaded (this session)</source>
        <translation>已下載 (本次工作階段)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Uploaded (this session)</source>
        <translation>已上傳 (本次工作階段)</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Average Downloadrate</source>
        <translation>平均下載速度</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Average Upload rate</source>
        <translation>平均上傳速度</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Downloaded total</source>
        <translation>總下載量</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Uploaded total</source>
        <translation>總上傳量</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Scores</source>
        <translation>評分</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>DL/UP Modifier</source>
        <translation>下載/上傳修正係數</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Rating (total)</source>
        <translation>評分 (總計)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>(Friend Slot)</source>
        <translation>(好友位置)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Uploadqueue Score</source>
        <translation>上傳佇列分數</translation>
    </message>
</context>
<context>
    <name>eMule::ClientListModel</name>
    <message>
        <location filename="../src/gui/controls/ClientListModel.cpp" line="+85"/>
        <source>Country</source>
        <translation>國家</translation>
    </message>
    <message>
        <location line="+25"/>
        <location line="+36"/>
        <source>Low ID</source>
        <translation>低-ID</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+15"/>
        <source>Yes</source>
        <translation>是</translation>
    </message>
    <message>
        <location line="-15"/>
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
        <location line="+78"/>
        <location line="+13"/>
        <location line="+13"/>
        <location line="+15"/>
        <source>User Name</source>
        <translation>使用者名稱</translation>
    </message>
    <message>
        <location line="-40"/>
        <location line="+14"/>
        <location line="+12"/>
        <source>File</source>
        <translation>檔案</translation>
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
        <translation>已傳輸</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Waited</source>
        <translation>已等待</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload Time</source>
        <translation>上傳時間</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status</source>
        <translation>狀態</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+28"/>
        <source>Obtained Parts</source>
        <translation>已取得部分</translation>
    </message>
    <message>
        <location line="-21"/>
        <location line="+32"/>
        <source>Software</source>
        <translation>軟體</translation>
    </message>
    <message>
        <location line="-29"/>
        <source>Available Parts</source>
        <translation>可用部分</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+27"/>
        <source>Transferred Down</source>
        <translation>已下載</translation>
    </message>
    <message>
        <location line="-26"/>
        <location line="+24"/>
        <source>Transferred Up</source>
        <translation>已上傳</translation>
    </message>
    <message>
        <location line="-23"/>
        <source>Source Type</source>
        <translation>來源類型</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>File Priority</source>
        <translation>檔案優先權</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Rating</source>
        <translation>評分</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Score</source>
        <translation>分數</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Asked</source>
        <translation>已要求</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Last Seen</source>
        <translation>最後出現</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Entered Queue</source>
        <translation>進入佇列</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Banned</source>
        <translation>已封鎖</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Upload Status</source>
        <translation>上傳狀態</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Download Status</source>
        <translation>下載狀態</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Connected</source>
        <translation>已連線</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hash</source>
        <translation>切細值</translation>
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
        <translation>修改收藏集...</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Create Collection...</source>
        <translation>建立收藏集...</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+197"/>
        <source>Shared (%1)</source>
        <translation>已分享 (%1)</translation>
    </message>
    <message>
        <location line="-195"/>
        <source>Switch between the shared files and every file known</source>
        <translation>在已分享檔案和所有已知檔案之間切換</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+36"/>
        <source>File Name</source>
        <translation>檔案名稱</translation>
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
        <translation>切細值</translation>
    </message>
    <message>
        <location line="-20"/>
        <source>Add to collection</source>
        <translation>加入到收藏集</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Remove from collection</source>
        <translation>從收藏集中移除</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Collection List (0)</source>
        <translation>收藏集清單 (0)</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Basic Options</source>
        <translation>基本選項</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Name:</source>
        <translation>名稱：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Advanced Options</source>
        <translation>進階選項</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Save collection in plain text format</source>
        <translation>以純文字格式儲存收藏集</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Sign collection with name and key</source>
        <translation>使用名稱和金鑰簽署收藏集</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Save</source>
        <translation>儲存</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="+102"/>
        <source>Known (%1)</source>
        <translation>已知 (%1)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Collection List (%1)</source>
        <translation>收藏集清單 (%1)</translation>
    </message>
    <message>
        <location line="+26"/>
        <location line="+6"/>
        <location line="+42"/>
        <location line="+9"/>
        <source>Collection</source>
        <translation>收藏集</translation>
    </message>
    <message>
        <location line="-57"/>
        <source>Please enter a collection name.</source>
        <translation>請輸入收藏集名稱。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Collection is empty. Add files first.</source>
        <translation>收藏集是空的。請先加入檔案。</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Do you want to replace existing file?</source>
        <translation>是否要取代現有的檔案？</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Failed to save collection: %1</source>
        <translation>儲存收藏集失敗：%1</translation>
    </message>
</context>
<context>
    <name>eMule::CollectionViewDialog</name>
    <message>
        <location filename="../src/gui/dialogs/CollectionViewDialog.cpp" line="+43"/>
        <source>Collection: %1</source>
        <translation>收藏集：%1</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Collection List (%1)</source>
        <translation>收藏集清單 (%1)</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>File Name</source>
        <translation>檔案名稱</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Hash</source>
        <translation>切細值</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Details</source>
        <translation>詳細資料</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Author:</source>
        <translation>作者：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Author Key:</source>
        <translation>作者金鑰：</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Options</source>
        <translation>選項</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Add new downloads into the collection category</source>
        <translation>將新下載加入到收藏集的分類中</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Download</source>
        <translation>下載</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Close</source>
        <translation>關閉</translation>
    </message>
</context>
<context>
    <name>eMule::CommentEditPanel</name>
    <message>
        <location filename="../src/gui/dialogs/CommentEditPanel.cpp" line="+70"/>
        <source>Comment This File! (This text will be shown to all users.)</source>
        <translation>評論此檔案！(此文字將顯示給所有使用者。)</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>For a film, you can say its length, its story, the language... And if it is a fake, you can inform other eMule users...</source>
        <translation>對於影片，您可以說明其長度、劇情、語言... 如果它是假檔，您可以告知其他 eMule 使用者...</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>File Quality</source>
        <translation>檔案品質</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Choose the file rating or advice users if the file is invalid!</source>
        <translation>選擇檔案評分，或在檔案無效時提醒其他使用者！</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Reset</source>
        <translation>重置</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Apply</source>
        <translation>套用</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Comment</source>
        <translation>評論</translation>
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
        <translation>(Kad 搜尋中...)</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+55"/>
        <source>Search Kad</source>
        <translation>搜尋 Kad</translation>
    </message>
    <message>
        <location line="-37"/>
        <source>Rating</source>
        <translation>評分</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Comment</source>
        <translation>評論</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>File Name</source>
        <translation>檔案名稱</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>User Name</source>
        <translation>使用者名稱</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Network</source>
        <translation>網路</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>No comments or ratings available for this file.</source>
        <translation>此檔案沒有可用的評論或評分。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Copy</source>
        <translation>複製</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Edit spam filter...</source>
        <translation>編輯垃圾訊息過濾器...</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Edit spam filter for comments</source>
        <translation>編輯評論的垃圾訊息過濾器</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ignore comments containing: (Separator | )</source>
        <translation>忽略包含以下內容的評論：(分隔符號 | )</translation>
    </message>
</context>
<context>
    <name>eMule::ConfidenceText</name>
    <message>
        <location filename="../src/core/search/ConfidenceText.h" line="+27"/>
        <source>Spam</source>
        <translation>垃圾訊息</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Likely fake</source>
        <translation>很可能是假檔</translation>
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
        <translation>看起來正常</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Genuine</source>
        <translation>可信</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Shared under names that describe different content</source>
        <translation>以描述不同內容的多個名稱分享</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Shared as different kinds of file (e.g. video and archive)</source>
        <translation>以不同類型的檔案分享 (例如影像和壓縮檔)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>A name matches a rule in FakeFileFilter.dat</source>
        <translation>某個名稱符合 FakeFileFilter.dat 中的規則</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>A comment matches a rule in FakeFileFilter.dat</source>
        <translation>某則評論符合 FakeFileFilter.dat 中的規則</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The first bytes are not what the extension claims</source>
        <translation>檔案開頭的位元組與副檔名不符</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>It is a program named like a media file</source>
        <translation>這是一個以媒體檔案命名的程式</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>It is an archive named like a media file</source>
        <translation>這是一個以媒體檔案命名的壓縮檔</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The published type does not fit the extension</source>
        <translation>發佈的類型與副檔名不符</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The spam filter rates it high</source>
        <translation>垃圾訊息過濾器評分偏高</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Counted as spam</source>
        <translation>被視為垃圾訊息</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Rated poor, or a Kad note calls it a fake</source>
        <translation>評分較差，或有 Kad 備註稱其為假檔</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Rated as fake by users</source>
        <translation>被使用者評為假檔</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Sources disagree on the AICH hash</source>
        <translation>各來源的 AICH 切細值不一致</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The length does not fit a file of this size</source>
        <translation>長度與此大小的檔案不相符</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The bitrate is not plausible</source>
        <translation>位元率不合理</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Length and bitrate do not add up to the size</source>
        <translation>長度與位元率和檔案大小對不上</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Artist, album and title appear in none of the names</source>
        <translation>演出者、專輯和標題均未出現在任何名稱中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>A name uses words that advertise child abuse material</source>
        <translation>名稱中含有宣傳兒童虐待內容的詞語</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The release is password protected</source>
        <translation>該發佈受密碼保護</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>%1 (fake score %2 of 100)</source>
        <translation>%1 (假檔評分 %2 / 100)</translation>
    </message>
</context>
<context>
    <name>eMule::ContactsGraph</name>
    <message>
        <location filename="../src/gui/controls/ContactsGraph.cpp" line="+88"/>
        <source>Contacts</source>
        <translation>聯絡人</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Time</source>
        <translation>時間</translation>
    </message>
</context>
<context>
    <name>eMule::CoreConnectDialog</name>
    <message>
        <location filename="../src/gui/dialogs/CoreConnectDialog.cpp" line="+23"/>
        <source>Connect to Core</source>
        <translation>連線到核心</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Could not find a local eMule core.
Enter the address and authentication token of a remote core.</source>
        <translation>找不到本機 eMule 核心。
請輸入遠端核心的位址和驗證權杖。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Remote Core</source>
        <translation>遠端核心</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Address:</source>
        <translation>位址：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Port:</source>
        <translation>連接埠：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>paste token here</source>
        <translation>在此貼上權杖</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Token:</source>
        <translation>權杖：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Save token</source>
        <translation>儲存權杖</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Connect</source>
        <translation>連線</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Exit</source>
        <translation>結束</translation>
    </message>
</context>
<context>
    <name>eMule::DetailDialog</name>
    <message>
        <location filename="../src/gui/dialogs/DetailDialog.cpp" line="+201"/>
        <source>Rename</source>
        <translation>重新命名</translation>
    </message>
    <message>
        <location line="+16"/>
        <location line="+7"/>
        <source>Archive Preview</source>
        <translation>壓縮檔預覽</translation>
    </message>
    <message>
        <location line="-6"/>
        <source>A preview file can only be opened when the core runs on this computer.</source>
        <translation>只有當核心在本機上執行時才能開啟預覽檔案。</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Search Kad</source>
        <translation>搜尋 Kad</translation>
    </message>
    <message>
        <location line="+103"/>
        <source>Comments</source>
        <translation>評論</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>Previous</source>
        <translation>上一個</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Next</source>
        <translation>下一個</translation>
    </message>
</context>
<context>
    <name>eMule::DownloadListModel</name>
    <message>
        <location filename="../src/gui/controls/DownloadListModel.cpp" line="+99"/>
        <source>Available parts: %1</source>
        <translation>可用部分：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>User Name: %1</source>
        <translation>使用者名稱：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unknown</source>
        <translation>未知</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server</source>
        <translation>伺服器</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Next re-ask</source>
        <translation>下次重新詢問</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Asked: %1 times; Available parts: %2</source>
        <translation>已詢問：%1 次；可用部分：%2</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Client&apos;s file name:</source>
        <translation>客戶端的檔案名稱：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>File comment:</source>
        <translation>檔案評論：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Rating</source>
        <translation>評分</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+676"/>
        <source>Asked for another file</source>
        <translation>已要求其他檔案</translation>
    </message>
    <message>
        <location line="-668"/>
        <source>A4AF files</source>
        <translation>A4AF 檔案</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Country: %1</source>
        <translation>國家：%1</translation>
    </message>
    <message>
        <location line="+122"/>
        <source>Queue Full</source>
        <translation>佇列已滿</translation>
    </message>
    <message>
        <location line="+113"/>
        <location line="+2"/>
        <source>Never</source>
        <translation>從不</translation>
    </message>
    <message>
        <location line="+35"/>
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
        <translation>檔案名稱：	%1
ED2K 切細值：	%2
大小：	%3
已完成：	%4 (%5%)
類型：	%6
狀態：	%7
優先權：	%8
來源：	%9
要求：	%10
已接受要求：	%11
已上傳：	%12</translation>
    </message>
    <message>
        <location line="+100"/>
        <source>File Name</source>
        <translation>檔案名稱</translation>
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
        <translation>進度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Sources</source>
        <translation>來源</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority</source>
        <translation>優先權</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status</source>
        <translation>狀態</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remaining</source>
        <translation>剩餘</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Seen Complete</source>
        <translation>已見完整</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Country</source>
        <translation>國家</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Last reception</source>
        <translation>最後接收</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Category</source>
        <translation>分類</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Added On</source>
        <translation>加入於</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Confidence</source>
        <translation>可信度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Transferred</source>
        <translation>已傳輸</translation>
    </message>
    <message>
        <location line="+218"/>
        <source>Importing part</source>
        <translation>正在匯入部分</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+5"/>
        <source>Hashing</source>
        <translation>切細中</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+1"/>
        <location line="+1"/>
        <source>Completing (%1)</source>
        <translation>正在完成 (%1)</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Copying</source>
        <translation>正在複製</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Uncompressing</source>
        <translation>正在解壓縮</translation>
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
        <translation>已暫停</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+3"/>
        <source>Insufficient disk space</source>
        <translation>磁碟空間不足</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Error</source>
        <translation>錯誤</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Downloading</source>
        <translation>下載中</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Waiting</source>
        <translation>等待中</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>HTTP Cache</source>
        <translation>HTTP 快取</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Swap blocked</source>
        <translation>置換被封鎖</translation>
    </message>
</context>
<context>
    <name>eMule::FileDetailDialog</name>
    <message numerus="yes">
        <location filename="../src/gui/dialogs/FileDetailDialog.cpp" line="+98"/>
        <source>File Details: %n files</source>
        <translation>
            <numerusform>檔案詳細資料：%n 個檔案</numerusform>
        </translation>
    </message>
    <message>
        <location line="+14"/>
        <location line="+48"/>
        <location line="+7"/>
        <source>General</source>
        <translation>一般</translation>
    </message>
    <message>
        <location line="-54"/>
        <location line="+49"/>
        <location line="+7"/>
        <source>Comments</source>
        <translation>評論</translation>
    </message>
    <message>
        <location line="-55"/>
        <location line="+49"/>
        <location line="+7"/>
        <source>Media Info</source>
        <translation>媒體資訊</translation>
    </message>
    <message>
        <location line="-55"/>
        <location line="+50"/>
        <location line="+7"/>
        <source>ED2K Link</source>
        <translation>ED2K 連結</translation>
    </message>
    <message>
        <location line="-23"/>
        <source>File Details: %1</source>
        <translation>檔案詳細資料：%1</translation>
    </message>
    <message>
        <location line="+12"/>
        <location line="+7"/>
        <source>File Names</source>
        <translation>檔案名稱</translation>
    </message>
    <message>
        <location line="-4"/>
        <location line="+7"/>
        <source>Metadata</source>
        <translation>中繼資料</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+2"/>
        <source>Archive Preview</source>
        <translation>壓縮檔預覽</translation>
    </message>
    <message>
        <location line="+29"/>
        <location line="+20"/>
        <location line="+50"/>
        <source>File Name</source>
        <translation>檔案名稱</translation>
    </message>
    <message>
        <location line="-69"/>
        <location line="+20"/>
        <source>Hash (MD4)</source>
        <translation>切細值 (MD4)</translation>
    </message>
    <message>
        <location line="-19"/>
        <location line="+27"/>
        <source>File Size</source>
        <translation>檔案大小</translation>
    </message>
    <message>
        <location line="-27"/>
        <location line="+27"/>
        <source>%1 (%2 Bytes)</source>
        <translation>%1 (%2 Bytes)</translation>
    </message>
    <message>
        <location line="-26"/>
        <location line="+31"/>
        <source>Completed</source>
        <translation>已完成</translation>
    </message>
    <message>
        <location line="-28"/>
        <location line="+33"/>
        <source>Transferred</source>
        <translation>已傳輸</translation>
    </message>
    <message>
        <location line="-32"/>
        <location line="+33"/>
        <source>Status</source>
        <translation>狀態</translation>
    </message>
    <message>
        <location line="-32"/>
        <location line="+33"/>
        <source>Priority</source>
        <translation>優先權</translation>
    </message>
    <message>
        <location line="-32"/>
        <location line="+37"/>
        <location line="+24"/>
        <source>Sources</source>
        <translation>來源</translation>
    </message>
    <message>
        <location line="-57"/>
        <location line="+40"/>
        <source>Created</source>
        <translation>建立時間</translation>
    </message>
    <message>
        <location line="-39"/>
        <location line="+40"/>
        <source>Last Seen Complete</source>
        <translation>最後一次見到完整檔案</translation>
    </message>
    <message>
        <location line="-39"/>
        <location line="+40"/>
        <source>Last Reception</source>
        <translation>最後接收</translation>
    </message>
    <message>
        <location line="-30"/>
        <source>AICH Hash</source>
        <translation>AICH 切細值</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>File Path</source>
        <translation>檔案路徑</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>No alternative file names reported by sources. Use “Search Kad” to look them up.</source>
        <translation>來源未回報其他檔案名稱。使用「搜尋 Kad」進行查詢。</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>&amp;Take over</source>
        <translation>採用(&amp;T)</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>&amp;Cleanup</source>
        <translation>清理(&amp;C)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Rename</source>
        <translation>重新命名</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Take over</source>
        <translation>採用</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Copy</source>
        <translation>複製</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Search Kad</source>
        <translation>搜尋 Kad</translation>
    </message>
    <message>
        <location line="+92"/>
        <source>No media information available.</source>
        <translation>沒有可用的媒體資訊。</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Title</source>
        <translation>標題</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Artist</source>
        <translation>演出者</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Album</source>
        <translation>專輯</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Codec</source>
        <translation>編解碼器</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Bitrate</source>
        <translation>位元率</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Length</source>
        <translation>長度</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Link Options</source>
        <translation>連結選項</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Include Hashset</source>
        <translation>包含切細值集</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Include Hostname</source>
        <translation>包含主機名稱</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add your hostname or public IPv6 as a source</source>
        <translation>將您的主機名稱或公開 IPv6 加入為來源</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>HTML Format</source>
        <translation>HTML 格式</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Copy to Clipboard</source>
        <translation>複製到剪貼簿</translation>
    </message>
</context>
<context>
    <name>eMule::FindInListDialog</name>
    <message>
        <location filename="../src/gui/dialogs/FindInListDialog.cpp" line="+90"/>
        <source>Search</source>
        <translation>搜尋</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Search for:</source>
        <translation>搜尋：</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Search in column:</source>
        <translation>在欄位中搜尋：</translation>
    </message>
</context>
<context>
    <name>eMule::FirstStartWizard</name>
    <message>
        <location filename="../src/gui/dialogs/FirstStartWizard.cpp" line="+52"/>
        <location line="+301"/>
        <source>Unlimited</source>
        <translation>無限制</translation>
    </message>
    <message>
        <location line="-292"/>
        <source>eMule First Runtime Wizard</source>
        <translation>eMule 首次執行精靈</translation>
    </message>
    <message>
        <location line="+92"/>
        <source>eMule uses two ports for communication with servers and clients. These ports must be free and available for remote clients. The TCP port must be available to ensure the main functionality of eMule. The UDP port is used for Kad (serverless network) and to reduce network usage (Overhead).</source>
        <translation>eMule 使用兩個連接埠與伺服器和客戶端通訊。這些連接埠必須空閒並可供遠端客戶端使用。TCP 連接埠必須可用以確保 eMule 的主要功能。UDP 連接埠用於 Kad (無伺服器網路) 和減少網路使用量 (額外負擔)。</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>You can change the ports here while no network activities have started.</source>
        <translation>在尚未開始任何網路活動前，您可以在此變更連接埠。</translation>
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
        <location line="+3"/>
        <source>Disable</source>
        <translation>停用</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Use UPnP to Setup Ports</source>
        <translation>使用 UPnP 設定連接埠</translation>
    </message>
    <message>
        <location line="+46"/>
        <source>Here you can test, if your TCP and UDP port can be connected to from remote. This success of this test is required for servers and clients to connect you. The TCP port have to succeed!</source>
        <translation>您可以在此測試 TCP 和 UDP 連接埠能否從遠端連線。伺服器和客戶端要連線到您，此測試必須成功。TCP 連接埠必須通過！</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Test Ports</source>
        <translation>測試連接埠</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Choose which Network(s) you want to use</source>
        <translation>選擇要使用的網路</translation>
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
        <location line="+31"/>
        <source>Select your internet connection. eMule derives its download and upload limits from it, leaving room for your other applications. You can change the limits at any time in the Options.</source>
        <translation>請選擇您的網際網路連線。eMule 會據此決定下載和上傳限制，並為其他應用程式保留餘裕。您可以隨時在選項中變更限制。</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Type</source>
        <translation>類型</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Down</source>
        <translation>下載</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Up</source>
        <translation>上傳</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Keep current settings</source>
        <translation>保留目前設定</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unknown (recommended defaults)</source>
        <translation>未知 (建議的預設值)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Custom</source>
        <translation>自訂</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+1"/>
        <source>(enter below)</source>
        <translation>(在下方輸入)</translation>
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
        <translation>下載：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload:</source>
        <translation>上傳：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source> KB/s</source>
        <translation> KB/s</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Download limit:</source>
        <translation>下載限制：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload limit:</source>
        <translation>上傳限制：</translation>
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
        <location line="-25"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Help</source>
        <translation>說明</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Connection Speed</source>
        <translation>連線速度</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Ports and Connection</source>
        <translation>連接埠和連線</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bandwidth</source>
        <translation>頻寬</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Connection</source>
        <translation>連線</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Finish</source>
        <translation>完成</translation>
    </message>
    <message>
        <location line="+180"/>
        <source>The ports are forwarded when the core starts.</source>
        <translation>連接埠將在核心啟動時轉送。</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>The core did not accept the port settings.</source>
        <translation>核心未接受連接埠設定。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>No answer from the core about port forwarding.</source>
        <translation>核心未就連接埠轉送作出回應。</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Network</source>
        <translation>網路</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>You must enable at least one network (Kad or eD2K).</source>
        <translation>您必須啟用至少一個網路 (Kad 或 eD2K)。</translation>
    </message>
</context>
<context>
    <name>eMule::ImportDownloadsDialog</name>
    <message>
        <location filename="../src/gui/dialogs/ImportDownloadsDialog.cpp" line="+40"/>
        <source>Convert Part Files</source>
        <translation>轉換部分檔案</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Current Job</source>
        <translation>目前工作</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+224"/>
        <source>Idle</source>
        <translation>閒置</translation>
    </message>
    <message>
        <location line="-213"/>
        <source>Job Queue</source>
        <translation>工作佇列</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Filename</source>
        <translation>檔案名稱</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Status</source>
        <translation>狀態</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>File Hash</source>
        <translation>檔案切細值</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Add Imports...</source>
        <translation>加入匯入...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Retry Selected</source>
        <translation>重試所選項目</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove Selected</source>
        <translation>移除所選項目</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Close</source>
        <translation>關閉</translation>
    </message>
    <message>
        <location line="+57"/>
        <location line="+13"/>
        <source>Import Downloads</source>
        <translation>匯入下載</translation>
    </message>
    <message>
        <location line="-12"/>
        <source>Import Downloads is only available for local connections.</source>
        <translation>匯入下載僅適用於本機連線。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Select folder to scan for importable downloads</source>
        <translation>選擇要掃描可匯入下載的資料夾</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Do you want to delete the source files after a successful import?</source>
        <translation>匯入成功後是否刪除來源檔案？</translation>
    </message>
    <message>
        <location line="+118"/>
        <source>Converting...</source>
        <translation>正在轉換...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Done</source>
        <translation>完成</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>OK</source>
        <translation>確定</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Queued</source>
        <translation>佇列中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>In Progress</source>
        <translation>進行中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Out of Disk Space</source>
        <translation>磁碟空間不足</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>.part.met Not Found</source>
        <translation>找不到 .part.met</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>I/O Error</source>
        <translation>I/O 錯誤</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Failed</source>
        <translation>失敗</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bad Format</source>
        <translation>格式錯誤</translation>
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
        <translation>%1 個月</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1 years</source>
        <translation>%1 年</translation>
    </message>
    <message>
        <location line="+109"/>
        <source>Posted: %1</source>
        <translation>發佈於：%1</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>%1 files</source>
        <translation>%1 個檔案</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Password protected</source>
        <translation>受密碼保護</translation>
    </message>
    <message>
        <location line="+31"/>
        <source>Name</source>
        <translation>名稱</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Age</source>
        <translation>發佈時間</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Category</source>
        <translation>分類</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Grabs</source>
        <translation>抓取數</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Indexer</source>
        <translation>索引器</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Seeders</source>
        <translation>做種數</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Peers</source>
        <translation>同儕數</translation>
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
    <name>eMule::IrcPanel</name>
    <message>
        <location filename="../src/gui/panels/IrcPanel.cpp" line="+143"/>
        <source>Select an IRC nick.</source>
        <translation>選擇一個 IRC 暱稱。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Should be no longer than 25 characters: letters, digits or symbols [_-{}]\.
Nick can be changed again in Options-&gt;IRC.</source>
        <translation>不超過 25 個字元：字母、數字或符號 [_-{}]\。
暱稱可在 選項-&gt;IRC 中再次變更。</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Connecting</source>
        <translation>連線中</translation>
    </message>
    <message>
        <location line="+66"/>
        <source>Connected</source>
        <translation>已連線</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Disconnect</source>
        <translation>中斷連線</translation>
    </message>
    <message>
        <location line="+27"/>
        <location line="+480"/>
        <source>Connect</source>
        <translation>連線</translation>
    </message>
    <message>
        <location line="-469"/>
        <source>* Disconnected</source>
        <translation>* 已中斷連線</translation>
    </message>
    <message>
        <location line="+148"/>
        <source>* %1 has joined %2</source>
        <translation>* %1 加入了 %2</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>* %1 has parted %2 (%3)</source>
        <translation>* %1 離開了 %2 (%3)</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>* %1 has quit (%2)</source>
        <translation>* %1 已離開 (%2)</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>* %1 was kicked by %2 (%3)</source>
        <translation>* %1 被 %2 踢出 (%3)</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>* %1 is now known as %2</source>
        <translation>* %1 現在改名為 %2</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>* %1 sets mode: %2 %3</source>
        <translation>* %1 設定模式：%2 %3</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>* Channel Topic: %1</source>
        <translation>* 頻道主題：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>* %1 changes topic to &apos;%2&apos;</source>
        <translation>* %1 將主題改為 &apos;%2&apos;</translation>
    </message>
    <message>
        <location line="+68"/>
        <source>Nick in use</source>
        <translation>暱稱已被使用</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The nick &quot;%1&quot; is already in use.
Please choose another:</source>
        <translation>暱稱 &quot;%1&quot; 已被使用。
請選擇另一個：</translation>
    </message>
    <message>
        <location line="+66"/>
        <location line="+599"/>
        <source>Nick</source>
        <translation>暱稱</translation>
    </message>
    <message>
        <location line="-533"/>
        <source>Status</source>
        <translation>狀態</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Close</source>
        <translation>關閉</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Smileys</source>
        <translation>表情符號</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Bold</source>
        <translation>粗體</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Italic</source>
        <translation>斜體</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Underline</source>
        <translation>底線</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Color</source>
        <translation>色彩</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Reset Formatting</source>
        <translation>重設格式</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Type a message...</source>
        <translation>輸入訊息...</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Send</source>
        <translation>傳送</translation>
    </message>
    <message>
        <location line="+76"/>
        <source>Channel</source>
        <translation>頻道</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Users</source>
        <translation>使用者</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Topic</source>
        <translation>主題</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Join</source>
        <translation>加入</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Channels</source>
        <translation>頻道</translation>
    </message>
    <message>
        <location line="+314"/>
        <source>Nick (%1)</source>
        <translation>暱稱 (%1)</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>* Private chat session started</source>
        <translation>* 私人聊天工作階段已開始</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>Nick : %1</source>
        <translation>暱稱：%1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Private Message</source>
        <translation>私人訊息</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Add to friends list</source>
        <translation>加入到好友清單</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Send this to friend: </source>
        <translation>傳送給好友： </translation>
    </message>
    <message>
        <location line="+1"/>
        <source>No Shared File Selected</source>
        <translation>未選擇已分享檔案</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Kick</source>
        <translation>踢人</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Ban</source>
        <translation>封鎖</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Slap</source>
        <translation>打人</translation>
    </message>
    <message>
        <location line="+95"/>
        <source>%1 attempted to send you a file. If you wanted to accept the files from this person, enable Receive files in the IRC Preferences.</source>
        <translation>%1 嘗試傳送檔案給您。如果您想接受此人的檔案，請在 IRC 偏好設定中啟用接收檔案。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>%1 sent (%2) to be auto started.</source>
        <translation>%1 傳送了 (%2)，將自動開始。</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>%1 attempted to send you a file. If you wanted to accept the files from this person, add him as a friend or change your IRC Preferences.</source>
        <translation>%1 嘗試傳送檔案給您。如果您想接受此人的檔案，請將其加入為好友或變更您的 IRC 偏好設定。</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>%1 added you as friend!</source>
        <translation>%1 已將您加入為好友！</translation>
    </message>
</context>
<context>
    <name>eMule::KadContactHistogram</name>
    <message>
        <location filename="../src/gui/controls/KadContactHistogram.cpp" line="+194"/>
        <source>Contacts</source>
        <translation>聯絡人</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Kademlia Network</source>
        <translation>Kademlia 網路</translation>
    </message>
</context>
<context>
    <name>eMule::KadContactsModel</name>
    <message>
        <location filename="../src/gui/controls/KadContactsModel.cpp" line="+79"/>
        <source>Type</source>
        <translation>類型</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>ID</source>
        <translation>ID</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Distance</source>
        <translation>距離</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Country</source>
        <translation>國家</translation>
    </message>
</context>
<context>
    <name>eMule::KadLookupGraph</name>
    <message>
        <location filename="../src/gui/controls/KadLookupGraph.cpp" line="+70"/>
        <source>No search selected</source>
        <translation>未選擇搜尋</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Distance</source>
        <translation>距離</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Time</source>
        <translation>時間</translation>
    </message>
    <message>
        <location line="+235"/>
        <source>Our node (search initiator)</source>
        <translation>我們的節點 (搜尋發起者)</translation>
    </message>
</context>
<context>
    <name>eMule::KadPanel</name>
    <message>
        <location filename="../src/gui/panels/KadPanel.cpp" line="+120"/>
        <location line="+12"/>
        <location line="+192"/>
        <location line="+2"/>
        <location line="+37"/>
        <location line="+213"/>
        <source>▸ Contacts (0)</source>
        <translation>▸ 聯絡人 (0)</translation>
    </message>
    <message>
        <location line="-455"/>
        <location line="+12"/>
        <location line="+392"/>
        <location line="+97"/>
        <source>▸ Current Searches (0)</source>
        <translation>▸ 目前搜尋 (0)</translation>
    </message>
    <message>
        <location line="-427"/>
        <source>The URL is invalid.</source>
        <translation>URL 無效。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Downloading...</source>
        <translation>正在下載...</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+224"/>
        <location line="+40"/>
        <source>Bootstrap</source>
        <translation>啟動</translation>
    </message>
    <message>
        <location line="-262"/>
        <location line="+77"/>
        <source>Kademlia</source>
        <translation>Kademlia</translation>
    </message>
    <message>
        <location line="+42"/>
        <location line="+358"/>
        <source>▸ Search Details</source>
        <translation>▸ 搜尋詳細資料</translation>
    </message>
    <message>
        <location line="-271"/>
        <source>Recheck Firewall</source>
        <translation>重新檢查防火牆</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+256"/>
        <source>Connect</source>
        <translation>連線</translation>
    </message>
    <message>
        <location line="-238"/>
        <source>IP or Address:</source>
        <translation>IP 或位址：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Port:</source>
        <translation>連接埠：</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Nodes.dat from URL:</source>
        <translation>從 URL 取得 Nodes.dat：</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>From known clients</source>
        <translation>從已知客戶端</translation>
    </message>
    <message>
        <location line="+142"/>
        <location line="+3"/>
        <source>▸ Contacts (%1)</source>
        <translation>▸ 聯絡人 (%1)</translation>
    </message>
    <message>
        <location line="+41"/>
        <source>▸ Current Searches (%1)</source>
        <translation>▸ 目前搜尋 (%1)</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Disconnect</source>
        <translation>中斷連線</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="+65"/>
        <source>▸ Search Details (%1)</source>
        <translation>▸ 搜尋詳細資料 (%1)</translation>
    </message>
</context>
<context>
    <name>eMule::KadSearchesModel</name>
    <message>
        <location filename="../src/gui/controls/KadSearchesModel.cpp" line="+36"/>
        <source>Stopping</source>
        <translation>正在停止</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Active</source>
        <translation>作用中</translation>
    </message>
    <message>
        <location line="+42"/>
        <source>Number</source>
        <translation>編號</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Key</source>
        <translation>索引鍵</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type</source>
        <translation>類型</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Name</source>
        <translation>名稱</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status</source>
        <translation>狀態</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Load</source>
        <translation>負載</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Packets Sent</source>
        <translation>已傳送封包</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Responses</source>
        <translation>回應</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Search Sources</source>
        <translation>搜尋來源</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Search Keywords</source>
        <translation>搜尋關鍵字</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Node Lookup</source>
        <translation>節點查詢</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Store File</source>
        <translation>儲存檔案</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Store Keyword</source>
        <translation>儲存關鍵字</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Find Buddy</source>
        <translation>尋找夥伴</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Store Notes</source>
        <translation>儲存備註</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Notes</source>
        <translation>備註</translation>
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
        <location filename="../src/gui/controls/LogTextView.cpp" line="+59"/>
        <source>Autoscroll</source>
        <translation>自動捲動</translation>
    </message>
</context>
<context>
    <name>eMule::LogWidget</name>
    <message>
        <location filename="../src/gui/controls/LogWidget.cpp" line="+87"/>
        <location line="+2"/>
        <source>Server Info</source>
        <translation>伺服器資訊</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+2"/>
        <source>Log</source>
        <translation>記錄</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+2"/>
        <source>Verbose</source>
        <translation>詳細</translation>
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
        <location line="+9"/>
        <location line="+2"/>
        <source>IPC</source>
        <translation>IPC</translation>
    </message>
    <message>
        <location line="+373"/>
        <source>Click here to check if a new version is available</source>
        <translation>按一下這裡檢查是否有新版本</translation>
    </message>
</context>
<context>
    <name>eMule::MainWindow</name>
    <message>
        <location filename="../src/gui/app/MainWindow.cpp" line="+90"/>
        <location line="+486"/>
        <source>eMule Qt v%1</source>
        <translation>eMule Qt v%1</translation>
    </message>
    <message>
        <location line="-411"/>
        <source>Quit eMule Qt</source>
        <translation>結束 eMule Qt</translation>
    </message>
    <message>
        <location line="+34"/>
        <location line="+9"/>
        <source>New Version Available</source>
        <translation>有新版本可用</translation>
    </message>
    <message>
        <location line="-8"/>
        <source>eMule Qt %1 has been released.</source>
        <translation>eMule Qt %1 已發佈。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Version %1 of eMule Qt was released on %2.</source>
        <translation>eMule Qt 版本 %1 已於 %2 發佈。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Version %1 of eMule Qt is available.</source>
        <translation>eMule Qt 版本 %1 現已推出。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>%1

You are running %2. Open the eMule Qt website?</source>
        <translation>%1

您正在執行 %2。是否開啟 eMule Qt 網站？</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+7"/>
        <location line="+777"/>
        <source>Version Check</source>
        <translation>版本檢查</translation>
    </message>
    <message>
        <location line="-783"/>
        <source>You are running the latest version of eMule Qt (v%1).</source>
        <translation>您正在執行最新版本的 eMule Qt (v%1)。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Could not check for a new version:

%1</source>
        <translation>無法檢查新版本：

%1</translation>
    </message>
    <message>
        <location line="+66"/>
        <source>Cannot Connect</source>
        <translation>無法連線</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Both the eD2K and Kad networks are disabled.

Enable at least one under Options → Connection to connect.</source>
        <translation>eD2K 和 Kad 網路均已停用。

請在「選項 → 連線」中至少啟用一個後再連線。</translation>
    </message>
    <message>
        <location line="+68"/>
        <source>eMule Version %1 ready</source>
        <translation>eMule 版本 %1 已就緒</translation>
    </message>
    <message>
        <location line="+63"/>
        <source>eD2K: Connected (LowID)</source>
        <translation>eD2K：已連線 (低-ID)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>eD2K: Connected</source>
        <translation>eD2K：已連線</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>eD2K: Connecting...</source>
        <translation>eD2K：連線中...</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Blocked: network interface not available</source>
        <translation>已封鎖：網路介面無法使用</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+1294"/>
        <source>eD2K: Disconnected</source>
        <translation>eD2K：已中斷連線</translation>
    </message>
    <message>
        <location line="-1287"/>
        <source>eD2K Server: %1  (%2 Users)</source>
        <translation>eD2K 伺服器：%1  (%2 位使用者)</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Preparing...</source>
        <translation>正在準備...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Waiting...</source>
        <translation>等待中...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Error!</source>
        <translation>錯誤！</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Kad: Connected</source>
        <translation>Kad：已連線</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Kad: Connected (Firewalled)</source>
        <translation>Kad：已連線 (防火牆內)</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Kad: Connecting...</source>
        <translation>Kad：連線中...</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+1229"/>
        <source>Kad: Disconnected</source>
        <translation>Kad：已中斷連線</translation>
    </message>
    <message>
        <location line="-1220"/>
        <source>Users: %1 | Files: %2</source>
        <translation>使用者：%1 | 檔案：%2</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>Up: %1(%2)</source>
        <translation>上傳: %1(%2)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Down: %1(%2)</source>
        <translation>下載: %1(%2)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Up: %1</source>
        <translation>上傳: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Down: %1</source>
        <translation>下載: %1</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>Connected</source>
        <translation>已連線</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Disconnected</source>
        <translation>已中斷連線</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>eMule Qt v%1 (%2)
Up: %3 | Down: %4</source>
        <translation>eMule Qt v%1 (%2)
上傳：%3 | 下載：%4</translation>
    </message>
    <message>
        <location line="+73"/>
        <source>Confirm Exit</source>
        <translation>確認結束</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Are you sure you want to exit eMule?</source>
        <translation>確定要結束 eMule 嗎？</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>The eMule core stopped right after it was started.</source>
        <translation>eMule 核心在啟動後立即停止了。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The eMule core at %1 accepts the connection but does not answer. It may be busy or stuck.</source>
        <translation>位於 %1 的 eMule 核心接受了連線但沒有回應。它可能正忙碌或已停滯。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>No eMule core is reachable at %1.</source>
        <translation>在 %1 無法連線到任何 eMule 核心。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The eMule core at %1 refused the connection: %2</source>
        <translation>位於 %1 的 eMule 核心拒絕了連線：%2</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>eMule Core</source>
        <translation>eMule 核心</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Click Retry to start it again.</source>
        <translation>按一下「重試」重新啟動它。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>eMule keeps trying in the background.</source>
        <translation>eMule 會在背景繼續嘗試。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Retry</source>
        <translation>重試</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Open Log</source>
        <translation>開啟記錄</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Quit</source>
        <translation>結束</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Log Entry</source>
        <translation>記錄項目</translation>
    </message>
    <message>
        <location line="+115"/>
        <source>eMule Log</source>
        <translation>eMule 記錄</translation>
    </message>
    <message>
        <location line="+73"/>
        <source>Open Incoming Folder...</source>
        <translation>開啟新進檔案資料夾...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Open Downloads Folder in Browser</source>
        <translation>在瀏覽器中開啟下載資料夾</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Import Downloads (eM,eD,ON)...</source>
        <translation>匯入下載 (eM,eD,ON)...</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>eMule First Runtime Wizard...</source>
        <translation>eMule 首次執行精靈...</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>IP Filter...</source>
        <translation>IP 過濾器...</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Paste eD2K Links...</source>
        <translation>貼上 eD2K 連結...</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Open WebUI</source>
        <translation>開啟 Web 介面</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Submit Bug Report...</source>
        <translation>提交錯誤報告...</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Links</source>
        <translation>連結</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>eMule Homepage</source>
        <translation>eMule 首頁</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Features</source>
        <translation>功能</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Edit Web Services...</source>
        <translation>編輯 Web 服務...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Web Services</source>
        <translation>Web 服務</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>webservices.dat was not found in the config folder.</source>
        <translation>在設定資料夾中找不到 webservices.dat。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Scheduler</source>
        <translation>時間表</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Disable Scheduler</source>
        <translation>停用時間表</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Enable Scheduler</source>
        <translation>啟用時間表</translation>
    </message>
    <message>
        <location line="+41"/>
        <source>Cannot Open Downloads Folder</source>
        <translation>無法開啟下載資料夾</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Cannot Open Web Interface</source>
        <translation>無法開啟 Web 介面</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not connected to the core.</source>
        <translation>未連線到核心。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Web Interface Disabled</source>
        <translation>Web 介面已停用</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The web interface is disabled.

Enable it under Options → Web Interface, then try again.</source>
        <translation>Web 介面已停用。

請在「選項 → Web 介面」中啟用，然後再試一次。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Open Options</source>
        <translation>開啟選項</translation>
    </message>
    <message>
        <location line="+175"/>
        <source>Main</source>
        <translation>主選單</translation>
    </message>
    <message>
        <location line="+162"/>
        <source>Toolbar Skins</source>
        <translation>工具列面板</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Select Toolbar Bitmap...</source>
        <translation>選擇工具列點陣圖...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Select Toolbar Bitmap</source>
        <translation>選擇工具列點陣圖</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Images (*.bmp *.png *.jpg);;All Files (*)</source>
        <translation>圖片 (*.bmp *.png *.jpg);;所有檔案 (*)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Select Toolbar Bitmap Directory...</source>
        <translation>選擇工具列點陣圖目錄...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Select Toolbar Bitmap Directory</source>
        <translation>選擇工具列點陣圖目錄</translation>
    </message>
    <message>
        <location line="+17"/>
        <location line="+62"/>
        <source>Default</source>
        <translation>預設</translation>
    </message>
    <message>
        <location line="-26"/>
        <source>Skin Profiles</source>
        <translation>面板設定檔</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Select Skin File...</source>
        <translation>選擇面板檔案...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Select Skin Profile</source>
        <translation>選擇面板設定檔</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Skin Files (*.eMuleSkin.ini);;All Files (*)</source>
        <translation>面板檔案 (*.eMuleSkin.ini);;所有檔案 (*)</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Select Skin Directory...</source>
        <translation>選擇面板目錄...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Select Skin Directory</source>
        <translation>選擇面板目錄</translation>
    </message>
    <message>
        <location line="+40"/>
        <source>Text Label Options</source>
        <translation>文字標籤選項</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Customize Toolbar...</source>
        <translation>自訂工具列...</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Disconnect</source>
        <translation>中斷連線</translation>
    </message>
    <message>
        <location line="+5"/>
        <location filename="../src/gui/app/ToolbarButtons.h" line="+48"/>
        <source>Connect</source>
        <translation>連線</translation>
    </message>
    <message>
        <location line="+71"/>
        <source>Ready</source>
        <translation>就緒</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Users: 0 | Files: 0</source>
        <translation>使用者：0 | 檔案：0</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Up: 0.0</source>
        <translation>上傳：0.0</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Down: 0.0</source>
        <translation>下載：0.0</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Double-click for Network Information</source>
        <translation>按兩下以檢視網路資訊</translation>
    </message>
    <message>
        <location line="+31"/>
        <source>New message — double-click to read</source>
        <translation>新訊息 — 按兩下以閱讀</translation>
    </message>
    <message>
        <location line="+303"/>
        <source>eD2K Link</source>
        <translation>eD2K 連結</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Do you want to download and use the Kad nodes file from
%1 ?</source>
        <translation>是否從以下位址下載並使用 Kad 節點檔案？
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
        <translation>未連線到核心</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Starting the core…</source>
        <translation>正在啟動核心…</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Connecting to the core… (attempt %1)</source>
        <translation>正在連線到核心… (第 %1 次嘗試)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Connecting to the core…</source>
        <translation>正在連線到核心…</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Waiting for the core to answer…</source>
        <translation>正在等待核心回應…</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Connected to the core</source>
        <translation>已連線到核心</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The core is not responding — retrying (attempt %1)</source>
        <translation>核心沒有回應 — 正在重試 (第 %1 次嘗試)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The core refused the connection: %1</source>
        <translation>核心拒絕了連線：%1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>The core is still loading its files…</source>
        <translation>核心仍在載入檔案…</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>The core is loading: client credits…</source>
        <translation>核心正在載入：客戶端額度…</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The core is loading: known files…</source>
        <translation>核心正在載入：已知檔案…</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The core is loading: shared files…</source>
        <translation>核心正在載入：已分享檔案…</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The core is loading: downloads…</source>
        <translation>核心正在載入：下載…</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The core is starting its networks…</source>
        <translation>核心正在啟動網路…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The core is loading its files…</source>
        <translation>核心正在載入檔案…</translation>
    </message>
    <message>
        <location filename="../src/gui/app/ToolbarButtons.h" line="+1"/>
        <source>Kad</source>
        <translation>Kad</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Servers</source>
        <translation>伺服器</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Transfers</source>
        <translation>傳輸</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Search</source>
        <translation>搜尋</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Shared Files</source>
        <translation>已分享檔案</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Messages</source>
        <translation>訊息</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>IRC</source>
        <translation>IRC</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Statistics</source>
        <translation>統計</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Usenet</source>
        <translation>Usenet</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Downloads Folder</source>
        <translation>下載資料夾</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Options</source>
        <translation>選項</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Tools</source>
        <translation>工具</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Help</source>
        <translation>說明</translation>
    </message>
</context>
<context>
    <name>eMule::MediaInfoPanel</name>
    <message>
        <location filename="../src/gui/dialogs/MediaInfoPanel.cpp" line="+44"/>
        <source>Scanning...</source>
        <translation>正在掃描...</translation>
    </message>
    <message>
        <location line="+17"/>
        <location line="+178"/>
        <source>No media information available.</source>
        <translation>沒有可用的媒體資訊。</translation>
    </message>
    <message>
        <location line="-146"/>
        <source>estimated</source>
        <translation>估計</translation>
    </message>
    <message>
        <location line="+164"/>
        <source>General</source>
        <translation>一般</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Format:</source>
        <translation>格式：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Length:</source>
        <translation>長度：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Video</source>
        <translation>影像</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+17"/>
        <source>Codec:</source>
        <translation>編解碼器：</translation>
    </message>
    <message>
        <location line="-16"/>
        <location line="+17"/>
        <source>Bitrate:</source>
        <translation>位元率：</translation>
    </message>
    <message>
        <location line="-16"/>
        <source>Resolution:</source>
        <translation>解析度：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Aspect Ratio:</source>
        <translation>長寬比：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>FPS:</source>
        <translation>FPS：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Audio</source>
        <translation>音樂</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Channels:</source>
        <translation>聲道：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Sample Rate:</source>
        <translation>取樣率：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Language:</source>
        <translation>語言：</translation>
    </message>
</context>
<context>
    <name>eMule::MessagesPanel</name>
    <message>
        <location filename="../src/gui/panels/MessagesPanel.cpp" line="+154"/>
        <location line="+547"/>
        <source> ...failed</source>
        <translation> ...失敗</translation>
    </message>
    <message>
        <location line="-545"/>
        <source>Me</source>
        <translation>我</translation>
    </message>
    <message>
        <location line="+96"/>
        <source>Friends (0)</source>
        <translation>好友 (0)</translation>
    </message>
    <message>
        <location line="+41"/>
        <source>Info</source>
        <translation>資訊</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Name:</source>
        <translation>名稱：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hash:</source>
        <translation>切細值：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Software:</source>
        <translation>軟體：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Identification:</source>
        <translation>身分識別：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Uploaded:</source>
        <translation>已上傳：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloaded:</source>
        <translation>已下載：</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Messages</source>
        <translation>訊息</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>Smileys</source>
        <translation>表情符號</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Type a message...</source>
        <translation>輸入訊息...</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Send</source>
        <translation>傳送</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+246"/>
        <source>Close</source>
        <translation>關閉</translation>
    </message>
    <message>
        <location line="-222"/>
        <location line="+190"/>
        <source>&amp;Details...</source>
        <translation>詳細資料(&amp;D)...</translation>
    </message>
    <message>
        <location line="-186"/>
        <source>Add...</source>
        <translation>加入...</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+203"/>
        <source>Remove</source>
        <translation>移除</translation>
    </message>
    <message>
        <location line="-200"/>
        <source>Send Message</source>
        <translation>傳送訊息</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>View Shared Files</source>
        <translation>檢視已分享檔案</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>cannot view shared files.</source>
        <translation>無法檢視已分享檔案。</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Establish Friend Slot</source>
        <translation>建立好友位置</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Find...</source>
        <translation>尋找...</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>Friends (%1)</source>
        <translation>好友 (%1)</translation>
    </message>
    <message>
        <location line="+59"/>
        <source>Successful</source>
        <translation>成功</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Invalid</source>
        <translation>無效</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not supported or disabled</source>
        <translation>不支援或已停用</translation>
    </message>
    <message>
        <location line="+44"/>
        <source>Add to friends list</source>
        <translation>加入到好友清單</translation>
    </message>
    <message>
        <location line="+105"/>
        <source>*** Connecting</source>
        <translation>*** 連線中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>*** Authenticating friend</source>
        <translation>*** 正在驗證好友</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>*** Searching friend in Kad</source>
        <translation>*** 正在 Kad 中搜尋好友</translation>
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
        <translation>*** 為了避免廣告訊息，這個使用者需要您在傳送訊息前先回答驗證碼。請輸入您在這張圖片中看到的字母作為回覆：</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>*** You have passed the captcha check and the user has received your message.</source>
        <translation>*** 您已通過驗證碼檢查，這個使用者已收到您的訊息。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>*** Your response to the captcha was wrong and your message has been ignored. You can request a new captcha by sending a new message.</source>
        <translation>*** 您回覆的驗證碼不正確，您的訊息已被忽略。您可以傳送新訊息來要求新的驗證碼。</translation>
    </message>
</context>
<context>
    <name>eMule::MetaAccountDialog</name>
    <message>
        <location filename="../src/gui/dialogs/MetaAccountDialog.cpp" line="+64"/>
        <source>User name:</source>
        <translation>使用者名稱：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Password:</source>
        <translation>密碼：</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Log In</source>
        <translation>登入</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Check Again</source>
        <translation>重新檢查</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Log Out</source>
        <translation>登出</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>eNode Account — %1</source>
        <translation>eNode 帳號 — %1</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>&lt;b&gt;%1&lt;/b&gt; needs an account to download torrent and Usenet search results. Log in, or register an account on the server&apos;s website.</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; 需要帳號才能下載種子和 Usenet 搜尋結果。請登入，或在伺服器網站上註冊帳號。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Log in to your account on &lt;b&gt;%1&lt;/b&gt;.</source>
        <translation>登入您在 &lt;b&gt;%1&lt;/b&gt; 上的帳號。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>is not active yet</source>
        <translation>尚未啟用</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>has expired</source>
        <translation>已過期</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>has been disabled by the operator</source>
        <translation>已被營運者停用</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Your account &lt;b&gt;%1&lt;/b&gt; on %2 %3. Complete the steps below on the server&apos;s website, then choose Check Again.</source>
        <translation>您在 %2 上的帳號 &lt;b&gt;%1&lt;/b&gt; %3。請在伺服器網站上完成下列步驟，然後選擇「重新檢查」。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>no expiry</source>
        <translation>無期限</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Logged in to &lt;b&gt;%1&lt;/b&gt; as &lt;b&gt;%2&lt;/b&gt;.&lt;br&gt;Access until: %3</source>
        <translation>已以 &lt;b&gt;%2&lt;/b&gt; 身分登入 &lt;b&gt;%1&lt;/b&gt;。&lt;br&gt;存取期限：%3</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>&lt;b&gt;%1&lt;/b&gt; needs no account — its torrent and Usenet results are free to download.</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; 不需要帳號 — 其種子和 Usenet 結果可免費下載。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Checking the account on &lt;b&gt;%1&lt;/b&gt;...</source>
        <translation>正在檢查 &lt;b&gt;%1&lt;/b&gt; 上的帳號...</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Open steps:</source>
        <translation>待完成步驟：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Register an account</source>
        <translation>註冊帳號</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Open account page</source>
        <translation>開啟帳號頁面</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Close</source>
        <translation>關閉</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Not connected to the eMule core.</source>
        <translation>未連線到 eMule 核心。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Enter a user name and a password.</source>
        <translation>請輸入使用者名稱和密碼。</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Logging in...</source>
        <translation>正在登入...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>The connection to the eMule core was lost.</source>
        <translation>與 eMule 核心的連線已中斷。</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>Checking...</source>
        <translation>正在檢查...</translation>
    </message>
</context>
<context>
    <name>eMule::MetaResultActions</name>
    <message>
        <location filename="../src/gui/utils/MetaResultActions.cpp" line="+62"/>
        <source>NZB files (*.nzb)</source>
        <translation>NZB 檔案 (*.nzb)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Link containers (*.dlc *.ccf *.rsdf)</source>
        <translation>連結容器 (*.dlc *.ccf *.rsdf)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Torrent files (*.torrent)</source>
        <translation>種子檔案 (*.torrent)</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Save NZB File</source>
        <translation>儲存 NZB 檔案</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Save Link Container</source>
        <translation>儲存連結容器</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Save Torrent File</source>
        <translation>儲存種子檔案</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>Save Files To</source>
        <translation>檔案儲存至</translation>
    </message>
    <message numerus="yes">
        <location line="+23"/>
        <source>Copied %n magnet link(s) to the clipboard.</source>
        <translation>
            <numerusform>已將 %n 個磁力連結複製到剪貼簿。</numerusform>
        </translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Queued &quot;%1&quot; for Usenet download.</source>
        <translation>已將「%1」加入 Usenet 下載佇列。</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Download</source>
        <translation>下載</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>%1

Download it again?</source>
        <translation>%1

要重新下載嗎？</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Could not queue &quot;%1&quot;: %2</source>
        <translation>無法將「%1」加入佇列：%2</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Could not download &quot;%1&quot;: %2</source>
        <translation>無法下載「%1」：%2</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Could not write %1: %2</source>
        <translation>無法寫入 %1：%2</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Saved %1</source>
        <translation>已儲存 %1</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Could not fetch the links of &quot;%1&quot;: %2</source>
        <translation>無法取得「%1」的連結：%2</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>&quot;%1&quot; holds no links.</source>
        <translation>「%1」不包含任何連結。</translation>
    </message>
    <message>
        <location line="+57"/>
        <source>Search</source>
        <translation>搜尋</translation>
    </message>
</context>
<context>
    <name>eMule::MetadataPage</name>
    <message>
        <location filename="../src/gui/dialogs/MetadataPage.cpp" line="+108"/>
        <source>No metadata tags available.</source>
        <translation>沒有可用的中繼資料標籤。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Tag Name</source>
        <translation>標籤名稱</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type</source>
        <translation>類型</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Value</source>
        <translation>數值</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Unknown</source>
        <translation>未知</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>Copy</source>
        <translation>複製</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Select All</source>
        <translation>全選</translation>
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
        <translation>已連線</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Upload</source>
        <translation>上傳</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Download</source>
        <translation>下載</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Completed</source>
        <translation>已完成</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Free Space</source>
        <translation>可用空間</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Restore Window</source>
        <translation>還原視窗</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Open Incoming Folder</source>
        <translation>開啟新進檔案資料夾</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Options</source>
        <translation>選項</translation>
    </message>
</context>
<context>
    <name>eMule::NetworkInfoDialog</name>
    <message>
        <location filename="../src/gui/dialogs/NetworkInfoDialog.cpp" line="+57"/>
        <source>Network Information</source>
        <translation>網路資訊</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>&lt;i&gt;Loading...&lt;/i&gt;</source>
        <translation>&lt;i&gt;載入中...&lt;/i&gt;</translation>
    </message>
    <message>
        <location line="+15"/>
        <location line="+10"/>
        <source>&lt;b&gt;Not connected to daemon.&lt;/b&gt;</source>
        <translation>&lt;b&gt;未連線到常駐程式。&lt;/b&gt;</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Client</source>
        <translation>客戶端</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Nick:</source>
        <translation>暱稱：</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+231"/>
        <source>Hash:</source>
        <translation>切細值：</translation>
    </message>
    <message>
        <location line="-229"/>
        <source>TCP Port:</source>
        <translation>TCP 連接埠：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>UDP Port:</source>
        <translation>UDP 連接埠：</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>eD2K Network</source>
        <translation>eD2K 網路</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+172"/>
        <source>Connected</source>
        <translation>已連線</translation>
    </message>
    <message>
        <location line="-170"/>
        <location line="+129"/>
        <location line="+39"/>
        <source>Connecting</source>
        <translation>連線中</translation>
    </message>
    <message>
        <location line="-166"/>
        <location line="+129"/>
        <source>Disconnected</source>
        <translation>已中斷連線</translation>
    </message>
    <message>
        <location line="-128"/>
        <location line="+129"/>
        <location line="+114"/>
        <source>Status:</source>
        <translation>狀態：</translation>
    </message>
    <message>
        <location line="-237"/>
        <location line="+58"/>
        <location line="+147"/>
        <source>Users:</source>
        <translation>使用者數：</translation>
    </message>
    <message>
        <location line="-204"/>
        <location line="+59"/>
        <location line="+147"/>
        <source>Files:</source>
        <translation>檔案數：</translation>
    </message>
    <message>
        <location line="-199"/>
        <location line="+2"/>
        <location line="+43"/>
        <location line="+89"/>
        <source>IP:Port:</source>
        <translation>IP:連接埠：</translation>
    </message>
    <message>
        <location line="-134"/>
        <location line="+26"/>
        <source>Unknown</source>
        <translation>未知</translation>
    </message>
    <message>
        <location line="-19"/>
        <location line="+131"/>
        <source>ID:</source>
        <translation>ID：</translation>
    </message>
    <message>
        <location line="-129"/>
        <source>Low ID</source>
        <translation>低-ID</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>High ID</source>
        <translation>高-ID</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+4"/>
        <location line="+84"/>
        <location line="+17"/>
        <source>Open</source>
        <translation>開放</translation>
    </message>
    <message>
        <location line="-103"/>
        <location line="+86"/>
        <location line="+15"/>
        <source>Firewalled</source>
        <translation>防火牆內</translation>
    </message>
    <message>
        <location line="-99"/>
        <location line="+103"/>
        <source>unverified</source>
        <translation>未驗證</translation>
    </message>
    <message>
        <location line="-100"/>
        <source>IPv6:</source>
        <translation>IPv6：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>IPv6 Status:</source>
        <translation>IPv6 狀態：</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>eD2K Server</source>
        <translation>eD2K 伺服器</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Name:</source>
        <translation>名稱：</translation>
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
        <translation>連線數：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Obfuscated</source>
        <translation>已模糊化</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Normal</source>
        <translation>普通</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Low ID:</source>
        <translation>低-ID：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Ping:</source>
        <translation>Ping：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1 ms</source>
        <translation>%1 毫秒</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>eD2K Server Features</source>
        <translation>eD2K 伺服器功能</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Soft/Hard File Limits:</source>
        <translation>軟/硬檔案限制：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>TCP compression</source>
        <translation>TCP 壓縮</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Short tags</source>
        <translation>短標籤</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Integer type tags</source>
        <translation>整數類型標籤</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+1"/>
        <source>Extended UDP protocol for source requests</source>
        <translation>用於來源要求的延伸 UDP 協定</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Extended UDP protocol for file requests</source>
        <translation>用於檔案要求的延伸 UDP 協定</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Support for large files</source>
        <translation>支援大型檔案</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+1"/>
        <source>Protocol Obfuscation</source>
        <translation>模糊協定</translation>
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
        <translation>Kad 網路</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>LAN Mode</source>
        <translation>區域網路模式</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>UDP Status:</source>
        <translation>UDP 狀態：</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Extern UDP Port:</source>
        <translation>外部 UDP 連接埠：</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>None</source>
        <translation>無</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Buddy:</source>
        <translation>夥伴：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Buddies served:</source>
        <translation>已服務的夥伴：</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>%1 (Experimental: %2)</source>
        <translation>%1（實驗性：%2）</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Indexed:</source>
        <translation>已索引：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Source:</source>
        <translation>來源：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Keyword:</source>
        <translation>關鍵字：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Notes:</source>
        <translation>備註：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Load:</source>
        <translation>負載：</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Web Interface</source>
        <translation>Web 介面</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Enabled</source>
        <translation>已啟用</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Disabled</source>
        <translation>已停用</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>not running</source>
        <translation>未執行</translation>
    </message>
    <message numerus="yes">
        <location line="+4"/>
        <source>%n active session(s)</source>
        <translation>
            <numerusform>%n 個作用中工作階段</numerusform>
        </translation>
    </message>
    <message>
        <location line="+10"/>
        <source>URL:</source>
        <translation>網址：</translation>
    </message>
</context>
<context>
    <name>eMule::NzbFileChooserDialog</name>
    <message>
        <location filename="../src/gui/dialogs/NzbFileChooserDialog.cpp" line="+42"/>
        <source>Choose Files</source>
        <translation>選擇檔案</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Unchecked files are not downloaded. The volumes of one archive are checked together, and PAR2 files are fetched only when a repair needs them.</source>
        <translation>未勾選的檔案不會被下載。同一壓縮檔的各個分卷會一起勾選，PAR2 檔案只在需要修復時才會取得。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Name</source>
        <translation>名稱</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Reading…</source>
        <translation>正在讀取…</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>PAR2 files are fetched when a repair needs them.</source>
        <translation>PAR2 檔案只在需要修復時才會取得。</translation>
    </message>
    <message>
        <location line="+20"/>
        <location line="+20"/>
        <source>Not connected to the eMule core.</source>
        <translation>未連線到 eMule 核心。</translation>
    </message>
    <message>
        <location line="-14"/>
        <source>Cannot read %1.</source>
        <translation>無法讀取 %1。</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>This file could not be read.</source>
        <translation>無法讀取這個檔案。</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Keep at least one file of &quot;%1&quot;.</source>
        <translation>請為「%1」至少保留一個檔案。</translation>
    </message>
</context>
<context>
    <name>eMule::OptionsDialog</name>
    <message>
        <location filename="../src/gui/dialogs/OptionsDialog.cpp" line="-2983"/>
        <source>Options</source>
        <translation>選項</translation>
    </message>
    <message>
        <location line="+61"/>
        <location line="+1953"/>
        <source>OK</source>
        <translation>確定</translation>
    </message>
    <message>
        <location line="-1952"/>
        <location line="+1953"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="-1952"/>
        <location line="+5612"/>
        <source>Apply</source>
        <translation>套用</translation>
    </message>
    <message>
        <location line="-5611"/>
        <source>Help</source>
        <translation>說明</translation>
    </message>
    <message>
        <location line="+257"/>
        <location line="+1963"/>
        <location line="+62"/>
        <location line="+5"/>
        <location line="+9"/>
        <location line="+11"/>
        <source>IP Filter</source>
        <translation>IP 過濾器</translation>
    </message>
    <message>
        <location line="-2049"/>
        <source>IP filter reloaded: %1 entries.</source>
        <translation>IP 過濾器已重新載入：%1 個項目。</translation>
    </message>
    <message>
        <location line="+89"/>
        <source>Options -&gt; %1 -&gt; %2</source>
        <translation>選項 -&gt; %1 -&gt; %2</translation>
    </message>
    <message>
        <location line="+177"/>
        <source>General options</source>
        <translation>一般選項</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Advanced options</source>
        <translation>進階選項</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Usenet</source>
        <translation>Usenet</translation>
    </message>
    <message>
        <location line="+74"/>
        <source>User Name</source>
        <translation>使用者名稱</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+5538"/>
        <source>Language</source>
        <translation>語言</translation>
    </message>
    <message>
        <location line="-5535"/>
        <source>System Default</source>
        <translation>系統預設</translation>
    </message>
    <message>
        <location line="+14"/>
        <location line="+719"/>
        <location line="+314"/>
        <location line="+450"/>
        <location line="+248"/>
        <source>Miscellaneous</source>
        <translation>其他</translation>
    </message>
    <message>
        <location line="-1728"/>
        <source>Bring to front on link click</source>
        <translation>點選連結時將視窗移至最前</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Prompt on exit</source>
        <translation>離開時提示</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Enable online signature</source>
        <translation>啟用上線簽名檔案</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Enable MiniMule</source>
        <translation>啟用 MiniMule</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Prevent standby mode while running</source>
        <translation>執行時避免進入待機模式</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Edit Web Services...</source>
        <translation>編輯 Web 服務...</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Web Services</source>
        <translation>Web 服務</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>webservices.dat was not found in the config folder.</source>
        <translation>在設定資料夾中找不到 webservices.dat。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Handle eD2K Links</source>
        <translation>處理 eD2K 連結</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Startup</source>
        <translation>啟動</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Check for new version</source>
        <translation>檢查新版本</translation>
    </message>
    <message>
        <location line="+4"/>
        <source> Days</source>
        <translation> 天</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Show splash screen</source>
        <translation>顯示啟動畫面</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Start minimized</source>
        <translation>啟動時最小化</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Start with macOS</source>
        <translation>隨 macOS 啟動</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Start with Windows</source>
        <translation>隨 Windows 啟動</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Start with system</source>
        <translation>隨系統啟動</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+5474"/>
        <source>Core</source>
        <translation>核心</translation>
    </message>
    <message>
        <location line="-5469"/>
        <source>Address:</source>
        <translation>位址：</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+1210"/>
        <location line="+720"/>
        <location line="+421"/>
        <source>Port:</source>
        <translation>連接埠：</translation>
    </message>
    <message>
        <location line="-2348"/>
        <source>authentication token</source>
        <translation>驗證權杖</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Token:</source>
        <translation>權杖：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Changes require a restart to take effect.</source>
        <translation>變更需要重新啟動才會生效。</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+9"/>
        <source>Shutdown eMule Core</source>
        <translation>關閉 eMule 核心</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>This will shut down both the eMule Core and the GUI.

Are you sure you want to continue?</source>
        <translation>這將同時關閉 eMule 核心和圖形介面。

您確定要繼續嗎？</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>Progressbar style</source>
        <translation>進度條樣式</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>flat</source>
        <translation>平面</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>round</source>
        <translation>圓角</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Tooltip delay time [sec.]</source>
        <translation>工具提示延遲時間 [秒]</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Minimize to system tray</source>
        <translation>最小化到系統列</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Download list double-click to expand</source>
        <translation>下載清單點兩下來展開</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show percentage of download completion in progressbar</source>
        <translation>在進度條顯示下載完成百分比</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show downloaded data of unfinished parts and part boundaries</source>
        <translation>顯示未完成部分的已下載資料和部分邊界</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Colours the data already downloaded in incomplete parts (green while being requested, grey otherwise) and marks every part boundary on the progress line.</source>
        <translation>為未完成部分中已下載的資料著色（要求中為綠色，否則為灰色），並在進度線上標示每個部分的邊界。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Show transfer rates on title</source>
        <translation>在標題列顯示傳輸速率</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show download info on category tabs</source>
        <translation>在類別分頁上顯示下載資訊</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Auto clear completed downloads</source>
        <translation>自動清除已完成的下載</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show additional toolbar on Transfers window</source>
        <translation>在傳輸視窗顯示額外工具列</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show speed graph in toolbar</source>
        <translation>在工具列顯示速度圖表</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Speed graph time range (minutes):</source>
        <translation>速度圖表時間範圍（分鐘）：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Remember open searches between restarts</source>
        <translation>重新啟動後記住開啟的搜尋</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Use original eMule icons</source>
        <translation>使用原版 eMule 圖示</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show more controls (advanced mode controls)</source>
        <translation>顯示更多控制項（進階模式控制項）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Shows advanced entries in context menus and extra details in lists.</source>
        <translation>在右鍵選單中顯示進階項目，並在清單中顯示更多詳細資訊。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Country flags (IP2Country)</source>
        <translation>國旗 (IP2Country)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Show country flags</source>
        <translation>顯示國旗</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>MaxMind account ID:</source>
        <translation>MaxMind 帳號 ID：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>License key:</source>
        <translation>授權金鑰：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Update the database weekly</source>
        <translation>每週更新資料庫</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Update now</source>
        <translation>立即更新</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>A database ships with each release. An optional free &lt;a href=&quot;https://www.maxmind.com/en/geolite2/signup&quot;&gt;MaxMind GeoLite2&lt;/a&gt; account keeps it current in between. This product includes GeoLite2 data created by MaxMind.</source>
        <translation>每個版本都附帶一個資料庫。可選用的免費 &lt;a href=&quot;https://www.maxmind.com/en/geolite2/signup&quot;&gt;MaxMind GeoLite2&lt;/a&gt; 帳號能在版本之間保持資料庫為最新。本產品包含由 MaxMind 建立的 GeoLite2 資料。</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Save CPU &amp;&amp; Memory Usage</source>
        <translation>節省 CPU &amp;&amp; 記憶體用量</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Disable Known Clients list</source>
        <translation>停用已知客戶端清單</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Disable Queue list</source>
        <translation>停用佇列清單</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Appearance</source>
        <translation>外觀</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Theme:</source>
        <translation>主題：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>System default</source>
        <translation>系統預設</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Light</source>
        <translation>淺色</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Dark</source>
        <translation>深色</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Font size:</source>
        <translation>字型大小：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>%1 %</source>
        <translation>%1 %</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Text size of lists, menus and dialogs. Takes effect after a restart.</source>
        <translation>清單、選單和對話框的文字大小。重新啟動後生效。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Font for Server-, Message- and IRC-Window</source>
        <translation>伺服器、訊息及 IRC 視窗的字型</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Select Font...</source>
        <translation>選擇字型...</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Select Font</source>
        <translation>選擇字型</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Auto completion (history function)</source>
        <translation>自動完成（記錄輸入過的文字）</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+1687"/>
        <location line="+128"/>
        <location line="+295"/>
        <location line="+1016"/>
        <location line="+152"/>
        <location line="+1047"/>
        <location line="+277"/>
        <location line="+29"/>
        <source>Enabled</source>
        <translation>已啟用</translation>
    </message>
    <message>
        <location line="-4629"/>
        <source>Reset</source>
        <translation>重置</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>Capacities</source>
        <translation>能力</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Download</source>
        <translation>下載</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+6"/>
        <location line="+4354"/>
        <source> KB/s</source>
        <translation> KB/s</translation>
    </message>
    <message>
        <location line="-4357"/>
        <source>Upload</source>
        <translation>上傳</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Limits</source>
        <translation>限制</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Download limit</source>
        <translation>下載限制</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Upload limit</source>
        <translation>上傳限制</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Target rate per upload slot</source>
        <translation>每個上傳槽的目標速率</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>A higher rate opens fewer upload slots: the upload limit divided by this rate, never fewer than two. Unchecked, the rate grows with the number of slots.</source>
        <translation>速率越高，開啟的上傳槽越少：上傳槽數為上傳限制除以此速率，且不少於兩個。未勾選時，速率會隨上傳槽數增加。</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Client Port</source>
        <translation>客戶端連接埠</translation>
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
        <translation>停用</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Test Ports</source>
        <translation>測試連接埠</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Use UPnP to Setup Ports</source>
        <translation>使用 UPnP 設定連接埠</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Port forwarding: unknown</source>
        <translation>連接埠轉送：未知</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Network interface</source>
        <translation>網路介面</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+6593"/>
        <source>Any</source>
        <translation>任何</translation>
    </message>
    <message>
        <location line="-6591"/>
        <source>Use only this network interface, for example a VPN tunnel. Pick one from the list, or type an IP address or a subnet such as 10.64.0.0/10 (the interface holding an address in it). Connections through a proxy are not bound.</source>
        <translation>只使用這個網路介面，例如 VPN 通道。可從清單中選擇，或輸入 IP 位址或子網路，例如 10.64.0.0/10（即擁有該範圍內位址的介面）。透過代理的連線不會被綁定。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>While the selected interface is not available, all P2P, Usenet and update connections stay closed.</source>
        <translation>在選取的介面無法使用期間，所有 P2P、Usenet 和更新連線都會保持關閉。</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Max. Sources/File</source>
        <translation>每個檔案最大來源數</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Hard limit</source>
        <translation>硬性限制</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Connection Limits</source>
        <translation>連線限制</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Max. connections</source>
        <translation>最大連線數</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Autoconnect on startup</source>
        <translation>啟動時自動連線</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Reconnect on loss</source>
        <translation>斷線時重新連線</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Show overhead bandwidth</source>
        <translation>顯示額外支出的頻寬</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Wizard...</source>
        <translation>精靈...</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Network</source>
        <translation>網路</translation>
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
        <translation>獨立的 IPv6 佇列</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Alternate freed upload slots between IPv4 and IPv6 clients when both are waiting, so IPv6 peers are not outbid on score alone. When only one family is waiting, no slot is held back.</source>
        <translation>當 IPv4 和 IPv6 客戶端同時等候時，在兩者之間輪流分配釋出的上傳槽，以免 IPv6 節點只因評分而落選。若只有一種協定在等候，則不會保留任何上傳槽。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Use IPv6 privacy address</source>
        <translation>使用 IPv6 隱私位址</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Connect to IPv6 peers and servers from the rotating temporary (RFC 4941) address. Off: always use the stable address, so peers can reach the address we advertise.</source>
        <translation>使用輪替的暫時位址 (RFC 4941) 連線到 IPv6 節點和伺服器。關閉：一律使用固定位址，讓節點能連到我們公布的位址。</translation>
    </message>
    <message>
        <location line="+62"/>
        <location line="+1409"/>
        <source>General</source>
        <translation>一般</translation>
    </message>
    <message>
        <location line="-1406"/>
        <source>Enable proxy</source>
        <translation>啟用代理</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Proxy type:</source>
        <translation>代理類型：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>No Proxy</source>
        <translation>不使用代理</translation>
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
        <translation>代理主機：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Proxy port:</source>
        <translation>代理連接埠：</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Use for news servers</source>
        <translation>同時用於新聞伺服器</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Route Usenet downloads, availability checks and the news server Test button through this proxy too. News servers switch over as soon as you press OK.

Every Usenet connection then passes through the proxy, so its speed caps the download, and many HTTP proxies only allow connections to port 443.</source>
        <translation>Usenet 下載、可用性檢查和新聞伺服器的「測試」按鈕也都透過這個代理。按下「確定」後，新聞伺服器會立即切換。

此後每個 Usenet 連線都會經過代理，因此代理的速度會限制下載速度，而且許多 HTTP 代理只允許連線到連接埠 443。</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+687"/>
        <source>Authentication</source>
        <translation>驗證</translation>
    </message>
    <message>
        <location line="-684"/>
        <source>Enable authentication</source>
        <translation>啟用驗證</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+1785"/>
        <location line="+1019"/>
        <location line="+152"/>
        <source>Name:</source>
        <translation>名稱：</translation>
    </message>
    <message>
        <location line="-2952"/>
        <location line="+688"/>
        <location line="+778"/>
        <location line="+19"/>
        <location line="+324"/>
        <source>Password:</source>
        <translation>密碼：</translation>
    </message>
    <message>
        <location line="-1775"/>
        <source>Update</source>
        <translation>更新</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Remove dead servers after</source>
        <translation>刪除失效伺服器於</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>retries</source>
        <translation>次重試</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Auto-update server list at startup</source>
        <translation>啟動時自動更新伺服器清單</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>List...</source>
        <translation>清單...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Server List URL</source>
        <translation>伺服器清單網址</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Enter the URL for server.met download:</source>
        <translation>輸入 server.met 下載網址：</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Update server list when connecting to a server</source>
        <translation>連線到伺服器時更新伺服器清單</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Update server list when a client connects</source>
        <translation>客戶端連線時更新伺服器清單</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Use smart LowID check on connect</source>
        <translation>連線時使用智慧型低-ID 檢查</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Prefer IPv6 when IPv4 gives a Low ID</source>
        <translation>IPv4 取得低-ID 時優先使用 IPv6</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Reconnect to a server over its IPv6 address when the IPv4 connection got a Low ID</source>
        <translation>當 IPv4 連線取得低-ID 時，透過伺服器的 IPv6 位址重新連線</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Safe Connect</source>
        <translation>安全的連線</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Autoconnect to servers in static list only</source>
        <translation>只自動連線到靜態伺服器</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Use priority system</source>
        <translation>使用優先權系統</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Use the manual server order (drag/Move Up-Down)</source>
        <translation>使用手動伺服器排序（拖曳/上移-下移）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Set manually added servers to high priority</source>
        <translation>將手動新增的伺服器設為高優先權</translation>
    </message>
    <message>
        <location line="+129"/>
        <source>Incoming Files</source>
        <translation>新進檔案</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Select Incoming Directory</source>
        <translation>選擇新進檔案資料夾</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Temporary Files</source>
        <translation>暫存檔案</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Select Temporary Directory</source>
        <translation>選擇暫存資料夾</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Shared Directories (Ctrl+Click includes subdirectories)</source>
        <translation>已分享資料夾（按住 Ctrl 再點選則包含子資料夾）</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Add UNC share</source>
        <translation>新增 UNC 分享</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Add UNC Share</source>
        <translation>新增 UNC 分享</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Enter UNC path (e.g., \\server\share):</source>
        <translation>輸入 UNC 路徑（例如 \\server\share）：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Invalid Path</source>
        <translation>無效的路徑</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>A UNC path must start with \\.</source>
        <translation>UNC 路徑必須以 \\ 開頭。</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>UNC shares are only supported on Windows</source>
        <translation>UNC 分享只在 Windows 上支援</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Initializations</source>
        <translation>初始化</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Add files to download in paused mode</source>
        <translation>以暫停模式加入下載檔案</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Add new shared files with auto priority</source>
        <translation>加入新的分享檔案時自動設定優先權</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Add new downloads with auto priority</source>
        <translation>加入新的下載時自動設定優先權</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Remember download sources between restarts</source>
        <translation>重新啟動後記住下載來源</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Stores each download&apos;s best sources in the temp folder and reconnects to them on the next start, so a rare file does not have to find its peers again.</source>
        <translation>將每個下載的最佳來源儲存在暫存資料夾中，並在下次啟動時重新連線，這樣稀有檔案就不必重新尋找節點。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Auto cleanup file names of new downloads</source>
        <translation>自動清理新下載的檔案名稱</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+588"/>
        <source>Edit...</source>
        <translation>編輯...</translation>
    </message>
    <message>
        <location line="-577"/>
        <source>Try to transfer full chunks to all uploads</source>
        <translation>嘗試對所有上傳傳輸完整區塊</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Try to download preview chunks first</source>
        <translation>嘗試首先下載預覽區塊</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Watch clipboard for eD2K links</source>
        <translation>監視剪貼簿中的 eD2K 連結</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Use advanced calculation method for remaining time</source>
        <translation>使用進階方法計算剩餘時間</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Start next paused file when a file completes</source>
        <translation>檔案完成時開始下一個暫停的檔案</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Prefer same category</source>
        <translation>偏好使用相同類別</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Only in same category</source>
        <translation>只限相同類別</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Remember downloaded files</source>
        <translation>記住已下載的檔案</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Remember cancelled files</source>
        <translation>記住已取消的檔案</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Remember files seen in search results</source>
        <translation>記住搜尋結果中出現過的檔案</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Keeps a local record of the files that turned up in your searches and the names they went by, so later searches can show what you have seen before.</source>
        <translation>在本機記錄搜尋中出現過的檔案及其當時的名稱，讓之後的搜尋能顯示您以前看過的內容。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Video Player</source>
        <translation>影片播放器</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Command</source>
        <translation>命令</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Select Video Player</source>
        <translation>選擇影片播放器</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Arguments</source>
        <translation>參數</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Pop-up Message</source>
        <translation>訊息彈出</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>No sound</source>
        <translation>無聲音</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Test</source>
        <translation>測試</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Play sound</source>
        <translation>播放聲音</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Speak notification message</source>
        <translation>朗讀通知訊息</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Select Sound File</source>
        <translation>選擇聲音檔案</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Sound Files (*.wav *.mp3 *.ogg);;All Files (*)</source>
        <translation>聲音檔案 (*.wav *.mp3 *.ogg);;所有檔案 (*)</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Pop-up when</source>
        <translation>彈出當</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Log entry added</source>
        <translation>新增記錄項目</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Chat session started</source>
        <translation>聊天工作階段已開始</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Chat message received</source>
        <translation>收到聊天訊息</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Download added</source>
        <translation>已加入下載</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Download finished (*)</source>
        <translation>下載已完成 (*)</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>New eMule Qt version detected</source>
        <translation>偵測到新的 eMule Qt 版本</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Urgent: out of disk space, server connection lost (*)</source>
        <translation>緊急：磁碟空間不足、伺服器連線中斷 (*)</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>(*) Email Notifications</source>
        <translation>(*) 電子郵件通知</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Enable email notifications</source>
        <translation>啟用電子郵件通知</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>SMTP server...</source>
        <translation>SMTP 伺服器...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Recipient address:</source>
        <translation>收件者地址：</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Sender address:</source>
        <translation>寄件者地址：</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>SMTP Server Settings</source>
        <translation>SMTP 伺服器設定</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Connection</source>
        <translation>連線</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Server:</source>
        <translation>伺服器：</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+14"/>
        <location line="+1173"/>
        <source>None</source>
        <translation>無</translation>
    </message>
    <message>
        <location line="-1185"/>
        <source>Security:</source>
        <translation>安全性：</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Authentication method:</source>
        <translation>驗證方式：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Username:</source>
        <translation>使用者名稱：</translation>
    </message>
    <message>
        <location line="+45"/>
        <source>Connect to help channel</source>
        <translation>連線到幫助頻道</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Load server channel list on connect</source>
        <translation>連線時載入伺服器頻道清單</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Add timestamp to messages</source>
        <translation>在訊息上加入時間戳記</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Ignore info messages</source>
        <translation>忽略資訊訊息</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ignore misc. info messages</source>
        <translation>忽略雜項資訊訊息</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Ignore Join info messages</source>
        <translation>忽略加入資訊訊息</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Ignore Part info messages</source>
        <translation>忽略告別資訊訊息</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Ignore Quit info messages</source>
        <translation>忽略離開資訊訊息</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Ignore Ping? Pong! messages</source>
        <translation>忽略 Ping? Pong! 訊息</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Ignore eMule protocol messages</source>
        <translation>忽略 eMule 協定訊息</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Ignore eMule add friend protocol messages</source>
        <translation>忽略 eMule 加入好友的協定訊息</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Ignore eMule send link protocol messages</source>
        <translation>忽略 eMule 傳送連結的協定訊息</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Allow others to add you as a friend</source>
        <translation>允許其他人將您加為好友</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Accept eD2K links in IRC (Use only with caution!)</source>
        <translation>在 IRC 中接受 eD2K 連結（使用時請小心！）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>From friends only</source>
        <translation>只限來自好友</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+164"/>
        <source>Show smileys</source>
        <translation>顯示表情符號</translation>
    </message>
    <message>
        <location line="-162"/>
        <source>Play sound events</source>
        <translation>播放聲音事件</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Enable UTF-8</source>
        <translation>啟用 UTF-8</translation>
    </message>
    <message>
        <location line="+31"/>
        <source>Server</source>
        <translation>伺服器</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Nick</source>
        <translation>暱稱</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Channels</source>
        <translation>頻道</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Use channel list filter</source>
        <translation>使用頻道清單過濾器</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+922"/>
        <location line="+1027"/>
        <location line="+154"/>
        <source>Name</source>
        <translation>名稱</translation>
    </message>
    <message>
        <location line="-2101"/>
        <source>Users</source>
        <translation>使用者</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Perform</source>
        <translation>執行</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Use perform string on connect</source>
        <translation>連線時使用執行字串</translation>
    </message>
    <message>
        <location line="+63"/>
        <source>Messages</source>
        <translation>訊息</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Filter messages containing: (Separator | )</source>
        <translation>過濾包含下列內容的訊息：（分隔符號 | ）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Accept from friends only</source>
        <translation>只接受好友訊息</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Advanced spam filter</source>
        <translation>進階廣告過濾器</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Require captcha authentication</source>
        <translation>需要驗證碼驗證</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Comments</source>
        <translation>評論</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Ignore comments containing: (Separator | )</source>
        <translation>忽略包含以下內容的評論：(分隔符號 | )</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Indicate downloads with comments/rating by icon</source>
        <translation>以圖示標示有評論/評分的下載</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Filter servers too</source>
        <translation>也過濾伺服器</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Filter level:   &lt;</source>
        <translation>過濾等級：   &lt;</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+431"/>
        <source>Reload</source>
        <translation>重新載入</translation>
    </message>
    <message>
        <location line="-412"/>
        <source>Update from URL: (filter.dat- or PeerGuardian-format, .gz/.zip accepted)</source>
        <translation>從網址更新：（filter.dat 或 PeerGuardian 格式，支援 .gz/.zip）</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+18"/>
        <source>Load</source>
        <translation>載入</translation>
    </message>
    <message>
        <location line="-13"/>
        <source>Loading...</source>
        <translation>載入中...</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Failed to download IP filter: %1</source>
        <translation>下載 IP 過濾器失敗：%1</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Downloaded IP filter is empty.</source>
        <translation>下載的 IP 過濾器是空的。</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Failed to save IP filter: %1</source>
        <translation>儲存 IP 過濾器失敗：%1</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>IP filter updated and reloaded.</source>
        <translation>IP 過濾器已更新並重新載入。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>IP filter updated and reloaded (unpacked &quot;%1&quot;).</source>
        <translation>IP 過濾器已更新並重新載入（已解壓縮「%1」）。</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>See My Shared Files/Directories</source>
        <translation>看見我分享的檔案/資料夾</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Everybody</source>
        <translation>每一個人</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Friends only</source>
        <translation>只限好友</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nobody</source>
        <translation>沒有人</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Protocol Obfuscation</source>
        <translation>模糊協定</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Enable protocol obfuscation</source>
        <translation>啟用模糊協定</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Allow obfuscated connections only (not recommended)</source>
        <translation>只允許模糊化連線（不建議使用）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Disable support for obfuscated connections</source>
        <translation>停用模糊化連線支援</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Use secure identification</source>
        <translation>使用安全識別</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Run eMule as unprivileged user</source>
        <translation>以沒有特權的使用者執行 eMule</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Enable spam filter for search results</source>
        <translation>啟用搜尋結果廣告過濾</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Anti-Leecher</source>
        <translation>反吸血</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Ban leechers with DLP rules:</source>
        <translation>以 DLP 規則封鎖吸血客戶端：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Lite</source>
        <translation>Lite</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+742"/>
        <source>Strict</source>
        <translation>嚴格</translation>
    </message>
    <message>
        <location line="-742"/>
        <source>Strict + Chinese</source>
        <translation>嚴格 + 中國</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bans hard leechers, soft leechers only lose queue score</source>
        <translation>封鎖嚴重吸血者，輕度吸血者只降低佇列評分</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Bans hard and soft leechers</source>
        <translation>封鎖嚴重和輕度吸血者</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Strict, and also bans VeryCD and easyMule mods</source>
        <translation>嚴格，並且封鎖 VeryCD 和 easyMule Mod</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Graphs</source>
        <translation>圖表</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Update delay: 3 sec</source>
        <translation>更新延遲：3 秒</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+154"/>
        <source>Update delay: %1 sec</source>
        <translation>更新延遲：%1 秒</translation>
    </message>
    <message>
        <location line="-153"/>
        <location line="+154"/>
        <source>Update delay: disabled</source>
        <translation>更新延遲：已停用</translation>
    </message>
    <message>
        <location line="-147"/>
        <source>Time for average graph: 5 mins</source>
        <translation>圖表平均時間：5 分鐘</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Time for average graph: %1 mins</source>
        <translation>圖表平均時間：%1 分鐘</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Colors</source>
        <translation>顏色</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Background</source>
        <translation>背景</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Grid</source>
        <translation>格線</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Download Session</source>
        <translation>工作階段下載</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Download Average</source>
        <translation>平均下載</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Download Current</source>
        <translation>目前下載</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Download Usenet</source>
        <translation>Usenet 下載</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload Session</source>
        <translation>工作階段上傳</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload Average</source>
        <translation>平均上傳</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload Current</source>
        <translation>目前上傳</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload Slots (no overhead)</source>
        <translation>上傳槽（無額外支出）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload Friend Slots</source>
        <translation>好友上傳槽</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Active Connections</source>
        <translation>作用中連線</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Active Uploads</source>
        <translation>作用中上傳</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Uploads</source>
        <translation>上傳總計</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Active Downloads</source>
        <translation>作用中下載</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Icon Bar</source>
        <translation>圖示列</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Default</source>
        <translation>預設</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Restore this colour to the eMule default</source>
        <translation>將此顏色還原為 eMule 預設值</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Auto</source>
        <translation>自動</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Select Color</source>
        <translation>選擇顏色</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Draw filled graphs</source>
        <translation>繪製填滿的圖表</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Connections statistics Y-axis scale:</source>
        <translation>連線統計 Y 軸比例：</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Active connections ratio:</source>
        <translation>作用中連線比率：</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Statistics Tree</source>
        <translation>統計樹狀</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Update delay: 5 sec</source>
        <translation>更新延遲：5 秒</translation>
    </message>
    <message>
        <location line="+40"/>
        <source>Enable REST API</source>
        <translation>啟用 REST API</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Serves /api/v1 with its description and a playground at /api/v1/docs. Calls need the API key.</source>
        <translation>提供 /api/v1 及其說明，並在 /api/v1/docs 提供測試頁面。呼叫需要 API 金鑰。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Enable MCP server for AI assistants</source>
        <translation>為 AI 助理啟用 MCP 伺服器</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Serves the Model Context Protocol at /mcp on the same port, with the same API key. Independent of the REST API.</source>
        <translation>在同一個連接埠的 /mcp 上以相同的 API 金鑰提供 Model Context Protocol。與 REST API 彼此獨立。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Read-only</source>
        <translation>唯讀</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Assistants can look at everything but change nothing.</source>
        <translation>助理可以檢視所有內容，但無法變更任何內容。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Gzip compression</source>
        <translation>Gzip 壓縮</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Include port into UPnP setup</source>
        <translation>將連接埠加入 UPnP 設定</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Listen address:</source>
        <translation>監聽位址：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>all interfaces</source>
        <translation>所有介面</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Address the web server listens on. Empty: every interface, reachable from other hosts. 127.0.0.1: this computer only.</source>
        <translation>Web 伺服器監聽的位址。留空：所有介面，其他主機可以連線。127.0.0.1：只限本機。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Template:</source>
        <translation>範本：</translation>
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
        <translation>工作階段逾時：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>minutes</source>
        <translation>分鐘</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Use HTTPS</source>
        <translation>使用 HTTPS</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Create new certificate</source>
        <translation>建立新憑證</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Certificate:</source>
        <translation>憑證：</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Key:</source>
        <translation>金鑰：</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>REST API Key:</source>
        <translation>REST API 金鑰：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Administrator</source>
        <translation>管理員</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Allow exit eMule, reboot and shutdown</source>
        <translation>允許離開 eMule、重新開機和關機</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Guest</source>
        <translation>訪客</translation>
    </message>
    <message>
        <location line="+55"/>
        <source>Web template reloaded</source>
        <translation>Web 範本已重新載入</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Web template reload failed</source>
        <translation>Web 範本重新載入失敗</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Select Template File</source>
        <translation>選擇範本檔案</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Template files (*.tmpl);;All files (*)</source>
        <translation>範本檔案 (*.tmpl);;所有檔案 (*)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Select Certificate File</source>
        <translation>選擇憑證檔案</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>PEM files (*.pem *.crt);;All files (*)</source>
        <translation>PEM 檔案 (*.pem *.crt);;所有檔案 (*)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Select Key File</source>
        <translation>選擇金鑰檔案</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>PEM files (*.pem *.key);;All files (*)</source>
        <translation>PEM 檔案 (*.pem *.key);;所有檔案 (*)</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Save Certificate</source>
        <translation>儲存憑證</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>PEM files (*.pem)</source>
        <translation>PEM 檔案 (*.pem)</translation>
    </message>
    <message>
        <location line="+131"/>
        <source>Enable Usenet downloads</source>
        <translation>啟用 Usenet 下載</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Gates automatic activity only. Adding a download by hand always works.</source>
        <translation>只限制自動活動。手動加入下載一律有效。</translation>
    </message>
    <message>
        <location line="+26"/>
        <location line="+38"/>
        <source>Account</source>
        <translation>帳號</translation>
    </message>
    <message>
        <location line="-36"/>
        <source>Advanced</source>
        <translation>進階</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+628"/>
        <location line="+274"/>
        <location line="+15"/>
        <source>News servers</source>
        <translation>新聞伺服器</translation>
    </message>
    <message>
        <location line="-911"/>
        <source>Host</source>
        <translation>主機</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Port</source>
        <translation>連接埠</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Priority</source>
        <translation>優先權</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Connections</source>
        <translation>連線數</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Used</source>
        <translation>已使用</translation>
    </message>
    <message>
        <location line="+19"/>
        <location line="+1019"/>
        <location line="+154"/>
        <location line="+1615"/>
        <location line="+2010"/>
        <source>Add</source>
        <translation>加入</translation>
    </message>
    <message>
        <location line="-4797"/>
        <location line="+1019"/>
        <location line="+154"/>
        <location line="+1342"/>
        <location line="+316"/>
        <location line="+1972"/>
        <source>Remove</source>
        <translation>移除</translation>
    </message>
    <message>
        <location line="-4782"/>
        <source>Display name (optional)</source>
        <translation>顯示名稱（選填）</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Host:</source>
        <translation>主機：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Encryption:</source>
        <translation>加密：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>None (119)</source>
        <translation>無 (119)</translation>
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
        <translation>使用者：</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Never set this above what your provider allows — exceeding the limit gets the account throttled, not queued.</source>
        <translation>切勿設定高於供應商允許的數量 — 超過限制會讓帳號被限速，而不是排入佇列。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Connections:</source>
        <translation>連線數：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Lower is tried first. A higher level is only used for articles that every server below reported as missing — that is what makes a block or fill account worth having.</source>
        <translation>等級越低越先嘗試。只有當所有較低等級的伺服器都回報文章遺失時，才會使用較高的等級 — 這正是區塊帳號或補充帳號的價值所在。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Priority level:</source>
        <translation>優先等級：</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Account options</source>
        <translation>帳號選項</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Unknown</source>
        <translation>未知</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+1004"/>
        <location line="+165"/>
        <source> days</source>
        <translation> 天</translation>
    </message>
    <message>
        <location line="-1168"/>
        <source>Retention:</source>
        <translation>保留期：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Accounts sharing a group number count as one for connection limits — use it when the same provider is reached through two host names, so the two entries cannot open twice what the plan allows.</source>
        <translation>共用同一個群組編號的帳號在連線限制上視為一個 — 當同一個供應商透過兩個主機名稱連線時使用它，這樣兩個項目就不會開啟方案允許數量兩倍的連線。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Connection group:</source>
        <translation>連線群組：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>None — accept any certificate</source>
        <translation>不檢查 — 接受任何憑證</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Minimal — allow a host name mismatch</source>
        <translation>最低 — 允許主機名稱不符</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Certificate check:</source>
        <translation>憑證檢查：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Optional — never fail a download on its own</source>
        <translation>選用 — 絕不會單獨導致下載失敗</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Send GROUP before fetching (only needed by a few old servers)</source>
        <translation>取得前先傳送 GROUP（只有少數舊伺服器需要）</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Unmetered</source>
        <translation>不限流量</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Monthly allowance</source>
        <translation>每月流量額度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Block account (prepaid)</source>
        <translation>區塊帳號（預付）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Allowance:</source>
        <translation>額度：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source> GB</source>
        <translation> GB</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>No limit</source>
        <translation>無限制</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Decimal GB, because that is what an invoice says — the 1024-based GB used elsewhere in eMule would put a 1000 GB plan 7% over.

Set it slightly under your plan. The figure is measured here, so it reads a few percent below your provider&apos;s, and articles already in flight when the limit is reached still finish.</source>
        <translation>使用十進位 GB，因為帳單上就是這樣寫的 — 若用 eMule 其他地方使用的 1024 進位 GB，1000 GB 的方案會超出 7%。

請設定得略低於您的方案。此數值是在本機測量，因此會比供應商的數值低幾個百分點；達到上限時已在傳輸中的文章仍會完成。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Allowance size:</source>
        <translation>額度大小：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Your billing day — providers reset on the day you signed up, not on the 1st. A month shorter than this rolls over on its last day.</source>
        <translation>您的帳單日 — 供應商會在您註冊的那一天重設額度，而不是每月 1 日。若某個月的天數少於此日期，則在該月最後一天重設。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Resets on day:</source>
        <translation>重設日：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>When the allowance is spent, use the next priority level</source>
        <translation>額度用盡時，使用下一個優先等級</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Off by default: block credit usually costs more per GB than the plan it would be covering, and spending it without being asked is the one thing a limit exists to prevent. Left off, downloads wait for the allowance instead — they are never failed and no article is ever given up on.</source>
        <translation>預設關閉：區塊額度每 GB 的費用通常高於它所補充的方案，而未經詢問就花掉它，正是設定限制所要防止的事。保持關閉時，下載會改為等候額度 — 它們絕不會失敗，也絕不會放棄任何一篇文章。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Used:</source>
        <translation>已使用：</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Correct…</source>
        <translation>修正…</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Downloading</source>
        <translation>下載中</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Retry a failed server after:</source>
        <translation>失敗伺服器的重試間隔：</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+913"/>
        <location line="+1315"/>
        <source> s</source>
        <translation> 秒</translation>
    </message>
    <message>
        <location line="-2227"/>
        <source>Never back off</source>
        <translation>永不退避</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Applies to every account: how long a server that refused or dropped a connection is passed over before it is tried again.</source>
        <translation>適用於所有帳號：拒絕或中斷連線的伺服器會被略過多久，然後才再次嘗試。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Share of the download limit:</source>
        <translation>占下載限制的比例：</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+42"/>
        <source> %</source>
        <translation> %</translation>
    </message>
    <message>
        <location line="-40"/>
        <source>How much of the global download limit Usenet may take while eD2K is also downloading. Whichever engine is idle lends its whole share to the other, so this only applies when both are busy.</source>
        <translation>當 eD2K 也在下載時，Usenet 可占用全域下載限制的比例。閒置的一方會把自己的全部份額讓給另一方，因此只有在兩者都忙碌時才會套用。</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>When adding</source>
        <translation>加入時</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Check availability:</source>
        <translation>檢查可用性：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Do not check</source>
        <translation>不檢查</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Sample one article per file</source>
        <translation>每個檔案抽查一篇文章</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Check every article</source>
        <translation>檢查每一篇文章</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Before downloading anything, ask your providers whether they still hold the release. It costs one small request per article asked about and no payload at all.

Sampling asks about the first article of each file, which is usually enough: providers expire whole posts by date, so a file is almost always present or absent as a unit. Checking every article is certain but can mean tens of thousands of requests for a large release.

The answer is never a verdict. Nothing here can stop an article being fetched — an article your providers deny may still arrive, and a release this pauses downloads normally when you resume it.</source>
        <translation>在下載任何內容之前，先詢問供應商是否仍保存該發布內容。每詢問一篇文章只需一個小型要求，完全不傳輸內容資料。

抽查只詢問每個檔案的第一篇文章，通常已經足夠：供應商會依日期將整個貼文一起過期，因此一個檔案幾乎總是整體存在或整體缺失。檢查每一篇文章較為確實，但對大型發布內容可能代表數萬次要求。

這個答案絕不是最終判定。這裡的任何設定都無法阻止文章被取得 — 供應商說沒有的文章仍可能到達，而被暫停的發布內容在您繼續後會照常下載。</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Pause below:</source>
        <translation>低於此值時暫停：</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+1300"/>
        <source>never</source>
        <translation>永不</translation>
    </message>
    <message>
        <location line="-1297"/>
        <source>A release that looks emptier than this is added paused, with the reason shown, so you decide rather than the guess. It is never failed and never refused.

A shortfall the release&apos;s own PAR2 recovery volumes can cover does not pause it, however low the figure goes.</source>
        <translation>看起來比此值更不完整的發布內容會以暫停狀態加入，並顯示原因，由您而不是由猜測來決定。它絕不會被判定為失敗，也絕不會被拒絕。

如果缺少的部分可由發布內容本身的 PAR2 修復卷補足，無論數值多低都不會暫停。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Start automatic downloads paused</source>
        <translation>自動加入的下載以暫停狀態開始</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Applies to anything queued without you asking for it directly: the watch folder below, and feeds.

With this on, an automatic download waits for you to press Resume, so a feed proposes rather than decides. Anything you add yourself starts normally either way.</source>
        <translation>適用於所有並非由您直接要求而加入佇列的內容：下方的監視資料夾以及訂閱源。

開啟後，自動下載會等待您按下「繼續」，讓訂閱源只是提議而不是替您決定。無論此項是否開啟，您自己加入的內容都會正常開始。</translation>
    </message>
    <message>
        <location line="+10"/>
        <location line="+49"/>
        <source>Watch folder</source>
        <translation>監視資料夾</translation>
    </message>
    <message>
        <location line="-45"/>
        <source>Any .nzb file left in this folder is queued and then moved into a _processed subfolder — or _failed, if it could not be read.</source>
        <translation>放進這個資料夾的任何 .nzb 檔案都會加入佇列，然後移到 _processed 子資料夾 — 如果無法讀取，則移到 _failed。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>No folder is being watched</source>
        <translation>未監視任何資料夾</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>A file is only read once it has stopped changing, so a large .nzb still being copied in is left alone until it is complete.

It cannot be inside your temp, incoming or configuration folders: the daemon writes there itself.</source>
        <translation>檔案只有在停止變動後才會被讀取，因此仍在複製中的大型 .nzb 會被暫時略過，直到複製完成。

它不能位於您的暫存、新進或設定資料夾內：常駐程式本身會寫入這些位置。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Browse…</source>
        <translation>瀏覽…</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Desktop</source>
        <translation>桌面</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Open .nzb files with eMule Qt</source>
        <translation>使用 eMule Qt 開啟 .nzb 檔案</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Claim .nzb files for this copy of eMule Qt, so double-clicking one queues it. The setting is for you alone and needs no administrator; it is re-applied at every start, so another program taking the association does not keep it.</source>
        <translation>將 .nzb 檔案關聯到這份 eMule Qt，讓點兩下檔案即可加入佇列。此設定只對您自己生效，不需要管理員權限；每次啟動都會重新套用，因此其他程式搶走關聯也無法一直保留。</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>After downloading</source>
        <translation>下載完成後</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Verify and repair with PAR2</source>
        <translation>使用 PAR2 驗證並修復</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Check the finished files against the release&apos;s PAR2 set and repair any damage from its recovery volumes. The recovery volumes are only downloaded when something actually needs repairing.

With this off, a release with missing articles fails instead of being shared, because there is no way to tell whether it is intact.</source>
        <translation>以發布內容的 PAR2 集驗證已完成的檔案，並用其修復卷修復任何損壞。只有確實需要修復時才會下載修復卷。

關閉此項時，缺少文章的發布內容會失敗而不會被分享，因為無法判斷它是否完整。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Restore filenames from PAR2</source>
        <translation>從 PAR2 還原檔案名稱</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Obfuscated releases are posted under meaningless filenames. The PAR2 metadata carries the real ones, and without them the archives cannot be identified for unpacking either.</source>
        <translation>模糊化的發布內容會以毫無意義的檔案名稱張貼。PAR2 中繼資料帶有真正的檔案名稱，沒有它們也無法辨識要解壓縮的壓縮檔。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Verify with SFV when there is no PAR2</source>
        <translation>沒有 PAR2 時使用 SFV 驗證</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>A release posted without a PAR2 set often comes with an .sfv file instead. Its checksums cannot repair anything, but a release they call damaged is not published.</source>
        <translation>沒有 PAR2 集的發布內容通常會改附一個 .sfv 檔案。它的檢查碼無法修復任何東西，但被它判定為損壞的內容不會被發布。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Unpack archives</source>
        <translation>解壓縮壓縮檔</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Extract RAR, 7z and ZIP volume sets once they have been verified.

Password-protected archives need 7-Zip or unrar installed — eMule&apos;s own archive reader can only decrypt ZIP.</source>
        <translation>在驗證通過後解壓縮 RAR、7z 和 ZIP 分卷集。

受密碼保護的壓縮檔需要安裝 7-Zip 或 unrar — eMule 本身的壓縮檔讀取器只能解密 ZIP。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Unpack while downloading</source>
        <translation>邊下載邊解壓縮</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Extract each archive volume as soon as it finishes instead of waiting for the whole release, so the content is ready the moment the download is.

It is the same extraction, moved earlier, so it costs no extra disk space. If the release turns out to need repairing, the result is discarded and it is unpacked again afterwards.</source>
        <translation>每個壓縮分卷一完成就解壓縮，而不是等待整個發布內容，這樣下載完成的同時內容也就緒了。

這只是把同一次解壓縮提前，不會占用額外的磁碟空間。如果發布內容最後需要修復，解壓縮結果會被捨棄，之後再重新解壓縮。</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Preview password-protected releases while downloading</source>
        <translation>下載時預覽受密碼保護的發布內容</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>An encrypted archive cannot be read a piece at a time, so previewing one means decrypting it again from the first volume every time more of it arrives.

Nothing runs unless a preview is actually open, and only RAR releases can do it at all — an incomplete 7z set decodes to nothing.</source>
        <translation>加密的壓縮檔無法逐段讀取，因此每當有更多資料到達時，預覽都代表要從第一個分卷重新解密。

只有確實開啟了預覽時才會執行，而且只有 RAR 發布內容才能這樣做 — 不完整的 7z 集根本解不出任何內容。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Unpacker:</source>
        <translation>解壓縮程式：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>automatic (7zz, 7z, unrar)</source>
        <translation>自動 (7zz, 7z, unrar)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Path to a 7-Zip or unrar binary, for password-protected archives.

Leave this empty to search the usual locations. Set it when eMule runs as a background service, whose search path is often much shorter than the one a terminal has.</source>
        <translation>7-Zip 或 unrar 執行檔的路徑，用於受密碼保護的壓縮檔。

留空則搜尋常見位置。當 eMule 以背景服務執行時請設定此項，因為服務的搜尋路徑通常比終端機的短得多。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Delete archives and PAR2 files after unpacking</source>
        <translation>解壓縮後刪除壓縮檔和 PAR2 檔案</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Keep only the unpacked content. Turning this off roughly doubles the disk space a release uses and shares the archive volumes and recovery files with eD2K peers, who have no use for them.</source>
        <translation>只保留解壓縮後的內容。關閉此項會使發布內容占用的磁碟空間大約加倍，並將壓縮分卷和修復檔案分享給 eD2K 節點，而它們對這些檔案毫無用處。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Keep downloading</source>
        <translation>繼續下載</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+11"/>
        <source>Pause it</source>
        <translation>暫停</translation>
    </message>
    <message>
        <location line="-10"/>
        <location line="+11"/>
        <source>Fail it</source>
        <translation>判定為失敗</translation>
    </message>
    <message>
        <location line="-9"/>
        <source>A release that has lost more than its recovery files could ever repair stops here instead of using up your allowance until the final check. The estimate only ever errs towards downloading.

Resume downloads it anyway.</source>
        <translation>當發布內容缺少的部分已超出其修復檔案所能修復的範圍時，會在此停止，而不是持續消耗額度直到最終檢查。這個估算只會偏向繼續下載。

「繼續」仍會照常下載它。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>When a download cannot be repaired:</source>
        <translation>當下載無法修復時：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Publish anyway</source>
        <translation>仍然發布</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>A movie or episode whose download contains programs or shortcuts is almost always a fake. Only releases with video or audio in them are checked, so software downloads are not affected.

Resume publishes it anyway.</source>
        <translation>如果一部電影或影集的下載中含有程式或捷徑，幾乎一定是假檔。只有包含視訊或音訊的發布內容才會被檢查，因此軟體下載不受影響。

「繼續」仍會照常發布它。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>When a media release has unwanted files:</source>
        <translation>當媒體發布內容含有不需要的檔案時：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>File extensions, separated by commas. A video file that is not really a video counts as well. Leave this empty to turn the check off.</source>
        <translation>副檔名，以逗號分隔。名為影片但實際上不是影片的檔案也算在內。留空則關閉此檢查。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Unwanted file types:</source>
        <translation>不需要的檔案類型：</translation>
    </message>
    <message>
        <location line="+191"/>
        <source>The news server list could not be saved.</source>
        <translation>無法儲存新聞伺服器清單。</translation>
    </message>
    <message>
        <location line="+146"/>
        <source>(unchanged)</source>
        <translation>（未變更）</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>(none set)</source>
        <translation>（未設定）</translation>
    </message>
    <message>
        <location line="+60"/>
        <source>not measured — the Usenet engine is stopped</source>
        <translation>未測量 — Usenet 引擎已停止</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>%1 used</source>
        <translation>已使用 %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1 of %2</source>
        <translation>%1 / %2</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>, resets %1</source>
        <translation>，%1 重設</translation>
    </message>
    <message>
        <location line="+5"/>
        <source> — spent</source>
        <translation> — 已用盡</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Measured here, not reported by the provider — NNTP has no command that asks. Expect a few percent below your provider&apos;s own figure.</source>
        <translation>此數值在本機測量，並非由供應商回報 — NNTP 沒有可查詢的命令。預計會比供應商自己的數字低幾個百分點。</translation>
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
        <translation>本週期已使用量，單位為 GB。

請輸入供應商控制台顯示的數值，或輸入 0 重新開始計算。</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>The usage counter could not be changed.</source>
        <translation>無法變更用量計數器。</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>At most %1 news servers can be configured.</source>
        <translation>最多只能設定 %1 個新聞伺服器。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>New server</source>
        <translation>新伺服器</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Enter a host name first.</source>
        <translation>請先輸入主機名稱。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Connecting…</source>
        <translation>正在連線…</translation>
    </message>
    <message>
        <location line="+51"/>
        <source>Search indexers answer keyword searches and hand back an NZB. They are separate from your news servers: on Usenet the provider you download from and the service you search are different businesses.</source>
        <translation>搜尋索引器負責回應關鍵字搜尋並傳回 NZB。它們與您的新聞伺服器是分開的：在 Usenet 上，您下載所用的供應商和您搜尋所用的服務是不同的業者。</translation>
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
        <translation>類型</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>API key</source>
        <translation>API 金鑰</translation>
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
        <translation>顯示名稱</translation>
    </message>
    <message>
        <location line="-150"/>
        <source>Also the identity of this account: it names the cached capabilities and appears in the Indexer column of the results.</source>
        <translation>它同時也是這個帳號的識別：快取的功能資訊以它命名，並顯示在結果的「索引器」欄位中。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>The API base URL. A bare host gets &quot;/api&quot; added; a URL that already has a path is used exactly as typed, which is what Jackett and NZBHydra2 endpoints need.</source>
        <translation>API 基礎網址。只填主機名稱時會自動加上「/api」；已帶有路徑的網址會照原樣使用，Jackett 和 NZBHydra2 的端點正需要這樣。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>API URL:</source>
        <translation>API 網址：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>API key:</source>
        <translation>API 金鑰：</translation>
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
        <translation>兩者 — Prowlarr、NZBHydra2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type:</source>
        <translation>類型：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Searching</source>
        <translation>搜尋</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Rows to ask for per request. An indexer that allows fewer silently returns fewer, so this is an upper bound rather than a promise.</source>
        <translation>每次要求索取的結果列數。允許列數較少的索引器會直接傳回較少的結果，因此這是上限而不是保證。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Results per request:</source>
        <translation>每次要求的結果數：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>How many pages one search may fetch from each indexer.

Every page is an API call against the allowance your account has, so this is a spending limit, not a speed setting.</source>
        <translation>一次搜尋最多可從每個索引器取得多少頁。

每一頁都是一次 API 呼叫，會消耗您帳號的配額，因此這是花費上限，而不是速度設定。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Pages per search:</source>
        <translation>每次搜尋的頁數：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Request timeout:</source>
        <translation>要求逾時：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>How often to re-read what each indexer supports. A stale answer never blocks a search — it only means a query field stays greyed out that the indexer has since started accepting.</source>
        <translation>多久重新讀取一次各索引器支援的功能。過時的資訊絕不會阻擋搜尋 — 只是某個索引器後來已開始支援的查詢欄位仍會顯示為灰色。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Refresh capabilities every:</source>
        <translation>重新整理功能資訊間隔：</translation>
    </message>
    <message>
        <location line="+51"/>
        <source>A feed is a search that runs on its own and queues what it finds. Its first check adds nothing — it only records what the indexer already lists, because otherwise a new feed would download everything still on the server.</source>
        <translation>訂閱源是一個自動執行並將找到的結果加入佇列的搜尋。它的第一次檢查不會加入任何內容 — 只會記錄索引器目前列出的項目，否則新的訂閱源會把伺服器上仍保留的一切都下載下來。</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+324"/>
        <location line="+277"/>
        <source>Feeds</source>
        <translation>訂閱源</translation>
    </message>
    <message>
        <location line="-596"/>
        <source>Search</source>
        <translation>搜尋</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Every</source>
        <translation>每隔</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Last checked</source>
        <translation>上次檢查</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Feed</source>
        <translation>訂閱源</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Also the identity of this feed: it names the file that remembers what the feed has already seen.</source>
        <translation>它同時也是這個訂閱源的識別：記錄該訂閱源已看過哪些項目的檔案以它命名。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Search my indexers</source>
        <translation>搜尋我的索引器</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>An RSS address I paste</source>
        <translation>我貼上的 RSS 位址</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Source:</source>
        <translation>來源：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Keywords. Leave it empty to take everything new in the categories below.</source>
        <translation>關鍵字。留空則接收下方類別中的所有新項目。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Search for:</source>
        <translation>搜尋：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>e.g. 2000, 5000</source>
        <translation>例如 2000, 5000</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Newznab category numbers, separated by commas. Empty means every category.</source>
        <translation>Newznab 類別編號，以逗號分隔。留空表示所有類別。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Categories:</source>
        <translation>類別：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Which indexers to ask, by name and separated by commas. Empty means all of them.

Adding one later does not fetch its back catalogue: a new indexer gets its own first check, which adds nothing.</source>
        <translation>要詢問哪些索引器，依名稱填寫並以逗號分隔。留空表示全部。

之後新增的索引器不會抓取過去的項目：新的索引器會進行自己的第一次檢查，而第一次檢查不會加入任何內容。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Indexers:</source>
        <translation>索引器：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>The RSS address from your indexer&apos;s website. It contains your API key, so it is stored encrypted and is only ever shown back to you with the key hidden.</source>
        <translation>來自索引器網站的 RSS 位址。它包含您的 API 金鑰，因此會加密儲存，顯示給您時也一律隱藏金鑰。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Feed URL:</source>
        <translation>訂閱源網址：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Only queue releases whose name matches this pattern. Empty accepts everything.</source>
        <translation>只將名稱符合此模式的發布內容加入佇列。留空表示接受全部。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Must match:</source>
        <translation>必須符合：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Never queue a release whose name matches this pattern. It wins over the one above.</source>
        <translation>絕不將名稱符合此模式的發布內容加入佇列。它優先於上面的規則。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Must not match:</source>
        <translation>必須不符合：</translation>
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
        <translation>無下限</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Smallest:</source>
        <translation>最小：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>no maximum</source>
        <translation>無上限</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Largest:</source>
        <translation>最大：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>any age</source>
        <translation>不限時間</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Posted within:</source>
        <translation>發布時間在此範圍內：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source> minutes</source>
        <translation> 分鐘</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>How often to check. Fifteen minutes is the floor: most indexers ask for no more than that, and checking harder gets an account suspended.</source>
        <translation>檢查頻率。十五分鐘是下限：大多數索引器要求不得比這更頻繁，檢查過於頻繁會導致帳號被停權。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Check every:</source>
        <translation>檢查間隔：</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+136"/>
        <source>No category</source>
        <translation>無類別</translation>
    </message>
    <message>
        <location line="-134"/>
        <source>Which download category this feed&apos;s matches go into. The category decides the folder they finish in, and it is resolved when a release completes — so repointing the category moves what is still running with it.</source>
        <translation>此訂閱源符合的內容要歸入哪個下載類別。類別決定它們完成後所在的資料夾，並在發布內容完成時才決定 — 因此變更類別的指向，仍在進行中的下載也會跟著改變。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Download category:</source>
        <translation>下載類別：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Queue what it already lists</source>
        <translation>將已列出的項目加入佇列</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Normally a feed&apos;s first check only takes note of what is there and queues nothing, because everything an indexer still holds is new to a feed that has never run. Turn this on to take the back catalogue as well — it can be a great deal of it.</source>
        <translation>通常訂閱源的第一次檢查只會記錄目前有哪些項目而不加入佇列，因為對從未執行過的訂閱源來說，索引器保留的一切都是新內容。開啟此項也會一併接收過去的項目 — 數量可能非常龐大。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Check now</source>
        <translation>立即檢查</translation>
    </message>
    <message>
        <location line="+97"/>
        <source>The indexer list could not be saved.</source>
        <translation>無法儲存索引器清單。</translation>
    </message>
    <message>
        <location line="+94"/>
        <source>The feed list could not be saved.</source>
        <translation>無法儲存訂閱源清單。</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>%1 min</source>
        <translation>%1 分鐘</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>checking…</source>
        <translation>正在檢查…</translation>
    </message>
    <message>
        <location line="+113"/>
        <source>%1 queued on the last check; %2 releases remembered.</source>
        <translation>上次檢查加入佇列 %1 個；已記住 %2 個發布內容。</translation>
    </message>
    <message>
        <location line="+71"/>
        <source>New feed</source>
        <translation>新訂閱源</translation>
    </message>
    <message>
        <location line="+50"/>
        <source>&quot;%1&quot; could not be checked.</source>
        <translation>無法檢查「%1」。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Checking &quot;%1&quot;…</source>
        <translation>正在檢查「%1」…</translation>
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
        <translation>兩者</translation>
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
        <translation>（已儲存金鑰 — 留空則保留）</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>At most %1 indexers can be configured.</source>
        <translation>最多只能設定 %1 個索引器。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>New indexer</source>
        <translation>新索引器</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Enter an API URL first.</source>
        <translation>請先輸入 API 網址。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Contacting the indexer…</source>
        <translation>正在聯絡索引器…</translation>
    </message>
    <message>
        <location line="+70"/>
        <source>Warning: Do not change these settings unless you know what you are doing. Otherwise you can easily make things worse for yourself. eMule will run fine without adjusting any of these settings.</source>
        <translation>警告：除非您知道您在做什麼，否則請不要更改這些設定。否則很容易使您自己的情況更糟。即使沒有更改這些設定值，eMule 也能執行得很好。</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>TCP/IP connections</source>
        <translation>TCP/IP 連線</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Max. new connections / 5 secs.:</source>
        <translation>每 5 秒最大新連線數：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Max. half-open connections:</source>
        <translation>最大半開連線數：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Server connection refresh interval [min.]:</source>
        <translation>伺服器連線重新整理間隔 [分鐘]：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Disabled</source>
        <translation>已停用</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Max. firewalled Kad nodes served as buddy:</source>
        <translation>作為夥伴服務的防火牆內 Kad 節點最大數：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>While your ports are open, eMule relays callback requests for this many firewalled Kad nodes. Each one uses a connection, so at most a tenth of &quot;Max. connections&quot; is served.</source>
        <translation>連接埠開放時，eMule 會為這麼多個防火牆內的 Kad 節點轉送回撥要求。每個節點占用一個連線，因此最多只服務「最大連線數」的十分之一。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Use credit system (reward uploaders)</source>
        <translation>使用額度系統（獎賞上傳者）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Remember the upload queue between restarts</source>
        <translation>重新啟動後記住上傳佇列</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Stores the longest-waiting clients in your upload queue and puts them back, with the places they had earned, when eMule starts again. They are not contacted on startup — they simply wait their turn as usual.</source>
        <translation>儲存上傳佇列中等候最久的客戶端，並在 eMule 再次啟動時依它們已取得的排位放回佇列。啟動時不會主動聯絡它們 — 它們只是像平常一樣等候輪到自己。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Filter server and client LAN IPs</source>
        <translation>過濾伺服器及客戶端區域網路 IP</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Disable A4AF checks to save CPU</source>
        <translation>停用 A4AF 檢查來節省 CPU</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Disable automatic archive preview start in file details</source>
        <translation>在檔案詳細資料中停用自動開始壓縮檔預覽</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Host name for own eD2K links:</source>
        <translation>自己的 eD2K 連結主機名稱：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>A DNS name or an IPv6 literal</source>
        <translation>DNS 名稱或 IPv6 位址常值</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Add own IPv6 address to eD2K links</source>
        <translation>將自己的 IPv6 位址加入 eD2K 連結</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Only when a public IPv6 address is confirmed. Legacy clients ignore it.</source>
        <translation>只在確認擁有公用 IPv6 位址時生效。舊版客戶端會忽略它。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Create new part files as &apos;sparse&apos; (NTFS only)</source>
        <translation>建立新的部分檔案成「疏鬆檔案」（限 NTFS 使用）</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Allocate full file size for non-sparse part files</source>
        <translation>為非疏鬆部分檔案分配完整檔案大小</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Check disk space</source>
        <translation>檢查磁碟空間</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Min. free disk space [MB]:</source>
        <translation>最小的可用磁碟空間 [MB]：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Max. disk load while hashing:</source>
        <translation>雜湊計算時的最大磁碟負載：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Hashing pauses between reads so the disk stays responsive. 100 % means no limit.</source>
        <translation>雜湊計算會在讀取之間暫停，讓磁碟保持回應。100 % 表示不限制。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Safe .met/.dat file writing</source>
        <translation>安全的 .met/.dat 檔案寫入</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+15"/>
        <source>Never</source>
        <translation>從不</translation>
    </message>
    <message>
        <location line="-14"/>
        <source>On shutdown</source>
        <translation>關閉時</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Always</source>
        <translation>總是</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Extract meta data</source>
        <translation>擷取中繼資料</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>MediaInfo Library</source>
        <translation>MediaInfo 程式庫</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Resolve shell links in shared directories</source>
        <translation>解析已分享資料夾中的捷徑</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Write eMule core logs to disk</source>
        <translation>將 eMule 核心記錄寫入磁碟</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Write eMule GUI logs to disk</source>
        <translation>將 eMule 圖形介面記錄寫入磁碟</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Verbose (additional program feedback)</source>
        <translation>詳細模式（回覆程式附加資訊）</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Log client source exchange and server source queries/answers</source>
        <translation>記錄客戶端來源交換及伺服器來源查詢/回覆</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Log server connection &amp;&amp; search details (TCP/UDP handshake)</source>
        <translation>記錄伺服器連線 &amp;&amp; 搜尋詳細資料（TCP/UDP 交握）</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Log banned clients</source>
        <translation>記錄已被封鎖的客戶端</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Log received file descriptions and ratings</source>
        <translation>記錄已接收的檔案描述及評分</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Log secure ident</source>
        <translation>記錄安全識別</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Log filtered and/or ignored IPs</source>
        <translation>記錄已過濾和/或已忽略的 IP</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Log file save actions</source>
        <translation>記錄檔案儲存動作</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Log A4AF actions</source>
        <translation>記錄 A4AF 動作</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Log upload/download events</source>
        <translation>記錄上傳/下載事件</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Log raw socket packets</source>
        <translation>記錄原始通訊端封包</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Log web server requests</source>
        <translation>記錄 Web 伺服器要求</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Log public IP address on startup</source>
        <translation>啟動時記錄公用 IP 位址</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show Usenet log tab</source>
        <translation>顯示 Usenet 記錄分頁</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Enable IPC log tab</source>
        <translation>啟用 IPC 記錄分頁</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Start core with console (debug)</source>
        <translation>以主控台啟動核心（除錯）</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Upload SpeedSense (not recommended)</source>
        <translation>上傳速度感應器（不建議使用）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Find best upload limit automatically</source>
        <translation>自動尋找最佳上傳限制</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Lowest allowed upload speed:</source>
        <translation>上傳最低允許速度：</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Ping tolerance (% of lowest ping):</source>
        <translation>Ping 容許值（最低 ping 的 %）：</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Ping tolerance (ms):</source>
        <translation>Ping 容許值（毫秒）：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source> ms</source>
        <translation> 毫秒</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Method for ping tolerance:</source>
        <translation>Ping 容許值方式：</translation>
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
        <translation>上升緩慢度：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Going down slowness:</source>
        <translation>下降緩慢度：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Max number of pings for average:</source>
        <translation>計算平均的最大 ping 次數：</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>UPnP</source>
        <translation>UPnP</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Remove UPnP port forwarding on exit</source>
        <translation>離開時移除 UPnP 連接埠轉送</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>PCP (RFC 6887) — preferred, supports IPv6</source>
        <translation>PCP (RFC 6887) — 建議使用，支援 IPv6</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>NAT-PMP (RFC 6886) — IPv4 only</source>
        <translation>NAT-PMP (RFC 6886) — 只限 IPv4</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>UPnP IGD — fallback</source>
        <translation>UPnP IGD — 備援</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Open IPv6 firewall pinholes</source>
        <translation>開啟 IPv6 防火牆穿孔</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Requested lease:</source>
        <translation>要求的租用期限：</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Sharing eMule with other computer users</source>
        <translation>和本電腦的其他使用者共用 eMule</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Each user has its own configuration and downloads</source>
        <translation>每個使用者有自己的設定及下載</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Everyone has the same configuration and downloads</source>
        <translation>每個人使用相同的設定及下載</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Store config and downloads in the program directory</source>
        <translation>儲存設定及下載在程式資料夾</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>File buffer size: %1 MB</source>
        <translation>檔案緩衝區大小：%1 MB</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Queue size: %1</source>
        <translation>佇列大小：%1</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>New</source>
        <translation>新增</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+19"/>
        <source>Title</source>
        <translation>標題</translation>
    </message>
    <message>
        <location line="-19"/>
        <source>Days</source>
        <translation>天</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Start Time</source>
        <translation>開始時間</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Details</source>
        <translation>詳細資料</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Time</source>
        <translation>時間</translation>
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
        <translation>星期一</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Tuesday</source>
        <translation>星期二</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Wednesday</source>
        <translation>星期三</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Thursday</source>
        <translation>星期四</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Friday</source>
        <translation>星期五</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Saturday</source>
        <translation>星期六</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Sunday</source>
        <translation>星期日</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Mon-Fri</source>
        <translation>星期一至五</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Mon-Sat</source>
        <translation>星期一至六</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Sat-Sun</source>
        <translation>星期六至日</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>No end time</source>
        <translation>無結束時間</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+5"/>
        <source>Action</source>
        <translation>動作</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Value</source>
        <translation>數值</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>New Schedule</source>
        <translation>新增時間表</translation>
    </message>
    <message>
        <location line="+161"/>
        <source>Enter the datarate limit: (%1, KB/s)</source>
        <translation>輸入限制的傳輸速率：(%1, KB/s)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Please enter the new value: (%1)</source>
        <translation>請輸入新的數值：(%1)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Configure Action</source>
        <translation>設定動作</translation>
    </message>
    <message>
        <location line="+40"/>
        <source>Select category</source>
        <translation>選擇類別</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>All uncategorized</source>
        <translation>所有未分類</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>All</source>
        <translation>全部</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Edit Value</source>
        <translation>編輯數值</translation>
    </message>
    <message>
        <location line="+99"/>
        <source>The %1 settings page is not yet implemented.</source>
        <translation>%1 設定頁面尚未實作。</translation>
    </message>
    <message>
        <location line="+181"/>
        <source>Proxy</source>
        <translation>代理</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Proxy settings will only apply to new connections.
Restart eMule for all connections to use the new proxy settings.

News server connections switch over immediately.</source>
        <translation>代理設定只會套用到新的連線。
重新啟動 eMule 讓所有連線都使用新的代理設定。

新聞伺服器連線會立即切換。</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>The language change will take effect after restarting the application.</source>
        <translation>語言變更將在重新啟動應用程式後生效。</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Core connection settings will take effect after restarting the application.</source>
        <translation>核心連線設定將在重新啟動應用程式後生效。</translation>
    </message>
    <message>
        <location line="+34"/>
        <source>Icons</source>
        <translation>圖示</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The icon change will take effect after restarting the application.</source>
        <translation>圖示變更將在重新啟動應用程式後生效。</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Theme</source>
        <translation>主題</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The theme change will take effect after restarting the application.</source>
        <translation>主題變更將在重新啟動應用程式後生效。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Font size</source>
        <translation>字型大小</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The font size change will take effect after restarting the application.</source>
        <translation>字型大小變更將在重新啟動應用程式後生效。</translation>
    </message>
    <message>
        <location line="+328"/>
        <source>Web Interface</source>
        <translation>Web 介面</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&quot;%1&quot; is not an IP address. The listen address was not changed.</source>
        <translation>「%1」不是 IP 位址。監聽位址未變更。</translation>
    </message>
    <message>
        <location line="+481"/>
        <source>File types</source>
        <translation>檔案類型</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Could not update the .nzb file association: %1</source>
        <translation>無法更新 .nzb 檔案關聯：%1</translation>
    </message>
    <message>
        <location line="+194"/>
        <source>specialdlp loaded, DLP v%1</source>
        <translation>specialdlp 已載入，DLP v%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>specialdlp library not found, checks are inactive</source>
        <translation>找不到 specialdlp 程式庫，檢查未啟用</translation>
    </message>
    <message>
        <location line="+405"/>
        <source>Port forwarding: %1</source>
        <translation>連接埠轉送：%1</translation>
    </message>
    <message>
        <location line="+51"/>
        <source>Database: not connected to the core</source>
        <translation>資料庫：未連線到核心</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Downloading the GeoLite2 country database...</source>
        <translation>正在下載 GeoLite2 國家資料庫...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Update failed: the core did not answer</source>
        <translation>更新失敗：核心沒有回應</translation>
    </message>
    <message>
        <location line="+53"/>
        <source>Database: GeoLite2-Country from %1</source>
        <translation>資料庫：GeoLite2-Country（%1）</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Database: none — enter your MaxMind account ID and license key</source>
        <translation>資料庫：無 — 請輸入您的 MaxMind 帳號 ID 和授權金鑰</translation>
    </message>
    <message>
        <location line="+3"/>
        <source> (update running)</source>
        <translation> （正在更新）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Last update failed: %1</source>
        <translation>上次更新失敗：%1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Filename Cleanup</source>
        <translation>檔案名稱清理</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Text to remove from the names of new downloads. Case is ignored.</source>
        <translation>要從新下載的名稱中移除的文字。不區分大小寫。</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Reset to Default</source>
        <translation>重設為預設值</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Automatic</source>
        <translation>自動</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>about %1 slots</source>
        <translation>約 %1 個上傳槽</translation>
    </message>
</context>
<context>
    <name>eMule::PasteLinksDialog</name>
    <message>
        <location filename="../src/gui/dialogs/PasteLinksDialog.cpp" line="+16"/>
        <source>Paste eD2K Links</source>
        <translation>貼上 eD2K 連結</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>eD2K Links:</source>
        <translation>eD2K 連結：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paste one or more ed2k:// links here, one per line...</source>
        <translation>在此貼上一個或多個 ed2k:// 連結，每行一個...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Download</source>
        <translation>下載</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Not Connected</source>
        <translation>未連線</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not connected to the daemon.</source>
        <translation>未連線到常駐程式。</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Invalid Links</source>
        <translation>無效的連結</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The following links could not be parsed:

%1</source>
        <translation>無法解析下列連結：

%1</translation>
    </message>
</context>
<context>
    <name>eMule::PasteTextDialog</name>
    <message>
        <location filename="../src/gui/dialogs/PasteTextDialog.cpp" line="+42"/>
        <source>optional</source>
        <translation>選填</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Category:</source>
        <translation>類別：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Priority:</source>
        <translation>優先權：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Start paused</source>
        <translation>以暫停狀態開始</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="+78"/>
        <source>Working…</source>
        <translation>處理中…</translation>
    </message>
</context>
<context>
    <name>eMule::PeerPreviewDialog</name>
    <message>
        <location filename="../src/gui/dialogs/PeerPreviewDialog.cpp" line="+17"/>
        <source>Preview: %1</source>
        <translation>預覽：%1</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Prior</source>
        <translation>上一張</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Next</source>
        <translation>下一個</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Close</source>
        <translation>關閉</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>No images</source>
        <translation>沒有圖片</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Image %1 of %2</source>
        <translation>第 %1 張，共 %2 張</translation>
    </message>
</context>
<context>
    <name>eMule::SearchDetailDialog</name>
    <message>
        <location filename="../src/gui/dialogs/SearchDetailDialog.cpp" line="+42"/>
        <source>Details: %1</source>
        <translation>詳細資料：%1</translation>
    </message>
    <message>
        <location line="+19"/>
        <location line="+2"/>
        <source>Metadata</source>
        <translation>中繼資料</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+2"/>
        <source>Comments</source>
        <translation>評論</translation>
    </message>
</context>
<context>
    <name>eMule::SearchPanel</name>
    <message>
        <location filename="../src/gui/panels/SearchPanel.cpp" line="+207"/>
        <source>Related</source>
        <translation>相關</translation>
    </message>
    <message>
        <location line="+103"/>
        <location line="+973"/>
        <location line="+13"/>
        <location line="+514"/>
        <source>Download</source>
        <translation>下載</translation>
    </message>
    <message>
        <location line="-1480"/>
        <source>Close All Searches</source>
        <translation>關閉所有搜尋</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Name:</source>
        <translation>名稱：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Enter search keywords...</source>
        <translation>輸入搜尋關鍵字...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Type:</source>
        <translation>類型：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Any</source>
        <translation>任何</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Audio</source>
        <translation>音樂</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Video</source>
        <translation>影像</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Image</source>
        <translation>圖片</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Document</source>
        <translation>文件</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Program</source>
        <translation>程式</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Archive</source>
        <translation>壓縮檔</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>CD-Image</source>
        <translation>光碟映像</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Collection</source>
        <translation>收藏集</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Method:</source>
        <translation>方式：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Automatic</source>
        <translation>自動</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Kad Network</source>
        <translation>Kad 網路</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ed2k Server</source>
        <translation>Ed2k 伺服器</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ed2k Global</source>
        <translation>Ed2k 全域</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Usenet (Indexer)</source>
        <translation>Usenet（索引器）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Usenet (Server)</source>
        <translation>Usenet（伺服器）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Torrent (Server)</source>
        <translation>Torrent（伺服器）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Web (Server)</source>
        <translation>網頁（伺服器）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Reset</source>
        <translation>重置</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Show results the server found on this network</source>
        <translation>顯示伺服器在此網路上找到的結果</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Usenet results</source>
        <translation>Usenet 結果</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Kad results</source>
        <translation>Kad 結果</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Torrent results</source>
        <translation>種子結果</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Web results</source>
        <translation>網頁結果</translation>
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
        <translation>完整來源：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Extension:</source>
        <translation>副檔名：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Codec:</source>
        <translation>編解碼器：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Min. Bitrate [Kbit/s]:</source>
        <translation>最小位元率 [Kbit/s]：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Min. Length [s]:</source>
        <translation>最短長度 [秒]：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Title:</source>
        <translation>標題：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Album:</source>
        <translation>專輯：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Artist:</source>
        <translation>演出者：</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Start</source>
        <translation>開始</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>More</source>
        <translation>更多</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Ask the server for further results of this search</source>
        <translation>向伺服器要求此搜尋的更多結果</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="+34"/>
        <location line="+24"/>
        <location line="+204"/>
        <source>Not connected to daemon — search cannot be started.</source>
        <translation>未連線到常駐程式 — 無法開始搜尋。</translation>
    </message>
    <message>
        <location line="-171"/>
        <source>Search</source>
        <translation>搜尋</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Kad: &quot;%1&quot; is already being searched — using &quot;%2&quot; as the search target.</source>
        <translation>Kad：已在搜尋「%1」 — 改用「%2」作為搜尋目標。</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Queued — waiting for a server connection</source>
        <translation>已排入佇列 — 正在等待伺服器連線</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Queued — waiting for Kad to connect</source>
        <translation>已排入佇列 — 正在等待 Kad 連線</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Queued — waiting for a server or Kad connection</source>
        <translation>已排入佇列 — 正在等待伺服器或 Kad 連線</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Queued — waiting for the previous search to finish</source>
        <translation>已排入佇列 — 正在等待上一個搜尋完成</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Queued — waiting for the server to say what it offers</source>
        <translation>已排入佇列 — 正在等待伺服器告知其提供的內容</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Queued</source>
        <translation>佇列中</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Search failed: %1</source>
        <translation>搜尋失敗：%1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>%1 results — scroll down for more</source>
        <translation>%1 個結果 — 向下捲動以檢視更多</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+231"/>
        <source>No results</source>
        <translation>沒有結果</translation>
    </message>
    <message>
        <location line="-224"/>
        <source>%1 of %2 results</source>
        <translation>%1 / %2 個結果</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Search &quot;%1&quot; failed: %2</source>
        <translation>搜尋「%1」失敗：%2</translation>
    </message>
    <message>
        <location line="+85"/>
        <location line="+182"/>
        <source>Usenet search</source>
        <translation>Usenet 搜尋</translation>
    </message>
    <message>
        <location line="-90"/>
        <source>%1 results — %2 of %3 indexers</source>
        <translation>%1 個結果 — %3 個索引站中的 %2 個</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Usenet search: %1</source>
        <translation>Usenet 搜尋：%1</translation>
    </message>
    <message>
        <location line="+73"/>
        <source>Could not queue &quot;%1&quot;: %2</source>
        <translation>無法將「%1」加入佇列：%2</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Queued &quot;%1&quot; for download from Usenet.</source>
        <translation>已將「%1」排入佇列，從 Usenet 下載。</translation>
    </message>
    <message>
        <location line="+97"/>
        <source>Restore Search Parameters</source>
        <translation>還原搜尋參數</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Close</source>
        <translation>關閉</translation>
    </message>
    <message>
        <location line="+92"/>
        <source>&amp;Download</source>
        <translation>下載(&amp;D)</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Download &amp;To</source>
        <translation>下載到(&amp;T)</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Copy &amp;Name</source>
        <translation>複製名稱(&amp;N)</translation>
    </message>
    <message>
        <location line="+83"/>
        <source>%1 (%2)</source>
        <translation>%1 (%2)</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Paused</source>
        <translation>已暫停</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Download NZB File...</source>
        <translation>下載 NZB 檔案...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Download Torrent</source>
        <translation>下載種子</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Copy Magnet Link</source>
        <translation>複製磁力連結</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Show Links...</source>
        <translation>顯示連結...</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Download Container...</source>
        <translation>下載容器...</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Details...</source>
        <translation>詳細資料...</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Comments...</source>
        <translation>評論...</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Copy eD2K Links</source>
        <translation>複製 eD2K 連結</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Copy eD2K Links (HTML)</source>
        <translation>複製 eD2K 連結 (HTML)</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Copy Magnet Links</source>
        <translation>複製磁力連結</translation>
    </message>
    <message>
        <location line="+17"/>
        <location line="+15"/>
        <location line="+10"/>
        <source>Mark as Spam</source>
        <translation>標記為垃圾</translation>
    </message>
    <message>
        <location line="-25"/>
        <source>Mark as not Spam</source>
        <translation>標記為非垃圾</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>Remove</source>
        <translation>移除</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Close Search Results</source>
        <translation>關閉搜尋結果</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Close All Search Results</source>
        <translation>關閉所有搜尋結果</translation>
    </message>
    <message>
        <location line="+13"/>
        <location line="+7"/>
        <location line="+971"/>
        <location line="+43"/>
        <source>Preview</source>
        <translation>預覽</translation>
    </message>
    <message>
        <location line="-1008"/>
        <source>Find...</source>
        <translation>尋找...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Search Related Files</source>
        <translation>搜尋相關檔案</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Web Services</source>
        <translation>Web 服務</translation>
    </message>
    <message>
        <location line="+274"/>
        <source>You have already downloaded the following file(s). Download them again?

%1</source>
        <translation>您已經下載過以下檔案。要重新下載嗎？

%1</translation>
    </message>
    <message>
        <location line="+659"/>
        <source>Preview requested - please wait</source>
        <translation>已要求預覽 - 請稍候</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>%1 sent no preview</source>
        <translation>%1 未傳送預覽</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Preview not available — web server is not running or stream token not received.</source>
        <translation>無法預覽 — Web 伺服器未執行或未收到串流權杖。</translation>
    </message>
    <message>
        <location line="+311"/>
        <source>Asking servers: %1 / %2</source>
        <translation>正在詢問伺服器：%1 / %2</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>All</source>
        <translation>全部</translation>
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
        <translation>串流</translation>
    </message>
    <message numerus="yes">
        <location line="+97"/>
        <source>%1 · %n name(s)</source>
        <translation>
            <numerusform>%1 · %n 個名稱</numerusform>
        </translation>
    </message>
    <message>
        <location line="+9"/>
        <source>File Name</source>
        <translation>檔案名稱</translation>
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
        <translation>完整來源</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type</source>
        <translation>類型</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Artist</source>
        <translation>演出者</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Album</source>
        <translation>專輯</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Title</source>
        <translation>標題</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Length</source>
        <translation>長度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bitrate</source>
        <translation>位元率</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Codec</source>
        <translation>編解碼器</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Known</source>
        <translation>已知</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Seen</source>
        <translation>已見過</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Confidence</source>
        <translation>可信度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>File ID</source>
        <translation>檔案 ID</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Folder</source>
        <translation>資料夾</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>AICH Hash</source>
        <translation>AICH 切細值</translation>
    </message>
</context>
<context>
    <name>eMule::ServerListModel</name>
    <message>
        <location filename="../src/gui/controls/ServerListModel.cpp" line="+77"/>
        <source>%1 (disabled)</source>
        <translation>%1（已停用）</translation>
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
        <location line="+89"/>
        <source>Server Name</source>
        <translation>伺服器名稱</translation>
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
        <translation>使用者</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Max Users</source>
        <translation>最大使用者數</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Files</source>
        <translation>檔案</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Preference</source>
        <translation>偏好</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Failed</source>
        <translation>失敗</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Static</source>
        <translation>靜態</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Soft File Limit</source>
        <translation>軟性檔案限制</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Low ID</source>
        <translation>低-ID</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Obfuscation</source>
        <translation>混淆</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Country</source>
        <translation>國家</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>IPv6</source>
        <translation>IPv6</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hard File Limit</source>
        <translation>硬性檔案限制</translation>
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
</context>
<context>
    <name>eMule::ServerPanel</name>
    <message>
        <location filename="../src/gui/panels/ServerPanel.cpp" line="+207"/>
        <source>Invalid URL: %1</source>
        <translation>無效的 URL：%1</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Downloading server.met from %1 ...</source>
        <translation>正在從 %1 下載 server.met ...</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Failed to download server.met: %1</source>
        <translation>下載 server.met 失敗：%1</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Downloaded empty server.met file.</source>
        <translation>下載的 server.met 檔案是空的。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Downloaded server.met (%1 bytes). Parsing...</source>
        <translation>已下載 server.met（%1 位元組）。正在解析...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloaded server.met, unpacked &quot;%1&quot; (%2 bytes). Parsing...</source>
        <translation>已下載 server.met，已解壓縮「%1」（%2 位元組）。正在解析...</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Disconnect</source>
        <translation>中斷連線</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+434"/>
        <source>Connect</source>
        <translation>連線</translation>
    </message>
    <message>
        <location line="-381"/>
        <source>Connect To</source>
        <translation>連線到</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Priority</source>
        <translation>優先權</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Low</source>
        <translation>低</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+547"/>
        <source>Normal</source>
        <translation>普通</translation>
    </message>
    <message>
        <location line="-546"/>
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
        <translation>加入到靜態清單</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Remove From Static List</source>
        <translation>從靜態清單移除</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Enable</source>
        <translation>啟用</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Copy eD2K Links</source>
        <translation>複製 eD2K 連結</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Paste eD2K Links</source>
        <translation>貼上 eD2K 連結</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Remove</source>
        <translation>移除</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Remove All</source>
        <translation>全部移除</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>eNode Account...</source>
        <translation>eNode 帳戶...</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Find...</source>
        <translation>尋找...</translation>
    </message>
    <message>
        <location line="+54"/>
        <source>▸ Servers (0)</source>
        <translation>▸ 伺服器 (0)</translation>
    </message>
    <message>
        <location line="+80"/>
        <source>New Server</source>
        <translation>新伺服器</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>IP Address:</source>
        <translation>IP 位址：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Port:</source>
        <translation>連接埠：</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+152"/>
        <source>Name:</source>
        <translation>名稱：</translation>
    </message>
    <message>
        <location line="-146"/>
        <source>Add to list</source>
        <translation>加入到清單</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Update server.met from URL</source>
        <translation>從 URL 更新 server.met</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Update server.met from URL:</source>
        <translation>從 URL 更新 server.met：</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Update</source>
        <translation>更新</translation>
    </message>
    <message>
        <location line="+70"/>
        <source>My Info</source>
        <translation>我的資訊</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>eD2K Network</source>
        <translation>eD2K 網路</translation>
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
        <translation>狀態：</translation>
    </message>
    <message>
        <location line="-95"/>
        <source>Connected</source>
        <translation>已連線</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+2"/>
        <location line="+24"/>
        <location line="+39"/>
        <source>IP:Port:</source>
        <translation>IP:連接埠：</translation>
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
        <location line="-61"/>
        <source>Low ID</source>
        <translation>低-ID</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>High ID</source>
        <translation>高-ID</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>eD2K Server</source>
        <translation>eD2K 伺服器</translation>
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
        <translation>使用者數：</translation>
    </message>
    <message>
        <location line="-47"/>
        <location line="+49"/>
        <source>Files:</source>
        <translation>檔案數：</translation>
    </message>
    <message>
        <location line="-48"/>
        <source>Connection:</source>
        <translation>連線：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Obfuscated</source>
        <translation>已模糊化</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+47"/>
        <source>Connecting...</source>
        <translation>連線中...</translation>
    </message>
    <message>
        <location line="-45"/>
        <location line="+48"/>
        <source>Disconnected</source>
        <translation>已中斷連線</translation>
    </message>
    <message>
        <location line="-44"/>
        <source>Kad Network</source>
        <translation>Kad 網路</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+8"/>
        <source>Firewalled</source>
        <translation>防火牆內</translation>
    </message>
    <message>
        <location line="-5"/>
        <location line="+7"/>
        <source>Open</source>
        <translation>開放</translation>
    </message>
    <message>
        <location line="-3"/>
        <location line="+6"/>
        <source>UDP Status:</source>
        <translation>UDP 狀態：</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>unverified</source>
        <translation>未驗證</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Extern UDP Port:</source>
        <translation>外部 UDP 連接埠：</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Web Interface</source>
        <translation>Web 介面</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Enabled</source>
        <translation>已啟用</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Disabled</source>
        <translation>已停用</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>▸ Servers (%1)</source>
        <translation>▸ 伺服器 (%1)</translation>
    </message>
    <message>
        <location line="+228"/>
        <source>Invalid server.met header: 0x%1</source>
        <translation>無效的 server.met 標頭：0x%1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Server count too large: %1</source>
        <translation>伺服器數量過多：%1</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Corrupt server.met: tag count %1 at server %2</source>
        <translation>server.met 已損壞：伺服器 %2 的標籤數 %1</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Corrupt server.met: truncated tag name</source>
        <translation>server.met 已損壞：標籤名稱被截斷</translation>
    </message>
    <message>
        <location line="+52"/>
        <source>Corrupt server.met: truncated hash tag</source>
        <translation>server.met 已損壞：切細值標籤被截斷</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>Unknown tag type 0x%1 at server %2, stopping parse</source>
        <translation>伺服器 %2 出現未知的標籤類型 0x%1，停止解析</translation>
    </message>
    <message>
        <location line="+49"/>
        <source>server.met processed: %1 servers added, %2 skipped (duplicates/invalid).</source>
        <translation>server.met 處理完成：已加入 %1 個伺服器，略過 %2 個（重複/無效）。</translation>
    </message>
    <message numerus="yes">
        <location line="+141"/>
        <source>%n server(s) not added — LAN addresses are filtered</source>
        <translation>
            <numerusform>未加入 %n 個伺服器 — 已過濾區域網路位址</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>eMule::SharedFilesModel</name>
    <message>
        <location filename="../src/gui/controls/SharedFilesModel.cpp" line="+44"/>
        <source>Yes</source>
        <translation>是</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>No</source>
        <translation>否</translation>
    </message>
    <message>
        <location line="+109"/>
        <source>%1 Kbit/s</source>
        <translation>%1 Kbit/s</translation>
    </message>
    <message>
        <location line="+7"/>
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
        <translation>檔案名稱：	%1
ED2K 切細值：	%2
類型：	%3
優先權：	%4
要求：	%5 (%6)
已接受：	%7 (%8)
已傳輸：	%9 (%10)
完整來源：	%11
分享 eD2K|Kad：	%12
資料夾：	%13</translation>
    </message>
    <message>
        <location line="+134"/>
        <source>File Name</source>
        <translation>檔案名稱</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type</source>
        <translation>類型</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority</source>
        <translation>優先權</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Requests</source>
        <translation>要求</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Transferred Data</source>
        <translation>已傳輸資料</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Shared parts</source>
        <translation>已分享部分</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Complete Sources</source>
        <translation>完整來源</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Shared eD2K|Kad</source>
        <translation>分享 eD2K|Kad</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Folder</source>
        <translation>資料夾</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>File ID</source>
        <translation>檔案 ID</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Accepted Requests</source>
        <translation>已接受的要求</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Artist</source>
        <translation>演出者</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Album</source>
        <translation>專輯</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Title</source>
        <translation>標題</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Length</source>
        <translation>長度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bitrate</source>
        <translation>位元率</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Codec</source>
        <translation>編解碼器</translation>
    </message>
</context>
<context>
    <name>eMule::SharedFilesPanel</name>
    <message>
        <location filename="../src/gui/panels/SharedFilesPanel.cpp" line="+176"/>
        <location line="+16"/>
        <location line="+490"/>
        <location line="+619"/>
        <source>Shared Files (0)</source>
        <translation>已分享檔案 (0)</translation>
    </message>
    <message>
        <location line="-996"/>
        <source>All</source>
        <translation>全部</translation>
    </message>
    <message>
        <location line="+95"/>
        <source>Open File</source>
        <translation>開啟檔案</translation>
    </message>
    <message>
        <location line="+13"/>
        <location line="+1451"/>
        <location line="+515"/>
        <source>Open Folder</source>
        <translation>開啟資料夾</translation>
    </message>
    <message>
        <location line="-1954"/>
        <source>Rename...</source>
        <translation>重新命名...</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+1950"/>
        <source>Delete From Disk</source>
        <translation>從磁碟刪除</translation>
    </message>
    <message>
        <location line="-1935"/>
        <source>Unshare</source>
        <translation>取消分享</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+1930"/>
        <source>Priority (Upload)</source>
        <translation>優先權（上傳）</translation>
    </message>
    <message>
        <location line="-1915"/>
        <location line="+1932"/>
        <source>Very Low</source>
        <translation>非常低</translation>
    </message>
    <message>
        <location line="-1931"/>
        <location line="+1932"/>
        <source>Low</source>
        <translation>低</translation>
    </message>
    <message>
        <location line="-1931"/>
        <location line="+1932"/>
        <source>Normal</source>
        <translation>普通</translation>
    </message>
    <message>
        <location line="-1931"/>
        <location line="+1932"/>
        <source>High</source>
        <translation>高</translation>
    </message>
    <message>
        <location line="-1931"/>
        <location line="+1932"/>
        <source>Release</source>
        <translation>發佈</translation>
    </message>
    <message>
        <location line="-1930"/>
        <location line="+1931"/>
        <source>Auto</source>
        <translation>自動</translation>
    </message>
    <message>
        <location line="-1917"/>
        <source>Collection</source>
        <translation>收藏集</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Create Collection...</source>
        <translation>建立收藏集...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Modify Collection...</source>
        <translation>修改收藏集...</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>View Collection...</source>
        <translation>檢視集合...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Search Author&apos;s Collections...</source>
        <translation>搜尋作者的集合...</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+6"/>
        <source>Search Author&apos;s Collections</source>
        <translation>搜尋作者的集合</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>This collection carries no author key, so its author&apos;s other collections cannot be looked up.</source>
        <translation>此集合不含作者金鑰，因此無法查詢該作者的其他集合。</translation>
    </message>
    <message>
        <location line="+19"/>
        <location line="+1837"/>
        <source>Details...</source>
        <translation>詳細資料...</translation>
    </message>
    <message>
        <location line="-1830"/>
        <location line="+1833"/>
        <source>Comments...</source>
        <translation>評論...</translation>
    </message>
    <message>
        <location line="-1824"/>
        <source>eD2K Links...</source>
        <translation>eD2K 連結...</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Add To IRC Clipboard</source>
        <translation>加入到 IRC 剪貼簿</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Find...</source>
        <translation>尋找...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Web Services</source>
        <translation>Web 服務</translation>
    </message>
    <message>
        <location line="+72"/>
        <source>Reload</source>
        <translation>重新載入</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Rescan the shared directories. Hold %1 to re-read the media information of all shared files instead.</source>
        <translation>重新掃描分享目錄。按住 %1 則改為重新讀取所有分享檔案的媒體資訊。</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>All Shared Files</source>
        <translation>所有分享檔案</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Incoming Files</source>
        <translation>新進檔案</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Incomplete Files</source>
        <translation>未完成的檔案</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Shared Directories</source>
        <translation>分享目錄</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>All Directories</source>
        <translation>所有目錄</translation>
    </message>
    <message>
        <location line="+221"/>
        <source>Current Session</source>
        <translation>目前工作階段</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+45"/>
        <source>Popularity Rank:</source>
        <translation>熱門度排名：</translation>
    </message>
    <message>
        <location line="-39"/>
        <location line="+45"/>
        <source>  Requests:</source>
        <translation>  要求數：</translation>
    </message>
    <message>
        <location line="-38"/>
        <source>On Queue:</source>
        <translation>等候中：</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+40"/>
        <source>  Accepted Uploads:</source>
        <translation>  已接受上傳：</translation>
    </message>
    <message>
        <location line="-33"/>
        <source>Uploading:</source>
        <translation>上傳中：</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+35"/>
        <source>  Transferred:</source>
        <translation>  已傳輸：</translation>
    </message>
    <message>
        <location line="-27"/>
        <source>Total</source>
        <translation>總計</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Statistics</source>
        <translation>統計</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Content</source>
        <translation>內容</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Basic Options</source>
        <translation>基本選項</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Add Source</source>
        <translation>加入來源</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Not available (requires public IP and open firewall)</source>
        <translation>無法使用（需要公開 IP 且防火牆已開放）</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Advanced Options</source>
        <translation>進階選項</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Add HTML</source>
        <translation>加入 HTML</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add Hashset</source>
        <translation>加入片段切細值</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hostname</source>
        <translation>主機名稱</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Copy</source>
        <translation>複製</translation>
    </message>
    <message>
        <location line="+14"/>
        <location line="+470"/>
        <source>Requires a hostname configured in Preferences, or a public IPv6</source>
        <translation>需要在偏好設定中設定主機名稱，或擁有公開 IPv6</translation>
    </message>
    <message>
        <location line="-466"/>
        <source>eD2K Links</source>
        <translation>eD2K 連結</translation>
    </message>
    <message>
        <location line="+167"/>
        <source>Shared Files (%1)</source>
        <translation>已分享檔案 (%1)</translation>
    </message>
    <message>
        <location line="+55"/>
        <source>%1 (%2 of %3 shared)</source>
        <translation>%1（%3 個中已分享 %2 個）</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Could not share that file</source>
        <translation>無法分享該檔案</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Could not unshare that file</source>
        <translation>無法取消分享該檔案</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>Are you sure you want to permanently delete &quot;%1&quot; from disk?</source>
        <translation>確定要從磁碟永久刪除「%1」嗎？</translation>
    </message>
    <message numerus="yes">
        <location line="+2"/>
        <source>Are you sure you want to permanently delete %n selected file(s) from disk?</source>
        <translation>
            <numerusform>確定要從磁碟永久刪除選取的 %n 個檔案嗎？</numerusform>
        </translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Delete File</source>
        <translation>刪除檔案</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Delete Files</source>
        <translation>刪除檔案</translation>
    </message>
    <message numerus="yes">
        <location line="+5"/>
        <source>Deleting %n shared file(s) from disk</source>
        <translation>
            <numerusform>正在從磁碟刪除 %n 個分享檔案</numerusform>
        </translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Remove &quot;%1&quot; from the shared files list?

The file will remain on disk.</source>
        <translation>要從分享檔案清單中移除「%1」嗎？

檔案仍會保留在磁碟上。</translation>
    </message>
    <message numerus="yes">
        <location line="+2"/>
        <source>Remove %n selected file(s) from the shared files list?

The files will remain on disk.</source>
        <translation>
            <numerusform>要從分享檔案清單中移除選取的 %n 個檔案嗎？

這些檔案仍會保留在磁碟上。</numerusform>
        </translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Unshare File</source>
        <translation>取消分享檔案</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Unshare Files</source>
        <translation>取消分享檔案</translation>
    </message>
    <message>
        <location line="+155"/>
        <source>Add your hostname or public IPv6 as a source</source>
        <translation>將您的主機名稱或公開 IPv6 加入為來源</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Showing eD2K links for the first %1 of %2 selected files.</source>
        <translation>正在顯示選取的 %2 個檔案中前 %1 個的 eD2K 連結。</translation>
    </message>
    <message>
        <location line="+283"/>
        <source>Share Directory</source>
        <translation>分享目錄</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Share with Subdirectories</source>
        <translation>連同子目錄一起分享</translation>
    </message>
    <message>
        <location line="+12"/>
        <location line="+526"/>
        <source>Unshare Directory</source>
        <translation>取消分享目錄</translation>
    </message>
    <message>
        <location line="-517"/>
        <location line="+526"/>
        <source>Unshare with Subdirectories</source>
        <translation>連同子目錄一起取消分享</translation>
    </message>
    <message>
        <location line="-430"/>
        <source>Open File not available — web server is not running or stream token not received.</source>
        <translation>無法開啟檔案 — Web 伺服器未執行或未收到串流權杖。</translation>
    </message>
    <message>
        <location line="+415"/>
        <source>Copy eD2K Links</source>
        <translation>複製 eD2K 連結</translation>
    </message>
</context>
<context>
    <name>eMule::StatisticsPanel</name>
    <message>
        <location filename="../src/gui/panels/StatisticsPanel.cpp" line="-161"/>
        <location line="+107"/>
        <source>Average (%1 mins)</source>
        <translation>平均 (%1 分鐘)</translation>
    </message>
    <message>
        <location line="-86"/>
        <location line="+103"/>
        <source>Active connections (1:%1)</source>
        <translation>使用中連線 (1:%1)</translation>
    </message>
    <message>
        <location line="-50"/>
        <source>Statistics Tree</source>
        <translation>統計樹狀</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+1064"/>
        <source>Statistics last reset: %1</source>
        <translation>統計上次重設：%1</translation>
    </message>
    <message>
        <location line="-1064"/>
        <location line="+312"/>
        <location line="+574"/>
        <location line="+167"/>
        <location line="+8"/>
        <location line="+67"/>
        <location line="+69"/>
        <location line="+988"/>
        <source>Unknown</source>
        <translation>未知</translation>
    </message>
    <message>
        <location line="-2160"/>
        <location line="+9"/>
        <source>Session average</source>
        <translation>工作階段平均值</translation>
    </message>
    <message>
        <location line="-6"/>
        <location line="+8"/>
        <source>Current</source>
        <translation>目前</translation>
    </message>
    <message>
        <location line="-7"/>
        <location line="+1942"/>
        <source>Usenet</source>
        <translation>Usenet</translation>
    </message>
    <message>
        <location line="-1941"/>
        <source>Download Speed</source>
        <translation>下載速度</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Current (excl. overhead)</source>
        <translation>目前（不含額外支出）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Friend upload</source>
        <translation>好友上傳</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload Speed</source>
        <translation>上傳速度</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Active uploads</source>
        <translation>使用中上傳</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total uploads</source>
        <translation>上傳總數</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Active downloads</source>
        <translation>使用中下載</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Connections</source>
        <translation>連線數</translation>
    </message>
    <message>
        <location line="+48"/>
        <source>Transfer</source>
        <translation>傳輸</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+685"/>
        <location line="+45"/>
        <location line="+1019"/>
        <source>Waiting...</source>
        <translation>等待中...</translation>
    </message>
    <message>
        <location line="-1748"/>
        <location line="+620"/>
        <source>Session UL:DL Ratio: %1</source>
        <translation>工作階段 上傳:下載 比率：%1</translation>
    </message>
    <message>
        <location line="-618"/>
        <location line="+620"/>
        <source>Session UL:DL Ratio (Friends UL excluded): %1</source>
        <translation>工作階段 上傳:下載 比率（不含對好友的上傳）：%1</translation>
    </message>
    <message>
        <location line="-619"/>
        <location line="+621"/>
        <source>Cumulative UL:DL Ratio: %1</source>
        <translation>累積 上傳:下載 比率：%1</translation>
    </message>
    <message>
        <location line="-618"/>
        <location line="+165"/>
        <location line="+25"/>
        <location line="+336"/>
        <location line="+1640"/>
        <source>Uploads</source>
        <translation>上傳</translation>
    </message>
    <message>
        <location line="-2162"/>
        <location line="+67"/>
        <location line="+83"/>
        <location line="+56"/>
        <location line="+43"/>
        <location line="+1615"/>
        <location line="+166"/>
        <location line="+121"/>
        <source>Session</source>
        <translation>工作階段</translation>
    </message>
    <message>
        <location line="-2148"/>
        <location line="+35"/>
        <source>Uploaded Data: 0 Bytes</source>
        <translation>已上傳資料：0 Bytes</translation>
    </message>
    <message>
        <location line="-34"/>
        <location line="+35"/>
        <location line="+32"/>
        <location line="+43"/>
        <location line="+114"/>
        <location line="+284"/>
        <source>Clients</source>
        <translation>用戶端</translation>
    </message>
    <message>
        <location line="-505"/>
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
        <location line="+279"/>
        <source>Port</source>
        <translation>連接埠</translation>
    </message>
    <message>
        <location line="-507"/>
        <location line="+35"/>
        <location line="+32"/>
        <location line="+43"/>
        <source>Default Port 4662: 0 Bytes</source>
        <translation>預設連接埠 4662：0 Bytes</translation>
    </message>
    <message>
        <location line="-109"/>
        <location line="+35"/>
        <location line="+32"/>
        <location line="+43"/>
        <source>Other Ports: 0 Bytes</source>
        <translation>其他連接埠：0 Bytes</translation>
    </message>
    <message>
        <location line="-109"/>
        <location line="+35"/>
        <location line="+481"/>
        <source>Data Source</source>
        <translation>資料來源</translation>
    </message>
    <message>
        <location line="-515"/>
        <location line="+35"/>
        <source>Complete File: 0 Bytes</source>
        <translation>完整檔案：0 Bytes</translation>
    </message>
    <message>
        <location line="-34"/>
        <location line="+35"/>
        <source>Part File: 0 Bytes</source>
        <translation>未完成檔案：0 Bytes</translation>
    </message>
    <message>
        <location line="-32"/>
        <source>Uploaded Data to Friends: 0 Bytes</source>
        <translation>上傳給好友的資料：0 Bytes</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+28"/>
        <location line="+480"/>
        <location line="+141"/>
        <location line="+45"/>
        <source>Upload Sessions</source>
        <translation>上傳工作階段</translation>
    </message>
    <message>
        <location line="-689"/>
        <location line="+28"/>
        <source>Average Upload Per Session: 0 Bytes</source>
        <translation>每個工作階段平均上傳：0 Bytes</translation>
    </message>
    <message>
        <location line="-26"/>
        <location line="+28"/>
        <source>Average Upload Time: 0:00:00</source>
        <translation>平均上傳時間：0:00:00</translation>
    </message>
    <message>
        <location line="-22"/>
        <location line="+75"/>
        <location line="+66"/>
        <location line="+41"/>
        <location line="+32"/>
        <location line="+1620"/>
        <location line="+162"/>
        <location line="+125"/>
        <source>Cumulative</source>
        <translation>累積</translation>
    </message>
    <message>
        <location line="-2093"/>
        <location line="+105"/>
        <location line="+24"/>
        <location line="+345"/>
        <location line="+1249"/>
        <location line="+380"/>
        <source>Downloads</source>
        <translation>下載</translation>
    </message>
    <message>
        <location line="-2096"/>
        <location line="+43"/>
        <source>Downloaded Data: 0 Bytes</source>
        <translation>已下載資料：0 Bytes</translation>
    </message>
    <message>
        <location line="-34"/>
        <location line="+43"/>
        <source>Completed Downloads: 0</source>
        <translation>已完成下載：0</translation>
    </message>
    <message>
        <location line="-41"/>
        <source>Found Sources: 0</source>
        <translation>發現來源數：0</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>UDP File Re-asks: 0, Failed: 0 (0.0%)</source>
        <translation>UDP 檔案重新詢問：0，失敗：0 (0.0%)</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+34"/>
        <location line="+419"/>
        <location line="+270"/>
        <location line="+10"/>
        <source>Download Sessions</source>
        <translation>下載工作階段</translation>
    </message>
    <message>
        <location line="-728"/>
        <location line="+34"/>
        <source>Average Download Per Session: 0 Bytes</source>
        <translation>每個工作階段平均下載：0 Bytes</translation>
    </message>
    <message>
        <location line="-32"/>
        <location line="+34"/>
        <source>Average Download Time: 0:00:00</source>
        <translation>平均下載時間：0:00:00</translation>
    </message>
    <message>
        <location line="-31"/>
        <location line="+34"/>
        <source>Gain Due To Compression: 0 Bytes (0.0%)</source>
        <translation>壓縮所得：0 Bytes (0.0%)</translation>
    </message>
    <message>
        <location line="-32"/>
        <location line="+34"/>
        <source>Lost Due To Corruption: 0 Bytes (0.0%)</source>
        <translation>損壞所失：0 Bytes (0.0%)</translation>
    </message>
    <message>
        <location line="-32"/>
        <location line="+34"/>
        <source>Parts Saved Due To ICH: 0</source>
        <translation>由 ICH 挽救的部分：0</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Connection</source>
        <translation>連線</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+26"/>
        <location line="+1586"/>
        <source>General</source>
        <translation>一般</translation>
    </message>
    <message>
        <location line="-1610"/>
        <location line="+26"/>
        <location line="+708"/>
        <location line="+33"/>
        <source>Reconnects: %1</source>
        <translation>重新連線次數：%1</translation>
    </message>
    <message>
        <location line="-759"/>
        <source>Upload Speed: 0 KB/s</source>
        <translation>上傳速度：0 KB/s</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+24"/>
        <source>Max Upload Rate: 0 KB/s</source>
        <translation>最大上傳速率：0 KB/s</translation>
    </message>
    <message>
        <location line="-23"/>
        <location line="+24"/>
        <source>Max Average Upload Rate: 0 KB/s</source>
        <translation>最大平均上傳速率：0 KB/s</translation>
    </message>
    <message>
        <location line="-20"/>
        <source>Download Speed: 0 KB/s</source>
        <translation>下載速度：0 KB/s</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+23"/>
        <source>Max Download Rate: 0 KB/s</source>
        <translation>最大下載速率：0 KB/s</translation>
    </message>
    <message>
        <location line="-22"/>
        <location line="+23"/>
        <source>Max Average Download Rate: 0 KB/s</source>
        <translation>最大平均下載速率：0 KB/s</translation>
    </message>
    <message>
        <location line="-8"/>
        <source>Average Upload Rate: 0 KB/s</source>
        <translation>平均上傳速率：0 KB/s</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Average Download Rate: 0 KB/s</source>
        <translation>平均下載速率：0 KB/s</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Time Statistics</source>
        <translation>時間統計</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+743"/>
        <location line="+5"/>
        <source>Statistics Last Reset: %1</source>
        <translation>統計上次重設：%1</translation>
    </message>
    <message>
        <location line="-746"/>
        <source>Time Since Last Reset: -</source>
        <translation>自上次重設以來的時間：-</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Runtime: 0:00:00</source>
        <translation>執行時間：0:00:00</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+11"/>
        <source>Transfer Time: 0:00:00</source>
        <translation>傳輸時間：0:00:00</translation>
    </message>
    <message>
        <location line="-10"/>
        <location line="+11"/>
        <source>Upload Time: 0:00:00</source>
        <translation>上傳時間：0:00:00</translation>
    </message>
    <message>
        <location line="-10"/>
        <location line="+11"/>
        <source>Download Time: 0:00:00</source>
        <translation>下載時間：0:00:00</translation>
    </message>
    <message>
        <location line="-8"/>
        <source>Current Server Duration: 0:00:00</source>
        <translation>目前伺服器連線時間：0:00:00</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+8"/>
        <source>Total Server Duration: 0:00:00</source>
        <translation>伺服器總連線時間：0:00:00</translation>
    </message>
    <message>
        <location line="-4"/>
        <source>Run Time: 0:00:00</source>
        <translation>執行時間：0:00:00</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Known Clients: 0</source>
        <translation>已知用戶端：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Client Software</source>
        <translation>用戶端軟體</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+1772"/>
        <source>Network</source>
        <translation>網路</translation>
    </message>
    <message>
        <location line="-1766"/>
        <source>Low ID: 0 (0.0%)</source>
        <translation>Low ID：0 (0.0%)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Banned Clients: 0</source>
        <translation>已禁止用戶端：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Filtered Clients: 0</source>
        <translation>已過濾用戶端：0</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Different clients, counted by user hash. An estimate, accurate to about 2%.</source>
        <translation>依使用者切細值計算的不同用戶端。估計值，誤差約 2%。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Clients that said hello on a connection.</source>
        <translation>在連線上送出 hello 的用戶端。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Clients that proved their user hash with Secure Identification.</source>
        <translation>已透過安全身分識別證明其使用者切細值的用戶端。</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+1741"/>
        <source>By Country</source>
        <translation>依國家</translation>
    </message>
    <message>
        <location line="-1740"/>
        <source>Clients by the country of the address they connected from.</source>
        <translation>依連線來源位址所屬國家分類的用戶端。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Servers</source>
        <translation>伺服器</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Working Servers: 0</source>
        <translation>運作中的伺服器：0</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Failed Servers: 0</source>
        <translation>失敗的伺服器：0</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Total: 0</source>
        <translation>總計：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Users: 0</source>
        <translation>使用者總數：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Files: 0</source>
        <translation>檔案總數：0</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+14"/>
        <source>Records</source>
        <translation>記錄</translation>
    </message>
    <message>
        <location line="-12"/>
        <source>Most Working Servers: 0</source>
        <translation>最多運作中伺服器：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Most Users Online: 0</source>
        <translation>最多線上使用者：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Most Files Available: 0</source>
        <translation>最多可用檔案：0</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Shared Files</source>
        <translation>已分享檔案</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+908"/>
        <source>Number of Shared Files: %1</source>
        <translation>分享檔案數：%1</translation>
    </message>
    <message>
        <location line="-895"/>
        <source>Disk Space</source>
        <translation>磁碟空間</translation>
    </message>
    <message>
        <location line="+169"/>
        <source>Projected Averages</source>
        <translation>預估平均值</translation>
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
        <location line="+20"/>
        <location line="+190"/>
        <location line="+57"/>
        <location line="+99"/>
        <location line="+36"/>
        <source>Total Overhead (Packets)</source>
        <translation>總額外支出（封包）</translation>
    </message>
    <message>
        <location line="-381"/>
        <location line="+190"/>
        <location line="+57"/>
        <location line="+99"/>
        <location line="+36"/>
        <source>File Request Overhead (Packets)</source>
        <translation>檔案要求額外支出（封包）</translation>
    </message>
    <message>
        <location line="-381"/>
        <location line="+190"/>
        <location line="+57"/>
        <location line="+99"/>
        <location line="+36"/>
        <source>Source Exchange Overhead (Packets)</source>
        <translation>來源交換額外支出（封包）</translation>
    </message>
    <message>
        <location line="-381"/>
        <location line="+190"/>
        <location line="+57"/>
        <location line="+99"/>
        <location line="+36"/>
        <source>Server Overhead (Packets)</source>
        <translation>伺服器額外支出（封包）</translation>
    </message>
    <message>
        <location line="-381"/>
        <location line="+190"/>
        <location line="+57"/>
        <location line="+99"/>
        <location line="+36"/>
        <source>Kad Overhead (Packets)</source>
        <translation>Kad 額外支出（封包）</translation>
    </message>
    <message>
        <location line="-363"/>
        <source>Default Port 4662</source>
        <translation>預設連接埠 4662</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Other Ports</source>
        <translation>其他連接埠</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Uploaded Data</source>
        <translation>已上傳資料</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Complete File</source>
        <translation>完整檔案</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Part File</source>
        <translation>未完成檔案</translation>
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
        <translation>失敗</translation>
    </message>
    <message>
        <location line="-6"/>
        <source>Downloaded Data</source>
        <translation>已下載資料</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Completed Downloads</source>
        <translation>已完成下載</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Gained Due To Compression</source>
        <translation>壓縮所得</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Lost Due To Corruption</source>
        <translation>損壞所失</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Parts Saved Due To ICH</source>
        <translation>由 ICH 挽救的部分</translation>
    </message>
    <message>
        <location line="+68"/>
        <location line="+74"/>
        <source>Uploaded Data: %1</source>
        <translation>已上傳資料：%1</translation>
    </message>
    <message>
        <location line="-63"/>
        <location line="+73"/>
        <location line="+57"/>
        <location line="+99"/>
        <source>Default Port 4662: %1 %2</source>
        <translation>預設連接埠 4662：%1 %2</translation>
    </message>
    <message>
        <location line="-226"/>
        <location line="+73"/>
        <location line="+57"/>
        <location line="+99"/>
        <source>Other Ports: %1 %2</source>
        <translation>其他連接埠：%1 %2</translation>
    </message>
    <message>
        <location line="-226"/>
        <location line="+73"/>
        <source>Complete File: %1 %2</source>
        <translation>完整檔案：%1 %2</translation>
    </message>
    <message>
        <location line="-70"/>
        <location line="+73"/>
        <source>Part File: %1 %2</source>
        <translation>未完成檔案：%1 %2</translation>
    </message>
    <message>
        <location line="-68"/>
        <source>Uploaded Data to Friends: %1</source>
        <translation>上傳給好友的資料：%1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Active Uploads/Needed to fill Bandwidth: %1</source>
        <translation>使用中上傳/填滿頻寬所需：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Total Uploads: %1</source>
        <translation>上傳總數：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Waiting Uploads: %1</source>
        <translation>等待中上傳：%1</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Total successful upload sessions</source>
        <translation>成功的上傳工作階段總數</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total failed upload sessions</source>
        <translation>失敗的上傳工作階段總數</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Successful Download Sessions</source>
        <translation>成功的下載工作階段</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Failed Download Sessions</source>
        <translation>失敗的下載工作階段</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+45"/>
        <source>Average Upload Per Session: %1</source>
        <translation>每個工作階段平均上傳：%1</translation>
    </message>
    <message>
        <location line="-42"/>
        <location line="+45"/>
        <source>Average Upload Time: %1</source>
        <translation>平均上傳時間：%1</translation>
    </message>
    <message>
        <location line="-41"/>
        <source>%1: %2</source>
        <translation>%1：%2</translation>
    </message>
    <message>
        <location line="+68"/>
        <location line="+99"/>
        <location line="+926"/>
        <source>Downloaded Data: %1</source>
        <translation>已下載資料：%1</translation>
    </message>
    <message>
        <location line="-1007"/>
        <source>Active Downloads (chunks): %1</source>
        <translation>使用中下載（區塊）：%1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Found Sources: %1</source>
        <translation>發現來源數：%1</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>On Queue</source>
        <translation>等候中</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Queue Full</source>
        <translation>佇列已滿</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>No needed parts</source>
        <translation>沒有需要的部分</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Asking</source>
        <translation>要求中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Receiving hashset</source>
        <translation>片段切細值接收中</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+583"/>
        <source>Connecting</source>
        <translation>連線中</translation>
    </message>
    <message>
        <location line="-583"/>
        <source>Connecting via server</source>
        <translation>經由伺服器連線中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Too many connections</source>
        <translation>太多連線數</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Cannot connect LowID to LowID</source>
        <translation>無法連線 Low ID 到 Low ID</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+261"/>
        <source>Problematic</source>
        <translation>有問題的</translation>
    </message>
    <message>
        <location line="-261"/>
        <source>Banned</source>
        <translation>已封鎖</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Asked for another file</source>
        <translation>已要求其他檔案</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>via eD2K Server</source>
        <translation>經由 eD2K 伺服器</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>via Kad</source>
        <translation>經由 Kad</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>via Source Exchange</source>
        <translation>經由來源交換</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>via Passive</source>
        <translation>經由被動方式</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Dead Sources: %1 (%2 + %3)</source>
        <translation>無效來源：%1 (%2 + %3)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>UDP File Re-asks: %1, Failed: %2 %3</source>
        <translation>UDP 檔案重新詢問：%1，失敗：%2 %3</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+61"/>
        <source>Completed Downloads: %1</source>
        <translation>已完成下載：%1</translation>
    </message>
    <message>
        <location line="-50"/>
        <location line="+10"/>
        <source>Average Download Per Session: %1</source>
        <translation>每個工作階段平均下載：%1</translation>
    </message>
    <message>
        <location line="-8"/>
        <location line="+10"/>
        <source>Average Download Time: %1</source>
        <translation>平均下載時間：%1</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+36"/>
        <source>Gain Due To Compression: %1 %2</source>
        <translation>壓縮所得：%1 %2</translation>
    </message>
    <message>
        <location line="-34"/>
        <location line="+36"/>
        <source>Lost Due To Corruption: %1 %2</source>
        <translation>損壞所失：%1 %2</translation>
    </message>
    <message>
        <location line="-34"/>
        <location line="+36"/>
        <source>Parts Saved Due To ICH: %1</source>
        <translation>由 ICH 挽救的部分：%1</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Active Connections (estimate): %1 (Half:%2 | Compl:%3 | Other:%4)</source>
        <translation>使用中連線（估計）：%1（半開：%2 | 完成：%3 | 其他：%4）</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+30"/>
        <source>Average Connections (estimate): %1</source>
        <translation>平均連線數（估計）：%1</translation>
    </message>
    <message>
        <location line="-28"/>
        <location line="+30"/>
        <source>Peak Connections (estimate): %1</source>
        <translation>連線數峰值（估計）：%1</translation>
    </message>
    <message>
        <location line="-24"/>
        <source>Max Connection Limit Reached: %1 : %2</source>
        <translation>達到最大連線數限制：%1 : %2</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+22"/>
        <source>Max Connection Limit Reached: %1</source>
        <translation>達到最大連線數限制：%1</translation>
    </message>
    <message>
        <location line="-19"/>
        <source>Average Uploadrate: %1</source>
        <translation>平均上傳速率：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Average Downloadrate: %1</source>
        <translation>平均下載速率：%1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Upload Speed: %1</source>
        <translation>上傳速度：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+17"/>
        <source>Max Upload Rate: %1</source>
        <translation>最大上傳速率：%1</translation>
    </message>
    <message>
        <location line="-16"/>
        <location line="+17"/>
        <source>Max Average Upload Rate: %1</source>
        <translation>最大平均上傳速率：%1</translation>
    </message>
    <message>
        <location line="-16"/>
        <location line="+850"/>
        <source>Download Speed: %1</source>
        <translation>下載速度：%1</translation>
    </message>
    <message>
        <location line="-849"/>
        <location line="+17"/>
        <location line="+834"/>
        <source>Max Download Rate: %1</source>
        <translation>最大下載速率：%1</translation>
    </message>
    <message>
        <location line="-850"/>
        <location line="+17"/>
        <source>Max Average Download Rate: %1</source>
        <translation>最大平均下載速率：%1</translation>
    </message>
    <message>
        <location line="-5"/>
        <source>Average Upload Rate: %1</source>
        <translation>平均上傳速率：%1</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+834"/>
        <source>Average Download Rate: %1</source>
        <translation>平均下載速率：%1</translation>
    </message>
    <message>
        <location line="-821"/>
        <location line="+5"/>
        <source>Time Since Last Reset: %1</source>
        <translation>自上次重設以來的時間：%1</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Runtime: %1</source>
        <translation>執行時間：%1</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+20"/>
        <source>Transfer Time: %1 %2</source>
        <translation>傳輸時間：%1 %2</translation>
    </message>
    <message>
        <location line="-18"/>
        <location line="+20"/>
        <source>Upload Time: %1 %2</source>
        <translation>上傳時間：%1 %2</translation>
    </message>
    <message>
        <location line="-18"/>
        <location line="+20"/>
        <location line="+776"/>
        <source>Download Time: %1 %2</source>
        <translation>下載時間：%1 %2</translation>
    </message>
    <message>
        <location line="-794"/>
        <source>Current Server Duration: %1 %2</source>
        <translation>目前伺服器連線時間：%1 %2</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+17"/>
        <source>Total Server Duration: %1 %2</source>
        <translation>伺服器總連線時間：%1 %2</translation>
    </message>
    <message>
        <location line="-8"/>
        <source>Run Time: %1</source>
        <translation>執行時間：%1</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Known Clients: %1</source>
        <translation>已知用戶端：%1</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Low ID: %1 %2</source>
        <translation>Low ID：%1 %2</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Default</source>
        <translation>預設</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Other</source>
        <translation>其他</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Secure Ident (OK : Failed )</source>
        <translation>安全身分識別（成功 : 失敗 ）</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Banned Clients: %1</source>
        <translation>已禁止用戶端：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Filtered Clients: %1</source>
        <translation>已過濾用戶端：%1</translation>
    </message>
    <message>
        <location line="+84"/>
        <source>Minor</source>
        <translation>次要</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Working Servers: %1</source>
        <translation>運作中的伺服器：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Users on Working Servers: %1; Low ID: %2 (%3%)</source>
        <translation>運作中伺服器上的使用者：%1；Low ID：%2 (%3%)</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Files on Working Servers: %1</source>
        <translation>運作中伺服器上的檔案：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server Occupation: %1%</source>
        <translation>伺服器使用率：%1%</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Failed Servers: %1</source>
        <translation>失敗的伺服器：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Deleted Servers: %1</source>
        <translation>已刪除的伺服器：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total: %1</source>
        <translation>總計：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Users: %1</source>
        <translation>使用者總數：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Files: %1</source>
        <translation>檔案總數：%1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Most Working Servers: %1</source>
        <translation>最多運作中伺服器：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Most Users Online: %1</source>
        <translation>最多線上使用者：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Most Files Available: %1</source>
        <translation>最多可用檔案：%1</translation>
    </message>
    <message>
        <location line="+10"/>
        <source> (%1 hashing)</source>
        <translation> （%1 個切細中）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Average file size: %1</source>
        <translation>平均檔案大小：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+9"/>
        <source>Largest Shared File: %1</source>
        <translation>最大分享檔案：%1</translation>
    </message>
    <message>
        <location line="-7"/>
        <source>Total size of Shared Files: %1</source>
        <translation>分享檔案總大小：%1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Max. Files Ever Shared: %1</source>
        <translation>歷史最多分享檔案數：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Largest Average File Size: %1</source>
        <translation>最大平均檔案大小：%1</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Largest Share Size: %1</source>
        <translation>最大分享容量：%1</translation>
    </message>
    <message>
        <location line="+10"/>
        <location line="+616"/>
        <source>Number of Downloads: %1</source>
        <translation>下載數量：%1</translation>
    </message>
    <message>
        <location line="-615"/>
        <location line="+623"/>
        <source>Total Size of Downloads: %1</source>
        <translation>下載總大小：%1</translation>
    </message>
    <message>
        <location line="-622"/>
        <source>Total Completed Size: %1 (%2%)</source>
        <translation>已完成總大小：%1 (%2%)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Total Size Left to Transfer: %1</source>
        <translation>剩餘待傳輸總大小：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Free Space on Tempdrive: %1</source>
        <translation>暫存磁碟可用空間：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source> (you need to free %1!)</source>
        <translation> （您需要釋放 %1！）</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Additional Space Needed for Downloads: %1</source>
        <translation>下載所需的額外空間：%1</translation>
    </message>
    <message>
        <location line="+65"/>
        <source>Clients Seen: ≈%1</source>
        <translation>見過的用戶端：≈%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Identified: ≈%1 %2</source>
        <translation>已識別：≈%1 %2</translation>
    </message>
    <message>
        <location line="+51"/>
        <source>Not running</source>
        <translation>未執行</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Connected (LAN mode)</source>
        <translation>已連線（區域網路模式）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Connected, TCP and UDP firewalled</source>
        <translation>已連線，TCP 和 UDP 在防火牆內</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Connected, TCP firewalled</source>
        <translation>已連線，TCP 在防火牆內</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Connected, UDP firewalled</source>
        <translation>已連線，UDP 在防火牆內</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Connected, open</source>
        <translation>已連線，開放</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Status: %1</source>
        <translation>狀態：%1</translation>
    </message>
    <message>
        <location line="+25"/>
        <location line="+49"/>
        <source>Reset Statistics</source>
        <translation>重設統計</translation>
    </message>
    <message>
        <location line="-48"/>
        <location line="+68"/>
        <source>Restore Statistics</source>
        <translation>還原統計</translation>
    </message>
    <message>
        <location line="-60"/>
        <source>Expand Main Sections</source>
        <translation>展開主要部分</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Expand All Sections</source>
        <translation>展開所有部分</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Collapse All Sections</source>
        <translation>摺疊所有部分</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+9"/>
        <source>Copy Branch</source>
        <translation>複製分支</translation>
    </message>
    <message>
        <location line="-8"/>
        <location line="+9"/>
        <source>Copy All Visible</source>
        <translation>複製所有可見項目</translation>
    </message>
    <message>
        <location line="-8"/>
        <location line="+9"/>
        <source>Copy All Statistics</source>
        <translation>複製所有統計</translation>
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
        <translation>匯出統計...</translation>
    </message>
    <message>
        <location line="-68"/>
        <source>Are you sure you wish to reset your cumulative statistics?

If you change your mind, you can reverse this action by clicking the &apos;Restore Stats&apos; button.</source>
        <translation>確定要重設累積統計嗎？

若改變主意，可按「還原統計」按鈕復原此動作。</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Are you sure you wish to restore your cumulative statistics from the backup file?

Clicking &apos;Restore Stats&apos; again will reload your current statistics.</source>
        <translation>確定要從備份檔案還原累積統計嗎？

再次按「還原統計」將重新載入目前的統計。</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>HTML Files (*.html);;All Files (*)</source>
        <translation>HTML 檔案 (*.html);;所有檔案 (*)</translation>
    </message>
    <message>
        <location line="+53"/>
        <location line="+49"/>
        <source>Statistics</source>
        <translation>統計</translation>
    </message>
    <message>
        <location line="-7"/>
        <source>Name:</source>
        <translation>名稱：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>eMule Qt v%1 %2 [%3]</source>
        <translation>eMule Qt v%1 %2 [%3]</translation>
    </message>
    <message>
        <location line="+163"/>
        <location line="+507"/>
        <source>Open Connections: %1</source>
        <translation>開啟的連線：%1</translation>
    </message>
    <message>
        <location line="-506"/>
        <source>Active Connections: %1</source>
        <translation>使用中連線：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Peak Connections: %1</source>
        <translation>連線數峰值：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Network Traffic: %1</source>
        <translation>網路流量：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Overhead: %1 %2</source>
        <translation>額外支出：%1 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Articles</source>
        <translation>文章</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloaded: %1</source>
        <translation>已下載：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not Found on a Server: %1</source>
        <translation>在某個伺服器上找不到：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Missing on All Servers: %1</source>
        <translation>所有伺服器上皆缺少：%1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Corrupt (Failed yEnc Check): %1</source>
        <translation>損壞（yEnc 檢查失敗）：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+502"/>
        <source>Connection Errors: %1</source>
        <translation>連線錯誤：%1</translation>
    </message>
    <message>
        <location line="-499"/>
        <source>Completed Downloads: %1 %2</source>
        <translation>已完成下載：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Completed Data: %1</source>
        <translation>已完成資料：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Failed Downloads: %1 %2</source>
        <translation>失敗的下載：%1 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Post-Processing</source>
        <translation>後續處理</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>PAR2 Verified: %1</source>
        <translation>PAR2 已驗證：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Repaired: %1 %2</source>
        <translation>已修復：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Repair Failed: %1 %2</source>
        <translation>修復失敗：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Blocks Repaired: %1</source>
        <translation>已修復區塊：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Recovery Volumes Fetched: %1</source>
        <translation>已取得復原卷：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Recovery Data: %1</source>
        <translation>復原資料：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unpacked: %1</source>
        <translation>已解壓縮：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+49"/>
        <source>Failed: %1</source>
        <translation>失敗：%1</translation>
    </message>
    <message>
        <location line="-48"/>
        <source>Password Required: %1</source>
        <translation>需要密碼：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Sets Unpacked While Downloading: %1</source>
        <translation>邊下載邊解壓縮的組數：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Time Spent: %1</source>
        <translation>耗費時間：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Verifying: %1 %2</source>
        <translation>驗證中：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Repairing: %1 %2</source>
        <translation>修復中：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unpacking: %1 %2</source>
        <translation>解壓縮中：%1 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Health Checks</source>
        <translation>健康度檢查</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Checks Run: %1</source>
        <translation>已執行檢查：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Passed: %1 %2</source>
        <translation>通過：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paused as Incomplete: %1 %2</source>
        <translation>因不完整而暫停：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Inconclusive: %1 %2</source>
        <translation>無法判定：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Articles Probed: %1</source>
        <translation>已探測文章：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Intake</source>
        <translation>匯入</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>NZBs Added: %1</source>
        <translation>已加入 NZB：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Files: %1 %2</source>
        <translation>檔案：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>URLs: %1 %2</source>
        <translation>URL：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Watch Folder: %1 %2</source>
        <translation>監視資料夾：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Feeds: %1 %2</source>
        <translation>訂閱來源：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Indexer Searches: %1 %2</source>
        <translation>索引站搜尋：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Duplicates: %1</source>
        <translation>重複項目：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Already Downloaded: %1</source>
        <translation>已下載過：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Invalid NZBs: %1</source>
        <translation>無效 NZB：%1</translation>
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
        <translation>搜尋次數：%1</translation>
    </message>
    <message>
        <location line="-137"/>
        <source>API Requests: %1</source>
        <translation>API 要求：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Errors: %1 %2</source>
        <translation>錯誤：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>NZBs Fetched: %1</source>
        <translation>已取得 NZB：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+298"/>
        <source>Failed: %1 %2</source>
        <translation>失敗：%1 %2</translation>
    </message>
    <message>
        <location line="-297"/>
        <source>Feed Polls: %1</source>
        <translation>訂閱來源輪詢：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Feed Matches: %1</source>
        <translation>訂閱來源符合：%1</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Downloading: %1</source>
        <translation>下載中：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Queued: %1</source>
        <translation>已排入佇列：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paused: %1</source>
        <translation>已暫停：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Checking: %1</source>
        <translation>檢查中：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Post-Processing: %1</source>
        <translation>後續處理：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Completed: %1</source>
        <translation>已完成：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Total Size Downloaded: %1</source>
        <translation>已下載總大小：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Size Left to Download: %1</source>
        <translation>剩餘下載大小：%1</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>News Servers</source>
        <translation>新聞伺服器</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Queue</source>
        <translation>佇列</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Contacts: %1</source>
        <translation>聯絡點：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+8"/>
        <source>IP Verified: %1 %2</source>
        <translation>IP 已驗證：%1 %2</translation>
    </message>
    <message>
        <location line="-7"/>
        <source>Type 0, Alive over 2 Hours: %1 %2</source>
        <translation>類型 0，存活超過 2 小時：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type 1, Alive 1 to 2 Hours: %1 %2</source>
        <translation>類型 1，存活 1 至 2 小時：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type 2, Alive under 1 Hour: %1 %2</source>
        <translation>類型 2，存活不到 1 小時：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type 3, New or Not Answering: %1 %2</source>
        <translation>類型 3，新加入或無回應：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type 4, Dead: %1 %2</source>
        <translation>類型 4，已失效：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Most Contacts: %1</source>
        <translation>最多聯絡點：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Contacts Added: %1</source>
        <translation>已加入的聯絡點：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Replaced a Weaker Contact: %1 %2</source>
        <translation>取代了較弱的聯絡點：%1 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Contacts Expired: %1</source>
        <translation>已過期的聯絡點：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Contacts Banned: %1</source>
        <translation>已禁止的聯絡點：%1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Nodes Seen: ≈%1</source>
        <translation>見過的節點：≈%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nodes Heard Of: ≈%1</source>
        <translation>聽說過的節點：≈%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Firewalled (Kad)</source>
        <translation>防火牆內 (Kad)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>UDP: %1 %2</source>
        <translation>UDP：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>TCP: %1 %2</source>
        <translation>TCP：%1 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Source Publishes Received: %1</source>
        <translation>收到的來源發佈：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Open: %1 %2</source>
        <translation>開放：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Firewalled, with Buddy: %1 %2</source>
        <translation>防火牆內，有夥伴：%1 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Firewalled, Direct Callback: %1 %2</source>
        <translation>防火牆內，直接回呼：%1 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Firewalled, without Buddy: %1 %2</source>
        <translation>防火牆內，無夥伴：%1 %2</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Estimated Users: %1</source>
        <translation>估計使用者數：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Estimated Files: %1</source>
        <translation>估計檔案數：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Indexed Keywords: %1</source>
        <translation>已索引的關鍵字：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Indexed Sources: %1</source>
        <translation>已索引的來源：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Indexed Notes: %1</source>
        <translation>已索引的註解：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Active Searches: %1</source>
        <translation>進行中搜尋：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Banned Addresses: %1</source>
        <translation>已禁止的位址：%1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Time Connected: %1 %2</source>
        <translation>連線時間：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hellos Sent: %1</source>
        <translation>已傳送的 Hello：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hellos Answered: %1 %2</source>
        <translation>已回應的 Hello：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Lookup Answers: %1</source>
        <translation>查詢回應：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bootstrap Answers: %1</source>
        <translation>啟動程序回應：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Node Lookups: %1 %2</source>
        <translation>節點查詢：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Keyword Searches: %1 %2</source>
        <translation>關鍵字搜尋：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Source Searches: %1 %2</source>
        <translation>來源搜尋：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Notes Searches: %1 %2</source>
        <translation>註解搜尋：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Publishes: %1</source>
        <translation>發佈：%1</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Kademlia</source>
        <translation>Kademlia</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Different nodes, counted by node ID. An estimate, accurate to about 2%.</source>
        <translation>依節點 ID 計算的不同節點數。估計值，誤差約 2%。</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Routing Table</source>
        <translation>路由表</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>By Version</source>
        <translation>依版本</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Nodes</source>
        <translation>節點</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Nodes that sent us a packet, by the country of the address it came from.</source>
        <translation>曾傳送封包給我們的節點，依來源位址所屬國家分類。</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Activity</source>
        <translation>活動</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Nodes that sent us a packet themselves.</source>
        <translation>曾親自傳送封包給我們的節點。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Nodes that other nodes named in their answers; most are never contacted.</source>
        <translation>其他節點在回應中提到的節點；其中大多數從未被聯絡過。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Counted per publish, not per node. Firewalled nodes that cannot publish are missing.</source>
        <translation>依發佈次數計算，而非依節點。無法發佈的防火牆內節點未列入。</translation>
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
        <translation>%1：%2 %3</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>%1: ≈%2 %3</source>
        <translation>%1：≈%2 %3</translation>
    </message>
    <message>
        <location line="+38"/>
        <source>Published: %1</source>
        <translation>已發佈：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Chunks Published: %1</source>
        <translation>已發佈區塊：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload Saved: %1</source>
        <translation>節省的上傳：%1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Fetched: %1</source>
        <translation>已取得：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Chunks Fetched: %1 %2</source>
        <translation>已取得區塊：%1 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Failed Hash Check: %1</source>
        <translation>切細值檢查失敗：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Resumed: %1</source>
        <translation>已繼續：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Offers Received: %1</source>
        <translation>收到的提議：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Declined: %1 %2</source>
        <translation>已拒絕：%1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Chunks Found in Kad: %1</source>
        <translation>在 Kad 中找到的區塊：%1</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>HTTP Cache</source>
        <translation>HTTP 快取</translation>
    </message>
    <message>
        <location line="+76"/>
        <source>n/a</source>
        <translation>無資料</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Measured here, not reported by the provider, in decimal GB as providers bill. Expect a few percent below the provider&apos;s own figure.</source>
        <translation>此數值於本機計算，並非由供應商回報，以供應商計費所用的十進位 GB 為單位。預計會比供應商自己的數字低幾個百分點。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>%1 (disabled)</source>
        <translation>%1（已停用）</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Session Traffic: %1 %2</source>
        <translation>工作階段流量：%1 %2</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Articles Downloaded: %1</source>
        <translation>已下載文章：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not Found: %1</source>
        <translation>找不到：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Corrupt: %1</source>
        <translation>已損壞：%1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>%1 of %2 %3</source>
        <translation>%1 / %2 %3</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>, resets %1</source>
        <translation>，%1 重設</translation>
    </message>
    <message>
        <location line="+3"/>
        <source> — spent</source>
        <translation> — 已用盡</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Block: %1</source>
        <translation>區塊帳戶：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>This Period: %1</source>
        <translation>本期：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>All Time: %1</source>
        <translation>全部時間：%1</translation>
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
        <translation>%1：%2 @ %3（%4 前）</translation>
    </message>
</context>
<context>
    <name>eMule::ToolbarCustomizeDialog</name>
    <message>
        <location filename="../src/gui/dialogs/ToolbarCustomizeDialog.cpp" line="+26"/>
        <source>── Separator ──</source>
        <translation>── 分隔線 ──</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Customize Toolbar</source>
        <translation>自訂工具列</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Available toolbar buttons:</source>
        <translation>可用的工具列按鈕：</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Add -&gt;</source>
        <translation>加入 -&gt;</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&lt;- Remove</source>
        <translation>&lt;- 移除</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Current toolbar buttons:</source>
        <translation>目前的工具列按鈕：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Close</source>
        <translation>關閉</translation>
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
    <name>eMule::TransferPanel</name>
    <message>
        <location filename="../src/gui/panels/TransferPanel.cpp" line="+362"/>
        <source>Priority (Download)</source>
        <translation>優先權（下載）</translation>
    </message>
    <message>
        <location line="+29"/>
        <location line="+1936"/>
        <location line="+165"/>
        <source>Low</source>
        <translation>低</translation>
    </message>
    <message>
        <location line="-2100"/>
        <location line="+1935"/>
        <location line="+166"/>
        <source>Normal</source>
        <translation>普通</translation>
    </message>
    <message>
        <location line="-2100"/>
        <location line="+1934"/>
        <location line="+167"/>
        <source>High</source>
        <translation>高</translation>
    </message>
    <message>
        <location line="-2097"/>
        <location line="+2101"/>
        <source>Very Low</source>
        <translation>非常低</translation>
    </message>
    <message>
        <location line="-2099"/>
        <location line="+2101"/>
        <source>Very High</source>
        <translation>非常高</translation>
    </message>
    <message>
        <location line="-2099"/>
        <location line="+2102"/>
        <source>Auto</source>
        <translation>自動</translation>
    </message>
    <message>
        <location line="-2091"/>
        <location line="+690"/>
        <location line="+1239"/>
        <source>Pause</source>
        <translation>暫停</translation>
    </message>
    <message>
        <location line="-1920"/>
        <location line="+687"/>
        <location line="+1235"/>
        <source>Stop</source>
        <translation>停止</translation>
    </message>
    <message>
        <location line="-1913"/>
        <location line="+684"/>
        <location line="+1231"/>
        <source>Resume</source>
        <translation>繼續</translation>
    </message>
    <message>
        <location line="-1902"/>
        <location line="+677"/>
        <location line="+1227"/>
        <location line="+4"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="-1892"/>
        <location line="+668"/>
        <location line="+551"/>
        <source>Open File</source>
        <translation>開啟檔案</translation>
    </message>
    <message>
        <location line="-1210"/>
        <location line="+2"/>
        <location line="+671"/>
        <location line="+586"/>
        <source>Preview</source>
        <translation>預覽</translation>
    </message>
    <message>
        <location line="-1251"/>
        <source>Preview with</source>
        <translation>預覽方式</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Pause when preview is possible</source>
        <translation>可預覽時暫停</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Increase priority for preview parts</source>
        <translation>提高預覽部分的優先權</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+2032"/>
        <location line="+99"/>
        <source>Details...</source>
        <translation>詳細資料...</translation>
    </message>
    <message>
        <location line="-2122"/>
        <source>Comments...</source>
        <translation>評論...</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Stop Import parts to file</source>
        <translation>停止將部分匯入檔案</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+1285"/>
        <location line="+1"/>
        <location line="+10"/>
        <source>Import parts to file...</source>
        <translation>將部分匯入檔案...</translation>
    </message>
    <message>
        <location line="-1280"/>
        <location line="+636"/>
        <source>Clear Completed</source>
        <translation>清除已完成</translation>
    </message>
    <message>
        <location line="-628"/>
        <source>Source Handling</source>
        <translation>來源處理</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add Sources...</source>
        <translation>加入來源...</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+1208"/>
        <source>Source Limit...</source>
        <translation>來源限制...</translation>
    </message>
    <message>
        <location line="-1198"/>
        <source>eD2K Links...</source>
        <translation>eD2K 連結...</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Paste eD2K Links</source>
        <translation>貼上 eD2K 連結</translation>
    </message>
    <message>
        <location line="+16"/>
        <location line="+2025"/>
        <location line="+72"/>
        <source>Find...</source>
        <translation>尋找...</translation>
    </message>
    <message>
        <location line="-2089"/>
        <source>Search Related Files</source>
        <translation>搜尋相關檔案</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Web Services</source>
        <translation>Web 服務</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+545"/>
        <source>Assign To Category</source>
        <translation>指定到類別</translation>
    </message>
    <message>
        <location line="-540"/>
        <source>New...</source>
        <translation>新增...</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+543"/>
        <source>(Unassign)</source>
        <translation>（取消指定）</translation>
    </message>
    <message>
        <location line="-525"/>
        <source>Show Toolbar</source>
        <translation>顯示工具列</translation>
    </message>
    <message>
        <location line="+80"/>
        <source>Downloads</source>
        <translation>下載</translation>
    </message>
    <message>
        <location line="+328"/>
        <source>Clients on queue:   0</source>
        <translation>等候中的用戶端：   0</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Close Toolbar</source>
        <translation>關閉工具列</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+1228"/>
        <source>Priority</source>
        <translation>優先權</translation>
    </message>
    <message>
        <location line="-1191"/>
        <source>Open Folder</source>
        <translation>開啟資料夾</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Details</source>
        <translation>詳細資料</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Comments</source>
        <translation>評論</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>eD2K Links</source>
        <translation>eD2K 連結</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Search Related</source>
        <translation>搜尋相關</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Find</source>
        <translation>尋找</translation>
    </message>
    <message>
        <location line="+502"/>
        <location line="+23"/>
        <source>Preview not available — web server is not running or stream token not received.</source>
        <translation>無法預覽 — Web 伺服器未執行或未收到串流權杖。</translation>
    </message>
    <message>
        <location line="-8"/>
        <source>Failed to execute: %1 %2</source>
        <translation>執行失敗：%1 %2</translation>
    </message>
    <message>
        <location line="+46"/>
        <source>Please enter the new source limit for the selected file(s)!
(set to 0 to use global setting)</source>
        <translation>請為選取的檔案輸入新的來源限制！
（設為 0 則使用全域設定）</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>cannot add the source.</source>
        <translation>無法加入來源。</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>The source %1 was not accepted.</source>
        <translation>來源 %1 未被接受。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>cannot import parts.</source>
        <translation>無法匯入部分。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Path of the file on the core&apos;s machine:</source>
        <translation>檔案在核心所在電腦上的路徑：</translation>
    </message>
    <message>
        <location line="+56"/>
        <source>Open File not available — web server is not running or stream token not received.</source>
        <translation>無法開啟檔案 — Web 伺服器未執行或未收到串流權杖。</translation>
    </message>
    <message>
        <location line="+375"/>
        <source>Uploading</source>
        <translation>上傳中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloading</source>
        <translation>下載中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>On Queue</source>
        <translation>等候中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Known Clients</source>
        <translation>已知用戶端</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Uploading (%1)</source>
        <translation>上傳中 (%1)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloading (%1)</source>
        <translation>下載中 (%1)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>On Queue (%1)</source>
        <translation>等候中 (%1)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Known Clients (%1)</source>
        <translation>已知用戶端 (%1)</translation>
    </message>
    <message>
        <location line="+13"/>
        <location line="+7"/>
        <source>Downloads (%1)</source>
        <translation>下載 (%1)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Clients on queue:   %1</source>
        <translation>等候中的用戶端：   %1</translation>
    </message>
    <message>
        <location line="+50"/>
        <source>Are you sure you want to cancel every download in &quot;%1&quot;?</source>
        <translation>確定要取消「%1」中的所有下載嗎？</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Resume next file</source>
        <translation>繼續下一個檔案</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Download In Alphabetical Order</source>
        <translation>依字母順序下載</translation>
    </message>
    <message>
        <location line="+181"/>
        <location line="+98"/>
        <source>Add To Friends</source>
        <translation>加入到好友</translation>
    </message>
    <message>
        <location line="-76"/>
        <location line="+101"/>
        <source>Send Message</source>
        <translation>傳送訊息</translation>
    </message>
    <message>
        <location line="-90"/>
        <location line="+97"/>
        <source>View Shared Files</source>
        <translation>檢視已分享檔案</translation>
    </message>
    <message>
        <location line="-90"/>
        <location line="+91"/>
        <source>cannot view shared files.</source>
        <translation>無法檢視已分享檔案。</translation>
    </message>
    <message>
        <location line="-81"/>
        <source>Unban</source>
        <translation>不禁止</translation>
    </message>
    <message>
        <location line="+115"/>
        <source>Bootstrap</source>
        <translation>啟動</translation>
    </message>
    <message>
        <location line="+168"/>
        <source>Cancel Download</source>
        <translation>取消下載</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+31"/>
        <source>Cancel download &quot;%1&quot;?</source>
        <translation>要取消下載「%1」嗎？</translation>
    </message>
    <message>
        <location line="-27"/>
        <location line="+29"/>
        <source>Cancel Downloads</source>
        <translation>取消下載</translation>
    </message>
    <message>
        <location line="-28"/>
        <location line="+27"/>
        <source>Cancel %1 selected downloads?</source>
        <translation>要取消選取的 %1 個下載嗎？</translation>
    </message>
    <message>
        <location line="+21"/>
        <location line="+18"/>
        <location line="+34"/>
        <source>Rename</source>
        <translation>重新命名</translation>
    </message>
    <message>
        <location line="-51"/>
        <source>Do you want to cleanup the file names of the selected files?</source>
        <translation>要清理選取檔案的檔案名稱嗎？</translation>
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
        <translation>下載：</translation>
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
        <translation>無限制</translation>
    </message>
    <message>
        <location line="-5"/>
        <source>Upload:</source>
        <translation>上傳：</translation>
    </message>
    <message>
        <location line="+34"/>
        <source>Set Full Up/Down-Speed</source>
        <translation>設定全速上傳/下載</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Throttle Up/Down-Speed</source>
        <translation>限制上傳/下載速度</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Pause Usenet</source>
        <translation>暫停 Usenet</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Connect</source>
        <translation>連線</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Disconnect</source>
        <translation>中斷連線</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Options</source>
        <translation>選項</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Restore</source>
        <translation>還原</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Exit</source>
        <translation>結束</translation>
    </message>
</context>
<context>
    <name>eMule::UsenetArchiveEntryDialog</name>
    <message>
        <location filename="../src/gui/dialogs/UsenetArchiveEntryDialog.cpp" line="+59"/>
        <source>Preview File</source>
        <translation>預覽檔案</translation>
    </message>
    <message>
        <location line="+42"/>
        <source>Playable</source>
        <translation>可播放</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>Nothing has arrived for this download yet.</source>
        <translation>此下載尚未收到任何資料。</translation>
    </message>
    <message numerus="yes">
        <location line="+3"/>
        <source>Reading the archive… %n file(s) found so far</source>
        <translation>
            <numerusform>正在讀取壓縮檔… 目前已找到 %n 個檔案</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="+6"/>
        <source>%n file(s) in the archive.</source>
        <translation>
            <numerusform>壓縮檔中有 %n 個檔案。</numerusform>
        </translation>
    </message>
    <message>
        <location line="+34"/>
        <source>Name</source>
        <translation>名稱</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Status</source>
        <translation>狀態</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Reading the archive…</source>
        <translation>正在讀取壓縮檔…</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Preview</source>
        <translation>預覽</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Keep Scanning</source>
        <translation>繼續掃描</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>The connection to the core was lost.</source>
        <translation>與核心的連線已中斷。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>The archive is still being read — the files listed so far are all that is known.</source>
        <translation>壓縮檔仍在讀取中 — 目前列出的檔案就是已知的全部內容。</translation>
    </message>
</context>
<context>
    <name>eMule::UsenetDetailsDialog</name>
    <message>
        <location filename="../src/gui/dialogs/UsenetDetailsDialog.cpp" line="+15"/>
        <location line="+18"/>
        <source>Release Details</source>
        <translation>發佈內容詳細資料</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Not checked.</source>
        <translation>未檢查。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>%1% of this release looks obtainable.</source>
        <translation>此發佈內容約 %1% 看起來可取得。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1% by the NZB&apos;s own article counts. No server was asked.</source>
        <translation>依 NZB 本身的文章數計算為 %1%。未詢問任何伺服器。</translation>
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
            <numerusform>有 %n 篇文章從未列在 NZB 中</numerusform>
        </translation>
    </message>
    <message>
        <location line="+58"/>
        <source>&quot;%1&quot; has not been published on its own.</source>
        <translation>「%1」尚未單獨發佈。</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>&quot;%1&quot; is outside the core&apos;s Incoming folder and cannot be opened from here.</source>
        <translation>「%1」不在核心的 Incoming 資料夾內，無法從這裡開啟。</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Total Size:</source>
        <translation>總大小：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Date:</source>
        <translation>日期：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status:</source>
        <translation>狀態：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Health:</source>
        <translation>健康度：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Poster:</source>
        <translation>發文者：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Articles:</source>
        <translation>文章：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Newsgroups:</source>
        <translation>新聞群組：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Files:</source>
        <translation>檔案數：</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Name</source>
        <translation>名稱</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Progress</source>
        <translation>進度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Articles</source>
        <translation>文章</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Missing</source>
        <translation>缺少</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Status</source>
        <translation>狀態</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Open Folder</source>
        <translation>開啟資料夾</translation>
    </message>
</context>
<context>
    <name>eMule::UsenetFileCheckList</name>
    <message>
        <location filename="../src/gui/controls/UsenetFileCheckList.cpp" line="+53"/>
        <source>Select All</source>
        <translation>全選</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Select None</source>
        <translation>全部不選</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Invert Selection</source>
        <translation>反向選取</translation>
    </message>
    <message>
        <location line="+73"/>
        <source>Could not change which files download.</source>
        <translation>無法變更要下載的檔案。</translation>
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
        <translation>加入 NZB</translation>
    </message>
    <message>
        <location line="-548"/>
        <location line="+50"/>
        <location line="+62"/>
        <source>Not connected to the eMule core.</source>
        <translation>未連線到 eMule 核心。</translation>
    </message>
    <message>
        <location line="-105"/>
        <source>Cannot read %1.</source>
        <translation>無法讀取 %1。</translation>
    </message>
    <message>
        <location line="+104"/>
        <source>Add NZB from URL</source>
        <translation>從 URL 加入 NZB</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Add NZB…</source>
        <translation>加入 NZB…</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+964"/>
        <source>Pause</source>
        <translation>暫停</translation>
    </message>
    <message>
        <location line="-960"/>
        <location line="+963"/>
        <source>Resume</source>
        <translation>繼續</translation>
    </message>
    <message>
        <location line="-961"/>
        <source>Remove</source>
        <translation>移除</translation>
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
        <translation>全部暫停</translation>
    </message>
    <message>
        <location line="-905"/>
        <source>Add NZB from URL…</source>
        <translation>從 URL 加入 NZB…</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Preview</source>
        <translation>預覽</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Check Availability</source>
        <translation>檢查可用性</translation>
    </message>
    <message>
        <location line="+269"/>
        <source>Priority</source>
        <translation>優先權</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Assign To Category</source>
        <translation>指定到類別</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>No category</source>
        <translation>無類別</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Set Password…</source>
        <translation>設定密碼…</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Download Selected Files</source>
        <translation>下載選取的檔案</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Skip Selected Files</source>
        <translation>略過選取的檔案</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Open File</source>
        <translation>開啟檔案</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Open Folder</source>
        <translation>開啟資料夾</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Details…</source>
        <translation>詳細資料…</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Remove and Delete Files</source>
        <translation>移除並刪除檔案</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>NZB files (*.nzb);;All files (*)</source>
        <translation>NZB 檔案 (*.nzb);;所有檔案 (*)</translation>
    </message>
    <message>
        <location line="+34"/>
        <location line="+15"/>
        <source>Set Password</source>
        <translation>設定密碼</translation>
    </message>
    <message>
        <location line="-14"/>
        <source>Archive password for &quot;%1&quot;:</source>
        <translation>「%1」的壓縮檔密碼：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>this download</source>
        <translation>此下載</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Remove the stored password for this download?</source>
        <translation>要移除此下載已儲存的密碼嗎？</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Could not set the password.</source>
        <translation>無法設定密碼。</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Remove Downloads</source>
        <translation>移除下載</translation>
    </message>
    <message numerus="yes">
        <location line="+1"/>
        <source>Remove %n download(s) and delete the files already fetched?</source>
        <translation>
            <numerusform>要移除 %n 個下載並刪除已取得的檔案嗎？</numerusform>
        </translation>
    </message>
    <message>
        <location line="+63"/>
        <location line="+82"/>
        <source>Nothing has completed yet for &quot;%1&quot;.</source>
        <translation>「%1」尚無已完成的內容。</translation>
    </message>
    <message>
        <location line="+100"/>
        <source>Nothing here can be previewed yet.</source>
        <translation>這裡還沒有可預覽的內容。</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Preview is unavailable — the daemon&apos;s web server is not running.</source>
        <translation>無法預覽 — 常駐程式的 Web 伺服器未執行。</translation>
    </message>
    <message>
        <location line="+58"/>
        <source>No Usenet downloads. Use &quot;Add NZB…&quot; to queue one.</source>
        <translation>沒有 Usenet 下載。使用「加入 NZB…」將其排入佇列。</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>%1 download(s), %2 active — %3% complete</source>
        <translation>%1 個下載，%2 個進行中 — 已完成 %3%</translation>
    </message>
    <message>
        <location line="+9"/>
        <source> — %1</source>
        <translation> — %1</translation>
    </message>
    <message>
        <location line="+7"/>
        <source> — limited to %1 KB/s while eD2K downloads</source>
        <translation> — eD2K 下載時限速 %1 KB/s</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Download limit %1 KB/s: Usenet up to %2 KB/s, eD2K up to %3 KB/s.
Whichever network is idle lends its share to the other.</source>
        <translation>下載限制 %1 KB/s：Usenet 最高 %2 KB/s，eD2K 最高 %3 KB/s。
閒置的網路會把自己的份額讓給另一方。</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Usenet: %1</source>
        <translation>Usenet：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Usenet: downloading again.</source>
        <translation>Usenet：已恢復下載。</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Resume All</source>
        <translation>全部繼續</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Let every Usenet download continue</source>
        <translation>讓所有 Usenet 下載繼續</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Stop starting new Usenet articles. Nothing is removed, and each release keeps its own state.</source>
        <translation>停止開始新的 Usenet 文章。不會移除任何內容，每個發佈內容都保留自己的狀態。</translation>
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
        <translation>要移除「%1」中的所有 Usenet 下載並刪除其檔案嗎？</translation>
    </message>
    <message>
        <location line="+40"/>
        <source>Could not apply that to the category: %1</source>
        <translation>無法套用到該類別：%1</translation>
    </message>
</context>
<context>
    <name>eMule::UsenetQueueModel</name>
    <message numerus="yes">
        <location filename="../src/gui/controls/UsenetQueueModel.cpp" line="-316"/>
        <source>%1% — %n article(s) missing</source>
        <translation>
            <numerusform>%1% — 缺少 %n 篇文章</numerusform>
        </translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Skipped — tick it to download this file</source>
        <translation>已略過 — 勾選即可下載此檔案</translation>
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
            <numerusform>，缺少 %n 篇</numerusform>
        </translation>
    </message>
    <message>
        <location line="+84"/>
        <source>Not checked.</source>
        <translation>未檢查。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1% of this release looks obtainable.</source>
        <translation>此發佈內容約 %1% 看起來可取得。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1% by the NZB&apos;s own article counts. No server was asked.</source>
        <translation>依 NZB 本身的文章數計算為 %1%。未詢問任何伺服器。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>The PAR2 recovery volumes should cover the shortfall.</source>
        <translation>PAR2 復原卷應足以補足缺少的部分。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>%1
The password for this release did not work. Right-click to set a different one.</source>
        <translation>%1
此發佈內容的密碼無效。按右鍵可設定其他密碼。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1
This release is password-protected. Right-click to set its password.</source>
        <translation>%1
此發佈內容受密碼保護。按右鍵可設定其密碼。</translation>
    </message>
    <message numerus="yes">
        <location line="+4"/>
        <source>%1
%n article(s) could not be found on any server</source>
        <translation>
            <numerusform>%1
有 %n 篇文章在所有伺服器上都找不到</numerusform>
        </translation>
    </message>
    <message>
        <location line="+4"/>
        <source>%1
A password is set for this release.</source>
        <translation>%1
已為此發佈內容設定密碼。</translation>
    </message>
    <message>
        <location line="+77"/>
        <source>Name</source>
        <translation>名稱</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Progress</source>
        <translation>進度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status</source>
        <translation>狀態</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Speed</source>
        <translation>速度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remaining</source>
        <translation>剩餘</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority</source>
        <translation>優先權</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Health</source>
        <translation>健康度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Category</source>
        <translation>分類</translation>
    </message>
</context>
<context>
    <name>eMule::VersionChecker</name>
    <message>
        <location filename="../src/gui/app/VersionChecker.cpp" line="+100"/>
        <source>the version manifest is not a JSON object</source>
        <translation>版本資訊清單不是 JSON 物件</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>invalid JSON response: %1</source>
        <translation>無效的 JSON 回應：%1</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>the version manifest has no &apos;latest&apos; field</source>
        <translation>版本資訊清單中沒有 &apos;latest&apos; 欄位</translation>
    </message>
</context>
<context>
    <name>eMule::WebLinksDialog</name>
    <message>
        <location filename="../src/gui/dialogs/WebLinksDialog.cpp" line="+47"/>
        <source>Links: %1</source>
        <translation>連結：%1</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>The page says these files are password protected.</source>
        <translation>頁面表示這些檔案受密碼保護。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>File</source>
        <translation>檔案</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Name</source>
        <translation>名稱</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Hoster</source>
        <translation>託管站</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Link</source>
        <translation>連結</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Copy Links</source>
        <translation>複製連結</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Open in Browser</source>
        <translation>在瀏覽器中開啟</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Save List...</source>
        <translation>儲存清單...</translation>
    </message>
    <message numerus="yes">
        <location line="+32"/>
        <source>Copied %n link(s) to the clipboard.</source>
        <translation>
            <numerusform>已將 %n 個連結複製到剪貼簿。</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="+7"/>
        <source>Open %n links in the browser?</source>
        <translation>
            <numerusform>要在瀏覽器中開啟 %n 個連結嗎？</numerusform>
        </translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Save Link List</source>
        <translation>儲存連結清單</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Link lists (*.txt);;All files (*)</source>
        <translation>連結清單 (*.txt);;所有檔案 (*)</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Could not write %1: %2</source>
        <translation>無法寫入 %1：%2</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Saved %1</source>
        <translation>已儲存 %1</translation>
    </message>
</context>
<context>
    <name>eMule::WebServer</name>
    <message>
        <location filename="../src/core/webserver/WebServer.cpp" line="-3363"/>
        <location line="+3252"/>
        <location line="+49"/>
        <source>Session expired — log in again</source>
        <translation>工作階段已過期 — 請重新登入</translation>
    </message>
    <message>
        <location line="-3297"/>
        <source>Guests cannot add downloads</source>
        <translation>訪客無法加入下載</translation>
    </message>
    <message>
        <location line="+590"/>
        <source>Looking for comments on Kad</source>
        <translation>正在 Kad 上尋找評論</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Rating: %1</source>
        <translation>評分：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Has comments</source>
        <translation>有評論</translation>
    </message>
    <message>
        <location line="+403"/>
        <location line="+1"/>
        <source>Incoming</source>
        <translation>下載完成資料夾</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Nothing has finished downloading yet.</source>
        <translation>還沒有下載完成的檔案。</translation>
    </message>
    <message>
        <location line="+32"/>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+90"/>
        <source>Name</source>
        <translation>名稱</translation>
    </message>
    <message>
        <location line="+0"/>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+58"/>
        <source>Size</source>
        <translation>大小</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Modified</source>
        <translation>修改時間</translation>
    </message>
    <message>
        <location line="+40"/>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="-89"/>
        <source>Download</source>
        <translation>下載</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Play</source>
        <translation>播放</translation>
    </message>
    <message>
        <location line="+91"/>
        <source>Open this URL in VLC or another player:</source>
        <translation>請在 VLC 或其他播放程式中開啟此 URL：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>This file is named %1 but its contents are %2. The name is wrong — common for files off the ed2k network — so a player that trusts it finds no %3 and sits at 0:00. It is being served as its real type, so it may still play above.</source>
        <translation>此檔案名為 %1，但內容是 %2。檔名有誤（ed2k 網路上的檔案常有此情況），因此信任檔名的播放程式找不到 %3，會停在 0:00。它正以真實類型提供，所以上方仍可能播放。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>This file is named %1 but does not start with the %2 signature every one of them has, and its contents match no media container we recognise. It is very likely a fake or a corrupt download — no player will get anything out of it.</source>
        <translation>此檔案名為 %1，但開頭並沒有此類檔案都具備的 %2 簽章，其內容也不符合任何已知的媒體容器。它很可能是假檔案或已損壞的下載 — 任何播放程式都無法播放。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>The raw URL, if you want to look for yourself:</source>
        <translation>原始 URL，如需自行查看：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Your browser probably cannot decode %1.</source>
        <translation>您的瀏覽器可能無法解碼 %1。</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Copy</source>
        <translation>複製</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Copied</source>
        <translation>已複製</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Download this file</source>
        <translation>下載此檔案</translation>
    </message>
    <message>
        <location line="+62"/>
        <source>Access denied — no password configured. Set a password in Options → Web Interface.</source>
        <translation>拒絕存取 — 未設定密碼。請在「選項 → Web 介面」中設定密碼。</translation>
    </message>
    <message numerus="yes">
        <location line="+11"/>
        <source>Too many failed logins. Try again in %n second(s).</source>
        <translation>
            <numerusform>登入失敗次數過多。請在 %n 秒後再試一次。</numerusform>
        </translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Login failed</source>
        <translation>登入失敗</translation>
    </message>
    <message numerus="yes">
        <location line="+236"/>
        <source>%n download(s) added</source>
        <translation>
            <numerusform>已加入 %n 個下載</numerusform>
        </translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Not sent yet — the search is waiting (%1) and starts by itself.</source>
        <translation>尚未送出 — 搜尋正在等待（%1），會自動開始。</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Nothing was saved: a value is missing or out of range.</source>
        <translation>未儲存任何內容：某個值缺少或超出範圍。</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Saved. The new ports are used after a restart.</source>
        <translation>已儲存。新的連接埠將在重新啟動後生效。</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Saved.</source>
        <translation>已儲存。</translation>
    </message>
    <message>
        <location line="+13"/>
        <location line="+1973"/>
        <source>Web Control Panel</source>
        <translation>Web 控制台</translation>
    </message>
    <message>
        <location line="-1965"/>
        <source>Not connected</source>
        <translation>未連線</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Blocked: network interface not available</source>
        <translation>已封鎖：網路介面無法使用</translation>
    </message>
    <message>
        <location line="+299"/>
        <source>Connected</source>
        <translation>已連線</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+335"/>
        <source>Disconnected</source>
        <translation>已中斷連線</translation>
    </message>
    <message numerus="yes">
        <location line="-282"/>
        <source>&quot;%1&quot;: %n result(s)</source>
        <translation>
            <numerusform>「%1」：%n 個結果</numerusform>
        </translation>
    </message>
    <message>
        <location line="+193"/>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+2"/>
        <source>Downloads</source>
        <translation>下載</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+107"/>
        <source>Uploads</source>
        <translation>上傳</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Active Connections</source>
        <translation>作用中連線</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Time</source>
        <translation>時間</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="-96"/>
        <source>KB/s</source>
        <translation>KB/s</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Connected to: %1 (%2:%3)</source>
        <translation>已連線到：%1 (%2:%3)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Client ID: %1 (%2)</source>
        <translation>用戶端 ID：%1 (%2)</translation>
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
        <translation>使用者：%1 | 檔案：%2</translation>
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
        <translation>連線中...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not connected to any server</source>
        <translation>未連線到任何伺服器</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Running</source>
        <translation>執行中</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Not available</source>
        <translation>無法使用</translation>
    </message>
    <message>
        <location line="+149"/>
        <location line="+1128"/>
        <source>Queued</source>
        <translation>佇列中</translation>
    </message>
    <message>
        <location line="-1127"/>
        <location line="+1126"/>
        <source>Downloading</source>
        <translation>下載中</translation>
    </message>
    <message>
        <location line="-1125"/>
        <source>Paused</source>
        <translation>已暫停</translation>
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
        <translation>失敗</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Verifying</source>
        <translation>驗證中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Repairing</source>
        <translation>修復中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unpacking</source>
        <translation>解壓縮中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Checking</source>
        <translation>檢查中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unknown</source>
        <translation>未知</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>Not checked.</source>
        <translation>未檢查。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1% of this release looks obtainable.</source>
        <translation>此發佈內容約 %1% 看起來可取得。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1% by the NZB&apos;s own article counts. No server was asked.</source>
        <translation>依 NZB 本身的文章數計算為 %1%。未詢問任何伺服器。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>The PAR2 recovery volumes should cover the shortfall.</source>
        <translation>PAR2 復原卷應足以補足缺少的部分。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>%1
The password for this release did not work. Right-click to set a different one.</source>
        <translation>%1
此發佈內容的密碼無效。按右鍵可設定其他密碼。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1
This release is password-protected. Right-click to set its password.</source>
        <translation>%1
此發佈內容受密碼保護。按右鍵可設定其密碼。</translation>
    </message>
    <message numerus="yes">
        <location line="+4"/>
        <source>%1
%n article(s) could not be found on any server</source>
        <translation>
            <numerusform>%1
有 %n 篇文章在所有伺服器上都找不到</numerusform>
        </translation>
    </message>
    <message>
        <location line="+4"/>
        <source>%1
A password is set for this release.</source>
        <translation>%1
已為此發佈內容設定密碼。</translation>
    </message>
    <message>
        <location line="+126"/>
        <source>%1 (and %2 other(s))</source>
        <translation>%1（及其他 %2 個）</translation>
    </message>
    <message>
        <location line="+55"/>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+99"/>
        <source>Usenet engine unavailable</source>
        <translation>Usenet 引擎無法使用</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Usenet item not found</source>
        <translation>找不到 Usenet 項目</translation>
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
        <translation>只能暫停已排入佇列或下載中的發佈內容</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Only a paused or failed release can be resumed</source>
        <translation>只能繼續已暫停或失敗的發佈內容</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+110"/>
        <location line="+174"/>
        <source>priority must be a number from -2 to 2</source>
        <translation>優先權必須是 -2 到 2 之間的數字</translation>
    </message>
    <message>
        <location line="-278"/>
        <location line="+110"/>
        <location line="+71"/>
        <location line="+90"/>
        <source>Unknown category</source>
        <translation>未知的類別</translation>
    </message>
    <message>
        <location line="-259"/>
        <location line="+4"/>
        <source>files must be a list of file numbers</source>
        <translation>files 必須是檔案編號的清單</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Unknown action</source>
        <translation>未知的動作</translation>
    </message>
    <message>
        <location line="+228"/>
        <source>Post the .nzb as the request body, or give a url</source>
        <translation>請將 .nzb 作為要求本文傳送，或提供 URL</translation>
    </message>
    <message>
        <location line="+57"/>
        <source>Guests cannot change downloads</source>
        <translation>訪客無法變更下載</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Nothing selected</source>
        <translation>未選取任何項目</translation>
    </message>
    <message>
        <location line="+49"/>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="-147"/>
        <source>All</source>
        <translation>全部</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>No category</source>
        <translation>無類別</translation>
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
        <translation>沒有 Usenet 下載。使用「加入 NZB…」將其排入佇列。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>%1 download(s), %2 active — %3% complete</source>
        <translation>%1 個下載，%2 個進行中 — 已完成 %3%</translation>
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
        <translation> — eD2K 下載時限速 %1 KB/s</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Download limit %1 KB/s: Usenet up to %2 KB/s, eD2K up to %3 KB/s.
Whichever network is idle lends its share to the other.</source>
        <translation>下載限制 %1 KB/s：Usenet 最高 %2 KB/s，eD2K 最高 %3 KB/s。
閒置的網路會把自己的份額讓給另一方。</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>No Usenet downloads here.</source>
        <translation>此處沒有 Usenet 下載。</translation>
    </message>
    <message numerus="yes">
        <location line="+48"/>
        <source>%n article(s) were never listed in the NZB</source>
        <translation>
            <numerusform>有 %n 篇文章從未列在 NZB 中</numerusform>
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
        <translation>應用程式語言（%1）</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="-9"/>
        <source>(still scanning)</source>
        <translation>（仍在掃描）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Accepted</source>
        <translation>已接受</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add</source>
        <translation>加入</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add NZB from URL…</source>
        <translation>從 URL 加入 NZB…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add NZB…</source>
        <translation>加入 NZB…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add paused</source>
        <translation>以暫停狀態加入</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add to Static</source>
        <translation>加入到靜態清單</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Adding %1 NZB(s)…</source>
        <translation>正在加入 %1 個 NZB…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Address</source>
        <translation>位址</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Any</source>
        <translation>任何</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Apply</source>
        <translation>套用</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Archive (.zip .rar ...)</source>
        <translation>壓縮檔 (.zip .rar ...)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Archive password for &quot;%1&quot;:</source>
        <translation>「%1」的壓縮檔密碼：</translation>
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
        <translation>音訊 (.mp3 .ogg ...)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Back to the queue</source>
        <translation>回到佇列</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>CD-Image (.iso .bin ...)</source>
        <translation>CD 映像檔 (.iso .bin ...)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Cancel this download?</source>
        <translation>要取消此下載嗎？</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Category</source>
        <translation>分類</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Category:</source>
        <translation>類別：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Category: %1</source>
        <translation>類別：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Check Availability</source>
        <translation>檢查可用性</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Choose .nzb files or paste links first.</source>
        <translation>請先選擇 .nzb 檔案或貼上連結。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Clear Completed</source>
        <translation>清除已完成</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Client</source>
        <translation>客戶端</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Collection</source>
        <translation>收藏集</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Comment</source>
        <translation>評論</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Comments</source>
        <translation>評論</translation>
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
        <translation>連線</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Copy ED2K Link</source>
        <translation>複製 ED2K 連結</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Could not apply that to the category: %1</source>
        <translation>無法套用到該類別：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Could not reach the eMule core.</source>
        <translation>無法連線到 eMule 核心。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Date:</source>
        <translation>日期：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Debug</source>
        <translation>偵錯</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Debug Log</source>
        <translation>偵錯記錄</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Description</source>
        <translation>描述</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Details…</source>
        <translation>詳細資料…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Disconnect</source>
        <translation>中斷連線</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Document (.doc .pdf ...)</source>
        <translation>文件 (.doc .pdf ...)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Download Speed</source>
        <translation>下載速度</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>File</source>
        <translation>檔案</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>File Name</source>
        <translation>檔案名稱</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Files</source>
        <translation>檔案</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Files:</source>
        <translation>檔案數：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>General</source>
        <translation>一般</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Global</source>
        <translation>全域</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Graphs</source>
        <translation>圖表</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hash</source>
        <translation>切細值</translation>
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
        <location line="+2"/>
        <source>Kad</source>
        <translation>Kad</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Kad Network</source>
        <translation>Kad 網路</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Kademlia</source>
        <translation>Kademlia</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Keep Scanning</source>
        <translation>繼續掃描</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Language</source>
        <translation>語言</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Log</source>
        <translation>記錄</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Login</source>
        <translation>登入</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Logout</source>
        <translation>登出</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Max Download (KB/s)</source>
        <translation>最大下載 (KB/s)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Max Download Speed</source>
        <translation>最大下載速度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Max Upload (KB/s)</source>
        <translation>最大上傳 (KB/s)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Max Upload Speed</source>
        <translation>最大上傳速度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Method</source>
        <translation>方式</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Missing</source>
        <translation>缺少</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>My Info</source>
        <translation>我的資訊</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>NZB URLs:</source>
        <translation>NZB 連結：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>NZB files:</source>
        <translation>NZB 檔案：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Newsgroups:</source>
        <translation>新聞群組：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nickname</source>
        <translation>暱稱</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nickname:</source>
        <translation>暱稱：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nothing has completed yet for &quot;%1&quot;.</source>
        <translation>「%1」尚無已完成的內容。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nothing here can be previewed yet.</source>
        <translation>這裡還沒有可預覽的內容。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Open File</source>
        <translation>開啟檔案</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Open Folder</source>
        <translation>開啟資料夾</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Password</source>
        <translation>密碼</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Password:</source>
        <translation>密碼：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paste one or more http(s) links to .nzb files here, one per line...</source>
        <translation>在此貼上一個或多個指向 .nzb 檔案的 http(s) 連結，每行一個...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Pause</source>
        <translation>暫停</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Pause All</source>
        <translation>全部暫停</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Picture (.jpg .png ...)</source>
        <translation>圖片 (.jpg .png ...)</translation>
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
        <translation>每次最多加入 %1 個連結。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Port</source>
        <translation>連接埠</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ports</source>
        <translation>連接埠</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Poster:</source>
        <translation>發文者：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Preferences</source>
        <translation>偏好設定</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Preview</source>
        <translation>預覽</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Preview File</source>
        <translation>預覽檔案</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority</source>
        <translation>優先權</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority:</source>
        <translation>優先權：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority: %1</source>
        <translation>優先權：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority: Auto</source>
        <translation>優先權：自動</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority: High</source>
        <translation>優先權：高</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority: Low</source>
        <translation>優先權：低</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority: Normal</source>
        <translation>優先權：普通</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Program (.exe ...)</source>
        <translation>程式 (.exe ...)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Progress</source>
        <translation>進度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Queued %1 NZB(s).</source>
        <translation>已將 %1 個 NZB 排入佇列。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Rating</source>
        <translation>評分</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Recheck Firewall</source>
        <translation>重新檢查防火牆</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Reconnects</source>
        <translation>重新連線</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Refresh</source>
        <translation>重新整理</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remaining</source>
        <translation>剩餘</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove</source>
        <translation>移除</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove %1 download(s) and delete the files already fetched?</source>
        <translation>要移除 %1 個下載並刪除已取得的檔案嗎？</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove and Delete Files</source>
        <translation>移除並刪除檔案</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove every Usenet download in &quot;%1&quot; and delete its files?</source>
        <translation>要移除「%1」中的所有 Usenet 下載並刪除其檔案嗎？</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove from Static</source>
        <translation>從靜態清單移除</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove the stored password for this download?</source>
        <translation>要移除此下載已儲存的密碼嗎？</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Requests</source>
        <translation>要求</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Resume</source>
        <translation>繼續</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Resume All</source>
        <translation>全部繼續</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Search</source>
        <translation>搜尋</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server</source>
        <translation>伺服器</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server Info</source>
        <translation>伺服器資訊</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server List</source>
        <translation>伺服器清單</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server Name</source>
        <translation>伺服器名稱</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Session Received</source>
        <translation>工作階段接收</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Session Sent</source>
        <translation>工作階段傳送</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Session Statistics</source>
        <translation>工作階段統計</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Set Password…</source>
        <translation>設定密碼…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Shared</source>
        <translation>已分享</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Shared Files</source>
        <translation>已分享檔案</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Sources</source>
        <translation>來源</translation>
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
        <translation>統計</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status</source>
        <translation>狀態</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status:</source>
        <translation>狀態：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Stop</source>
        <translation>停止</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>TCP Port</source>
        <translation>TCP 連接埠</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>That did not work.</source>
        <translation>操作未成功。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>These could not be added: %1</source>
        <translation>以下項目無法加入：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>This archive cannot be previewed.</source>
        <translation>無法預覽此壓縮檔。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>This download is no longer in the queue.</source>
        <translation>此下載已不在佇列中。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total</source>
        <translation>總計</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Size:</source>
        <translation>總大小：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Transfer</source>
        <translation>傳輸</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Transferred</source>
        <translation>已傳輸</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type</source>
        <translation>類型</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>UDP Port</source>
        <translation>UDP 連接埠</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload Speed</source>
        <translation>上傳速度</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Uptime</source>
        <translation>運作時間</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Usenet</source>
        <translation>Usenet</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>User</source>
        <translation>使用者</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>User Information</source>
        <translation>使用者資訊</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>User Name</source>
        <translation>使用者名稱</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Users</source>
        <translation>使用者</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Video (.avi .mkv ...)</source>
        <translation>視訊 (.avi .mkv ...)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>You have already downloaded %1 of these. Download them again?</source>
        <translation>其中 %1 個已下載過。要重新下載嗎？</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>this download</source>
        <translation>此下載</translation>
    </message>
</context>
</TS>
