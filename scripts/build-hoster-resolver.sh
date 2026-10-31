#!/bin/sh
# Builds the ed3k hoster resolver: JDownloader's plugins + core, unmodified, and the driver in
# tools/hoster-resolver. See docs/hoster.md and docs/ed3k-spec/docs/hoster-resolver.local.md.
#
#   build-hoster-resolver.sh [<dir holding JDownloader, AppWorkUtils, JDBrowser, MyJDownloaderClient>] [<out dir>]
#
# Defaults: $JD_DIR (environment, else the project .env) or /Volumes/MySSD/Coding/Java,
# and build/hoster-resolver.
# Output:   <out>/resolver/{Core.jar, jd/plugins/{hoster,decrypter}/, libs/, revision.txt, LICENSE}
#           and <out>/resolver.zip, which the bundle scripts ship and emulecored unpacks
#           into its config dir.
# Plugins stay loose .class files: JD's plugin scanner lists the directory next to the jar
# holding jd.SecondLevelLaunch, not a jar. All other classes go into Core.jar (#88: 12k files).
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
# JD_DIR from the project .env when the environment has none
if [ -z "${JD_DIR:-}" ] && [ -f "$ROOT/.env" ]; then
    JD_DIR=$(sed -n 's/^[[:space:]]*JD_DIR[[:space:]]*=[[:space:]]*//p' "$ROOT/.env" | tail -n 1 \
        | sed 's/[[:space:]]*$//; s/^"\(.*\)"$/\1/; s/^'"'"'\(.*\)'"'"'$/\1/')
fi
JD=$(cd "${1:-${JD_DIR:-/Volumes/MySSD/Coding/Java}}" && pwd)
OUT="${2:-$ROOT/build/hoster-resolver}"
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)

for p in JDownloader AppWorkUtils JDBrowser MyJDownloaderClient; do
    [ -d "$JD/$p/src" ] || { echo "build-hoster-resolver: $JD/$p/src missing" >&2; exit 2; }
done

# JDK 17+: the runtime is downloaded as Java 21, classes are made for 17
if [ -n "$JAVA_HOME" ] && [ -x "$JAVA_HOME/bin/javac" ]; then
    JDK="$JAVA_HOME"
elif [ -x /usr/libexec/java_home ]; then
    JDK=$(/usr/libexec/java_home -v 17+ 2>/dev/null || true)
else
    JDK=$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")
fi
[ -n "$JDK" ] && [ -x "$JDK/bin/javac" ] || { echo "build-hoster-resolver: need a JDK 17 or newer (set JAVA_HOME)" >&2; exit 2; }

WORK="$OUT/work"
RES="$OUT/resolver"
rm -rf "$WORK" "$RES" "$OUT/resolver.zip"
mkdir -p "$WORK/classes" "$RES/jd/plugins" "$RES/libs"

SRC="$JD/JDownloader/src"
# compile against every jar; ship only what a headless resolver loads (see is_dropped)
find "$JD/JDownloader/ressourcen/libs" "$JD/JDownloader/ressourcen/libs_ext" "$JD/AppWorkUtils/libs" \
     "$JD/AppWorkUtils/dev_libs" "$JD/JDBrowser/libs" "$JD/MyJDownloaderClient/libs" \
     -name "*.jar" ! -name "*-sources.jar" 2>/dev/null | sort > "$WORK/jars.txt"
CP=$(tr '\n' ':' < "$WORK/jars.txt")

find "$SRC/jd/plugins/hoster" "$SRC/jd/plugins/decrypter" -name "*.java" | sort > "$WORK/files.txt"
# core classes the driver calls that no plugin pulls in
for c in jd/SecondLevelLaunch \
         jd/controlling/downloadcontroller/SingleDownloadController \
         jd/controlling/linkcrawler/LinkCrawler \
         jd/controlling/linkchecker/LinkCheckerThread \
         jd/controlling/accountchecker/AccountCheckerThread \
         org/jdownloader/plugins/controller/host/HostPluginController \
         org/jdownloader/plugins/controller/crawler/CrawlerPluginController \
         org/jdownloader/captcha/v2/ChallengeResponseController \
         org/jdownloader/net/AutoBCSSLSocketStreamFactory; do
    echo "$SRC/$c.java" >> "$WORK/files.txt"
done
find "$ROOT/tools/hoster-resolver/src" -name "*.java" >> "$WORK/files.txt"

REV=$(svn info --show-item revision "$JD/JDownloader" 2>/dev/null || echo "?")
echo "== compiling $(wc -l < "$WORK/files.txt" | tr -d ' ') sources, JDownloader r$REV, $("$JDK/bin/javac" -version 2>&1)"
"$JDK/bin/javac" -J-Xmx8g --release 17 -nowarn -proc:none -encoding UTF-8 -Xmaxerrs 200 -implicit:class \
    -sourcepath "$SRC:$JD/AppWorkUtils/src:$JD/JDBrowser/src:$JD/MyJDownloaderClient/src" \
    -cp "$CP" -d "$WORK/classes" @"$WORK/files.txt" > "$WORK/compile.log" 2>&1 \
    || { tail -40 "$WORK/compile.log" >&2; echo "build-hoster-resolver: compile failed, see $WORK/compile.log" >&2; exit 1; }

# non-class resources JD reads from its source roots (translations, json defaults)
for s in "$SRC" "$JD/AppWorkUtils/src" "$JD/JDBrowser/src"; do
    (cd "$s" && find . -type f ! -name "*.java" ! -path "*/.svn/*" \( -name "*.json" -o -name "*.properties" -o -name "*.lng" -o -name "*.dat" \) \
        -exec sh -c 'mkdir -p "$0/$(dirname "$1")" && cp "$1" "$0/$1"' "$WORK/classes" {} \;)
done

# plugins loose next to Core.jar, the rest packed
mv "$WORK/classes/jd/plugins/hoster" "$WORK/classes/jd/plugins/decrypter" "$RES/jd/plugins/"
"$JDK/bin/jar" --create --file "$RES/Core.jar" -C "$WORK/classes" .

is_dropped() {
    case "$(basename "$1")" in
        synthetica*|flatlaf*|svnkit*|sqljet*|sequence-library*|antlr-runtime*|irclib*|jsyntaxpane*|\
        sevenzipjbinding*|zip4j*|AppleJavaExtensions*|libdbus*|unix.jar|hexdump.jar|debug-disable.jar) return 0 ;;
        # UPnP reconnect / MyJD direct server only (cling, its seamless http/xml), SVG icons,
        # and JD's copy of jna-platform.jar under another name
        cling-*|seamless-http*|seamless-xml*|jsvg*|jna_platform.jar) return 0 ;;
    esac
    return 1
}
while read -r j; do
    is_dropped "$j" || cp "$j" "$RES/libs/"
done < "$WORK/jars.txt"

echo "$REV" > "$RES/revision.txt"
cp "$ROOT/tools/hoster-resolver/LICENSE" "$RES/LICENSE"
(cd "$OUT" && rm -f resolver.zip && zip -qr -X resolver.zip resolver)
echo "== $(unzip -l "$RES/Core.jar" | grep -c '\.class$') core + $(find "$RES/jd" -name '*.class' | wc -l | tr -d ' ') plugin classes, $(ls "$RES/libs" | wc -l | tr -d ' ') jars, $(du -sh "$OUT/resolver.zip" | cut -f1) zip -> $OUT/resolver.zip"
