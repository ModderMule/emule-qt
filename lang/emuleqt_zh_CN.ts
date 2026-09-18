<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_CN">
<context>
    <name>ContainerSniffer</name>
    <message>
        <location filename="../src/core/media/ContainerSniffer.cpp" line="+164"/>
        <source>Named .%1 but matches no media container we recognise — very likely a fake.</source>
        <translation>文件名为 .%1，但与我们能识别的任何媒体容器都不匹配 — 极可能是假文件。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Named .%1 but the contents are %2.</source>
        <translation>文件名为 .%1，但内容实际是 %2。</translation>
    </message>
</context>
<context>
    <name>Ed2kLinkImporter</name>
    <message>
        <location filename="../src/gui/utils/Ed2kLinkImporter.cpp" line="+187"/>
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
        <location line="+85"/>
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
        <location filename="../src/gui/utils/IpcFeedback.cpp" line="+24"/>
        <source>The request was rejected by eMule.</source>
        <translation>请求被 eMule 拒绝。</translation>
    </message>
</context>
<context>
    <name>PreviewLauncher</name>
    <message>
        <location filename="../src/gui/utils/PreviewLauncher.cpp" line="+195"/>
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
        <location filename="../src/gui/utils/PriorityText.cpp" line="+16"/>
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
        <location line="+2"/>
        <source>Release</source>
        <translation>发布</translation>
    </message>
    <message>
        <location line="+2"/>
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
        <location filename="../src/gui/utils/Ed2kLinkImporter.cpp" line="-288"/>
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
        <location filename="../src/gui/app/main.cpp" line="+556"/>
        <source>Download Added</source>
        <translation>下载已添加</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>A new download has been added.</source>
        <translation>已添加新的下载。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Chat Message from %1</source>
        <translation>来自 %1 的聊天消息</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Log Entry</source>
        <translation>日志条目</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Connection Lost</source>
        <translation>连接丢失</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server connection has been lost.</source>
        <translation>服务器连接已丢失。</translation>
    </message>
    <message>
        <source>Very Low</source>
        <translation type="vanished">非常低</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/UsenetQueueModel.cpp" line="+200"/>
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
        <location line="+408"/>
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
        <location filename="../src/gui/utils/RatingIcons.cpp" line="+57"/>
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
        <location line="+101"/>
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
        <location filename="../src/gui/controls/DownloadListModel.cpp" line="+72"/>
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
        <location line="+226"/>
        <source>Yes</source>
        <translation>是</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/DownloadListModel.cpp" line="-34"/>
        <source>Never</source>
        <translation>从不</translation>
    </message>
    <message>
        <location line="+7"/>
        <location filename="../src/gui/controls/SharedFilesModel.cpp" line="+19"/>
        <source>Archive</source>
        <translation>压缩包</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/gui/controls/SharedFilesModel.cpp" line="+1"/>
        <source>Audio</source>
        <translation>音频</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/gui/controls/SharedFilesModel.cpp" line="+1"/>
        <source>Video</source>
        <translation>视频</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/gui/controls/SharedFilesModel.cpp" line="+1"/>
        <source>Image</source>
        <translation>图片</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/gui/controls/SharedFilesModel.cpp" line="+1"/>
        <source>Program</source>
        <translation>程序</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/gui/controls/SharedFilesModel.cpp" line="+1"/>
        <source>Document</source>
        <translation>文档</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/gui/controls/SharedFilesModel.cpp" line="+1"/>
        <source>CD-Image</source>
        <translation>CD 镜像</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/gui/controls/SharedFilesModel.cpp" line="+1"/>
        <source>eMule Collection</source>
        <translation>eMule 合集</translation>
    </message>
    <message>
        <location line="+14"/>
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
        <location filename="../src/gui/controls/ClientListModel.cpp" line="-227"/>
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
        <location filename="../src/gui/controls/KnownTypeStyle.h" line="+22"/>
        <source>Shared</source>
        <translation>已共享</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/gui/controls/UsenetQueueModel.cpp" line="-411"/>
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
        <location filename="../src/gui/panels/StatisticsPanel.cpp" line="+282"/>
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
        <location filename="../src/gui/dialogs/OptionsDialog.cpp" line="+2777"/>
        <source>Test</source>
        <translation>测试</translation>
    </message>
    <message>
        <location filename="../src/gui/utils/FileAssociation.cpp" line="+155"/>
        <source>Could not write the file association to the registry.</source>
        <translation>无法将文件关联写入注册表。</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Could not remove the file association from the registry.</source>
        <translation>无法从注册表中移除文件关联。</translation>
    </message>
    <message>
        <location line="+15"/>
        <location line="+29"/>
        <source>No writable data directory.</source>
        <translation>没有可写的数据目录。</translation>
    </message>
    <message>
        <location line="-23"/>
        <source>Could not create %1.</source>
        <translation>无法创建 %1。</translation>
    </message>
    <message>
        <location filename="../src/gui/utils/NzbAdd.cpp" line="+25"/>
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
</context>
<context>
    <name>Rating</name>
    <message>
        <location filename="../src/core/utils/OtherFunctions.cpp" line="+405"/>
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
    <name>Units</name>
    <message>
        <location filename="../src/core/utils/StringUtils.cpp" line="+72"/>
        <location filename="../src/core/webserver/WebServer.cpp" line="+4110"/>
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
    <name>eMule::AddFriendDialog</name>
    <message>
        <location filename="../src/gui/dialogs/AddFriendDialog.cpp" line="+23"/>
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
        <location line="+5"/>
        <source>Port:</source>
        <translation>端口：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Additional Information</source>
        <translation>附加信息</translation>
    </message>
    <message>
        <location line="+4"/>
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
        <location line="+41"/>
        <location line="+6"/>
        <source>Add Friend</source>
        <translation>添加好友</translation>
    </message>
    <message>
        <location line="-5"/>
        <source>Please enter an IP address.</source>
        <translation>请输入 IP 地址。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Please enter a valid port (1-65535).</source>
        <translation>请输入有效端口（1-65535）。</translation>
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
        <location filename="../src/gui/dialogs/AddNzbUrlDialog.cpp" line="+16"/>
        <location line="+27"/>
        <location line="+31"/>
        <location line="+71"/>
        <source>Add NZB from URL</source>
        <translation>从 URL 添加 NZB</translation>
    </message>
    <message>
        <location line="-127"/>
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
        <location line="+39"/>
        <source>You have already downloaded %n of these. Download them again?

%1</source>
        <translation>
            <numerusform>其中 %n 个您已下载过。要重新下载吗？

%1</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>eMule::ArchivePreviewPanel</name>
    <message>
        <location filename="../src/gui/dialogs/ArchivePreviewPanel.cpp" line="+68"/>
        <source>Scanning...</source>
        <translation>正在扫描...</translation>
    </message>
    <message>
        <location line="+92"/>
        <location line="+37"/>
        <location line="+18"/>
        <source>Archive type: --</source>
        <translation>压缩包类型：--</translation>
    </message>
    <message>
        <location line="-54"/>
        <location line="+17"/>
        <location line="+41"/>
        <source>Ready</source>
        <translation>就绪</translation>
    </message>
    <message>
        <location line="-25"/>
        <source>Archive type: %1</source>
        <translation>压缩包类型：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>No entries found or unsupported format</source>
        <translation>未找到条目或格式不支持</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Files: %1</source>
        <translation>文件：%1</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Create Preview Copy</source>
        <translation>创建预览副本</translation>
    </message>
    <message>
        <location line="+4"/>
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
        <location line="+0"/>
        <source>Comment</source>
        <translation>评论</translation>
    </message>
</context>
<context>
    <name>eMule::BugReportDialog</name>
    <message>
        <location filename="../src/gui/dialogs/BugReportDialog.cpp" line="+54"/>
        <location line="+133"/>
        <location line="+127"/>
        <source>Submit Bug Report</source>
        <translation>提交错误报告</translation>
    </message>
    <message>
        <location line="-255"/>
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
        <location line="+5"/>
        <source>Attachments</source>
        <translation>附件</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Screenshots:</source>
        <translation>屏幕截图：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Add...</source>
        <translation>添加...</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+13"/>
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
        <location line="+122"/>
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
        <location filename="../src/gui/dialogs/CategoryDialog.cpp" line="+34"/>
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
        <location line="+71"/>
        <source>Choose a folder for incoming files</source>
        <translation>选择接收文件的文件夹</translation>
    </message>
    <message>
        <location line="-67"/>
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
        <location line="+6"/>
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
        <source>Stored for compatibility — the per-category view filter is not implemented yet.</source>
        <translation>仅为兼容性保存 — 按分类的视图过滤器尚未实现。</translation>
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
        <location line="+34"/>
        <source>Default</source>
        <translation>默认</translation>
    </message>
</context>
<context>
    <name>eMule::CategoryTabBar</name>
    <message>
        <location filename="../src/gui/controls/CategoryTabBar.cpp" line="+27"/>
        <location line="+47"/>
        <location line="+59"/>
        <source>All</source>
        <translation>全部</translation>
    </message>
    <message>
        <location line="-52"/>
        <source>Cat %1</source>
        <translation>分类 %1</translation>
    </message>
    <message>
        <location line="+74"/>
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
        <location line="+138"/>
        <source>Remove Category</source>
        <translation>移除分类</translation>
    </message>
    <message>
        <location line="-44"/>
        <source>Could not save categories: %1</source>
        <translation>无法保存分类：%1</translation>
    </message>
    <message>
        <location line="+45"/>
        <source>Remove the category &quot;%1&quot;?

Its downloads keep their files and move to All.</source>
        <translation>移除分类“%1”？

其下载会保留文件并移至“全部”。</translation>
    </message>
</context>
<context>
    <name>eMule::ClientDetailDialog</name>
    <message>
        <location filename="../src/gui/dialogs/ClientDetailDialog.cpp" line="+62"/>
        <source>Client Details: %1</source>
        <translation>客户端详情：%1</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>General</source>
        <translation>常规</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>User Name</source>
        <translation>用户名</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>User Hash</source>
        <translation>用户哈希</translation>
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
        <translation>客户端软件</translation>
    </message>
    <message>
        <location line="+15"/>
        <location line="+4"/>
        <source>Server</source>
        <translation>服务器</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Identification</source>
        <translation>识别</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Obfuscation</source>
        <translation>混淆</translation>
    </message>
    <message>
        <location line="+3"/>
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
        <source>Not connected</source>
        <translation>未连接</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Transfer</source>
        <translation>传输</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Currently Downloading</source>
        <translation>当前正在下载</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Currently Uploading</source>
        <translation>当前正在上传</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Downloaded (Session)</source>
        <translation>已下载（会话）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Uploaded (Session)</source>
        <translation>已上传（会话）</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+2"/>
        <source>Download Rate</source>
        <translation>下载速率</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Downloaded (Total)</source>
        <translation>已下载（总计）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Uploaded (Total)</source>
        <translation>已上传（总计）</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Scores</source>
        <translation>评分</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>DL/UP Modifier</source>
        <translation>下载/上传修正系数</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Rating (Total)</source>
        <translation>评分（总计）</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Upload Queue Score</source>
        <translation>上传队列评分</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Friend Slot</source>
        <translation>好友位</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Yes</source>
        <translation>是</translation>
    </message>
