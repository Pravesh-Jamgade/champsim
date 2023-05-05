import subprocess
from gc import collect
import json
import os
import shlex
from os.path import exists
import sys

traces = [
    '471.omnetpp-188B.champsimtrace.xz',
    '429.mcf-51B.champsimtrace.xz',
    '437.leslie3d-134B.champsimtrace.xz',
    '433.milc-127B.champsimtrace.xz',
    '470.lbm-1274B.champsimtrace.xz',
    'pr-10.trace.gz',
    'bc-0.trace.gz',
    'bfs-10.trace.gz',
    'cc-13.trace.gz'
]

four = [
    [traces[0], traces[1], traces[4], traces[5]],
    [traces[0], traces[1], traces[6], traces[7]],
    [traces[0], traces[1], traces[2], traces[3]],
    [traces[2], traces[3], traces[4], traces[5]],
    [traces[2], traces[3], traces[6], traces[7]],
    [traces[4], traces[5], traces[6], traces[7]],
    [traces[0], traces[2], traces[4], traces[6]],
]

two = [
    [traces[0], traces[3]],
    [traces[1], traces[5]],
    [traces[2], traces[6]],
    [traces[3], traces[7]],
    [traces[4], traces[5]],
    [traces[5], traces[6]],
    [traces[6], traces[7]],
]

one = [
    [traces[0]],
    [traces[1]],
    [traces[2]],
    [traces[3]],
    [traces[4]],
    [traces[5]],
    [traces[6]],
]

mixes = []

def task1():
    # trace_list = f"traces/{trace} traces/{trace} traces/{trace} traces/{trace}"
    if len(sys.argv) < 5:
        print("warm, sim, workload mix [0..6], #cores  missing\n")
        exit(0)
    
    tag = ""
    path = ""
  
    if str(sys.argv[4]) == "one":
        mixes=one
    if str(sys.argv[4]) == "two":
        mixes=two
    if str(sys.argv[4]) == "four":
        mixes=four

    for trace in mixes[int(sys.argv[3])]:
        path = path + f"../traces/{trace} "
        if tag == "":
            tag = trace.split('.')[0]
        else:
            tag = tag + "-" + trace.split('.')[0]
    
    print(f"{tag}\n{path}")
    cmd = f"./bin/champsim --warmup_instructions {sys.argv[1]}000000 --simulation_instructions {sys.argv[2]}000000 {path}"
    try:
        print(f"running... {cmd}")
        subprocess.run(shlex.split(cmd))
    except subprocess.CalledProcessError as e:
        print("error: ", e.output, trace)
task1()