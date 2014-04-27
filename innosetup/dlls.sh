#!/bin/sh

get_dlls() {
  objdump --private-headers $1   | 
    awk '/DLL Name/ {print $3;}' |  # Format is "        DLL Name: KERNEL32.dll"
    xargs -n1 which              |  # search on path
    grep -iv "system32"             # no system libraries
}

(
  for DLL in $(get_dlls $1)
  do
    echo "$DLL"
    $0 $DLL
  done
) | sort -u