</context>
<context>
    <name>eMule::ClientListModel</name>
    <message>
        <location filename="../src/gui/controls/ClientListModel.cpp" line="+304"/>
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
        <location line="+40"/>
        <source>Transferred</source>
        <translation>已传输</translation>
    </message>
    <message>
        <location line="-39"/>
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
        <source>Transferred Up</source>
        <translation>已上传</translation>
    </message>
    <message>
        <location line="+1"/>
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
        <location line="-26"/>
        <location line="+27"/>
        <source>Transferred Down</source>
        <translation>已下载</translation>
    </message>
    <message>
        <location line="-176"/>
        <location line="+36"/>
        <source>Low ID</source>
        <translation>Low ID</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Yes</source>
        <translation>是</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>No</source>
        <translation>否</translation>
    </message>
    <message>
        <location line="+138"/>
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
        <location filename="../src/gui/dialogs/ClientSharedFilesDialog.cpp" line="+33"/>
        <source>Shared Files — %1</source>
        <translation>共享文件 — %1</translation>
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
        <location line="+43"/>
        <source>Download Selected</source>
        <translation>下载所选</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Close</source>
        <translation>关闭</translation>
    </message>
</context>
<context>
    <name>eMule::CollectionCreateDialog</name>
    <message>
        <location filename="../src/gui/dialogs/CollectionCreateDialog.cpp" line="+54"/>
        <source>Modify Collection...</source>
        <translation>修改合集...</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Create Collection...</source>
        <translation>创建合集...</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Shared (0)</source>
        <translation>已共享 (0)</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+36"/>
        <source>File Name</source>
        <translation>文件名</translation>
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
        <location line="+91"/>
        <source>Shared (%1)</source>
        <translation>已共享 (%1)</translation>
    </message>
    <message>
        <location line="+1"/>
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
        <location filename="../src/gui/dialogs/CollectionViewDialog.cpp" line="+41"/>
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
        <location line="+27"/>
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
        <location line="+2"/>
        <source>Add to new category</source>
        <translation>添加到新分类</translation>
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
        <location filename="../src/gui/dialogs/CommentsPanel.cpp" line="+60"/>
        <source>Kad</source>
        <translation>Kad</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>(Kad search in progress...)</source>
        <translation>（Kad 搜索中...）</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+50"/>
        <source>Search Kad</source>
        <translation>搜索 Kad</translation>
    </message>
    <message>
        <location line="-32"/>
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
        <location line="+6"/>
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
    <name>eMule::ContactsGraph</name>
    <message>
        <location filename="../src/gui/controls/ContactsGraph.cpp" line="+87"/>
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
        <source>Search Kad</source>
        <translation>搜索 Kad</translation>
    </message>
    <message>
        <location line="+95"/>
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
        <location filename="../src/gui/controls/DownloadListModel.cpp" line="+616"/>
        <source>Downloading</source>
        <translation>正在下载</translation>
    </message>
    <message>
        <location line="-362"/>
        <source>Auto [%1]</source>
        <translation>自动 [%1]</translation>
    </message>
    <message>
        <location line="-81"/>
        <source>Queue Full</source>
        <translation>队列已满</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Available parts: %1 / %2</source>
        <translation>可用部分：%1 / %2</translation>
    </message>
    <message>
        <location line="+81"/>
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
已传输数据：	%12</translation>
    </message>
    <message>
        <location line="+68"/>
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
        <location line="+218"/>
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
</context>
<context>
    <name>eMule::FileDetailDialog</name>
    <message>
        <location filename="../src/gui/dialogs/FileDetailDialog.cpp" line="+101"/>
        <source>File Details: %1</source>
        <translation>文件详情：%1</translation>
    </message>
    <message>
        <location line="+11"/>
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
        <location line="-6"/>
        <location line="+7"/>
        <source>Comments</source>
        <translation>评论</translation>
    </message>
    <message>
        <location line="-6"/>
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
        <location line="-6"/>
        <location line="+7"/>
        <source>ED2K Link</source>
        <translation>ED2K 链接</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+2"/>
        <source>Archive Preview</source>
        <translation>压缩包预览</translation>
    </message>
    <message>
        <location line="+24"/>
        <location line="+47"/>
        <source>File Name</source>
        <translation>文件名</translation>
    </message>
    <message>
        <location line="-46"/>
        <source>Hash (MD4)</source>
        <translation>哈希 (MD4)</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>AICH Hash</source>
        <translation>AICH 哈希</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>File Size</source>
        <translation>文件大小</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>%1 (%2 Bytes)</source>
        <translation>%1 (%2 Bytes)</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Completed</source>
        <translation>已完成</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Status</source>
        <translation>状态</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority</source>
        <translation>优先级</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+24"/>
        <source>Sources</source>
        <translation>来源</translation>
    </message>
    <message>
        <location line="-19"/>
        <source>File Path</source>
        <translation>文件路径</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Created</source>
        <translation>创建时间</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Last Seen Complete</source>
        <translation>最后一次见到完整文件</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Last Reception</source>
        <translation>最后接收</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>No alternative file names reported by sources. Use “Search Kad” to look them up.</source>
        <translation>来源未报告备用文件名。使用“搜索 Kad”进行查找。</translation>
    </message>
    <message>
        <location line="+12"/>
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
        <location line="+21"/>
        <source>Copy to Clipboard</source>
        <translation>复制到剪贴板</translation>
    </message>
</context>
<context>
    <name>eMule::FindInListDialog</name>
    <message>
        <location filename="../src/gui/dialogs/FindInListDialog.cpp" line="+23"/>
        <source>Search</source>
        <translation>搜索</translation>
    </message>
    <message>
        <location line="+8"/>
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
        <location filename="../src/gui/dialogs/FirstStartWizard.cpp" line="+29"/>
        <source>eMule First Runtime Wizard</source>
        <translation>eMule 首次运行向导</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>Ports and Connection</source>
        <translation>端口和连接</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Connection</source>
        <translation>连接</translation>
    </message>
    <message>
        <location line="+40"/>
        <source>eMule uses two ports for communication with servers and clients. These ports must be free and available for remote clients. The TCP port must be available to ensure the main functionality of eMule. The UDP port is used for Kad (serverless network) and to reduce network usage (Overhead).</source>
        <translation>eMule 使用两个端口与服务器和客户端通信。这些端口必须空闲并对远程客户端可用。TCP 端口必须可用以确保 eMule 的主要功能。UDP 端口用于 Kad（无服务器网络）和减少网络使用（开销）。</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>You can change the ports here while no network activities have started.</source>
        <translation>您可以在尚未开始网络活动时在此更改端口。</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>TCP:</source>
        <translation>TCP：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>UDP:</source>
        <translation>UDP：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Use UPnP to Setup Ports</source>
        <translation>使用 UPnP 设置端口</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Choose which Network(s) you want to use</source>
        <translation>选择要使用的网络</translation>
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
        <translation>&lt; 上一步</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Finish</source>
        <translation>完成</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Help</source>
        <translation>帮助</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>Network</source>
        <translation>网络</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>You must enable at least one network (Kad or eD2K).</source>
        <translation>您必须启用至少一个网络（Kad 或 eD2K）。</translation>
    </message>
    <message>
        <location line="+62"/>
        <source>UPnP</source>
        <translation>UPnP</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>UPnP port mapping timed out. Your router may not support UPnP, or it may be disabled. You can set up port forwarding manually.</source>
        <translation>UPnP 端口映射超时。您的路由器可能不支持 UPnP，或者已被禁用。您可以手动设置端口转发。</translation>
    </message>
</context>
<context>
    <name>eMule::ImportDownloadsDialog</name>
    <message>
        <location filename="../src/gui/dialogs/ImportDownloadsDialog.cpp" line="+39"/>
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
        <location line="+217"/>
        <source>Idle</source>
        <translation>空闲</translation>
    </message>
    <message>
        <location line="-206"/>
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
        <source>Import Downloads</source>
        <translation>导入下载</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Import Downloads is only available for local connections.</source>
        <translation>导入下载仅适用于本地连接。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Select folder to scan for importable downloads</source>
        <translation>选择要扫描可导入下载的文件夹</translation>
    </message>
    <message>
        <location line="+119"/>
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
        <location filename="../src/gui/controls/IndexerResultsModel.cpp" line="+25"/>
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
        <location line="+102"/>
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
        <location line="+27"/>
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
</context>
<context>
    <name>eMule::IrcPanel</name>
    <message>
        <location filename="../src/gui/panels/IrcPanel.cpp" line="+131"/>
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
        <location line="+84"/>
        <source>Disconnect</source>
        <translation>断开连接</translation>
    </message>
    <message>
        <location line="+27"/>
        <location line="+397"/>
        <source>Connect</source>
        <translation>连接</translation>
    </message>
    <message>
        <location line="-103"/>
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
        <location line="+33"/>
        <location line="+537"/>
        <source>Nick</source>
        <translation>昵称</translation>
    </message>
    <message>
        <location line="-478"/>
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
        <location line="+79"/>
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
        <location line="+18"/>
        <source>Channels</source>
        <translation>频道</translation>
    </message>
    <message>
        <location line="+261"/>
        <source>Nick (%1)</source>
        <translation>昵称 (%1)</translation>
    </message>
</context>
<context>
    <name>eMule::KadContactHistogram</name>
    <message>
        <location filename="../src/gui/controls/KadContactHistogram.cpp" line="+193"/>
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
        <location filename="../src/gui/controls/KadContactsModel.cpp" line="+67"/>
        <source>Status</source>
        <translation>状态</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Client ID</source>
        <translation>客户端 ID</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Distance</source>
        <translation>距离</translation>
    </message>
</context>
<context>
    <name>eMule::KadLookupGraph</name>
    <message>
        <location filename="../src/gui/controls/KadLookupGraph.cpp" line="+69"/>
        <source>No search selected</source>
        <translation>未选择搜索</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Distance</source>
        <translation>距离</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Time</source>
        <translation>时间</translation>
    </message>
    <message>
        <location line="+235"/>
        <source>Our node (search initiator)</source>
        <translation>我们的节点（搜索发起者）</translation>
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
        <translation>▸ 联系人 (0)</translation>
    </message>
    <message>
        <location line="-431"/>
        <location line="+12"/>
        <location line="+369"/>
        <location line="+94"/>
        <source>▸ Current Searches (0)</source>
        <translation>▸ 当前搜索 (0)</translation>
    </message>
    <message>
        <location line="-268"/>
        <location line="+332"/>
        <source>▸ Search Details</source>
        <translation>▸ 搜索详情</translation>
    </message>
    <message>
        <location line="-252"/>
        <source>Recheck Firewall</source>
        <translation>重新检查防火墙</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+239"/>
        <source>Connect</source>
        <translation>连接</translation>
    </message>
    <message>
        <location line="-447"/>
        <location line="+223"/>
        <location line="+28"/>
        <source>Bootstrap</source>
        <translation>引导</translation>
    </message>
    <message>
        <location line="-262"/>
        <source>Downloading...</source>
        <translation>正在下载...</translation>
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
        <translation>下载nodes.dat失败: %1</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Downloaded nodes.dat is empty.</source>
        <translation>下载的nodes.dat为空。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Failed to save nodes.dat: %1</source>
        <translation>保存nodes.dat失败: %1</translation>
    </message>
    <message>
        <location line="+210"/>
        <source>IP Address:</source>
        <translation>IP 地址：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Port:</source>
        <translation>端口：</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Nodes.dat from URL:</source>
        <translation>从 URL 获取 Nodes.dat：</translation>
    </message>
    <message>
        <location line="+137"/>
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
        <location line="+24"/>
        <source>Disconnect</source>
        <translation>断开连接</translation>
    </message>
    <message>
        <location line="+64"/>
        <source>▸ Search Details (%1)</source>
        <translation>▸ 搜索详情 (%1)</translation>
    </message>
