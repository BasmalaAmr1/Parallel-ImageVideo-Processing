import grpc
import time
from datetime import datetime

import sobel_service_pb2
import sobel_service_pb2_grpc

# إعدادات بسيطة جداً
SERVER = "localhost:50052"
NUM_REQUESTS = 5   # هنرسل 5 requests بس
SIZE = 1024
MODE = "omp"
THREADS = 4

def send_request(stub, request_id):
    request = sobel_service_pb2.SobelRequest(size=SIZE, mode=MODE, threads=THREADS)
    start = time.time()
    try:
        stub.ProcessImage(request)
        status = "success"
    except grpc.RpcError as e:
        status = f"failed: {e.code()}"
    latency = time.time() - start
    timestamp = datetime.now().strftime("%H:%M:%S.%f")
    print(f"[{timestamp}] Req {request_id} | {status} | {latency:.3f}s")

def run_load_generator():
    channel = grpc.insecure_channel(SERVER)
    stub = sobel_service_pb2_grpc.SobelServiceStub(channel)
    for i in range(1, NUM_REQUESTS + 1):
        send_request(stub, i)
        time.sleep(0.2)  # ندي 0.2 ثانية بين كل request

if __name__ == "__main__":
    run_load_generator()
