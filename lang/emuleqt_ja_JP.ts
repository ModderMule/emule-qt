<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="ja_JP">
<context>
    <name>ContainerSniffer</name>
    <message>
        <location filename="../src/core/media/ContainerSniffer.cpp" line="+175"/>
        <source>Named .%1 but matches no media container we recognise — very likely a fake.</source>
        <translation>.%1 という名前ですが、認識できるメディアコンテナではありません — 偽物の可能性が非常に高いです。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Named .%1 but the contents are %2.</source>
        <translation>.%1 という名前ですが、中身は %2 です。</translation>
    </message>
</context>
<context>
    <name>Ed2kLinkImporter</name>
    <message>
        <location filename="../src/gui/utils/Ed2kLinkImporter.cpp" line="+187"/>
        <source>already shared</source>
        <translation>共有済み</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>already downloading</source>
        <translation>ダウンロード中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>already downloaded</source>
        <translation>ダウンロード済み</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>previously cancelled</source>
        <translation>以前にキャンセル済み</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>You already have the file &quot;%1&quot;.</source>
        <translation>ファイル「%1」はすでに存在します。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>You are already trying to download the file &quot;%1&quot;.</source>
        <translation>ファイル「%1」はすでにダウンロード中です。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>You previously cancelled the download of &quot;%1&quot;.</source>
        <translation>「%1」のダウンロードは以前にキャンセルされました。</translation>
    </message>
    <message numerus="yes">
        <location line="+85"/>
        <source>%n further HTTP Cache link(s) ignored — apply one at a time.</source>
        <translation>
            <numerusform>さらに %n 件の HTTP キャッシュリンクを無視しました — 一度に 1 件ずつ適用してください。</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="+104"/>
        <source>%n eD2K link(s) not added — already known</source>
        <translation>
            <numerusform>%n 件の eD2K リンクは追加されませんでした — すでに登録済みです</numerusform>
        </translation>
    </message>
    <message>
        <location line="+9"/>
        <source>eD2K Link</source>
        <translation>eD2K リンク</translation>
    </message>
</context>
<context>
    <name>HttpCacheLinkImporter</name>
    <message>
        <location filename="../src/gui/utils/HttpCacheLinkImporter.cpp" line="+44"/>
        <source>The core did not answer.</source>
        <translation>コアが応答しませんでした。</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>Server: %1</source>
        <translation>サーバー：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Key: %1</source>
        <translation>鍵：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Version %1%2</source>
        <translation>バージョン %1%2</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>This server also accepts uploads without a key.</source>
        <translation>このサーバーは鍵なしのアップロードも受け付けます。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>This replaces the entry already stored for %1.</source>
        <translation>%1 に既に保存されているエントリを置き換えます。</translation>
    </message>
    <message numerus="yes">
        <location line="+3"/>
        <source>Uploads are shared across your cache servers; this makes %n of them.</source>
        <translation>
            <numerusform>アップロードはキャッシュサーバー間で分散されます。これで %n 台になります。</numerusform>
        </translation>
    </message>
    <message>
        <location line="+10"/>
        <source>

This link uses plain HTTP. The key and every chunk address will cross the network unencrypted.</source>
        <translation>

このリンクは平文の HTTP を使用します。鍵とすべてのチャンクのアドレスが暗号化されずにネットワークを流れます。</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Update your HTTP Cache settings for &quot;%1&quot;?</source>
        <translation>「%1」の HTTP キャッシュ設定を更新しますか？</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Add &quot;%1&quot; as an HTTP Cache server?</source>
        <translation>「%1」を HTTP キャッシュサーバーとして追加しますか？</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Use &quot;%1&quot; as your HTTP Cache server?</source>
        <translation>「%1」を HTTP キャッシュサーバーとして使用しますか？</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+26"/>
        <source>HTTP Cache</source>
        <translation>HTTP キャッシュ</translation>
    </message>
    <message>
        <location line="-23"/>
        <source>

HTTP Cache will be enabled and this key stored for uploads.</source>
        <translation>

HTTP キャッシュが有効になり、この鍵がアップロード用に保存されます。</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>HTTP Cache link refused: %1</source>
        <translation>HTTP キャッシュリンクを拒否しました：%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+29"/>
        <source>HTTP Cache link refused</source>
        <translation>HTTP キャッシュリンクを拒否しました</translation>
    </message>
    <message>
        <location line="-16"/>
        <location line="+2"/>
        <source>HTTP Cache is already configured for %1.</source>
        <translation>HTTP キャッシュは %1 用に既に設定されています。</translation>
    </message>
    <message numerus="yes">
        <location line="+10"/>
        <source>You already have %n HTTP Cache server(s) configured. Remove one from preferences.yml before adding another.</source>
        <translation>
            <numerusform>HTTP キャッシュサーバーは既に %n 台設定されています。別のサーバーを追加する前に preferences.yml から 1 台削除してください。</numerusform>
        </translation>
    </message>
    <message>
        <location line="+17"/>
        <source>HTTP Cache configuration for %1 was not applied.</source>
        <translation>%1 の HTTP キャッシュ設定は適用されませんでした。</translation>
    </message>
    <message>
        <location line="+22"/>
        <location line="+2"/>
        <source>HTTP Cache configured for %1.</source>
        <translation>%1 の HTTP キャッシュを設定しました。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>HTTP Cache configuration failed: %1</source>
        <translation>HTTP キャッシュの設定に失敗しました：%1</translation>
    </message>
</context>
<context>
    <name>IpcFeedback</name>
    <message>
        <location filename="../src/gui/utils/IpcFeedback.cpp" line="+27"/>
        <source>The request was rejected by eMule.</source>
        <translation>リクエストは eMule によって拒否されました。</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Not connected to daemon — %1</source>
        <translation>デーモンに接続していません — %1</translation>
    </message>
</context>
<context>
    <name>PortChange</name>
    <message>
        <location filename="../src/gui/dialogs/PortChangeNotice.h" line="+42"/>
        <source>Now listening on TCP port %1 and UDP port %2.</source>
        <translation>TCP ポート %1 と UDP ポート %2 で待ち受けています。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The new ports take effect after restarting eMule, because the core is connected to a network or to other clients.

Until then it keeps listening on TCP port %1 and UDP port %2.</source>
        <translation>コアがネットワークまたは他のクライアントに接続しているため、新しいポートは eMule の再起動後に有効になります。

それまでは TCP ポート %1 と UDP ポート %2 で待ち受けを続けます。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>The new port could not be opened. It may be in use by another program.

The core keeps listening on TCP port %1 and UDP port %2.</source>
        <translation>新しいポートを開けませんでした。他のプログラムが使用している可能性があります。

コアは TCP ポート %1 と UDP ポート %2 で待ち受けを続けます。</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Ports</source>
        <translation>ポート</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Restart Now</source>
        <translation>今すぐ再起動</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Later</source>
        <translation>後で</translation>
    </message>
</context>
<context>
    <name>PortMapStatus</name>
    <message>
        <location filename="../src/gui/dialogs/PortMapStatusText.h" line="+37"/>
        <source>Ports forwarded via %1.</source>
        <translation>%1 でポートを転送しました。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ports forwarded via %1 (external address %2).</source>
        <translation>%1 でポートを転送しました (外部アドレス %2)。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>The router granted the ports, but they are not reachable from the Internet.</source>
        <translation>ルーターはポートを許可しましたが、インターネットからは到達できません。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The router granted the ports, but its address %1 is not public, so they stay unreachable over IPv4.</source>
        <translation>ルーターはポートを許可しましたが、そのアドレス %1 は公開アドレスではないため、IPv4 では到達できません。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>No router answered PCP, NAT-PMP or UPnP. Forward the ports manually.</source>
        <translation>PCP、NAT-PMP、UPnP に応答するルーターがありません。ポートを手動で転送してください。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>The router refused the port forwarding.</source>
        <translation>ルーターがポート転送を拒否しました。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Automatic port forwarding is switched off.</source>
        <translation>自動ポート転送はオフになっています。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Asking the router...</source>
        <translation>ルーターに問い合わせ中...</translation>
    </message>
</context>
<context>
    <name>PreviewLauncher</name>
    <message>
        <location filename="../src/gui/utils/PreviewLauncher.cpp" line="+227"/>
        <source>Not connected to the core.</source>
        <translation>コアに接続されていません。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The core has not sent its stream token yet. It arrives with the next status update — try again in a moment.</source>
        <translation>コアはまだストリームトークンを送信していません。次のステータス更新で届きます — 少し待ってからもう一度お試しください。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>The core runs on another machine and its web server only listens on localhost.

Enable Web Interface or REST API under Options → Web Interface.</source>
        <translation>コアは別のマシンで動作しており、その Web サーバーは localhost でのみ待ち受けています。

オプション → Web インターフェース で Web インターフェースまたは REST API を有効にしてください。</translation>
    </message>
</context>
<context>
    <name>Priority</name>
    <message>
        <location filename="../src/gui/utils/PriorityText.cpp" line="+20"/>
        <source>Very Low</source>
        <translation>非常に低い</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Auto [Lo]</source>
        <translation>自動 [低]</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Low</source>
        <translation>低い</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Auto [Hi]</source>
        <translation>自動 [高]</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>High</source>
        <translation>高い</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Release</source>
        <translation>リリース</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Very High</source>
        <translation>非常に高い</translation>
    </message>
    <message>
        <location line="-24"/>
        <source>Auto [No]</source>
        <translation>自動 [通常]</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Normal</source>
        <translation>通常</translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <location filename="../src/gui/utils/Ed2kLinkImporter.cpp" line="-288"/>
        <location line="+20"/>
        <source>eD2K Link</source>
        <translation>eD2K リンク</translation>
    </message>
    <message>
        <location line="-19"/>
        <source>Do you want to download the following file(s)?

%1</source>
        <translation>以下のファイルをダウンロードしますか？

%1</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>You have already downloaded the following file(s). Download them again?

%1</source>
        <translation>次のファイルはすでにダウンロード済みです。もう一度ダウンロードしますか？

%1</translation>
    </message>
    <message>
        <location filename="../src/gui/app/main.cpp" line="+638"/>
        <source>Download Added</source>
        <translation>ダウンロード追加</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>A new download has been added.</source>
        <translation>新しいダウンロードが追加されました。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Chat Message from %1</source>
        <translation>%1 からのチャットメッセージ</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Log Entry</source>
        <translation>ログエントリ</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Connection Lost</source>
        <translation>接続が失われました</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server connection has been lost.</source>
        <translation>サーバー接続が失われました。</translation>
    </message>
    <message>
        <source>Very Low</source>
        <translation type="vanished">非常に低い</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/UsenetQueueModel.cpp" line="+200"/>
        <source>Skipped</source>
        <translation>スキップ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Held back — fetched if a repair needs it</source>
        <translation>保留中 — 修復に必要な場合に取得します</translation>
    </message>
    <message numerus="yes">
        <location line="+2"/>
        <location line="+4"/>
        <source>%n article(s) missing</source>
        <translation>
            <numerusform>%n 件の記事が欠落</numerusform>
        </translation>
    </message>
    <message>
        <location line="-3"/>
        <source>Complete</source>
        <translation>完了</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Queued</source>
        <translation>キュー待ち</translation>
    </message>
    <message>
        <location line="+424"/>
        <source>Very high</source>
        <translation>非常に高い</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Low</source>
        <translation>低い</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Very low</source>
        <translation>非常に低い</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Normal</source>
        <translation>通常</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>High</source>
        <translation>高い</translation>
    </message>
    <message>
        <source>Very High</source>
        <translation type="vanished">非常に高い</translation>
    </message>
    <message>
        <source>Auto [%1]</source>
        <translation type="vanished">自動 [%1]</translation>
    </message>
    <message>
        <location filename="../src/gui/utils/RatingIcons.cpp" line="+59"/>
        <source>Fake</source>
        <translation>偽物</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Poor</source>
        <translation>悪い</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Fair</source>
        <translation>普通</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Good</source>
        <translation>良い</translation>
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
評価:	%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>
Has comments</source>
        <translation>
コメントあり</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/ClientListModel.cpp" line="+56"/>
        <source>Server</source>
        <translation>サーバー</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Kad</source>
        <translation>Kad</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Source Exch.</source>
        <translation>ソース交換</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/gui/controls/DownloadListModel.cpp" line="+70"/>
        <source>Passive</source>
        <translation>パッシブ</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/gui/controls/DownloadListModel.cpp" line="+1"/>
        <source>Link</source>
        <translation>リンク</translation>
    </message>
    <message>
        <location line="+2"/>
        <location filename="../src/gui/controls/DownloadListModel.cpp" line="+2"/>
        <source>HTTP Cache</source>
        <translation>HTTP キャッシュ</translation>
    </message>
    <message>
        <source>Yes</source>
        <translation type="vanished">はい</translation>
    </message>
    <message>
        <source>Never</source>
        <translation type="vanished">なし</translation>
    </message>
    <message>
        <source>Archive</source>
        <translation type="vanished">アーカイブ</translation>
    </message>
    <message>
        <source>Audio</source>
        <translation type="vanished">オーディオ</translation>
    </message>
    <message>
        <source>Video</source>
        <translation type="vanished">ビデオ</translation>
    </message>
    <message>
        <source>Image</source>
        <translation type="vanished">画像</translation>
    </message>
    <message>
        <source>Program</source>
        <translation type="vanished">プログラム</translation>
    </message>
    <message>
        <source>Document</source>
        <translation type="vanished">ドキュメント</translation>
    </message>
    <message>
        <source>CD-Image</source>
        <translation type="vanished">CD イメージ</translation>
    </message>
    <message>
        <source>eMule Collection</source>
        <translation type="vanished">eMule コレクション</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/DownloadListModel.cpp" line="-6"/>
        <source>eD2K Server</source>
        <translation>eD2Kサーバー</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Kademlia</source>
        <translation>Kademlia</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Source Exchange</source>
        <translation>ソース交換</translation>
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
        <translation>不明</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/KnownTypeStyle.h" line="+22"/>
        <source>Shared</source>
        <translation>共有済み</translation>
    </message>
    <message>
        <location line="+1"/>
        <location filename="../src/gui/controls/UsenetQueueModel.cpp" line="-427"/>
        <source>Downloading</source>
        <translation>ダウンロード中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloaded</source>
        <translation>ダウンロード済み</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Cancelled</source>
        <translation>キャンセル済み</translation>
    </message>
    <message>
        <location filename="../src/gui/panels/StatisticsPanel.cpp" line="+307"/>
        <source>Total Overhead (Packets): 0 Bytes (0)</source>
        <translation>総オーバーヘッド (パケット): 0 Bytes (0)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>File Request Overhead (Packets): 0 Bytes (0)</source>
        <translation>ファイル要求オーバーヘッド (パケット): 0 Bytes (0)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Source Exchange Overhead (Packets): 0 Bytes (0)</source>
        <translation>ソース交換オーバーヘッド (パケット): 0 Bytes (0)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server Overhead (Packets): 0 Bytes (0)</source>
        <translation>サーバーオーバーヘッド (パケット): 0 Bytes (0)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Kad Overhead (Packets): 0 Bytes (0)</source>
        <translation>Kad オーバーヘッド (パケット): 0 Bytes (0)</translation>
    </message>
    <message>
        <source>%1 Bytes</source>
        <translation type="vanished">%1 Bytes</translation>
    </message>
    <message>
        <location filename="../src/gui/dialogs/OptionsDialog.cpp" line="+2918"/>
        <source>Test</source>
        <translation>テスト</translation>
    </message>
    <message>
        <location filename="../src/gui/utils/FileAssociation.cpp" line="+258"/>
        <source>Could not write the file association to the registry.</source>
        <translation>ファイルの関連付けをレジストリに書き込めませんでした。</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Could not remove the file association from the registry.</source>
        <translation>ファイルの関連付けをレジストリから削除できませんでした。</translation>
    </message>
    <message>
        <location line="+15"/>
        <location line="+31"/>
        <source>No writable data directory.</source>
        <translation>書き込み可能なデータディレクトリがありません。</translation>
    </message>
    <message>
        <location line="-23"/>
        <source>Could not create %1.</source>
        <translation>%1 を作成できませんでした。</translation>
    </message>
    <message>
        <location filename="../src/gui/utils/NzbAdd.cpp" line="+25"/>
        <source>Could not add &quot;%1&quot;.</source>
        <translation>「%1」を追加できませんでした。</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+42"/>
        <location line="+10"/>
        <source>Add NZB</source>
        <translation>NZBを追加</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1

Download it again?</source>
        <translation>%1

もう一度ダウンロードしますか？</translation>
    </message>
</context>
<context>
    <name>Rating</name>
    <message>
        <location filename="../src/core/utils/OtherFunctions.cpp" line="+528"/>
        <source>Not rated</source>
        <translation>未評価</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Invalid / Corrupt / Fake</source>
        <translation>無効 / 破損 / 偽物</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Poor</source>
        <translation>悪い</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Fair</source>
        <translation>普通</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Good</source>
        <translation>良い</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Excellent</source>
        <translation>優秀</translation>
    </message>
</context>
<context>
    <name>Units</name>
    <message>
        <location filename="../src/core/utils/StringUtils.cpp" line="+72"/>
        <location filename="../src/core/webserver/WebServer.cpp" line="+4012"/>
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
        <translation>時間</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>d</source>
        <translation>日</translation>
    </message>
</context>
<context>
    <name>UsenetDetailsDialog</name>
    <message numerus="yes">
        <location filename="../src/gui/dialogs/UsenetDetailsDialog.cpp" line="+66"/>
        <source>%1 (and %n other(s))</source>
        <translation>
            <numerusform>%1 (ほか %n 件)</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <source>%n article(s) missing</source>
        <translation type="vanished">
            <numerusform>%n 件の記事が欠落</numerusform>
        </translation>
    </message>
    <message>
        <source>Complete</source>
        <translation type="vanished">完了</translation>
    </message>
    <message>
        <source>Downloading</source>
        <translation type="vanished">ダウンロード中</translation>
    </message>
    <message>
        <source>Queued</source>
        <translation type="vanished">キュー待ち</translation>
    </message>
</context>
<context>
    <name>eMule::AddFriendDialog</name>
    <message>
        <location filename="../src/gui/dialogs/AddFriendDialog.cpp" line="+25"/>
        <source>Add...</source>
        <translation>追加...</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Required Information</source>
        <translation>必須情報</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>IP Address:</source>
        <translation>IP アドレス：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Port:</source>
        <translation>ポート：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Additional Information</source>
        <translation>追加情報</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Name:</source>
        <translation>名前：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Hash:</source>
        <translation>ハッシュ：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Unknown</source>
        <translation>不明</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>KadID:</source>
        <translation>Kad ID：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Last Seen:</source>
        <translation>最後に確認：</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Add</source>
        <translation>追加</translation>
    </message>
    <message>
        <location line="+38"/>
        <source>Friend Details</source>
        <translation>フレンドの詳細</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Never</source>
        <translation>なし</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Close</source>
        <translation>閉じる</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+6"/>
        <source>Add Friend</source>
        <translation>フレンドを追加</translation>
    </message>
    <message>
        <location line="-5"/>
        <source>Please enter an IP address.</source>
        <translation>IP アドレスを入力してください。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Please enter a valid port (1-65535).</source>
        <translation>有効なポート（1-65535）を入力してください。</translation>
    </message>
</context>
<context>
    <name>eMule::AddNzbFilesDialog</name>
    <message>
        <location filename="../src/gui/dialogs/AddNzbFilesDialog.cpp" line="+15"/>
        <source>Add NZB</source>
        <translation>NZBを追加</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>NZB files:</source>
        <translation>NZB ファイル:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Add</source>
        <translation>追加</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Password:</source>
        <translation>パスワード：</translation>
    </message>
    <message>
        <location line="+18"/>
        <location line="+31"/>
        <source>Choose Files…</source>
        <translation>ファイルを選択…</translation>
    </message>
    <message numerus="yes">
        <location line="-1"/>
        <source>Choose Files… (%n skipped)</source>
        <translation>
            <numerusform>ファイルを選択… (%n 件スキップ)</numerusform>
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
        <translation>URLからNZBを追加</translation>
    </message>
    <message>
        <location line="-127"/>
        <source>NZB URLs:</source>
        <translation>NZB の URL:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paste one or more http(s) links to .nzb files here, one per line...</source>
        <translation>.nzb ファイルへの http(s) リンクを 1 行に 1 つずつここに貼り付けてください...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Download</source>
        <translation>ダウンロード</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Password:</source>
        <translation>パスワード：</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Not Connected</source>
        <translation>未接続</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not connected to the eMule core.</source>
        <translation>eMule コアに接続されていません。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Please add at most %1 links at a time.</source>
        <translation>一度に追加できるリンクは %1 件までです。</translation>
    </message>
    <message numerus="yes">
        <location line="+23"/>
        <source>Queued %n NZB(s) from URL.</source>
        <translation>
            <numerusform>URL から %n 個の NZB をキューに追加しました。</numerusform>
        </translation>
    </message>
    <message>
        <location line="+8"/>
        <source>These links could not be added:

%1</source>
        <translation>次のリンクは追加できませんでした:

%1</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Could not reach the eMule core.</source>
        <translation>eMule コアに接続できませんでした。</translation>
    </message>
    <message numerus="yes">
        <location line="+39"/>
        <source>You have already downloaded %n of these. Download them again?

%1</source>
        <translation>
            <numerusform>このうち %n 件はすでにダウンロード済みです。もう一度ダウンロードしますか？

%1</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>eMule::AddSourceDialog</name>
    <message>
        <location filename="../src/gui/dialogs/AddSourceDialog.cpp" line="+23"/>
        <source>Add Sources</source>
        <translation>ソースを追加</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add Sources (%1)</source>
        <translation>ソースを追加 (%1)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Source Type</source>
        <translation>ソースの種類</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Source</source>
        <translation>ソース</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>User IP</source>
        <translation>ユーザー IP</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Port</source>
        <translation>ポート</translation>
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
        <translation>追加</translation>
    </message>
</context>
<context>
    <name>eMule::ArchivePreviewPanel</name>
    <message>
        <location filename="../src/gui/dialogs/ArchivePreviewPanel.cpp" line="+69"/>
        <source>Scanning...</source>
        <translation>スキャン中...</translation>
    </message>
    <message>
        <location line="+92"/>
        <location line="+37"/>
        <location line="+18"/>
        <source>Archive type: --</source>
        <translation>アーカイブの種類：--</translation>
    </message>
    <message>
        <location line="-54"/>
        <location line="+17"/>
        <location line="+41"/>
        <source>Ready</source>
        <translation>準備完了</translation>
    </message>
    <message>
        <location line="-25"/>
        <source>Archive type: %1</source>
        <translation>アーカイブの種類：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>No entries found or unsupported format</source>
        <translation>エントリが見つからないか、サポートされていない形式です</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Files: %1</source>
        <translation>ファイル：%1</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Create Preview Copy</source>
        <translation>プレビューコピーを作成</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Update</source>
        <translation>更新</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Name</source>
        <translation>名前</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Size</source>
        <translation>サイズ</translation>
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
        <translation>最終更新</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Comment</source>
        <translation>コメント</translation>
    </message>
</context>
<context>
    <name>eMule::BugReportDialog</name>
    <message>
        <location filename="../src/gui/dialogs/BugReportDialog.cpp" line="+54"/>
        <location line="+133"/>
        <location line="+128"/>
        <source>Submit Bug Report</source>
        <translation>バグ報告を送信</translation>
    </message>
    <message>
        <location line="-256"/>
        <source>Report Details</source>
        <translation>報告の詳細</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Bug Report</source>
        <translation>バグ報告</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Feature Request</source>
        <translation>機能リクエスト</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type:</source>
        <translation>種類：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Brief summary of the issue</source>
        <translation>問題の簡単な要約</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Title:</source>
        <translation>タイトル：</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+4"/>
        <source>(optional)</source>
        <translation>(任意)</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>Name:</source>
        <translation>名前：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Email:</source>
        <translation>メール:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Description</source>
        <translation>説明</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Describe the issue in detail...</source>
        <translation>問題を詳しく説明してください...</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Attachments</source>
        <translation>添付ファイル</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Screenshots:</source>
        <translation>スクリーンショット:</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Add...</source>
        <translation>追加...</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+13"/>
        <source>Remove</source>
        <translation>削除</translation>
    </message>
    <message>
        <location line="-6"/>
        <source>Crash Dump:</source>
        <translation>クラッシュダンプ:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Browse...</source>
        <translation>参照...</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Alternatively, you can submit bug reports at &lt;a href=&quot;%1&quot;&gt;emule-qt.org/submit-bug-report&lt;/a&gt;</source>
        <translation>&lt;a href=&quot;%1&quot;&gt;emule-qt.org/submit-bug-report&lt;/a&gt; からバグを報告することもできます</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Submit</source>
        <translation>送信</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Please fill in both the title and description fields.</source>
        <translation>タイトルと説明の両方を入力してください。</translation>
    </message>
    <message>
        <location line="+123"/>
        <source>Bug report submitted successfully.</source>
        <translation>バグ報告を送信しました。</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Error: %1</source>
        <translation>エラー: %1</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Select Screenshots</source>
        <translation>スクリーンショットを選択</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp)</source>
        <translation>画像 (*.png *.jpg *.jpeg *.bmp *.gif *.webp)</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Invalid Files</source>
        <translation>無効なファイル</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The following files are not valid images and were skipped:
%1</source>
        <translation>次のファイルは有効な画像ではないためスキップされました:
%1</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Select Crash Dump</source>
        <translation>クラッシュダンプを選択</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Dump Files (*.dmp *.crash *.txt);;All Files (*)</source>
        <translation>ダンプファイル (*.dmp *.crash *.txt);;すべてのファイル (*)</translation>
    </message>
</context>
<context>
    <name>eMule::CategoryDialog</name>
    <message>
        <location filename="../src/gui/dialogs/CategoryDialog.cpp" line="+34"/>
        <source>Edit Category-Properties</source>
        <translation>カテゴリのプロパティを編集</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Title</source>
        <translation>タイトル</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Comments</source>
        <translation>コメント</translation>
    </message>
    <message>
        <location line="+13"/>
        <location line="+68"/>
        <source>Choose a folder for incoming files</source>
        <translation>受信ファイル用のフォルダを選択</translation>
    </message>
    <message>
        <location line="-64"/>
        <source>Incoming Files  (Folder will be shared!)</source>
        <translation>受信ファイル  (フォルダは共有されます！)</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Low</source>
        <translation>低い</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Normal</source>
        <translation>通常</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>High</source>
        <translation>高い</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Priority for this category</source>
        <translation>このカテゴリの優先度</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+56"/>
        <source>Color</source>
        <translation>色</translation>
    </message>
    <message>
        <location line="-53"/>
        <source>Auto cat. assignment (separate patterns with |)</source>
        <translation>カテゴリの自動割り当て（パターンは | で区切ります）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>As Regular Expression</source>
        <translation>正規表現として</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Used when the tab&apos;s view filter is set to &quot;Regular Expression&quot;; it must match the whole file name.</source>
        <translation>タブの表示フィルタが &quot;正規表現&quot; のときに使用されます。ファイル名全体に一致する必要があります。</translation>
    </message>
    <message>
        <source>Stored for compatibility — the per-category view filter is not implemented yet.</source>
        <translation type="vanished">互換性のために保存されます — カテゴリごとの表示フィルターはまだ実装されていません。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Regular expression for view filter:</source>
        <translation>表示フィルターの正規表現:</translation>
    </message>
    <message>
        <location line="+55"/>
        <source>A category needs a title.</source>
        <translation>カテゴリにはタイトルが必要です。</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Invalid folder. Folder can not be created. Please check name and location.</source>
        <translation>無効なフォルダです。フォルダを作成できません。名前と場所を確認してください。</translation>
    </message>
    <message>
        <location line="+10"/>
        <location line="+6"/>
        <source>Bad regular expression</source>
        <translation>正規表現が正しくありません</translation>
    </message>
    <message>
        <location line="+34"/>
        <source>Default</source>
        <translation>既定</translation>
    </message>
</context>
<context>
    <name>eMule::CategoryTabBar</name>
    <message>
        <location filename="../src/gui/controls/CategoryTabBar.cpp" line="+30"/>
        <location line="+47"/>
        <location line="+63"/>
        <location line="+20"/>
        <location line="+37"/>
        <source>All</source>
        <translation>すべて</translation>
    </message>
    <message>
        <location line="-113"/>
        <source>Cat %1</source>
        <translation>カテゴリ %1</translation>
    </message>
    <message>
        <location line="+77"/>
        <source>Uncategorized</source>
        <translation>未分類</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Incomplete</source>
        <translation>未完了</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Completed</source>
        <translation>完了</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Waiting</source>
        <translation>待機中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloading</source>
        <translation>ダウンロード中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Erroneous</source>
        <translation>エラー</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paused</source>
        <translation>一時停止</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ever Seen Complete</source>
        <translation>完全な状態を確認済み</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Video</source>
        <translation>ビデオ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Audio</source>
        <translation>オーディオ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Archive</source>
        <translation>アーカイブ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>CD-Image</source>
        <translation>CD イメージ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Document</source>
        <translation>ドキュメント</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Picture</source>
        <translation>画像</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Program</source>
        <translation>プログラム</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Regular Expression</source>
        <translation>正規表現</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Collection</source>
        <translation>コレクション</translation>
    </message>
    <message>
        <location line="+69"/>
        <source>Select View Filter</source>
        <translation>表示フィルタを選択</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Evaluate All Files</source>
        <translation>すべてのファイルを評価</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Negate Filter</source>
        <translation>フィルタを反転</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Category</source>
        <translation>カテゴリ</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Category (%1)</source>
        <translation>カテゴリ (%1)</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Open Incoming Folder</source>
        <translation>受信フォルダを開く</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Add Category...</source>
        <translation>カテゴリを追加...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Edit Category...</source>
        <translation>カテゴリを編集...</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+159"/>
        <source>Remove Category</source>
        <translation>カテゴリを削除</translation>
    </message>
    <message>
        <location line="-61"/>
        <source>Could not save categories: %1</source>
        <translation>カテゴリを保存できませんでした: %1</translation>
    </message>
    <message>
        <location line="+62"/>
        <source>Remove the category &quot;%1&quot;?

Its downloads keep their files and move to All.</source>
        <translation>カテゴリ「%1」を削除しますか?

そのダウンロードはファイルを保持したまま「すべて」に移動します。</translation>
    </message>
</context>
<context>
    <name>eMule::ClientDetailDialog</name>
    <message>
        <location filename="../src/gui/dialogs/ClientDetailDialog.cpp" line="+65"/>
        <source>Client Details: %1</source>
        <translation>クライアント詳細: %1</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>General</source>
        <translation>全般</translation>
    </message>
    <message>
        <source>User Name</source>
        <translation type="vanished">ユーザー名</translation>
    </message>
    <message>
        <source>User Hash</source>
        <translation type="vanished">ユーザーハッシュ</translation>
    </message>
    <message>
        <location line="+14"/>
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
        <translation type="vanished">クライアントソフトウェア</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Country</source>
        <translation>国</translation>
    </message>
    <message>
        <source>Server</source>
        <translation type="vanished">サーバー</translation>
    </message>
    <message>
        <location line="-19"/>
        <source>Name</source>
        <translation>名前</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hash</source>
        <translation>ハッシュ</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Software</source>
        <translation>ソフトウェア</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Server IP</source>
        <translation>サーバー IP</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server Name</source>
        <translation>サーバー名</translation>
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
        <translation>無効</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+5"/>
        <source>Not supported or disabled</source>
        <translation>未対応または無効</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>Obfuscation</source>
        <translation>難読化</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Enabled</source>
        <translation>有効</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Supported</source>
        <translation>対応</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Kad</source>
        <translation>Kad</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Connected</source>
        <translation>接続済み</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Disconnected</source>
        <translation>未接続</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Currently downloading</source>
        <translation>現在ダウンロード中</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Requested to upload</source>
        <translation>アップロードを要求</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Downloaded (this session)</source>
        <translation>ダウンロード済み (このセッション)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Uploaded (this session)</source>
        <translation>アップロード済み (このセッション)</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Average Downloadrate</source>
        <translation>平均ダウンロード速度</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Average Upload rate</source>
        <translation>平均アップロード速度</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Downloaded total</source>
        <translation>ダウンロード合計</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Uploaded total</source>
        <translation>アップロード合計</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Rating (total)</source>
        <translation>評価 (合計)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>(Friend Slot)</source>
        <translation>(フレンドスロット)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Uploadqueue Score</source>
        <translation>アップロードキューのスコア</translation>
    </message>
    <message>
        <source>Not connected</source>
        <translation type="vanished">未接続</translation>
    </message>
    <message>
        <location line="-63"/>
        <source>Transfer</source>
        <translation>転送</translation>
    </message>
    <message>
        <source>Currently Downloading</source>
        <translation type="vanished">現在ダウンロード中</translation>
    </message>
    <message>
        <source>Currently Uploading</source>
        <translation type="vanished">現在アップロード中</translation>
    </message>
    <message>
        <source>Downloaded (Session)</source>
        <translation type="vanished">ダウンロード済み (セッション)</translation>
    </message>
    <message>
        <source>Uploaded (Session)</source>
        <translation type="vanished">アップロード済み (セッション)</translation>
    </message>
    <message>
        <source>Download Rate</source>
        <translation type="vanished">ダウンロード速度</translation>
    </message>
    <message>
        <source>Downloaded (Total)</source>
        <translation type="vanished">ダウンロード済み (合計)</translation>
    </message>
    <message>
        <source>Uploaded (Total)</source>
        <translation type="vanished">アップロード済み (合計)</translation>
    </message>
    <message>
        <location line="+38"/>
        <source>Scores</source>
        <translation>スコア</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>DL/UP Modifier</source>
        <translation>DL/UP 修正値</translation>
    </message>
    <message>
        <source>Rating (Total)</source>
        <translation type="vanished">評価 (合計)</translation>
    </message>
    <message>
        <source>Upload Queue Score</source>
        <translation type="vanished">アップロードキューのスコア</translation>
    </message>
    <message>
        <source>Friend Slot</source>
        <translation type="vanished">フレンドスロット</translation>
    </message>
    <message>
        <source>Yes</source>
        <translation type="vanished">はい</translation>
    </message>
</context>
<context>
    <name>eMule::ClientListModel</name>
    <message>
        <location filename="../src/gui/controls/ClientListModel.cpp" line="+233"/>
        <location line="+13"/>
        <location line="+13"/>
        <location line="+15"/>
        <source>User Name</source>
        <translation>ユーザー名</translation>
    </message>
    <message>
        <location line="-40"/>
        <location line="+14"/>
        <location line="+12"/>
        <source>File</source>
        <translation>ファイル</translation>
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
        <translation>転送済み</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Waited</source>
        <translation>待機時間</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload Time</source>
        <translation>アップロード時間</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status</source>
        <translation>ステータス</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+28"/>
        <source>Obtained Parts</source>
        <translation>取得済みパート</translation>
    </message>
    <message>
        <location line="-21"/>
        <location line="+32"/>
        <source>Software</source>
        <translation>ソフトウェア</translation>
    </message>
    <message>
        <location line="-29"/>
        <source>Available Parts</source>
        <translation>利用可能なパート</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+24"/>
        <source>Transferred Up</source>
        <translation>アップロード済み</translation>
    </message>
    <message>
        <location line="-23"/>
        <source>Source Type</source>
        <translation>ソースの種類</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>File Priority</source>
        <translation>ファイル優先度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Rating</source>
        <translation>評価</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Score</source>
        <translation>スコア</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Asked</source>
        <translation>リクエスト済み</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Last Seen</source>
        <translation>最後に確認</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Entered Queue</source>
        <translation>キューに入った</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Banned</source>
        <translation>BAN済み</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Upload Status</source>
        <translation>アップロードステータス</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Download Status</source>
        <translation>ダウンロードステータス</translation>
    </message>
    <message>
        <location line="-26"/>
        <location line="+27"/>
        <source>Transferred Down</source>
        <translation>ダウンロード済み</translation>
    </message>
    <message>
        <location line="-202"/>
        <source>Country</source>
        <translation>国</translation>
    </message>
    <message>
        <location line="+25"/>
        <location line="+36"/>
        <source>Low ID</source>
        <translation>Low ID</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+15"/>
        <source>Yes</source>
        <translation>はい</translation>
    </message>
    <message>
        <location line="-15"/>
        <location line="+15"/>
        <source>No</source>
        <translation>いいえ</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Unknown</source>
        <translation>不明</translation>
    </message>
    <message>
        <location line="+125"/>
        <source>Connected</source>
        <translation>接続済み</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hash</source>
        <translation>ハッシュ</translation>
    </message>
