# SMPP Server Simulator

A lightweight, heavy-load-safe **SMPP 3.4 simulator** written in C++.
This server is designed for functional testing, load testing, and integration testing of SMPP clients (ESME applications).

It supports:

* `bind_transceiver`
* `submit_sm`
* Automatic `deliver_sm` (DLR simulation)
* Multiple concurrent TCP sessions
* Non-blocking I/O via `select()`
* Simple delivery queue with delayed receipts

---

## Build Instructions

Compile using `g++` with C++11:

```bash
g++ -std=c++11 smscsimulator.cpp -o smppserver
```

This produces the executable:

```
./smppserver
```

---

## Run

```bash
./smppserver
```

Server listens on:

```
Port: 2775
Host: 0.0.0.0 (all interfaces)
```

Console output example:

```
SMPP Server listening on 2775
Client connected
Bind OK
Submit OK
Sending DeliverSM
```

---

## Supported SMPP Operations

### 1️⃣ BindTransceiver (0x00000009)

When the client sends a `bind_transceiver`:

* Server responds with `bind_transceiver_resp`
* System ID returned: `"SMSC"`
* Status: `ESME_ROK (0x00000000)`

---

### 2️⃣ SubmitSM (0x00000004)

When the client submits a message:

* Server responds with `submit_sm_resp`
* Message ID returned: `"abc123"`
* Delivery receipt scheduled after **3 seconds**

---

### 3️⃣ DeliverSM (0x00000005)

After 3 seconds, the simulator sends a delivery receipt:

Example receipt payload:

```
id:abc123 sub:001 dlvrd:001 submit date:2402241200 done date:2402241205 stat:DELIVRD err:000 text:HelloWorldTest12
```

This simulates a successful delivery (`stat:DELIVRD`).

---

## Architecture Overview

### Event Loop

* Uses `select()` for multiplexed I/O
* Supports up to 100 concurrent connections
* Non-blocking polling every 1 second

### Session Model

Each TCP connection gets:

```
SMPPSession
 ├── Bound state
 ├── Delivery queue
 └── PDU handler
```

### Delivery Queue

* Stores message ID + scheduled delivery time
* Checked every loop iteration
* Sends DLR when `deliverAt <= now`

---

## Heavy Load Safety

This simulator is designed to:

* Handle multiple concurrent binds
* Process high volumes of `submit_sm`
* Avoid SIGPIPE crashes
* Fully drain socket buffers with `sendAll()` / `recvAll()`
* Keep delivery scheduling independent per session

---

## Default Configuration

| Parameter  | Value       |
| ---------- | ----------- |
| Port       | 2775        |
| Bind Mode  | Transceiver |
| Message ID | `abc123`    |
| DLR Delay  | 3 seconds   |
| DLR Status | DELIVRD     |
| System ID  | SMSC        |

---

## Intended Use Cases

* SMPP client integration testing
* Load testing TPS behavior
* Window size tuning validation
* DLR handling validation
* Broker / queue stress testing
* SMPP failover logic validation

---

## Limitations

This is a **minimal simulator**, not a full SMPP stack.

Not implemented:

* EnquireLink
* Unbind
* SubmitSM error codes
* TLV parsing
* Window management
* Async read/write threads
* Real message ID generation
* Configurable DLR timing
* Bind authentication

---

## Example Testing Scenarios

### Functional Test

1. Start server
2. Connect SMPP client
3. Send bind_transceiver
4. Submit SMS
5. Verify:

   * submit_sm_resp received
   * deliver_sm received after ~3 seconds

---

### Load Test

Run your SMPP client with:

* High TPS (e.g. 100–500)
* Window size > 10
* Multiple concurrent connections

Observe:

* Stability
* Delivery timing
* No server crashes

---

## Code Structure Summary

```
main()
 ├── Accept connections
 ├── Maintain session map
 ├── Run session read handlers
 └── Trigger timed delivery events
```

Core Components:

* `sendAll()` – safe full-buffer send
* `recvAll()` – safe full-buffer read
* `DeliverQueue` – scheduled DLR manager
* `SMPPSession` – PDU handler

---

## Extension Ideas

If you want to evolve this into a more complete simulator:

* Add `enquire_link`
* Add configurable DLR delay
* Add message ID auto-increment
* Add random error simulation
* Add window control
* Add throughput limiting
* Add metrics endpoint
* Add logging levels
* Add multi-threaded epoll version

---

## Summary

This is a **simple, deterministic, high-stability SMPP simulator** ideal for:

* Backend SMS gateway testing
* TPS benchmarking
* DLR logic validation
* Client performance testing

It is intentionally minimal to remain predictable under heavy load.

