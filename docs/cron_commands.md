# CronManager

`CronManager` is a local, time-of-day settings scheduler for MeshCore repeater
firmware. It lets you schedule a config change (the same kind of change you'd
make with an interactive `set <key> <value>` command) to happen automatically
at a given clock time each day — for example, turning CAD off at 21:00 and
back on at 06:00.

**It does not send anything over the mesh.** It only mutates local node
settings (`NodePrefs`, via the existing `CommonCLI`/`set` machinery) at the
scheduled time. There is no radio traffic involved.

## Commands

All commands start with `cron` and are handled the same way whether typed
over local serial or sent by a remote client (an authenticated client's
`cron` command goes through the same dispatch as `set`/`get`/`filter`).

### `cron add <key> <value> time:<HH>[:MM] [persist]`

Schedules `<key>` to be set to `<value>` every day at `HH:MM` (UTC, taken
from the node's RTC/GPS/manual clock — see [Time source](#time-source)
below). `<value>` may contain spaces (e.g. free-text fields like `owner`);
everything between `<key>` and the `time:` marker is taken as the value.

By default, a fired entry only applies the change in RAM (via the same path
as an interactive `set`) — no flash write happens. Add the bare `persist`
keyword to also call `savePrefs()` on that fire, writing the change to
flash so it survives a reboot.

```
cron add cad off time:21
cron add cad on time:6 persist
cron add owner Michael's repeater, Hvidovre time:3:30
```

Reply on success: `OK - cron #<id> added: <key>=<value> at HH:MM (persist:yes|no)`

`<id>` is the slot number assigned to this entry — you'll need it for `del`,
`enable`, and `disable`.

### `cron list [page]`

Lists scheduled entries.

- **Local serial** (`sender == NULL`): full list is printed directly, no
  pagination.
- **Remote client**: paginated to fit inside the reply packet budget. If
  there's more than one page, the reply ends with a hint like
  `-> cron list 1` — send that exact command to see the next page.

```
#0 cad=off 21:00 on temp
#1 cad=on 06:00 on persist
```
Fields, left to right: entry id, `key=value`, scheduled time, `on`/`off`
(whether the entry is currently enabled), `persist`/`temp` (whether a fire
also writes the change to flash).

### `cron del <id>`

Removes entry `<id>` entirely (frees the slot).

### `cron enable <id>` / `cron disable <id>`

Turns an entry on/off without deleting it. A disabled entry is skipped by
the scheduler but keeps its slot and configuration.

## How scheduling works

- Each entry fires **once per day**, at its configured `hour:minute`.
- `CronManager::loop()` should be called roughly once per second from the
  main firmware loop. It checks the current time against every enabled
  entry; on a match, it issues the equivalent of `set <key> <value>` through
  `CommonCLI`. `savePrefs()` (the flash write) is only called if the entry
  was added with `persist` — an interactive `set` doesn't save to flash by
  itself either way, so a non-`persist` entry applies the change in RAM
  only, every day, with no flash write at all.
- Firing is tracked per calendar day (`last_fired_yday`), so an entry won't
  re-fire multiple times within the same minute or day, even across reboots
  — this state is persisted along with the entry (independent of the
  entry's own `persist` flag, which only governs the *setting* it changes,
  not the cron schedule itself).

## Time source

