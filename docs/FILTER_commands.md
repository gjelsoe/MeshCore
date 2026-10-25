# MeshCore Repeater — Packet Filter Engine

The filter engine allows fine-grained control over which packets a repeater forwards. Rules are evaluated in order — the first matching rule wins. If no rule matches, the default policy (`mode`) applies.

The engine supports two independent profiles. Rules and mode are configured per profile. Only the active profile is used for packet evaluation. Profiles and the active profile selection survive reboot.

Rules are stored in `/filter_rules.json`. If the format changes between firmware versions, the file is automatically discarded and rules start fresh. A legacy `/filter_rules.bin` file is migrated automatically on first boot and then deleted.

---

## Profiles

The filter engine has two profiles. Each profile has its own set of rules and its own default mode. Only the active profile evaluates packets.

```
filter set profile 1         → switch active profile to 1 (default at first boot)
filter set profile 2         → switch active profile to 2
filter profile               → show active profile number
```

A typical use is to have profile 1 as a normal blacklist and profile 2 as a lockdown whitelist — see the lockdown example below.

---

## Commands

All commands are prefixed with `filter`. Most commands operate on the active profile by default. Prefix a command with `1` or `2` to target a specific profile regardless of which is active.

| Command | Description |
|---|---|
| `filter add <action> <field> <op> <value> [and ...]` | Add rule to active profile |
| `filter 1 add ...` / `filter 2 add ...` | Add rule to specific profile |
| `filter del <id>` | Delete rule by ID from active profile |
| `filter 1 del <id>` | Delete rule from profile 1 |
| `filter disable <id>` | Temporarily disable a rule |
| `filter enable <id>` | Re-enable a disabled rule |
| `filter list` | List active profile rules — page 0 |
| `filter list <page>` | List active profile rules — specific page (0-indexed) |
| `filter 1 list` / `filter 2 list` | List rules in specific profile |
| `filter clear` | Delete all rules in active profile |
| `filter 1 clear` / `filter 2 clear` | Delete all rules in specific profile |
| `filter mode <allow\|drop>` | Set default policy for active profile |
| `filter 1 mode drop` / `filter 2 mode allow` | Set mode for specific profile |
| `filter set profile <1\|2>` | Switch active profile |
| `filter profile` | Show active profile number |

---

## Actions

| Token | Description |
|---|---|
| `drop` | Drop the packet — do not forward |
| `allow` | Allow the packet — forward it |

---

## Fields

| Token | Matches | Operators |
|---|---|---|
| `route` | Route type of the packet | `eq` `neq` |
| `payload` | Payload type of the packet | `eq` `neq` `gt` `lt` |
| `hops` | Number of hops the packet has travelled | `eq` `neq` `gt` `lt` |
| `pathsize` | Hash size per hop entry in path (1–3 bytes) | `eq` `neq` `gt` `lt` |
| `path` | Last hop repeater hash (OR-match against a list) | `eq` `neq` |
| `channel` | Channel hash byte — OR-list (GRP_TXT and GRP_DATA only) | `eq` `neq` |
| `snr` | SNR of the received packet in whole dB | `eq` `neq` `gt` `lt` |
| `rssi` | RSSI of the received packet in dBm | `eq` `neq` `gt` `lt` |

---

## Operators

| Token | Meaning |
|---|---|
| `eq` | Equal to |
| `neq` | Not equal to |
| `gt` | Greater than |
| `lt` | Less than |

> `gt` and `lt` are not meaningful for `path` and are silently ignored.

---

## AND condition

A rule can have an optional AND condition. Both the primary and the AND condition must match for the rule to fire.

```
filter add <action> <field> <op> <value> and <field> <op> <value>
```

**Restrictions:**
- Maximum one AND condition per rule
- `path` is not supported as the AND field (it can still be the primary field)
- `channel` is not supported as the AND field (it can still be the primary field)
- The AND field must be different from the primary field

---

## Values

### `route` values