</context>
<context>
    <name>eMule::ClientSharedFilesDialog</name>
    <message>
        <source>Shared Files — %1</source>
        <translation type="vanished">共有ファイル — %1</translation>
    </message>
    <message>
        <source>File Name</source>
        <translation type="vanished">ファイル名</translation>
    </message>
    <message>
        <source>Size</source>
        <translation type="vanished">サイズ</translation>
    </message>
    <message>
        <source>Hash</source>
        <translation type="vanished">ハッシュ</translation>
    </message>
    <message>
        <source>Download Selected</source>
        <translation type="vanished">選択項目をダウンロード</translation>
    </message>
    <message>
        <source>Close</source>
        <translation type="vanished">閉じる</translation>
    </message>
</context>
<context>
    <name>eMule::ClientState</name>
    <message>
        <location filename="../src/gui/utils/ClientStateText.h" line="+68"/>
        <source>Unknown</source>
        <translation>不明</translation>
    </message>
</context>
<context>
    <name>eMule::CollectionCreateDialog</name>
    <message>
        <location filename="../src/gui/dialogs/CollectionCreateDialog.cpp" line="+55"/>
        <source>Modify Collection...</source>
        <translation>コレクションを編集...</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Create Collection...</source>
        <translation>コレクションを作成...</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Shared (0)</source>
        <translation>共有 (0)</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+36"/>
        <source>File Name</source>
        <translation>ファイル名</translation>
    </message>
    <message>
        <location line="-20"/>
        <source>Add to collection</source>
        <translation>コレクションに追加</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Remove from collection</source>
        <translation>コレクションから削除</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Collection List (0)</source>
        <translation>コレクション一覧 (0)</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Basic Options</source>
        <translation>基本オプション</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Name:</source>
        <translation>名前：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Advanced Options</source>
        <translation>詳細オプション</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Save collection in plain text format</source>
        <translation>コレクションをプレーンテキスト形式で保存する</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Sign collection with name and key</source>
        <translation>名前と鍵でコレクションに署名する</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Save</source>
        <translation>保存</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Cancel</source>
        <translation>キャンセル</translation>
    </message>
    <message>
        <location line="+88"/>
        <source>Shared (%1)</source>
        <translation>共有 (%1)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Collection List (%1)</source>
        <translation>コレクション一覧 (%1)</translation>
    </message>
    <message>
        <location line="+26"/>
        <location line="+6"/>
        <location line="+42"/>
        <location line="+9"/>
        <source>Collection</source>
        <translation>コレクション</translation>
    </message>
    <message>
        <location line="-57"/>
        <source>Please enter a collection name.</source>
        <translation>コレクション名を入力してください。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Collection is empty. Add files first.</source>
        <translation>コレクションが空です。先にファイルを追加してください。</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Do you want to replace existing file?</source>
        <translation>既存のファイルを置き換えますか？</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Failed to save collection: %1</source>
        <translation>コレクションを保存できませんでした: %1</translation>
    </message>
</context>
<context>
    <name>eMule::CollectionViewDialog</name>
    <message>
        <location filename="../src/gui/dialogs/CollectionViewDialog.cpp" line="+41"/>
        <source>Collection: %1</source>
        <translation>コレクション: %1</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Collection List (%1)</source>
        <translation>コレクション一覧 (%1)</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>File Name</source>
        <translation>ファイル名</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Size</source>
        <translation>サイズ</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Hash</source>
        <translation>ハッシュ</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Details</source>
        <translation>詳細</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Author:</source>
        <translation>作成者:</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Author Key:</source>
        <translation>作成者キー:</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Options</source>
        <translation>オプション</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Add to new category</source>
        <translation>新しいカテゴリに追加</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Download</source>
        <translation>ダウンロード</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Close</source>
        <translation>閉じる</translation>
    </message>
</context>
<context>
    <name>eMule::CommentEditPanel</name>
    <message>
        <location filename="../src/gui/dialogs/CommentEditPanel.cpp" line="+70"/>
        <source>Comment This File! (This text will be shown to all users.)</source>
        <translation>このファイルにコメントしましょう！（このテキストはすべてのユーザーに表示されます。）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>For a film, you can say its length, its story, the language... And if it is a fake, you can inform other eMule users...</source>
        <translation>映画なら、長さ、あらすじ、言語などを書けます... 偽物の場合は、他の eMule ユーザーに知らせることができます...</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>File Quality</source>
        <translation>ファイルの品質</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Choose the file rating or advice users if the file is invalid!</source>
        <translation>ファイルの評価を選ぶか、ファイルが無効な場合はユーザーに知らせてください！</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Reset</source>
        <translation>リセット</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Apply</source>
        <translation>適用</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Comment</source>
        <translation>コメント</translation>
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
        <location line="+9"/>
        <source>(Kad search in progress...)</source>
        <translation>（Kad を検索中...）</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+55"/>
        <source>Search Kad</source>
        <translation>Kad を検索</translation>
    </message>
    <message>
        <location line="-37"/>
        <source>Rating</source>
        <translation>評価</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Comment</source>
        <translation>コメント</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>File Name</source>
        <translation>ファイル名</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>User Name</source>
        <translation>ユーザー名</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Network</source>
        <translation>ネットワーク</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>No comments or ratings available for this file.</source>
        <translation>このファイルにはコメントや評価がありません。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Copy</source>
        <translation>コピー</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Edit spam filter...</source>
        <translation>スパムフィルターを編集...</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Edit spam filter for comments</source>
        <translation>コメントのスパムフィルターを編集</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ignore comments containing: (Separator | )</source>
        <translation>含むコメントを無視：（区切り | ）</translation>
    </message>
</context>
<context>
    <name>eMule::ConfidenceText</name>
    <message>
        <location filename="../src/core/search/ConfidenceText.h" line="+27"/>
        <source>Spam</source>
        <translation>スパム</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Likely fake</source>
        <translation>偽物の可能性が高い</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Suspect</source>
        <translation>疑わしい</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Caution: %1%</source>
        <translation>注意: %1%</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Looks good</source>
        <translation>問題なさそう</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Genuine</source>
        <translation>本物</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Shared under names that describe different content</source>
        <translation>異なる内容を表す複数の名前で共有されています</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Shared as different kinds of file (e.g. video and archive)</source>
        <translation>異なる種類のファイルとして共有されています (例: 動画とアーカイブ)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>A name matches a rule in FakeFileFilter.dat</source>
        <translation>名前が FakeFileFilter.dat のルールに一致します</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>A comment matches a rule in FakeFileFilter.dat</source>
        <translation>コメントが FakeFileFilter.dat のルールに一致します</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The first bytes are not what the extension claims</source>
        <translation>先頭のバイトが拡張子の示す形式と一致しません</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>It is a program named like a media file</source>
        <translation>メディアファイルのような名前のプログラムです</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>It is an archive named like a media file</source>
        <translation>メディアファイルのような名前のアーカイブです</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The published type does not fit the extension</source>
        <translation>公開されている種類が拡張子と一致しません</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The spam filter rates it high</source>
        <translation>スパムフィルターの評価値が高い</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Counted as spam</source>
        <translation>スパムと判定されています</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Rated poor, or a Kad note calls it a fake</source>
        <translation>評価が低い、または Kad のノートで偽物とされています</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Rated as fake by users</source>
        <translation>ユーザーから偽物と評価されています</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Sources disagree on the AICH hash</source>
        <translation>ソース間で AICH ハッシュが一致しません</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The length does not fit a file of this size</source>
        <translation>再生時間がこのサイズのファイルに見合いません</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The bitrate is not plausible</source>
        <translation>ビットレートが不自然です</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Length and bitrate do not add up to the size</source>
        <translation>再生時間とビットレートがファイルサイズと合いません</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Artist, album and title appear in none of the names</source>
        <translation>アーティスト、アルバム、タイトルがどの名前にも含まれていません</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>A name uses words that advertise child abuse material</source>
        <translation>名前に児童虐待コンテンツを宣伝する語が含まれています</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The release is password protected</source>
        <translation>リリースはパスワードで保護されています</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>%1 (fake score %2 of 100)</source>
        <translation>%1 (偽物スコア %2 / 100)</translation>
    </message>
</context>
<context>
    <name>eMule::ContactsGraph</name>
    <message>
        <location filename="../src/gui/controls/ContactsGraph.cpp" line="+87"/>
        <source>Contacts</source>
        <translation>連絡先</translation>
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
        <translation>コアに接続</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Could not find a local eMule core.
Enter the address and authentication token of a remote core.</source>
        <translation>ローカルの eMule コアが見つかりません。
リモートコアのアドレスと認証トークンを入力してください。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Remote Core</source>
        <translation>リモートコア</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Address:</source>
        <translation>アドレス：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Port:</source>
        <translation>ポート：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>paste token here</source>
        <translation>ここにトークンを貼り付け</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Token:</source>
        <translation>トークン：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Save token</source>
        <translation>トークンを保存</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Connect</source>
        <translation>接続</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Exit</source>
        <translation>終了</translation>
    </message>
</context>
<context>
    <name>eMule::DetailDialog</name>
    <message>
        <location filename="../src/gui/dialogs/DetailDialog.cpp" line="+202"/>
        <source>Search Kad</source>
        <translation>Kad を検索</translation>
    </message>
    <message>
        <location line="+103"/>
        <source>Comments</source>
        <translation>コメント</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>Previous</source>
        <translation>前へ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Next</source>
        <translation>次へ</translation>
    </message>
</context>
<context>
    <name>eMule::DownloadListModel</name>
    <message>
        <location filename="../src/gui/controls/DownloadListModel.cpp" line="+685"/>
        <source>Downloading</source>
        <translation>ダウンロード中</translation>
    </message>
    <message>
        <source>Auto [%1]</source>
        <translation type="vanished">自動 [%1]</translation>
    </message>
    <message>
        <location line="-505"/>
        <source>Queue Full</source>
        <translation>キューが満杯</translation>
    </message>
    <message>
        <location line="+47"/>
        <source>Available parts: %1 / %2</source>
        <translation>利用可能なパート: %1 / %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Country: %1</source>
        <translation>国: %1</translation>
    </message>
    <message>
        <location line="+57"/>
        <location line="+2"/>
        <source>Never</source>
        <translation>なし</translation>
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
        <translation type="vanished">ファイル名:	%1
ED2K ハッシュ:	%2
サイズ:	%3
完了:	%4 (%5%)
種類:	%6
ステータス:	%7
優先度:	%8
ソース:	%9
要求:	%10
受理された要求:	%11
転送データ:	%12</translation>
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
        <translation>ファイル名:	%1
ED2K ハッシュ:	%2
サイズ:	%3
完了:	%4 (%5%)
種類:	%6
ステータス:	%7
優先度:	%8
ソース:	%9
要求:	%10
受理された要求:	%11
アップロード済み:	%12</translation>
    </message>
    <message>
        <location line="+100"/>
        <source>File Name</source>
        <translation>ファイル名</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Size</source>
        <translation>サイズ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Completed</source>
        <translation>完了</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Speed</source>
        <translation>速度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Progress</source>
        <translation>進捗</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Sources</source>
        <translation>ソース</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority</source>
        <translation>優先度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status</source>
        <translation>ステータス</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remaining</source>
        <translation>残り</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Seen Complete</source>
        <translation>完了確認済み</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Country</source>
        <translation>国</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Last reception</source>
        <translation>最後の受信</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Category</source>
        <translation>カテゴリ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Added On</source>
        <translation>追加日</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Confidence</source>
        <translation>信頼度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Transferred</source>
        <translation>転送済み</translation>
    </message>
    <message>
        <location line="+218"/>
        <source>Importing part</source>
        <translation>パートをインポート中</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+5"/>
        <source>Hashing</source>
        <translation>ハッシュ計算中</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+1"/>
        <location line="+1"/>
        <source>Completing (%1)</source>
        <translation>完了処理中（%1）</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Copying</source>
        <translation>コピー中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Uncompressing</source>
        <translation>展開中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Completing</source>
        <translation>完了処理中</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Complete</source>
        <translation>完了</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Stopped</source>
        <translation>停止</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Paused</source>
        <translation>一時停止</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+3"/>
        <source>Insufficient disk space</source>
        <translation>ディスク容量不足</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Error</source>
        <translation>エラー</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Waiting</source>
        <translation>待機中</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>HTTP Cache</source>
        <translation>HTTP キャッシュ</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Asked for another file</source>
        <translation>別のファイルを要求</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Swap blocked</source>
        <translation>スワップ不可</translation>
    </message>
</context>
<context>
    <name>eMule::FileDetailDialog</name>
    <message>
        <location filename="../src/gui/dialogs/FileDetailDialog.cpp" line="+146"/>
        <source>File Details: %1</source>
        <translation>ファイル詳細: %1</translation>
    </message>
    <message>
        <location line="-37"/>
        <location line="+48"/>
        <location line="+7"/>
        <source>General</source>
        <translation>全般</translation>
    </message>
    <message>
        <location line="-6"/>
        <location line="+7"/>
        <source>File Names</source>
        <translation>ファイル名</translation>
    </message>
    <message>
        <location line="-55"/>
        <location line="+49"/>
        <location line="+7"/>
        <source>Comments</source>
        <translation>コメント</translation>
    </message>
    <message>
        <location line="-55"/>
        <location line="+49"/>
        <location line="+7"/>
        <source>Media Info</source>
        <translation>メディア情報</translation>
    </message>
    <message>
        <location line="-6"/>
        <location line="+7"/>
        <source>Metadata</source>
        <translation>メタデータ</translation>
    </message>
    <message>
        <location line="-56"/>
        <location line="+50"/>
        <location line="+7"/>
        <source>ED2K Link</source>
        <translation>ED2K リンク</translation>
    </message>
    <message>
        <location line="+10"/>
        <location line="+2"/>
        <source>Archive Preview</source>
        <translation>アーカイブのプレビュー</translation>
    </message>
    <message>
        <location line="+29"/>
        <location line="+20"/>
        <location line="+50"/>
        <source>File Name</source>
        <translation>ファイル名</translation>
    </message>
    <message>
        <location line="-69"/>
        <location line="+20"/>
        <source>Hash (MD4)</source>
        <translation>ハッシュ (MD4)</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>AICH Hash</source>
        <translation>AICH ハッシュ</translation>
    </message>
    <message>
        <location line="-23"/>
        <location line="+27"/>
        <source>File Size</source>
        <translation>ファイルサイズ</translation>
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
        <translation>完了</translation>
    </message>
    <message>
        <location line="-27"/>
        <location line="+33"/>
        <source>Status</source>
        <translation>ステータス</translation>
    </message>
    <message>
        <location line="-32"/>
        <location line="+33"/>
        <source>Priority</source>
        <translation>優先度</translation>
    </message>
    <message>
        <location line="-32"/>
        <location line="+37"/>
        <location line="+24"/>
        <source>Sources</source>
        <translation>ソース</translation>
    </message>
    <message>
        <location line="-19"/>
        <source>File Path</source>
        <translation>ファイルパス</translation>
    </message>
    <message>
        <location line="-38"/>
        <location line="+40"/>
        <source>Created</source>
        <translation>作成日時</translation>
    </message>
    <message numerus="yes">
        <location line="-168"/>
        <source>File Details: %n files</source>
        <translation>
            <numerusform>ファイルの詳細: %n 個のファイル</numerusform>
        </translation>
    </message>
    <message>
        <location line="+121"/>
        <location line="+33"/>
        <source>Transferred</source>
        <translation>転送済み</translation>
    </message>
    <message>
        <location line="-25"/>
        <location line="+40"/>
        <source>Last Seen Complete</source>
        <translation>最後に完全な状態で確認</translation>
    </message>
    <message>
        <location line="-39"/>
        <location line="+40"/>
        <source>Last Reception</source>
        <translation>最終受信</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>No alternative file names reported by sources. Use “Search Kad” to look them up.</source>
        <translation>ソースから代替ファイル名は報告されていません。「Kad を検索」で検索してください。</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Search Kad</source>
        <translation>Kad を検索</translation>
    </message>
    <message>
        <location line="+92"/>
        <source>No media information available.</source>
        <translation>メディア情報はありません。</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Title</source>
        <translation>タイトル</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Artist</source>
        <translation>アーティスト</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Album</source>
        <translation>アルバム</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Codec</source>
        <translation>コーデック</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Bitrate</source>
        <translation>ビットレート</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Length</source>
        <translation>長さ</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Link Options</source>
        <translation>リンクオプション</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Include Hashset</source>
        <translation>ハッシュセットを含める</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Include Hostname</source>
        <translation>ホスト名を含める</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add your hostname or public IPv6 as a source</source>
        <translation>自分のホスト名またはパブリック IPv6 をソースとして追加する</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>HTML Format</source>
        <translation>HTML 形式</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Copy to Clipboard</source>
        <translation>クリップボードにコピー</translation>
    </message>
</context>
<context>
    <name>eMule::FindInListDialog</name>
    <message>
        <location filename="../src/gui/dialogs/FindInListDialog.cpp" line="+90"/>
        <source>Search</source>
        <translation>検索</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Search for:</source>
        <translation>検索：</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Search in column:</source>
        <translation>列で検索：</translation>
    </message>
</context>
<context>
    <name>eMule::FirstStartWizard</name>
    <message>
        <location filename="../src/gui/dialogs/FirstStartWizard.cpp" line="+57"/>
        <source>eMule First Runtime Wizard</source>
        <translation>eMule 初回実行ウィザード</translation>
    </message>
    <message>
        <location line="+337"/>
        <source>Ports and Connection</source>
        <translation>ポートと接続</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Connection</source>
        <translation>接続</translation>
    </message>
    <message>
        <location line="-246"/>
        <source>eMule uses two ports for communication with servers and clients. These ports must be free and available for remote clients. The TCP port must be available to ensure the main functionality of eMule. The UDP port is used for Kad (serverless network) and to reduce network usage (Overhead).</source>
        <translation>eMule はサーバーおよびクライアントとの通信に2つのポートを使用します。これらのポートはリモートクライアントに対して空いている必要があります。TCP ポートは eMule の主要機能を確保するために必要です。UDP ポートは Kad（サーバーレスネットワーク）およびネットワーク使用量の削減（オーバーヘッド）に使用されます。</translation>
    </message>
    <message>
        <location line="-101"/>
        <location line="+281"/>
        <source>Unlimited</source>
        <translation>無制限</translation>
    </message>
    <message>
        <location line="-167"/>
        <source>You can change the ports here while no network activities have started.</source>
        <translation>ネットワーク活動が開始されていない間に、ここでポートを変更できます。</translation>
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
        <translation>無効化</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Use UPnP to Setup Ports</source>
        <translation>UPnP でポートを設定</translation>
    </message>
    <message>
        <location line="+50"/>
        <source>Choose which Network(s) you want to use</source>
        <translation>使用するネットワークを選択</translation>
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
        <location line="+29"/>
        <source>Select your internet connection. eMule derives its download and upload limits from it, leaving room for your other applications. You can change the limits at any time in the Options.</source>
        <translation>インターネット接続を選択してください。eMule はそこからダウンロードとアップロードの制限を決め、他のアプリケーションのための余裕を残します。制限はオプションでいつでも変更できます。</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Type</source>
        <translation>種類</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Down</source>
        <translation>下り</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Up</source>
        <translation>上り</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Keep current settings</source>
        <translation>現在の設定を維持</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unknown (recommended defaults)</source>
        <translation>不明 (推奨の既定値)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Custom</source>
        <translation>カスタム</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+1"/>
        <source>(enter below)</source>
        <translation>(下に入力)</translation>
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
        <translation>ダウンロード:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload:</source>
        <translation>アップロード:</translation>
    </message>
    <message>
        <location line="+7"/>
        <source> KB/s</source>
        <translation> KB/s</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Download limit:</source>
        <translation>ダウンロード制限:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload limit:</source>
        <translation>アップロード制限:</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>&lt; Back</source>
        <translation>&lt; 戻る</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+28"/>
        <source>Next &gt;</source>
        <translation>次へ &gt;</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>Connection Speed</source>
        <translation>接続速度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bandwidth</source>
        <translation>帯域幅</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Finish</source>
        <translation>完了</translation>
    </message>
    <message>
        <location line="-25"/>
        <source>Cancel</source>
        <translation>キャンセル</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Help</source>
        <translation>ヘルプ</translation>
    </message>
    <message>
        <source>Download limit: %1    Upload limit: %2</source>
        <translation type="vanished">ダウンロード制限: %1    アップロード制限: %2</translation>
    </message>
    <message>
        <location line="+144"/>
        <source>The ports are forwarded when the core starts.</source>
        <translation>ポートはコアの起動時に転送されます。</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>The core did not accept the port settings.</source>
        <translation>コアがポート設定を受け付けませんでした。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>No answer from the core about port forwarding.</source>
        <translation>ポート転送についてコアから応答がありません。</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Network</source>
        <translation>ネットワーク</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>You must enable at least one network (Kad or eD2K).</source>
        <translation>少なくとも1つのネットワーク（Kad または eD2K）を有効にしてください。</translation>
    </message>
    <message>
        <source>UPnP</source>
        <translation type="vanished">UPnP</translation>
    </message>
    <message>
        <source>UPnP port mapping timed out. Your router may not support UPnP, or it may be disabled. You can set up port forwarding manually.</source>
        <translation type="vanished">UPnP ポートマッピングがタイムアウトしました。ルーターが UPnP をサポートしていないか、無効になっている可能性があります。ポートフォワーディングを手動で設定できます。</translation>
    </message>
</context>
<context>
    <name>eMule::ImportDownloadsDialog</name>
    <message>
        <location filename="../src/gui/dialogs/ImportDownloadsDialog.cpp" line="+39"/>
        <source>Convert Part Files</source>
        <translation>Part ファイルを変換</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Current Job</source>
        <translation>現在のジョブ</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+217"/>
        <source>Idle</source>
        <translation>アイドル</translation>
    </message>
    <message>
        <location line="-206"/>
        <source>Job Queue</source>
        <translation>ジョブキュー</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Filename</source>
        <translation>ファイル名</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Status</source>
        <translation>ステータス</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Size</source>
        <translation>サイズ</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>File Hash</source>
        <translation>ファイルハッシュ</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Add Imports...</source>
        <translation>インポートを追加...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Retry Selected</source>
        <translation>選択項目を再試行</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove Selected</source>
        <translation>選択項目を削除</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Close</source>
        <translation>閉じる</translation>
    </message>
    <message>
        <location line="+57"/>
        <source>Import Downloads</source>
        <translation>ダウンロードをインポート</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Import Downloads is only available for local connections.</source>
        <translation>ダウンロードのインポートはローカル接続でのみ利用可能です。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Select folder to scan for importable downloads</source>
        <translation>インポート可能なダウンロードをスキャンするフォルダを選択</translation>
    </message>
    <message>
        <location line="+119"/>
        <source>Converting...</source>
        <translation>変換中...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Done</source>
        <translation>完了</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>OK</source>
        <translation>OK</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Queued</source>
        <translation>キュー待ち</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>In Progress</source>
        <translation>進行中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Out of Disk Space</source>
        <translation>ディスク空き容量不足</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>.part.met Not Found</source>
        <translation>.part.met が見つかりません</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>I/O Error</source>
        <translation>I/O エラー</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Failed</source>
        <translation>失敗</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bad Format</source>
        <translation>不正な形式</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Already Exists</source>
        <translation>既に存在します</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unknown</source>
        <translation>不明</translation>
    </message>
</context>
<context>
    <name>eMule::IndexerResultsModel</name>
    <message>
        <location filename="../src/gui/controls/IndexerResultsModel.cpp" line="+26"/>
        <source>today</source>
        <translation>今日</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>1 day</source>
        <translation>1 日</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1 days</source>
        <translation>%1 日</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1 months</source>
        <translation>%1 か月</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1 years</source>
        <translation>%1 年</translation>
    </message>
    <message>
        <location line="+109"/>
        <source>Posted: %1</source>
        <translation>投稿日: %1</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>%1 files</source>
        <translation>%1 ファイル</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Password protected</source>
        <translation>パスワード保護</translation>
    </message>
    <message>
        <location line="+31"/>
        <source>Name</source>
        <translation>名前</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Size</source>
        <translation>サイズ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Age</source>
        <translation>経過日数</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Category</source>
        <translation>カテゴリ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Grabs</source>
        <translation>取得数</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Indexer</source>
        <translation>インデクサー</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Seeders</source>
        <translation>シーダー</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Peers</source>
        <translation>ピア</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Known</source>
        <translation>既知</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Confidence</source>
        <translation>信頼度</translation>
    </message>
</context>
<context>
    <name>eMule::IrcPanel</name>
    <message>
        <location filename="../src/gui/panels/IrcPanel.cpp" line="+129"/>
        <source>Select an IRC nick.</source>
        <translation>IRC ニックネームを選択してください。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Should be no longer than 25 characters: letters, digits or symbols [_-{}]\.
Nick can be changed again in Options-&gt;IRC.</source>
        <translation>25文字以内：文字、数字または記号 [_-{}]\。
ニックネームはオプション-&gt;IRCで再変更できます。</translation>
    </message>
    <message>
        <location line="+84"/>
        <source>Disconnect</source>
        <translation>切断</translation>
    </message>
    <message>
        <location line="+27"/>
        <location line="+403"/>
        <source>Connect</source>
        <translation>接続</translation>
    </message>
    <message>
        <location line="-109"/>
        <source>Nick in use</source>
        <translation>ニックネーム使用中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The nick &quot;%1&quot; is already in use.
Please choose another:</source>
        <translation>ニックネーム &quot;%1&quot; は既に使用されています。
別のニックネームを選択してください：</translation>
    </message>
    <message>
        <location line="+33"/>
        <location line="+551"/>
        <source>Nick</source>
        <translation>ニックネーム</translation>
    </message>
    <message>
        <location line="-486"/>
        <source>Status</source>
        <translation>ステータス</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Close</source>
        <translation>閉じる</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Smileys</source>
        <translation>スマイリー</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Bold</source>
        <translation>太字</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Italic</source>
        <translation>斜体</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Underline</source>
        <translation>下線</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Color</source>
        <translation>色</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Reset Formatting</source>
        <translation>書式をリセット</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Type a message...</source>
        <translation>メッセージを入力...</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Send</source>
        <translation>送信</translation>
    </message>
    <message>
        <location line="+82"/>
        <source>Channel</source>
        <translation>チャンネル</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Users</source>
        <translation>ユーザー</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Topic</source>
        <translation>トピック</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Join</source>
        <translation>参加</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Channels</source>
        <translation>チャンネル</translation>
    </message>
    <message>
        <location line="+261"/>
        <source>Nick (%1)</source>
        <translation>ニックネーム (%1)</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>* Private chat session started</source>
        <translation>* プライベートチャットを開始しました</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>Nick : %1</source>
        <translation>ニックネーム: %1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Private Message</source>
        <translation>プライベートメッセージ</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Add to friends list</source>
        <translation>フレンドリストに追加</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Send this to friend: </source>
        <translation>フレンドに送信: </translation>
    </message>
    <message>
        <location line="+1"/>
        <source>No Shared File Selected</source>
        <translation>共有ファイルが選択されていません</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Kick</source>
        <translation>キック</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Ban</source>
        <translation>BAN</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Slap</source>
        <translation>スラップ</translation>
    </message>
    <message>
        <location line="+93"/>
        <source>%1 attempted to send you a file. If you wanted to accept the files from this person, enable Receive files in the IRC Preferences.</source>
        <translation>%1 がファイルを送信しようとしました。この相手からのファイルを受け取るには、IRC 設定でファイルの受信を有効にしてください。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>%1 sent (%2) to be auto started.</source>
        <translation>%1 が (%2) を送信しました。自動的に開始します。</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>%1 attempted to send you a file. If you wanted to accept the files from this person, add him as a friend or change your IRC Preferences.</source>
        <translation>%1 がファイルを送信しようとしました。この相手からのファイルを受け取るには、フレンドに追加するか IRC 設定を変更してください。</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>%1 added you as friend!</source>
        <translation>%1 があなたをフレンドに追加しました！</translation>
    </message>
</context>
<context>
    <name>eMule::KadContactHistogram</name>
    <message>
        <location filename="../src/gui/controls/KadContactHistogram.cpp" line="+193"/>
        <source>Contacts</source>
        <translation>連絡先</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Kademlia Network</source>
        <translation>Kademlia ネットワーク</translation>
    </message>
</context>
<context>
    <name>eMule::KadContactsModel</name>
    <message>
        <source>Status</source>
        <translation type="vanished">ステータス</translation>
    </message>
    <message>
        <source>Client ID</source>
        <translation type="vanished">クライアント ID</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/KadContactsModel.cpp" line="+78"/>
        <source>Type</source>
        <translation>種類</translation>
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
        <translation>国</translation>
    </message>
</context>
<context>
    <name>eMule::KadLookupGraph</name>
    <message>
        <location filename="../src/gui/controls/KadLookupGraph.cpp" line="+69"/>
        <source>No search selected</source>
        <translation>検索が選択されていません</translation>
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
        <translation>自ノード（検索開始者）</translation>
    </message>
</context>
<context>
    <name>eMule::KadPanel</name>
    <message>
        <location filename="../src/gui/panels/KadPanel.cpp" line="+118"/>
        <location line="+12"/>
        <location line="+192"/>
        <location line="+2"/>
        <location line="+37"/>
        <location line="+213"/>
        <source>▸ Contacts (0)</source>
        <translation>▸ 連絡先 (0)</translation>
    </message>
    <message>
        <location line="-455"/>
        <location line="+12"/>
        <location line="+392"/>
        <location line="+97"/>
        <source>▸ Current Searches (0)</source>
        <translation>▸ 現在の検索 (0)</translation>
    </message>
    <message>
        <location line="-427"/>
        <source>The URL is invalid.</source>
        <translation>URL が無効です。</translation>
    </message>
    <message>
        <location line="+136"/>
        <location line="+358"/>
        <source>▸ Search Details</source>
        <translation>▸ 検索詳細</translation>
    </message>
    <message>
        <location line="-271"/>
        <source>Recheck Firewall</source>
        <translation>ファイアウォールを再チェック</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+256"/>
        <source>Connect</source>
        <translation>接続</translation>
    </message>
    <message>
        <location line="-238"/>
        <source>IP or Address:</source>
        <translation>IP またはアドレス:</translation>
    </message>
    <message>
        <location line="-227"/>
        <location line="+224"/>
        <location line="+40"/>
        <source>Bootstrap</source>
        <translation>ブートストラップ</translation>
    </message>
    <message>
        <location line="-273"/>
        <source>Downloading...</source>
        <translation>ダウンロード中...</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+77"/>
        <source>Kademlia</source>
        <translation>Kademlia</translation>
    </message>
    <message>
        <source>Failed to download nodes.dat: %1</source>
        <translation type="vanished">nodes.datのダウンロードに失敗しました: %1</translation>
    </message>
    <message>
        <source>Downloaded nodes.dat is empty.</source>
        <translation type="vanished">ダウンロードしたnodes.datが空です。</translation>
    </message>
    <message>
        <source>Failed to save nodes.dat: %1</source>
        <translation type="vanished">nodes.datの保存に失敗しました: %1</translation>
    </message>
    <message>
        <source>IP Address:</source>
        <translation type="vanished">IP アドレス：</translation>
    </message>
    <message>
        <location line="+152"/>
        <source>Port:</source>
        <translation>ポート：</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Nodes.dat from URL:</source>
        <translation>URL から Nodes.dat：</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>From known clients</source>
        <translation>既知のクライアントから</translation>
    </message>
    <message>
        <location line="+142"/>
        <location line="+3"/>
        <source>▸ Contacts (%1)</source>
        <translation>▸ 連絡先 (%1)</translation>
    </message>
    <message>
        <location line="+41"/>
        <source>▸ Current Searches (%1)</source>
        <translation>▸ 現在の検索 (%1)</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Disconnect</source>
        <translation>切断</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Cancel</source>
        <translation>キャンセル</translation>
    </message>
    <message>
        <location line="+65"/>
        <source>▸ Search Details (%1)</source>
        <translation>▸ 検索詳細 (%1)</translation>
    </message>
</context>
<context>
    <name>eMule::KadSearchesModel</name>
    <message>
        <source>No.</source>
        <translation type="vanished">番号</translation>
    </message>
    <message>
        <location filename="../src/gui/controls/KadSearchesModel.cpp" line="+36"/>
        <source>Stopping</source>
        <translation>停止中</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Active</source>
        <translation>アクティブ</translation>
    </message>
    <message>
        <location line="+42"/>
        <source>Number</source>
        <translation>番号</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Key</source>
        <translation>キー</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type</source>
        <translation>種類</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Name</source>
        <translation>名前</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status</source>
        <translation>ステータス</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Load</source>
        <translation>読み込み</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Packets Sent</source>
        <translation>送信パケット</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Responses</source>
        <translation>応答</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Search Sources</source>
        <translation>ソース検索</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Search Keywords</source>
        <translation>キーワード検索</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Node Lookup</source>
        <translation>ノード検索</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Store File</source>
        <translation>ファイルを保存</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Store Keyword</source>
        <translation>キーワードを保存</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Find Buddy</source>
        <translation>バディを検索</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Store Notes</source>
        <translation>ノートを保存</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Notes</source>
        <translation>ノート</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unknown</source>
        <translation>不明</translation>
    </message>
</context>
<context>
    <name>eMule::LogTextView</name>
    <message>
        <location filename="../src/gui/controls/LogTextView.cpp" line="+43"/>
        <source>Autoscroll</source>
        <translation>自動スクロール</translation>
    </message>
</context>
<context>
    <name>eMule::LogWidget</name>
    <message>
        <location filename="../src/gui/controls/LogWidget.cpp" line="+86"/>
        <location line="+2"/>
        <source>Server Info</source>
        <translation>サーバー情報</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+2"/>
        <source>Log</source>
        <translation>ログ</translation>
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
        <location line="+357"/>
        <source>Click here to check if a new version is available</source>
        <translation>クリックして新しいバージョンがあるか確認します</translation>
    </message>
