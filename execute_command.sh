#!/bin/bash

# This script reads commands from a file and executes them one by one

if [ $# -ne 1 ]; then
    echo "Usage: $0 <commands_file>"
    exit 1
fi

commands_file="$1"
echo "attempting to acces the file."
echo $commands_file
if [ ! -f "$commands_file" ]; then
    echo "Error: File not found: $commands_file"
    exit 1
fi

# Read and execute each command from the file
while IFS= read -r command || [[ -n "$command" ]]; do
    echo "Executing: $command"
    eval "$command"
    status=$?
    
    if [ $status -ne 0 ]; then
        echo "Error executing: $command (Exit code: $status)"
        exit $status
    fi
done < "$commands_file"

echo "All commands have been executed successfully."

