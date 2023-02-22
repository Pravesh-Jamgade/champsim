import subprocess
from gc import collect
import json
import os
import shlex
from os.path import exists

traces = [
    '429.mcf-217B.champsimtrace.xz',
    '437.leslie3d-134B.champsimtrace.xz',
    '433.milc-127B.champsimtrace.xz',
    '470.lbm-1274B.champsimtrace.xz',
    '482.sphinx3-1100B.champsimtrace.xz'
]
def task1():
    for trace in traces:
        trace_list = f"traces/{trace} traces/{trace} traces/{trace} traces/{trace} traces/{trace} traces/{trace} traces/{trace} traces/{trace}"
        cmd = f"./bin/champsim --warmup_instructions 1000000 --simulation_instructions 2000000 {trace_list} --trace_name {trace} --policy srrip --size 8"
        try:
            print(f"running... {cmd}")
            subprocess.run(shlex.split(cmd))
        except subprocess.CalledProcessError as e:
            print("error: ", e.output, trace)

task1()