</context>
<context>
    <name>eMule::MainWindow</name>
    <message>
        <location filename="../src/gui/app/MainWindow.cpp" line="+83"/>
        <location line="+464"/>
        <source>eMule Qt v%1</source>
        <translation>eMule Qt v%1</translation>
    </message>
    <message>
        <location line="-360"/>
        <location line="+9"/>
        <source>New Version Available</source>
        <translation>新しいバージョンが利用可能</translation>
    </message>
    <message>
        <location line="+199"/>
        <source>eD2K: Connected (LowID)</source>
        <translation>eD2K：接続済み (LowID)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>eD2K: Connected</source>
        <translation>eD2K：接続済み</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>eD2K: Connecting...</source>
        <translation>eD2K：接続中...</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+1181"/>
        <source>eD2K: Disconnected</source>
        <translation>eD2K：未接続</translation>
    </message>
    <message>
        <location line="-1124"/>
        <source>Kad: Connected</source>
        <translation>Kad：接続済み</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Kad: Connected (Firewalled)</source>
        <translation>Kad：接続済み（ファイアウォール）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Kad: Connecting...</source>
        <translation>Kad：接続中...</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+1116"/>
        <source>Kad: Disconnected</source>
        <translation>Kad：未接続</translation>
    </message>
    <message>
        <location line="-1107"/>
        <source>Users: %1 | Files: %2</source>
        <translation>ユーザー：%1 | ファイル：%2</translation>
    </message>
    <message>
        <location line="+315"/>
        <source>Open Incoming Folder...</source>
        <translation>受信フォルダを開く...</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Import Downloads (eM,eD,ON)...</source>
        <translation>ダウンロードをインポート (eM,eD,ON)...</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>eMule First Runtime Wizard...</source>
        <translation>eMule 初回実行ウィザード...</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>IP Filter...</source>
        <translation>IP フィルター...</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Paste eD2K Links...</source>
        <translation>eD2K リンクを貼り付け...</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Links</source>
        <translation>リンク</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>eMule Homepage</source>
        <translation>eMule ホームページ</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Features</source>
        <translation>機能</translation>
    </message>
    <message>
        <location line="-656"/>
        <location line="+7"/>
        <location line="+652"/>
        <source>Version Check</source>
        <translation>バージョン確認</translation>
    </message>
    <message>
        <location line="-714"/>
        <source>Quit eMule Qt</source>
        <translation>eMule Qt を終了</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>eMule Qt %1 has been released.</source>
        <translation>eMule Qt %1 がリリースされました。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Version %1 of eMule Qt was released on %2.</source>
        <translation>eMule Qt のバージョン %1 は %2 にリリースされました。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Version %1 of eMule Qt is available.</source>
        <translation>eMule Qt のバージョン %1 が利用可能です。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>%1

You are running %2. Open the eMule Qt website?</source>
        <translation>%1

現在 %2 を使用しています。eMule Qt のウェブサイトを開きますか？</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>You are running the latest version of eMule Qt (v%1).</source>
        <translation>最新バージョンの eMule Qt (v%1) を使用しています。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Could not check for a new version:

%1</source>
        <translation>新しいバージョンを確認できませんでした:

%1</translation>
    </message>
    <message>
        <location line="+63"/>
        <source>Cannot Connect</source>
        <translation>接続できません</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Both the eD2K and Kad networks are disabled.

Enable at least one under Options → Connection to connect.</source>
        <translation>eD2K ネットワークと Kad ネットワークの両方が無効です。

接続するには、オプション → 接続 で少なくとも一方を有効にしてください。</translation>
    </message>
    <message>
        <location line="+129"/>
        <source>Blocked: network interface not available</source>
        <translation>ブロック中: ネットワークインターフェースが利用できません</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>eD2K Server: %1  (%2 Users)</source>
        <translation>eD2K サーバー: %1  (%2 ユーザー)</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Preparing...</source>
        <translation>準備中...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Waiting...</source>
        <translation>待機中...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Error!</source>
        <translation>エラー！</translation>
    </message>
    <message>
        <location line="+119"/>
        <source>Connected</source>
        <translation>接続済み</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Disconnected</source>
        <translation>切断</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>eMule Qt v%1 (%2)
Up: %3 | Down: %4</source>
        <translation>eMule Qt v%1 (%2)
上り: %3 | 下り: %4</translation>
    </message>
    <message>
        <location line="+72"/>
        <source>Confirm Exit</source>
        <translation>終了の確認</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Are you sure you want to exit eMule?</source>
        <translation>eMule を終了してもよろしいですか？</translation>
    </message>
    <message>
        <location line="+95"/>
        <source>eMule Log</source>
        <translation>eMule ログ</translation>
    </message>
    <message>
        <location line="+81"/>
        <source>Open Downloads Folder in Browser</source>
        <translation>ダウンロードフォルダをブラウザで開く</translation>
    </message>
    <message>
        <location line="+34"/>
        <source>Open WebUI</source>
        <translation>Web インターフェースを開く</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Submit Bug Report...</source>
        <translation>バグ報告を送信...</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Edit Web Services...</source>
        <translation>Web サービスを編集...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Web Services</source>
        <translation>Web サービス</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>webservices.dat was not found in the config folder.</source>
        <translation>webservices.dat が設定フォルダに見つかりませんでした。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Scheduler</source>
        <translation>スケジューラ</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Disable Scheduler</source>
        <translation>スケジューラを無効化</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Enable Scheduler</source>
        <translation>スケジューラを有効化</translation>
    </message>
    <message>
        <location line="+41"/>
        <source>Cannot Open Downloads Folder</source>
        <translation>ダウンロードフォルダを開けません</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Cannot Open Web Interface</source>
        <translation>Web インターフェースを開けません</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not connected to the core.</source>
        <translation>コアに接続されていません。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Web Interface Disabled</source>
        <translation>Web インターフェースは無効です</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The web interface is disabled.

Enable it under Options → Web Interface, then try again.</source>
        <translation>Web インターフェースは無効になっています。

オプション → Web インターフェース で有効にしてから、もう一度お試しください。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Open Options</source>
        <translation>オプションを開く</translation>
    </message>
    <message>
        <location line="+173"/>
        <source>Main</source>
        <translation>メイン</translation>
    </message>
    <message>
        <location line="+162"/>
        <source>Toolbar Skins</source>
        <translation>ツールバースキン</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Select Toolbar Bitmap...</source>
        <translation>ツールバービットマップを選択...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Select Toolbar Bitmap</source>
        <translation>ツールバービットマップを選択</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Images (*.bmp *.png *.jpg);;All Files (*)</source>
        <translation>画像 (*.bmp *.png *.jpg);;すべてのファイル (*)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Select Toolbar Bitmap Directory...</source>
        <translation>ツールバービットマップのフォルダーを選択...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Select Toolbar Bitmap Directory</source>
        <translation>ツールバービットマップのフォルダーを選択</translation>
    </message>
    <message>
        <location line="+17"/>
        <location line="+61"/>
        <source>Default</source>
        <translation>既定</translation>
    </message>
    <message>
        <location line="-25"/>
        <source>Skin Profiles</source>
        <translation>スキンプロファイル</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Select Skin File...</source>
        <translation>スキンファイルを選択...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Select Skin Profile</source>
        <translation>スキンプロファイルを選択</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Skin Files (*.eMuleSkin.ini);;All Files (*)</source>
        <translation>スキンファイル (*.eMuleSkin.ini);;すべてのファイル (*)</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Select Skin Directory...</source>
        <translation>スキンフォルダーを選択...</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Select Skin Directory</source>
        <translation>スキンフォルダーを選択</translation>
    </message>
    <message>
        <location line="+40"/>
        <source>Text Label Options</source>
        <translation>テキストラベルのオプション</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Customize Toolbar...</source>
        <translation>ツールバーのカスタマイズ...</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Disconnect</source>
        <translation>切断</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Connect</source>
        <translation>接続</translation>
    </message>
    <message>
        <location line="+71"/>
        <source>Ready</source>
        <translation>準備完了</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Users: 0 | Files: 0</source>
        <translation>ユーザー：0 | ファイル：0</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Up: 0.0</source>
        <translation>アップ：0.0</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Down: 0.0</source>
        <translation>ダウン：0.0</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Double-click for Network Information</source>
        <translation>ダブルクリックでネットワーク情報を表示</translation>
    </message>
    <message>
        <location line="+31"/>
        <source>New message — double-click to read</source>
        <translation>新しいメッセージ — ダブルクリックで読む</translation>
    </message>
</context>
<context>
    <name>eMule::MediaInfoPanel</name>
    <message>
        <location filename="../src/gui/dialogs/MediaInfoPanel.cpp" line="+44"/>
        <source>Scanning...</source>
        <translation>スキャン中...</translation>
    </message>
    <message>
        <location line="+17"/>
        <location line="+178"/>
        <source>No media information available.</source>
        <translation>メディア情報はありません。</translation>
    </message>
    <message>
        <location line="-146"/>
        <source>estimated</source>
        <translation>推定</translation>
    </message>
    <message>
        <location line="+164"/>
        <source>General</source>
        <translation>全般</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Format:</source>
        <translation>形式:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Length:</source>
        <translation>長さ:</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Video</source>
        <translation>ビデオ</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+17"/>
        <source>Codec:</source>
        <translation>コーデック：</translation>
    </message>
    <message>
        <location line="-16"/>
        <location line="+17"/>
        <source>Bitrate:</source>
        <translation>ビットレート:</translation>
    </message>
    <message>
        <location line="-16"/>
        <source>Resolution:</source>
        <translation>解像度:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Aspect Ratio:</source>
        <translation>アスペクト比:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>FPS:</source>
        <translation>FPS:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Audio</source>
        <translation>オーディオ</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Channels:</source>
        <translation>チャンネル:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Sample Rate:</source>
        <translation>サンプルレート:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Language:</source>
        <translation>言語:</translation>
    </message>
</context>
<context>
    <name>eMule::MessagesPanel</name>
    <message>
        <location filename="../src/gui/panels/MessagesPanel.cpp" line="+153"/>
        <location line="+543"/>
        <source> ...failed</source>
        <translation> ...失敗</translation>
    </message>
    <message>
        <location line="-541"/>
        <source>Me</source>
        <translation>自分</translation>
    </message>
    <message>
        <location line="+93"/>
        <source>Friends (0)</source>
        <translation>フレンド (0)</translation>
    </message>
    <message>
        <location line="+41"/>
        <source>Info</source>
        <translation>情報</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Name:</source>
        <translation>名前：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hash:</source>
        <translation>ハッシュ：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Software:</source>
        <translation>ソフトウェア：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Identification:</source>
        <translation>識別：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Uploaded:</source>
        <translation>アップロード済み：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloaded:</source>
        <translation>ダウンロード済み：</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Messages</source>
        <translation>メッセージ</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>Smileys</source>
        <translation>スマイリー</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Type a message...</source>
        <translation>メッセージを入力...</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Send</source>
        <translation>送信</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+246"/>
        <source>Close</source>
        <translation>閉じる</translation>
    </message>
    <message>
        <location line="-222"/>
        <location line="+190"/>
        <source>&amp;Details...</source>
        <translation>詳細(&amp;D)...</translation>
    </message>
    <message>
        <location line="-186"/>
        <source>Add...</source>
        <translation>追加...</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+203"/>
        <source>Remove</source>
        <translation>削除</translation>
    </message>
    <message>
        <location line="-200"/>
        <source>Send Message</source>
        <translation>メッセージを送信</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>View Shared Files</source>
        <translation>共有ファイルを表示</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>cannot view shared files.</source>
        <translation>共有ファイルを表示できません。</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Establish Friend Slot</source>
        <translation>フレンドスロットを確立</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Find...</source>
        <translation>検索...</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>Friends (%1)</source>
        <translation>フレンド (%1)</translation>
    </message>
    <message>
        <location line="+59"/>
        <source>Successful</source>
        <translation>成功</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Invalid</source>
        <translation>無効</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not supported or disabled</source>
        <translation>未対応または無効</translation>
    </message>
    <message>
        <location line="+44"/>
        <source>Add to friends list</source>
        <translation>フレンドリストに追加</translation>
    </message>
    <message>
        <location line="+104"/>
        <source>*** Connecting</source>
        <translation>*** 接続中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>*** Authenticating friend</source>
        <translation>*** フレンドを認証中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>*** Searching friend in Kad</source>
        <translation>*** Kad でフレンドを検索中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source> ...found</source>
        <translation> ...見つかりました</translation>
    </message>
    <message>
        <location line="+1"/>
        <source> ...OK</source>
        <translation> ...OK</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>*** In order to avoid spam messages, this user requires you to solve a captcha before you can send him a message. Please enter the letters you see on this image as response:</source>
        <translation>*** スパムメッセージを防ぐため、このユーザーにメッセージを送るにはキャプチャを解く必要があります。この画像に表示されている文字を返信として入力してください:</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>*** You have passed the captcha check and the user has received your message.</source>
        <translation>*** キャプチャの確認に成功し、ユーザーにメッセージが届きました。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>*** Your response to the captcha was wrong and your message has been ignored. You can request a new captcha by sending a new message.</source>
        <translation>*** キャプチャへの回答が間違っていたため、メッセージは無視されました。新しいメッセージを送ると、新しいキャプチャを要求できます。</translation>
    </message>
    <message>
        <source>Find Friend</source>
        <translation type="vanished">フレンドを検索</translation>
    </message>
</context>
<context>
    <name>eMule::MetaAccountDialog</name>
    <message>
        <location filename="../src/gui/dialogs/MetaAccountDialog.cpp" line="+63"/>
        <source>User name:</source>
        <translation>ユーザー名：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Password:</source>
        <translation>パスワード：</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Log In</source>
        <translation>ログイン</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Check Again</source>
        <translation>再確認</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Log Out</source>
        <translation>ログアウト</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>eNode Account — %1</source>
        <translation>eNode アカウント — %1</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>&lt;b&gt;%1&lt;/b&gt; needs an account to download torrent and Usenet search results. Log in, or register an account on the server&apos;s website.</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; で Torrent と Usenet の検索結果をダウンロードするにはアカウントが必要です。ログインするか、サーバーの Web サイトでアカウントを登録してください。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Log in to your account on &lt;b&gt;%1&lt;/b&gt;.</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; のアカウントにログインします。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>is not active yet</source>
        <translation>はまだ有効ではありません</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>has expired</source>
        <translation>は期限切れです</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>has been disabled by the operator</source>
        <translation>は運営者によって無効化されています</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Your account &lt;b&gt;%1&lt;/b&gt; on %2 %3. Complete the steps below on the server&apos;s website, then choose Check Again.</source>
        <translation>%2 のアカウント &lt;b&gt;%1&lt;/b&gt;%3。サーバーの Web サイトで以下の手順を完了してから、「再確認」を選択してください。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>no expiry</source>
        <translation>期限なし</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Logged in to &lt;b&gt;%1&lt;/b&gt; as &lt;b&gt;%2&lt;/b&gt;.&lt;br&gt;Access until: %3</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; に &lt;b&gt;%2&lt;/b&gt; としてログイン中。&lt;br&gt;利用期限: %3</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>&lt;b&gt;%1&lt;/b&gt; needs no account — its torrent and Usenet results are free to download.</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; はアカウント不要です — Torrent と Usenet の結果は自由にダウンロードできます。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Checking the account on &lt;b&gt;%1&lt;/b&gt;...</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; のアカウントを確認中...</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Open steps:</source>
        <translation>未完了の手順：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Register an account</source>
        <translation>アカウントを登録</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Open account page</source>
        <translation>アカウントページを開く</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Cancel</source>
        <translation>キャンセル</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Close</source>
        <translation>閉じる</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Not connected to the eMule core.</source>
        <translation>eMule コアに接続されていません。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Enter a user name and a password.</source>
        <translation>ユーザー名とパスワードを入力してください。</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Logging in...</source>
        <translation>ログイン中...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>The connection to the eMule core was lost.</source>
        <translation>eMule コアとの接続が切断されました。</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>Checking...</source>
        <translation>確認中...</translation>
    </message>
</context>
<context>
    <name>eMule::MetaResultActions</name>
    <message>
        <location filename="../src/gui/utils/MetaResultActions.cpp" line="+75"/>
        <source>NZB files (*.nzb)</source>
        <translation>NZB ファイル (*.nzb)</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Torrent files (*.torrent)</source>
        <translation>Torrent ファイル (*.torrent)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Save NZB File</source>
        <translation>NZB ファイルを保存</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Save Torrent File</source>
        <translation>Torrent ファイルを保存</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Save Files To</source>
        <translation>ファイルの保存先</translation>
    </message>
    <message>
        <location line="+44"/>
        <source>Queued &quot;%1&quot; for Usenet download.</source>
        <translation>「%1」を Usenet ダウンロードのキューに追加しました。</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Download</source>
        <translation>ダウンロード</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>%1

Download it again?</source>
        <translation>%1

もう一度ダウンロードしますか？</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Could not queue &quot;%1&quot;: %2</source>
        <translation>「%1」をキューに追加できませんでした: %2</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Could not download &quot;%1&quot;: %2</source>
        <translation>「%1」をダウンロードできませんでした: %2</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Could not write %1: %2</source>
        <translation>%1 を書き込めませんでした: %2</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Saved %1</source>
        <translation>%1 を保存しました</translation>
    </message>
    <message>
        <location line="+45"/>
        <source>Search</source>
        <translation>検索</translation>
    </message>
</context>
<context>
    <name>eMule::MetadataPage</name>
    <message>
        <location filename="../src/gui/dialogs/MetadataPage.cpp" line="+108"/>
        <source>No metadata tags available.</source>
        <translation>利用可能なメタデータタグがありません。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Tag Name</source>
        <translation>タグ名</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type</source>
        <translation>種類</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Value</source>
        <translation>値</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Unknown</source>
        <translation>不明</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>Copy</source>
        <translation>コピー</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Select All</source>
        <translation>すべて選択</translation>
    </message>
</context>
<context>
    <name>eMule::MiniMuleWidget</name>
    <message>
        <location filename="../src/gui/app/MiniMuleWidget.cpp" line="+74"/>
        <source>Yes</source>
        <translation>はい</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>No</source>
        <translation>いいえ</translation>
    </message>
    <message>
        <location line="+100"/>
        <source>Connected</source>
        <translation>接続済み</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Upload</source>
        <translation>アップロード</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Download</source>
        <translation>ダウンロード</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Completed</source>
        <translation>完了</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Free Space</source>
        <translation>空き容量</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Restore Window</source>
        <translation>ウィンドウを復元</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Open Incoming Folder</source>
        <translation>受信フォルダを開く</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Options</source>
        <translation>オプション</translation>
    </message>
</context>
<context>
    <name>eMule::NetworkInfoDialog</name>
    <message>
        <location filename="../src/gui/dialogs/NetworkInfoDialog.cpp" line="+53"/>
        <source>Network Information</source>
        <translation>ネットワーク情報</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>&lt;i&gt;Loading...&lt;/i&gt;</source>
        <translation>&lt;i&gt;読み込み中...&lt;/i&gt;</translation>
    </message>
    <message>
        <location line="+15"/>
        <location line="+10"/>
        <source>&lt;b&gt;Not connected to daemon.&lt;/b&gt;</source>
        <translation>&lt;b&gt;デーモンに接続されていません。&lt;/b&gt;</translation>
    </message>
    <message>
        <location line="+52"/>
        <location line="+151"/>
        <source>Connected</source>
        <translation>接続済み</translation>
    </message>
    <message>
        <location line="-149"/>
        <location line="+108"/>
        <location line="+39"/>
        <source>Connecting</source>
        <translation>接続中</translation>
    </message>
    <message>
        <location line="-145"/>
        <location line="+108"/>
        <source>Disconnected</source>
        <translation>未接続</translation>
    </message>
    <message>
        <location line="-93"/>
        <location line="+26"/>
        <source>Unknown</source>
        <translation>不明</translation>
    </message>
    <message>
        <location line="-17"/>
        <source>Low ID</source>
        <translation>Low ID</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>High ID</source>
        <translation>High ID</translation>
    </message>
    <message>
        <location line="+48"/>
        <source>Obfuscated</source>
        <translation>難読化済み</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Normal</source>
        <translation>通常</translation>
    </message>
    <message>
        <location line="+126"/>
        <source>Enabled</source>
        <translation>有効</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>not running</source>
        <translation>未実行</translation>
    </message>
    <message numerus="yes">
        <location line="+4"/>
        <source>%n active session(s)</source>
        <translation>
            <numerusform>%n 件のアクティブなセッション</numerusform>
        </translation>
    </message>
    <message>
        <location line="-167"/>
        <location line="+67"/>
        <location line="+13"/>
        <source>Firewalled</source>
        <translation>ファイアウォール</translation>
    </message>
    <message>
        <location line="-82"/>
        <location line="+4"/>
        <location line="+65"/>
        <location line="+15"/>
        <source>Open</source>
        <translation>オープン</translation>
    </message>
    <message>
        <location line="-80"/>
        <location line="+82"/>
        <source>unverified</source>
        <translation>未検証</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>None</source>
        <translation>なし</translation>
    </message>
    <message>
        <location line="+41"/>
        <source>Disabled</source>
        <translation>無効</translation>
    </message>
</context>
<context>
    <name>eMule::NzbFileChooserDialog</name>
    <message>
        <location filename="../src/gui/dialogs/NzbFileChooserDialog.cpp" line="+42"/>
        <source>Choose Files</source>
        <translation>ファイルを選択</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Unchecked files are not downloaded. The volumes of one archive are checked together, and PAR2 files are fetched only when a repair needs them.</source>
        <translation>チェックを外したファイルはダウンロードされません。1 つのアーカイブのボリュームはまとめてチェックされ、PAR2 ファイルは修復に必要な場合にのみ取得されます。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Name</source>
        <translation>名前</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Size</source>
        <translation>サイズ</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Reading…</source>
        <translation>読み込み中…</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>PAR2 files are fetched when a repair needs them.</source>
        <translation>PAR2 ファイルは修復に必要な場合に取得されます。</translation>
    </message>
    <message>
        <location line="+20"/>
        <location line="+20"/>
        <source>Not connected to the eMule core.</source>
        <translation>eMule コアに接続されていません。</translation>
    </message>
    <message>
        <location line="-14"/>
        <source>Cannot read %1.</source>
        <translation>%1 を読み取れません。</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>This file could not be read.</source>
        <translation>このファイルは読み取れませんでした。</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Keep at least one file of &quot;%1&quot;.</source>
        <translation>「%1」のファイルを少なくとも 1 つは残してください。</translation>
    </message>
</context>
<context>
    <name>eMule::OptionsDialog</name>
    <message>
        <location filename="../src/gui/dialogs/OptionsDialog.cpp" line="-2817"/>
        <source>Options</source>
        <translation>オプション</translation>
    </message>
    <message>
        <location line="+61"/>
        <location line="+1835"/>
        <source>OK</source>
        <translation>OK</translation>
    </message>
    <message>
        <location line="-1834"/>
        <location line="+1835"/>
        <source>Cancel</source>
        <translation>キャンセル</translation>
    </message>
    <message>
        <location line="-1834"/>
        <location line="+5443"/>
        <source>Apply</source>
        <translation>適用</translation>
    </message>
    <message>
        <location line="-5442"/>
        <source>Help</source>
        <translation>ヘルプ</translation>
    </message>
    <message>
        <location line="+254"/>
        <location line="+1822"/>
        <location line="+63"/>
        <location line="+5"/>
        <location line="+9"/>
        <location line="+11"/>
        <source>IP Filter</source>
        <translation>IP フィルター</translation>
    </message>
    <message>
        <location line="-1909"/>
        <source>IP filter reloaded: %1 entries.</source>
        <translation>IP フィルターを再読み込み：%1 エントリ。</translation>
    </message>
    <message>
        <location line="+89"/>
        <source>Options -&gt; %1 -&gt; %2</source>
        <translation>オプション -&gt; %1 -&gt; %2</translation>
    </message>
    <message>
        <location line="+177"/>
        <source>General options</source>
        <translation>全般オプション</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Advanced options</source>
        <translation>詳細オプション</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Usenet</source>
        <translation>Usenet</translation>
    </message>
    <message>
        <location line="+74"/>
        <source>User Name</source>
        <translation>ユーザー名</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+5339"/>
        <source>Language</source>
        <translation>言語</translation>
    </message>
    <message>
        <location line="-5336"/>
        <source>System Default</source>
        <translation>システム既定</translation>
    </message>
    <message>
        <location line="+14"/>
        <location line="+663"/>
        <location line="+262"/>
        <location line="+375"/>
        <location line="+291"/>
        <source>Miscellaneous</source>
        <translation>その他</translation>
    </message>
    <message>
        <location line="-1588"/>
        <source>Bring to front on link click</source>
        <translation>リンクのクリック時に前面に表示</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Prompt on exit</source>
        <translation>終了時に確認</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Enable online signature</source>
        <translation>オンライン署名を有効化</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Enable MiniMule</source>
        <translation>MiniMule を有効化</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Prevent standby mode while running</source>
        <translation>実行中はスタンバイモードを防止</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Edit Web Services...</source>
        <translation>Web サービスを編集...</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Web Services</source>
        <translation>Web サービス</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>webservices.dat was not found in the config folder.</source>
        <translation>webservices.dat が設定フォルダに見つかりませんでした。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Handle eD2K Links</source>
        <translation>eD2K リンクを処理</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Startup</source>
        <translation>起動</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Check for new version</source>
        <translation>新しいバージョンを確認</translation>
    </message>
    <message>
        <location line="+4"/>
        <source> Days</source>
        <translation> 日</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Show splash screen</source>
        <translation>スプラッシュスクリーンを表示</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Start minimized</source>
        <translation>最小化で起動</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Start with macOS</source>
        <translation>macOS と共に起動</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Start with Windows</source>
        <translation>Windows と共に起動</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Start with system</source>
        <translation>システムと共に起動</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+5275"/>
        <source>Core</source>
        <translation>コア</translation>
    </message>
    <message>
        <location line="-5270"/>
        <source>Address:</source>
        <translation>アドレス：</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+1105"/>
        <location line="+662"/>
        <location line="+421"/>
        <source>Port:</source>
        <translation>ポート：</translation>
    </message>
    <message>
        <location line="-2185"/>
        <source>authentication token</source>
        <translation>認証トークン</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Token:</source>
        <translation>トークン：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Changes require a restart to take effect.</source>
        <translation>変更を有効にするには再起動が必要です。</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+9"/>
        <source>Shutdown eMule Core</source>
        <translation>eMule コアをシャットダウン</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>This will shut down both the eMule Core and the GUI.

Are you sure you want to continue?</source>
        <translation>eMule コアと GUI の両方がシャットダウンされます。

続行してもよろしいですか？</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>Progressbar style</source>
        <translation>プログレスバーのスタイル</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>flat</source>
        <translation>フラット</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>round</source>
        <translation>丸形</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Tooltip delay time [sec.]</source>
        <translation>ツールチップの遅延時間 [秒]</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Minimize to system tray</source>
        <translation>システムトレイに最小化</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Download list double-click to expand</source>
        <translation>ダウンロードリストをダブルクリックで展開</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show percentage of download completion in progressbar</source>
        <translation>プログレスバーにダウンロード完了率を表示</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show downloaded data of unfinished parts and part boundaries</source>
        <translation>未完了パートのダウンロード済みデータとパート境界を表示</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Colours the data already downloaded in incomplete parts (green while being requested, grey otherwise) and marks every part boundary on the progress line.</source>
        <translation>未完了パートのダウンロード済みデータを色分けし（要求中は緑、それ以外は灰色）、進捗ラインにすべてのパート境界を表示します。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Show transfer rates on title</source>
        <translation>タイトルに転送速度を表示</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show download info on category tabs</source>
        <translation>カテゴリタブにダウンロード情報を表示</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Auto clear completed downloads</source>
        <translation>完了したダウンロードを自動クリア</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show additional toolbar on Transfers window</source>
        <translation>転送ウィンドウに追加ツールバーを表示</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show speed graph in toolbar</source>
        <translation>ツールバーに速度グラフを表示</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Speed graph time range (minutes):</source>
        <translation>速度グラフの時間範囲（分）:</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Remember open searches between restarts</source>
        <translation>再起動間で開いている検索を記憶</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Use original eMule icons</source>
        <translation>オリジナルの eMule アイコンを使用</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>A database ships with each release. An optional free &lt;a href=&quot;https://www.maxmind.com/en/geolite2/signup&quot;&gt;MaxMind GeoLite2&lt;/a&gt; account keeps it current in between. This product includes GeoLite2 data created by MaxMind.</source>
        <translation>各リリースにはデータベースが同梱されています。任意の無料 &lt;a href=&quot;https://www.maxmind.com/en/geolite2/signup&quot;&gt;MaxMind GeoLite2&lt;/a&gt; アカウントを使うと、リリースの間も最新の状態に保たれます。この製品には MaxMind が作成した GeoLite2 データが含まれています。</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Save CPU &amp;&amp; Memory Usage</source>
        <translation>CPU &amp;&amp; メモリ使用量を節約</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Disable Known Clients list</source>
        <translation>既知のクライアントリストを無効化</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Disable Queue list</source>
        <translation>キューリストを無効化</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Font for Server-, Message- and IRC-Window</source>
        <translation>サーバー、メッセージ、IRC ウィンドウのフォント</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Select Font...</source>
        <translation>フォントを選択...</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Select Font</source>
        <translation>フォントを選択</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Auto completion (history function)</source>
        <translation>自動補完（履歴機能）</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+1549"/>
        <location line="+128"/>
        <location line="+295"/>
        <location line="+1016"/>
        <location line="+152"/>
        <location line="+1049"/>
        <location line="+273"/>
        <location line="+29"/>
        <source>Enabled</source>
        <translation>有効</translation>
    </message>
    <message>
        <location line="-4489"/>
        <source>Reset</source>
        <translation>リセット</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>Capacities</source>
        <translation>容量</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Download</source>
        <translation>ダウンロード</translation>
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
        <translation>アップロード</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Limits</source>
        <translation>制限</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Download limit</source>
        <translation>ダウンロード制限</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Upload limit</source>
        <translation>アップロード制限</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Client Port</source>
        <translation>クライアントポート</translation>
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
        <translation>無効化</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Test Ports</source>
        <translation>ポートテスト</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Use UPnP to Setup Ports</source>
        <translation>UPnP でポートを設定</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Port forwarding: unknown</source>
        <translation>ポートフォワーディング: 不明</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Network interface</source>
        <translation>ネットワークインターフェース</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+6437"/>
        <source>Any</source>
        <translation>すべて</translation>
    </message>
    <message>
        <location line="-6435"/>
        <source>Use only this network interface, for example a VPN tunnel. Pick one from the list, or type an IP address or a subnet such as 10.64.0.0/10 (the interface holding an address in it). Connections through a proxy are not bound.</source>
        <translation>このネットワークインターフェース（VPN トンネルなど）のみを使用します。一覧から選ぶか、IP アドレスまたは 10.64.0.0/10 のようなサブネット（その範囲のアドレスを持つインターフェース）を入力してください。プロキシ経由の接続はバインドされません。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>While the selected interface is not available, all P2P, Usenet and update connections stay closed.</source>
        <translation>選択したインターフェースが利用できない間、P2P、Usenet、更新のすべての接続は閉じたままになります。</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Max. Sources/File</source>
        <translation>最大ソース/ファイル</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Hard limit</source>
        <translation>ハードリミット</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Connection Limits</source>
        <translation>接続制限</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Max. connections</source>
        <translation>最大接続数</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Autoconnect on startup</source>
        <translation>起動時に自動接続</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Reconnect on loss</source>
        <translation>切断時に再接続</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Show overhead bandwidth</source>
        <translation>オーバーヘッド帯域幅を表示</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Wizard...</source>
        <translation>ウィザード...</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Network</source>
        <translation>ネットワーク</translation>
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
        <translation>IPv6 用の個別キュー</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Alternate freed upload slots between IPv4 and IPv6 clients when both are waiting, so IPv6 peers are not outbid on score alone. When only one family is waiting, no slot is held back.</source>
        <translation>IPv4 と IPv6 の両方のクライアントが待機している場合、解放されたアップロードスロットを交互に割り当て、IPv6 のピアがスコアだけで負けないようにします。片方のみが待機している場合、スロットは保留されません。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Use IPv6 privacy address</source>
        <translation>IPv6 プライバシーアドレスを使用</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Connect to IPv6 peers and servers from the rotating temporary (RFC 4941) address. Off: always use the stable address, so peers can reach the address we advertise.</source>
        <translation>IPv6 のピアやサーバーへ、定期的に変わる一時アドレス (RFC 4941) から接続します。オフ: 常に固定アドレスを使用し、ピアが通知したアドレスに到達できるようにします。</translation>
    </message>
    <message>
        <location line="+53"/>
        <location line="+1302"/>
        <source>General</source>
        <translation>全般</translation>
    </message>
    <message>
        <location line="-1299"/>
        <source>Enable proxy</source>
        <translation>プロキシを有効化</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Proxy type:</source>
        <translation>プロキシの種類：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>No Proxy</source>
        <translation>プロキシなし</translation>
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
        <translation>プロキシホスト：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Proxy port:</source>
        <translation>プロキシポート：</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Authentication</source>
        <translation>認証</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Enable authentication</source>
        <translation>認証を有効化</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+1678"/>
        <location line="+1019"/>
        <location line="+152"/>
        <source>Name:</source>
        <translation>名前：</translation>
    </message>
    <message>
        <location line="-2845"/>
        <location line="+629"/>
        <location line="+730"/>
        <location line="+19"/>
        <location line="+324"/>
        <source>Password:</source>
        <translation>パスワード：</translation>
    </message>
    <message>
        <location line="-1668"/>
        <source>Update</source>
        <translation>更新</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Remove dead servers after</source>
        <translation>無応答サーバーを削除 回数：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>retries</source>
        <translation>回</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Auto-update server list at startup</source>
        <translation>起動時にサーバーリストを自動更新</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>List...</source>
        <translation>リスト...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Server List URL</source>
        <translation>サーバーリスト URL</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Enter the URL for server.met download:</source>
        <translation>server.met ダウンロードの URL を入力：</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Update server list when connecting to a server</source>
        <translation>サーバー接続時にサーバーリストを更新</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Update server list when a client connects</source>
        <translation>クライアント接続時にサーバーリストを更新</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Use smart LowID check on connect</source>
        <translation>接続時にスマート LowID チェックを使用</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Safe Connect</source>
        <translation>安全な接続</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Autoconnect to servers in static list only</source>
        <translation>静的リストのサーバーのみに自動接続</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Use priority system</source>
        <translation>優先度システムを使用</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Use the manual server order (drag/Move Up-Down)</source>
        <translation>サーバーの手動並び順を使用する（ドラッグ／上へ・下へ移動）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Set manually added servers to high priority</source>
        <translation>手動追加サーバーを高優先度に設定</translation>
    </message>
    <message>
        <location line="+100"/>
        <source>Incoming Files</source>
        <translation>受信ファイル</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Select Incoming Directory</source>
        <translation>受信ディレクトリを選択</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Temporary Files</source>
        <translation>一時ファイル</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Select Temporary Directory</source>
        <translation>一時ディレクトリを選択</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Shared Directories (Ctrl+Click includes subdirectories)</source>
        <translation>共有ディレクトリ（Ctrl+クリックでサブディレクトリを含む）</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Add UNC share</source>
        <translation>UNC 共有を追加</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Add UNC Share</source>
        <translation>UNC 共有を追加</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Enter UNC path (e.g., \\server\share):</source>
        <translation>UNC パスを入力してください（例: \\server\share）:</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Invalid Path</source>
        <translation>無効なパス</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>A UNC path must start with \\.</source>
        <translation>UNC パスは \\ で始まる必要があります。</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>UNC shares are only supported on Windows</source>
        <translation>UNC 共有は Windows でのみサポートされています</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Initializations</source>
        <translation>初期化</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Add files to download in paused mode</source>
        <translation>一時停止モードでダウンロードにファイルを追加</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Add new shared files with auto priority</source>
        <translation>自動優先度で新しい共有ファイルを追加</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Add new downloads with auto priority</source>
        <translation>自動優先度で新しいダウンロードを追加</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Remember download sources between restarts</source>
        <translation>再起動間でダウンロードソースを記憶</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Stores each download&apos;s best sources in the temp folder and reconnects to them on the next start, so a rare file does not have to find its peers again.</source>
        <translation>各ダウンロードの最良のソースを一時フォルダーに保存し、次回の起動時に再接続します。これにより、希少なファイルが再びソースを探す必要がなくなります。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Auto cleanup file names of new downloads</source>
        <translation>新しいダウンロードのファイル名を自動クリーンアップ</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+555"/>
        <source>Edit...</source>
        <translation>編集...</translation>
    </message>
    <message>
        <source>Filename Cleanup Rules</source>
        <translation type="vanished">ファイル名クリーンアップルール</translation>
    </message>
    <message>
        <source>Define patterns to automatically clean up filenames of new downloads.