`CronManager` doesn't manage time itself — it reads `mesh::RTCClock::
getCurrentTime()`, the same clock abstraction the rest of the firmware uses.
Depending on the board, that clock may be backed by a hardware RTC, GPS, or
a manually-set software clock. `CronManager` doesn't need to know which.

**Known caveat — no hardware RTC:** on boards without a real RTC chip (e.g.
`ESP32RTCClock` in `ESP32Board.h`, or `VolatileRTCClock` in
`ArduinoHelpers.h` used on nRF52/generic boards), the clock is seeded from
a hardcoded fallback date/time (`RTC_TIME_MIN`) after a cold/crash/brownout
reset, and then ticks forward normally from that seed until a real GPS fix
or manual `time set` happens. Because it's a real ticking clock rather than
a frozen value, `getCurrentTime()`'s return value alone cannot reliably
tell "still on the fallback seed" apart from "genuinely synced" — a few
seconds after boot the epoch has already moved past the fallback constant.

`CronManager` therefore relies on `mesh::RTCClock::isTimeValid()` (added to
the base interface, default `true`) rather than inspecting the epoch value.
Board-specific clock implementations that have a fallback-seed concept
(`ESP32RTCClock`, `VolatileRTCClock`) override it to track whether a real
sync has happened — see the `_synced` member in each. Clocks without a
fallback concept (a plain HW RTC, for instance) just use the default `true`
and need no changes.

One residual edge case in the ESP32 implementation: if the node enters deep
sleep *before* ever getting a real sync, ESP-IDF preserves system time
across the sleep regardless, so `_synced` can end up `true` on wake even
though the underlying time is still the fallback seed. Rare in practice
(requires deep sleep very early after a cold boot, before GPS/manual sync).

While time is invalid, `loop()` does nothing — no entries fire. The moment
the clock becomes valid (GPS fix / manual set / reboot with a real RTC),
`CronManager` "arms" itself: entries whose scheduled time has already passed
today are marked as done *without* executing, so a late GPS fix at, say,
21:32 won't retroactively fire a `time:21` entry — but an entry does still
fire normally if the fix happens to land in its exact target minute.

### Behavior across reboots

`CronManager` is reconstructed from scratch on every reboot (it's a member
of `MyMesh`), so `_timeWasValid` always starts `false` in RAM. This means
the "arm" logic above always runs on the very first `loop()` call after any
reboot — harmless by design, since it only marks already-past entries as
done without executing them, and lets an entry due right now still fire
through the normal matching path in the same call.

What differs between reboot types is how fast `isTimeValid()` becomes true
again:

- **ESP32 deep sleep wake:** ESP-IDF preserves system time across deep
  sleep, and `ESP32RTCClock::begin()` doesn't touch `_synced` on this path
  — it keeps whatever value it had before sleep. If the node was synced
  before sleeping, it's synced immediately on wake.
- **Reboot without power loss** (watchdog, panic, software reset,
  brownout): both `ESP32RTCClock` (via `RTC_NOINIT_ATTR` backup) and
  `VolatileRTCClock` on nRF52 (via `persistent_magic`/`persistent_time`)
  can restore the last known real time and set `_synced = true`
  immediately — no need to wait for a fresh GPS fix. Each entry's own
  `last_fired_yday` is also persisted in `/cron_rules.json` and reloaded in
  `begin()`, so even though the in-RAM `CronEntry` objects are new, no
  entry double-fires.
- **Full power loss** (battery pulled, full power cycle): the backup/
  persistent state isn't reliable (it lives in RAM-backed storage, not
  flash), so both clocks fall back to their hardcoded seed and
  `_synced = false`. `CronManager::loop()` does nothing at all until a
  fresh GPS fix or manual time set — at which point the "arm" logic above
  kicks in exactly as it would on any other first-valid-time event.

In short: the better a reboot preserves time, the faster cron becomes
operational again — but in the worst case (full power loss), the system
stays safely inactive rather than incorrectly active, until there's a real
time source to trust.

## Persistence

Entries are stored as JSON at `/cron_rules.json`, using the same
`ConfigSerializer` mechanism as `NodePrefs` and `ChannelFilter`. Storage is
a fixed set of `MAX_CRON_ENTRIES` slots (default 16, adjustable via
`#define MAX_CRON_ENTRIES` before including `CronManager.h`, capped at 26
— see below); each slot has an `in_use` flag. An entry's `<id>` is simply
its slot index — deleting an entry frees that slot for reuse by the next
`cron add`.

Each slot is written as a named sub-object keyed `"ea"`, `"eb"`, ... (index
0 → `a`, index 1 → `b`, etc.), **not** `"e0"`, `"e1"`. `ConfigSerializer`'s
key tokenizer (`is_key_char()` in `ConfigSerializer.cpp`) only accepts
letters and `_`, never digits — a digit-suffixed key writes to flash fine
but fails to parse back on the next load, silently: `loadSerial()` just
stops at the first bad key, leaving the whole list empty. This is why
`MAX_CRON_ENTRIES` is capped at 26 (one letter, `a`-`z`) rather than being
freely adjustable.

## Firmware integration

```cpp
// MyMesh.h
#include "CronManager.h"
...
  CronManager _cron;   // member, alongside _cli, _filter, etc.

// MyMesh.cpp — begin()
_cli.loadPrefs(_fs);
_cron.begin(&_cli, getRTCClock(), *_fs);

// MyMesh.cpp — handleCommand()
} else if (strncmp(command, "cron", 4) == 0 && (command[4] == ' ' || command[4] == '\0')) {
    const char* cron_args = (command[4] == ' ') ? command + 5 : "";
    _cron.handleCommand(cron_args, reply, *_fs, sender);
} else {
    _cli.handleCommand(sender_timestamp, command, reply);
}

// MyMesh.cpp — loop()
mesh::Mesh::loop();
_cron.loop();
```

## Limitations

- One fire per entry per day — there's no "every N minutes" or "every N
  days" interval scheduling, only daily time-of-day.
- Scheduled changes go through the same validation as an interactive `set`
  command, so an invalid `<key>`/`<value>` pair will silently fail the same
  way a bad manual `set` would (`CronManager` doesn't currently surface
  that failure anywhere other than the CLI reply of the `set` call itself,
  which isn't logged).
- Fixed number of slots (`MAX_CRON_ENTRIES`); `cron add` replies with an
  error once all slots are in use.