</context>
<context>
    <name>eMule::KadSearchesModel</name>
    <message>
        <location filename="../src/gui/controls/KadSearchesModel.cpp" line="+78"/>
        <source>No.</source>
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
</context>
<context>
    <name>eMule::LogWidget</name>
    <message>
        <location filename="../src/gui/controls/LogWidget.cpp" line="+66"/>
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
        <translation>点击此处检查是否有新版本</translation>
    </message>
</context>
<context>
    <name>eMule::MainWindow</name>
    <message>
        <location filename="../src/gui/app/MainWindow.cpp" line="+80"/>
        <source>eMule Qt v%1</source>
        <translation>eMule Qt v%1</translation>
    </message>
    <message>
        <location line="+97"/>
        <location line="+9"/>
        <source>New Version Available</source>
        <translation>有新版本可用</translation>
    </message>
    <message>
        <location line="+169"/>
        <source>eD2K: Connected (LowID)</source>
        <translation>eD2K：已连接 (LowID)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>eD2K: Connected</source>
        <translation>eD2K：已连接</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>eD2K: Connecting...</source>
        <translation>eD2K：连接中...</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+1050"/>
        <source>eD2K: Disconnected</source>
        <translation>eD2K：已断开</translation>
    </message>
    <message>
        <location line="-1036"/>
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
        <location line="+1028"/>
        <source>Kad: Disconnected</source>
        <translation>Kad：已断开</translation>
    </message>
    <message>
        <location line="-1019"/>
        <source>Users: %1 | Files: %2</source>
        <translation>用户：%1 | 文件：%2</translation>
    </message>
    <message>
        <location line="+269"/>
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
        <location line="+26"/>
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
        <source>FAQ</source>
        <translation>常见问题</translation>
    </message>
    <message>
        <location line="-533"/>
        <location line="+7"/>
        <location line="+529"/>
        <source>Version Check</source>
        <translation>版本检查</translation>
    </message>
    <message>
        <location line="-591"/>
        <source>Quit eMule Qt</source>
        <translation>退出 eMule Qt</translation>
    </message>
    <message>
        <location line="+35"/>
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
        <location line="+63"/>
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
        <location line="+190"/>
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
        <location line="+72"/>
        <source>Confirm Exit</source>
        <translation>确认退出</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Are you sure you want to exit eMule?</source>
        <translation>确定要退出 eMule 吗？</translation>
    </message>
    <message>
        <location line="+147"/>
        <source>Open Downloads Folder in Browser</source>
        <translation>在浏览器中打开下载文件夹</translation>
    </message>
    <message>
        <location line="+34"/>
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
        <location line="+141"/>
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
        <location line="+61"/>
        <source>Default</source>
        <translation>默认</translation>
    </message>
    <message>
        <location line="-25"/>
        <source>Skin Profiles</source>
        <translation>皮肤配置</translation>
    </message>
    <message>
        <location line="+2"/>
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
        <location line="+29"/>
        <source>Disconnect</source>
        <translation>断开连接</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Connect</source>
        <translation>连接</translation>
    </message>
    <message>
        <location line="+71"/>
        <source>Ready</source>
        <translation>就绪</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Users: 0 | Files: 0</source>
        <translation>用户：0 | 文件：0</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Up: 0.0</source>
        <translation>上传：0.0</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Down: 0.0</source>
        <translation>下载：0.0</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Double-click for Network Information</source>
        <translation>双击查看网络信息</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>New message — double-click to read</source>
        <translation>新消息 — 双击阅读</translation>
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
        <location filename="../src/gui/panels/MessagesPanel.cpp" line="+139"/>
        <source>Me</source>
        <translation>我</translation>
    </message>
    <message>
        <location line="+76"/>
        <source>Friends (0)</source>
        <translation>好友 (0)</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Info</source>
        <translation>信息</translation>
    </message>
    <message>
        <location line="+15"/>
        <location line="+291"/>
        <source>Name:</source>
        <translation>名称：</translation>
    </message>
    <message>
        <location line="-290"/>
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
        <location line="+34"/>
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
        <source>Close</source>
        <translation>关闭</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Add...</source>
        <translation>添加...</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Remove</source>
        <translation>删除</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Send Message</source>
        <translation>发送消息</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>View Shared Files</source>
        <translation>查看共享文件</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Establish Friend Slot</source>
        <translation>建立好友位</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Find...</source>
        <translation>查找...</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Friends (%1)</source>
        <translation>好友 (%1)</translation>
    </message>
    <message>
        <location line="+108"/>
        <source>Find Friend</source>
        <translation>查找好友</translation>
    </message>
</context>
<context>
    <name>eMule::MetadataPage</name>
    <message>
        <location filename="../src/gui/dialogs/MetadataPage.cpp" line="+104"/>
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
</context>
<context>
    <name>eMule::MiniMuleWidget</name>
    <message>
        <location filename="../src/gui/app/MiniMuleWidget.cpp" line="+74"/>
        <source>Yes</source>
        <translation>是</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>No</source>
        <translation>否</translation>
    </message>
    <message>
        <location line="+100"/>
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
        <location filename="../src/gui/dialogs/NetworkInfoDialog.cpp" line="+53"/>
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
        <location line="+52"/>
        <source>Connected</source>
        <translation>已连接</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+87"/>
        <source>Connecting</source>
        <translation>连接中</translation>
    </message>
    <message>
        <location line="-85"/>
        <location line="+87"/>
        <source>Disconnected</source>
        <translation>已断开</translation>
    </message>
    <message>
        <location line="-72"/>
        <source>Unknown</source>
        <translation>未知</translation>
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
        <translation>已混淆</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Normal</source>
        <translation>普通</translation>
    </message>
    <message>
        <location line="+32"/>
        <location line="+13"/>
        <source>Firewalled</source>
        <translation>防火墙</translation>
    </message>
    <message>
        <location line="-13"/>
        <location line="+15"/>
        <source>Open</source>
        <translation>开放</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>unverified</source>
        <translation>未验证</translation>
    </message>
    <message>
        <location line="+53"/>
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
        <location filename="../src/gui/dialogs/OptionsDialog.cpp" line="-2690"/>
        <source>Options</source>
        <translation>选项</translation>
    </message>
    <message>
        <location line="+49"/>
        <location line="+1760"/>
        <source>OK</source>
        <translation>确定</translation>
    </message>
    <message>
        <location line="-1759"/>
        <location line="+1760"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="-1759"/>
        <location line="+5301"/>
        <source>Apply</source>
        <translation>应用</translation>
    </message>
    <message>
        <location line="-5300"/>
        <source>Help</source>
        <translation>帮助</translation>
    </message>
    <message>
        <location line="+245"/>
        <location line="+1741"/>
        <location line="+63"/>
        <location line="+5"/>
        <location line="+9"/>
        <location line="+11"/>
        <source>IP Filter</source>
        <translation>IP 过滤器</translation>
    </message>
    <message>
        <location line="-1828"/>
        <source>IP filter reloaded: %1 entries.</source>
        <translation>IP 过滤器已重新加载：%1 条目。</translation>
    </message>
    <message>
        <location line="+84"/>
        <source>Options -&gt; %1 -&gt; %2</source>
        <translation>选项 -&gt; %1 -&gt; %2</translation>
    </message>
    <message>
        <location line="+152"/>
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
        <location line="+74"/>
        <source>User Name</source>
        <translation>用户名</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+5226"/>
        <source>Language</source>
        <translation>语言</translation>
    </message>
    <message>
        <location line="-5223"/>
        <source>System Default</source>
        <translation>系统默认</translation>
    </message>
    <message>
        <location line="+14"/>
        <location line="+599"/>
        <location line="+293"/>
        <location line="+372"/>
        <location line="+276"/>
        <source>Miscellaneous</source>
        <translation>其他</translation>
    </message>
    <message>
        <location line="-1537"/>
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
        <location line="+10"/>
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
        <source>Handle eD2K Links</source>
        <translation>处理 eD2K 链接</translation>
    </message>
    <message>
        <location line="+12"/>
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
        <location line="+5152"/>
        <source>Core</source>
        <translation>核心</translation>
    </message>
    <message>
        <location line="-5147"/>
        <source>Address:</source>
        <translation>地址：</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+1059"/>
        <location line="+633"/>
        <location line="+409"/>
        <source>Port:</source>
        <translation>端口：</translation>
    </message>
    <message>
        <location line="-2098"/>
        <source>authentication token</source>
        <translation>认证令牌</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Token:</source>
        <translation>令牌：</translation>
    </message>
    <message>
        <location line="+2"/>
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
        <location line="+10"/>
        <source>flat</source>
        <translation>扁平</translation>
    </message>
    <message>
        <location line="+6"/>
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
        <location line="+3"/>
        <source>Use original eMule icons</source>
        <translation>使用原版 eMule 图标</translation>
    </message>
    <message>
        <location line="+4"/>
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
        <location line="+5"/>
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
        <location line="+597"/>
        <location line="+928"/>
        <location line="+108"/>
        <location line="+289"/>
        <location line="+1006"/>
        <location line="+152"/>
        <location line="+1036"/>
        <location line="+270"/>
        <location line="+29"/>
        <source>Enabled</source>
        <translation>已启用</translation>
    </message>
    <message>
        <location line="-4413"/>
        <source>Reset</source>
        <translation>重置</translation>
    </message>
    <message>
        <location line="+28"/>
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
        <source> KB/s</source>
        <translation> KB/s</translation>
    </message>
    <message>
        <location line="-3"/>
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
        <location line="+14"/>
        <source>Client Port</source>
        <translation>客户端端口</translation>
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
        <translation>禁用</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Test Ports</source>
        <translation>测试端口</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Use UPnP to Setup Ports</source>
        <translation>使用 UPnP 设置端口</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Port forwarding: unknown</source>
        <translation>端口转发：未知</translation>
    </message>
    <message>
        <location line="+9"/>
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
        <source>Connection Limits</source>
        <translation>连接限制</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Max. connections</source>
        <translation>最大连接数</translation>
    </message>
    <message>
        <location line="+12"/>
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
        <location line="+11"/>
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
        <location line="+54"/>
        <location line="+1313"/>
        <source>General</source>
        <translation>常规</translation>
    </message>
    <message>
        <location line="-1310"/>
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
        <source>Authentication</source>
        <translation>认证</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Enable authentication</source>
        <translation>启用认证</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+1663"/>
        <location line="+1009"/>
        <location line="+152"/>
        <source>Name:</source>
        <translation>名称：</translation>
    </message>
    <message>
        <location line="-2820"/>
        <location line="+655"/>
        <location line="+695"/>
        <location line="+19"/>
        <location line="+318"/>
        <source>Password:</source>
        <translation>密码：</translation>
    </message>
    <message>
        <location line="-1653"/>
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
        <location line="+9"/>
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
        <location line="+4"/>
        <source>Safe Connect</source>
        <translation>安全连接</translation>
    </message>
    <message>
        <location line="+4"/>
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
        <location line="+81"/>
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
        <location line="+13"/>
        <source>Select Temporary Directory</source>
        <translation>选择临时目录</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Shared Directories (Ctrl+Click includes subdirectories)</source>
        <translation>共享目录（Ctrl+点击包含子目录）</translation>
    </message>
    <message>
        <location line="+21"/>
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
        <location line="+17"/>
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
        <location line="+587"/>
        <source>Edit...</source>
        <translation>编辑...</translation>
    </message>
    <message>
        <location line="-583"/>
        <source>Filename Cleanup Rules</source>
        <translation>文件名清理规则</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Define patterns to automatically clean up filenames of new downloads.
