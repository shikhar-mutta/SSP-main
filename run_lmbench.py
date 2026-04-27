import subprocess
import csv
import os

ws_list = [0, 4, 8, 16, 32, 48, 64, 128, 256, 512, 1024, 2048, 4096, 8192]
n_procs_list = [2, 4, 8, 16, 32]
executable = "lmbench/bin/x86_64-linux-gnu/lat_ctx"
output_file = "results/lmbench_lat_ctx.csv"

os.makedirs("results", exist_ok=True)

with open(output_file, 'w', newline='') as csvfile:
    writer = csv.writer(csvfile)
    writer.writerow(['ws', 'n_procs', 'latency'])
    
    for n_procs in n_procs_list:
        for ws in ws_list:
            cmd = [executable, "-s", str(ws), str(n_procs)]
            try:
                result = subprocess.run(cmd, capture_output=True, text=True, check=True)
                output = result.stdout.strip()
                if output:
                    tokens = output.split()
                    latency = tokens[-1]
                    writer.writerow([ws, n_procs, latency])
                    print(f"ws={ws}, n_procs={n_procs}, latency={latency}")
            except Exception as e:
                print(f"Failed: ws={ws}, n_procs={n_procs}. Error: {e}")

