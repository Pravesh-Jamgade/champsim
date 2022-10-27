import subprocess
from gc import collect
import json
import os
import shlex
from os.path import exists

import shutil
import sunau

inputs = [
    '410.bwaves-945B.champsimtrace.xz',
    '429.mcf-217B.champsimtrace.xz',
    '433.milc-127B.champsimtrace.xz',
    '434.zeusmp-10B.champsimtrace.xz',
    '435.gromacs-111B.champsimtrace.xz',
    '436.cactusADM-1804B.champsimtrace.xz',
    '437.leslie3d-134B.champsimtrace.xz',
    '444.namd-120B.champsimtrace.xz',
    '445.gobmk-17B.champsimtrace.xz',
    '447.dealII-3B.champsimtrace.xz'
]

allCombi=[]

def update(key, subkey, value):
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

replacement = ['lru', 'random', 'srrip']

curdir = os.getcwd()
default_file_name = "default_config.json"#read from
config_file_name = "champsim_config.json"#write to

default_file_path = os.path.join(curdir, default_file_name)
config_file_path = os.path.join(curdir, config_file_name)

print(default_file_path)
default_json_file = open(default_file_path, "r") 

cj = json.load(default_json_file)

for fol in inputs:
    
    #check if trace exists
    file_exist = exists( os.path.join(curdir, "traces/{}".format(fol)))

    if not file_exist:
        print("{} ..fail".format(fol))
        continue

    #foreach trace use reaplce policy
    for replace_policy in replacement:

        #with LLC size
        for size in all_size:
            combi_str = ""
            
            #no. of sets
            byteSize = size * pow(2, 20) #MB to B
            setsize = BLOCK_SIZE * ways[0] #
            sets=int(byteSize/setsize)

            #update llc
            update(cache[0], "ways", ways[0])
            update(cache[0], "sets", sets)
            update(cache[0], "replacement", replace_policy)

            combi_str = "{}-{}".format(size, replace_policy)

            #saving new setting
            json_string = json.dumps(cj)
            with open(config_file_path, 'w') as outfile:
                outfile.write(json_string)

            subprocess.run(['./config.sh'.format(curdir), 'champsim_config.json'])
            subprocess.run(['make'])
            
            trace_inital = fol.split('.')[1]
            cmd = "./bin/champsim --warmup_instructions 50000000 --simulation_instructions 20000000 traces/{} --config {}".format(fol, combi_str)

            with subprocess.Popen(shlex.split(cmd), stdout=subprocess.PIPE, stderr=subprocess.PIPE) as proc:
                op, er = proc.communicate()
         
            print("{} in {}  ..ok\n".format(combi_str, fol)) 


default_json_file.close()
            