Each rule replaces a regex pattern with a replacement string.</source>
        <translation>定义模式以自动清理新下载的文件名。
每条规则将正则表达式模式替换为替换字符串。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Pattern</source>
        <translation>模式</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Replacement</source>
        <translation>替换</translation>
    </message>
    <message>
        <location line="+50"/>
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
        <location line="+6"/>
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
        <location line="+4"/>
        <source>Create backup to preview</source>
        <translation>创建备份以预览</translation>
    </message>
    <message>
        <location line="+20"/>
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
        <location line="+20"/>
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
        <location line="+4"/>
        <source>Server:</source>
        <translation>服务器：</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+1089"/>
        <source>None</source>
        <translation>无</translation>
    </message>
    <message>
        <location line="-1088"/>
        <source>Plain</source>
        <translation>明文</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Authentication:</source>
        <translation>认证：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Use TLS/STARTTLS</source>
        <translation>使用 TLS/STARTTLS</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Username:</source>
        <translation>用户名：</translation>
    </message>
    <message>
        <location line="+43"/>
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
        <location line="+902"/>
        <location line="+1016"/>
        <location line="+154"/>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <location line="-2070"/>
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
        <location line="+20"/>
        <source>Connect to help channel</source>
        <translation>连接到帮助频道</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Load server channel list on connect</source>
        <translation>连接时加载服务器频道列表</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Add timestamp to messages</source>
        <translation>为消息添加时间戳</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Ignore info messages</source>
        <translation>忽略信息消息</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Ignore misc. info messages</source>
        <translation>忽略杂项信息消息</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Ignore Join info messages</source>
        <translation>忽略加入信息消息</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Ignore Part info messages</source>
        <translation>忽略离开信息消息</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Ignore Quit info messages</source>
        <translation>忽略退出信息消息</translation>
    </message>
    <message>
        <location line="+35"/>
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
        <location line="+5"/>
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
        <location line="+389"/>
        <source>Reload</source>
        <translation>重新加载</translation>
    </message>
    <message>
        <location line="-366"/>
        <source>http://example.com/ipfilter.dat</source>
        <translation>http://example.com/ipfilter.dat</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+19"/>
        <source>Load</source>
        <translation>加载</translation>
    </message>
    <message>
        <location line="-13"/>
        <source>Loading...</source>
        <translation>加载中...</translation>
    </message>
    <message>
        <location line="+17"/>
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
        <location line="+9"/>
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
        <location line="+9"/>
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
        <location line="+4"/>
        <source>Warn when opening untrusted files</source>
        <translation>打开不受信任的文件时警告</translation>
    </message>
    <message>
        <location line="+23"/>
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
        <location line="+154"/>
        <source>Update delay: %1 sec</source>
        <translation>更新延迟：%1 秒</translation>
    </message>
    <message>
        <location line="-153"/>
        <location line="+154"/>
        <source>Update delay: disabled</source>
        <translation>更新延迟：已禁用</translation>
    </message>
    <message>
        <location line="-147"/>
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
        <location line="+6"/>
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
        <location line="+5"/>
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
        <location line="-4"/>
        <source>Upload Friend Slots</source>
        <translation>上传好友位</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Upload Slots (no overhead)</source>
        <translation>上传位（无开销）</translation>
    </message>
    <message>
        <location line="-1139"/>
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
        <location line="+1133"/>
        <source>Download Usenet</source>
        <translation>Usenet 下载</translation>
    </message>
    <message>
        <location line="+20"/>
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
        <location line="+3"/>
        <source>Gzip compression</source>
        <translation>Gzip 压缩</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Include port into UPnP setup</source>
        <translation>将端口加入 UPnP 设置</translation>
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
        <location line="+50"/>
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
        <location line="+131"/>
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
        <location line="+37"/>
        <source>Account</source>
        <translation>账户</translation>
    </message>
    <message>
        <location line="-35"/>
        <source>Advanced</source>
        <translation>高级</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+627"/>
        <location line="+264"/>
        <location line="+15"/>
        <source>News servers</source>
        <translation>新闻服务器</translation>
    </message>
    <message>
        <location line="-900"/>
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
        <location line="+40"/>
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
        <location line="+2"/>
        <location line="+994"/>
        <location line="+165"/>
        <source> days</source>
        <translation> 天</translation>
    </message>
    <message>
        <location line="-1158"/>
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
        <location line="+1"/>
        <source>Strict</source>
        <translation>严格</translation>
    </message>
    <message>
        <location line="+2"/>
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
        <source>No limit</source>
        <translation>无限制</translation>
    </message>
    <message>
        <location line="+8"/>
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
        <source>Share of the download limit:</source>
        <translation>占下载限速的份额：</translation>
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
        <location line="+1290"/>
        <source>never</source>
        <translation>从不</translation>
    </message>
    <message>
        <location line="-1287"/>
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
        <location line="+49"/>
        <source>Watch folder</source>
        <translation>监视文件夹</translation>
    </message>
    <message>
        <location line="-45"/>
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
        <source>Browse…</source>
        <translation>浏览…</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Desktop</source>
        <translation>桌面</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Open .nzb files with eMule Qt</source>
        <translation>使用 eMule Qt 打开 .nzb 文件</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Claim .nzb files for this copy of eMule Qt, so double-clicking one queues it. The setting is for you alone and needs no administrator; it is re-applied at every start, so another program taking the association does not keep it.</source>
        <translation>将 .nzb 文件关联到此副本的 eMule Qt，这样双击即可将其加入队列。该设置仅对您自己生效，无需管理员权限；每次启动都会重新应用，因此其他程序抢走关联也无法一直占着。</translation>
    </message>
    <message>
        <location line="+21"/>
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
        <location line="+136"/>
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
        <location line="+70"/>
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
        <location line="+3"/>
        <source>Disabled</source>
        <translation>已禁用</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Autotake eD2K links only during runtime</source>
        <translation>仅在运行时自动接受 eD2K 链接</translation>
    </message>
    <message>
        <location line="+5"/>
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
        <location line="+7"/>
        <source>Open/close ports on WinXP firewall when starting/exiting eMule</source>
        <translation>启动/退出 eMule 时在 WinXP 防火墙上打开/关闭端口</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Filter server and client LAN IPs</source>
        <translation>过滤服务器和客户端局域网 IP</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show more controls (advanced mode controls)</source>
        <translation>显示更多控件（高级模式控件）</translation>
    </message>
    <message>
        <location line="+3"/>
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
        <location line="+4"/>
        <source>Allocate full file size for non-sparse part files</source>
        <translation>为非稀疏 part 文件分配完整文件大小</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Check disk space</source>
        <translation>检查磁盘空间</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Min. free disk space [MB]:</source>
        <translation>最小空闲磁盘空间 [MB]：</translation>
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
        <location line="+8"/>
        <source>Log level:</source>
        <translation>日志级别：</translation>
    </message>
    <message>
        <location line="+8"/>
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
        <location line="+18"/>
        <source>Upload SpeedSense (not recommended)</source>
        <translation>上传速度感应（不推荐）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Find best upload limit automatically</source>
        <translation>自动查找最佳上传限制</translation>
    </message>
    <message>
        <location line="+4"/>
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
        <location line="+22"/>
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
        <location line="+589"/>
        <source>Proxy settings will only apply to new connections.
Restart eMule for all connections to use the new proxy settings.

News server connections switch over immediately.</source>
        <translation>代理设置仅应用于新连接。
重启 eMule 以使所有连接使用新的代理设置。

新闻服务器连接会立即切换。</translation>
    </message>
    <message>
        <location line="+846"/>
        <source>File types</source>
        <translation>文件类型</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Could not update the .nzb file association: %1</source>
        <translation>无法更新 .nzb 文件关联：%1</translation>
    </message>
    <message>
        <location line="-5163"/>
        <location line="+1283"/>
        <location line="+1009"/>
        <location line="+154"/>
        <location line="+1324"/>
        <location line="+281"/>
        <source>Remove</source>
        <translation>删除</translation>
    </message>
    <message>
        <location line="-3840"/>
        <source>New eMule Qt version detected</source>
        <translation>检测到新的 eMule Qt 版本</translation>
    </message>
    <message>
        <location line="+357"/>
        <source>Update from URL: (filter.dat- or PeerGuardian-format, .gz/.zip accepted)</source>
        <translation>从 URL 更新：（filter.dat 或 PeerGuardian 格式，支持 .gz/.zip）</translation>
    </message>
    <message>
        <location line="+2915"/>
        <source>Write eMule core logs to disk</source>
        <translation>将 eMule 核心日志写入磁盘</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Write eMule GUI logs to disk</source>
        <translation>将 eMule 图形界面日志写入磁盘</translation>
    </message>
    <message>
        <location line="+26"/>
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
        <location line="+3"/>
        <source>Enable IPC log tab</source>
        <translation>启用 IPC 日志标签</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Start core with console (debug)</source>
        <translation>以控制台启动核心（调试）</translation>
    </message>
    <message>
        <location line="+89"/>
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
        <location line="-2195"/>
        <location line="+903"/>
        <location line="+1296"/>
        <source> s</source>
        <translation> 秒</translation>
    </message>
    <message>
        <location line="-2241"/>
        <source>Decimal GB, because that is what an invoice says — the 1024-based GB used elsewhere in eMule would put a 1000 GB plan 7% over.

Set it slightly under your plan. The figure is measured here, so it reads a few percent below your provider&apos;s, and articles already in flight when the limit is reached still finish.</source>
        <translation>使用十进制 GB，因为账单上就是这样写的——若按 eMule 其他地方使用的 1024 进制 GB 计算，1000 GB 的套餐会超出 7%。

