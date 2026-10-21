# Search syntax

The search box takes the same expressions as classic eMule. Everything below works
for server (local/global) and Kad searches unless noted.

## Words and operators

| Type this | Finds |
|---|---|
| `matrix reloaded` | names with **both** words (a space means AND) |
| `matrix AND reloaded` | same, written out |
| `matrix OR reloaded` | names with either word |
| `matrix -1080p` | `matrix`, but **not** names containing `1080p` |
| `matrix NOT 1080p` | same as `-1080p` |
| `"matrix reloaded"` | the quoted phrase as one term |
| `matrix (dvdrip OR bdrip)` | parentheses group terms |

- `AND`, `OR`, `NOT` must be written in capitals. Lower-case `or` is an ordinary word.
- `-` excludes only at the start of a word. `spider-man` is one word.
- `-` and `NOT` need a word in front: `matrix -1080p` works, `-1080p matrix` is an error.
- Exclude several: `matrix -1080p -720p -cam`. Exclude a phrase: `matrix -"director cut"`.
- Binding: `NOT` binds tightest, then `OR`, then `AND`.
  `a b OR c` means `a AND (b OR c)`; `a OR b -c` means `a OR (b NOT c)`.
  Add parentheses when in doubt.
- At most 10 operators per search (implicit ANDs count). More gives "Expression too complex".

## Attributes

Attributes filter on file properties. Names can be cut to 3 letters (`@siz`, `@ava`).

| Attribute | Operators | Value |
|---|---|---|
| `@size` | `= > < >= <= <>` | number, `B`/`K`/`M`/`G` suffix (1024-based). No suffix = MB. `@size>700` = over 700 MB |
| `@type` | `=` | `audio`, `video`, `image` (`img`), `document`, `program`, `archive`, `iso` (`cd`) |
| `@ext` | `=` | file extension: `@ext=mkv` |
| `@sources` / `@availability` | `= > < >= <= <>` | number of sources, `k`/`m`/`g` suffix (1000-based) |
| `@complete` | `= > < >= <= <>` | number of complete sources |
| `@rating` | `= > < >= <= <>` | rating number |
| `@bitrate` | `= > < >= <= <>` | kbit/s: `@bitrate>=192` |
| `@length` | `= > < >= <= <>` | seconds, or `s`/`m`/`h` suffix, or `m:ss`, `h:mm:ss`: `@length>1:30:00` |
| `@codec` | `=` | `@codec=h264` |
| `@title`, `@album`, `@artist` | `=` | media tag text, quote it if it has spaces: `@artist="pink floyd"` |

Examples:

```
matrix -1080p @size>700 @type=video
"pink floyd" @type=audio @bitrate>=256
linux iso @size>=4G @sources>10
```

Attributes combine like any other term, so they can be excluded too:
`matrix -@ext=avi`. Not every server supports every attribute; one that does not may
ignore it.

An `ed2k://|file|…|/` link pasted alone searches for that exact file.

## Kad notes

Kad looks up one word first (the keyword) and filters the rest at the remote node.

- The keyword is the first word every result must contain, at least 3 characters.
  Words after `-`/`NOT`, words inside `OR`, and attributes are never the keyword:
  `@size>100 matrix -1080p` looks up `matrix`.
- At least one plain word must stand outside an `OR`. `matrix OR reloaded` is refused on
  Kad; `matrix (reloaded OR revolutions)` works.
- When that keyword is already being searched, the next eligible word is used.

## Errors

A mistake is reported when the search starts, with the same messages as classic
eMule: "Missing right side for NOT operator", "Missing closing ')'", "Unterminated
string", "Unknown search attribute '@foo'", "Syntax error in '@size' search attribute".