Each rule replaces a regex pattern with a replacement string.</source>
        <translation type="vanished">新しいダウンロードのファイル名を自動的にクリーンアップするパターンを定義します。
各ルールは正規表現パターンを置換文字列に置き換えます。</translation>
    </message>
    <message>
        <source>Pattern</source>
        <translation type="vanished">パターン</translation>
    </message>
    <message>
        <source>Replacement</source>
        <translation type="vanished">置換</translation>
    </message>
    <message>
        <location line="-544"/>
        <source>Try to transfer full chunks to all uploads</source>
        <translation>すべてのアップロードにフルチャンクの転送を試行</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Try to download preview chunks first</source>
        <translation>プレビューチャンクを優先的にダウンロード</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Watch clipboard for eD2K links</source>
        <translation>クリップボードで eD2K ファイルリンクを監視</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Use advanced calculation method for remaining time</source>
        <translation>残り時間の高度な計算方法を使用</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Start next paused file when a file completes</source>
        <translation>ファイル完了時に次の一時停止ファイルを開始</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Prefer same category</source>
        <translation>同じカテゴリを優先</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Only in same category</source>
        <translation>同じカテゴリのみ</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Remember downloaded files</source>
        <translation>ダウンロード済みファイルを記憶</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Remember cancelled files</source>
        <translation>キャンセル済みファイルを記憶</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Video Player</source>
        <translation>ビデオプレーヤー</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Command</source>
        <translation>コマンド</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Select Video Player</source>
        <translation>ビデオプレーヤーを選択</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Arguments</source>
        <translation>引数</translation>
    </message>
    <message>
        <source>Create backup to preview</source>
        <translation type="vanished">プレビュー用にバックアップを作成</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Pop-up Message</source>
        <translation>ポップアップメッセージ</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>No sound</source>
        <translation>サウンドなし</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Test</source>
        <translation>テスト</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Play sound</source>
        <translation>サウンドを再生</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Speak notification message</source>
        <translation>通知メッセージを読み上げ</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Select Sound File</source>
        <translation>サウンドファイルを選択</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Sound Files (*.wav *.mp3 *.ogg);;All Files (*)</source>
        <translation>サウンドファイル (*.wav *.mp3 *.ogg);;すべてのファイル (*)</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Pop-up when</source>
        <translation>ポップアップ条件</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Log entry added</source>
        <translation>ログエントリが追加された</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Chat session started</source>
        <translation>チャットセッションが開始された</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Chat message received</source>
        <translation>チャットメッセージを受信した</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Download added</source>
        <translation>ダウンロードが追加された</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Download finished (*)</source>
        <translation>ダウンロードが完了した (*)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Urgent: out of disk space, server connection lost (*)</source>
        <translation>緊急：ディスク容量不足、サーバー接続喪失 (*)</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>(*) Email Notifications</source>
        <translation>(*) メール通知</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Enable email notifications</source>
        <translation>メール通知を有効化</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>SMTP server...</source>
        <translation>SMTP サーバー...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Recipient address:</source>
        <translation>受信者アドレス：</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Sender address:</source>
        <translation>送信者アドレス：</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>SMTP Server Settings</source>
        <translation>SMTP サーバー設定</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Server:</source>
        <translation>サーバー：</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+1130"/>
        <source>None</source>
        <translation>なし</translation>
    </message>
    <message>
        <location line="-1129"/>
        <source>Plain</source>
        <translation>プレーン</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Authentication:</source>
        <translation>認証：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Use TLS/STARTTLS</source>
        <translation>TLS/STARTTLS を使用</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Username:</source>
        <translation>ユーザー名：</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Server</source>
        <translation>サーバー</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Nick</source>
        <translation>ニックネーム</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Channels</source>
        <translation>チャンネル</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Use channel list filter</source>
        <translation>チャンネルリストフィルターを使用</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+942"/>
        <location line="+1027"/>
        <location line="+154"/>
        <source>Name</source>
        <translation>名前</translation>
    </message>
    <message>
        <location line="-2121"/>
        <source>Users</source>
        <translation>ユーザー</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Perform</source>
        <translation>実行</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Use perform string on connect</source>
        <translation>接続時に perform 文字列を使用</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Connect to help channel</source>
        <translation>ヘルプチャンネルに接続</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Load server channel list on connect</source>
        <translation>接続時にサーバーチャンネルリストを読み込み</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Add timestamp to messages</source>
        <translation>メッセージにタイムスタンプを追加</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Ignore info messages</source>
        <translation>情報メッセージを無視</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Ignore misc. info messages</source>
        <translation>その他の情報メッセージを無視</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Ignore Join info messages</source>
        <translation>Join メッセージを無視</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Ignore Part info messages</source>
        <translation>Part メッセージを無視</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Ignore Quit info messages</source>
        <translation>Quit メッセージを無視</translation>
    </message>
    <message>
        <location line="+50"/>
        <source>Messages</source>
        <translation>メッセージ</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Filter messages containing: (Separator | )</source>
        <translation>含むメッセージをフィルター：（区切り | ）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Accept from friends only</source>
        <translation>フレンドからのみ受け入れ</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Advanced spam filter</source>
        <translation>高度なスパムフィルター</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Require captcha authentication</source>
        <translation>CAPTCHA 認証を要求</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Show smileys</source>
        <translation>スマイリーを表示</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Comments</source>
        <translation>コメント</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Ignore comments containing: (Separator | )</source>
        <translation>含むコメントを無視：（区切り | ）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Indicate downloads with comments/rating by icon</source>
        <translation>コメント/評価付きダウンロードをアイコンで表示</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Filter servers too</source>
        <translation>サーバーもフィルター</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Filter level:   &lt;</source>
        <translation>フィルターレベル：   &lt;</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+409"/>
        <source>Reload</source>
        <translation>再読み込み</translation>
    </message>
    <message>
        <location line="-386"/>
        <source>http://example.com/ipfilter.dat</source>
        <translation>http://example.com/ipfilter.dat</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+19"/>
        <source>Load</source>
        <translation>読み込み</translation>
    </message>
    <message>
        <location line="-13"/>
        <source>Loading...</source>
        <translation>読み込み中...</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Failed to download IP filter: %1</source>
        <translation>IPフィルターのダウンロードに失敗しました: %1</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Downloaded IP filter is empty.</source>
        <translation>ダウンロードした IP フィルターが空です。</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Failed to save IP filter: %1</source>
        <translation>IP フィルターの保存に失敗しました: %1</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>IP filter updated and reloaded.</source>
        <translation>IPフィルターが更新され再読み込みされました。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>IP filter updated and reloaded (unpacked &quot;%1&quot;).</source>
        <translation>IP フィルターを更新して再読み込みしました（&quot;%1&quot; を展開）。</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>See My Shared Files/Directories</source>
        <translation>共有ファイル/ディレクトリの閲覧</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Everybody</source>
        <translation>全員</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Friends only</source>
        <translation>フレンドのみ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nobody</source>
        <translation>誰にも許可しない</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Protocol Obfuscation</source>
        <translation>プロトコル難読化</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Enable protocol obfuscation</source>
        <translation>プロトコル難読化を有効化</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Allow obfuscated connections only (not recommended)</source>
        <translation>難読化接続のみ許可（非推奨）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Disable support for obfuscated connections</source>
        <translation>難読化接続のサポートを無効化</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Use secure identification</source>
        <translation>安全な識別を使用</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Run eMule as unprivileged user</source>
        <translation>非特権ユーザーとして eMule を実行</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Enable spam filter for search results</source>
        <translation>検索結果のスパムフィルターを有効化</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Warn when opening untrusted files</source>
        <translation>信頼できないファイルを開く時に警告</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Graphs</source>
        <translation>グラフ</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Update delay: 3 sec</source>
        <translation>更新遅延：3 秒</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+154"/>
        <source>Update delay: %1 sec</source>
        <translation>更新遅延：%1 秒</translation>
    </message>
    <message>
        <location line="-153"/>
        <location line="+154"/>
        <source>Update delay: disabled</source>
        <translation>更新遅延：無効</translation>
    </message>
    <message>
        <location line="-147"/>
        <source>Time for average graph: 5 mins</source>
        <translation>平均グラフの時間：5 分</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Time for average graph: %1 mins</source>
        <translation>平均グラフの時間：%1 分</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Colors</source>
        <translation>色</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Background</source>
        <translation>背景</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Grid</source>
        <translation>グリッド</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Download Current</source>
        <translation>現在のダウンロード</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Download Average</source>
        <translation>ダウンロード平均</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Download Session</source>
        <translation>ダウンロードセッション</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Upload Current</source>
        <translation>現在のアップロード</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Upload Average</source>
        <translation>アップロード平均</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Upload Session</source>
        <translation>アップロードセッション</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Active Connections</source>
        <translation>アクティブ接続</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Total Uploads</source>
        <translation>合計アップロード</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Active Uploads</source>
        <translation>アクティブアップロード</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Icon Bar</source>
        <translation>アイコンバー</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Active Downloads</source>
        <translation>アクティブダウンロード</translation>
    </message>
    <message>
        <location line="-4"/>
        <source>Upload Friend Slots</source>
        <translation>アップロードフレンドスロット</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>Upload Slots (no overhead)</source>
        <translation>アップロードスロット（オーバーヘッドなし）</translation>
    </message>
    <message>
        <location line="-1477"/>
        <source>Country flags (IP2Country)</source>
        <translation>国旗 (IP2Country)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Show country flags</source>
        <translation>国旗を表示</translation>
    </message>
    <message>
        <source>Country column:</source>
        <translation type="vanished">国の列:</translation>
    </message>
    <message>
        <source>Hidden</source>
        <translation type="vanished">非表示</translation>
    </message>
    <message>
        <source>Short name</source>
        <translation type="vanished">短縮名</translation>
    </message>
    <message>
        <source>Long name</source>
        <translation type="vanished">正式名</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>MaxMind account ID:</source>
        <translation>MaxMind アカウント ID:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>License key:</source>
        <translation>ライセンスキー:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Update the database weekly</source>
        <translation>データベースを毎週更新</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Update now</source>
        <translation>今すぐ更新</translation>
    </message>
    <message>
        <source>Needs a free &lt;a href=&quot;https://www.maxmind.com/en/geolite2/signup&quot;&gt;MaxMind GeoLite2&lt;/a&gt; account. This product includes GeoLite2 data created by MaxMind.</source>
        <translation type="vanished">無料の &lt;a href=&quot;https://www.maxmind.com/en/geolite2/signup&quot;&gt;MaxMind GeoLite2&lt;/a&gt; アカウントが必要です。この製品には MaxMind が作成した GeoLite2 データが含まれています。</translation>
    </message>
    <message>
        <location line="+334"/>
        <source>Use for news servers</source>
        <translation>ニュースサーバーにも使用する</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Route Usenet downloads, availability checks and the news server Test button through this proxy too. News servers switch over as soon as you press OK.

Every Usenet connection then passes through the proxy, so its speed caps the download, and many HTTP proxies only allow connections to port 443.</source>
        <translation>Usenet のダウンロード、可用性の確認、ニュースサーバーのテストボタンもこのプロキシ経由にします。ニュースサーバーは OK を押すとすぐに切り替わります。

すべての Usenet 接続がプロキシを通るため、その速度がダウンロード速度の上限になります。また、多くの HTTP プロキシはポート 443 への接続しか許可していません。</translation>
    </message>
    <message>
        <location line="+411"/>
        <source>Remember files seen in search results</source>
        <translation>検索結果に出たファイルを記憶する</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Keeps a local record of the files that turned up in your searches and the names they went by, so later searches can show what you have seen before.</source>
        <translation>検索に現れたファイルとそのときの名前をローカルに記録し、以後の検索で以前に見たものを表示できるようにします。</translation>
    </message>
    <message>
        <location line="+389"/>
        <source>Ignore eMule add friend protocol messages</source>
        <translation>eMule のフレンド追加プロトコルメッセージを無視</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ignore eMule send link protocol messages</source>
        <translation>eMule のリンク送信プロトコルメッセージを無視</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Allow others to add you as a friend</source>
        <translation>他のユーザーによるフレンド追加を許可</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Accept eD2K links in IRC (Use only with caution!)</source>
        <translation>IRC で eD2K リンクを受け入れる (注意して使用してください！)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>From friends only</source>
        <translation>フレンドからのみ</translation>
    </message>
    <message>
        <location line="+314"/>
        <source>Download Usenet</source>
        <translation>Usenet ダウンロード</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Default</source>
        <translation>既定値</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Restore this colour to the eMule default</source>
        <translation>この色を eMule の既定値に戻す</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Auto</source>
        <translation>自動</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Select Color</source>
        <translation>色を選択</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Draw filled graphs</source>
        <translation>塗りつぶしグラフを描画</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Connections statistics Y-axis scale:</source>
        <translation>接続統計 Y 軸スケール：</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Active connections ratio:</source>
        <translation>アクティブ接続比率：</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Statistics Tree</source>
        <translation>統計ツリー</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Update delay: 5 sec</source>
        <translation>更新遅延：5 秒</translation>
    </message>
    <message>
        <location line="+40"/>
        <source>Enable REST API</source>
        <translation>REST API を有効化</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Serves /api/v1 with its description and a playground at /api/v1/docs. Calls need the API key.</source>
        <translation>/api/v1 とその説明、および /api/v1/docs の試用ページを提供します。呼び出しには API キーが必要です。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Enable MCP server for AI assistants</source>
        <translation>AI アシスタント用 MCP サーバーを有効にする</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Serves the Model Context Protocol at /mcp on the same port, with the same API key. Independent of the REST API.</source>
        <translation>同じポートの /mcp で、同じ API キーを使って Model Context Protocol を提供します。REST API とは独立しています。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Read-only</source>
        <translation>読み取り専用</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Assistants can look at everything but change nothing.</source>
        <translation>アシスタントはすべてを参照できますが、何も変更できません。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Gzip compression</source>
        <translation>Gzip 圧縮</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Include port into UPnP setup</source>
        <translation>UPnP 設定にポートを含める</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Listen address:</source>
        <translation>待ち受けアドレス:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>all interfaces</source>
        <translation>すべてのインターフェース</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Address the web server listens on. Empty: every interface, reachable from other hosts. 127.0.0.1: this computer only.</source>
        <translation>Web サーバーが待ち受けるアドレス。空欄: すべてのインターフェース(他のホストからアクセス可能)。127.0.0.1: このコンピューターのみ。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Template:</source>
        <translation>テンプレート：</translation>
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
        <translation>セッションタイムアウト：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>minutes</source>
        <translation>分</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Use HTTPS</source>
        <translation>HTTPS を使用</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Create new certificate</source>
        <translation>新しい証明書を作成</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Certificate:</source>
        <translation>証明書：</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Key:</source>
        <translation>鍵：</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>REST API Key:</source>
        <translation>REST API キー：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Administrator</source>
        <translation>管理者</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Allow exit eMule, reboot and shutdown</source>
        <translation>eMule の終了、再起動、シャットダウンを許可</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Guest</source>
        <translation>ゲスト</translation>
    </message>
    <message>
        <location line="+55"/>
        <source>Web template reloaded</source>
        <translation>Web テンプレートを再読み込みしました</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Web template reload failed</source>
        <translation>Web テンプレートの再読み込みに失敗しました</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Select Template File</source>
        <translation>テンプレートファイルを選択</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Template files (*.tmpl);;All files (*)</source>
        <translation>テンプレートファイル (*.tmpl);;すべてのファイル (*)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Select Certificate File</source>
        <translation>証明書ファイルを選択</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>PEM files (*.pem *.crt);;All files (*)</source>
        <translation>PEM ファイル (*.pem *.crt);;すべてのファイル (*)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Select Key File</source>
        <translation>鍵ファイルを選択</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>PEM files (*.pem *.key);;All files (*)</source>
        <translation>PEM ファイル (*.pem *.key);;すべてのファイル (*)</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Save Certificate</source>
        <translation>証明書を保存</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>PEM files (*.pem)</source>
        <translation>PEM ファイル (*.pem)</translation>
    </message>
    <message>
        <location line="+131"/>
        <source>Enable Usenet downloads</source>
        <translation>Usenet のダウンロードを有効化</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Gates automatic activity only. Adding a download by hand always works.</source>
        <translation>自動的な動作のみを制御します。手動でのダウンロード追加は常に可能です。</translation>
    </message>
    <message>
        <location line="+26"/>
        <location line="+38"/>
        <source>Account</source>
        <translation>アカウント</translation>
    </message>
    <message>
        <location line="-36"/>
        <source>Advanced</source>
        <translation>詳細</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+628"/>
        <location line="+274"/>
        <location line="+15"/>
        <source>News servers</source>
        <translation>ニュースサーバー</translation>
    </message>
    <message>
        <location line="-911"/>
        <source>Host</source>
        <translation>ホスト</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Port</source>
        <translation>ポート</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Priority</source>
        <translation>優先度</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Connections</source>
        <translation>接続数</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Used</source>
        <translation>使用量</translation>
    </message>
    <message>
        <location line="+41"/>
        <source>Display name (optional)</source>
        <translation>表示名（任意）</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Host:</source>
        <translation>ホスト:</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Encryption:</source>
        <translation>暗号化:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>None (119)</source>
        <translation>なし (119)</translation>
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
        <translation>ユーザー:</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Never set this above what your provider allows — exceeding the limit gets the account throttled, not queued.</source>
        <translation>プロバイダーが許可する数を超えて設定しないでください — 上限を超えると、接続が待たされるのではなくアカウントが帯域制限されます。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Connections:</source>
        <translation>接続数:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Lower is tried first. A higher level is only used for articles that every server below reported as missing — that is what makes a block or fill account worth having.</source>
        <translation>小さい値から順に試されます。上位のレベルは、下位のすべてのサーバーが欠落と報告した記事にのみ使用されます — ブロックアカウントや補完用アカウントが役に立つのはこのためです。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Priority level:</source>
        <translation>優先レベル:</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Account options</source>
        <translation>アカウントオプション</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Unknown</source>
        <translation>不明</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+1004"/>
        <location line="+165"/>
        <source> days</source>
        <translation> 日</translation>
    </message>
    <message>
        <location line="-1168"/>
        <source>Retention:</source>
        <translation>保持期間:</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Accounts sharing a group number count as one for connection limits — use it when the same provider is reached through two host names, so the two entries cannot open twice what the plan allows.</source>
        <translation>同じグループ番号を持つアカウントは、接続数の上限では 1 つとして数えられます — 同じプロバイダーに 2 つのホスト名で接続する場合に使用すると、2 つの項目がプランの許可する接続数の 2 倍を開くことを防げます。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Connection group:</source>
        <translation>接続グループ:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>None — accept any certificate</source>
        <translation>なし — すべての証明書を受け入れる</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Minimal — allow a host name mismatch</source>
        <translation>最小限 — ホスト名の不一致を許可する</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Strict</source>
        <translation>厳格</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Certificate check:</source>
        <translation>証明書の確認:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Optional — never fail a download on its own</source>
        <translation>任意 — これだけでダウンロードを失敗させません</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Send GROUP before fetching (only needed by a few old servers)</source>
        <translation>取得前に GROUP を送信する（一部の古いサーバーでのみ必要）</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Unmetered</source>
        <translation>無制限</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Monthly allowance</source>
        <translation>月間の通信量上限</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Block account (prepaid)</source>
        <translation>ブロックアカウント（前払い）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Allowance:</source>
        <translation>通信量上限:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source> GB</source>
        <translation> GB</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>No limit</source>
        <translation>制限なし</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Allowance size:</source>
        <translation>上限サイズ:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Your billing day — providers reset on the day you signed up, not on the 1st. A month shorter than this rolls over on its last day.</source>
        <translation>請求日です — プロバイダーは 1 日ではなく、契約した日にリセットします。これより短い月は、その月の最終日に繰り越されます。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Resets on day:</source>
        <translation>リセット日:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>When the allowance is spent, use the next priority level</source>
        <translation>通信量上限を使い切ったら、次の優先レベルを使用する</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Off by default: block credit usually costs more per GB than the plan it would be covering, and spending it without being asked is the one thing a limit exists to prevent. Left off, downloads wait for the allowance instead — they are never failed and no article is ever given up on.</source>
        <translation>既定ではオフです。ブロッククレジットは通常、それが補うプランよりも GB あたりの単価が高く、確認なしに消費してしまうことこそ、上限を設ける意味を失わせるものだからです。オフのままなら、ダウンロードは上限が回復するまで待機します — 失敗することはなく、記事が諦められることもありません。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Used:</source>
        <translation>使用量:</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Correct…</source>
        <translation>修正…</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Downloading</source>
        <translation>ダウンロード中</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Retry a failed server after:</source>
        <translation>失敗したサーバーを再試行するまで:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Never back off</source>
        <translation>待機しない</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Applies to every account: how long a server that refused or dropped a connection is passed over before it is tried again.</source>
        <translation>すべてのアカウントに適用されます: 接続を拒否または切断したサーバーを、再試行するまでどれだけの間スキップするかを指定します。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Share of the download limit:</source>
        <translation>ダウンロード制限の割り当て:</translation>
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
        <translation>eD2K も同時にダウンロードしているときに、Usenet が全体のダウンロード制限のうちどれだけを使えるかを指定します。アイドル状態のエンジンは自分の割り当てをすべてもう一方に譲るため、これは両方が動作しているときにのみ適用されます。</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>When adding</source>
        <translation>追加時</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Check availability:</source>
        <translation>可用性の確認:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Do not check</source>
        <translation>確認しない</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Sample one article per file</source>
        <translation>ファイルごとに 1 記事を抽出して確認</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Check every article</source>
        <translation>すべての記事を確認</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Before downloading anything, ask your providers whether they still hold the release. It costs one small request per article asked about and no payload at all.

Sampling asks about the first article of each file, which is usually enough: providers expire whole posts by date, so a file is almost always present or absent as a unit. Checking every article is certain but can mean tens of thousands of requests for a large release.

The answer is never a verdict. Nothing here can stop an article being fetched — an article your providers deny may still arrive, and a release this pauses downloads normally when you resume it.</source>
        <translation>ダウンロードを始める前に、そのリリースをまだ保持しているかどうかをプロバイダーに問い合わせます。問い合わせた記事 1 件につき小さなリクエストが 1 回かかるだけで、データ本体の転送はありません。

サンプリングは各ファイルの最初の記事について問い合わせるもので、通常はこれで十分です。プロバイダーは投稿全体を日付単位で期限切れにするため、ファイルはほぼ常にまとめて存在するか、まとめて存在しないかのどちらかです。すべての記事を確認すれば確実ですが、大きなリリースでは数万件のリクエストになることがあります。

この答えは判定ではありません。ここでの結果が記事の取得を止めることはありません — プロバイダーが否定した記事でも届くことがありますし、これによって一時停止されたリリースも、再開すれば通常どおりダウンロードされます。</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Pause below:</source>
        <translation>一時停止のしきい値:</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+1300"/>
        <source>never</source>
        <translation>しない</translation>
    </message>
    <message>
        <location line="-1297"/>
        <source>A release that looks emptier than this is added paused, with the reason shown, so you decide rather than the guess. It is never failed and never refused.

A shortfall the release&apos;s own PAR2 recovery volumes can cover does not pause it, however low the figure goes.</source>
        <translation>これより内容が少なく見えるリリースは、理由を表示したうえで一時停止状態で追加されます。推測ではなくあなたが判断できるようにするためです。失敗させたり拒否したりすることはありません。

リリース自身の PAR2 リカバリーボリュームで補える不足であれば、数値がどれだけ低くても一時停止されません。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Start automatic downloads paused</source>
        <translation>自動ダウンロードを一時停止状態で開始</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Applies to anything queued without you asking for it directly: the watch folder below, and feeds.

With this on, an automatic download waits for you to press Resume, so a feed proposes rather than decides. Anything you add yourself starts normally either way.</source>
        <translation>自分で直接指示していないものすべてに適用されます: 下の監視フォルダとフィードです。

これをオンにすると、自動的なダウンロードは「再開」を押すまで待機するため、フィードは決定するのではなく提案するだけになります。自分で追加したものは、どちらの設定でも通常どおり開始します。</translation>
    </message>
    <message>
        <location line="+10"/>
        <location line="+49"/>
        <source>Watch folder</source>
        <translation>監視フォルダ</translation>
    </message>
    <message>
        <location line="-45"/>
        <source>Any .nzb file left in this folder is queued and then moved into a _processed subfolder — or _failed, if it could not be read.</source>
        <translation>このフォルダに置かれた .nzb ファイルはキューに追加され、その後 _processed サブフォルダに移動されます — 読み取れなかった場合は _failed に移動されます。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>No folder is being watched</source>
        <translation>監視中のフォルダはありません</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>A file is only read once it has stopped changing, so a large .nzb still being copied in is left alone until it is complete.

It cannot be inside your temp, incoming or configuration folders: the daemon writes there itself.</source>
        <translation>ファイルは変更が止まってから読み込まれるため、コピー中の大きな .nzb は完了するまでそのままにされます。

監視フォルダは、一時フォルダ、受信フォルダ、設定フォルダの中には置けません: デーモン自身がそこに書き込むためです。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Browse…</source>
        <translation>参照…</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Desktop</source>
        <translation>デスクトップ</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Open .nzb files with eMule Qt</source>
        <translation>.nzb ファイルを eMule Qt で開く</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Claim .nzb files for this copy of eMule Qt, so double-clicking one queues it. The setting is for you alone and needs no administrator; it is re-applied at every start, so another program taking the association does not keep it.</source>
        <translation>.nzb ファイルをこの eMule Qt に関連付けて、ダブルクリックするだけでキューに追加できるようにします。この設定はあなた専用で、管理者権限は不要です。起動のたびに再適用されるため、他のプログラムが関連付けを奪っても元に戻ります。</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>After downloading</source>
        <translation>ダウンロード後</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Verify and repair with PAR2</source>
        <translation>PAR2 で検証して修復する</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Check the finished files against the release&apos;s PAR2 set and repair any damage from its recovery volumes. The recovery volumes are only downloaded when something actually needs repairing.

With this off, a release with missing articles fails instead of being shared, because there is no way to tell whether it is intact.</source>
        <translation>完了したファイルをリリースの PAR2 セットと照合し、破損があればそのリカバリーボリュームから修復します。リカバリーボリュームは、実際に修復が必要になったときにのみダウンロードされます。

これをオフにすると、記事が欠落しているリリースは共有されずに失敗します。完全かどうかを確かめる手段がないためです。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Restore filenames from PAR2</source>
        <translation>PAR2 からファイル名を復元する</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Obfuscated releases are posted under meaningless filenames. The PAR2 metadata carries the real ones, and without them the archives cannot be identified for unpacking either.</source>
        <translation>難読化されたリリースは意味のないファイル名で投稿されます。PAR2 のメタデータには本来の名前が含まれており、それがないとアーカイブを識別して展開することもできません。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Verify with SFV when there is no PAR2</source>
        <translation>PAR2 がない場合は SFV で検証する</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>A release posted without a PAR2 set often comes with an .sfv file instead. Its checksums cannot repair anything, but a release they call damaged is not published.</source>
        <translation>PAR2 セットなしで投稿されたリリースには、代わりに .sfv ファイルが付いていることがよくあります。そのチェックサムでは修復はできませんが、破損と判定されたリリースは公開されません。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Unpack archives</source>
        <translation>アーカイブを展開する</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Extract RAR, 7z and ZIP volume sets once they have been verified.

Password-protected archives need 7-Zip or unrar installed — eMule&apos;s own archive reader can only decrypt ZIP.</source>
        <translation>検証が完了した RAR、7z、ZIP のボリュームセットを展開します。

パスワード保護されたアーカイブには 7-Zip または unrar のインストールが必要です — eMule 自身のアーカイブリーダーは ZIP しか復号できません。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Unpack while downloading</source>
        <translation>ダウンロード中に展開する</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Extract each archive volume as soon as it finishes instead of waiting for the whole release, so the content is ready the moment the download is.

It is the same extraction, moved earlier, so it costs no extra disk space. If the release turns out to need repairing, the result is discarded and it is unpacked again afterwards.</source>
        <translation>リリース全体を待たずに、各アーカイブボリュームが完了した時点で展開します。ダウンロードが終わった瞬間に内容を使えるようになります。

同じ展開処理を前倒しするだけなので、ディスク容量を余分に消費することはありません。修復が必要だと判明した場合は、結果を破棄して修復後にもう一度展開します。</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Preview password-protected releases while downloading</source>
        <translation>パスワード保護されたリリースをダウンロード中にプレビューする</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>An encrypted archive cannot be read a piece at a time, so previewing one means decrypting it again from the first volume every time more of it arrives.

Nothing runs unless a preview is actually open, and only RAR releases can do it at all — an incomplete 7z set decodes to nothing.</source>
        <translation>暗号化されたアーカイブは少しずつ読み進めることができないため、プレビューするにはデータが届くたびに最初のボリュームから復号し直すことになります。

プレビューを実際に開いているときにしか動作せず、そもそも対応できるのは RAR のリリースだけです — 不完全な 7z セットは何も復号できません。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Unpacker:</source>
        <translation>展開プログラム:</translation>
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
        <translation>パスワード保護されたアーカイブ用の 7-Zip または unrar の実行ファイルのパスです。

空欄にすると通常の場所を検索します。eMule をバックグラウンドサービスとして実行する場合は指定してください。サービスの検索パスは、ターミナルのものよりずっと短いことがよくあります。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Delete archives and PAR2 files after unpacking</source>
        <translation>展開後にアーカイブと PAR2 ファイルを削除する</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Keep only the unpacked content. Turning this off roughly doubles the disk space a release uses and shares the archive volumes and recovery files with eD2K peers, who have no use for them.</source>
        <translation>展開された内容だけを残します。これをオフにすると、リリースが使うディスク容量はおよそ 2 倍になり、アーカイブのボリュームやリカバリーファイルまで eD2K のピアに共有されます。ピアにとっては不要なものです。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Keep downloading</source>
        <translation>ダウンロードを続ける</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+11"/>
        <source>Pause it</source>
        <translation>一時停止する</translation>
    </message>
    <message>
        <location line="-10"/>
        <location line="+11"/>
        <source>Fail it</source>
        <translation>失敗にする</translation>
    </message>
    <message>
        <location line="-9"/>
        <source>A release that has lost more than its recovery files could ever repair stops here instead of using up your allowance until the final check. The estimate only ever errs towards downloading.

Resume downloads it anyway.</source>
        <translation>リカバリーファイルで修復できる限度を超えて欠落したリリースは、最終チェックまで通信量を使い切ることなくここで停止します。この推定は常にダウンロードを続ける側に誤差を取ります。

「再開」すればそのままダウンロードします。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>When a download cannot be repaired:</source>
        <translation>ダウンロードを修復できない場合:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Publish anyway</source>
        <translation>そのまま公開する</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>A movie or episode whose download contains programs or shortcuts is almost always a fake. Only releases with video or audio in them are checked, so software downloads are not affected.

Resume publishes it anyway.</source>
        <translation>映画やエピソードのダウンロードにプログラムやショートカットが含まれている場合、ほぼ確実に偽物です。チェックされるのは映像や音声を含むリリースだけなので、ソフトウェアのダウンロードには影響しません。

「再開」すればそのまま公開します。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>When a media release has unwanted files:</source>
        <translation>メディアリリースに不要なファイルがある場合:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>File extensions, separated by commas. A video file that is not really a video counts as well. Leave this empty to turn the check off.</source>
        <translation>ファイル拡張子をカンマ区切りで指定します。実際には動画ではない動画ファイルも対象になります。空欄にするとこのチェックを無効にします。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Unwanted file types:</source>
        <translation>不要なファイルの種類:</translation>
    </message>
    <message>
        <location line="+191"/>
        <source>The news server list could not be saved.</source>
        <translation>ニュースサーバーのリストを保存できませんでした。</translation>
    </message>
    <message>
        <location line="+146"/>
        <source>(unchanged)</source>
        <translation>(変更なし)</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>(none set)</source>
        <translation>(未設定)</translation>
    </message>
    <message>
        <location line="+60"/>
        <source>not measured — the Usenet engine is stopped</source>
        <translation>未計測 — Usenet エンジンは停止しています</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>%1 used</source>
        <translation>%1 使用</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1 of %2</source>
        <translation>%1 / %2</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>, resets %1</source>
        <translation>、%1 にリセット</translation>
    </message>
    <message>
        <location line="+5"/>
        <source> — spent</source>
        <translation> — 使用済み</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Measured here, not reported by the provider — NNTP has no command that asks. Expect a few percent below your provider&apos;s own figure.</source>
        <translation>この値はここで計測したもので、プロバイダーから報告されたものではありません — NNTP には問い合わせるコマンドがありません。プロバイダー自身の数値より数パーセント低くなります。</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Correct usage</source>
        <translation>使用量を修正</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Used this period, in GB.

Enter what your provider&apos;s control panel says, or 0 to start again.</source>
        <translation>今期の使用量（GB）です。

プロバイダーの管理画面に表示されている値を入力するか、0 を入力してやり直してください。</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>The usage counter could not be changed.</source>
        <translation>使用量カウンターを変更できませんでした。</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>At most %1 news servers can be configured.</source>
        <translation>設定できるニュースサーバーは最大 %1 台です。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>New server</source>
        <translation>新しいサーバー</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Enter a host name first.</source>
        <translation>先にホスト名を入力してください。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Connecting…</source>
        <translation>接続中…</translation>
    </message>
    <message>
        <location line="+51"/>
        <source>Search indexers answer keyword searches and hand back an NZB. They are separate from your news servers: on Usenet the provider you download from and the service you search are different businesses.</source>
        <translation>検索インデクサーはキーワード検索に応答して NZB を返します。ニュースサーバーとは別物です: Usenet では、ダウンロード先のプロバイダーと検索に使うサービスは別の事業者です。</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+384"/>
        <location line="+541"/>
        <source>Indexers</source>
        <translation>インデクサー</translation>
    </message>
    <message>
        <location line="-920"/>
        <source>URL</source>
        <translation>URL</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Type</source>
        <translation>種類</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>API key</source>
        <translation>API キー</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Indexer</source>
        <translation>インデクサー</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+152"/>
        <source>Display name</source>
        <translation>表示名</translation>
    </message>
    <message>
        <location line="-150"/>
        <source>Also the identity of this account: it names the cached capabilities and appears in the Indexer column of the results.</source>
        <translation>このアカウントの識別名でもあります: キャッシュされた機能情報の名前になり、結果のインデクサー列に表示されます。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>The API base URL. A bare host gets &quot;/api&quot; added; a URL that already has a path is used exactly as typed, which is what Jackett and NZBHydra2 endpoints need.</source>
        <translation>API のベース URL です。ホスト名だけの場合は &quot;/api&quot; が追加されます。すでにパスを含む URL は入力したとおりに使用されます。Jackett や NZBHydra2 のエンドポイントにはこれが必要です。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>API URL:</source>
        <translation>API URL:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>API key:</source>
        <translation>API キー:</translation>
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
        <translation>両方 — Prowlarr、NZBHydra2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type:</source>
        <translation>種類：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Searching</source>
        <translation>検索</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Rows to ask for per request. An indexer that allows fewer silently returns fewer, so this is an upper bound rather than a promise.</source>
        <translation>1 回のリクエストで要求する件数です。これより少ない件数しか許可しないインデクサーは黙って少なく返すため、これは約束ではなく上限です。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Results per request:</source>
        <translation>リクエストあたりの結果数:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>How many pages one search may fetch from each indexer.

