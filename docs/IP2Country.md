# IP2Country — country flags

A port of MorphXT/EastShare's IP2Country feature. Each client, source, server, Kad contact,
friend and news server gets a country flag after its icon. There is also an optional **Country**
column.

MorphXT used a `GeoIPCountryWhois.csv` file and a `countryflag32.dll` resource DLL. eMule Qt
replaces them with a MaxMind **GeoLite2-Country** database (`.mmdb`, read through libmaxminddb)
and MorphXT's own 18×16 flag icons, compiled into the GUI.

## Setup

1. Create a free MaxMind account at <https://www.maxmind.com/en/geolite2/signup>.
2. In the MaxMind account portal, generate a **license key**.
3. In eMule Qt, open **Options → Display → Country flags (IP2Country)**. Enter the account ID and
   the license key, then press **Update now** or **Apply**.

The daemon downloads `GeoLite2-Country.mmdb` into its config directory. The database is **never
bundled** with a release, so each user needs their own account. Instead of entering an account,
you can also copy a `GeoLite2-Country.mmdb` (or a compatible country database, such as DB-IP
Lite) into the config directory by hand. The daemon uses it on its next start.

## Where the flag shows

| List | Flag | Country column |
|---|---|---|
| Transfers: Uploading, Downloading, On Queue, Known Clients | after the client icon | yes |
| Downloads, expanded source rows | after the client icon (source rows now show the client icon too) | yes (sources only) |
| Servers | before the server name | yes |
| Kad contacts | after the contact icon | yes |
| Friends | after the friend icon | – |
| Options → Usenet → News servers | before the name | – |
| Client details dialog | "Country" row | – |

The Usenet panel has no list of servers or connections, so it gets no flag.

For news servers the daemon resolves the configured host name itself (`LookupHostCountries`).
Behind a proxy the flag therefore still shows the news server's country, not the proxy's.

## Settings

| Setting | Where it is stored | Default |
|---|---|---|
| Show country flags | GUI, `uistate.yml` `showCountryFlags` | on |
| Country column (Hidden / Short name / Long name) | GUI, `uistate.yml` `countryNameMode` | Hidden |
| MaxMind account ID / license key | daemon, `preferences.yml` `geoip.accountId` / `geoip.licenseKey` | empty |
| Update the database weekly | daemon, `preferences.yml` `geoip.autoUpdate` | on |

The Country column follows the Options setting, not the saved header layout. The "Short name" is
the ISO 3166-1 alpha-2 code. MorphXT's "mid" name (ISO3) is dropped because the mmdb has no ISO3
codes.

## How it works

- **Lookup:** the daemon does it (`src/core/geo/IP2Country`) and puts a two-letter `cc` field on
  every client, server, friend and Kad contact row it sends over IPC. The GUI only maps the code to
  a flag and a name (`src/gui/utils/CountryFlags`), so a remote GUI needs no database. LAN,
  loopback and unknown addresses get an empty code. The database is memory-mapped, and lookups
  are cached and thread-safe.
- **Update:** `src/core/geo/GeoIpUpdater` does the download.
  - It requests `https://download.maxmind.com/geoip/databases/GeoLite2-Country/download?suffix=tar.gz`
    with HTTP Basic auth. The license key is never put in a URL or a log line.
  - It sends `If-Modified-Since`, so a database that hasn't changed isn't downloaded again.
  - It unpacks the tarball, validates the new file, and swaps it in place without a restart.
  - An hourly tick downloads when the database is missing or the last check is a week old. After
    a failure it waits 6 h before retrying automatically, because GeoLite2 limits downloads per
    day.
- **IPC:**
  - `GetGeoIpStatus` (273)
  - `UpdateGeoIpDatabase` (274)
  - `LookupHostCountries` (275)

## Attribution

This product includes GeoLite2 data created by MaxMind, available from <https://www.maxmind.com>.
The flag icons are MorphXT's "Advanced Country Flags" set, GPL like eMule Qt.
