import grpc, uuid, time, csv
import predict_pb2, predict_pb2_grpc
REPLICAS = ["localhost:50052"]
MAX_RETRIES = 3
with open("metrics.csv", "w", newline="") as f:
  w = csv.writer(f)
  w.writerow(["ts","replica","success","latency_ms"])
  for i in range(100):
    req_id = str(uuid.uuid4())
    input_data = "1,2,3,4,5"
    success = False
    start = time.time()
    for attempt in range(MAX_RETRIES):
      addr = REPLICAS[attempt % len(REPLICAS)]
      try:
        ch = grpc.insecure_channel(addr)
        stub = predict_pb2_grpc.PredictServiceStub(ch)
        r = stub.Predict(
          predict_pb2.PredictRequest(input=input_data, request_id=req_id),
          timeout=1.0
        )
        success = True
        print("Response:", r.output, "latency_ms:", r.latency_ms)
        w.writerow([time.time(), addr, True, r.latency_ms])
        break
      except grpc.RpcError:
        continue
    if not success:
      w.writerow([time.time(), None, False, (time.time()-start)*1000])
    print(i, addr, success)
    time.sleep(0.15)