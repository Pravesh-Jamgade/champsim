from subprocess import check_output, CalledProcessError
import shlex, subprocess
import re
from threading import Thread
import time

input_home = '/media/ubuntu/DATA/tvkalyan/DATA/gapbs/benchmark/graphs/'
inputs = [
    '-g 5',
    '-f {}amazonU.sg'.format(input_home),
    '-f {}liveJournalU.sg'.format(input_home),
    '-f {}rmatU.sg'.format(input_home),
    '-f {}orkutU.sg'.format(input_home),
    '-f {}roadU.sg'.format(input_home),
    '-f {}roadCAU.sg'.format(input_home),
    '-f {}webGoogleU.sg'.format(input_home),
    '-f {}webU.sg'.format(input_home),
    '-f {}twitterU.sg'.format(input_home),
    '-f {}urandU.sg'.format(input_home),
]
path = [
     'single_thread/./cc ',
#     'single_thread/./pr ',
     'single_thread/./tc ',
     'single_thread/./bc ',
     'single_thread/./bfs ',
     'single_thread/./cc_sv ',
#     'single_thread/./pr_spmv ',
#     'single_thread/./sssp ',
   

     'multi_thread/./cc ',
#     'multi_thread/./pr ',
     'multi_thread/./tc ',
     'multi_thread/./bc ',
     'multi_thread/./bfs ',
     'multi_thread/./cc_sv ',
#     'multi_thread/./pr_spmv ',
#     'multi_thread/./sssp ',
  
]

def func_process(exe, hint, start):
    r= subprocess.run(shlex.split(exe), shell=False, check=True, stdout=subprocess.PIPE)
    if r.returncode == 0:
        page=r.stdout.decode("utf-8") 
        match = re.search("XXX",page)
        ind = match.start()
        found_b = str(page[ind:])
        print(hint + " " +found_b + " " + str(time.clock() - start))
        #print(r.output)


all_threads = []

tag = 1
for exe in path:
    for inx in inputs:
        fileName = "output_{}".format(tag)
        hint = fileName + "   "+ exe + inx
        tmp = "../snipersim/pin_kit/pin  -t tester.so -- " + exe + inx + ' > {} '.format(fileName +'.log')

        start = time.clock()
        t = Thread(target=func_process, args=(tmp, hint, start,))
        t.start()
        all_threads.append(t)
        tag = tag + 1

for t in all_threads:
    t.join()
