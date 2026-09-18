# Web interface: templates, escaping and languages

The template web UI (`data/config/eMule.tmpl`, rendered by `src/core/webserver/WebServer.cpp`)
and how its text is filled in, escaped and translated. The REST API under `/api/v1` is not
covered here; it stays English and returns JSON.

## Template syntax

- **Sections:** `<--TMPL_NAME-->` … `<--TMPL_NAME_END-->`. Each page builder fills one or
  more sections.
- **Variables:** `[Key]` becomes a value the builder supplies.
- **Translatable text:**
  - `{{Text}}` is element text or a quoted attribute value.
  - `{{js:Text}}` sits inside a `'…'` / `"…"` JavaScript string literal.
- **Custom templates** (`webserver.templatePath`) without markers still render, in English.

## How substitution works

`WebTemplateEngine::substitute()` makes **one left-to-right pass**:

- **`[Key]`:** replaced by its value.
  - A key the builder did not supply stays as written, so CSS `input[type=…]`, `[hidden]`
    and script `a[i]` are safe.
  - Inserted values are never scanned again. A file or server named `[Session]` stays
    exactly that.
  - The old engine ran `replace()` once per key, which rewrote such names.
- **`{{Text}}`:** `QCoreApplication::translate("eMule::WebServer", "Text")`, then HTML-escaped.
- **`{{js:Text}}`:** the same translation, escaped for a JS string literal.
  - `\ ' " < > &` become `\x..` escapes, and line breaks become `\n` / ` `.
  - The result holds no HTML-special character, so it is safe in a `<script>` block and in
    an `on*` attribute alike.
  - Script that puts such text into `innerHTML` must still pass it through `esc()`.
    The header defines `esc()`; the menu helpers `mi()` and `unMi()` already call it.

## Marker rules

- **Content:** plain text on one line, with no `<`, `&`, `{` or `}`. Use `…` rather than `&hellip;`.
- **Values stay outside:** write `{{Downloads}} ([DownloadCount])`.
- **Script placeholders:** a sentence the page script completes uses `%1`, `%2`, filled by
  `unFmt()`. A JS string cannot pick a plural form, so it says "download(s)".
- **Reuse:** where the Qt GUI already has the same sentence, copy its English wording exactly.
  `translate_missing.py reuse` then fills in its translation.

## Escaping rules for page builders

- **Every value that is not markup the builder produced itself goes through `htmlText()`.**
  That includes server names and descriptions, log lines, the nickname, file names, release
  names and categories. The engine does no escaping of its own.
- **Remote-controlled values never go inside a JS string in an attribute.** They go into a
  `data-*` attribute that the script reads with `this.dataset`.
  - Examples: `servermenu(event,'[Session]',this.dataset.ip,this.dataset.port)`,
    `sharedmenu(event,this.dataset.link)`, and every Usenet row.
- **Translated sentences that contain markup:** escape the translation, then add the markup
  as arguments. Use the multi-argument form so an argument holding `%2` stays text:
  `htmlText(tr("… %1 …")).arg(strong(name), htmlText(other))`.
- **Numbers in `arg()`:** pass them as `QString::number(…)` to the multi-argument call.
  Chained `.arg().arg()` would reinterpret a `%2` inside the first argument.

## Languages

- **Default:** the app language, `general.language` in `preferences.yml`. Empty means the
  system locale, the same rule the GUI uses.
- **Override:**
  - The header's language menu sets it for one session by loading the page with
    `?setlang=<code>`.
  - An empty code follows the app again, and an unknown code is ignored.
  - The choice lives with the session and ends with it.
- **Incoming listing and player** (`/api/v1/incoming`, token routes with no session):
  - They honour a `lang=` parameter, and every link they draw carries it on.
  - The Usenet page adds it to the links it opens.
- **Translated:** every page and fragment, and the web UI's own JSON routes
  (`/usenet/action`, `/usenet/add`, `/usenet/entries`).
- **English:** the REST API.
- **Languages offered:** exactly the `emuleqt_<code>.qm` files the GUI ships
  (`AppConfig::availableLanguages()`), plus English.

### Mechanism

- **The router:** `TranslationRouter` (`src/core/app/TranslationRouter.{h,cpp}`) is a
  `QTranslator` that `emulecored` installs once.
- **Outside a scope** it translates nothing. The log, IPC, the REST API and worker threads
  behave exactly as if no translator were installed.
- **Inside a scope:** a web-UI handler opens `TranslationRouter::Scope(router, code)`, and
  every translation lookup on that thread then answers in `code`. That covers `tr()` and
  core helpers such as `formatByteSize` units and rating labels.
- **Loading:** translators load on first use through `AppConfig::langCandidates()` and are
  kept for the life of the process.
- **Context:** template markers and the web server's C++ strings share `eMule::WebServer`.
  Member functions call `tr()`; anonymous-namespace helpers call `WebServer::tr()`.

## Adding or changing a string

1. **Mark it.**
   - In the template, wrap it in `{{…}}` or `{{js:…}}`.
   - In C++, use `tr()` or `WebServer::tr()`. Never use a local `tr` lambda, which lupdate
     files under the wrong context.
2. **Extract:** run `scripts/localize.sh extract`.
   - It runs `scripts/extract_web_strings.py` first, which regenerates
     `src/core/webserver/WebTemplateStrings.h` so lupdate sees the markers.
   - lupdate runs with `-I src/core`, so `tr()` in `WebServer.cpp` lands in
     `eMule::WebServer` rather than `WebServer`.
3. **Borrow existing translations:** `scripts/translate_missing.py reuse` copies them from
   identical source text in other contexts.
4. **Translate the rest:**
   - `scripts/translate_missing.py export --context eMule::WebServer`
   - fill in `lang/missing.json`
   - `scripts/translate_missing.py apply`
5. **Tidy and build:**
   - Run `scripts/localize.sh extract` again, because `reuse` and `apply` rewrite the `.ts`
     layout.
   - Build: `qt_add_translations` compiles the `.qm`.

`tst_WebServer` fails in three cases:

- `WebTemplateStrings.h` is stale.
- A marker contains markup.
- The shipped German catalogue does not translate the web context.
