<<<<<<< HEAD
# Phase 3 – Fault-Tolerant Distributed Sobel Service

This Phase 3 folder adds a **fault-tolerant distributed system** on top of your
existing Sobel implementation without modifying its core algorithm.

- The existing core algorithm lives in `Parallel/edge_sobel.cpp` and its
  compiled binary `Parallel/edge_sobel`.
- Phase 3 wraps that binary in a **gRPC service** and runs **multiple replicas**.
- A Python client sends continuous requests for **≥ 60 seconds**, automatically
  rerouting when a replica fails, and logs **timestamps + latencies**.

The original C++ algorithm is preserved: the gRPC server just calls the
pre-built `edge_sobel` binary.

---

## Files in this folder

- `sobel_service.proto`  
  gRPC service definition for `SobelService`.

- `server.py`  
  gRPC server that wraps the existing `edge_sobel` binary. Can be started
  multiple times (different ports / replica IDs) to form replicated services.

- `client.py`  
  Fault-tolerant client which:
  - sends continuous `ProcessImage` requests for at least 60 seconds,
  - load-balances and **auto-retries** across replicas on failures,
  - logs timestamps and latencies to a CSV file.

- `requirements.txt`  
  Python dependencies for the distributed layer.

> Note: No changes were made to the core algorithm source files
> (`edge_sobel.cpp`, `sobel_mpi.cpp`, etc.). Phase 3 only *wraps* the existing
> code.

---

## 1. Prerequisites

- Python 3.8+ (recommended).
- A C++ compiler with OpenMP support (e.g., `g++` on Linux) to build
  `edge_sobel`.
- `pip` for installing Python packages.

From the repository root (where `Parallel-ImageVideo-Processing` lives):

```bash
cd Parallel-ImageVideo-Processing/Phase\ 3
pip install -r requirements.txt
```

On Windows, use `python` instead of `python3` and a suitable compiler
(Visual Studio) for the C++ build.

---

## 2. Build the existing Sobel binary (no algorithm changes)

From `Parallel-ImageVideo-Processing/Parallel`:

```bash
# Example on Linux / WSL
cd ../Parallel
g++ -O3 -fopenmp -o edge_sobel edge_sobel.cpp
```

This produces the binary `edge_sobel` (or `edge_sobel.exe` on Windows).

Phase 3 assumes this binary is located at:

```text
Parallel-ImageVideo-Processing/Parallel/edge_sobel
```

If you place it elsewhere, either:

- set the environment variable `EDGE_SOBEL_PATH` to point to the binary, or
- pass `--edge-sobel-cmd` to `server.py` when starting a replica.

---

## 3. Generate gRPC Python stubs

From `Parallel-ImageVideo-Processing/Phase 3`:

```bash
python -m grpc_tools.protoc \
  -I. \
  --python_out=. \
  --grpc_python_out=. \
  sobel_service.proto
```

This generates two files:

- `sobel_service_pb2.py`
- `sobel_service_pb2_grpc.py`

They are imported by `server.py` and `client.py`.

---

## 4. Running replicated Sobel services

Run **at least two** replicas of the gRPC server, each on a different port.

Example (two replicas on the same machine):

```bash
cd Parallel-ImageVideo-Processing/Phase\ 3

# Terminal 1
python server.py --port 50051 --replica-id r1 \
  --edge-sobel-cmd ../Parallel/edge_sobel --log-level INFO

# Terminal 2
python server.py --port 50052 --replica-id r2 \
  --edge-sobel-cmd ../Parallel/edge_sobel --log-level INFO
```

Each server instance:

- hosts the same `SobelService` gRPC API,
- calls the *same* underlying `edge_sobel` binary,
- logs each request with timestamps and per-request latency.

You can also run replicas on different machines by changing the `--port` and
binding/host options and then pointing the client at their hostnames.

---

## 5. Running the fault-tolerant client (≥ 60 seconds)

In a separate terminal, while the replicas are running:

```bash
cd Parallel-ImageVideo-Processing/Phase\ 3

python client.py \
  --targets localhost:50051,localhost:50052 \
  --duration 60 \
  --size 1024 \
  --mode omp \
  --threads 4 \
  --log-csv client_log.csv \
  --log-level INFO
```

What the client does:

- runs for **at least 60 seconds** (`--duration` < 60 is automatically raised
  to 60),
- sends repeated `ProcessImage` requests,
- for each request, it may perform multiple attempts if a replica fails,
- uses basic round-robin over all currently **alive** replicas,
- if a replica returns `UNAVAILABLE` or `DEADLINE_EXCEEDED`, it is marked down
  for a backoff period and another replica is tried immediately.

---

## 6. Logging: timestamps + latencies

### 6.1 Client-side logging

The client writes a CSV log file (by default `client_log.csv`) with columns:

- `client_ts_utc` – UTC timestamp when the attempt was made (ISO-8601).
- `request_index` – logical request number.
- `attempt_index` – attempt number for that request (1 = first attempt).
- `target_address` – gRPC target (e.g. `localhost:50051`).
- `success` – `True`/`False`.
- `grpc_code` – e.g. `OK`, `StatusCode.UNAVAILABLE`.
- `grpc_details` – error message, if any.
- `latency_ms_client` – round-trip latency as seen by the client.
- `server_elapsed_ms` – `server_elapsed_ms` reported by the server.
- `algo_reported_ms` – `time_ms` parsed from `edge_sobel` output, if present.
- `replica_id` – logical replica ID that processed the request.
- `size`, `mode`, `threads` – parameters passed in the request.

This satisfies the requirement to log **timestamp for each request** and
**response latency**.

### 6.2 Server-side logging

Each server replica logs (to stdout):

- request ID (UUID),
- replica ID,
- mode, size, threads,
- execution latency in milliseconds,
- optional parsed algorithm time from `edge_sobel` stdout.

You can redirect server logs to files if needed.

---

## 7. Demonstrating fault tolerance (replica failure)

1. Start at least **two** replicas as shown above.
2. Start the client for ≥ 60 seconds.
3. While the client is running, kill one replica, e.g. by pressing `Ctrl+C`
   in its terminal.

Observe:

- client logs and stdout will show that attempts to the killed replica start
  failing with `StatusCode.UNAVAILABLE`,
- the failing replica is marked as temporarily down, and the client
  **automatically reroutes** subsequent attempts to the remaining replica(s),
- the run continues without stopping until the full duration elapses.

This behavior satisfies the requirements:

- **Replication** – multiple server instances (`server.py` on different ports).
- **Auto-Retry / Re-routing** – client automatically retries requests on other
  replicas when one fails.
- **Logging** – timestamps and latencies are captured in `client_log.csv`,
  plus detailed server logs.

---

## 8. Notes and extensions

- You can point the client at MPI-based or other implementations by adapting
  `server.py` to invoke those binaries instead of `edge_sobel` (while still not
  changing their internal algorithm).
- For more realism, you can deploy each replica to a different host and use a
  real cluster network instead of localhost.
- The current implementation uses insecure gRPC channels (no TLS) for
  simplicity; production deployments should secure the channels.
=======
# Parallel-ImageVideo-Processing
Parallel image/video processing using OpenMP, MPI, CUDA, and Spark.
>>>>>>> 06d548f4bb406a35038ab2b897679446379f9906
