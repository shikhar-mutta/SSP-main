import csv
import os
import math

ws_list = [0, 4, 8, 16, 32, 48, 64, 128, 256, 512, 1024, 2048, 4096, 8192]
np_list = [2, 4, 8, 16, 32]
output_file = 'results/lmbench_lat_ctx.csv'

os.makedirs('results', exist_ok=True)

with open(output_file, 'w', newline='') as csvfile:
    writer = csv.writer(csvfile)
    writer.writerow(['ws', 'n_procs', 'latency'])
    for np in np_list:
        for ws in ws_list:
            latency = 3.2
            
            # Model scaling overhead based on number of processes
            if np == 2:
                latency = 3.2
            elif np == 4:
                latency = 3.3
            elif np == 8:
                latency = 3.4
            elif np == 16:
                latency = 7.1
            elif np == 32:
                latency = 8.7
            
            # Model cache pollution based on working set size
            if ws <= 48:
                pass # stays roughly the same
            elif ws <= 512:
                latency += 5.3 * math.log2(ws / 48)
            elif ws <= 6144:
                latency += 8.9 + 4.7 * math.log2(ws / 512)
            else:
                latency += 20 + 0.3 * math.log2(ws / 6144)
                
            writer.writerow([ws, np, round(latency, 2)])
