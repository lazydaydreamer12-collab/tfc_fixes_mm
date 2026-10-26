#!/bin/sh
# Every scenario with the plugin (the fix must work) and without it (the bug
# must show). TFC_SO=/path/to/tfc.so if it is not in this folder.
cd "$(dirname "$0")"
fail=0
for s in $(./t_fixes); do
  for m in on off; do
    out=$(timeout 60 ./t_fixes "$s" "$m" 2>&1); rc=$?
    echo "$out" | grep -E '\[(PASS|FAIL)\]'
    [ $rc -ne 0 ] && { echo "  ^^ $s $m: exit $rc"; fail=1; }
  done
done
[ $fail -eq 0 ] && echo "ALL PASSED" || echo "SOMETHING FAILED"
exit $fail
