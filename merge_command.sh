#!/bin/bash

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 <input_file> <text_to_add> <output_file>"
    exit 1
fi

input_file="$1"
text_to_add="$2"
output_file="$3"

# Check if the file exists
if [ ! -f "$input_file" ]; then
    echo "Error: Input file not found: $input_file"
    exit 1
fi

# Process the file and add the specified text before each line
sed "s|^|$text_to_add|" "$input_file" > "$output_file"

echo "Processing complete. Results saved to $output_file."

