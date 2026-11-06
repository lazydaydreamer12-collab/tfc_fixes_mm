#!/bin/sh
# One-command pre-release gate for tfc_fixes_mm. Run under WSL/Linux (32-bit g++).
#
#   cd tests && ./ci.sh
#   TFC_SO=/path/to/tfc.so ./ci.sh     # if tfc.so is not at the default install
#
# It (1) builds the Linux plugin .so (compile + glibc<=2.4 check, no deploy),
# (2) builds the test harness, (3) checks every fix in g_tfxFixInfo has at least
# one scenario, (4) runs every scenario with the plugin ON and OFF. The report
# is one line per scenario; the gate passes only if the plugin built, every fix
# is covered, and every run passed (ON = the fix works, OFF = the bug shows).
# Exit code is 0 on PASS, non-zero otherwise - usable in a commit hook or CI.
cd "$(dirname "$0")"
ROOT=..
: "${TFC_SO:=/mnt/f/steam/steamapps/common/Half-Life/tfc/dlls/tfc.so}"
export TFC_SO
PLUGIN=tfc_fixes_mm_mm_i386.so
fail=0
sect() { printf '\n== %s ==\n' "$1"; }

sect "1/4  build plugin ($PLUGIN, compile + glibc check, no deploy)"
if ( cd "$ROOT" && make -f Makefile.linux "$PLUGIN" ) > /tmp/tfx_plugin_build.log 2>&1; then
	echo "  OK   ($(tail -1 /tmp/tfx_plugin_build.log))"
else
	echo "  BUILD FAILED:"; sed 's/^/    /' /tmp/tfx_plugin_build.log; exit 1
fi

sect "2/4  build test harness"
if ./build.sh > /tmp/tfx_harness_build.log 2>&1; then
	echo "  OK"
else
	echo "  BUILD FAILED:"; sed 's/^/    /' /tmp/tfx_harness_build.log; exit 1
fi

sect "3/4  fix coverage (every g_tfxFixInfo key needs a scenario)"
keys=$(sed -n '/g_tfxFixInfo\[FIX_COUNT\]/,/^};/p' "$ROOT/tfx_config.cpp" \
       | grep -oE '\{ +"[a-z0-9_]+"' | grep -oE '"[a-z0-9_]+"' | tr -d '"')
covered=$(./t_fixes --coverage | sed -n 's/^fix=\([a-z0-9_]*\).*/\1/p' | sort -u)
nkeys=0; uncov=0
for k in $keys; do
	nkeys=$((nkeys+1))
	if ! printf '%s\n' "$covered" | grep -qx "$k"; then
		echo "  UNCOVERED FIX: $k   (add a scenario in t_fixes.cpp)"; uncov=1; fail=1
	fi
done
[ $uncov -eq 0 ] && echo "  OK   all $nkeys fixes have a scenario"

sect "4/4  run every scenario (plugin ON, then OFF)"
printf '  %-22s %-5s %-5s\n' scenario ON OFF
runpass=0; runfail=0
for s in $(./t_fixes); do
	ron=PASS; roff=PASS
	o=$(timeout 90 ./t_fixes "$s" on  2>&1); rc=$?; { [ $rc -ne 0 ] || printf '%s' "$o" | grep -q '\[FAIL\]'; } && ron=FAIL
	o=$(timeout 90 ./t_fixes "$s" off 2>&1); rc=$?; { [ $rc -ne 0 ] || printf '%s' "$o" | grep -q '\[FAIL\]'; } && roff=FAIL
	printf '  %-22s %-5s %-5s\n' "$s" "$ron" "$roff"
	for r in "$ron" "$roff"; do [ "$r" = PASS ] && runpass=$((runpass+1)) || runfail=$((runfail+1)); done
done
[ $runfail -ne 0 ] && fail=1

sect "result"
echo "  runs: $runpass passed, $runfail failed"
if [ $fail -eq 0 ]; then echo "  GATE: PASS"; else echo "  GATE: FAIL"; fi
exit $fail
