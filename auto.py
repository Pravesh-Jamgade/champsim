import subprocess
from gc import collect
import json
import os
import shlex

from black import replace

allCombi=[]

def update(key, subkey, value):
    cj[key][subkey] = int(value)


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

# use combination of ways 2 for each hence total 8 combination of selecting ways
ways = ['8']

# cache size in MB
all_size = [0.5, 1, 2]
cache = ['LLC']

replacement = ['lru', 'random', 'srrip']

curdir = os.getcwd()
config_file_name = "champsim_config.json"
config_file_path = os.path.join(curdir, config_file_name)
print(config_file_path)
config_json_file = open(config_file_path, "r") 

cj = json.load(config_json_file)
config_json_file.close()

# for i in range(len(ways)):
#     bag=[]
#     combi(3, i, bag)

update(cache[0], "ways", ways[0])

for replace_policy in replacement:
    for size in all_size:#iterating 8 possibilities of way
        combi_str = ""

        byteSize = size * pow(2, 20) #MB to B
        update(cache[0], "sets", byteSize/BLOCK_SIZE)
        update(cache[0], "replacement", replace_policy)

        combi_str = "{}{}".format(size, replace_policy)

        setting = "size={}, replacement_policy={}".format(size, replace_policy)

        print("[config] {},{},{}".format(setting, combi_str, replace_policy))

        #saving new setting
        json_string = json.dumps(cj)
        with open(config_file_path, 'w') as outfile:
            outfile.write(json_string)

        subprocess.run(['./config.sh'.format(curdir), 'champsim_config.json'])
        
        cmd = "./bin/champsim --warmup_instructions 50000000 --simulation_instructions 200000000 traces/410.bwaves-945B.champsimtrace.xz"

        combi_str = combi_str + '.log'
    
        stat_file = os.path.join(curdir, combi_str)

        print("[output]",stat_file)

        outfile = open(stat_file, 'w')

        with subprocess.Popen(shlex.split(cmd), stdout=subprocess.PIPE, stderr=subprocess.PIPE) as proc:
            op, er = proc.communicate()
            lines = op.decode('utf-8').splitlines()
            
            for line in lines:
                outfile.write(line+'\n')
        
        outfile.close()

            
