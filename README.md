# cachekit

A Redis-like in-memory key-value store written in C. TCP server, RESP protocol, core data types, TTL expiration, LRU eviction, and RDB persistence.

## Overview

cachekit is a single-threaded network server that speaks the Redis Serialization Protocol (RESP). It supports strings, integers, lists, and hashes with optional TTL and configurable max memory with approximate LRU eviction. Data can be persisted to a binary snapshot file (RDB-style).

## Build and run

**Linux / macOS / WSL**

```bash
make
./cachekit -p 6380
```

**Windows (MinGW)**

```bash
make LDFLAGS="-lm -lws2_32"
./cachekit.exe -p 6380
```

**Options**

- `-p port` — listen port (default 6380)
- `-d file` — RDB snapshot path (default `dump.ckdb`)

**Verify**

```bash
redis-cli -p 6380 PING
# PONG
redis-cli -p 6380 SET foo bar
redis-cli -p 6380 GET foo
# "bar"
```

**CI** — GitHub Actions runs `make` and `make test` (gcc + clang, AddressSanitizer, Valgrind) on every push to main.

## Architecture

```
client → TCP → server (select loop) → resp_parse → command_dispatch → store
                                                                         ↓
client ← TCP ← resp_buf (response) ← command_dispatch ← store (get/set/list/hash)
```

- **Event loop**: single-threaded `select()` on the listen socket and all client sockets. Read → feed parser → parse one RESP value → dispatch command → write response. Pipelined commands are drained after each response is sent.
- **Store**: hash table (Robin Hood) for keys; values are strings, integers, linked lists, or nested hash tables. Entries carry optional expiry (ms) and last-access for LRU.
- **Eviction**: when `maxmemory` is set and exceeded, approximate LRU removes keys (random sample) until under the limit.
- **Persistence**: `SAVE` writes a binary snapshot; on startup, `persistence_load()` restores from the RDB file if present.

## Supported commands

| Command | Description |
|---------|-------------|
| PING \[message\] | PONG or echo message |
| ECHO message | Echo message |
| SET key value \[EX seconds\] | Set string, optional TTL |
| GET key | Get string |
| DEL key \[key ...\] | Delete keys |
| INCR / DECR key | Atomic integer increment/decrement |
| LPUSH / RPUSH / LPOP / RPOP key value | List operations |
| LRANGE key start stop | List range |
| LLEN key | List length |
| HSET / HGET / HDEL / HGETALL key field value | Hash operations |
| EXPIRE / TTL / PERSIST key seconds | TTL management |
| KEYS pattern | Keys matching glob pattern |
| DBSIZE / FLUSHDB | DB info and clear |
| SAVE | Sync snapshot to RDB file |
| INFO | Server info |

## Design decisions

- **select()** instead of epoll/kqueue so the same code builds and runs on Windows (Winsock) and Unix. For higher concurrency, a port to epoll (Linux) or kqueue (macOS) would be straightforward.
- **One response at a time per client** with parser drain after send so pipelined commands are handled without queuing multiple response buffers.
- **Approximate LRU** (random sampling) to avoid maintaining a global LRU list; matches Redis’s approach for bounded memory overhead.

## Limitations

- Single-threaded: one process, one core.
- No replication, no cluster, no pub/sub.
- No authentication (server listens on all interfaces; restrict with firewall or run locally).

## Benchmark

Start the server in one terminal (`./cachekit -p 6380`), then in another:

```bash
make bench
./benchmark 127.0.0.1 6380 10000 16
```

Optional args: `[host] [port] [n_SET+GET_pairs] [payload_bytes]`. Output: requests, elapsed time, ops/sec.

| Payload | cachekit (example) | Redis (reference) |
|---------|--------------------|-------------------|
| 16 B    | run `make bench`   | `redis-benchmark -t set,get -n 10000 -d 16` |
| 256 B   | `./benchmark 127.0.0.1 6380 10000 256` | same with `-d 256` |

## Config

See `cachekit.conf.example`. Options: port, RDB path, maxmemory, eviction policy. Command-line `-p` and `-d` override.

## Tests

```bash
make test
```

Unit tests: hashtable, list, store, protocol, persistence. AddressSanitizer: `make asan`. Before first push run `./scripts/verify-authors.sh`.

## License

MIT
