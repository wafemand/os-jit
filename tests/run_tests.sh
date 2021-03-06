#!/usr/bin/env bash

INTERPRETER=$(realpath ${1})
ROOT=$(pwd)

for test in $(find . ! -path . -type d -printf "%f\n"); do
    cd "${ROOT}"
    echo "--------------- ${test} ----------------"

    cd "${test}"
    if [[ -e "input.txt" ]]
    then
        INPUT="input.txt"
    else
        INPUT="/dev/stdin"
    fi

    if ! [[ -e "correct.txt" ]]
    then
        echo "NO CORRECT OUTPUT FILE"
        continue
    fi

    time ${INTERPRETER} "${test}.bf" <"${INPUT}" >"output.txt"
    
    DIFF=$(diff --strip-trailing-cr "correct.txt" "output.txt")
    if [ "$DIFF" == "" ] ; then
        echo "CORRECT"
    else
        echo "FAILED"
    fi

done
