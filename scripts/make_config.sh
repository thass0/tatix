#!/usr/bin/env bash

usage() {
    echo "usage: $0 [-f FORMAT] [-o OUTPUT-FILE] CONFIG-FILE" >&2
    echo "" >&2
    echo "  CONFIG-FILE: File name of config file" >&2
    echo "  FORMAT:      One of 'header', 'linker', 'nasm'" >&2
    echo "  OUTPUT-FILE: File to write the output to (optional)" >&2
    exit 1
}

# Default values
format=""
output_file=""

# Parse options
while [ $# -gt 0 ]; do
    case "$1" in
        -f)
            if [ -z "$2" ]; then
                usage
            fi
            format="$2"
            shift 2
            ;;
        -o)
            if [ -z "$2" ]; then
                usage
            fi
            output_file="$2"
            shift 2
            ;;
        -*)
            usage
            ;;
        *)
            config_file="$1"
            shift
            ;;
    esac
done

if [ -z "$config_file" ]; then
    usage
fi

if [ ! -f "$config_file" ]; then
    echo "$0: File '$config_file' not found" >&2
    exit 1
fi

if [ -z "$format" ]; then
    usage
fi

if [ "$format" = "header" ]; then
    fmt="#define %s %s"
elif [ "$format" = "linker" ]; then
    fmt="%s = %s;"
elif [ "$format" = "nasm" ]; then
    fmt="%s equ %s"
else
    echo "$0: Invalid format '$format'" >&2
    exit 1
fi

output=""

if [ "$format" = "header" ]; then
    output="#ifndef __BUILD_TX_CONFIG__
#define __BUILD_TX_CONFIG__

// Auto-generated config
"
elif [ "$format" = "linker" ]; then
    output="/* Auto-generated linker config */"
elif [ "$format" = "nasm" ]; then
    output=";; Auto-generated config"
fi

while IFS='=' read -r key value
do
    if [ -n "$key" ] && [ "${key###}" = "$key" ]; then
        output="$output
$(printf -- "$fmt" "$key" "$value")"
    fi
done < "$config_file"

if [ "$format" = "header" ]; then
    output=$output"

#endif // __BUILD_TX_CONFIG__"
fi

if [ -n "$output_file" ]; then
    echo "$output" > "$output_file"
else
    echo "$output"
fi
