import argparse
import logging
import os
import subprocess
import time
import uuid
from concurrent import futures

import grpc

import sobel_service_pb2
import sobel_service_pb2_grpc
import predict_pb2
import predict_pb2_grpc


DEFAULT_EDGE_SOBEL = os.environ.get(
    "EDGE_SOBEL_PATH",
    os.path.join("..", "Parallel", "edge_sobel"),
)


class SobelServiceServicer(sobel_service_pb2_grpc.SobelServiceServicer):
    """gRPC service that wraps the existing edge_sobel C++ binary.

    The core Sobel algorithm is *not* reimplemented here; instead this
    service invokes the pre-built Parallel/edge_sobel program.
    """

    def __init__(self, replica_id: str, edge_sobel_cmd: str) -> None:
        self._replica_id = replica_id
        self._edge_sobel_cmd = edge_sobel_cmd

    def ProcessImage(self, request, context):  # noqa: N802 (gRPC naming)
        request_id = str(uuid.uuid4())
        size = int(request.size)
        mode = request.mode or "omp"
        threads = int(request.threads or 4)

        cmd = [self._edge_sobel_cmd, mode, str(size)]
        if mode == "omp":
            cmd.append(str(threads))

        start_time = time.time()

        logging.info(
            "REQ id=%s replica=%s mode=%s size=%d threads=%d cmd=%s",
            request_id,
            self._replica_id,
            mode,
            size,
            threads,
            " ".join(cmd),
        )

        algo_ms = 0.0
        try:
            proc = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                check=True,
            )
            stdout = proc.stdout or ""
            stderr = proc.stderr or ""

            if stdout:
                logging.debug("edge_sobel stdout (id=%s):\n%s", request_id, stdout)
            if stderr:
                logging.debug("edge_sobel stderr (id=%s):\n%s", request_id, stderr)

            for line in stdout.splitlines():
                if "time_ms=" in line:
                    parts = line.strip().split("time_ms=")
                    if len(parts) == 2:
                        try:
                            algo_ms = float(parts[1])
                        except ValueError:
                            pass
                    break

        except subprocess.CalledProcessError as exc:
            latency_ms = (time.time() - start_time) * 1000.0
            logging.error(
                "REQ id=%s replica=%s FAILED after %.2f ms: returncode=%s stderr=%s",
                request_id,
                self._replica_id,
                latency_ms,
                exc.returncode,
                exc.stderr,
            )
            context.set_details(f"edge_sobel failed: {exc.stderr}")
            context.set_code(grpc.StatusCode.INTERNAL)
            return sobel_service_pb2.SobelResponse(
                size=size,
                mode=mode,
                threads=threads,
                server_elapsed_ms=latency_ms,
                algo_reported_ms=algo_ms,
                replica_id=self._replica_id,
            )

        latency_ms = (time.time() - start_time) * 1000.0

        logging.info(
            "RESP id=%s replica=%s mode=%s size=%d threads=%d latency_ms=%.2f algo_ms=%.2f",
            request_id,
            self._replica_id,
            mode,
            size,
            threads,
            latency_ms,
            algo_ms,
        )

        return sobel_service_pb2.SobelResponse(
            size=size,
            mode=mode,
            threads=threads,
            server_elapsed_ms=latency_ms,
            algo_reported_ms=algo_ms,
            replica_id=self._replica_id,
        )


class PredictServiceServicer(predict_pb2_grpc.PredictServiceServicer):
    def Predict(self, request, context):  # noqa: N802 (gRPC naming)
        start_time = time.perf_counter()
        try:
            values = [float(x) for x in request.input.split(",") if x.strip()]
            result = sum(values)
            output = str(result)
        except Exception as exc:  # invalid input format
            context.set_details(f"Invalid input: {exc}")
            context.set_code(grpc.StatusCode.INVALID_ARGUMENT)
            return predict_pb2.PredictResponse(output="", latency_ms=0.0)

        latency_ms = (time.perf_counter() - start_time) * 1000.0
        return predict_pb2.PredictResponse(output=output, latency_ms=latency_ms)

    def Health(self, request, context):  # noqa: N802 (gRPC naming)
        return predict_pb2.HealthStatus(alive=True)


def serve(port: int, replica_id: str, edge_sobel_cmd: str, max_workers: int = 4) -> None:
    server = grpc.server(
        futures.ThreadPoolExecutor(max_workers=max_workers),
        options=[
            ("grpc.keepalive_time_ms", 10000),
            ("grpc.keepalive_timeout_ms", 10000),
        ],
    )

    sobel_service_pb2_grpc.add_SobelServiceServicer_to_server(
        SobelServiceServicer(replica_id=replica_id, edge_sobel_cmd=edge_sobel_cmd),
        server,
    )

    predict_pb2_grpc.add_PredictServiceServicer_to_server(
        PredictServiceServicer(),
        server,
    )

    server.add_insecure_port(f"[::]:{port}")
    server.start()

    logging.info("Server started on port %d", port)

    try:
        while True:
            time.sleep(86400)
    except KeyboardInterrupt:
        logging.info("Shutting down replica=%s", replica_id)
        server.stop(grace=None)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Phase 3 Sobel gRPC server (replicated front-end for edge_sobel)",
    )
    parser.add_argument("--port", type=int, default=50052, help="Port to listen on.")
    parser.add_argument(
        "--replica-id",
        type=str,
        default=None,
        help="Logical replica identifier (default: replica-<port>).",
    )
    parser.add_argument(
        "--edge-sobel-cmd",
        type=str,
        default=DEFAULT_EDGE_SOBEL,
        help="Path to the existing edge_sobel binary.",
    )
    parser.add_argument(
        "--log-level",
        type=str,
        default="INFO",
        help="Logging level (DEBUG, INFO, WARNING, ERROR).",
    )
    parser.add_argument(
        "--max-workers",
        type=int,
        default=4,
        help="Maximum number of worker threads in the gRPC server thread pool.",
    )

    args = parser.parse_args()

    logging.basicConfig(
        level=getattr(logging, args.log_level.upper(), logging.INFO),
        format="%(asctime)s [%(levelname)s] %(message)s",
    )

    replica_id = args.replica_id or f"replica-{args.port}"

    edge_cmd = args.edge_sobel_cmd
    if not os.path.exists(edge_cmd) and os.name == "nt" and not edge_cmd.lower().endswith(".exe"):
        candidate = edge_cmd + ".exe"
        if os.path.exists(candidate):
            edge_cmd = candidate

    if not os.path.exists(edge_cmd):
        logging.warning(
            "edge_sobel binary '%s' does not exist yet. Compile it from Parallel/edge_sobel.cpp "
            "and/or set EDGE_SOBEL_PATH.",
            edge_cmd,
        )

    serve(
        port=args.port,
        replica_id=replica_id,
        edge_sobel_cmd=edge_cmd,
        max_workers=args.max_workers,
    )


if __name__ == "__main__":
    main()