请设置得略低于您的套餐。此数值在本地测量，因此会比服务商的数值低几个百分点；达到上限时已在传输中的文章仍会完成。</translation>
    </message>
    <message>
        <location line="+2356"/>
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
        <location line="+112"/>
        <source>Daily</source>
        <translation>每天</translation>
    </message>
    <message>
        <location line="-112"/>
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
        <location line="+4"/>
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
        <location line="-3866"/>
        <location line="+1286"/>
        <location line="+1009"/>
        <location line="+154"/>
        <location line="+1578"/>
        <source>Add</source>
        <translation>添加</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Action Value</source>
        <translation>操作值</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+15"/>
        <source>Enter value:</source>
        <translation>输入值：</translation>
    </message>
    <message>
        <location line="-3"/>
        <location line="+2"/>
        <source>Edit Value</source>
        <translation>编辑值</translation>
    </message>
    <message>
        <location line="+93"/>
        <source>The %1 settings page is not yet implemented.</source>
        <translation>设置页面 %1 尚未实现。</translation>
    </message>
    <message>
        <location line="+176"/>
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
        <location line="+29"/>
        <source>Core connection settings will take effect after restarting the application.</source>
        <translation>核心连接设置将在重启应用程序后生效。</translation>
    </message>
    <message>
        <location line="+27"/>
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
        <location filename="../src/gui/dialogs/PasteLinksDialog.cpp" line="+14"/>
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
        <location line="+9"/>
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
        <location filename="../src/gui/dialogs/PasteTextDialog.cpp" line="+42"/>
        <source>optional</source>
        <translation>可选</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Category:</source>
        <translation>分类：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Priority:</source>
        <translation>优先级：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Start paused</source>
        <translation>以暂停状态开始</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="+78"/>
        <source>Working…</source>
        <translation>正在处理…</translation>
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
    <name>eMule::SearchPanel</name>
    <message>
        <location filename="../src/gui/panels/SearchPanel.cpp" line="+220"/>
        <location line="+707"/>
        <location line="+315"/>
        <source>Download</source>
        <translation>下载</translation>
    </message>
    <message>
        <location line="-1002"/>
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
        <location line="+2"/>
        <source>Reset</source>
        <translation>重置</translation>
    </message>
    <message>
        <location line="+13"/>
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
        <location line="+5"/>
        <source>Min. Bitrate [kbps]:</source>
        <translation>最小比特率 [kbps]：</translation>
    </message>
    <message>
        <location line="+6"/>
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
        <location line="+11"/>
        <source>Start</source>
        <translation>开始</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="+32"/>
        <location line="+24"/>
        <location line="+106"/>
        <source>Not connected to daemon — search cannot be started.</source>
        <translation>未连接到守护进程 — 无法开始搜索。</translation>
    </message>
    <message>
        <location line="-73"/>
        <source>Search</source>
        <translation>搜索</translation>
    </message>
    <message>
        <location line="+47"/>
        <source>Kad: &quot;%1&quot; is already being searched — using &quot;%2&quot; as the search target.</source>
        <translation>Kad：正在搜索 &quot;%1&quot; — 使用 &quot;%2&quot; 作为搜索目标。</translation>
    </message>
    <message>
        <location line="+47"/>
        <location line="+191"/>
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
        <location line="+4"/>
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
        <location line="+103"/>
        <source>&amp;Download</source>
        <translation>下载(&amp;D)</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Download &amp;To</source>
        <translation>下载到(&amp;T)</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Copy &amp;Name</source>
        <translation>复制名称(&amp;N)</translation>
    </message>
    <message>
        <location line="+52"/>
        <source>Details...</source>
        <translation>详情...</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Comments...</source>
        <translation>评论...</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Copy eD2K Links</source>
        <translation>复制 eD2K 链接</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Copy eD2K Links (HTML)</source>
        <translation>复制 eD2K 链接 (HTML)</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Mark as not Spam</source>
        <translation>标记为非垃圾</translation>
    </message>
    <message>
        <location line="+61"/>
        <location line="+629"/>
        <source>Preview</source>
        <translation>预览</translation>
    </message>
    <message>
        <location line="-441"/>
        <source>You have already downloaded the following file(s). Download them again?

%1</source>
        <translation>您已经下载过以下文件。要重新下载吗？

%1</translation>
    </message>
    <message>
        <location line="+687"/>
        <source>Asking servers: %1 / %2</source>
        <translation>正在询问服务器：%1 / %2</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>All</source>
        <translation>全部</translation>
    </message>
    <message>
        <location line="-975"/>
        <location line="+14"/>
        <source>Mark as Spam</source>
        <translation>标记为垃圾</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Remove</source>
        <translation>删除</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Close Search Results</source>
        <translation>关闭搜索结果</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Close All Search Results</source>
        <translation>关闭所有搜索结果</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Find...</source>
        <translation>查找...</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Search Related Files</source>
        <translation>搜索相关文件</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Web Services</source>
        <translation>Web 服务</translation>
    </message>
    <message>
        <location line="+608"/>
        <source>Preview not available — web server is not running or stream token not received.</source>
        <translation>预览不可用 — Web 服务器未运行或未收到流令牌。</translation>
    </message>
</context>
<context>
    <name>eMule::SearchResultsModel</name>
    <message>
        <location filename="../src/gui/controls/SearchResultsModel.cpp" line="+70"/>
        <source>Yes</source>
        <translation>是</translation>
    </message>
    <message>
        <location line="+119"/>
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
</context>
<context>
    <name>eMule::ServerListModel</name>
    <message>
        <location filename="../src/gui/controls/ServerListModel.cpp" line="+74"/>
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
        <location line="+61"/>
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
        <location line="+32"/>
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
        <location line="-33"/>
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
        <location filename="../src/gui/panels/ServerPanel.cpp" line="+234"/>
        <source>Disconnect</source>
        <translation>断开连接</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+24"/>
        <location line="+50"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="-72"/>
        <location line="+440"/>
        <source>Connect</source>
        <translation>连接</translation>
    </message>
    <message>
        <location line="-491"/>
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
        <location line="+587"/>
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
        <location line="-622"/>
        <source>Connect To</source>
        <translation>连接到</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Priority</source>
        <translation>优先级</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Low</source>
        <translation>低</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+511"/>
        <source>Normal</source>
        <translation>普通</translation>
    </message>
    <message>
        <location line="-510"/>
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
        <location line="+24"/>
        <source>Copy eD2K Links</source>
        <translation>复制 eD2K 链接</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Paste eD2K Links</source>
        <translation>粘贴 eD2K 链接</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Remove</source>
        <translation>删除</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Remove All</source>
        <translation>全部删除</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Find...</source>
        <translation>查找...</translation>
    </message>
    <message>
        <location line="+52"/>
        <source>▸ Servers (0)</source>
        <translation>▸ 服务器 (0)</translation>
    </message>
    <message>
        <location line="+64"/>
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
        <location line="+120"/>
        <source>Name:</source>
        <translation>名称：</translation>
    </message>
    <message>
        <location line="-114"/>
        <source>Add to list</source>
        <translation>添加到列表</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Update server.met from URL</source>
        <translation>从 URL 更新 server.met</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Update server.met from URL:</source>
        <translation>从 URL 更新 server.met：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Update</source>
        <translation>更新</translation>
    </message>
    <message>
        <location line="+46"/>
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
    <message>
        <location line="-46"/>
        <source>Low ID</source>
        <translation>Low ID</translation>
    </message>
    <message>
        <location line="+277"/>
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
        <location line="+43"/>
        <source>server.met processed: %1 servers added, %2 skipped (duplicates/invalid).</source>
        <translation>server.met处理完成: 添加%1个服务器，跳过%2个（重复/无效）。</translation>
    </message>
</context>
<context>
    <name>eMule::SharedFilesModel</name>
    <message>
        <location filename="../src/gui/controls/SharedFilesModel.cpp" line="+190"/>
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
        <source>Shared eD2K/Kad</source>
        <translation>共享 eD2K/Kad</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Folder</source>
        <translation>文件夹</translation>
    </message>
</context>
<context>
    <name>eMule::SharedFilesPanel</name>
    <message>
        <location filename="../src/gui/panels/SharedFilesPanel.cpp" line="+129"/>
        <location line="+12"/>
        <location line="+380"/>
        <location line="+362"/>
        <location line="+99"/>
        <source>Shared Files (0)</source>
        <translation>共享文件 (0)</translation>
    </message>
    <message>
        <location line="-755"/>
        <source>Open File</source>
        <translation>打开文件</translation>
    </message>
    <message>
        <location line="+13"/>
        <location line="+1308"/>
        <source>Open Folder</source>
        <translation>打开文件夹</translation>
    </message>
    <message>
        <location line="-1296"/>
        <source>Rename...</source>
        <translation>重命名...</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Rename File</source>
        <translation>重命名文件</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>New file name:</source>
        <translation>新文件名:</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Delete From Disk</source>
        <translation>从磁盘删除</translation>
    </message>
    <message>
        <location line="+798"/>
        <source>Delete File</source>
        <translation>删除文件</translation>
    </message>
    <message>
        <location line="-6"/>
        <source>Are you sure you want to permanently delete &quot;%1&quot; from disk?</source>
        <translation>确定要从磁盘永久删除&quot;%1&quot;吗？</translation>
    </message>
    <message>
        <location line="-777"/>
        <source>Unshare</source>
        <translation>取消共享</translation>
    </message>
    <message>
        <location line="+810"/>
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
        <location line="-793"/>
        <source>Priority (Upload)</source>
        <translation>优先级（上传）</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Very Low</source>
        <translation>非常低</translation>
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
        <location line="+1"/>
        <source>High</source>
        <translation>高</translation>
    </message>
    <message>
        <source>Very High</source>
        <translation type="vanished">非常高</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Auto</source>
        <translation>自动</translation>
    </message>
    <message>
        <location line="+14"/>
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
        <location line="+20"/>
        <source>Details...</source>
        <translation>详情...</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Comments...</source>
        <translation>评论...</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>eD2K Links...</source>
        <translation>eD2K 链接...</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Find...</source>
        <translation>查找...</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Web Services</source>
        <translation>Web 服务</translation>
    </message>
    <message>
        <location line="+72"/>
        <source>Reload</source>
        <translation>重新加载</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>File Name</source>
        <translation>文件名</translation>
    </message>
    <message>
        <location line="+5"/>
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
        <location line="+130"/>
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
        <location line="+231"/>
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
        <location line="+518"/>
        <source>Share Directory</source>
        <translation>共享目录</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Share with Subdirectories</source>
        <translation>连同子目录一起共享</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Unshare Directory</source>
        <translation>取消共享目录</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Unshare with Subdirectories</source>
        <translation>连同子目录一起取消共享</translation>
    </message>
    <message>
        <location line="+70"/>
        <source>Open File not available — web server is not running or stream token not received.</source>
        <translation>打开文件不可用 — Web 服务器未运行或未收到流令牌。</translation>
    </message>
    <message>
        <location line="-868"/>
        <source>Content</source>
        <translation>内容</translation>
    </message>
    <message>
        <location line="+58"/>
        <source>eD2K Links</source>
        <translation>eD2K 链接</translation>
    </message>
    <message>
        <location line="-18"/>
        <source>Copy</source>
        <translation>复制</translation>
    </message>
    <message>
        <location line="-511"/>
        <source>Release</source>
        <translation>发布</translation>
    </message>
    <message>
        <location line="+485"/>
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
        <location line="+24"/>
        <location line="+411"/>
        <source>Requires a hostname configured in Preferences, or a public IPv6</source>
        <translation>需要在首选项中配置主机名，或拥有公网 IPv6</translation>
    </message>
    <message>
        <location line="-299"/>
        <source>Shared Files (%1)</source>
        <translation>共享文件 (%1)</translation>
    </message>
    <message numerus="yes">
        <location line="+112"/>
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
        <location line="+155"/>
        <source>Add your hostname or public IPv6 as a source</source>
        <translation>将您的主机名或公网 IPv6 添加为来源</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Showing eD2K links for the first %1 of %2 selected files.</source>
        <translation>正在显示所选 %2 个文件中前 %1 个的 eD2K 链接。</translation>
    </message>
