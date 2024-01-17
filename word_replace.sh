#!/bin/bash

if [ $# -ne 3 ]; then
    echo "Usage: $0 <input_file> <old_word> <new_word>"
    exit 1
fi

input_file="$1"
old_word="$2"
new_word="$3"

# Check if the file exists
if [ ! -f "$input_file" ]; then
    echo "Error: File not found: $input_file"
    exit 1
fi

# Perform the replacement using sed
sed -i "s/\b${old_word}\b/${new_word}/g" "$input_file"

echo "Replacement complete. '${old_word}' replaced with '${new_word}' in '${input_file}'."