| Token | Constant |
|---|---|
| `tflood` | `ROUTE_TYPE_TRANSPORT_FLOOD` |
| `flood` | `ROUTE_TYPE_FLOOD` |
| `direct` | `ROUTE_TYPE_DIRECT` |
| `tdirect` | `ROUTE_TYPE_TRANSPORT_DIRECT` |

### `payload` values

| Token | Constant | Description |
|---|---|---|
| `req` | `PAYLOAD_TYPE_REQ` | Request |
| `resp` | `PAYLOAD_TYPE_RESPONSE` | Response |
| `txt` | `PAYLOAD_TYPE_TXT_MSG` | Direct text message |
| `ack` | `PAYLOAD_TYPE_ACK` | Acknowledgement |
| `advert` | `PAYLOAD_TYPE_ADVERT` | Node advertisement |
| `grptxt` | `PAYLOAD_TYPE_GRP_TXT` | Group text message |
| `grpdata` | `PAYLOAD_TYPE_GRP_DATA` | Group data packet |
| `anonreq` | `PAYLOAD_TYPE_ANON_REQ` | Anonymous request |
| `path` | `PAYLOAD_TYPE_PATH` | Path packet |
| `trace` | `PAYLOAD_TYPE_TRACE` | Trace packet |
| `multi` | `PAYLOAD_TYPE_MULTIPART` | Multipart packet |
| `ctrl` | `PAYLOAD_TYPE_CONTROL` | Control/discovery packet |
| `raw` | `PAYLOAD_TYPE_RAW_CUSTOM` | Raw custom packet |

Numeric values (decimal or hex) are also accepted for `payload`, e.g. `5` or `0x05`.

### `path` values

One or more hex strings separated by spaces. Hash size is determined automatically from the number of hex characters:

| Hex chars | Bytes | Example |
|---|---|---|
| 2 | 1 | `AB` |
| 4 | 2 | `ABBA` |
| 6 | 3 | `ABBA11` |

All hashes in a single rule must be the same size — mixing sizes returns an error. The size must match the hash size your network is configured to use.

The rule matches if the last hop in the packet path equals **any** of the listed hashes (OR logic).

```
filter add drop path eq AB              ← single 1-byte hash
filter add drop path eq AB CD EF        ← any of three 1-byte hashes
filter add drop path eq ABBA CDCD       ← any of two 2-byte hashes
filter add drop path eq ABBA11 CDCD22   ← any of two 3-byte hashes
```

> `path` cannot be used as an AND field. It can only be the primary field.

### `channel` values

One or more hex bytes separated by spaces, with or without `0x` prefix. The rule matches if the packet's channel hash equals **any** of the listed values (OR logic). Only applies to `grptxt` and `grpdata` packets.

```
filter add drop channel eq 0xAB             ← single channel
filter add drop channel eq 0x81 0x11 0x22   ← any of these three channels
```

> `channel` cannot be used as an AND field. It can only be the primary field.

### `snr` values

Signed integer in whole dB, e.g. `-10` or `5`. Stored internally in quarter-dB units to match the radio driver.

### `rssi` values

Signed integer in dBm, e.g. `-110`.

---

## Default mode

Each profile has its own default policy. When no rule in the active profile matches, the profile's mode applies:

| Token | Behaviour |
|---|---|
| `allow` | Forward the packet *(default at first boot for both profiles)* |
| `drop` | Drop the packet |

Use `filter mode drop` together with explicit `allow` rules to build a whitelist. Use `filter mode allow` (the default) with `drop` rules to build a blacklist.

---

## `filter list` output format

The number of rules per page is determined dynamically — longer rules (with AND) may result in fewer rules per page to stay within the packet size limit. The header shows the profile number and an `*` if it is the active profile.

```
p1* mode:allow rules:3/8
0 drop payload eq grptxt
1 drop route eq tflood and hops gt 5
2* drop snr lt -10
```

```
p2 mode:drop rules:6/8 pg1/2
0 allow payload eq advert
1 allow payload eq req
2 allow payload eq resp
-> filter 2 list 1
```

