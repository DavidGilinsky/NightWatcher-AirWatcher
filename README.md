<!--
  Author:   David Gilinsky
  File:     README.md
  Purpose:  Project overview, install, configuration, and build instructions.
  License:  GPL-3.0-or-later
-->
# NightWatcher AirWatcher

A small C++ daemon that pulls raw FITS frames off **ZWO ASIAir** devices over their
SMB share and drops them into the [nightwatcher-ingest](https://github.com/DavidGilinsky/nightwatcher-ingest)
landing directory. It replaces an SMB sync tool (e.g. GoodSync) with a purpose-built
copier that adds copy scheduling, optional deletion, subnet discovery, its own web UI,
and a live status tab inside [NightWatcher2](https://github.com/DavidGilinsky/NightWatcher2).

```
ASIAir (SMB)  ──►  AirWatcher  ──►  incoming/  ──►  nightwatcher-ingest  ──►  archive
```

It is a companion to NightWatcher2: it manages ASIAirs itself (its own web UI + DB
tables) and, while running, registers so a read-only **AirWatcher** tab appears in the
NightWatcher2 web UI. NightWatcher2's core is not modified.

## What it does

- **Discovers** ASIAirs on a subnet (scans for the ASIAir control port 4400, confirmed
  by the SMB share / `ASIAIR` NetBIOS name) — like `sqmctl discover` for SQMs. Or add one
  by IP/hostname.
- **Copies** new FITS frames from each ASIAir's `Autorun`/`Plan` folders into `incoming/`
  (guest SMB, no password), writing `*.part` then renaming so ingest only sees finished files.
  A frame is copied only once its size and modification time have held still for
  `stable_seconds` (the ASIAir rewrites each light in place to add its plate solution
  15-30 s after capture; a copy taken during that rewrite is torn). After the copy the
  source is re-checked and the landed file is verified as one whole FITS file; anything
  else is discarded and retried once the source settles, and logged as `retry` in the
  action log.
- **Passes filenames through unchanged**, including the custom name the ASIAir app
  appends to every frame it saves. That is how a manual filter drawer gets its filter
  into the archive: type `F_<name>` (for example `F_ALP_T_5nm`) in the ASIAir's custom
  file-name field, and nightwatcher-ingest reads the token, writes it as `FILTER` and
  files lights and flats under that filter. See *Manual filter drawer* in the
  [nightwatcher-ingest README](https://github.com/DavidGilinsky/nightwatcher-ingest).
- **Deletes** copied frames from the ASIAir, if you enable it (see *Delete* below).
- **Schedules** copy and delete: immediately, in batches of N, after N frames, or at a
  time of day (local).
- **Shows status**: per-ASIAir total frames in Autorun and how many remain to copy, in its
  own web UI and in the NightWatcher2 tab.

## Manage ASIAirs like sensors

Add, discover, enable/disable, and configure ASIAirs from **AirWatcher's own web UI** on
its own port (default `8686`, distinct from NightWatcher2's `8080`). Each ASIAir has:

- **Copy policy** — `immediate` · `batch` (N per cycle) · `after_frames` (wait for N) · `time_of_day`.
- **Delete policy** — off, or delete `immediately` / `after N` / at a `time_of_day`, `via` SMB or SSH.
- host, SMB share (default `EMMC Images`), timezone, notes.

## Install (Debian/Ubuntu)

```sh
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
( cd build && cpack -G DEB )
sudo apt install ./build/airwatcher_*.deb
```

The install is **debconf-driven**: it prompts for the NightWatcher database password, the
`incoming` directory, the web UI bind/port and an optional access token, the default
discovery subnet, and an optional group to grant the `airwatcher` service account write
access to `incoming/`. Re-run any time with `sudo dpkg-reconfigure airwatcher`.

It installs the bundle under `/usr/local/airwatcher`, creates an unprivileged `airwatcher`
account, seeds `/etc/airwatcher/airwatcher.conf`, writes secrets to
`/etc/airwatcher/airwatcher.env` (mode 0640), and enables the systemd service. Give the
`airwatcher` account write access to your `incoming/` directory, then browse to
`http://<host>:8686/`.

## Upgrading from 0.1.0 or 0.1.1

Before 0.1.2 the `first_seen` column of `airwatcher_files` (and `created_at` of
`asiairs`) took the table default, which is the database server's local clock,
while every other timestamp was written in UTC. On an MST server those rows sit
7 hours behind. 0.1.2 writes UTC everywhere and resets the column defaults on
start, but it cannot tell which existing rows are local, so convert them once,
with the service stopped, using the server's own offset:

```sql
SET @off = TIMESTAMPDIFF(SECOND, NOW(), UTC_TIMESTAMP());
UPDATE airwatcher_files SET first_seen = first_seen + INTERVAL @off SECOND;
UPDATE asiairs SET created_at = created_at + INTERVAL @off SECOND;
```

All DATETIME columns AirWatcher owns are UTC; the web UI appends `Z` when it
renders them.

## Delete over SMB

ZWO documents the ASIAir share as read-only, but at least some firmware serves it
**guest-writable** — in which case `delete_via = smb` works directly (verified on an ASIAir
Plus). If your firmware's share is truly read-only, deletion needs SSH into a rooted device
(`delete_via = ssh`, a documented future option) or you delete from the ASIAir app. Test
your device from the web UI's per-ASIAir **Test** button.

## Configuration

`/etc/airwatcher/airwatcher.conf` (INI) configures only the daemon: the `[database]`
connection, the `[web]` UI bind/port/TLS, the `[copier]` incoming dir + cadence + default
subnet, and `[extension]` registration. See
[`config/airwatcher.conf.example.in`](config/airwatcher.conf.example.in). The database
password and the optional web token come from the environment (the `.env` file), never the
config. ASIAirs live in the database, managed from the web UI.

## Build from source

Requirements: a C++17 compiler, CMake ≥ 3.16, `libmariadb-dev`, `libsmbclient-dev`,
`libssl-dev`.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
NWDB_PASSWORD=... ctest --test-dir build   # db_smoke needs a MariaDB
```

Cross-compile for arm64 (Raspberry Pi) with `-DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64.cmake`.

## Repository layout

```
src/db/         Database layer (libmariadb): asiairs, files, status, log, extension registry
src/smb/        libsmbclient wrapper (walk/copy/delete a guest share)
src/discovery/  subnet scan for ASIAirs (port 4400)
src/copier/     copy engine + policy scheduler
src/api/        cpp-httplib web server + JSON API (+ vendored tls_cert)
src/daemon/     config, logging, main
web/            static web UI (vanilla JS, dark theme)
config/         example config (templated)
systemd/        service unit (templated)
debian/         .deb maintainer scripts + debconf config/templates
tests/          db + smb + discovery + copier probes
```

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