Every page is an API call against the allowance your account has, so this is a spending limit, not a speed setting.</source>
        <translation>1 回の検索で各インデクサーから取得できるページ数です。

各ページはアカウントの利用枠を消費する API 呼び出しになるため、これは速度ではなく消費量の上限です。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Pages per search:</source>
        <translation>検索あたりのページ数:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Request timeout:</source>
        <translation>リクエストのタイムアウト:</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>How often to re-read what each indexer supports. A stale answer never blocks a search — it only means a query field stays greyed out that the indexer has since started accepting.</source>
        <translation>各インデクサーが対応している機能を再取得する間隔です。情報が古くても検索が妨げられることはありません — インデクサーが後から受け付けるようになった検索項目が、グレー表示のままになるだけです。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Refresh capabilities every:</source>
        <translation>機能情報の更新間隔:</translation>
    </message>
    <message>
        <location line="+51"/>
        <source>A feed is a search that runs on its own and queues what it finds. Its first check adds nothing — it only records what the indexer already lists, because otherwise a new feed would download everything still on the server.</source>
        <translation>フィードは自動的に実行され、見つけたものをキューに追加する検索です。最初のチェックでは何も追加されません — インデクサーがすでに掲載しているものを記録するだけです。そうしないと、新しいフィードがサーバーに残っているものをすべてダウンロードしてしまうためです。</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+324"/>
        <location line="+277"/>
        <source>Feeds</source>
        <translation>フィード</translation>
    </message>
    <message>
        <location line="-596"/>
        <source>Search</source>
        <translation>検索</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Every</source>
        <translation>間隔</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Last checked</source>
        <translation>最終チェック</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Feed</source>
        <translation>フィード</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Also the identity of this feed: it names the file that remembers what the feed has already seen.</source>
        <translation>このフィードの識別名でもあります: フィードがすでに見たものを記憶するファイルの名前になります。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Search my indexers</source>
        <translation>自分のインデクサーを検索</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>An RSS address I paste</source>
        <translation>貼り付けた RSS アドレス</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Source:</source>
        <translation>ソース:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Keywords. Leave it empty to take everything new in the categories below.</source>
        <translation>キーワードです。空欄にすると、下のカテゴリの新着をすべて取り込みます。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Search for:</source>
        <translation>検索：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>e.g. 2000, 5000</source>
        <translation>例: 2000, 5000</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Newznab category numbers, separated by commas. Empty means every category.</source>
        <translation>Newznab のカテゴリ番号をカンマ区切りで指定します。空欄はすべてのカテゴリを意味します。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Categories:</source>
        <translation>カテゴリ:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Which indexers to ask, by name and separated by commas. Empty means all of them.

Adding one later does not fetch its back catalogue: a new indexer gets its own first check, which adds nothing.</source>
        <translation>問い合わせるインデクサーを名前でカンマ区切りで指定します。空欄はすべてを意味します。

後から追加しても過去の分は取得されません: 新しいインデクサーには独自の初回チェックが行われ、そこでは何も追加されません。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Indexers:</source>
        <translation>インデクサー:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>The RSS address from your indexer&apos;s website. It contains your API key, so it is stored encrypted and is only ever shown back to you with the key hidden.</source>
        <translation>インデクサーのウェブサイトで取得した RSS アドレスです。API キーが含まれるため暗号化して保存され、表示される際は常にキーが伏せられます。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Feed URL:</source>
        <translation>フィード URL:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Only queue releases whose name matches this pattern. Empty accepts everything.</source>
        <translation>名前がこのパターンに一致するリリースだけをキューに追加します。空欄はすべてを受け入れます。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Must match:</source>
        <translation>一致必須:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Never queue a release whose name matches this pattern. It wins over the one above.</source>
        <translation>名前がこのパターンに一致するリリースはキューに追加しません。上の設定より優先されます。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Must not match:</source>
        <translation>除外パターン:</translation>
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
        <translation>下限なし</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Smallest:</source>
        <translation>最小サイズ:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>no maximum</source>
        <translation>上限なし</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Largest:</source>
        <translation>最大サイズ:</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>any age</source>
        <translation>経過日数を問わない</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Posted within:</source>
        <translation>投稿からの期間:</translation>
    </message>
    <message>
        <location line="+4"/>
        <source> minutes</source>
        <translation> 分</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>How often to check. Fifteen minutes is the floor: most indexers ask for no more than that, and checking harder gets an account suspended.</source>
        <translation>チェックする間隔です。下限は 15 分です: ほとんどのインデクサーはそれ以上頻繁なアクセスを認めておらず、それを超えるとアカウントが停止されます。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Check every:</source>
        <translation>チェック間隔:</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+136"/>
        <source>No category</source>
        <translation>カテゴリなし</translation>
    </message>
    <message>
        <location line="-134"/>
        <source>Which download category this feed&apos;s matches go into. The category decides the folder they finish in, and it is resolved when a release completes — so repointing the category moves what is still running with it.</source>
        <translation>このフィードに一致したものを入れるダウンロードカテゴリです。カテゴリは完了時の保存先フォルダを決め、リリースの完了時に解決されます — そのためカテゴリの保存先を変更すると、実行中のものもそれに従って移動します。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Download category:</source>
        <translation>ダウンロードカテゴリ:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Queue what it already lists</source>
        <translation>掲載済みのものもキューに追加</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Normally a feed&apos;s first check only takes note of what is there and queues nothing, because everything an indexer still holds is new to a feed that has never run. Turn this on to take the back catalogue as well — it can be a great deal of it.</source>
        <translation>通常、フィードの初回チェックは現在の内容を記録するだけで、何もキューに追加しません。一度も実行していないフィードにとっては、インデクサーが保持しているものすべてが新着だからです。過去の分も取り込むにはこれをオンにしてください — 非常に大量になることがあります。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Check now</source>
        <translation>今すぐチェック</translation>
    </message>
    <message>
        <location line="+97"/>
        <source>The indexer list could not be saved.</source>
        <translation>インデクサーのリストを保存できませんでした。</translation>
    </message>
    <message>
        <location line="+94"/>
        <source>The feed list could not be saved.</source>
        <translation>フィードのリストを保存できませんでした。</translation>
    </message>
    <message>
        <location line="+37"/>
        <source>%1 min</source>
        <translation>%1 分</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>checking…</source>
        <translation>チェック中…</translation>
    </message>
    <message>
        <location line="+113"/>
        <source>%1 queued on the last check; %2 releases remembered.</source>
        <translation>前回のチェックで %1 件をキューに追加。%2 件のリリースを記憶しています。</translation>
    </message>
    <message>
        <location line="+71"/>
        <source>New feed</source>
        <translation>新しいフィード</translation>
    </message>
    <message>
        <location line="+50"/>
        <source>&quot;%1&quot; could not be checked.</source>
        <translation>「%1」をチェックできませんでした。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Checking &quot;%1&quot;…</source>
        <translation>「%1」をチェック中…</translation>
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
        <translation>両方</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>yes</source>
        <translation>はい</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>no</source>
        <translation>いいえ</translation>
    </message>
    <message>
        <location line="+68"/>
        <source>(a key is stored — leave empty to keep it)</source>
        <translation>(キーは保存済みです — そのままにする場合は空欄のまま)</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>At most %1 indexers can be configured.</source>
        <translation>設定できるインデクサーは最大 %1 件です。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>New indexer</source>
        <translation>新しいインデクサー</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Enter an API URL first.</source>
        <translation>先に API URL を入力してください。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Contacting the indexer…</source>
        <translation>インデクサーに接続中…</translation>
    </message>
    <message>
        <location line="+70"/>
        <source>Warning: Do not change these settings unless you know what you are doing. Otherwise you can easily make things worse for yourself. eMule will run fine without adjusting any of these settings.</source>
        <translation>警告：何をしているか分かっていない限り、これらの設定を変更しないでください。さもないと、問題を悪化させる可能性があります。eMule はこれらの設定を調整しなくても正常に動作します。</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>TCP/IP connections</source>
        <translation>TCP/IP 接続</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Max. new connections / 5 secs.:</source>
        <translation>5秒あたりの最大新規接続数：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Max. half-open connections:</source>
        <translation>最大ハーフオープン接続数：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Server connection refresh interval [min.]:</source>
        <translation>サーバー接続更新間隔 [分]：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Disabled</source>
        <translation>無効</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Autotake eD2K links only during runtime</source>
        <translation>実行中のみ eD2K リンクを自動取得</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Use credit system (reward uploaders)</source>
        <translation>クレジットシステムを使用（アップロード者を報酬）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Remember the upload queue between restarts</source>
        <translation>再起動後もアップロードキューを保持する</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Stores the longest-waiting clients in your upload queue and puts them back, with the places they had earned, when eMule starts again. They are not contacted on startup — they simply wait their turn as usual.</source>
        <translation>アップロードキューで最も長く待っているクライアントを保存し、次に eMule を起動したときに獲得済みの順位のまま戻します。起動時に接続することはなく、通常どおり順番を待つだけです。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Open/close ports on WinXP firewall when starting/exiting eMule</source>
        <translation>eMule の起動/終了時に WinXP ファイアウォールのポートを開閉</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Filter server and client LAN IPs</source>
        <translation>サーバーとクライアントの LAN IP をフィルター</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Show more controls (advanced mode controls)</source>
        <translation>詳細コントロールを表示（詳細モード）</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Disable A4AF checks to save CPU</source>
        <translation>CPU 節約のため A4AF チェックを無効化</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Disable automatic archive preview start in file details</source>
        <translation>ファイル詳細でのアーカイブ自動プレビューを無効化</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Host name for own eD2K links:</source>
        <translation>自身の eD2K リンクのホスト名：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>A DNS name or an IPv6 literal</source>
        <translation>DNS 名または IPv6 リテラル</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Add own IPv6 address to eD2K links</source>
        <translation>自分の IPv6 アドレスを eD2K リンクに追加する</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Only when a public IPv6 address is confirmed. Legacy clients ignore it.</source>
        <translation>パブリック IPv6 アドレスが確認された場合のみ。旧来のクライアントは無視します。</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Create new part files as &apos;sparse&apos; (NTFS only)</source>
        <translation>新しい part ファイルを「スパース」として作成（NTFS のみ）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Allocate full file size for non-sparse part files</source>
        <translation>非スパース part ファイルにフルサイズを割り当て</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Check disk space</source>
        <translation>ディスク空き容量を確認</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Min. free disk space [MB]:</source>
        <translation>最小空きディスク容量 [MB]：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Max. disk load while hashing:</source>
        <translation>ハッシュ計算中の最大ディスク負荷:</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Hashing pauses between reads so the disk stays responsive. 100 % means no limit.</source>
        <translation>ディスクの応答性を保つため、ハッシュ計算は読み取りの合間に一時停止します。100 % は無制限を意味します。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Safe .met/.dat file writing</source>
        <translation>安全な .met/.dat ファイル書き込み</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+15"/>
        <source>Never</source>
        <translation>なし</translation>
    </message>
    <message>
        <location line="-14"/>
        <source>On shutdown</source>
        <translation>シャットダウン時</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Always</source>
        <translation>常に</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Extract meta data</source>
        <translation>メタデータを抽出</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>MediaInfo Library</source>
        <translation>MediaInfo ライブラリ</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Resolve shell links in shared directories</source>
        <translation>共有ディレクトリのシェルリンクを解決</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Verbose (additional program feedback)</source>
        <translation>詳細（追加のプログラムフィードバック）</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Log level:</source>
        <translation>ログレベル：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Log client source exchange and server source queries/answers</source>
        <translation>クライアントソース交換とサーバーソース照会/応答を記録</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Log banned clients</source>
        <translation>BAN されたクライアントを記録</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Log received file descriptions and ratings</source>
        <translation>受信したファイル説明と評価を記録</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Log secure ident</source>
        <translation>安全な識別を記録</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Log filtered and/or ignored IPs</source>
        <translation>フィルター/無視された IP を記録</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Log file save actions</source>
        <translation>ファイル保存アクションを記録</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Log A4AF actions</source>
        <translation>A4AF アクションを記録</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Log upload/download events</source>
        <translation>アップロード/ダウンロードイベントを記録</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Log raw socket packets</source>
        <translation>生のソケットパケットを記録</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Show Usenet log tab</source>
        <translation>Usenet ログタブを表示</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Upload SpeedSense (not recommended)</source>
        <translation>アップロード速度センス（非推奨）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Find best upload limit automatically</source>
        <translation>最適なアップロード制限を自動検出</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Ping tolerance (% of lowest ping):</source>
        <translation>Ping 許容値（最低 ping の %）：</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Ping tolerance (ms):</source>
        <translation>Ping 許容値（ミリ秒）：</translation>
    </message>
    <message>
        <location line="+3"/>
        <source> ms</source>
        <translation> ミリ秒</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Method for ping tolerance:</source>
        <translation>Ping 許容値の方法：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Percent (%)</source>
        <translation>パーセント (%)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Milliseconds (ms)</source>
        <translation>ミリ秒 (ms)</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Going up slowness:</source>
        <translation>上昇の遅さ：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Going down slowness:</source>
        <translation>下降の遅さ：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Max number of pings for average:</source>
        <translation>平均のための最大 ping 回数：</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>UPnP</source>
        <translation>UPnP</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Remove UPnP port forwarding on exit</source>
        <translation>終了時に UPnP ポートフォワーディングを削除</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Sharing eMule with other computer users</source>
        <translation>他のコンピューターユーザーと eMule を共有</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Each user has its own configuration and downloads</source>
        <translation>各ユーザーが独自の設定とダウンロードを持つ</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Everyone has the same configuration and downloads</source>
        <translation>全員が同じ設定とダウンロードを共有</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Store config and downloads in the program directory</source>
        <translation>設定とダウンロードをプログラムディレクトリに保存</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>File buffer size: %1 MB</source>
        <translation>ファイルバッファサイズ：%1 MB</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Queue size: %1</source>
        <translation>キューサイズ：%1</translation>
    </message>
    <message>
        <location line="+599"/>
        <source>Proxy settings will only apply to new connections.
Restart eMule for all connections to use the new proxy settings.

News server connections switch over immediately.</source>
        <translation>プロキシ設定は新しい接続にのみ適用されます。
すべての接続で新しいプロキシ設定を使用するには eMule を再起動してください。

ニュースサーバーの接続はすぐに切り替わります。</translation>
    </message>
    <message>
        <location line="+422"/>
        <source>Web Interface</source>
        <translation>Web インターフェース</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&quot;%1&quot; is not an IP address. The listen address was not changed.</source>
        <translation>「%1」は IP アドレスではありません。待ち受けアドレスは変更されませんでした。</translation>
    </message>
    <message>
        <location line="+495"/>
        <source>File types</source>
        <translation>ファイルの種類</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Could not update the .nzb file association: %1</source>
        <translation>.nzb ファイルの関連付けを更新できませんでした: %1</translation>
    </message>
    <message>
        <location line="+593"/>
        <source>Port forwarding: %1</source>
        <translation>ポート転送: %1</translation>
    </message>
    <message>
        <location line="+67"/>
        <source>Database: not connected to the core</source>
        <translation>データベース: コアに未接続</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Downloading the GeoLite2 country database...</source>
        <translation>GeoLite2 国データベースをダウンロード中...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Update failed: the core did not answer</source>
        <translation>更新に失敗しました: コアが応答しません</translation>
    </message>
    <message>
        <location line="+53"/>
        <source>Database: GeoLite2-Country from %1</source>
        <translation>データベース: GeoLite2-Country (%1)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Database: none — enter your MaxMind account ID and license key</source>
        <translation>データベース: なし — MaxMind のアカウント ID とライセンスキーを入力してください</translation>
    </message>
    <message>
        <location line="+3"/>
        <source> (update running)</source>
        <translation> (更新中)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Last update failed: %1</source>
        <translation>前回の更新に失敗しました: %1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Filename Cleanup</source>
        <translation>ファイル名のクリーンアップ</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Text to remove from the names of new downloads. Case is ignored.</source>
        <translation>新しいダウンロードの名前から削除するテキストです。大文字と小文字は区別されません。</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Reset to Default</source>
        <translation>既定値に戻す</translation>
    </message>
    <message>
        <location line="-4781"/>
        <location line="+1019"/>
        <location line="+154"/>
        <location line="+1340"/>
        <location line="+281"/>
        <location line="+1985"/>
        <source>Remove</source>
        <translation>削除</translation>
    </message>
    <message>
        <location line="-5892"/>
        <source>New eMule Qt version detected</source>
        <translation>新しい eMule Qt バージョンを検出しました</translation>
    </message>
    <message>
        <location line="+372"/>
        <source>Update from URL: (filter.dat- or PeerGuardian-format, .gz/.zip accepted)</source>
        <translation>URL から更新: （filter.dat 形式または PeerGuardian 形式、.gz/.zip 可）</translation>
    </message>
    <message>
        <location line="+2964"/>
        <source>Write eMule core logs to disk</source>
        <translation>eMule コアのログをディスクに書き込む</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Write eMule GUI logs to disk</source>
        <translation>eMule GUI のログをディスクに書き込む</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Log server connection &amp;&amp; search details (TCP/UDP handshake)</source>
        <translation>サーバー接続 &amp;&amp; 検索の詳細をログに記録する（TCP/UDP ハンドシェイク）</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Log web server requests</source>
        <translation>Web サーバーのリクエストをログに記録する</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Log public IP address on startup</source>
        <translation>起動時にパブリック IP アドレスをログに記録する</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Enable IPC log tab</source>
        <translation>IPC ログタブを有効にする</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Start core with console (debug)</source>
        <translation>コンソール付きでコアを起動する（デバッグ）</translation>
    </message>
    <message>
        <location line="+89"/>
        <source>PCP (RFC 6887) — preferred, supports IPv6</source>
        <translation>PCP (RFC 6887) — 推奨、IPv6 対応</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>NAT-PMP (RFC 6886) — IPv4 only</source>
        <translation>NAT-PMP (RFC 6886) — IPv4 のみ</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>UPnP IGD — fallback</source>
        <translation>UPnP IGD — フォールバック</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Open IPv6 firewall pinholes</source>
        <translation>IPv6 ファイアウォールのピンホールを開く</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Requested lease:</source>
        <translation>要求するリース時間:</translation>
    </message>
    <message>
        <location line="-2221"/>
        <location line="+913"/>
        <location line="+1312"/>
        <source> s</source>
        <translation> 秒</translation>
    </message>
    <message>
        <location line="-2267"/>
        <source>Decimal GB, because that is what an invoice says — the 1024-based GB used elsewhere in eMule would put a 1000 GB plan 7% over.

Set it slightly under your plan. The figure is measured here, so it reads a few percent below your provider&apos;s, and articles already in flight when the limit is reached still finish.</source>
        <translation>請求書の表記に合わせた10進数のGBです。eMuleの他の箇所で使われている1024ベースのGBでは、1000 GBのプランを7%超過してしまいます。

プランより少し小さめに設定してください。この値はここで計測されるため、プロバイダーの数値より数パーセント低くなります。また、上限に達した時点で転送中の記事はそのまま完了します。</translation>
    </message>
    <message>
        <location line="+2382"/>
        <source>New</source>
        <translation>新規</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+19"/>
        <source>Title</source>
        <translation>タイトル</translation>
    </message>
    <message>
        <location line="-19"/>
        <source>Days</source>
        <translation>日</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Start Time</source>
        <translation>開始時間</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Details</source>
        <translation>詳細</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Time</source>
        <translation>時間</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+112"/>
        <source>Daily</source>
        <translation>毎日</translation>
    </message>
    <message>
        <location line="-112"/>
        <source>Monday</source>
        <translation>月曜日</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Tuesday</source>
        <translation>火曜日</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Wednesday</source>
        <translation>水曜日</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Thursday</source>
        <translation>木曜日</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Friday</source>
        <translation>金曜日</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Saturday</source>
        <translation>土曜日</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Sunday</source>
        <translation>日曜日</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Mon-Fri</source>
        <translation>月〜金</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Mon-Sat</source>
        <translation>月〜土</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Sat-Sun</source>
        <translation>土〜日</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>No end time</source>
        <translation>終了時間なし</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+4"/>
        <source>Action</source>
        <translation>アクション</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Value</source>
        <translation>値</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>New Schedule</source>
        <translation>新しいスケジュール</translation>
    </message>
    <message>
        <location line="-2606"/>
        <location line="+1019"/>
        <location line="+154"/>
        <location line="+1594"/>
        <location line="+2007"/>
        <source>Add</source>
        <translation>追加</translation>
    </message>
    <message>
        <location line="-1999"/>
        <source>Action Value</source>
        <translation>アクション値</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+15"/>
        <source>Enter value:</source>
        <translation>値を入力：</translation>
    </message>
    <message>
        <location line="-3"/>
        <location line="+2"/>
        <source>Edit Value</source>
        <translation>値を編集</translation>
    </message>
    <message>
        <location line="+94"/>
        <source>The %1 settings page is not yet implemented.</source>
        <translation>設定ページ %1 はまだ実装されていません。</translation>
    </message>
    <message>
        <location line="+185"/>
        <source>Proxy</source>
        <translation>プロキシ</translation>
    </message>
    <message>
        <source>Proxy settings will only apply to new connections.
Restart eMule for all connections to use the new proxy settings.</source>
        <translation type="vanished">プロキシ設定は新しい接続にのみ適用されます。
すべての接続で新しいプロキシ設定を使用するには eMule を再起動してください。</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>The language change will take effect after restarting the application.</source>
        <translation>言語の変更はアプリケーションの再起動後に有効になります。</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Core connection settings will take effect after restarting the application.</source>
        <translation>コア接続設定はアプリケーションの再起動後に有効になります。</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Icons</source>
        <translation>アイコン</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The icon change will take effect after restarting the application.</source>
        <translation>アイコンの変更はアプリケーションの再起動後に有効になります。</translation>
    </message>
</context>
<context>
    <name>eMule::PasteLinksDialog</name>
    <message>
        <location filename="../src/gui/dialogs/PasteLinksDialog.cpp" line="+14"/>
        <source>Paste eD2K Links</source>
        <translation>eD2K リンクを貼り付け</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>eD2K Links:</source>
        <translation>eD2K リンク：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paste one or more ed2k:// links here, one per line...</source>
        <translation>ここに ed2k:// リンクを1行に1つ貼り付けてください...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Download</source>
        <translation>ダウンロード</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation type="vanished">キャンセル</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Not Connected</source>
        <translation>未接続</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not connected to the daemon.</source>
        <translation>デーモンに接続されていません。</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Invalid Links</source>
        <translation>無効なリンク</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The following links could not be parsed:

%1</source>
        <translation>以下のリンクを解析できませんでした：

%1</translation>
    </message>
</context>
<context>
    <name>eMule::PasteTextDialog</name>
    <message>
        <location filename="../src/gui/dialogs/PasteTextDialog.cpp" line="+42"/>
        <source>optional</source>
        <translation>任意</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Category:</source>
        <translation>カテゴリ:</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Priority:</source>
        <translation>優先度:</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Start paused</source>
        <translation>一時停止状態で開始</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Cancel</source>
        <translation>キャンセル</translation>
    </message>
    <message>
        <location line="+78"/>
        <source>Working…</source>
        <translation>処理中…</translation>
    </message>
</context>
<context>
    <name>eMule::PeerPreviewDialog</name>
    <message>
        <location filename="../src/gui/dialogs/PeerPreviewDialog.cpp" line="+17"/>
        <source>Preview: %1</source>
        <translation>プレビュー: %1</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Prior</source>
        <translation>前へ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Next</source>
        <translation>次へ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Close</source>
        <translation>閉じる</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>No images</source>
        <translation>画像がありません</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Image %1 of %2</source>
        <translation>画像 %1 / %2</translation>
    </message>
</context>
<context>
    <name>eMule::SearchDetailDialog</name>
    <message>
        <location filename="../src/gui/dialogs/SearchDetailDialog.cpp" line="+42"/>
        <source>Details: %1</source>
        <translation>詳細: %1</translation>
    </message>
    <message>
        <location line="+19"/>
        <location line="+2"/>
        <source>Metadata</source>
        <translation>メタデータ</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+2"/>
        <source>Comments</source>
        <translation>コメント</translation>
    </message>
</context>
<context>
    <name>eMule::SearchPanel</name>
    <message>
        <location filename="../src/gui/panels/SearchPanel.cpp" line="+302"/>
        <location line="+942"/>
        <location line="+13"/>
        <location line="+415"/>
        <source>Download</source>
        <translation>ダウンロード</translation>
    </message>
    <message>
        <location line="-1350"/>
        <source>Close All Searches</source>
        <translation>すべての検索を閉じる</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Name:</source>
        <translation>名前：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Enter search keywords...</source>
        <translation>検索キーワードを入力...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Type:</source>
        <translation>種類：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Any</source>
        <translation>すべて</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Audio</source>
        <translation>オーディオ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Video</source>
        <translation>ビデオ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Image</source>
        <translation>画像</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Document</source>
        <translation>ドキュメント</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Program</source>
        <translation>プログラム</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Archive</source>
        <translation>アーカイブ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>CD-Image</source>
        <translation>CD イメージ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Collection</source>
        <translation>コレクション</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Method:</source>
        <translation>方法：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Automatic</source>
        <translation>自動</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Kad Network</source>
        <translation>Kad ネットワーク</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ed2k Server</source>
        <translation>Ed2k サーバー</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ed2k Global</source>
        <translation>Ed2k グローバル</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Usenet (Indexer)</source>
        <translation>Usenet (インデクサー)</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Usenet (Server)</source>
        <translation>Usenet (サーバー)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Torrent (Server)</source>
        <translation>Torrent (サーバー)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Reset</source>
        <translation>リセット</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Min. Size [MB]:</source>
        <translation>最小サイズ [MB]：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Max. Size [MB]:</source>
        <translation>最大サイズ [MB]：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Availability:</source>
        <translation>可用性：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Complete Sources:</source>
        <translation>完全なソース：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Extension:</source>
        <translation>拡張子：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Codec:</source>
        <translation>コーデック：</translation>
    </message>
    <message>
        <source>Min. Bitrate [kbps]:</source>
        <translation type="vanished">最小ビットレート [kbps]：</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Min. Length [s]:</source>
        <translation>最小長さ [秒]：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Title:</source>
        <translation>タイトル：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Album:</source>
        <translation>アルバム：</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Artist:</source>
        <translation>アーティスト：</translation>
    </message>
    <message>
        <location line="-78"/>
        <source>Show results the server found on this network</source>
        <translation>サーバーがこのネットワークで見つけた結果を表示</translation>
    </message>
    <message>
        <location line="-202"/>
        <source>Related</source>
        <translation>関連</translation>
    </message>
    <message>
        <location line="+208"/>
        <source>Usenet results</source>
        <translation>Usenet の結果</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Kad results</source>
        <translation>Kad の結果</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Torrent results</source>
        <translation>トレントの結果</translation>
    </message>
    <message>
        <location line="+46"/>
        <source>Min. Bitrate [Kbit/s]:</source>
        <translation>最小ビットレート [Kbit/s]:</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Start</source>
        <translation>開始</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>More</source>
        <translation>さらに</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Ask the server for further results of this search</source>
        <translation>この検索の追加の結果をサーバーに要求</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Cancel</source>
        <translation>キャンセル</translation>
    </message>
    <message>
        <location line="+34"/>
        <location line="+24"/>
        <location line="+194"/>
        <source>Not connected to daemon — search cannot be started.</source>
        <translation>デーモンに接続していません — 検索を開始できません。</translation>
    </message>
    <message>
        <location line="-161"/>
        <source>Search</source>
        <translation>検索</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Kad: &quot;%1&quot; is already being searched — using &quot;%2&quot; as the search target.</source>
        <translation>Kad: 「%1」はすでに検索中です — 検索対象として「%2」を使用します。</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Queued — waiting for a server connection</source>
        <translation>待機中 — サーバー接続を待っています</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Queued — waiting for Kad to connect</source>
        <translation>待機中 — Kad の接続を待っています</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Queued — waiting for a server or Kad connection</source>
        <translation>待機中 — サーバーまたは Kad の接続を待っています</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Queued — waiting for the previous search to finish</source>
        <translation>待機中 — 前の検索の終了を待っています</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Queued — waiting for the server to say what it offers</source>
        <translation>待機中 — サーバーが提供内容を通知するのを待っています</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Queued</source>
        <translation>キュー待ち</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Search failed: %1</source>
        <translation>検索に失敗しました: %1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>%1 results — scroll down for more</source>
        <translation>%1 件の結果 — 下にスクロールするとさらに表示</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>%1 of %2 results</source>
        <translation>%2 件中 %1 件の結果</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Search &quot;%1&quot; failed: %2</source>
        <translation>検索「%1」に失敗しました: %2</translation>
    </message>
    <message>
        <location line="+75"/>
        <location line="+182"/>
        <source>Usenet search</source>
        <translation>Usenet 検索</translation>
    </message>
    <message>
        <location line="-90"/>
        <source>%1 results — %2 of %3 indexers</source>
        <translation>%1 件の結果 — %3 件中 %2 件のインデクサー</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Usenet search: %1</source>
        <translation>Usenet 検索: %1</translation>
    </message>
    <message>
        <location line="-217"/>
        <location line="+221"/>
        <source>No results</source>
        <translation>結果なし</translation>
    </message>
    <message>
        <location line="+69"/>
        <source>Could not queue &quot;%1&quot;: %2</source>
        <translation>「%1」をキューに追加できませんでした: %2</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Queued &quot;%1&quot; for download from Usenet.</source>
        <translation>「%1」を Usenet からのダウンロードとしてキューに追加しました。</translation>
    </message>
    <message>
        <location line="+95"/>
        <source>Restore Search Parameters</source>
        <translation>検索パラメータを復元</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Close</source>
        <translation>閉じる</translation>
    </message>
    <message>
        <location line="+92"/>
        <source>&amp;Download</source>
        <translation>ダウンロード(&amp;D)</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Download &amp;To</source>
        <translation>ダウンロード先(&amp;T)</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Copy &amp;Name</source>
        <translation>名前をコピー(&amp;N)</translation>
    </message>
    <message>
        <location line="+75"/>
        <source>%1 (%2)</source>
        <translation>%1 (%2)</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Paused</source>
        <translation>一時停止</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Download NZB File...</source>
        <translation>NZB ファイルをダウンロード...</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Download Torrent</source>
        <translation>Torrent をダウンロード</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Details...</source>
        <translation>詳細...</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Comments...</source>
        <translation>コメント...</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Copy eD2K Links</source>
        <translation>eD2K リンクをコピー</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Copy eD2K Links (HTML)</source>
        <translation>eD2K リンクをコピー (HTML)</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Copy Magnet Links</source>
        <translation>マグネットリンクをコピー</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Mark as not Spam</source>
        <translation>スパムではないとマーク</translation>
    </message>
    <message>
        <location line="+50"/>
        <location line="+7"/>
        <location line="+906"/>
        <location line="+43"/>
        <source>Preview</source>
        <translation>プレビュー</translation>
    </message>
    <message>
        <location line="-678"/>
        <source>You have already downloaded the following file(s). Download them again?

%1</source>
        <translation>次のファイルはすでにダウンロード済みです。もう一度ダウンロードしますか？

%1</translation>
    </message>
    <message>
        <location line="+636"/>
        <source>Preview requested - please wait</source>
        <translation>プレビューを要求しました - お待ちください</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>%1 sent no preview</source>
        <translation>%1 からプレビューは送られませんでした</translation>
    </message>
    <message>
        <location line="+319"/>
        <source>Asking servers: %1 / %2</source>
        <translation>サーバーに問い合わせ中：%1 / %2</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>All</source>
        <translation>すべて</translation>
    </message>
    <message>
        <location line="-1338"/>
        <location line="+14"/>
        <source>Mark as Spam</source>
        <translation>スパムとしてマーク</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Remove</source>
        <translation>削除</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Close Search Results</source>
        <translation>検索結果を閉じる</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Close All Search Results</source>
        <translation>すべての検索結果を閉じる</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Find...</source>
        <translation>検索...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Search Related Files</source>
        <translation>関連ファイルを検索</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Web Services</source>
        <translation>Web サービス</translation>
    </message>
    <message>
        <location line="+912"/>
        <source>Preview not available — web server is not running or stream token not received.</source>
        <translation>プレビューは利用できません — Web サーバーが実行されていないか、ストリームトークンを受信していません。</translation>
    </message>
</context>
<context>
    <name>eMule::SearchResultsModel</name>
    <message>
        <location filename="../src/gui/controls/SearchResultsModel.cpp" line="+84"/>
        <source>Yes</source>
        <translation>はい</translation>
    </message>
    <message numerus="yes">
        <location line="+308"/>
        <source>%1 · %n name(s)</source>
        <translation>
            <numerusform>%1 · 名前 %n 件</numerusform>
        </translation>
    </message>
    <message>
        <location line="+9"/>
        <source>File Name</source>
        <translation>ファイル名</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Size</source>
        <translation>サイズ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Availability</source>
        <translation>可用性</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Complete Sources</source>
        <translation>完全なソース</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type</source>
        <translation>種類</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Artist</source>
        <translation>アーティスト</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Album</source>
        <translation>アルバム</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Title</source>
        <translation>タイトル</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Length</source>
        <translation>長さ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bitrate</source>
        <translation>ビットレート</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Codec</source>
        <translation>コーデック</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Known</source>
        <translation>既知</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Seen</source>
        <translation>既出</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Confidence</source>
        <translation>信頼度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>File ID</source>
        <translation>ファイル ID</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Folder</source>
        <translation>フォルダ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>AICH Hash</source>
        <translation>AICH ハッシュ</translation>
    </message>
</context>
<context>
    <name>eMule::ServerListModel</name>
    <message>
        <location filename="../src/gui/controls/ServerListModel.cpp" line="+75"/>
        <source>%1 (disabled)</source>
        <translation>%1 (無効)</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+3"/>
        <source>Yes</source>
        <translation>はい</translation>
    </message>
    <message>
        <location line="-3"/>
        <location line="+3"/>
        <source>No</source>
        <translation>いいえ</translation>
    </message>
    <message>
        <location line="+82"/>
        <source>Server Name</source>
        <translation>サーバー名</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>IP</source>
        <translation>IP</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Description</source>
        <translation>説明</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ping</source>
        <translation>Ping</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Users</source>
        <translation>ユーザー</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Max Users</source>
        <translation>最大ユーザー</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Files</source>
        <translation>ファイル</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Preference</source>
        <translation>優先設定</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Failed</source>
        <translation>失敗</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Static</source>
        <translation>静的</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Soft File Limit</source>
        <translation>ソフトファイル制限</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Country</source>
        <translation>国</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>IPv6</source>
        <translation>IPv6</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>High</source>
        <translation>高い</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Low</source>
        <translation>低い</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Normal</source>
        <translation>通常</translation>
    </message>
    <message>
        <source>Soft Files</source>
        <translation type="vanished">ソフトファイル</translation>
    </message>
    <message>
        <location line="-38"/>
        <source>Low ID</source>
        <translation>Low ID</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Obfuscation</source>
        <translation>難読化</translation>
    </message>
