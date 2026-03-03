# High TPS SMPP Simulator (kqueue / epoll)

A high-performance SMPP 3.4 simulator designed for load testing ESME connectors at very high TPS.

Supports:

* macOS / BSD → `kqueue`
* Linux → `epoll`
* Non-blocking sockets
* Large kernel buffers
* Configurable DLR delay
* Efficient session handling
* Timed delivery receipts (DLR)

---

## Architecture Overview

This simulator is:

* Single-threaded event-driven
* Fully non-blocking
* Designed for high concurrency
* Optimized for high window sizes
* Suitable for TPS stress testing

### Event Loop Backend

| Platform | Event Engine |
| -------- | ------------ |
| macOS    | kqueue       |
| FreeBSD  | kqueue       |
| Linux    | epoll        |

The networking logic is identical across platforms. Only the event loop backend differs.

---

## Supported SMPP Operations

| Command          | Supported |
| ---------------- | --------- |
| bind_transceiver | ✅         |
| submit_sm        | ✅         |
| enquire_link     | ✅         |
| deliver_sm (DLR) | ✅         |

---

## Default Behavior

* Port: `2775`
* DLR delay: 2 seconds
* Max events: 4096
* Socket buffers: 4MB
* TCP_NODELAY enabled
* Fully non-blocking

---

## Directory Structure Example

```
.
├── main_kqueue.cpp     # macOS / BSD version
├── main_epoll.cpp      # Linux version
└── README.md
```

You may rename them as desired.

---

# Building

## macOS / BSD (kqueue)

```bash
g++ -O3 -std=c++11 main_kqueue.cpp -o smppserver
```

## Linux (epoll)

```bash
g++ -O3 -std=c++11 main_epoll.cpp -o smppserver
```

Optional recommended flags for performance:

```bash
g++ -O3 -march=native -flto -std=c++11 main_epoll.cpp -o smppserver
```

---

# Running

```bash
./smppserver
```

Expected output:

```
High TPS SMPP Server with DLR on 2775
```

---

# Performance Characteristics

This simulator is designed to:

* Handle thousands of concurrent connections
* Sustain high submit_sm throughput
* Respond immediately to bind and enquire_link
* Generate timed DLR asynchronously

Performance depends on:

* OS kernel tuning
* File descriptor limits
* CPU frequency
* NIC offloading
* Window size of ESME

---

# Recommended OS Tuning

## macOS

```
ulimit -n 200000
```

## Linux

```
ulimit -n 200000
sysctl -w net.core.somaxconn=65535
sysctl -w net.ipv4.tcp_tw_reuse=1
sysctl -w net.core.rmem_max=4194304
sysctl -w net.core.wmem_max=4194304
```

---

# Load Testing Example

Using curl (HTTP wrapper example if applicable):

```bash
curl -X POST http://localhost:8080/insms \
  -H "Content-Type: application/json" \
  -d '{
        "transaction_id": "TX123456789",
        "sender": "Taz",
        "destination": "+1234567890",
        "message": "Testing high TPS",
        "test": "false"
      }'
```

For SMPP load testing, use:

* smppclient tools
* custom Go SMPP connector
* jmeter SMPP plugin
* your internal connector

---

# Design Notes

* Event loop tick = 5ms
* DLR scheduling uses priority_queue
* O(N sessions) DLR scanning per tick
* Level-triggered event model
* No thread contention
* No locking

---

# Production Considerations

For extreme TPS (100k+):

* Use SO_REUSEPORT (multi-process model)
* CPU pin processes
* Consider edge-triggered epoll
* Consider io_uring (Linux only)
* Add send queue buffering
* Use hugepages (optional)

---

# Limitations

* Single-threaded
* No persistence
* No authentication validation
* Basic SMPP parsing
* No throttling logic

This is a simulator, not a production SMSC.

---

# SMPP Version

Implements partial SMPP 3.4 behavior.

---

# License

Internal testing tool. No warranty provided.

---

# Author

Tahseen Jamal

---
