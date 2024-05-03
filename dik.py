
# import os
# import sys
# import shlex
# import subprocess
# from threading import Thread

# list_cmd = ["./run_champsim.sh bimodal-no-no-no-no-drrip-1core 100 500 657.xz_s-3167B.champsimtrace.xz",
# "./run_champsim.sh bimodal-no-no-no-no-lru-1core 100 500 657.xz_s-3167B.champsimtrace.xz",
# "./run_champsim.sh bimodal-no-no-no-no-lruDistance-1core 100 500 657.xz_s-3167B.champsimtrace.xz"
 
# ]

# def func_process(cmd):
#     subprocess.run(shlex.split(cmd), shell=False, check=True)

# all_th = []
# for exe in list_cmd:
#     t = Thread(target=func_process, args=(exe,))
#     t.start()
#     all_th.append(t)

# for t in all_th:
#     t.join()
# file_path = "traces_name_spec2006"

# try:
#     # Open the file in read mode
#     with open(file_path, "r") as file:
#         # Read all lines into a list
#         lines = file.readlines()
#         print(lines)

#         # Iterate over each line and print it
#         # for line in lines:
#         #     print(line.strip())  # strip() removes trailing newline characters
# except FileNotFoundError:
#     print("File not found or cannot be opened.")
# except IOError:
#     print("Error reading the file.")

# file_path = "traces_name_spec2006"
# text_to_append = " - Appended text"

# modified_lines = []

# try:
#     with open(file_path, "r") as file:
#         for line in file:
#             # Append the text to each line
#             modified_line = line.strip() + text_to_append
#             modified_lines.append(modified_line)
# except FileNotFoundError:
#     print("File not found or cannot be opened.")
# except IOError:
#     print("Error reading the file.")

# # Now modified_lines contains all the modified lines
# for line in modified_lines:
#     print(line)
import os
import sys
import shlex
import subprocess
from threading import Thread

file_path = "traces_name_spec2006"
text_to_append = "./run_champsim.sh bimodal-no-no-no-no-GIPLR-1core 100 500 "

list_cmd = []

try:
    with open(file_path, "r") as file:
        for line in file:
            modified_line = text_to_append + line.strip()
            list_cmd.append(modified_line)
except FileNotFoundError:
    print("File not found or cannot be opened.")
except IOError:
    print("Error reading the file.")

def func_process(cmd):
    subprocess.run(shlex.split(cmd), shell=False, check=True)

all_th = []
for exe in list_cmd:
    t = Thread(target=func_process, args=(exe,))
    t.start()
    all_th.append(t)

for t in all_th:
    t.join()