</context>
<context>
    <name>eMule::ServerPanel</name>
    <message>
        <location filename="../src/gui/panels/ServerPanel.cpp" line="+246"/>
        <source>Disconnect</source>
        <translation>切断</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+24"/>
        <location line="+53"/>
        <source>Cancel</source>
        <translation>キャンセル</translation>
    </message>
    <message>
        <location line="-75"/>
        <location line="+443"/>
        <source>Connect</source>
        <translation>接続</translation>
    </message>
    <message>
        <location line="-499"/>
        <source>Invalid URL: %1</source>
        <translation>無効なURL: %1</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Downloading server.met from %1 ...</source>
        <translation>%1からserver.metをダウンロード中...</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Failed to download server.met: %1</source>
        <translation>server.metのダウンロードに失敗しました: %1</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Downloaded empty server.met file.</source>
        <translation>ダウンロードしたserver.metファイルが空です。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Downloaded server.met (%1 bytes). Parsing...</source>
        <translation>server.metをダウンロードしました (%1バイト)。解析中...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloaded server.met, unpacked &quot;%1&quot; (%2 bytes). Parsing...</source>
        <translation>server.met をダウンロードし、&quot;%1&quot; を展開しました (%2 バイト)。解析中...</translation>
    </message>
    <message>
        <location line="+605"/>
        <location line="+2"/>
        <location line="+24"/>
        <location line="+39"/>
        <source>IP:Port:</source>
        <translation>IP:ポート:</translation>
    </message>
    <message>
        <location line="-65"/>
        <source>Unknown</source>
        <translation>不明</translation>
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
        <translation>eD2Kサーバー</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Description:</source>
        <translation>説明:</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Version:</source>
        <translation>バージョン:</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+48"/>
        <source>Users:</source>
        <translation>ユーザー:</translation>
    </message>
    <message>
        <location line="-47"/>
        <location line="+49"/>
        <source>Files:</source>
        <translation>ファイル:</translation>
    </message>
    <message>
        <location line="-48"/>
        <source>Connection:</source>
        <translation>接続:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Obfuscated</source>
        <translation>難読化済み</translation>
    </message>
    <message>
        <location line="+18"/>
        <location line="+7"/>
        <source>Open</source>
        <translation>オープン</translation>
    </message>
    <message>
        <location line="-3"/>
        <location line="+6"/>
        <source>UDP Status:</source>
        <translation>UDP ステータス:</translation>
    </message>
    <message>
        <location line="-1"/>
        <source>unverified</source>
        <translation>未検証</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Extern UDP Port:</source>
        <translation>外部 UDP ポート:</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Web Interface</source>
        <translation>Web インターフェース</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Enabled</source>
        <translation>有効</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Disabled</source>
        <translation>無効</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>▸ Servers (%1)</source>
        <translation>▸ サーバー (%1)</translation>
    </message>
    <message>
        <location line="-632"/>
        <source>Connect To</source>
        <translation>接続先</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Priority</source>
        <translation>優先度</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Low</source>
        <translation>低い</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+521"/>
        <source>Normal</source>
        <translation>通常</translation>
    </message>
    <message>
        <location line="-520"/>
        <source>High</source>
        <translation>高い</translation>
    </message>
    <message>
        <location line="+93"/>
        <source>Move Up</source>
        <translation>上へ移動</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Move Down</source>
        <translation>下へ移動</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Add To Static List</source>
        <translation>静的リストに追加</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Remove From Static List</source>
        <translation>静的リストから削除</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Enable</source>
        <translation>有効にする</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Copy eD2K Links</source>
        <translation>eD2K リンクをコピー</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Paste eD2K Links</source>
        <translation>eD2K リンクを貼り付け</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Remove</source>
        <translation>削除</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Remove All</source>
        <translation>すべて削除</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>eNode Account...</source>
        <translation>eNode アカウント...</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Find...</source>
        <translation>検索...</translation>
    </message>
    <message>
        <location line="+52"/>
        <source>▸ Servers (0)</source>
        <translation>▸ サーバー (0)</translation>
    </message>
    <message>
        <location line="+78"/>
        <source>New Server</source>
        <translation>新しいサーバー</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>IP Address:</source>
        <translation>IP アドレス：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Port:</source>
        <translation>ポート：</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+130"/>
        <source>Name:</source>
        <translation>名前：</translation>
    </message>
    <message>
        <location line="-124"/>
        <source>Add to list</source>
        <translation>リストに追加</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Update server.met from URL</source>
        <translation>URL から server.met を更新</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Update server.met from URL:</source>
        <translation>URL から server.met を更新：</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Update</source>
        <translation>更新</translation>
    </message>
    <message>
        <location line="+48"/>
        <source>My Info</source>
        <translation>マイ情報</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>eD2K Network</source>
        <translation>eD2K ネットワーク</translation>
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
        <translation>ステータス：</translation>
    </message>
    <message>
        <location line="-95"/>
        <source>Connected</source>
        <translation>接続済み</translation>
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
        <translation>接続中...</translation>
    </message>
    <message>
        <location line="-45"/>
        <location line="+48"/>
        <source>Disconnected</source>
        <translation>未接続</translation>
    </message>
    <message>
        <location line="-44"/>
        <source>Kad Network</source>
        <translation>Kad ネットワーク</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+8"/>
        <source>Firewalled</source>
        <translation>ファイアウォール内</translation>
    </message>
    <message numerus="yes">
        <location line="+578"/>
        <source>%n server(s) not added — LAN addresses are filtered</source>
        <translation>
            <numerusform>%n 件のサーバーを追加しませんでした — LAN アドレスはフィルタされています</numerusform>
        </translation>
    </message>
    <message>
        <location line="-624"/>
        <source>Low ID</source>
        <translation>Low ID</translation>
    </message>
    <message>
        <location line="+301"/>
        <source>Invalid server.met header: 0x%1</source>
        <translation>無効なserver.metヘッダー: 0x%1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Server count too large: %1</source>
        <translation>サーバー数が多すぎます: %1</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Corrupt server.met: tag count %1 at server %2</source>
        <translation>破損したserver.met: サーバー%2のタグ数%1</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Corrupt server.met: truncated tag name</source>
        <translation>破損したserver.met: タグ名が切り詰められています</translation>
    </message>
    <message>
        <location line="+52"/>
        <source>Corrupt server.met: truncated hash tag</source>
        <translation>server.met が破損しています: ハッシュタグが切り詰められています</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>Unknown tag type 0x%1 at server %2, stopping parse</source>
        <translation>サーバー%2で不明なタグタイプ0x%1、解析を中止</translation>
    </message>
    <message>
        <location line="+49"/>
        <source>server.met processed: %1 servers added, %2 skipped (duplicates/invalid).</source>
        <translation>server.met処理完了: %1サーバー追加、%2スキップ（重複/無効）。</translation>
    </message>
</context>
<context>
    <name>eMule::SharedFilesModel</name>
    <message>
        <location filename="../src/gui/controls/SharedFilesModel.cpp" line="+41"/>
        <source>Yes</source>
        <translation>はい</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>No</source>
        <translation>いいえ</translation>
    </message>
    <message>
        <location line="+98"/>
        <source>%1 Kbit/s</source>
        <translation>%1 Kbit/s</translation>
    </message>
    <message>
        <location line="+141"/>
        <source>File Name</source>
        <translation>ファイル名</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Size</source>
        <translation>サイズ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type</source>
        <translation>種類</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority</source>
        <translation>優先度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Requests</source>
        <translation>リクエスト</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Transferred Data</source>
        <translation>転送データ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Shared parts</source>
        <translation>共有パート</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Complete Sources</source>
        <translation>完全なソース</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Shared eD2K|Kad</source>
        <translation>共有 eD2K|Kad</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>File ID</source>
        <translation>ファイル ID</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Accepted Requests</source>
        <translation>承認されたリクエスト</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Artist</source>
        <translation>アーティスト</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Album</source>
        <translation>アルバム</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Title</source>
        <translation>タイトル</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Length</source>
        <translation>長さ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bitrate</source>
        <translation>ビットレート</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Codec</source>
        <translation>コーデック</translation>
    </message>
    <message>
        <source>Shared eD2K/Kad</source>
        <translation type="vanished">共有 eD2K/Kad</translation>
    </message>
    <message>
        <location line="-8"/>
        <source>Folder</source>
        <translation>フォルダ</translation>
    </message>
</context>
<context>
    <name>eMule::SharedFilesPanel</name>
    <message>
        <location filename="../src/gui/panels/SharedFilesPanel.cpp" line="+173"/>
        <location line="+14"/>
        <location line="+372"/>
        <location line="+587"/>
        <source>Shared Files (0)</source>
        <translation>共有ファイル (0)</translation>
    </message>
    <message>
        <location line="-868"/>
        <source>Open File</source>
        <translation>ファイルを開く</translation>
    </message>
    <message>
        <location line="+13"/>
        <location line="+1392"/>
        <location line="+506"/>
        <source>Open Folder</source>
        <translation>フォルダを開く</translation>
    </message>
    <message>
        <location line="-1886"/>
        <source>Rename...</source>
        <translation>名前変更...</translation>
    </message>
    <message>
        <location line="+1945"/>
        <source>Rename File</source>
        <translation>ファイル名を変更</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>New file name:</source>
        <translation>新しいファイル名:</translation>
    </message>
    <message>
        <location line="-1937"/>
        <source>Delete From Disk</source>
        <translation>ディスクから削除</translation>
    </message>
    <message>
        <location line="+931"/>
        <source>Delete File</source>
        <translation>ファイルを削除</translation>
    </message>
    <message>
        <location line="-6"/>
        <source>Are you sure you want to permanently delete &quot;%1&quot; from disk?</source>
        <translation>&quot;%1&quot;をディスクから完全に削除しますか？</translation>
    </message>
    <message>
        <location line="-910"/>
        <source>Unshare</source>
        <translation>共有解除</translation>
    </message>
    <message>
        <location line="+943"/>
        <source>Unshare File</source>
        <translation>ファイルの共有を解除</translation>
    </message>
    <message>
        <location line="-6"/>
        <source>Remove &quot;%1&quot; from the shared files list?

The file will remain on disk.</source>
        <translation>共有ファイルリストから&quot;%1&quot;を削除しますか？

ファイルはディスクに残ります。</translation>
    </message>
    <message>
        <location line="-926"/>
        <source>Priority (Upload)</source>
        <translation>優先度（アップロード）</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Very Low</source>
        <translation>非常に低い</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Low</source>
        <translation>低い</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Normal</source>
        <translation>通常</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>High</source>
        <translation>高い</translation>
    </message>
    <message>
        <source>Very High</source>
        <translation type="vanished">非常に高い</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Auto</source>
        <translation>自動</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Collection</source>
        <translation>コレクション</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Create Collection...</source>
        <translation>コレクションを作成...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Modify Collection...</source>
        <translation>コレクションを編集...</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>View Collection...</source>
        <translation>コレクションを表示...</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Search Author&apos;s Collections...</source>
        <translation>作成者のコレクションを検索...</translation>
    </message>
    <message>
        <location line="+9"/>
        <location line="+6"/>
        <source>Search Author&apos;s Collections</source>
        <translation>作成者のコレクションを検索</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>This collection carries no author key, so its author&apos;s other collections cannot be looked up.</source>
        <translation>このコレクションには作成者キーが含まれていないため、同じ作成者の他のコレクションを検索できません。</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Details...</source>
        <translation>詳細...</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Comments...</source>
        <translation>コメント...</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>eD2K Links...</source>
        <translation>eD2K リンク...</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Find...</source>
        <translation>検索...</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Web Services</source>
        <translation>Web サービス</translation>
    </message>
    <message>
        <location line="+72"/>
        <source>Reload</source>
        <translation>再読み込み</translation>
    </message>
    <message>
        <source>File Name</source>
        <translation type="vanished">ファイル名</translation>
    </message>
    <message>
        <location line="+34"/>
        <source>All Shared Files</source>
        <translation>すべての共有ファイル</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Incoming Files</source>
        <translation>受信ファイル</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Incomplete Files</source>
        <translation>不完全なファイル</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Shared Directories</source>
        <translation>共有ディレクトリ</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>All Directories</source>
        <translation>すべてのディレクトリ</translation>
    </message>
    <message>
        <location line="+190"/>
        <source>Current Session</source>
        <translation>現在のセッション</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+45"/>
        <source>Popularity Rank:</source>
        <translation>人気ランキング：</translation>
    </message>
    <message>
        <location line="-39"/>
        <location line="+45"/>
        <source>  Requests:</source>
        <translation>  リクエスト数：</translation>
    </message>
    <message>
        <location line="-38"/>
        <source>On Queue:</source>
        <translation>キュー：</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+40"/>
        <source>  Accepted Uploads:</source>
        <translation>  承認済みアップロード：</translation>
    </message>
    <message>
        <location line="-33"/>
        <source>Uploading:</source>
        <translation>アップロード中：</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+35"/>
        <source>  Transferred:</source>
        <translation>  転送済み：</translation>
    </message>
    <message>
        <location line="-27"/>
        <source>Total</source>
        <translation>合計</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Statistics</source>
        <translation>統計</translation>
    </message>
    <message>
        <location line="+288"/>
        <source>%1 (%2 of %3 shared)</source>
        <translation>%1（%3 件中 %2 件を共有）</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Could not share that file</source>
        <translation>そのファイルを共有できませんでした</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Could not unshare that file</source>
        <translation>そのファイルの共有を解除できませんでした</translation>
    </message>
    <message>
        <location line="+491"/>
        <source>Share Directory</source>
        <translation>ディレクトリを共有</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Share with Subdirectories</source>
        <translation>サブディレクトリごと共有</translation>
    </message>
    <message>
        <location line="+12"/>
        <location line="+476"/>
        <source>Unshare Directory</source>
        <translation>ディレクトリの共有を解除</translation>
    </message>
    <message>
        <location line="-467"/>
        <location line="+476"/>
        <source>Unshare with Subdirectories</source>
        <translation>サブディレクトリごと共有を解除</translation>
    </message>
    <message>
        <location line="-380"/>
        <source>Open File not available — web server is not running or stream token not received.</source>
        <translation>ファイルを開く操作は利用できません — Web サーバーが実行されていないか、ストリームトークンを受信していません。</translation>
    </message>
    <message>
        <location line="-923"/>
        <source>Content</source>
        <translation>コンテンツ</translation>
    </message>
    <message>
        <location line="+58"/>
        <source>eD2K Links</source>
        <translation>eD2K リンク</translation>
    </message>
    <message>
        <location line="-18"/>
        <source>Copy</source>
        <translation>コピー</translation>
    </message>
    <message>
        <location line="-586"/>
        <source>Release</source>
        <translation>リリース</translation>
    </message>
    <message>
        <location line="+125"/>
        <source>Add To IRC Clipboard</source>
        <translation>IRC クリップボードに追加</translation>
    </message>
    <message>
        <location line="+94"/>
        <source>Rescan the shared directories. Hold %1 to re-read the media information of all shared files instead.</source>
        <translation>共有ディレクトリを再スキャンします。%1 を押しながらクリックすると、代わりにすべての共有ファイルのメディア情報を読み直します。</translation>
    </message>
    <message>
        <location line="+341"/>
        <source>Basic Options</source>
        <translation>基本オプション</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Add Source</source>
        <translation>ソースを追加</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Not available (requires public IP and open firewall)</source>
        <translation>利用できません（パブリック IP と開放されたファイアウォールが必要）</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Advanced Options</source>
        <translation>詳細オプション</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Add HTML</source>
        <translation>HTML を追加</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add Hashset</source>
        <translation>ハッシュセットを追加</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hostname</source>
        <translation>ホスト名</translation>
    </message>
    <message>
        <location line="+24"/>
        <location line="+469"/>
        <source>Requires a hostname configured in Preferences, or a public IPv6</source>
        <translation>設定でホスト名が構成されているか、パブリック IPv6 が必要です</translation>
    </message>
    <message>
        <location line="-299"/>
        <source>Shared Files (%1)</source>
        <translation>共有ファイル (%1)</translation>
    </message>
    <message numerus="yes">
        <location line="+112"/>
        <source>Are you sure you want to permanently delete %n selected file(s) from disk?</source>
        <translation>
            <numerusform>選択した %n 件のファイルをディスクから完全に削除してもよろしいですか？</numerusform>
        </translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Delete Files</source>
        <translation>ファイルを削除</translation>
    </message>
    <message numerus="yes">
        <location line="+5"/>
        <source>Deleting %n shared file(s) from disk</source>
        <translation>
            <numerusform>共有ファイル %n 件をディスクから削除しています</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="+18"/>
        <source>Remove %n selected file(s) from the shared files list?

The files will remain on disk.</source>
        <translation>
            <numerusform>選択した %n 件のファイルを共有ファイル一覧から削除しますか？

ファイルはディスク上に残ります。</numerusform>
        </translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Unshare Files</source>
        <translation>ファイルの共有を解除</translation>
    </message>
    <message>
        <location line="+155"/>
        <source>Add your hostname or public IPv6 as a source</source>
        <translation>自分のホスト名またはパブリック IPv6 をソースとして追加する</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Showing eD2K links for the first %1 of %2 selected files.</source>
        <translation>選択した %2 件のうち最初の %1 件の eD2K リンクを表示しています。</translation>
    </message>
</context>
<context>
    <name>eMule::StatisticsPanel</name>
    <message>
        <location filename="../src/gui/panels/StatisticsPanel.cpp" line="-49"/>
        <location line="+8"/>
        <source>Session average</source>
        <translation>セッション平均</translation>
    </message>
    <message>
        <location line="-7"/>
        <location line="+8"/>
        <source>Average (3 min)</source>
        <translation>平均 (3 分)</translation>
    </message>
    <message>
        <location line="-7"/>
        <location line="+8"/>
        <source>Current</source>
        <translation>現在</translation>
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
        <translation>現在（オーバーヘッド除く）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Friend slots</source>
        <translation>フレンドスロット</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Active connections</source>
        <translation>アクティブ接続</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Active uploads</source>
        <translation>アクティブアップロード</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total uploads</source>
        <translation>合計アップロード</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Active downloads</source>
        <translation>アクティブダウンロード</translation>
    </message>
    <message>
        <location line="+45"/>
        <source>Transfer</source>
        <translation>転送</translation>
    </message>
    <message>
        <source>Session UL:DL Ratio: -</source>
        <translation type="vanished">セッション UL:DL 比率：-</translation>
    </message>
    <message>
        <source>Friend Session UL:DL Ratio: -</source>
        <translation type="vanished">フレンドセッション UL:DL 比率：-</translation>
    </message>
    <message>
        <source>Cumulative UL:DL Ratio: -</source>
        <translation type="vanished">累積 UL:DL 比率：-</translation>
    </message>
    <message>
        <location line="+10"/>
        <location line="+159"/>
        <location line="+22"/>
        <location line="+321"/>
        <location line="+1508"/>
        <source>Uploads</source>
        <translation>アップロード</translation>
    </message>
    <message>
        <location line="-2006"/>
        <location line="+63"/>
        <location line="+81"/>
        <location line="+53"/>
        <location line="+35"/>
        <location line="+1489"/>
        <location line="+153"/>
        <location line="+121"/>
        <source>Session</source>
        <translation>セッション</translation>
    </message>
    <message>
        <location line="-1992"/>
        <location line="+32"/>
        <source>Uploaded Data: 0 Bytes</source>
        <translation>アップロードデータ：0 Bytes</translation>
    </message>
    <message>
        <location line="-19"/>
        <source>Uploaded Data to Friends: 0 Bytes</source>
        <translation>フレンドへのアップロードデータ：0 Bytes</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Active Uploads: 0</source>
        <translation>アクティブアップロード：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Waiting Uploads: 0</source>
        <translation>待機中アップロード：0</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+27"/>
        <location line="+459"/>
        <source>Upload Sessions</source>
        <translation>アップロードセッション</translation>
    </message>
    <message>
        <location line="-485"/>
        <location line="+27"/>
        <location line="+39"/>
        <location line="+33"/>
        <source>Successful: 0</source>
        <translation>成功：0</translation>
    </message>
    <message>
        <location line="-98"/>
        <location line="+27"/>
        <location line="+39"/>
        <location line="+33"/>
        <source>Failed: 0</source>
        <translation>失敗：0</translation>
    </message>
    <message>
        <location line="-97"/>
        <location line="+27"/>
        <source>Average Upload Per Session: 0 Bytes</source>
        <translation>セッションあたりの平均アップロード：0 Bytes</translation>
    </message>
    <message>
        <location line="-25"/>
        <location line="+27"/>
        <source>Average Upload Time: 0:00:00</source>
        <translation>平均アップロード時間：0:00:00</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+102"/>
        <location line="+22"/>
        <location line="+330"/>
        <location line="+1130"/>
        <location line="+367"/>
        <source>Downloads</source>
        <translation>ダウンロード</translation>
    </message>
    <message>
        <location line="-1944"/>
        <location line="+42"/>
        <source>Downloaded Data: 0 Bytes</source>
        <translation>ダウンロードデータ：0 Bytes</translation>
    </message>
    <message>
        <location line="-33"/>
        <source>Active Downloads: 0</source>
        <translation>アクティブダウンロード：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Found Sources: 0</source>
        <translation>検出ソース：0</translation>
    </message>
    <message>
        <location line="+64"/>
        <source>Connection</source>
        <translation>接続</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Active Connections: 0</source>
        <translation>アクティブ接続：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+24"/>
        <source>Peak Connections: 0</source>
        <translation>ピーク接続：0</translation>
    </message>
    <message>
        <location line="-23"/>
        <source>Max Connections Limit Reached: 0</source>
        <translation>最大接続制限到達：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Reconnects: 0</source>
        <translation>再接続：0</translation>
    </message>
    <message>
        <location line="+38"/>
        <source>Time Statistics</source>
        <translation>時間統計</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Time Since Last Reset: -</source>
        <translation>最後のリセットからの時間：-</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Runtime: 0:00:00</source>
        <translation>実行時間：0:00:00</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+11"/>
        <source>Transfer Time: 0:00:00</source>
        <translation>転送時間：0:00:00</translation>
    </message>
    <message>
        <location line="-10"/>
        <location line="+11"/>
        <source>Upload Time: 0:00:00</source>
        <translation>アップロード時間：0:00:00</translation>
    </message>
    <message>
        <location line="-10"/>
        <location line="+11"/>
        <source>Download Time: 0:00:00</source>
        <translation>ダウンロード時間：0:00:00</translation>
    </message>
    <message>
        <source>Server Duration: 0:00:00</source>
        <translation type="vanished">サーバー接続時間：0:00:00</translation>
    </message>
    <message>
        <location line="-209"/>
        <location line="+32"/>
        <location line="+31"/>
        <location line="+42"/>
        <location line="+110"/>
        <location line="+269"/>
        <source>Clients</source>
        <translation>クライアント</translation>
    </message>
    <message>
        <location line="-481"/>
        <location line="+32"/>
        <location line="+31"/>
        <location line="+42"/>
        <source>%1: 0 Bytes</source>
        <translation>%1: 0 Bytes</translation>
    </message>
    <message>
        <location line="-104"/>
        <location line="+32"/>
        <location line="+31"/>
        <location line="+42"/>
        <location line="+379"/>
        <source>Port</source>
        <translation>ポート</translation>
    </message>
    <message>
        <location line="-483"/>
        <location line="+32"/>
        <location line="+31"/>
        <location line="+42"/>
        <source>Default Port 4662: 0 Bytes</source>
        <translation>既定ポート 4662: 0 Bytes</translation>
    </message>
    <message>
        <location line="-104"/>
        <location line="+32"/>
        <location line="+31"/>
        <location line="+42"/>
        <source>Other Ports: 0 Bytes</source>
        <translation>その他のポート: 0 Bytes</translation>
    </message>
    <message>
        <location line="-104"/>
        <location line="+32"/>
        <location line="+460"/>
        <source>Data Source</source>
        <translation>データソース</translation>
    </message>
    <message>
        <location line="-491"/>
        <location line="+32"/>
        <source>Complete File: 0 Bytes</source>
        <translation>完全なファイル: 0 Bytes</translation>
    </message>
    <message>
        <location line="-31"/>
        <location line="+32"/>
        <source>Part File: 0 Bytes</source>
        <translation>パートファイル: 0 Bytes</translation>
    </message>
    <message>
        <location line="-13"/>
        <location line="+73"/>
        <location line="+63"/>
        <location line="+40"/>
        <location line="+24"/>
        <location line="+1494"/>
        <location line="+149"/>
        <location line="+125"/>
        <source>Cumulative</source>
        <translation>累計</translation>
    </message>
    <message>
        <location line="-1916"/>
        <location line="+33"/>
        <source>Completed Downloads: 0</source>
        <translation>完了したダウンロード: 0</translation>
    </message>
    <message>
        <location line="-31"/>
        <location line="+33"/>
        <location line="+400"/>
        <source>Download Sessions</source>
        <translation>ダウンロードセッション</translation>
    </message>
    <message>
        <location line="-429"/>
        <location line="+33"/>
        <source>Average Download Per Session: 0 Bytes</source>
        <translation>セッションあたりの平均ダウンロード: 0 Bytes</translation>
    </message>
    <message>
        <location line="-31"/>
        <location line="+33"/>
        <source>Average Download Time: 0:00:00</source>
        <translation>平均ダウンロード時間: 0:00:00</translation>
    </message>
    <message>
        <location line="-30"/>
        <location line="+33"/>
        <source>Gain Due To Compression: 0 Bytes (0.0%)</source>
        <translation>圧縮による節約: 0 Bytes (0.0%)</translation>
    </message>
    <message>
        <location line="-31"/>
        <location line="+33"/>
        <source>Lost Due To Corruption: 0 Bytes (0.0%)</source>
        <translation>破損による損失: 0 Bytes (0.0%)</translation>
    </message>
    <message>
        <location line="-31"/>
        <location line="+33"/>
        <source>Parts Saved Due To ICH: 0</source>
        <translation>ICH により回復したパート: 0</translation>
    </message>
    <message>
        <location line="+15"/>
        <location line="+24"/>
        <location line="+1451"/>
        <source>General</source>
        <translation>全般</translation>
    </message>
    <message>
        <location line="-1469"/>
        <source>Average Connections: 0.0</source>
        <translation>平均接続数: 0.0</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Upload Speed: 0 KB/s</source>
        <translation>アップロード速度: 0 KB/s</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+22"/>
        <source>Max Upload Rate: 0 KB/s</source>
        <translation>最大アップロードレート: 0 KB/s</translation>
    </message>
    <message>
        <location line="-21"/>
        <location line="+22"/>
        <source>Max Average Upload Rate: 0 KB/s</source>
        <translation>最大平均アップロードレート: 0 KB/s</translation>
    </message>
    <message>
        <location line="-18"/>
        <source>Download Speed: 0 KB/s</source>
        <translation>ダウンロード速度: 0 KB/s</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+22"/>
        <source>Max Download Rate: 0 KB/s</source>
        <translation>最大ダウンロードレート: 0 KB/s</translation>
    </message>
    <message>
        <location line="-21"/>
        <location line="+22"/>
        <source>Max Average Download Rate: 0 KB/s</source>
        <translation>最大平均ダウンロードレート: 0 KB/s</translation>
    </message>
    <message>
        <location line="-14"/>
        <source>Server Reconnects: 0</source>
        <translation>サーバー再接続: 0</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Connection Limit Reached: 0</source>
        <translation>接続制限に到達: 0</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Average Upload Rate: 0 KB/s</source>
        <translation>平均アップロードレート: 0 KB/s</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Average Download Rate: 0 KB/s</source>
        <translation>平均ダウンロードレート: 0 KB/s</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+693"/>
        <location line="+5"/>
        <source>Statistics Last Reset: %1</source>
        <translation>統計の最終リセット: %1</translation>
    </message>
    <message>
        <location line="-995"/>
        <location line="+297"/>
        <location line="+543"/>
        <location line="+148"/>
        <location line="+8"/>
        <location line="+1027"/>
        <source>Unknown</source>
        <translation>不明</translation>
    </message>
    <message>
        <location line="-2030"/>
        <source>Statistics Tree</source>
        <translation>統計ツリー</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+999"/>
        <source>Statistics last reset: %1</source>
        <translation>統計の最終リセット: %1</translation>
    </message>
    <message>
        <location line="-971"/>
        <location line="+1794"/>
        <source>Usenet</source>
        <translation>Usenet</translation>
    </message>
    <message>
        <location line="-1729"/>
        <location line="+1606"/>
        <source>Waiting...</source>
        <translation>待機中...</translation>
    </message>
    <message>
        <location line="-1603"/>
        <location line="+596"/>
        <source>Session UL:DL Ratio (Friends UL excluded): %1</source>
        <translation>セッション UL:DL 比率 (友達へのULを除く)：%1</translation>
    </message>
    <message>
        <location line="-506"/>
        <source>UDP File Re-asks: 0, Failed: 0 (0.0%)</source>
        <translation>UDP ファイル再問い合わせ：0、失敗：0 (0.0%)</translation>
    </message>
    <message>
        <location line="+1558"/>
        <source>Corrupt (Failed yEnc Check): %1</source>
        <translation>破損 (yEnc チェック失敗): %1</translation>
    </message>
    <message>
        <location line="+350"/>
        <source>HTTP Cache</source>
        <translation>HTTP キャッシュ</translation>
    </message>
    <message>
        <source>Published: 0 Bytes</source>
        <translation type="vanished">公開済み: 0 Bytes</translation>
    </message>
    <message>
        <source>Fetched: 0 Bytes</source>
        <translation type="vanished">取得済み: 0 Bytes</translation>
    </message>
    <message>
        <source>Upload Saved: 0 Bytes</source>
        <translation type="vanished">アップロード節約: 0 Bytes</translation>
    </message>
    <message>
        <source>Chunks Published: 0</source>
        <translation type="vanished">公開チャンク数: 0</translation>
    </message>
    <message>
        <source>Chunks Fetched: 0</source>
        <translation type="vanished">取得チャンク数: 0</translation>
    </message>
    <message>
        <location line="-1780"/>
        <source>Run Time: 0:00:00</source>
        <translation>実行時間: 0:00:00</translation>
    </message>
    <message>
        <location line="-4"/>
        <location line="+8"/>
        <source>Total Server Duration: 0:00:00</source>
        <translation>サーバー合計時間: 0:00:00</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Known Clients: 0</source>
        <translation>既知のクライアント：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Client Software</source>
        <translation>クライアントソフトウェア</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Low ID: 0 (0.0%)</source>
        <translation>Low ID：0 (0.0%)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Banned Clients: 0</source>
        <translation>BAN されたクライアント：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Filtered Clients: 0</source>
        <translation>フィルターされたクライアント：0</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Servers</source>
        <translation>サーバー</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Working Servers: 0</source>
        <translation>稼働中のサーバー：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Failed Servers: 0</source>
        <translation>失敗したサーバー：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total: 0</source>
        <translation>合計：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Users: 0</source>
        <translation>合計ユーザー：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Files: 0</source>
        <translation>合計ファイル：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Low ID Users: 0</source>
        <translation>Low ID ユーザー：0</translation>
    </message>
    <message>
        <location line="+2"/>
        <location line="+14"/>
        <source>Records</source>
        <translation>記録</translation>
    </message>
    <message>
        <location line="-12"/>
        <source>Most Working Servers: 0</source>
        <translation>最多の稼働サーバー: 0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Most Users Online: 0</source>
        <translation>最多のオンラインユーザー: 0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Most Files Available: 0</source>
        <translation>最多の利用可能ファイル: 0</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Shared Files</source>
        <translation>共有ファイル</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Number of Shared Files: 0</source>
        <translation>共有ファイル数：0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Size: 0 Bytes</source>
        <translation>合計サイズ：0 Bytes</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Average File Size: 0 Bytes</source>
        <translation>平均ファイルサイズ: 0 Bytes</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Largest Shared File: 0 Bytes</source>
        <translation>最大共有ファイル：0 Bytes</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Most Files Shared: 0</source>
        <translation>最多の共有ファイル: 0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Largest Share Size: 0 Bytes</source>
        <translation>最大の共有サイズ: 0 Bytes</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Largest Average File Size: 0 Bytes</source>
        <translation>最大の平均ファイルサイズ: 0 Bytes</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Largest File Size: 0 Bytes</source>
        <translation>最大のファイルサイズ: 0 Bytes</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Total Downloads</source>
        <translation>ダウンロード合計</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Number of Downloads: 0</source>
        <translation>ダウンロード数: 0</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Size of Downloads: 0 Bytes</source>
        <translation>ダウンロードの総サイズ: 0 Bytes</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Size Downloaded: 0 Bytes</source>
        <translation>ダウンロード済みの総サイズ: 0 Bytes</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Size Left to Download: 0 Bytes</source>
        <translation>残りのダウンロードサイズ: 0 Bytes</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Free Space on Drive: 0 Bytes</source>
        <translation>ドライブの空き容量: 0 Bytes</translation>
    </message>
    <message>
        <location line="-298"/>
        <location line="+596"/>
        <source>Session UL:DL Ratio: %1</source>
        <translation>セッション UL:DL 比率：%1</translation>
    </message>
    <message>
        <source>Friend Session UL:DL Ratio: %1</source>
        <translation type="vanished">フレンドセッション UL:DL 比率：%1</translation>
    </message>
    <message>
        <location line="-593"/>
        <location line="+597"/>
        <source>Cumulative UL:DL Ratio: %1</source>
        <translation>累積 UL:DL 比率：%1</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+56"/>
        <source>Uploaded Data: %1</source>
        <translation>アップロードデータ：%1</translation>
    </message>
    <message>
        <location line="-45"/>
        <location line="+55"/>
        <location line="+60"/>
        <location line="+100"/>
        <source>Default Port 4662: %1 %2</source>
        <translation>既定ポート 4662: %1 %2</translation>
    </message>
    <message>
        <location line="-212"/>
        <location line="+55"/>
        <location line="+60"/>
        <location line="+100"/>
        <source>Other Ports: %1 %2</source>
        <translation>その他のポート: %1 %2</translation>
    </message>
    <message>
        <location line="-212"/>
        <location line="+55"/>
        <source>Complete File: %1 %2</source>
        <translation>完全なファイル: %1 %2</translation>
    </message>
    <message>
        <location line="-52"/>
        <location line="+55"/>
        <source>Part File: %1 %2</source>
        <translation>パートファイル: %1 %2</translation>
    </message>
    <message>
        <location line="-50"/>
        <source>Uploaded Data to Friends: %1</source>
        <translation>フレンドへのアップロードデータ：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Active Uploads: %1</source>
        <translation>アクティブアップロード：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Waiting Uploads: %1</source>
        <translation>待機中アップロード：%1</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+48"/>
        <location line="+96"/>
        <location line="+12"/>
        <source>Successful: %1%2</source>
        <translation>成功：%1%2</translation>
    </message>
    <message>
        <location line="-154"/>
        <location line="+48"/>
        <location line="+96"/>
        <location line="+12"/>
        <location line="+871"/>
        <location line="+49"/>
        <source>Failed: %1</source>
        <translation>失敗：%1</translation>
    </message>
    <message>
        <location line="-1073"/>
        <location line="+48"/>
        <source>Average Upload Per Session: %1</source>
        <translation>セッションあたりの平均アップロード：%1</translation>
    </message>
    <message>
        <location line="-46"/>
        <location line="+48"/>
        <source>Average Upload Time: %1</source>
        <translation>平均アップロード時間：%1</translation>
    </message>
    <message>
        <location line="-44"/>
        <source>%1: %2</source>
        <translation>%1: %2</translation>
    </message>
    <message>
        <location line="-168"/>
        <location line="+172"/>
        <location line="+60"/>
        <location line="+100"/>
        <location line="+36"/>
        <source>Total Overhead (Packets)</source>
        <translation>総オーバーヘッド (パケット)</translation>
    </message>
    <message>
        <location line="-367"/>
        <location line="+172"/>
        <location line="+60"/>
        <location line="+100"/>
        <location line="+36"/>
        <source>File Request Overhead (Packets)</source>
        <translation>ファイル要求オーバーヘッド (パケット)</translation>
    </message>
    <message>
        <location line="-367"/>
        <location line="+172"/>
        <location line="+60"/>
        <location line="+100"/>
        <location line="+36"/>
        <source>Source Exchange Overhead (Packets)</source>
        <translation>ソース交換オーバーヘッド (パケット)</translation>
    </message>
    <message>
        <location line="-367"/>
        <location line="+172"/>
        <location line="+60"/>
        <location line="+100"/>
        <location line="+36"/>
        <source>Server Overhead (Packets)</source>
        <translation>サーバーオーバーヘッド (パケット)</translation>
    </message>
    <message>
        <location line="-367"/>
        <location line="+172"/>
        <location line="+60"/>
        <location line="+100"/>
        <location line="+36"/>
        <source>Kad Overhead (Packets)</source>
        <translation>Kad オーバーヘッド (パケット)</translation>
    </message>
    <message>
        <source>Published</source>
        <translation type="vanished">公開済み</translation>
    </message>
    <message>
        <source>Fetched</source>
        <translation type="vanished">取得済み</translation>
    </message>
    <message>
        <source>Upload Saved</source>
        <translation type="vanished">アップロード節約</translation>
    </message>
    <message>
        <source>Chunks Published</source>
        <translation type="vanished">公開チャンク数</translation>
    </message>
    <message>
        <source>Chunks Fetched</source>
        <translation type="vanished">取得チャンク数</translation>
    </message>
    <message>
        <location line="-133"/>
        <location line="+100"/>
        <location line="+821"/>
        <source>Downloaded Data: %1</source>
        <translation>ダウンロードデータ：%1</translation>
    </message>
    <message>
        <location line="-903"/>
        <source>Active Downloads: %1</source>
        <translation>アクティブダウンロード：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Found Sources: %1</source>
        <translation>見つかったソース: %1</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>UDP File Re-asks: %1, Failed: %2 %3</source>
        <translation>UDP ファイル再問い合わせ：%1、失敗：%2 %3</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+65"/>
        <source>Completed Downloads: %1</source>
        <translation>完了したダウンロード: %1</translation>
    </message>
    <message>
        <location line="-31"/>
        <location line="+36"/>
        <source>Gain Due To Compression: %1 %2</source>
        <translation>圧縮による節約: %1 %2</translation>
    </message>
    <message>
        <location line="-34"/>
        <location line="+36"/>
        <source>Lost Due To Corruption: %1 %2</source>
        <translation>破損による損失: %1 %2</translation>
    </message>
    <message>
        <location line="-34"/>
        <location line="+36"/>
        <source>Parts Saved Due To ICH: %1</source>
        <translation>ICH により回復したパート: %1</translation>
    </message>
    <message>
        <location line="+10"/>
        <location line="+782"/>
        <source>Active Connections: %1</source>
        <translation>アクティブ接続：%1</translation>
    </message>
    <message>
        <location line="-780"/>
        <location line="+19"/>
        <location line="+762"/>
        <source>Peak Connections: %1</source>
        <translation>ピーク接続：%1</translation>
    </message>
    <message>
        <location line="-779"/>
        <source>Max Connections Limit Reached: %1</source>
        <translation>最大接続制限到達：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Reconnects: %1</source>
        <translation>再接続：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Average Connections: %1</source>
        <translation>平均接続：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Upload Speed: %1</source>
        <translation>アップロード速度: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+15"/>
        <source>Max Upload Rate: %1</source>
        <translation>最大アップロードレート: %1</translation>
    </message>
    <message>
        <location line="-14"/>
        <location line="+15"/>
        <source>Max Average Upload Rate: %1</source>
        <translation>最大平均アップロードレート: %1</translation>
    </message>
    <message>
        <location line="-14"/>
        <location line="+764"/>
        <source>Download Speed: %1</source>
        <translation>ダウンロード速度: %1</translation>
    </message>
    <message>
        <location line="-763"/>
        <location line="+15"/>
        <location line="+750"/>
        <source>Max Download Rate: %1</source>
        <translation>最大ダウンロードレート: %1</translation>
    </message>
    <message>
        <location line="-764"/>
        <location line="+15"/>
        <source>Max Average Download Rate: %1</source>
        <translation>最大平均ダウンロードレート: %1</translation>
    </message>
    <message>
        <location line="-11"/>
        <source>Server Reconnects: %1</source>
        <translation>サーバー再接続: %1</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Connection Limit Reached: %1</source>
        <translation>接続制限に到達: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Average Upload Rate: %1</source>
        <translation>平均アップロードレート: %1</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+750"/>
        <source>Average Download Rate: %1</source>
        <translation>平均ダウンロードレート: %1</translation>
    </message>
    <message>
        <location line="-737"/>
        <location line="+5"/>
        <source>Time Since Last Reset: %1</source>
        <translation>最後のリセットからの時間：%1</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Runtime: %1</source>
        <translation>実行時間：%1</translation>
    </message>
    <message>
        <location line="+8"/>
        <location line="+20"/>
        <source>Transfer Time: %1 %2</source>
        <translation>転送時間：%1 %2</translation>
    </message>
    <message>
        <location line="-18"/>
        <location line="+20"/>
        <source>Upload Time: %1 %2</source>
        <translation>アップロード時間：%1 %2</translation>
    </message>
    <message>
        <location line="-18"/>
        <location line="+20"/>
        <location line="+692"/>
        <source>Download Time: %1 %2</source>
        <translation>ダウンロード時間：%1 %2</translation>
    </message>
    <message>
        <source>Server Duration: %1 %2</source>
        <translation type="vanished">サーバー接続時間：%1 %2</translation>
    </message>
    <message>
        <location line="-698"/>
        <source>Run Time: %1</source>
        <translation>実行時間: %1</translation>
    </message>
    <message>
        <location line="-9"/>
        <location line="+17"/>
        <source>Total Server Duration: %1 %2</source>
        <translation>サーバー合計時間: %1 %2</translation>
    </message>
    <message>
        <location line="-731"/>
        <source>Current Server Duration: 0:00:00</source>
        <translation>現在のサーバー接続時間: 0:00:00</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Different clients, counted by user hash. An estimate, accurate to about 2%.</source>
        <translation>ユーザーハッシュで数えた異なるクライアント。誤差約 2% の推定値です。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Clients that said hello on a connection.</source>
        <translation>接続上で hello を送ってきたクライアント。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Clients that proved their user hash with Secure Identification.</source>
        <translation>安全な識別でユーザーハッシュを証明したクライアント。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Clients by the country of the address they connected from.</source>
        <translation>接続元アドレスの国別のクライアント。</translation>
    </message>
    <message>
        <location line="+531"/>
        <location line="+12"/>
        <source>Average Download Per Session: %1</source>
        <translation>セッションあたりの平均ダウンロード: %1</translation>
    </message>
    <message>
        <location line="-10"/>
        <location line="+12"/>
        <source>Average Download Time: %1</source>
        <translation>平均ダウンロード時間: %1</translation>
    </message>
    <message>
        <location line="+127"/>
        <source>Current Server Duration: %1 %2</source>
        <translation>現在のサーバー接続時間: %1 %2</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Known Clients: %1</source>
        <translation>既知のクライアント：%1</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Low ID: %1 %2</source>
        <translation>Low ID：%1 %2</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Banned Clients: %1</source>
        <translation>BAN されたクライアント：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Filtered Clients: %1</source>
        <translation>フィルターされたクライアント：%1</translation>
    </message>
    <message>
        <location line="+87"/>
        <source>Working Servers: %1</source>
        <translation>稼働中のサーバー：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Failed Servers: %1</source>
        <translation>失敗したサーバー：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Total: %1</source>
        <translation>合計：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Total Users: %1</source>
        <translation>合計ユーザー：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Total Files: %1</source>
        <translation>合計ファイル：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Low ID Users: %1</source>
        <translation>Low ID ユーザー：%1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Most Working Servers: %1</source>
        <translation>最多の稼働サーバー: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Most Users Online: %1</source>
        <translation>最多のオンラインユーザー: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Most Files Available: %1</source>
        <translation>最多の利用可能ファイル: %1</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Number of Shared Files: %1</source>
        <translation>共有ファイル数：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Total Size: %1</source>
        <translation>合計サイズ：%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Average File Size: %1</source>
        <translation>平均ファイルサイズ: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Largest Shared File: %1</source>
        <translation>最大共有ファイル：%1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Most Files Shared: %1</source>
        <translation>最多の共有ファイル: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Largest Share Size: %1</source>
        <translation>最大の共有サイズ: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Largest Average File Size: %1</source>
        <translation>最大の平均ファイルサイズ: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Largest File Size: %1</source>
        <translation>最大のファイルサイズ: %1</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+620"/>
        <source>Number of Downloads: %1</source>
        <translation>ダウンロード数: %1</translation>
    </message>
    <message>
        <location line="-618"/>
        <location line="+626"/>
        <source>Total Size of Downloads: %1</source>
        <translation>ダウンロードの総サイズ: %1</translation>
    </message>
    <message>
        <location line="-624"/>
        <location line="+625"/>
        <source>Total Size Downloaded: %1</source>
        <translation>ダウンロード済みの総サイズ: %1</translation>
    </message>
    <message>
        <location line="-623"/>
        <location line="+624"/>
        <source>Total Size Left to Download: %1</source>
        <translation>残りのダウンロードサイズ: %1</translation>
    </message>
    <message>
        <location line="-622"/>
        <source>Free Space on Drive: %1</source>
        <translation>ドライブの空き容量: %1</translation>
    </message>
    <message>
        <location line="+64"/>
        <source>Clients Seen: ≈%1</source>
        <translation>確認したクライアント: ≈%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Identified: ≈%1 %2</source>
        <translation>識別済み: ≈%1 %2</translation>
    </message>
    <message>
        <location line="+47"/>
        <source>Not running</source>
        <translation>停止中</translation>
    </message>
    <message>
        <location line="-466"/>
        <location line="+468"/>
        <source>Connecting</source>
        <translation>接続中</translation>
    </message>
    <message>
        <location line="-755"/>
        <source>Projected Averages</source>
        <translation>予測平均</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Daily</source>
        <translation>毎日</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Monthly</source>
        <translation>月次</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Yearly</source>
        <translation>年次</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Default Port 4662</source>
        <translation>既定ポート 4662</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Other Ports</source>
        <translation>その他のポート</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Uploaded Data</source>
        <translation>アップロードしたデータ</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Complete File</source>
        <translation>完全なファイル</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Part File</source>
        <translation>部分ファイル</translation>
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
        <translation>ダウンロードしたデータ</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Completed Downloads</source>
        <translation>完了したダウンロード</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Gained Due To Compression</source>
        <translation>圧縮による節約</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Lost Due To Corruption</source>
        <translation>破損による損失</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Parts Saved Due To ICH</source>
        <translation>ICH で救済したパート</translation>
    </message>
    <message>
        <location line="+208"/>
        <source>On Queue</source>
        <translation>キュー待ち</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Queue Full</source>
        <translation>キューが満杯</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>No needed parts</source>
        <translation>必要なパートなし</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Asking</source>
        <translation>問い合わせ中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Receiving hashset</source>
        <translation>ハッシュセット受信中</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Connecting via server</source>
        <translation>サーバー経由で接続中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Too many connections</source>
        <translation>接続数超過</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Cannot connect LowID to LowID</source>
        <translation>LowID 同士は接続できません</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Problematic</source>
        <translation>問題あり</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Banned</source>
        <translation>BAN済み</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Asked for another file</source>
        <translation>別のファイルを要求</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>via eD2K Server</source>
        <translation>eD2K サーバー経由</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>via Kad</source>
        <translation>Kad 経由</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>via Source Exchange</source>
        <translation>ソース交換経由</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>via Passive</source>
        <translation>パッシブ経由</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Dead Sources: %1 (%2 + %3)</source>
        <translation>無効なソース: %1 (%2 + %3)</translation>
    </message>
    <message>
        <location line="+453"/>
        <source>Connected (LAN mode)</source>
        <translation>接続済み (LAN モード)</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Connected, TCP and UDP firewalled</source>
        <translation>接続済み、TCP と UDP がファイアウォール内</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Connected, TCP firewalled</source>
        <translation>接続済み、TCP がファイアウォール内</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Connected, UDP firewalled</source>
        <translation>接続済み、UDP がファイアウォール内</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Connected, open</source>
        <translation>接続済み、オープン</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Status: %1</source>
        <translation>状態: %1</translation>
    </message>
    <message>
        <location line="+25"/>
        <location line="+45"/>
        <source>Reset Statistics</source>
        <translation>統計をリセット</translation>
    </message>
    <message>
        <location line="-44"/>
        <location line="+64"/>
        <source>Restore Statistics</source>
        <translation>統計を復元</translation>
    </message>
    <message>
        <location line="-58"/>
        <source>Expand Main Sections</source>
        <translation>メインセクションを展開</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Expand All Sections</source>
        <translation>すべてのセクションを展開</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Collapse All Sections</source>
        <translation>すべてのセクションを折りたたむ</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+7"/>
        <source>Copy Branch</source>
        <translation>ブランチをコピー</translation>
    </message>
    <message>
        <location line="-6"/>
        <location line="+7"/>
        <source>Copy All Visible</source>
        <translation>表示中のすべてをコピー</translation>
    </message>
    <message>
        <location line="-6"/>
        <location line="+7"/>
        <source>Copy All Statistics</source>
        <translation>すべての統計をコピー</translation>
    </message>
    <message>
        <location line="-3"/>
        <source>HTML Features</source>
        <translation>HTML 機能</translation>
    </message>
    <message>
        <location line="+5"/>
        <location line="+80"/>
        <location line="+7"/>
        <source>Export Statistics...</source>
        <translation>統計をエクスポート...</translation>
    </message>
    <message>
        <location line="-68"/>
        <source>Are you sure you wish to reset your cumulative statistics?

