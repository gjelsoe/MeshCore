# MeshCore Repeater Report Engine

The report engine allows a repeater node to automatically send messages to a channel based on sensor readings or a fixed schedule — without any user interaction. Rules are stored in JSON format on SPIFFS/LittleFS and survive reboots.

> **Note:** The node name is automatically prepended to all outgoing messages — e.g. `DK-REP-01: 3742mV`. No template token is needed for this.

> **Migration:** If an existing binary rule file is found at boot, it is automatically migrated to JSON format and the binary file is deleted. No manual action required.

---

## Command Reference

### `report add`

Adds a new rule. Rules are evaluated every 60 seconds.

```
report add <trigger> /<interval> @<channel> [at:HH:MM] [@scope:<region>] [@zerohop] [message]
```

| Field | Description |
|---|---|
| `<trigger>` | What causes the rule to fire — see [Triggers](#triggers) |
| `/<interval>` | Minimum seconds between sends — minimum is `1800` (30 min) |
| `@<channel>` | Destination channel — see [Channels](#channels) |
| `at:HH:MM` | Optional time of day to fire (UTC) — see [Time of Day](#time-of-day) |
| `@scope:<region>` | Optional region scope name (e.g. `denmark`) |
| `@zerohop` | Optional — send to direct neighbors only, no flood — see [Zero-Hop](#zero-hop) |
| `[message]` | Optional message template — see [Template Tokens](#template-tokens). Defaults to `{value}{unit}` if omitted |

The `at:`, `@scope:` and `@zerohop` tokens are optional and can appear in any order after the channel.

---

### `report list`

Lists rules in a compact paged format. Due to the 160-byte remote CLI limit, a maximum of 3 rules are shown per page.

```
report list          — page 1
report list 2        — page 2
report list d<n>     — full detail for rule n
```

Compact output example (3 rules per page):
```
p1/2:
1:on report:bat/3600 @h:alertchan
2:off bat<3400/1800 @p:b8da.. 08:00 @s:denmar
3:on report:noise/86400 @h:test9 @zh
```

Detail output example (`report list d1`):
```
1:on report:bat/3600s at:08:00 @private:b8da52900c881c97aea2ca8327681411 @scope:denmark "Bat: {value}{unit}"
```

In compact view: `@h:` = hash channel, `@p:` = first 4 chars of private key + `..`, `@s:` = first 6 chars of scope name, `@zh` = zero-hop.

> **Note:** When connected via serial, full output is shown without paging.

---

### `report del <n>`

Deletes the rule at index `n` (1-based). Subsequent rules are renumbered.

```
report del 1
```

---

### `report clear`

Deletes all rules.

```
report clear
```

---

### `report test <n>`

Forces rule `n` to fire immediately, regardless of trigger condition, interval, or `at:` time. Useful for verifying channel setup and message formatting.

```
report test 1
```

---

### `report enable <n>` / `report disable <n>`

Enables or disables rule `n` without deleting it.

```
report enable 2
report disable 2
```

---

## Triggers

### Condition triggers

Fire only when the measured value crosses the threshold.

| Trigger | Description | Unit |
|---|---|---|
| `bat<XXXX` | Battery voltage below threshold | mV |
| `bat>XXXX` | Battery voltage above threshold | mV |
| `temp<XX` | Temperature below threshold | °C (integer) |
| `temp>XX` | Temperature above threshold | °C (integer) |
| `noise>XX` | Noise floor above threshold | dBm (negative integer) |
| `noise<XX` | Noise floor below threshold | dBm (negative integer) |

Examples:
```
bat<3400      fires if battery is below 3400 mV
bat>4250      fires if battery is above 4250 mV
temp<-10      fires if temperature is below -10°C
temp>60       fires if temperature is above 60°C
noise>-90     fires if noise floor is above -90 dBm
noise<-110    fires if noise floor drops below -110 dBm
```

### Periodic triggers

Fire unconditionally on every interval, regardless of value.

| Trigger | Description |
|---|---|
| `report:bat` | Report current battery voltage |
| `report:temp` | Report current temperature |
| `report:noise` | Report current noise floor |

Examples:
```
report:bat      send battery voltage every interval
report:temp     send temperature every interval
report:noise    send noise floor every interval
```

---

## Time of Day

The optional `at:HH:MM` token restricts a rule to fire only at a specific time of day. Without it, the rule fires whenever the trigger condition is met and the interval has elapsed.

```
at:08:00      fire at 08:00 UTC
at:23:30      fire at 23:30 UTC
```

> **Note:** All times are UTC. Account for your local timezone offset when setting the time.

When using `at:`, set the interval to `86400` (24 hours) to fire once per day:

```
report add report:bat /86400 at:08:00 @hash:alertchan "Bat: {value}{unit}"
```

The rule engine evaluates every 60 seconds, so the fire window is within ±1 minute of the specified time.

`at:` can be combined with condition triggers to create a daily check at a specific time:

```
report add bat<3400 /86400 at:06:00 @hash:alertchan "battery low {value}{unit}"
```

---

## Channels

Two channel types are supported.

### Hash channel (`@hash:name`)

Sends to a public hashtag channel. The channel name must be entered **without** the `#` prefix — the `#` is added internally when deriving the channel key.

```
@hash:alertchan      sends to #alertchan
@hash:denmark        sends to #denmark
```

### Private channel (`@private:hexkey`)

Sends to a key-protected private channel. The key is a 32-character hex string (16 raw bytes), as shown in the companion app channel settings.

```
@private:b8da52900c881c97aea2ca8327681411
```

Since CLI commands are encrypted peer-to-peer, the key can be safely sent over LoRa via the CLI.

---

## Scope (optional)

If a region scope is specified, the packet is sent with a transport code matching that region. If omitted, `default_scope` is used as-is (which may be null, resulting in a plain unscoped flood).

```
@scope:denmark
@scope:sweden
```

The scope name must match a region name configured in the node's region map. `@scope:` and `@zerohop` are mutually exclusive — if `@zerohop` is set, scope is ignored.

---

## Zero-Hop

The optional `@zerohop` token sends the message only to direct neighbors — no flooding, no multi-hop routing. This saves duty cycle when the destination is always within direct range (e.g. a base station that is always a direct neighbor).

```
report add report:bat /3600 @hash:alertchan @zerohop "Bat: {value}{unit}"
```

> **Note:** Zero-hop provides no delivery guarantee — only neighbors that are actively listening at the moment of transmission will receive the message. No ACK, no retry.

`@zerohop` and `@scope:` are mutually exclusive. When `@zerohop` is set, scope is ignored.

---

## Template Tokens

The message field supports the following tokens, which are replaced at send time:

| Token | Replaced with | Example |
|---|---|---|
| `{value}` | The measured value as an integer | `3742` or `-103` |
| `{unit}` | The unit for the measured value | `mV`, `C`, or `dBm` |

The node name is always prepended automatically — there is no `{node}` token.

Temperature is always reported in °C.

If no message is provided, the default template `{value}{unit}` is used.

Example messages and resulting output (node name = `DK-REP-01`):
```
"Bat: {value}{unit}"          →  "DK-REP-01: Bat: 3742mV"
"battery low {value}{unit}"   →  "DK-REP-01: battery low 3312mV"
"noise {value}{unit}"         →  "DK-REP-01: noise -103dBm"
"temp {value}{unit}"          →  "DK-REP-01: temp 42C"
```

---

## Storage

Rules are stored in JSON format at `/report_rules.json` on the filesystem. The format is human-readable and backward compatible — new fields added in future versions will default to safe values when loading older files.

If a binary rule file from an older version is found at boot (`/report_rules`), it is automatically migrated to JSON format and the binary file is deleted.

---

## Limits

| Parameter | Value |
|---|---|
| Maximum rules | 8 |
| Minimum interval | 1800 seconds (30 minutes) |
| Rules per list page | 3 (remote CLI) |
| Remote CLI reply limit | 160 bytes |
| Message template length | 64 characters |
| Channel name / key length | 36 characters |
| Scope name length | 32 characters |

---

## Examples

```
# Report battery every hour on #alertchan
report add report:bat /3600 @hash:alertchan "Bat: {value}{unit}"

# Report battery every day at 08:00 UTC
report add report:bat /86400 at:08:00 @hash:alertchan "Bat: {value}{unit}"

# Report battery to direct neighbors only (zero-hop)
report add report:bat /3600 @hash:alertchan @zerohop "Bat: {value}{unit}"

# Report noise floor every day at 06:00 UTC with region scope
report add report:noise /86400 at:06:00 @hash:alertchan @scope:denmark "Noise: {value}{unit}"

# Alert if battery drops below 3.4V (check every 30 min)
report add bat<3400 /1800 @hash:alertchan "battery low {value}{unit}"

# Daily battery check at 07:00 UTC — only alert if low
report add bat<3400 /86400 at:07:00 @hash:alertchan "battery low {value}{unit}"

# Alert if temperature exceeds 60°C
report add temp>60 /1800 @hash:alertchan "temp high {value}{unit}"

# Alert if noise floor drops below -110 dBm (possible radio issue)
report add noise<-110 /1800 @hash:alertchan "noise low {value}{unit}"

# Report noise floor every 2 hours on a private channel
report add report:noise /7200 @private:b8da52900c881c97aea2ca8327681411 "Noise: {value}{unit}"

# List rules — page 1
report list

# List rules — page 2
report list 2

# Show full detail for rule 1
report list d1

# Force rule 1 to fire immediately for testing
report test 1

# Disable rule 2 temporarily
report disable 2

# Delete rule 1
report del 1
```

---

## Boot Message

The boot message is sent once after startup (after a 15-second delay to allow the radio to be ready). It uses the same channel options as rules — hash channel, private channel, scope and zero-hop are all supported.

### `report boot set`

```
report boot set @<channel> [@scope:<region>] [@zerohop] [message]
```

If no message is provided, defaults to `"Rebooted"`.

Examples:
```
report boot set @hash:alertchan "Rebooted"
report boot set @hash:alertchan @scope:denmark "Rebooted"
report boot set @hash:alertchan @zerohop "Rebooted"
report boot set @private:b8da52900c881c97aea2ca8327681411 "Rebooted"
```

### `report boot clear`

Disables and clears the boot message.

```
report boot clear
```

### `report boot show`

Shows the current boot message configuration.

```
report boot show
```

Example output:
```
Boot: @hash:alertchan @scope:denmark "Rebooted"
```

> **Note:** The node name is automatically prepended — e.g. `DK-REP-01: Rebooted`. The boot message does not support `{value}` or `{unit}` tokens.

### Boot message template tokens

| Token | Replaced with | Example |
|---|---|---|
| `{reason}` | Reset reason from the board | `Power-on reset`, `Panic / exception reset` |

If `{reason}` is not in the message, the reset reason is not appended. Examples:

```
report boot set @hash:alertchan "Rebooted: {reason}"
  →  "DK-REP-01: Rebooted: Power-on reset"

report boot set @hash:alertchan "Boot OK"
  →  "DK-REP-01: Boot OK"

report boot set @hash:alertchan "{reason}"
  →  "DK-REP-01: Panic / exception reset"
```

---

## Direct Messages (`@dm:`)

The `@dm:` channel type sends a direct peer-to-peer message to a specific node, identified by its pubkey hash as shown in the companion app. It can be used in `report add` and `report boot set` just like `@hash:` and `@private:`.

```
report add bat<3400 /1800 @dm:a3f2e1b4 "Battery low: {value}{unit}"
report add report:bat /3600 @dm:a3f2e1b4 "Bat: {value}{unit}"
report boot set @dm:a3f2e1b4 "Rebooted"
```

By default, DM messages are sent via flood (the node will receive it if reachable). If a direct path to the destination is known, it can be set once and reused for all `@dm:` sends.

> **Note:** If the peer is not found in the node's contact list at send time, the message is silently dropped. Run a discovery first to ensure the peer is known.

---

## DM Path

The optional DM path is a global setting that applies to all `@dm:` sends — both rules and the boot message. If no path is set, `@dm:` messages are sent via flood.

The path is a hex string of the raw path bytes as used internally by MeshCore. Each hop in the path occupies 1, 2 or 3 bytes depending on the ID mode configured on the network:

| ID mode | Bytes per hop | Max hops (16 byte limit) |
|---|---|---|
| 1-byte | 1 byte | 16 hops |
| 2-byte | 2 bytes | 8 hops |
| 3-byte | 3 bytes | 5 hops |

The path bytes can be read from a `trace` command output or from the companion app. The path is stored as-is and passed directly to `sendDirect()`.

> **Note:** The path must match the ID mode of the network. A path recorded in 1-byte mode will not work correctly on a 2-byte network.

### `report dm path set <hex>`

```
report dm path set 0a1b2c3d          ← 4 hops, 1-byte ID mode
report dm path set 0a1b2c3d4e5f6a7b  ← 4 hops, 2-byte ID mode
```

### `report dm path clear`

Clears the path — subsequent `@dm:` sends use flood.

```
report dm path clear
```

### `report dm show`

Shows the current DM path and hop count.

```
report dm show
```

Example output:
```
DM path: 0a1b2c3d (4 hops)
```

---

## Channel Type Summary

| Token | Type | Description |
|---|---|---|
| `@hash:name` | Hash channel | Public hashtag channel — `#name` |
| `@private:hexkey` | Private channel | Key-protected channel (32 hex chars) |
| `@dm:pubhash` | Direct message | Peer-to-peer to a specific node |

All three channel types support `@scope:`, `@zerohop` and `at:HH:MM` where applicable. `@zerohop` and `@dm:` can be combined — the message is sent zero-hop to the peer if they are a direct neighbor.
