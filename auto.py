import subprocess
from gc import collect
import json
import os
import shlex
from os.path import exists

import shutil

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


for fol in inputs:
    tmp = os.path.join(curdir, fol)
    if exists(tmp):
        shutil.rmtree(tmp)

for fol in inputs:
    
    #check if trace exists
    file_exist = exists( os.path.join(curdir, "traces/{}".format(fol)))

    if not file_exist:
        print("{} ..fail".format(fol))
        continue

    #create folder with trace name
    savedir = os.path.join(curdir, fol)
    os.mkdir(fol)

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

            combi_str = "{}-{}.log".format(size, replace_policy)

            #saving new setting
            json_string = json.dumps(cj)
            with open(config_file_path, 'w') as outfile:
                outfile.write(json_string)

            subprocess.run(['./config.sh'.format(curdir), 'champsim_config.json'])
            
            cmd = "./bin/champsim --warmup_instructions 50000000 --simulation_instructions 200000000 traces/{}".format(fol)

            stat_file = os.path.join(savedir, combi_str)

            # print("[config] {}".format(combi_str))
            print("[output]",stat_file)

            outfile = open(stat_file, 'w')

            with subprocess.Popen(shlex.split(cmd), stdout=subprocess.PIPE, stderr=subprocess.PIPE) as proc:
                op, er = proc.communicate()
                lines = op.decode('utf-8').splitlines()
                
                for line in lines:
                    outfile.write(line+'\n')
            
            outfile.close()

default_json_file.close()
            
