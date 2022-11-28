import subprocess
from gc import collect
import json
import os
import shlex
from os.path import exists

import shutil

inputs = [
    ('410.bwaves-945B.champsimtrace.xz','444.namd-120B.champsimtrace.xz', '445.gobmk-17B.champsimtrace.xz','447.dealII-3B.champsimtrace.xz'),
    ('433.milc-127B.champsimtrace.xz','434.zeusmp-10B.champsimtrace.xz', '435.gromacs-111B.champsimtrace.xz','436.cactusADM-1804B.champsimtrace.xz'),
    ('437.leslie3d-134B.champsimtrace.xz','429.mcf-217B.champsimtrace.xz', '410.bwaves-945B.champsimtrace.xz', '445.gobmk-17B.champsimtrace.xz')
]

allCombi=[]

def update(key, subkey, value):
    if subkey == "":
        cj[key] = value
    else:
        cj[key][subkey] = value


def combi(depth, j, bag):
    if depth == 0:
        allCombi.append(bag.copy())
        return
    for i in range(len(ways)):
        bag.append(ways[i])
        combi(depth-1, i, bag)
        bag.pop()

# block size is 64B
BLOCK_SIZE = 64

ways = [8] #llc
all_size = [0.5, 1, 2]#llc cache size in MB
cache = ['LLC']

replacement = [
    'lru', 
    # 'random', 
    # 'srrip'
]

curdir = os.getcwd()
default_file_name = "default_config.json"#read from
config_file_name = "champsim_config.json"#write to

default_file_path = os.path.join(curdir, default_file_name)
config_file_path = os.path.join(curdir, config_file_name)

print(default_file_path)
default_json_file = open(default_file_path, "r") 

cj = json.load(default_json_file)

# for i in range(len(ways)):
#     bag=[]
#     combi(3, i, bag)


result_status = []

update("num_cores", "", 4)
cj['ooo_cpu'].append(cj['ooo_cpu'][0])
cj['ooo_cpu'].append(cj['ooo_cpu'][0])
cj['ooo_cpu'].append(cj['ooo_cpu'][0])

for fol in inputs:

    folName = ""
    for wl in fol:
        folName = folName + "-" + wl.split('.')[1]
    
    #foreach trace use reaplce policy
    for replace_policy in replacement:

        #with LLC size
        for size in all_size:
            combi_str = ""
            
            frun = open("run.log", 'a')

            #no. of sets
            byteSize = size * pow(2, 20) #MB to B
            setsize = BLOCK_SIZE * ways[0] #
            sets=int(byteSize/setsize)

            #update llc
            update(cache[0], "ways", ways[0])
            update(cache[0], "sets", sets)
            update(cache[0], "replacement", replace_policy)

            #saving new setting
            json_string = json.dumps(cj)
            with open(config_file_path, 'w') as outfile:
                outfile.write(json_string)

            trace_path1 = "traces/{}".format(fol[0])
            trace_path2 = "traces/{}".format(fol[1])
            trace_path3 = "traces/{}".format(fol[2])
            trace_path4 = "traces/{}".format(fol[3])

            combi_str = "{},{},{}".format(size, replace_policy, folName)
            
            all_cmd = [
                'make clean', 
                './config.sh champsim_config.json', 
                'make -s', 
                "./bin/champsim --warmup_instructions 50000000 --simulation_instructions 200000000 {} {} {} {} --trace_name {} --policy {} --size {}".format(trace_path1, trace_path2, trace_path3, trace_path4, folName, replace_policy, size)
            ]
            
            for cmd in all_cmd:
                try:
                    with subprocess.Popen(shlex.split(cmd), stdout=subprocess.PIPE, stderr=subprocess.PIPE) as proc:
                        op, er = proc.communicate()
                        if proc.returncode < 0:
                            raise Exception("*fail*")
                except:
                    frun.write("{} {} ..fail\n".format(cmd, combi_str))
                    print("{} for {} ..fail\n".format(cmd, combi_str))
                    exit()
            frun.write("{} ..pass\n".format(combi_str))
            print("{} ..pass\n".format(combi_str))
            
            frun.close()
            
default_json_file.close()

            