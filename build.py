import subprocess
from gc import collect
import json
import os
import shlex
from os.path import exists
import sys

def configure(cmd):
    subprocess.run(shlex.split(cmd))

def make(cmd):
    subprocess.run(shlex.split(cmd))

def v1():
    a = './config.sh 1core.json'
    b = 'make TAG=_1CoreV1'
    configure(a)
    make(b)

    a = './config.sh 2core.json'
    b = 'make TAG=_2CoreV1'
    configure(a)
    make(b)

    # a = './config.sh 4core.json'
    # b = 'make TAG=_4CoreV1'
    # configure(a)
    # make(b)

def v2():
    a = './config.sh 1core.json'
    b = 'make TAG=_1CoreV2'
    configure(a)
    make(b)

    a = './config.sh 2core.json'
    b = 'make TAG=_2CoreV2'
    configure(a)
    make(b)

    # a = './config.sh 4core.json'
    # b = 'make TAG=_4CoreV2'
    # configure(a)
    # make(b)

def v3():
    a = './config.sh 1core.json'
    b = 'make TAG=_1CoreV3'
    configure(a)
    make(b)

    a = './config.sh 2core.json'
    b = 'make TAG=_2CoreV3'
    configure(a)
    make(b)

    # a = './config.sh 4core.json'
    # b = 'make TAG=_4CoreV3'
    # configure(a)
    # make(b)

def v4():
    a = './config.sh 1core.json'
    b = 'make TAG=_1CoreV4'
    configure(a)
    make(b)

    # a = './config.sh 2core.json'
    # b = 'make TAG=_2CoreV4'
    # configure(a)
    # make(b)

    # a = './config.sh 4core.json'
    # b = 'make TAG=_4CoreV3'
    # configure(a)
    # make(b)

v4()