If you change your mind, you can reverse this action by clicking the &apos;Restore Stats&apos; button.</source>
        <translation>累積統計をリセットしてもよろしいですか?

気が変わった場合は、「統計を復元」ボタンをクリックしてこの操作を元に戻せます。</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Are you sure you wish to restore your cumulative statistics from the backup file?

Clicking &apos;Restore Stats&apos; again will reload your current statistics.</source>
        <translation>バックアップファイルから累積統計を復元してもよろしいですか?

「統計を復元」をもう一度クリックすると、現在の統計が再度読み込まれます。</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>HTML Files (*.html);;All Files (*)</source>
        <translation>HTML ファイル (*.html);;すべてのファイル (*)</translation>
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
        <translation>名前：</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>eMule Qt v%1 %2 [%3]</source>
        <translation>eMule Qt v%1 %2 [%3]</translation>
    </message>
    <message>
        <location line="+179"/>
        <location line="+491"/>
        <source>Open Connections: %1</source>
        <translation>オープン接続: %1</translation>
    </message>
    <message>
        <location line="-487"/>
        <source>Network Traffic: %1</source>
        <translation>ネットワークトラフィック: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Overhead: %1 %2</source>
        <translation>オーバーヘッド: %1 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Articles</source>
        <translation>記事</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloaded: %1</source>
        <translation>ダウンロード済み: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not Found on a Server: %1</source>
        <translation>サーバーで見つからず: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Missing on All Servers: %1</source>
        <translation>すべてのサーバーで欠落: %1</translation>
    </message>
    <message>
        <location line="+4"/>
        <location line="+486"/>
        <source>Connection Errors: %1</source>
        <translation>接続エラー: %1</translation>
    </message>
    <message>
        <location line="-483"/>
        <source>Completed Downloads: %1 %2</source>
        <translation>完了したダウンロード: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Completed Data: %1</source>
        <translation>完了したデータ: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Failed Downloads: %1 %2</source>
        <translation>失敗したダウンロード: %1 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Post-Processing</source>
        <translation>後処理</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>PAR2 Verified: %1</source>
        <translation>PAR2 検証済み: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Repaired: %1 %2</source>
        <translation>修復済み: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Repair Failed: %1 %2</source>
        <translation>修復失敗: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Blocks Repaired: %1</source>
        <translation>修復したブロック: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Recovery Volumes Fetched: %1</source>
        <translation>取得したリカバリーボリューム: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Recovery Data: %1</source>
        <translation>リカバリーデータ: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unpacked: %1</source>
        <translation>展開済み: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Password Required: %1</source>
        <translation>パスワードが必要: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Sets Unpacked While Downloading: %1</source>
        <translation>ダウンロード中に展開したセット: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Time Spent: %1</source>
        <translation>所要時間: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Verifying: %1 %2</source>
        <translation>検証中: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Repairing: %1 %2</source>
        <translation>修復中: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unpacking: %1 %2</source>
        <translation>展開中: %1 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Health Checks</source>
        <translation>健全性チェック</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Checks Run: %1</source>
        <translation>実行したチェック: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Passed: %1 %2</source>
        <translation>合格: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paused as Incomplete: %1 %2</source>
        <translation>不完全として一時停止: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Inconclusive: %1 %2</source>
        <translation>判定不能: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Articles Probed: %1</source>
        <translation>調査した記事: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Intake</source>
        <translation>取り込み</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>NZBs Added: %1</source>
        <translation>追加した NZB: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Files: %1 %2</source>
        <translation>ファイル: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>URLs: %1 %2</source>
        <translation>URL: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Watch Folder: %1 %2</source>
        <translation>監視フォルダ: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Feeds: %1 %2</source>
        <translation>フィード: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Indexer Searches: %1 %2</source>
        <translation>インデクサー検索: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Duplicates: %1</source>
        <translation>重複: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Already Downloaded: %1</source>
        <translation>すでにダウンロード済み: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Invalid NZBs: %1</source>
        <translation>無効な NZB: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Indexers</source>
        <translation>インデクサー</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+128"/>
        <source>Searches: %1</source>
        <translation>検索: %1</translation>
    </message>
    <message>
        <location line="-127"/>
        <source>API Requests: %1</source>
        <translation>API リクエスト: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Errors: %1 %2</source>
        <translation>エラー: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>NZBs Fetched: %1</source>
        <translation>取得した NZB: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+285"/>
        <source>Failed: %1 %2</source>
        <translation>失敗: %1 %2</translation>
    </message>
    <message>
        <location line="-284"/>
        <source>Feed Polls: %1</source>
        <translation>フィードのチェック回数: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Feed Matches: %1</source>
        <translation>フィードの一致件数: %1</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Downloading: %1</source>
        <translation>ダウンロード中: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Queued: %1</source>
        <translation>キュー待ち: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paused: %1</source>
        <translation>一時停止中: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Checking: %1</source>
        <translation>チェック中: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Post-Processing: %1</source>
        <translation>後処理: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Completed: %1</source>
        <translation>完了: %1</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>News Servers</source>
        <translation>ニュースサーバー</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Queue</source>
        <translation>キュー</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Contacts: %1</source>
        <translation>コンタクト: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+8"/>
        <source>IP Verified: %1 %2</source>
        <translation>IP 検証済み: %1 %2</translation>
    </message>
    <message>
        <location line="-7"/>
        <source>Type 0, Alive over 2 Hours: %1 %2</source>
        <translation>タイプ 0、2 時間以上稼働: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type 1, Alive 1 to 2 Hours: %1 %2</source>
        <translation>タイプ 1、1～2 時間稼働: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type 2, Alive under 1 Hour: %1 %2</source>
        <translation>タイプ 2、1 時間未満稼働: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type 3, New or Not Answering: %1 %2</source>
        <translation>タイプ 3、新規または無応答: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type 4, Dead: %1 %2</source>
        <translation>タイプ 4、停止: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Most Contacts: %1</source>
        <translation>最大コンタクト数: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Contacts Added: %1</source>
        <translation>追加されたコンタクト: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Replaced a Weaker Contact: %1 %2</source>
        <translation>弱いコンタクトを置き換え: %1 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Contacts Expired: %1</source>
        <translation>期限切れのコンタクト: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Contacts Banned: %1</source>
        <translation>禁止されたコンタクト: %1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Nodes Seen: ≈%1</source>
        <translation>確認したノード: ≈%1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nodes Heard Of: ≈%1</source>
        <translation>伝え聞いたノード: ≈%1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Firewalled (Kad)</source>
        <translation>ファイアウォール内 (Kad)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>UDP: %1 %2</source>
        <translation>UDP: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>TCP: %1 %2</source>
        <translation>TCP: %1 %2</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Estimated Users: %1</source>
        <translation>推定ユーザー数: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Estimated Files: %1</source>
        <translation>推定ファイル数: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Indexed Keywords: %1</source>
        <translation>インデックス済みキーワード: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Indexed Sources: %1</source>
        <translation>インデックス済みソース: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Indexed Notes: %1</source>
        <translation>インデックス済みノート: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Active Searches: %1</source>
        <translation>実行中の検索: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Banned Addresses: %1</source>
        <translation>禁止されたアドレス: %1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Time Connected: %1 %2</source>
        <translation>接続時間: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hellos Sent: %1</source>
        <translation>送信した Hello: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hellos Answered: %1 %2</source>
        <translation>応答のあった Hello: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Lookup Answers: %1</source>
        <translation>ルックアップ応答: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Bootstrap Answers: %1</source>
        <translation>ブートストラップ応答: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Node Lookups: %1 %2</source>
        <translation>ノード検索: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Keyword Searches: %1 %2</source>
        <translation>キーワード検索: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Source Searches: %1 %2</source>
        <translation>ソース検索: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Notes Searches: %1 %2</source>
        <translation>ノート検索: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Publishes: %1</source>
        <translation>公開: %1</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Kademlia</source>
        <translation>Kademlia</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Different nodes, counted by node ID. An estimate, accurate to about 2%.</source>
        <translation>ノード ID で数えた異なるノードの数。誤差約 2% の推定値です。</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Routing Table</source>
        <translation>ルーティングテーブル</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>By Version</source>
        <translation>バージョン別</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Nodes</source>
        <translation>ノード</translation>
    </message>
    <message>
        <location line="-1602"/>
        <location line="+1605"/>
        <source>By Country</source>
        <translation>国別</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nodes that sent us a packet, by the country of the address it came from.</source>
        <translation>パケットを送ってきたノードを、送信元アドレスの国別に示します。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Network</source>
        <translation>ネットワーク</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Activity</source>
        <translation>アクティビティ</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Nodes that sent us a packet themselves.</source>
        <translation>自らパケットを送ってきたノード。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Nodes that other nodes named in their answers; most are never contacted.</source>
        <translation>他のノードが応答の中で挙げたノード。大半には接続しません。</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>Version %1 (eMule %2)</source>
        <translation>バージョン %1 (eMule %2)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Version %1</source>
        <translation>バージョン %1</translation>
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
        <translation>公開済み: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Chunks Published: %1</source>
        <translation>公開したチャンク: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload Saved: %1</source>
        <translation>節約したアップロード: %1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Fetched: %1</source>
        <translation>取得済み: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Chunks Fetched: %1 %2</source>
        <translation>取得したチャンク: %1 %2</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Failed Hash Check: %1</source>
        <translation>ハッシュチェック失敗: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Resumed: %1</source>
        <translation>再開: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Offers Received: %1</source>
        <translation>受信したオファー: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Declined: %1 %2</source>
        <translation>拒否: %1 %2</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Chunks Found in Kad: %1</source>
        <translation>Kad で見つかったチャンク: %1</translation>
    </message>
    <message>
        <location line="+125"/>
        <source>Measured here, not reported by the provider, in decimal GB as providers bill. Expect a few percent below the provider&apos;s own figure.</source>
        <translation>この値はここで計測したもので、プロバイダーから報告されたものではありません。単位は請求と同じ 10 進数の GB です。プロバイダー自身の数値より数パーセント低くなります。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>%1 (disabled)</source>
        <translation>%1 (無効)</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Session Traffic: %1 %2</source>
        <translation>セッションのトラフィック: %1 %2</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Articles Downloaded: %1</source>
        <translation>ダウンロードした記事: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not Found: %1</source>
        <translation>見つからず: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Corrupt: %1</source>
        <translation>破損: %1</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>%1 of %2 %3</source>
        <translation>%2 中 %1 %3</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>, resets %1</source>
        <translation>、%1 にリセット</translation>
    </message>
    <message>
        <location line="+3"/>
        <source> — spent</source>
        <translation> — 使用済み</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Block: %1</source>
        <translation>ブロック: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>This Period: %1</source>
        <translation>今期: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>All Time: %1</source>
        <translation>累計: %1</translation>
    </message>
</context>
<context>
    <name>eMule::ToolbarCustomizeDialog</name>
    <message>
        <location filename="../src/gui/dialogs/ToolbarCustomizeDialog.cpp" line="+38"/>
        <source>Customize Toolbar</source>
        <translation>ツールバーのカスタマイズ</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Available toolbar buttons:</source>
        <translation>使用できるボタン:</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Add -&gt;</source>
        <translation>追加 -&gt;</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>&lt;- Remove</source>
        <translation>&lt;- 削除</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Current toolbar buttons:</source>
        <translation>現在のボタン:</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Close</source>
        <translation>閉じる</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Reset</source>
        <translation>リセット</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Move Up</source>
        <translation>上へ移動</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Move Down</source>
        <translation>下へ移動</translation>
    </message>
</context>
<context>
    <name>eMule::TransferPanel</name>
    <message>
        <location filename="../src/gui/panels/TransferPanel.cpp" line="+713"/>
        <source>Downloads</source>
        <translation>ダウンロード</translation>
    </message>
    <message>
        <location line="-364"/>
        <source>Priority (Download)</source>
        <translation>優先度（ダウンロード）</translation>
    </message>
    <message>
        <location line="+29"/>
        <location line="+1888"/>
        <location line="+162"/>
        <source>Low</source>
        <translation>低い</translation>
    </message>
    <message>
        <location line="-2049"/>
        <location line="+1887"/>
        <location line="+163"/>
        <source>Normal</source>
        <translation>通常</translation>
    </message>
    <message>
        <location line="-2049"/>
        <location line="+1886"/>
        <location line="+164"/>
        <source>High</source>
        <translation>高い</translation>
    </message>
    <message>
        <location line="-2048"/>
        <location line="+2050"/>
        <source>Very Low</source>
        <translation>非常に低い</translation>
    </message>
    <message>
        <location line="-2049"/>
        <location line="+2050"/>
        <source>Very High</source>
        <translation>非常に高い</translation>
    </message>
    <message>
        <location line="-2048"/>
        <location line="+2051"/>
        <source>Auto</source>
        <translation>自動</translation>
    </message>
    <message>
        <location line="-2040"/>
        <location line="+684"/>
        <location line="+1200"/>
        <source>Pause</source>
        <translation>一時停止</translation>
    </message>
    <message>
        <location line="-1875"/>
        <location line="+681"/>
        <location line="+1196"/>
        <source>Stop</source>
        <translation>停止</translation>
    </message>
    <message>
        <location line="-1868"/>
        <location line="+678"/>
        <location line="+1192"/>
        <source>Resume</source>
        <translation>再開</translation>
    </message>
    <message>
        <location line="-1857"/>
        <location line="+671"/>
        <location line="+1188"/>
        <location line="+4"/>
        <source>Cancel</source>
        <translation>キャンセル</translation>
    </message>
    <message>
        <location line="-1830"/>
        <source>Preview with</source>
        <translation>プレビューに使用</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Pause when preview is possible</source>
        <translation>プレビュー可能になったら一時停止</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Increase priority for preview parts</source>
        <translation>プレビュー用パートの優先度を上げる</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Stop Import parts to file</source>
        <translation>ファイルへのパートのインポートを停止</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+1264"/>
        <location line="+1"/>
        <location line="+10"/>
        <source>Import parts to file...</source>
        <translation>ファイルにパートをインポート...</translation>
    </message>
    <message>
        <location line="-1251"/>
        <source>Source Handling</source>
        <translation>ソース処理</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add Sources...</source>
        <translation>ソースを追加...</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+1187"/>
        <source>Source Limit...</source>
        <translation>ソース上限...</translation>
    </message>
    <message>
        <location line="-1124"/>
        <source>New...</source>
        <translation>新規...</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Show Toolbar</source>
        <translation>ツールバーを表示</translation>
    </message>
    <message>
        <location line="+430"/>
        <source>Close Toolbar</source>
        <translation>ツールバーを閉じる</translation>
    </message>
    <message>
        <location line="+627"/>
        <source>Failed to execute: %1 %2</source>
        <translation>実行に失敗しました: %1 %2</translation>
    </message>
    <message>
        <location line="+46"/>
        <source>Please enter the new source limit for the selected file(s)!
(set to 0 to use global setting)</source>
        <translation>選択したファイルの新しいソース上限を入力してください。
(0 でグローバル設定を使用)</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>cannot add the source.</source>
        <translation>ソースを追加できません。</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>The source %1 was not accepted.</source>
        <translation>ソース %1 は受け入れられませんでした。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>cannot import parts.</source>
        <translation>パートをインポートできません。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Path of the file on the core&apos;s machine:</source>
        <translation>コアのマシン上のファイルパス:</translation>
    </message>
    <message>
        <location line="+517"/>
        <source>Download In Alphabetical Order</source>
        <translation>アルファベット順にダウンロード</translation>
    </message>
    <message>
        <location line="+217"/>
        <location line="+91"/>
        <source>cannot view shared files.</source>
        <translation>共有ファイルを表示できません。</translation>
    </message>
    <message>
        <location line="-81"/>
        <source>Unban</source>
        <translation>BAN 解除</translation>
    </message>
    <message>
        <location line="+113"/>
        <source>Bootstrap</source>
        <translation>ブートストラップ</translation>
    </message>
    <message>
        <location line="+168"/>
        <source>Cancel Download</source>
        <translation>ダウンロードをキャンセル</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+31"/>
        <source>Cancel download &quot;%1&quot;?</source>
        <translation>ダウンロード「%1」をキャンセルしますか？</translation>
    </message>
    <message>
        <location line="-27"/>
        <location line="+29"/>
        <source>Cancel Downloads</source>
        <translation>複数のダウンロードをキャンセル</translation>
    </message>
    <message>
        <location line="-28"/>
        <location line="+27"/>
        <source>Cancel %1 selected downloads?</source>
        <translation>選択した %1 件のダウンロードをキャンセルしますか？</translation>
    </message>
    <message>
        <location line="+25"/>
        <location line="+12"/>
        <source>Rename</source>
        <translation>名前の変更</translation>
    </message>
    <message>
        <location line="-12"/>
        <source>File name:</source>
        <translation>ファイル名:</translation>
    </message>
    <message>
        <location line="-2427"/>
        <location line="+662"/>
        <source>Open File</source>
        <translation>ファイルを開く</translation>
    </message>
    <message>
        <location line="-653"/>
        <location line="+2"/>
        <location line="+665"/>
        <location line="+571"/>
        <source>Preview</source>
        <translation>プレビュー</translation>
    </message>
    <message>
        <location line="-1199"/>
        <location line="+1981"/>
        <location line="+98"/>
        <source>Details...</source>
        <translation>詳細...</translation>
    </message>
    <message>
        <location line="-2070"/>
        <source>Comments...</source>
        <translation>コメント...</translation>
    </message>
    <message>
        <location line="+37"/>
        <location line="+630"/>
        <source>Clear Completed</source>
        <translation>完了済みをクリア</translation>
    </message>
    <message>
        <location line="-608"/>
        <source>eD2K Links...</source>
        <translation>eD2K リンク...</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Paste eD2K Links</source>
        <translation>eD2K リンクを貼り付け</translation>
    </message>
    <message>
        <location line="+15"/>
        <location line="+1974"/>
        <location line="+71"/>
        <source>Find...</source>
        <translation>検索...</translation>
    </message>
    <message>
        <location line="-2037"/>
        <source>Search Related Files</source>
        <translation>関連ファイルを検索</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Web Services</source>
        <translation>Web サービス</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+541"/>
        <source>Assign To Category</source>
        <translation>カテゴリに割り当て</translation>
    </message>
    <message>
        <source>(All)</source>
        <translation type="vanished">(すべて)</translation>
    </message>
    <message>
        <source>All</source>
        <translation type="vanished">すべて</translation>
    </message>
    <message>
        <location line="+1062"/>
        <source>Uploading</source>
        <translation>アップロード中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloading</source>
        <translation>ダウンロード中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>On Queue</source>
        <translation>キュー待ち</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Known Clients</source>
        <translation>既知のクライアント</translation>
    </message>
    <message>
        <location line="-1175"/>
        <source>Clients on queue:   0</source>
        <translation>キュー内のクライアント：   0</translation>
    </message>
    <message>
        <location line="-422"/>
        <location line="+539"/>
        <source>(Unassign)</source>
        <translation>(割り当て解除)</translation>
    </message>
    <message>
        <location line="-80"/>
        <location line="+1189"/>
        <source>Priority</source>
        <translation>優先度</translation>
    </message>
    <message>
        <location line="-1152"/>
        <source>Open Folder</source>
        <translation>フォルダを開く</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Details</source>
        <translation>詳細</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Comments</source>
        <translation>コメント</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>eD2K Links</source>
        <translation>eD2K リンク</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Search Related</source>
        <translation>関連検索</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Find</source>
        <translation>検索</translation>
    </message>
    <message>
        <location line="+487"/>
        <location line="+23"/>
        <source>Preview not available — web server is not running or stream token not received.</source>
        <translation>プレビューは利用できません — Web サーバーが実行されていないか、ストリームトークンを受信していません。</translation>
    </message>
    <message>
        <location line="+127"/>
        <source>Open File not available — web server is not running or stream token not received.</source>
        <translation>ファイルを開く操作は利用できません — Web サーバーが実行されていないか、ストリームトークンを受信していません。</translation>
    </message>
    <message>
        <location line="+407"/>
        <source>Downloads (%1)</source>
        <translation>ダウンロード (%1)</translation>
    </message>
    <message>
        <location line="-11"/>
        <source>Uploading (%1)</source>
        <translation>アップロード中 (%1)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Downloading (%1)</source>
        <translation>ダウンロード中 (%1)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>On Queue (%1)</source>
        <translation>キュー待ち (%1)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Known Clients (%1)</source>
        <translation>既知のクライアント (%1)</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Clients on queue:   %1</source>
        <translation>キュー内のクライアント：   %1</translation>
    </message>
    <message>
        <source>Cat %1</source>
        <translation type="vanished">カテゴリ %1</translation>
    </message>
    <message>
        <source>Category</source>
        <translation type="obsolete">カテゴリ</translation>
    </message>
    <message>
        <location line="+50"/>
        <source>Are you sure you want to cancel every download in &quot;%1&quot;?</source>
        <translation>「%1」内のすべてのダウンロードをキャンセルしてもよろしいですか？</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Resume next file</source>
        <translation>次のファイルを再開</translation>
    </message>
    <message>
        <source>Open Incoming Folder</source>
        <translation type="obsolete">受信フォルダを開く</translation>
    </message>
    <message>
        <location line="+184"/>
        <location line="+97"/>
        <source>Add To Friends</source>
        <translation>フレンドに追加</translation>
    </message>
    <message>
        <location line="-75"/>
        <location line="+100"/>
        <source>Send Message</source>
        <translation>メッセージを送信</translation>
    </message>
    <message>
        <source>Message:</source>
        <translation type="vanished">メッセージ:</translation>
    </message>
    <message>
        <location line="-89"/>
        <location line="+96"/>
        <source>View Shared Files</source>
        <translation>共有ファイルを表示</translation>
    </message>