</context>
<context>
    <name>eMule::StatisticsPanel</name>
    <message>
        <location filename="../src/gui/panels/StatisticsPanel.cpp" line="-49"/>
        <location line="+8"/>
        <source>Session average</source>
        <translation>会话平均</translation>
    </message>
    <message>
        <location line="-7"/>
        <location line="+8"/>
        <source>Average (3 min)</source>
        <translation>平均 (3 分钟)</translation>
    </message>
    <message>
        <location line="-7"/>
        <location line="+8"/>
        <source>Current</source>
        <translation>当前</translation>
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
        <translation>当前（不含开销）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Friend slots</source>
        <translation>好友位</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Active connections</source>
        <translation>活跃连接</translation>
    </message>
    <message>
        <location line="+1"/>
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
        <location line="+45"/>
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
        <location line="+155"/>
        <location line="+19"/>
        <location line="+1100"/>
        <source>Uploads</source>
        <translation>上传</translation>
    </message>
    <message>
        <location line="-1270"/>
        <location line="+63"/>
        <location line="+78"/>
        <location line="+47"/>
        <location line="+1001"/>
        <location line="+70"/>
        <source>Session</source>
        <translation>会话</translation>
    </message>
    <message>
        <location line="-1256"/>
        <location line="+32"/>
        <source>Uploaded Data: 0 Bytes</source>
        <translation>已上传数据：0 Bytes</translation>
    </message>
    <message>
        <location line="-19"/>
        <source>Uploaded Data to Friends: 0 Bytes</source>
        <translation>上传给好友的数据：0 Bytes</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Active Uploads: 0</source>
        <translation>活跃上传：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Waiting Uploads: 0</source>
        <translation>等待上传：0</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+27"/>
        <source>Upload Sessions</source>
        <translation>上传会话</translation>
    </message>
    <message>
        <location line="-26"/>
        <location line="+27"/>
        <location line="+36"/>
        <location line="+33"/>
        <source>Successful: 0</source>
        <translation>成功：0</translation>
    </message>
    <message>
        <location line="-95"/>
        <location line="+27"/>
        <location line="+36"/>
        <location line="+33"/>
        <source>Failed: 0</source>
        <translation>失败：0</translation>
    </message>
    <message>
        <location line="-94"/>
        <location line="+27"/>
        <source>Average Upload Per Session: 0 Bytes</source>
        <translation>每次会话平均上传：0 Bytes</translation>
    </message>
    <message>
        <location line="-25"/>
        <location line="+27"/>
        <source>Average Upload Time: 0:00:00</source>
        <translation>平均上传时间：0:00:00</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+97"/>
        <location line="+19"/>
        <location line="+936"/>
        <location line="+163"/>
        <source>Downloads</source>
        <translation>下载</translation>
    </message>
    <message>
        <location line="-1208"/>
        <location line="+39"/>
        <source>Downloaded Data: 0 Bytes</source>
        <translation>已下载数据：0 Bytes</translation>
    </message>
    <message>
        <location line="-30"/>
        <source>Active Downloads: 0</source>
        <translation>活跃下载：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Found Sources: 0</source>
        <translation>已找到来源：0</translation>
    </message>
    <message>
        <location line="+61"/>
        <source>Connection</source>
        <translation>连接</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Active Connections: 0</source>
        <translation>活跃连接：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+21"/>
        <source>Peak Connections: 0</source>
        <translation>峰值连接：0</translation>
    </message>
    <message>
        <location line="-20"/>
        <source>Max Connections Limit Reached: 0</source>
        <translation>已达到最大连接限制：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Reconnects: 0</source>
        <translation>重连次数：0</translation>
    </message>
    <message>
        <location line="+33"/>
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
        <location line="-200"/>
        <location line="+32"/>
        <location line="+31"/>
        <location line="+39"/>
        <location line="+102"/>
        <source>Clients</source>
        <translation>客户端</translation>
    </message>
    <message>
        <location line="-201"/>
        <location line="+32"/>
        <location line="+31"/>
        <location line="+39"/>
        <source>%1: 0 Bytes</source>
        <translation>%1：0 Bytes</translation>
    </message>
    <message>
        <location line="-101"/>
        <location line="+32"/>
        <location line="+31"/>
        <location line="+39"/>
        <source>Port</source>
        <translation>端口</translation>
    </message>
    <message>
        <location line="-101"/>
        <location line="+32"/>
        <location line="+31"/>
        <location line="+39"/>
        <source>Default Port 4662: 0 Bytes</source>
        <translation>默认端口 4662：0 Bytes</translation>
    </message>
    <message>
        <location line="-101"/>
        <location line="+32"/>
        <location line="+31"/>
        <location line="+39"/>
        <source>Other Ports: 0 Bytes</source>
        <translation>其他端口：0 Bytes</translation>
    </message>
    <message>
        <location line="-101"/>
        <location line="+32"/>
        <source>Data Source</source>
        <translation>数据来源</translation>
    </message>
    <message>
        <location line="-31"/>
        <location line="+32"/>
        <source>Complete File: 0 Bytes</source>
        <translation>完整文件：0 Bytes</translation>
    </message>
    <message>
        <location line="-31"/>
        <location line="+32"/>
        <source>Part File: 0 Bytes</source>
        <translation>分块文件：0 Bytes</translation>
    </message>
    <message>
        <location line="-13"/>
        <location line="+70"/>
        <location line="+60"/>
        <location line="+37"/>
        <location line="+995"/>
        <location line="+70"/>
        <source>Cumulative</source>
        <translation>累计</translation>
    </message>
    <message>
        <location line="-1183"/>
        <location line="+33"/>
        <source>Completed Downloads: 0</source>
        <translation>已完成下载：0</translation>
    </message>
    <message>
        <location line="-31"/>
        <location line="+33"/>
        <source>Download Sessions</source>
        <translation>下载会话</translation>
    </message>
    <message>
        <location line="-29"/>
        <location line="+33"/>
        <source>Average Download Per Session: 0 Bytes</source>
        <translation>每次会话平均下载：0 Bytes</translation>
    </message>
    <message>
        <location line="-31"/>
        <location line="+33"/>
        <source>Average Download Time: 0:00:00</source>
        <translation>平均下载时间：0:00:00</translation>
    </message>
    <message>
        <location line="-30"/>
        <location line="+33"/>
        <source>Gain Due To Compression: 0 Bytes (0.0%)</source>
        <translation>压缩带来的收益：0 Bytes (0.0%)</translation>
    </message>
    <message>
        <location line="-31"/>
        <location line="+33"/>
        <source>Lost Due To Corruption: 0 Bytes (0.0%)</source>
        <translation>损坏造成的损失：0 Bytes (0.0%)</translation>
    </message>
    <message>
        <location line="-31"/>
        <location line="+33"/>
        <source>Parts Saved Due To ICH: 0</source>
        <translation>通过 ICH 挽救的分块：0</translation>
    </message>
    <message>
        <location line="+15"/>
        <location line="+21"/>
        <location line="+925"/>
        <source>General</source>
        <translation>常规</translation>
    </message>
    <message>
        <location line="-941"/>
        <source>Average Connections: 0.0</source>
        <translation>平均连接数：0.0</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Upload Speed: 0 KB/s</source>
        <translation>上传速度：0 KB/s</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+19"/>
        <source>Max Upload Rate: 0 KB/s</source>
        <translation>最大上传速率：0 KB/s</translation>
    </message>
    <message>
        <location line="-18"/>
        <location line="+19"/>
        <source>Max Average Upload Rate: 0 KB/s</source>
        <translation>最大平均上传速率：0 KB/s</translation>
    </message>
    <message>
        <location line="-16"/>
        <source>Download Speed: 0 KB/s</source>
        <translation>下载速度：0 KB/s</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+19"/>
        <source>Max Download Rate: 0 KB/s</source>
        <translation>最大下载速率：0 KB/s</translation>
    </message>
    <message>
        <location line="-18"/>
        <location line="+19"/>
        <source>Max Average Download Rate: 0 KB/s</source>
        <translation>最大平均下载速率：0 KB/s</translation>
    </message>
    <message>
        <location line="-12"/>
        <source>Server Reconnects: 0</source>
        <translation>服务器重连次数：0</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Connection Limit Reached: 0</source>
        <translation>达到连接限制：0</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Average Upload Rate: 0 KB/s</source>
        <translation>平均上传速率：0 KB/s</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Average Download Rate: 0 KB/s</source>
        <translation>平均下载速率：0 KB/s</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+458"/>
        <location line="+4"/>
        <source>Statistics Last Reset: %1</source>
        <translation>统计上次重置: %1</translation>
    </message>
    <message>
        <location line="-749"/>
        <location line="+287"/>
        <location line="+456"/>
        <location line="+7"/>
        <source>Unknown</source>
        <translation>未知</translation>
    </message>
    <message>
        <location line="-757"/>
        <source>Statistics Tree</source>
        <translation>统计树</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+753"/>
        <source>Statistics last reset: %1</source>
        <translation>统计上次重置: %1</translation>
    </message>
    <message>
        <location line="-726"/>
        <location line="+1262"/>
        <source>Usenet</source>
        <translation>Usenet</translation>
    </message>
    <message>
        <location line="-1197"/>
        <location line="+1074"/>
        <source>Waiting...</source>
        <translation>等待...</translation>
    </message>
    <message>
        <location line="-1071"/>
        <location line="+404"/>
        <source>Session UL:DL Ratio (Friends UL excluded): %1</source>
        <translation>会话上传:下载比率 (排除对好友的上传)：%1</translation>
    </message>
    <message>
        <location line="-316"/>
        <source>UDP File Re-asks: 0, Failed: 0 (0.0%)</source>
        <translation>UDP 文件重新请求：0，失败：0 (0.0%)</translation>
    </message>
    <message>
        <location line="+1028"/>
        <source>Corrupt (Failed yEnc Check): %1</source>
        <translation>损坏（yEnc 校验失败）：%1</translation>
    </message>
    <message>
        <location line="+146"/>
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
        <location line="-1053"/>
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
        <location line="+5"/>
        <source>Known Clients: 0</source>
        <translation>已知客户端：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Client Software</source>
        <translation>客户端软件</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Low ID: 0 (0.0%)</source>
        <translation>Low ID：0 (0.0%)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Banned Clients: 0</source>
        <translation>已封禁客户端：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Filtered Clients: 0</source>
        <translation>已过滤客户端：0</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Servers</source>
        <translation>服务器</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Working Servers: 0</source>
        <translation>工作中的服务器：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Failed Servers: 0</source>
        <translation>失败的服务器：0</translation>
    </message>
    <message>
        <location line="+1"/>
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
        <location line="+1"/>
        <source>Low ID Users: 0</source>
        <translation>Low ID 用户：0</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+13"/>
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
        <location line="+2"/>
        <source>Number of Shared Files: 0</source>
        <translation>共享文件数：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Size: 0 Bytes</source>
        <translation>总大小：0 Bytes</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Average File Size: 0 Bytes</source>
        <translation>平均文件大小：0 Bytes</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Largest Shared File: 0 Bytes</source>
        <translation>最大共享文件：0 Bytes</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Most Files Shared: 0</source>
        <translation>最多共享文件：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Largest Share Size: 0 Bytes</source>
        <translation>最大共享容量：0 Bytes</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Largest Average File Size: 0 Bytes</source>
        <translation>最大平均文件大小：0 Bytes</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Largest File Size: 0 Bytes</source>
        <translation>最大文件大小：0 Bytes</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Total Downloads</source>
        <translation>下载总计</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Number of Downloads: 0</source>
        <translation>下载数量：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Size of Downloads: 0 Bytes</source>
        <translation>下载总大小：0 Bytes</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Size Downloaded: 0 Bytes</source>
        <translation>已下载总大小：0 Bytes</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Size Left to Download: 0 Bytes</source>
        <translation>剩余下载大小：0 Bytes</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Free Space on Drive: 0 Bytes</source>
        <translation>磁盘可用空间：0 Bytes</translation>
    </message>
    <message>
        <location line="-264"/>
        <location line="+404"/>
        <source>Session UL:DL Ratio: %1</source>
        <translation>会话上传:下载比率：%1</translation>
    </message>
    <message>
        <source>Friend Session UL:DL Ratio: %1</source>
        <translation type="vanished">好友会话上传:下载比率：%1</translation>
    </message>
    <message>
        <location line="-401"/>
        <location line="+405"/>
        <source>Cumulative UL:DL Ratio: %1</source>
        <translation>累计上传:下载比率：%1</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+56"/>
        <source>Uploaded Data: %1</source>
        <translation>已上传数据：%1</translation>
    </message>
    <message>
        <location line="-45"/>
        <location line="+55"/>
        <location line="+60"/>
        <location line="+48"/>
        <source>Default Port 4662: %1 %2</source>
        <translation>默认端口 4662：%1 %2</translation>
    </message>
    <message>
        <location line="-160"/>
        <location line="+55"/>
        <location line="+60"/>
        <location line="+48"/>
        <source>Other Ports: %1 %2</source>
        <translation>其他端口：%1 %2</translation>
    </message>
    <message>
        <location line="-160"/>
        <location line="+55"/>
        <source>Complete File: %1 %2</source>
        <translation>完整文件：%1 %2</translation>
    </message>
    <message>
        <location line="-52"/>
        <location line="+55"/>
        <source>Part File: %1 %2</source>
        <translation>分块文件：%1 %2</translation>
    </message>
    <message>
        <location line="-50"/>
        <source>Uploaded Data to Friends: %1</source>
        <translation>上传给好友的数据：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Active Uploads: %1</source>
        <translation>活跃上传：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Waiting Uploads: %1</source>
        <translation>等待上传：%1</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+48"/>
        <source>Successful: %1%2</source>
        <translation>成功：%1%2</translation>
    </message>
    <message>
        <location line="-46"/>
        <location line="+48"/>
        <location line="+639"/>
        <location line="+49"/>
        <source>Failed: %1</source>
        <translation>失败：%1</translation>
    </message>
    <message>
        <location line="-733"/>
        <location line="+48"/>
        <source>Average Upload Per Session: %1</source>
        <translation>每次会话平均上传：%1</translation>
    </message>
    <message>
        <location line="-46"/>
        <location line="+48"/>
        <source>Average Upload Time: %1</source>
        <translation>平均上传时间：%1</translation>
    </message>
    <message>
        <location line="-44"/>
        <source>%1: %2</source>
        <translation>%1：%2</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+60"/>
        <location line="+48"/>
        <location line="+36"/>
        <source>Total Overhead (Packets)</source>
        <translation>总开销（数据包）</translation>
    </message>
    <message>
        <location line="-143"/>
        <location line="+60"/>
        <location line="+48"/>
        <location line="+36"/>
        <source>File Request Overhead (Packets)</source>
        <translation>文件请求开销（数据包）</translation>
    </message>
    <message>
        <location line="-143"/>
        <location line="+60"/>
        <location line="+48"/>
        <location line="+36"/>
        <source>Source Exchange Overhead (Packets)</source>
        <translation>来源交换开销（数据包）</translation>
    </message>
    <message>
        <location line="-143"/>
        <location line="+60"/>
        <location line="+48"/>
        <location line="+36"/>
        <source>Server Overhead (Packets)</source>
        <translation>服务器开销（数据包）</translation>
    </message>
    <message>
        <location line="-143"/>
        <location line="+60"/>
        <location line="+48"/>
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
        <location line="-81"/>
        <location line="+48"/>
        <location line="+533"/>
        <source>Downloaded Data: %1</source>
        <translation>已下载数据：%1</translation>
    </message>
    <message>
        <location line="-563"/>
        <source>Active Downloads: %1</source>
        <translation>活跃下载：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Found Sources: %1</source>
        <translation>找到的来源: %1</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>UDP File Re-asks: %1, Failed: %2 %3</source>
        <translation>UDP 文件重新请求：%1，失败：%2 %3</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+37"/>
        <source>Completed Downloads: %1</source>
        <translation>已完成下载：%1</translation>
    </message>
    <message>
        <location line="-31"/>
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
        <location line="+10"/>
        <location line="+494"/>
        <source>Active Connections: %1</source>
        <translation>活跃连接：%1</translation>
    </message>
    <message>
        <location line="-492"/>
        <location line="+19"/>
        <location line="+474"/>
        <source>Peak Connections: %1</source>
        <translation>峰值连接：%1</translation>
    </message>
    <message>
        <location line="-491"/>
        <source>Max Connections Limit Reached: %1</source>
        <translation>已达到最大连接限制：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Reconnects: %1</source>
        <translation>重连次数：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Average Connections: %1</source>
        <translation>平均连接：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Upload Speed: %1</source>
        <translation>上传速度：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+15"/>
        <source>Max Upload Rate: %1</source>
        <translation>最大上传速率：%1</translation>
    </message>
    <message>
        <location line="-14"/>
        <location line="+15"/>
        <source>Max Average Upload Rate: %1</source>
        <translation>最大平均上传速率：%1</translation>
    </message>
    <message>
        <location line="-14"/>
        <location line="+476"/>
        <source>Download Speed: %1</source>
        <translation>下载速度：%1</translation>
    </message>
    <message>
        <location line="-475"/>
        <location line="+15"/>
        <location line="+462"/>
        <source>Max Download Rate: %1</source>
        <translation>最大下载速率：%1</translation>
    </message>
    <message>
        <location line="-476"/>
        <location line="+15"/>
        <source>Max Average Download Rate: %1</source>
        <translation>最大平均下载速率：%1</translation>
    </message>
    <message>
        <location line="-11"/>
        <source>Server Reconnects: %1</source>
        <translation>服务器重连次数：%1</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Connection Limit Reached: %1</source>
        <translation>达到连接限制：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Average Upload Rate: %1</source>
        <translation>平均上传速率：%1</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+462"/>
        <source>Average Download Rate: %1</source>
        <translation>平均下载速率：%1</translation>
    </message>
    <message>
        <location line="-449"/>
        <location line="+4"/>
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
        <source>Upload Time: %1 %2</source>
        <translation>上传时间：%1 %2</translation>
    </message>
    <message>
        <location line="-18"/>
        <location line="+20"/>
        <location line="+405"/>
        <source>Download Time: %1 %2</source>
        <translation>下载时间：%1 %2</translation>
    </message>
    <message>
        <source>Server Duration: %1 %2</source>
        <translation type="vanished">服务器持续时间：%1 %2</translation>
    </message>
    <message>
        <location line="-411"/>
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
        <location line="-495"/>
        <source>Current Server Duration: 0:00:00</source>
        <translation>当前服务器持续时间：0:00:00</translation>
    </message>
    <message>
        <location line="+475"/>
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
        <location line="+3"/>
        <source>Banned Clients: %1</source>
        <translation>已封禁客户端：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Filtered Clients: %1</source>
        <translation>已过滤客户端：%1</translation>
    </message>
    <message>
        <location line="+87"/>
        <source>Working Servers: %1</source>
        <translation>工作中的服务器：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Failed Servers: %1</source>
        <translation>失败的服务器：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Total: %1</source>
        <translation>总计：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Total Users: %1</source>
        <translation>总用户：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Total Files: %1</source>
        <translation>总文件：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Low ID Users: %1</source>
        <translation>Low ID 用户：%1</translation>
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
        <location line="+6"/>
        <source>Number of Shared Files: %1</source>
        <translation>共享文件数：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Total Size: %1</source>
        <translation>总大小：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Average File Size: %1</source>
        <translation>平均文件大小：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Largest Shared File: %1</source>
        <translation>最大共享文件：%1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Most Files Shared: %1</source>
        <translation>最多共享文件：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Largest Share Size: %1</source>
        <translation>最大共享容量：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Largest Average File Size: %1</source>
        <translation>最大平均文件大小：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Largest File Size: %1</source>
        <translation>最大文件大小：%1</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+333"/>
        <source>Number of Downloads: %1</source>
        <translation>下载数量：%1</translation>
    </message>
    <message>
        <location line="-331"/>
        <location line="+339"/>
        <source>Total Size of Downloads: %1</source>
        <translation>下载总大小：%1</translation>
    </message>
    <message>
        <location line="-337"/>
        <location line="+338"/>
        <source>Total Size Downloaded: %1</source>
        <translation>已下载总大小：%1</translation>
    </message>
    <message>
        <location line="-336"/>
        <location line="+337"/>
        <source>Total Size Left to Download: %1</source>
        <translation>剩余下载大小：%1</translation>
    </message>
    <message>
        <location line="-335"/>
        <source>Free Space on Drive: %1</source>
        <translation>磁盘可用空间：%1</translation>
    </message>
    <message>
        <location line="+66"/>
        <location line="+39"/>
        <source>Reset Statistics</source>
        <translation>重置统计</translation>
    </message>
    <message>
        <location line="-38"/>
        <location line="+58"/>
        <source>Restore Statistics</source>
        <translation>恢复统计</translation>
    </message>
    <message>
        <location line="-52"/>
        <source>Expand Main Sections</source>
        <translation>展开主要部分</translation>
    </message>
    <message>
        <location line="+4"/>
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
        <source>Copy Branch</source>
        <translation>复制分支</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Copy All Visible</source>
        <translation>复制所有可见</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Copy All Statistics</source>
        <translation>复制所有统计</translation>
    </message>
    <message>
        <location line="+19"/>
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
        <location line="+127"/>
        <location line="+287"/>
        <source>Open Connections: %1</source>
        <translation>打开的连接：%1</translation>
    </message>
    <message>
        <location line="-283"/>
        <source>Network Traffic: %1</source>
        <translation>网络流量：%1</translation>
    </message>
    <message>
        <location line="+1"/>
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
        <location line="+282"/>
        <source>Connection Errors: %1</source>
        <translation>连接错误：%1</translation>
    </message>
    <message>
        <location line="-279"/>
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
        <source>Searches: %1</source>
        <translation>搜索次数：%1</translation>
    </message>
    <message>
        <location line="+1"/>
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
        <location line="+81"/>
        <source>Failed: %1 %2</source>
        <translation>失败：%1 %2</translation>
    </message>
    <message>
        <location line="-80"/>
        <source>Feed Polls: %1</source>
        <translation>订阅源轮询：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Feed Matches: %1</source>
        <translation>订阅源匹配：%1</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Downloading: %1</source>
        <translation>正在下载：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Queued: %1</source>
        <translation>排队中：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paused: %1</source>
        <translation>已暂停：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Checking: %1</source>
        <translation>正在检查：%1</translation>
    </message>
    <message>
        <location line="+1"/>
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
        <location line="+125"/>
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
    <name>eMule::ToolbarCustomizeDialog</name>
    <message>
        <location filename="../src/gui/dialogs/ToolbarCustomizeDialog.cpp" line="+38"/>
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
    <name>eMule::TransferPanel</name>
    <message>
        <location filename="../src/gui/panels/TransferPanel.cpp" line="+615"/>
        <source>Downloads</source>
        <translation>下载</translation>
    </message>
    <message>
        <location line="-284"/>
        <source>Priority (Download)</source>
        <translation>优先级（下载）</translation>
    </message>
    <message>
        <location line="+29"/>
        <location line="+1517"/>
        <location line="+98"/>
        <source>Low</source>
        <translation>低</translation>
    </message>
    <message>
        <location line="-1614"/>
        <location line="+1516"/>
        <location line="+99"/>
        <source>Normal</source>
        <translation>普通</translation>
    </message>
    <message>
        <location line="-1614"/>
        <location line="+1515"/>
        <location line="+100"/>
        <source>High</source>
        <translation>高</translation>
    </message>
    <message>
        <location line="-1613"/>
        <location line="+1615"/>
        <source>Very Low</source>
        <translation>非常低</translation>
    </message>
    <message>
        <location line="-1614"/>
        <location line="+1615"/>
        <source>Very High</source>
        <translation>非常高</translation>
    </message>
    <message>
        <location line="-1613"/>
        <location line="+1616"/>
        <source>Auto</source>
        <translation>自动</translation>
    </message>
    <message>
        <location line="-1605"/>
        <location line="+512"/>
        <location line="+1001"/>
        <source>Pause</source>
        <translation>暂停</translation>
    </message>
    <message>
        <location line="-1504"/>
        <location line="+509"/>
        <location line="+997"/>
        <source>Stop</source>
        <translation>停止</translation>
    </message>
    <message>
        <location line="-1497"/>
        <location line="+506"/>
        <location line="+993"/>
        <source>Resume</source>
        <translation>恢复</translation>
    </message>
    <message>
        <location line="-1486"/>
        <location line="+499"/>
        <location line="+989"/>
        <location line="+4"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="-1489"/>
        <location line="+510"/>
        <source>Cancel Download</source>
        <translation>取消下载</translation>
    </message>
    <message>
        <location line="-509"/>
        <location line="+510"/>
        <source>Cancel download &quot;%1&quot;?</source>
        <translation>取消下载 &quot;%1&quot;？</translation>
    </message>
    <message>
        <location line="-506"/>
        <location line="+510"/>
        <source>Cancel Downloads</source>
        <translation>取消多个下载</translation>
    </message>
    <message>
        <location line="-509"/>
        <location line="+510"/>
        <source>Cancel %1 selected downloads?</source>
        <translation>取消选中的 %1 个下载？</translation>
    </message>
    <message>
        <location line="-497"/>
        <location line="+508"/>
        <source>Open File</source>
        <translation>打开文件</translation>
    </message>
    <message>
        <location line="-499"/>
        <location line="+513"/>
        <source>Preview</source>
        <translation>预览</translation>
    </message>
    <message>
        <location line="-507"/>
        <location line="+1573"/>
        <location line="+82"/>
        <source>Details...</source>
        <translation>详情...</translation>
    </message>
    <message>
        <location line="-1649"/>
        <source>Comments...</source>
        <translation>评论...</translation>
    </message>
    <message>
        <location line="+17"/>
        <location line="+532"/>
        <source>Clear Completed</source>
        <translation>清除已完成</translation>
    </message>
    <message>
        <location line="-522"/>
        <source>eD2K Links...</source>
        <translation>eD2K 链接...</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Paste eD2K Links</source>
        <translation>粘贴 eD2K 链接</translation>
    </message>
    <message>
        <location line="+21"/>
        <location line="+1579"/>
        <location line="+77"/>
        <source>Find...</source>
        <translation>查找...</translation>
    </message>
    <message>
        <location line="-1652"/>
        <source>Search Related Files</source>
        <translation>搜索相关文件</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Web Services</source>
        <translation>Web 服务</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+454"/>
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
        <location line="+842"/>
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
        <location line="-965"/>
        <source>Clients on queue:   0</source>
        <translation>排队客户端：   0</translation>
    </message>
    <message>
        <location line="-330"/>
        <location line="+457"/>
        <source>(Unassign)</source>
        <translation>（取消分配）</translation>
    </message>
    <message>
        <location line="-104"/>
        <location line="+990"/>
        <source>Priority</source>
        <translation>优先级</translation>
    </message>
    <message>
        <location line="-929"/>
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
        <location line="+32"/>
        <source>Search Related</source>
        <translation>相关搜索</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Find</source>
        <translation>查找</translation>
    </message>
    <message>
        <location line="+442"/>
        <source>Preview not available — web server is not running or stream token not received.</source>
        <translation>预览不可用 — Web 服务器未运行或未收到流令牌。</translation>
    </message>
    <message>
        <location line="+31"/>
        <source>Open File not available — web server is not running or stream token not received.</source>
        <translation>打开文件不可用 — Web 服务器未运行或未收到流令牌。</translation>
    </message>
    <message>
        <location line="+351"/>
        <source>Downloads (%1)</source>
        <translation>下载 (%1)</translation>
    </message>
    <message>
        <location line="-11"/>
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
        <location line="+16"/>
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
        <location line="+47"/>
        <source>Are you sure you want to cancel every download in &quot;%1&quot;?</source>
        <translation>确定要取消“%1”中的所有下载吗？</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Resume next file</source>
        <translation>恢复下一个文件</translation>
    </message>
    <message>
        <source>Open Incoming Folder</source>
        <translation type="obsolete">打开接收文件夹</translation>
    </message>
    <message>
        <location line="+120"/>
        <location line="+81"/>
        <source>Add To Friends</source>
        <translation>添加到好友</translation>
    </message>
    <message>
        <location line="-59"/>
        <location line="+7"/>
        <location line="+76"/>
        <location line="+3"/>
        <source>Send Message</source>
        <translation>发送消息</translation>
    </message>
    <message>
        <location line="-79"/>
        <location line="+79"/>
        <source>Message:</source>
        <translation>消息:</translation>
    </message>
    <message>
        <location line="-68"/>
        <location line="+81"/>
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
        <location filename="../src/gui/panels/UsenetPanel.cpp" line="+164"/>
        <location line="+7"/>
        <location line="+44"/>
        <location line="+483"/>
        <source>Add NZB</source>
        <translation>添加 NZB</translation>
    </message>
    <message>
        <location line="-533"/>
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
        <location line="+938"/>
        <source>Pause</source>
        <translation>暂停</translation>
    </message>
    <message>
        <location line="-934"/>
        <location line="+937"/>
        <source>Resume</source>
        <translation>恢复</translation>
    </message>
    <message>
        <location line="-935"/>
        <source>Remove</source>
        <translation>删除</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+893"/>
        <source>Pause All</source>
        <translation>全部暂停</translation>
    </message>
    <message>
        <location line="-886"/>
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
        <location line="+261"/>
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
        <location line="+9"/>
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
        <location line="+55"/>
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
        <location line="+39"/>
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
        <location line="+22"/>
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
        <location filename="../src/gui/controls/UsenetQueueModel.cpp" line="+114"/>
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
        <location line="+61"/>
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
        <location filename="../src/core/webserver/WebServer.cpp" line="-3593"/>
        <location line="+3485"/>
        <location line="+45"/>
        <source>Session expired — log in again</source>
        <translation>会话已过期 — 请重新登录</translation>
    </message>
    <message>
        <location line="-3528"/>
        <source>Guests cannot add downloads</source>
        <translation>访客无法添加下载</translation>
    </message>
    <message>
        <location line="+859"/>
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
        <location line="+403"/>
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
        <source>Download</source>
        <translation>下载</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Play</source>
        <translation>播放</translation>
    </message>
    <message>
        <location line="+91"/>
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
        <location line="+322"/>
        <source>Access denied — no password configured. Set a password in Options → Web Interface.</source>
        <translation>拒绝访问 — 未设置密码。请在“选项 → Web 界面”中设置密码。</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Login failed</source>
        <translation>登录失败</translation>
    </message>
    <message>
        <location line="+205"/>
        <location line="+1819"/>
        <source>Web Control Panel</source>
        <translation>Web 控制面板</translation>
    </message>
    <message>
        <location line="-1810"/>
        <source>Not connected</source>
        <translation>未连接</translation>
    </message>
    <message>
        <location line="+289"/>
        <source>Connected</source>
        <translation>已连接</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+283"/>
        <source>Disconnected</source>
        <translation>已断开</translation>
    </message>
    <message>
        <location line="-84"/>
        <source>Active Connections</source>
        <translation>活跃连接</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Time</source>
        <translation>时间</translation>
    </message>
    <message>
        <location line="+29"/>
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
        <location line="+34"/>
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
        <location line="+1036"/>
        <source>Queued</source>
        <translation>排队中</translation>
    </message>
    <message>
        <location line="-1035"/>
        <location line="+1034"/>
        <source>Downloading</source>
        <translation>正在下载</translation>
    </message>
    <message>
        <location line="-1033"/>
        <source>Paused</source>
        <translation>已暂停</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+867"/>
        <location line="+164"/>
        <source>Complete</source>
        <translation>已完成</translation>
    </message>
    <message>
        <location line="-1030"/>
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
        <location line="+97"/>
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
        <location line="+120"/>
        <location line="+165"/>
        <source>priority must be a number from -2 to 2</source>
        <translation>优先级必须是 -2 到 2 之间的数字</translation>
    </message>
    <message>
        <location line="-279"/>
        <location line="+120"/>
        <location line="+74"/>
        <location line="+78"/>
        <source>Unknown category</source>
        <translation>未知分类</translation>
    </message>
    <message>
        <location line="-260"/>
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
        <location line="+241"/>
        <source>Post the .nzb as the request body, or give a url</source>
        <translation>请将 .nzb 作为请求正文发送，或提供 URL</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Guests cannot change downloads</source>
        <translation>访客无法更改下载</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Nothing selected</source>
        <translation>未选择任何项</translation>
    </message>
    <message>
        <location line="+49"/>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>All</source>
        <translation>全部</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>No category</source>
        <translation>无分类</translation>
    </message>
    <message numerus="yes">
        <location line="+115"/>
        <location line="+164"/>
        <source>%n article(s) missing</source>
        <translation>
            <numerusform>缺少 %n 篇文章</numerusform>
        </translation>
    </message>
    <message>
        <location line="-100"/>
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
        <location line="+22"/>
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
        <location line="+1"/>
        <source>CD Image (.iso .bin ...)</source>
        <translation>光盘映像 (.iso .bin ...)</translation>
    </message>
    <message>
        <location line="+1"/>
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
        <location line="+1"/>
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
        <location line="+1"/>
        <source>Download Speed</source>
        <translation>下载速度</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebServer.cpp" line="-1358"/>
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
        <location line="+1"/>
        <source>Image (.jpg .png ...)</source>
        <translation>图片 (.jpg .png ...)</translation>
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
        <location filename="../src/core/webserver/WebServer.cpp" line="-1240"/>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
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
        <location filename="../src/core/webserver/WebServer.cpp" line="+1237"/>
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
        <location filename="../src/core/webserver/WebServer.cpp" line="+483"/>
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
