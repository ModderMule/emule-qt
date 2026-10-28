# Builds the ed3k hoster resolver on Windows; see build-hoster-resolver.sh, which this follows.
#
#   build-hoster-resolver.ps1 [-JdDir <dir holding JDownloader, AppWorkUtils, JDBrowser, MyJDownloaderClient>] [-OutDir <out>]
#
# Needs a JDK 17 or newer (JAVA_HOME or javac on PATH). Output: <out>\resolver\ and <out>\resolver.zip.
param(
    [string]$JdDir = '',
    [string]$OutDir = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\hoster-resolver')
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
# JD_DIR: environment, else the project .env, else C:\Coding\Java
if (-not $JdDir) {
    $JdDir = $env:JD_DIR
    $envFile = Join-Path $root '.env'
    if (-not $JdDir -and (Test-Path $envFile)) {
        $line = Get-Content $envFile | Where-Object { $_ -match '^\s*JD_DIR\s*=' } | Select-Object -Last 1
        if ($line) { $JdDir = ($line -replace '^\s*JD_DIR\s*=\s*', '').Trim().Trim('"', "'") }
    }
    if (-not $JdDir) { $JdDir = 'C:\Coding\Java' }
}

foreach ($p in 'JDownloader', 'AppWorkUtils', 'JDBrowser', 'MyJDownloaderClient') {
    if (-not (Test-Path (Join-Path $JdDir "$p\src"))) { throw "build-hoster-resolver: $JdDir\$p\src missing" }
}
$javac = if ($env:JAVA_HOME) { Join-Path $env:JAVA_HOME 'bin\javac.exe' } else { (Get-Command javac).Source }
if (-not (Test-Path $javac)) { throw 'build-hoster-resolver: need a JDK 17 or newer (set JAVA_HOME)' }

$work = Join-Path $OutDir 'work'
$res = Join-Path $OutDir 'resolver'
Remove-Item -Recurse -Force $work, $res, (Join-Path $OutDir 'resolver.zip') -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $work, (Join-Path $res 'classes'), (Join-Path $res 'libs') | Out-Null

$src = Join-Path $JdDir 'JDownloader\src'
$jarDirs = 'JDownloader\ressourcen\libs', 'JDownloader\ressourcen\libs_ext', 'AppWorkUtils\libs', 'AppWorkUtils\dev_libs', 'JDBrowser\libs', 'MyJDownloaderClient\libs'
$jars = foreach ($d in $jarDirs) {
    $full = Join-Path $JdDir $d
    if (Test-Path $full) { Get-ChildItem $full -Recurse -Filter *.jar | Where-Object { $_.Name -notlike '*-sources.jar' } }
}
$cp = ($jars | ForEach-Object FullName) -join ';'

$files = @(Get-ChildItem (Join-Path $src 'jd\plugins\hoster'), (Join-Path $src 'jd\plugins\decrypter') -Recurse -Filter *.java | ForEach-Object FullName)
foreach ($c in 'jd\SecondLevelLaunch', 'jd\controlling\downloadcontroller\SingleDownloadController', 'jd\controlling\linkcrawler\LinkCrawler',
               'jd\controlling\linkchecker\LinkCheckerThread', 'jd\controlling\accountchecker\AccountCheckerThread',
               'org\jdownloader\plugins\controller\host\HostPluginController', 'org\jdownloader\plugins\controller\crawler\CrawlerPluginController',
               'org\jdownloader\captcha\v2\ChallengeResponseController', 'org\jdownloader\net\AutoBCSSLSocketStreamFactory') {
    $files += Join-Path $src "$c.java"
}
$files += Get-ChildItem (Join-Path $root 'tools\hoster-resolver\src') -Recurse -Filter *.java | ForEach-Object FullName
$files | ForEach-Object { '"' + ($_ -replace '\\', '/') + '"' } | Set-Content -Encoding ASCII (Join-Path $work 'files.txt')

$rev = try { (& svn info --show-item revision (Join-Path $JdDir 'JDownloader')).Trim() } catch { '?' }
Write-Host "== compiling $($files.Count) sources, JDownloader r$rev"
$sp = @($src, (Join-Path $JdDir 'AppWorkUtils\src'), (Join-Path $JdDir 'JDBrowser\src'), (Join-Path $JdDir 'MyJDownloaderClient\src')) -join ';'
& $javac -J-Xmx8g --release 17 -nowarn -proc:none -encoding UTF-8 -Xmaxerrs 200 -implicit:class -sourcepath $sp -cp $cp `
    -d (Join-Path $res 'classes') "@$(Join-Path $work 'files.txt')" *> (Join-Path $work 'compile.log')
if ($LASTEXITCODE -ne 0) { Get-Content (Join-Path $work 'compile.log') -Tail 40; throw 'build-hoster-resolver: compile failed' }

$dropped = '^(synthetica|flatlaf|svnkit|sqljet|sequence-library|antlr-runtime|irclib|jsyntaxpane|sevenzipjbinding|zip4j|AppleJavaExtensions|libdbus|unix\.jar|hexdump\.jar|debug-disable\.jar)'
foreach ($j in $jars) { if ($j.Name -notmatch $dropped) { Copy-Item $j.FullName (Join-Path $res 'libs') -Force } }
foreach ($s in $src, (Join-Path $JdDir 'AppWorkUtils\src'), (Join-Path $JdDir 'JDBrowser\src')) {
    Get-ChildItem $s -Recurse -Include *.json, *.properties, *.lng, *.dat | Where-Object { $_.FullName -notmatch '\\\.svn\\' } | ForEach-Object {
        $rel = $_.FullName.Substring($s.Length + 1)
        $dst = Join-Path (Join-Path $res 'classes') $rel
        New-Item -ItemType Directory -Force -Path (Split-Path $dst) | Out-Null
        Copy-Item $_.FullName $dst -Force
    }
}
Set-Content -Encoding ASCII (Join-Path $res 'revision.txt') $rev
Copy-Item (Join-Path $root 'tools\hoster-resolver\LICENSE') (Join-Path $res 'LICENSE') -Force
# tar.exe (bsdtar, Windows 10+): forward slashes in the zip, which Compress-Archive of PowerShell 5 does not write
Push-Location $OutDir
try { & tar.exe -a -cf resolver.zip resolver; if ($LASTEXITCODE -ne 0) { throw 'build-hoster-resolver: zip failed' } }
finally { Pop-Location }
Write-Host "== $OutDir\resolver.zip"
