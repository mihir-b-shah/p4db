cat $1 | sed -nE 's/^.*has ([0-9]+) passes$/\1/p' | grep -v 0 | sort -n | uniq -c
