# 2026 Spring Computer Networks Lab 1

This repository contains the Linux implementation for the 2026 spring Computer Networks Lab 1. The lab focuses on data-link-layer reliable transmission protocols over the supplied simulator.

## Contents

| Target | Source | Description |
| --- | --- | --- |
| `stopwait` | `protocols/stopwait.c` | Stop-and-Wait / Alternating Bit protocol. |
| `gbn` | `protocols/gbn.c` | Go-Back-N with cumulative ACK and timeout retransmission. |
| `sr` | `protocols/sr.c` | Selective Repeat with per-frame timers and receiver buffering. |
| `sr_opt` | `protocols/sr_opt.c` | Optimized Selective Repeat with compact ACK/NAK frames and ring buffers. |

The framework files (`protocol.c`, `protocol.h`, `datalink.h`, `lprintf.c`, `crc32.c`) are provided by the lab environment. `Makefile` selects which protocol implementation is linked into the `datalink` executable.

## Build

```bash
make sr
make sr_opt
make gbn
make stopwait
```

`make` builds `sr` by default. Run `make clean` before switching targets if you want to remove the previous `datalink` executable.

## Run

Open two terminals in the repository directory and start station A before station B:

```bash
./datalink -f -u -n -t 650 a
./datalink -f -u -n -t 650 b
```

Useful options:

| Option | Meaning |
| --- | --- |
| `a` / `b` | Start station A or B. |
| `-u` | Use an error-free channel. |
| `-f` | Enable flood mode. |
| `-b <rate>` | Set bit error rate, for example `-b 1e-4`. |
| `-t <seconds>` | Set runtime. |
| `-n` | Disable log-file generation. |
| `-d <mask>` | Enable debug output. |
| `-p <port>` | Use a custom port when the default port is occupied. |

## Notes

This repository intentionally keeps only source code, build files, and small reference executables needed for the lab. Experiment reports, score sheets, generated logs, and local build outputs are excluded.
