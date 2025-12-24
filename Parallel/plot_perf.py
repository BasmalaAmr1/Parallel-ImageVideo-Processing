import pandas as pd
import matplotlib.pyplot as plt
import re

# Parse the timings file manually
data = []
with open("timings.csv") as f:
    next(f)  # skip header line
    for line in f:
        mode = re.search(r"MODE=(\w+)", line).group(1)
        n = int(re.search(r"N=(\d+)", line).group(1))
        threads_match = re.search(r"threads=(\d+)", line)
        threads = int(threads_match.group(1)) if threads_match else 1
        time_ms = float(re.search(r"time_ms=(\d+)", line).group(1))
        data.append((mode, n, threads, time_ms))

df = pd.DataFrame(data, columns=["Mode", "N", "Threads", "Time_ms"])

# Extract sequential baselines
seq = df[df["Mode"] == "SEQUENTIAL"].set_index("N")["Time_ms"]

# Compute Speedup & Efficiency
df["Speedup"] = df.apply(lambda x: seq[x["N"]] / x["Time_ms"], axis=1)
df["Efficiency"] = df["Speedup"] / df["Threads"]

# Filter OpenMP data
omp = df[df["Mode"] == "OPENMP"]

# Plot Speedup
for N, group in omp.groupby("N"):
    plt.plot(group["Threads"], group["Speedup"], marker="o", label=f"N={N}")

plt.xlabel("Threads")
plt.ylabel("Speedup")
plt.title("Speedup vs Threads")
plt.legend()
plt.grid(True)
plt.savefig("speedup_plot.png")
plt.close()

# Plot Efficiency
for N, group in omp.groupby("N"):
    plt.plot(group["Threads"], group["Efficiency"], marker="o", label=f"N={N}")

plt.xlabel("Threads")
plt.ylabel("Efficiency")
plt.title("Efficiency vs Threads")
plt.legend()
plt.grid(True)
plt.savefig("efficiency_plot.png")
plt.close()

print("✅ Plots saved: speedup_plot.png, efficiency_plot.png")