- `p1*` — profile 1, currently active (`*`)
- `p2` — profile 2, not active
- The rule ID is used with `del`, `disable`, and `enable`
- A `*` after the rule ID means the rule is currently **disabled**
- Page numbers are 0-indexed in the command, but displayed as 1-indexed in the header

---

## Examples

### Basic rules (active profile)

```
filter add drop payload eq grptxt
filter add drop route eq tflood
filter add drop hops gt 5
filter add drop snr lt -10
filter add drop rssi lt -110
filter add drop channel eq 0xAB
filter add drop channel eq 0x81 0x11 0x22
filter add drop channel eq 0x11 and hops gt 8
filter add drop snr lt -10 and rssi lt -100
filter add drop path eq AB and hops gt 4
```

### Write to a specific profile without switching

```
filter 2 add drop payload eq grptxt
filter 2 mode drop
filter 2 list
filter 2 clear
```

### Switch between profiles

```
filter set profile 2      → activate profile 2
filter set profile 1      → back to profile 1
filter profile            → show which profile is active
```

### Lockdown profile (whitelist — allow only essential packets)

Set up profile 2 as a lockdown profile that drops everything except the packets needed to maintain control over the repeater and its neighbours:

```
filter 2 mode drop
filter 2 add allow payload eq advert
filter 2 add allow payload eq req
filter 2 add allow payload eq resp
filter 2 add allow payload eq ack
filter 2 add allow payload eq path
filter 2 add allow payload eq ctrl
filter 2 enable 0
filter 2 enable 1
filter 2 enable 2
filter 2 enable 3
filter 2 enable 4
filter 2 enable 5
```

Activate lockdown:
```
filter set profile 2
```

Return to normal:
```
filter set profile 1
```

### Whitelist mode — only forward flood packets

```
filter mode drop
filter add allow route eq flood
```

### Disable a rule temporarily

```
filter disable 2
filter enable 2
```

### View, delete and clear

```
filter list           → page 0 of active profile
filter list 1         → page 1 of active profile
filter 2 list         → profile 2 rules
filter del 1          → delete rule 1 from active profile
filter 1 del 0        → delete rule 0 from profile 1
filter clear          → clear active profile
filter mode allow     → reset active profile mode
```

---

## JSON file format

Rules are stored in `/filter_rules.json`. The file is human-readable and follows this structure:

```json
{ap:1,pa:{mode:0,ra:{...},rb:{...},...},pb:{mode:1,ra:{...},...}}
```

| Key | Description |
|---|---|
| `ap` | Active profile index (0 = profile 1, 1 = profile 2) |
| `pa` | Profile 1 data |
| `pb` | Profile 2 data |
| `mode` | Default policy (0 = allow, 1 = drop) |
| `ra`..`rh` | Rules 0–7 |

Per rule keys:

| Key | Field |
|---|---|
| `act` | Action (0=drop, 1=allow) |
| `fld` | Field (0=route, 1=payload, 2=hops, 3=pathsize, 4=path, 5=channel, 6=snr, 7=rssi) |
| `op` | Operator (0=eq, 1=neq, 2=gt, 3=lt) |
| `val` | Scalar value |
| `af` | AND field (255 = no AND condition) |
| `aop` | AND operator |
| `av` | AND value |
| `ch` | Channel hashes (hex blob, 4 bytes) |
| `ch_n` | Number of channel hashes |
| `path` | Path hashes (hex blob, 12 bytes) |
| `pl` | Path hash length in bytes |
| `pn` | Number of path hashes |
| `en` | Enabled (true/false) |
| `use` | In use (true/false) |

---

## Limits

| Parameter | Value |
|---|---|
| Profiles | 2 |
| Maximum rules per profile | 8 |
| Maximum AND conditions per rule | 1 |
| Maximum path hashes per rule | 4 |
| Maximum channel hashes per rule | 4 |
| Path hash size | 1, 2 or 3 bytes (2, 4 or 6 hex chars) — all hashes in a rule must be the same size |
| `path` as AND field | Not supported |
| `channel` as AND field | Not supported |