</context>
<context>
    <name>eMule::TrayMenuManager</name>
    <message>
        <location filename="../src/gui/app/TrayMenuManager.cpp" line="+86"/>
        <source>eMule Speed</source>
        <translation>eMule の速度</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Download:</source>
        <translation>ダウンロード:</translation>
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
        <translation>無制限</translation>
    </message>
    <message>
        <location line="-5"/>
        <source>Upload:</source>
        <translation>アップロード:</translation>
    </message>
    <message>
        <location line="+34"/>
        <source>Set Full Up/Down-Speed</source>
        <translation>アップ/ダウン速度を最大にする</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Throttle Up/Down-Speed</source>
        <translation>アップ/ダウン速度を制限する</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>Pause Usenet</source>
        <translation>Usenet を一時停止</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Connect</source>
        <translation>接続</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Disconnect</source>
        <translation>切断</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Options</source>
        <translation>オプション</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Restore</source>
        <translation>元に戻す</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Exit</source>
        <translation>終了</translation>
    </message>
</context>
<context>
    <name>eMule::UsenetArchiveEntryDialog</name>
    <message>
        <location filename="../src/gui/dialogs/UsenetArchiveEntryDialog.cpp" line="+59"/>
        <source>Preview File</source>
        <translation>プレビューするファイル</translation>
    </message>
    <message>
        <location line="+42"/>
        <source>Playable</source>
        <translation>再生可能</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>Nothing has arrived for this download yet.</source>
        <translation>このダウンロードはまだ何も届いていません。</translation>
    </message>
    <message numerus="yes">
        <location line="+3"/>
        <source>Reading the archive… %n file(s) found so far</source>
        <translation>
            <numerusform>アーカイブを読み込み中… これまでに %n 件のファイルを検出</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location line="+6"/>
        <source>%n file(s) in the archive.</source>
        <translation>
            <numerusform>アーカイブ内に %n 件のファイルがあります。</numerusform>
        </translation>
    </message>
    <message>
        <location line="+34"/>
        <source>Name</source>
        <translation>名前</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Size</source>
        <translation>サイズ</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Status</source>
        <translation>ステータス</translation>
    </message>
    <message>
        <location line="+25"/>
        <source>Reading the archive…</source>
        <translation>アーカイブを読み込み中…</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Preview</source>
        <translation>プレビュー</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Keep Scanning</source>
        <translation>スキャンを続ける</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>The connection to the core was lost.</source>
        <translation>コアへの接続が失われました。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>The archive is still being read — the files listed so far are all that is known.</source>
        <translation>アーカイブはまだ読み込み中です — ここに表示されているファイルが現時点で判明しているすべてです。</translation>
    </message>
</context>
<context>
    <name>eMule::UsenetDetailsDialog</name>
    <message>
        <location filename="../src/gui/dialogs/UsenetDetailsDialog.cpp" line="+15"/>
        <location line="+18"/>
        <source>Release Details</source>
        <translation>リリースの詳細</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Not checked.</source>
        <translation>未確認。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>%1% of this release looks obtainable.</source>
        <translation>このリリースの %1% は取得できそうです。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1% by the NZB&apos;s own article counts. No server was asked.</source>
        <translation>NZB 自体の記事数による %1%。サーバーには問い合わせていません。</translation>
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
            <numerusform>%n 件の記事が NZB に記載されていませんでした</numerusform>
        </translation>
    </message>
    <message>
        <location line="+58"/>
        <source>&quot;%1&quot; has not been published on its own.</source>
        <translation>「%1」は単独では公開されていません。</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>&quot;%1&quot; is outside the core&apos;s Incoming folder and cannot be opened from here.</source>
        <translation>「%1」はコアの受信フォルダの外にあるため、ここから開くことはできません。</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Total Size:</source>
        <translation>合計サイズ:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Date:</source>
        <translation>日付:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status:</source>
        <translation>ステータス：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Health:</source>
        <translation>健全性:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Poster:</source>
        <translation>投稿者:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Articles:</source>
        <translation>記事:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Newsgroups:</source>
        <translation>ニュースグループ:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Files:</source>
        <translation>ファイル:</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Name</source>
        <translation>名前</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Size</source>
        <translation>サイズ</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Progress</source>
        <translation>進捗</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Articles</source>
        <translation>記事</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Missing</source>
        <translation>欠落</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Status</source>
        <translation>ステータス</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Open Folder</source>
        <translation>フォルダを開く</translation>
    </message>
</context>
<context>
    <name>eMule::UsenetFileCheckList</name>
    <message>
        <location filename="../src/gui/controls/UsenetFileCheckList.cpp" line="+53"/>
        <source>Select All</source>
        <translation>すべて選択</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Select None</source>
        <translation>選択解除</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Invert Selection</source>
        <translation>選択を反転</translation>
    </message>
    <message>
        <location line="+73"/>
        <source>Could not change which files download.</source>
        <translation>ダウンロードするファイルを変更できませんでした。</translation>
    </message>
</context>
<context>
    <name>eMule::UsenetPanel</name>
    <message>
        <location filename="../src/gui/panels/UsenetPanel.cpp" line="+165"/>
        <location line="+7"/>
        <location line="+44"/>
        <location line="+498"/>
        <source>Add NZB</source>
        <translation>NZBを追加</translation>
    </message>
    <message>
        <location line="-548"/>
        <location line="+50"/>
        <location line="+62"/>
        <source>Not connected to the eMule core.</source>
        <translation>eMule コアに接続されていません。</translation>
    </message>
    <message>
        <location line="-105"/>
        <source>Cannot read %1.</source>
        <translation>%1 を読み取れません。</translation>
    </message>
    <message>
        <location line="+104"/>
        <source>Add NZB from URL</source>
        <translation>URLからNZBを追加</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Add NZB…</source>
        <translation>NZBを追加…</translation>
    </message>
    <message>
        <location line="+3"/>
        <location line="+964"/>
        <source>Pause</source>
        <translation>一時停止</translation>
    </message>
    <message>
        <location line="-960"/>
        <location line="+963"/>
        <source>Resume</source>
        <translation>再開</translation>
    </message>
    <message>
        <location line="-961"/>
        <source>Remove</source>
        <translation>削除</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Clear Completed</source>
        <translation>完了済みをクリア</translation>
    </message>
    <message>
        <location line="+6"/>
        <location line="+912"/>
        <source>Pause All</source>
        <translation>すべて一時停止</translation>
    </message>
    <message>
        <location line="-905"/>
        <source>Add NZB from URL…</source>
        <translation>URLからNZBを追加…</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Preview</source>
        <translation>プレビュー</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Check Availability</source>
        <translation>可用性を確認</translation>
    </message>
    <message>
        <location line="+269"/>
        <source>Priority</source>
        <translation>優先度</translation>
    </message>
    <message>
        <source>High</source>
        <translation type="obsolete">高い</translation>
    </message>
    <message>
        <source>Normal</source>
        <translation type="obsolete">通常</translation>
    </message>
    <message>
        <source>Low</source>
        <translation type="obsolete">低い</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Assign To Category</source>
        <translation>カテゴリに割り当て</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>No category</source>
        <translation>カテゴリなし</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Set Password…</source>
        <translation>パスワードを設定…</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Download Selected Files</source>
        <translation>選択したファイルをダウンロード</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Skip Selected Files</source>
        <translation>選択したファイルをスキップ</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Open File</source>
        <translation>ファイルを開く</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Open Folder</source>
        <translation>フォルダを開く</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Details…</source>
        <translation>詳細…</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Remove and Delete Files</source>
        <translation>削除してファイルも消去</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>NZB files (*.nzb);;All files (*)</source>
        <translation>NZB ファイル (*.nzb);;すべてのファイル (*)</translation>
    </message>
    <message>
        <location line="+34"/>
        <location line="+15"/>
        <source>Set Password</source>
        <translation>パスワードを設定</translation>
    </message>
    <message>
        <location line="-14"/>
        <source>Archive password for &quot;%1&quot;:</source>
        <translation>「%1」のアーカイブパスワード:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>this download</source>
        <translation>このダウンロード</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Remove the stored password for this download?</source>
        <translation>このダウンロードに保存されたパスワードを削除しますか?</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Could not set the password.</source>
        <translation>パスワードを設定できませんでした。</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Remove Downloads</source>
        <translation>ダウンロードを削除</translation>
    </message>
    <message numerus="yes">
        <location line="+1"/>
        <source>Remove %n download(s) and delete the files already fetched?</source>
        <translation>
            <numerusform>%n 件のダウンロードを削除し、取得済みのファイルも消去しますか?</numerusform>
        </translation>
    </message>
    <message>
        <location line="+63"/>
        <location line="+82"/>
        <source>Nothing has completed yet for &quot;%1&quot;.</source>
        <translation>「%1」はまだ何も完了していません。</translation>
    </message>
    <message>
        <location line="+100"/>
        <source>Nothing here can be previewed yet.</source>
        <translation>まだプレビューできるものはありません。</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Preview is unavailable — the daemon&apos;s web server is not running.</source>
        <translation>プレビューは利用できません — デーモンの Web サーバーが実行されていません。</translation>
    </message>
    <message>
        <location line="+58"/>
        <source>No Usenet downloads. Use &quot;Add NZB…&quot; to queue one.</source>
        <translation>Usenet のダウンロードはありません。「NZBを追加…」でキューに追加してください。</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>%1 download(s), %2 active — %3% complete</source>
        <translation>ダウンロード %1 件、アクティブ %2 件 — %3% 完了</translation>
    </message>
    <message>
        <location line="+9"/>
        <source> — %1</source>
        <translation> — %1</translation>
    </message>
    <message>
        <location line="+7"/>
        <source> — limited to %1 KB/s while eD2K downloads</source>
        <translation> — eD2K のダウンロード中は %1 KB/s に制限</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Download limit %1 KB/s: Usenet up to %2 KB/s, eD2K up to %3 KB/s.
Whichever network is idle lends its share to the other.</source>
        <translation>ダウンロード制限 %1 KB/s: Usenet は最大 %2 KB/s、eD2K は最大 %3 KB/s。
アイドル状態のネットワークは自分の割り当てをもう一方に譲ります。</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Usenet: %1</source>
        <translation>Usenet: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Usenet: downloading again.</source>
        <translation>Usenet: ダウンロードを再開しました。</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Resume All</source>
        <translation>すべて再開</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Let every Usenet download continue</source>
        <translation>すべての Usenet ダウンロードを継続します</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Stop starting new Usenet articles. Nothing is removed, and each release keeps its own state.</source>
        <translation>新しい Usenet 記事の取得開始を停止します。何も削除されず、各リリースは自身の状態を保ちます。</translation>
    </message>
    <message>
        <location line="+42"/>
        <location line="+4"/>
        <source>Cancel</source>
        <translation>キャンセル</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove every Usenet download in &quot;%1&quot; and delete its files?</source>
        <translation>「%1」内のすべての Usenet ダウンロードを削除し、ファイルも消去しますか?</translation>
    </message>
    <message>
        <location line="+40"/>
        <source>Could not apply that to the category: %1</source>
        <translation>カテゴリに適用できませんでした: %1</translation>
    </message>
</context>
<context>
    <name>eMule::UsenetQueueModel</name>
    <message numerus="yes">
        <source>%n article(s) missing</source>
        <translation type="vanished">
            <numerusform>%n 件の記事が欠落</numerusform>
        </translation>
    </message>
    <message>
        <source>Complete</source>
        <translation type="vanished">完了</translation>
    </message>
    <message numerus="yes">
        <location filename="../src/gui/controls/UsenetQueueModel.cpp" line="+114"/>
        <source>%1% — %n article(s) missing</source>
        <translation>
            <numerusform>%1% — %n 件の記事が欠落</numerusform>
        </translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Skipped — tick it to download this file</source>
        <translation>スキップ — このファイルをダウンロードするにはチェックを入れてください</translation>
    </message>
    <message>
        <location line="+28"/>
        <source>%1% — %2 of %3 articles</source>
        <translation>%1% — %3 件中 %2 件の記事</translation>
    </message>
    <message numerus="yes">
        <location line="+4"/>
        <source>, %n missing</source>
        <translation>
            <numerusform>、%n 件欠落</numerusform>
        </translation>
    </message>
    <message>
        <location line="+84"/>
        <source>Not checked.</source>
        <translation>未確認。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1% of this release looks obtainable.</source>
        <translation>このリリースの %1% は取得できそうです。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1% by the NZB&apos;s own article counts. No server was asked.</source>
        <translation>NZB 自体の記事数による %1%。サーバーには問い合わせていません。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>The PAR2 recovery volumes should cover the shortfall.</source>
        <translation>PAR2 リカバリーボリュームで不足分を補えるはずです。</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>%1
The password for this release did not work. Right-click to set a different one.</source>
        <translation>%1
このリリースのパスワードは正しくありませんでした。右クリックして別のパスワードを設定してください。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1
This release is password-protected. Right-click to set its password.</source>
        <translation>%1
このリリースはパスワードで保護されています。右クリックしてパスワードを設定してください。</translation>
    </message>
    <message numerus="yes">
        <location line="+4"/>
        <source>%1
%n article(s) could not be found on any server</source>
        <translation>
            <numerusform>%1
%n 件の記事がどのサーバーにも見つかりませんでした</numerusform>
        </translation>
    </message>
    <message>
        <location line="+4"/>
        <source>%1
A password is set for this release.</source>
        <translation>%1
このリリースにはパスワードが設定されています。</translation>
    </message>
    <message>
        <location line="+77"/>
        <source>Name</source>
        <translation>名前</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Size</source>
        <translation>サイズ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Progress</source>
        <translation>進捗</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status</source>
        <translation>ステータス</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Speed</source>
        <translation>速度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remaining</source>
        <translation>残り</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority</source>
        <translation>優先度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Health</source>
        <translation>健全性</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Category</source>
        <translation>カテゴリ</translation>
    </message>
</context>
<context>
    <name>eMule::VersionChecker</name>
    <message>
        <location filename="../src/gui/app/VersionChecker.cpp" line="+100"/>
        <source>the version manifest is not a JSON object</source>
        <translation>バージョンマニフェストが JSON オブジェクトではありません</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>invalid JSON response: %1</source>
        <translation>無効な JSON 応答: %1</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>the version manifest has no &apos;latest&apos; field</source>
        <translation>バージョンマニフェストに &apos;latest&apos; フィールドがありません</translation>
    </message>
</context>
<context>
    <name>eMule::WebServer</name>
    <message>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+15"/>
        <source>(still scanning)</source>
        <translation>（スキャン中）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Accepted</source>
        <translation>承認</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add</source>
        <translation>追加</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add NZB from URL…</source>
        <translation>URLからNZBを追加…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add NZB…</source>
        <translation>NZBを追加…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add paused</source>
        <translation>一時停止状態で追加</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add to Static</source>
        <translation>静的リストに追加</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Adding %1 NZB(s)…</source>
        <translation>%1 個のNZBを追加中…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Address</source>
        <translation>アドレス</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebServer.cpp" line="-3350"/>
        <location line="+3239"/>
        <location line="+49"/>
        <source>Session expired — log in again</source>
        <translation>セッションの有効期限が切れました — 再度ログインしてください</translation>
    </message>
    <message>
        <location line="-3284"/>
        <source>Guests cannot add downloads</source>
        <translation>ゲストはダウンロードを追加できません</translation>
    </message>
    <message>
        <location line="+590"/>
        <source>Looking for comments on Kad</source>
        <translation>Kad でコメントを検索中</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Rating: %1</source>
        <translation>評価: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Has comments</source>
        <translation>コメントあり</translation>
    </message>
    <message>
        <location line="+403"/>
        <location line="+1"/>
        <source>Incoming</source>
        <translation>受信フォルダ</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Nothing has finished downloading yet.</source>
        <translation>まだ完了したダウンロードはありません。</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Modified</source>
        <translation>更新日時</translation>
    </message>
    <message>
        <location line="+40"/>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+36"/>
        <source>Download</source>
        <translation>ダウンロード</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Play</source>
        <translation>再生</translation>
    </message>
    <message>
        <location line="+91"/>
        <source>Open this URL in VLC or another player:</source>
        <translation>この URL を VLC などのプレーヤーで開いてください:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>This file is named %1 but its contents are %2. The name is wrong — common for files off the ed2k network — so a player that trusts it finds no %3 and sits at 0:00. It is being served as its real type, so it may still play above.</source>
        <translation>このファイルの名前は %1 ですが、中身は %2 です。名前が間違っています（ed2k ネットワークのファイルではよくあります）。そのため名前を信じるプレーヤーは %3 を見つけられず 0:00 のまま止まります。実際の形式で配信しているので、上で再生できる可能性があります。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>This file is named %1 but does not start with the %2 signature every one of them has, and its contents match no media container we recognise. It is very likely a fake or a corrupt download — no player will get anything out of it.</source>
        <translation>このファイルの名前は %1 ですが、この形式のファイルが必ず持つ %2 シグネチャで始まっておらず、内容も既知のメディアコンテナに一致しません。偽物か破損したダウンロードの可能性が非常に高く、どのプレーヤーでも再生できません。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>The raw URL, if you want to look for yourself:</source>
        <translation>自分で確認したい場合の直接 URL:</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Your browser probably cannot decode %1.</source>
        <translation>お使いのブラウザーは %1 をデコードできない可能性があります。</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Copy</source>
        <translation>コピー</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Copied</source>
        <translation>コピーしました</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Download this file</source>
        <translation>このファイルをダウンロード</translation>
    </message>
    <message>
        <location line="+62"/>
        <source>Access denied — no password configured. Set a password in Options → Web Interface.</source>
        <translation>アクセスが拒否されました — パスワードが設定されていません。オプション → Web インターフェースでパスワードを設定してください。</translation>
    </message>
    <message numerus="yes">
        <location line="+11"/>
        <source>Too many failed logins. Try again in %n second(s).</source>
        <translation>
            <numerusform>ログインの失敗が多すぎます。%n 秒後にもう一度お試しください。</numerusform>
        </translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Login failed</source>
        <translation>ログインに失敗しました</translation>
    </message>
    <message numerus="yes">
        <location line="+226"/>
        <source>%n download(s) added</source>
        <translation>
            <numerusform>%n 件のダウンロードを追加しました</numerusform>
        </translation>
    </message>
    <message>
        <source>Not connected — the search could not be sent.</source>
        <translation type="vanished">未接続 — 検索を送信できませんでした。</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Not sent yet — the search is waiting (%1) and starts by itself.</source>
        <translation>未送信 — 検索は待機中 (%1) で、自動的に開始されます。</translation>
    </message>
    <message>
        <location line="+27"/>
        <source>Nothing was saved: a value is missing or out of range.</source>
        <translation>保存されませんでした: 値が未入力か、範囲外です。</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Saved. The new ports are used after a restart.</source>
        <translation>保存しました。新しいポートは再起動後に使用されます。</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Saved.</source>
        <translation>保存しました。</translation>
    </message>
    <message>
        <location line="+13"/>
        <location line="+1970"/>
        <source>Web Control Panel</source>
        <translation>Web コントロールパネル</translation>
    </message>
    <message>
        <location line="-1962"/>
        <source>Not connected</source>
        <translation>未接続</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Blocked: network interface not available</source>
        <translation>ブロック中: ネットワークインターフェースが利用できません</translation>
    </message>
    <message>
        <location line="+299"/>
        <source>Connected</source>
        <translation>接続済み</translation>
    </message>
    <message>
        <location line="+0"/>
        <location line="+332"/>
        <source>Disconnected</source>
        <translation>未接続</translation>
    </message>
    <message numerus="yes">
        <location line="-279"/>
        <source>&quot;%1&quot;: %n result(s)</source>
        <translation>
            <numerusform>「%1」: %n 件の結果</numerusform>
        </translation>
    </message>
    <message>
        <location line="+192"/>
        <source>Active Connections</source>
        <translation>アクティブ接続</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Time</source>
        <translation>時間</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Connected to: %1 (%2:%3)</source>
        <translation>接続先: %1 (%2:%3)</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Client ID: %1 (%2)</source>
        <translation>クライアント ID: %1 (%2)</translation>
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
        <translation>ユーザー：%1 | ファイル：%2</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Description: %1</source>
        <translation>説明: %1</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Ping: %1 ms</source>
        <translation>Ping: %1 ms</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Connecting...</source>
        <translation>接続中...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Not connected to any server</source>
        <translation>どのサーバーにも接続していません</translation>
    </message>
    <message>
        <location line="+33"/>
        <source>Running</source>
        <translation>実行中</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Not available</source>
        <translation>利用できません</translation>
    </message>
    <message>
        <location line="+149"/>
        <location line="+1128"/>
        <source>Queued</source>
        <translation>キュー待ち</translation>
    </message>
    <message>
        <location line="-1127"/>
        <location line="+1126"/>
        <source>Downloading</source>
        <translation>ダウンロード中</translation>
    </message>
    <message>
        <location line="-1125"/>
        <source>Paused</source>
        <translation>一時停止</translation>
    </message>
    <message>
        <location line="+1"/>
        <location line="+960"/>
        <location line="+163"/>
        <source>Complete</source>
        <translation>完了</translation>
    </message>
    <message>
        <location line="-1122"/>
        <source>Failed</source>
        <translation>失敗</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Verifying</source>
        <translation>検証中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Repairing</source>
        <translation>修復中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unpacking</source>
        <translation>展開中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Checking</source>
        <translation>確認中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unknown</source>
        <translation>不明</translation>
    </message>
    <message>
        <location line="+30"/>
        <source>Not checked.</source>
        <translation>未確認。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1% of this release looks obtainable.</source>
        <translation>このリリースの %1% は取得できそうです。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>%1% by the NZB&apos;s own article counts. No server was asked.</source>
        <translation>NZB 自体の記事数による %1%。サーバーには問い合わせていません。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>The PAR2 recovery volumes should cover the shortfall.</source>
        <translation>PAR2 リカバリーボリュームで不足分を補えるはずです。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>%1
The password for this release did not work. Right-click to set a different one.</source>
        <translation>%1
このリリースのパスワードは正しくありませんでした。右クリックして別のパスワードを設定してください。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1
This release is password-protected. Right-click to set its password.</source>
        <translation>%1
このリリースはパスワードで保護されています。右クリックしてパスワードを設定してください。</translation>
    </message>
    <message numerus="yes">
        <location line="+4"/>
        <source>%1
%n article(s) could not be found on any server</source>
        <translation>
            <numerusform>%1
%n 件の記事がどのサーバーにも見つかりませんでした</numerusform>
        </translation>
    </message>
    <message>
        <location line="+4"/>
        <source>%1
A password is set for this release.</source>
        <translation>%1
このリリースにはパスワードが設定されています。</translation>
    </message>
    <message>
        <location line="+126"/>
        <source>%1 (and %2 other(s))</source>
        <translation>%1（他 %2 件）</translation>
    </message>
    <message>
        <location line="+60"/>
        <source>Usenet item not found</source>
        <translation>Usenet の項目が見つかりません</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Very high</source>
        <translation>非常に高い</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>High</source>
        <translation>高い</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Low</source>
        <translation>低い</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Very low</source>
        <translation>非常に低い</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Normal</source>
        <translation>通常</translation>
    </message>
    <message>
        <location line="+188"/>
        <source>Only a queued or downloading release can be paused</source>
        <translation>一時停止できるのは待機中またはダウンロード中のリリースだけです</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Only a paused or failed release can be resumed</source>
        <translation>再開できるのは一時停止中または失敗したリリースだけです</translation>
    </message>
    <message>
        <location line="+11"/>
        <location line="+110"/>
        <location line="+174"/>
        <source>priority must be a number from -2 to 2</source>
        <translation>優先度は -2 から 2 の数値で指定してください</translation>
    </message>
    <message>
        <location line="-278"/>
        <location line="+110"/>
        <location line="+71"/>
        <location line="+90"/>
        <source>Unknown category</source>
        <translation>不明なカテゴリ</translation>
    </message>
    <message>
        <location line="-259"/>
        <location line="+4"/>
        <source>files must be a list of file numbers</source>
        <translation>files はファイル番号のリストで指定してください</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Unknown action</source>
        <translation>不明な操作</translation>
    </message>
    <message>
        <location line="+228"/>
        <source>Post the .nzb as the request body, or give a url</source>
        <translation>.nzb をリクエスト本文として送信するか、URL を指定してください</translation>
    </message>
    <message>
        <location line="+57"/>
        <source>Guests cannot change downloads</source>
        <translation>ゲストはダウンロードを変更できません</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Nothing selected</source>
        <translation>何も選択されていません</translation>
    </message>
    <message>
        <location line="+49"/>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="-35"/>
        <source>All</source>
        <translation>すべて</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>No category</source>
        <translation>カテゴリなし</translation>
    </message>
    <message numerus="yes">
        <location line="+114"/>
        <location line="+163"/>
        <source>%n article(s) missing</source>
        <translation>
            <numerusform>%n 件の記事が欠落</numerusform>
        </translation>
    </message>
    <message>
        <location line="-99"/>
        <source>No Usenet downloads. Use &quot;Add NZB…&quot; to queue one.</source>
        <translation>Usenet のダウンロードはありません。「NZBを追加…」でキューに追加してください。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>%1 download(s), %2 active — %3% complete</source>
        <translation>ダウンロード %1 件、アクティブ %2 件 — %3% 完了</translation>
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
        <translation> — eD2K のダウンロード中は %1 KB/s に制限</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Download limit %1 KB/s: Usenet up to %2 KB/s, eD2K up to %3 KB/s.
Whichever network is idle lends its share to the other.</source>
        <translation>ダウンロード制限 %1 KB/s: Usenet は最大 %2 KB/s、eD2K は最大 %3 KB/s。
アイドル状態のネットワークは自分の割り当てをもう一方に譲ります。</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>No Usenet downloads here.</source>
        <translation>ここには Usenet のダウンロードはありません。</translation>
    </message>
    <message numerus="yes">
        <location line="+48"/>
        <source>%n article(s) were never listed in the NZB</source>
        <translation>
            <numerusform>%n 件の記事が NZB に記載されていませんでした</numerusform>
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
        <translation>アプリの言語 (%1)</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>Any</source>
        <translation>すべて</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Apply</source>
        <translation>適用</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Archive (.zip .rar ...)</source>
        <translation>アーカイブ (.zip .rar ...)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Archive password for &quot;%1&quot;:</source>
        <translation>「%1」のアーカイブパスワード:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Articles</source>
        <translation>記事</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Articles:</source>
        <translation>記事:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Audio (.mp3 .ogg ...)</source>
        <translation>オーディオ (.mp3 .ogg ...)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Back to the queue</source>
        <translation>キューに戻る</translation>
    </message>
    <message>
        <source>CD Image (.iso .bin ...)</source>
        <translation type="vanished">CD イメージ (.iso .bin ...)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Cancel</source>
        <translation>キャンセル</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Cancel this download?</source>
        <translation>このダウンロードをキャンセルしますか?</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Category</source>
        <translation>カテゴリ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Category:</source>
        <translation>カテゴリ:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Category: %1</source>
        <translation>カテゴリ: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Check Availability</source>
        <translation>可用性を確認</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Choose .nzb files or paste links first.</source>
        <translation>先に .nzb ファイルを選ぶか、リンクを貼り付けてください。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Clear Completed</source>
        <translation>完了済みをクリア</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Client</source>
        <translation>クライアント</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Comment</source>
        <translation>コメント</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Comments</source>
        <translation>コメント</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Completed</source>
        <translation>完了</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Confidence</source>
        <translation>信頼度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Connect</source>
        <translation>接続</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Copy ED2K Link</source>
        <translation>ED2K リンクをコピー</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Could not apply that to the category: %1</source>
        <translation>カテゴリに適用できませんでした: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Could not reach the eMule core.</source>
        <translation>eMule コアに接続できませんでした。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Date:</source>
        <translation>日付:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Debug</source>
        <translation>デバッグ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Debug Log</source>
        <translation>デバッグログ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Description</source>
        <translation>説明</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Details…</source>
        <translation>詳細…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Disconnect</source>
        <translation>切断</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Document (.doc .pdf ...)</source>
        <translation>ドキュメント (.doc .pdf ...)</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Download Speed</source>
        <translation>ダウンロード速度</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebServer.cpp" line="-1453"/>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>Downloads</source>
        <translation>ダウンロード</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>File</source>
        <translation>ファイル</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>File Name</source>
        <translation>ファイル名</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Files</source>
        <translation>ファイル</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Files:</source>
        <translation>ファイル:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>General</source>
        <translation>全般</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Global</source>
        <translation>グローバル</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Graphs</source>
        <translation>グラフ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hash</source>
        <translation>ハッシュ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Health</source>
        <translation>健全性</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Health:</source>
        <translation>健全性:</translation>
    </message>
    <message>
        <source>Image (.jpg .png ...)</source>
        <translation type="vanished">画像 (.jpg .png ...)</translation>
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
        <translation>Kad ネットワーク</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Kademlia</source>
        <translation>Kademlia</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Keep Scanning</source>
        <translation>スキャンを続ける</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Language</source>
        <translation>言語</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Log</source>
        <translation>ログ</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Login</source>
        <translation>ログイン</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Logout</source>
        <translation>ログアウト</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Max Download (KB/s)</source>
        <translation>最大ダウンロード (KB/s)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Max Download Speed</source>
        <translation>最大ダウンロード速度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Max Upload (KB/s)</source>
        <translation>最大アップロード (KB/s)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Max Upload Speed</source>
        <translation>最大アップロード速度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Method</source>
        <translation>方式</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Missing</source>
        <translation>欠落</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>My Info</source>
        <translation>マイ情報</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>NZB URLs:</source>
        <translation>NZB の URL:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>NZB files:</source>
        <translation>NZB ファイル:</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebServer.cpp" line="-1168"/>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>Name</source>
        <translation>名前</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="-57"/>
        <source>CD-Image (.iso .bin ...)</source>
        <translation>CD イメージ (.iso .bin ...)</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Collection</source>
        <translation>コレクション</translation>
    </message>
    <message>
        <location line="+48"/>
        <source>Newsgroups:</source>
        <translation>ニュースグループ:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nickname</source>
        <translation>ニックネーム</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nickname:</source>
        <translation>ニックネーム:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nothing has completed yet for &quot;%1&quot;.</source>
        <translation>「%1」はまだ何も完了していません。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Nothing here can be previewed yet.</source>
        <translation>まだプレビューできるものはありません。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Open File</source>
        <translation>ファイルを開く</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Open Folder</source>
        <translation>フォルダを開く</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Password</source>
        <translation>パスワード</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Password:</source>
        <translation>パスワード：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Paste one or more http(s) links to .nzb files here, one per line...</source>
        <translation>.nzb ファイルへの http(s) リンクを 1 行に 1 つずつここに貼り付けてください...</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Pause</source>
        <translation>一時停止</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Pause All</source>
        <translation>すべて一時停止</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Picture (.jpg .png ...)</source>
        <translation>画像 (.jpg .png ...)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ping</source>
        <translation>Ping</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Playable</source>
        <translation>再生可能</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Please add at most %1 links at a time.</source>
        <translation>一度に追加できるリンクは %1 件までです。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Port</source>
        <translation>ポート</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Ports</source>
        <translation>ポート</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Poster:</source>
        <translation>投稿者:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Preferences</source>
        <translation>設定</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Preview</source>
        <translation>プレビュー</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Preview File</source>
        <translation>プレビューするファイル</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority</source>
        <translation>優先度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority:</source>
        <translation>優先度:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority: %1</source>
        <translation>優先度: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority: Auto</source>
        <translation>優先度: 自動</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority: High</source>
        <translation>優先度: 高</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority: Low</source>
        <translation>優先度: 低</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Priority: Normal</source>
        <translation>優先度: 普通</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Program (.exe ...)</source>
        <translation>プログラム (.exe ...)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Progress</source>
        <translation>進捗</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Queued %1 NZB(s).</source>
        <translation>%1 個のNZBをキューに追加しました。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Rating</source>
        <translation>評価</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Recheck Firewall</source>
        <translation>ファイアウォールを再チェック</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Reconnects</source>
        <translation>再接続</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Refresh</source>
        <translation>更新</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remaining</source>
        <translation>残り</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove</source>
        <translation>削除</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove %1 download(s) and delete the files already fetched?</source>
        <translation>%1 件のダウンロードを削除し、取得済みのファイルも削除しますか?</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove and Delete Files</source>
        <translation>削除してファイルも消去</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove every Usenet download in &quot;%1&quot; and delete its files?</source>
        <translation>「%1」内のすべての Usenet ダウンロードを削除し、ファイルも消去しますか?</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove from Static</source>
        <translation>静的リストから削除</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove the stored password for this download?</source>
        <translation>このダウンロードに保存されたパスワードを削除しますか?</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Requests</source>
        <translation>リクエスト</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Resume</source>
        <translation>再開</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Resume All</source>
        <translation>すべて再開</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Search</source>
        <translation>検索</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server</source>
        <translation>サーバー</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server Info</source>
        <translation>サーバー情報</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server List</source>
        <translation>サーバーリスト</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Server Name</source>
        <translation>サーバー名</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Session Received</source>
        <translation>受信量（セッション）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Session Sent</source>
        <translation>送信量（セッション）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Session Statistics</source>
        <translation>セッション統計</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Set Password…</source>
        <translation>パスワードを設定…</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Shared</source>
        <translation>共有済み</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Shared Files</source>
        <translation>共有ファイル</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebServer.cpp" line="+0"/>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>Size</source>
        <translation>サイズ</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>Sources</source>
        <translation>ソース</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Speed</source>
        <translation>速度</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Speed Limits</source>
        <translation>速度制限</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Statistics</source>
        <translation>統計</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status</source>
        <translation>ステータス</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Status:</source>
        <translation>ステータス：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Stop</source>
        <translation>停止</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>TCP Port</source>
        <translation>TCP ポート</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>That did not work.</source>
        <translation>うまくいきませんでした。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>These could not be added: %1</source>
        <translation>次のものは追加できませんでした: %1</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>This archive cannot be previewed.</source>
        <translation>このアーカイブはプレビューできません。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>This download is no longer in the queue.</source>
        <translation>このダウンロードはもうキューにありません。</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total</source>
        <translation>合計</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Total Size:</source>
        <translation>合計サイズ:</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Transfer</source>
        <translation>転送</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Transferred</source>
        <translation>転送済み</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Type</source>
        <translation>種類</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>UDP Port</source>
        <translation>UDP ポート</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Upload Speed</source>
        <translation>アップロード速度</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebServer.cpp" line="+1165"/>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>Uploads</source>
        <translation>アップロード</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>Uptime</source>
        <translation>稼働時間</translation>
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
        <translation>Usenet エンジンを利用できません</translation>
    </message>
    <message>
        <location filename="../src/core/webserver/WebTemplateStrings.h" line="+1"/>
        <source>User</source>
        <translation>ユーザー</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>User Information</source>
        <translation>ユーザー情報</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>User Name</source>
        <translation>ユーザー名</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Users</source>
        <translation>ユーザー</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Video (.avi .mkv ...)</source>
        <translation>動画 (.avi .mkv ...)</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>You have already downloaded %1 of these. Download them again?</source>
        <translation>このうち %1 件はすでにダウンロード済みです。もう一度ダウンロードしますか?</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>this download</source>
        <translation>このダウンロード</translation>
    </message>
</context>
</TS>
