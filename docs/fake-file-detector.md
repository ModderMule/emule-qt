# Fake-file detector and the Confidence column

The search list and the download list have a **Confidence** column. It says how far a file can
be trusted to be what its name claims, and the tooltip says why. It is a warning only: nothing
is hidden, blocked or removed.

| Confidence | Meaning |
|---|---|
| Spam | The spam filter counts it as spam (marked by you, or like something you marked) |
| Likely fake | Fake score 75–100 |
| Suspect | Fake score 50–74 |
| Caution: N% | Fake score 25–49 |
| Looks good | Nothing, or little, speaks against it |
| Genuine | Nothing at all speaks against it, and something speaks for it: a user rating of 4+, or many independent Kad publishers together with a second name that describes the same content |

Sorting the column puts the worst first. The same values are in the REST API and MCP rows
(`confidence`, `fakeScore`, `fakeReasons`) and on the web interface's search and transfer pages.

## What is looked at

Each reason counts once; the sum is capped at 100.

| Reason (`fakeReasons`) | Points | When |
|---|---|---|
| `multiple_names` | 10 / 25 | The hash is shared under names for 2 / 3 or more different contents |
| `names_span_kinds` | 25 | The names are of different kinds of file, e.g. a video and an archive |
| `bad_signal_name` | 25 | A name matches a rule in `FakeFileFilter.dat` |
| `bad_signal_comment` | 15 | A comment matches a rule |
| `header_extension_mismatch` | 45 | The first bytes are not the container the extension claims (downloads) |
| `executable_masquerade` | +25 | … and they are a program |
| `archive_masquerade` | +20 | … and they are an archive |
| `claimed_type_mismatch` | 15 | Published as audio/video but named like a program or archive, or the reverse |
| `spam_score` | 15 / 25 | Spam rating of 30 / 60 or more |
| `spam_status` | 15 | Counted as spam |
| `fake_rating` | 30 | Rated 1 ("fake") |
| `bad_rating` | 20 | Rated 2, or a Kad note rates it 1 |
| `multiple_aich` | 35 | Answers disagree on the AICH hash |
| `implausible_media_length` | 10 | Video of 50 MiB+ under a minute or over 12 h; audio of 1 MiB+ under 2 s or over 24 h |
| `implausible_media_bitrate` | 10 | Video under 40 or over 500,000 kbit/s; audio under 16 or over 2,000 |
| `media_size_mismatch` | 10 | Length × bitrate is more than 3× off the file size |
| `name_media_tag_mismatch` | 10 | Artist, album and title tags appear in none of the names |

**Names.** All names seen for a hash are compared: those of the current search, those earlier
searches recorded (the seen-files index, when it is on), and for a download those its sources
report. Release words (codec, resolution, language, container), common short words, numbers and
hex runs are ignored. Two names count as the same content when they share one remaining word, a
year, or an episode number (`S01E08` and `1x08` are the same). A film under its original and its
translated title can therefore count as two contents; that alone is 10 points and stays
"Looks good".

**First bytes.** Checked for a download as soon as its first bytes are complete, for the media
extensions whose container has a mandatory signature (avi, mkv, mp4, wmv, mpg, flac, …).

## FakeFileFilter.dat

In the configuration directory; created at first start and never overwritten. Read at program
start.

```
[tokens]
# whole words or phrases, any case
fake
wrong file

[regex]
# regular expressions, any case, on the whole name or comment
\.mp4\.exe$
```

A regular expression that does not compile is skipped and named in the log. When the file is
missing or holds no rule, the built-in rules apply (`fake, corrupt, wrong file, password
protected, virus, trojan, malware`; `\.mp4\.exe$`, `\.avi\.scr$`).

## Limits

- A Kad search only returns the names that contain the search words, so one search rarely shows
  all the names a fake goes by. The seen-files index adds the names of earlier searches; the
  verdict gets better the longer the index has been on.

- A file met once, under one name, with no rating: there is little to judge. It is "Looks good".
- A fake whose spreader uses one consistent name is found only by ratings, comments, the spam
  filter or, once downloading, its first bytes.
- Torrent and Usenet rows of a server's meta search are not judged.

## For contributors

`src/core/search/FakeFileDetector.{h,cpp}` is a pure function (`assessFile`). The search side
fills it in `SearchList::assess`, the download side in `PartFile::fakeVerdict`. The wording is in
`src/core/search/ConfidenceText.h`. `EMULE_FAKE_SAMPLE=<json> tst_FakeFileDetector sample_replay`
prints the verdicts for a sample of real names (`[{h, type, names: [[name, count], …]}, …]`).
