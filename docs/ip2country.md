# IP2Country (country flags)

Daemon resolves IP → ISO country code from an `.mmdb` file (`src/core/geo/IP2Country`);
GUI only gets `cc` on IPC rows. `GeoIpUpdater` keeps the file current.

## Sources (Options → Display → Country flags)

| pref `geoIpSource` | source | file in config dir | download |
|---|---|---|---|
| 0 (default) | MaxMind GeoLite2-Country | `GeoLite2-Country.mmdb` | permalink `.tar.gz`, HTTP Basic (account ID + license key) |
| 1 | DB-IP Country Lite | `dbip-country-lite.mmdb` | `https://download.db-ip.com/free/dbip-country-lite-YYYY-MM.mmdb.gz`, no account |

- DB-IP added for #100: MaxMind refuses downloads from CN.
- Account ID / license key inputs greyed out unless MaxMind selected.
- Each source has its own file. Selected file missing → the other stays loaded
  (DB-IP users keep the bundled GeoLite2 until first download).
- Only GeoLite2 ships in releases (bundle adoption, newest build wins).
- DB-IP is monthly: current UTC month first, 404 → previous month once.
- Schedule same for both: hourly tick, refresh after 7 days, 6 h back-off after a
  failure. MaxMind schedule stays quiet without credentials; DB-IP needs none.
- Switching source (IPC `SetPreferences geoIpSource`) reopens the files, drops the
  back-off and downloads right away when the selected file is missing.
- Status (`GetGeoIpStatus`) carries `type` = mmdb `database_type`
  (`GeoLite2-Country` / `DBIP-Country-Lite`) and `source`.

## Licences

- GeoLite2: "This product includes GeoLite2 data created by MaxMind."
- DB-IP Lite: CC BY 4.0, link back required: "IP Geolocation by DB-IP" → https://db-ip.com
  (shown on the Options page while DB-IP is selected).
