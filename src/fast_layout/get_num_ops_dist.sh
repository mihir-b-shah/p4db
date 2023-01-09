cat $1 | sed -nE 's/^.*, ([0-9]+) ops$/\1/p' | grep -v 0 | sort -n | uniq -c
