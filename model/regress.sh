#!/bin/sh
# regress.sh — switch51.2t 回归:收集器(不重解释判据)  [2026-10-08 建]
cd "$(dirname "$0")" || exit 1
if ! make -q >/dev/null 2>&1; then echo "[构建] 有过期目标 ⇒ 先 make"; make -s || { echo "构建失败,回归中止"; exit 1; }; fi
echo "==== switch51.2t 回归($(date '+%Y-%m-%d %H:%M'))===="
fail=0; npass=0; ntot=0
for spec in "tb_sw_bringup2" "tb_sw_bringup64" "tb_sw_l2_uni" "tb_sw_l2_flood" "tb_sw_l2_mcast" \
            "tb_sw_pfc xoff" "tb_sw_pfc gate" "tb_sw_pfc lossless" "tb_sw_pfc off" \
            "tb_sw_ecn mark" "tb_sw_ecn count" \
            "tb_sw_scale64" "tb_sw_stress_q" "tb_sw_vlan" "tb_mac_loopback" "tb_mac_lane800"; do
	ntot=$((ntot+1))
	bin=${spec%% *}; arg=${spec#* }
	if [ "$arg" = "$spec" ]; then out=$(./"$bin" 2>&1) || true; else out=$(./"$bin" "$arg" 2>&1) || true; fi
	if echo "$out" | grep -q "^TB_.* PASS"; then res="PASS"; npass=$((npass+1)); else res="FAIL"; fail=1; fi
	read=$(echo "$out" | grep -E '^TB_|^   v |^   计数|^   读数' | tail -2 | tr '\n' ' ')
	printf "%-22s %-6s %s\n" "$spec" "$res" "$read"
done
[ "$fail" != 0 ] && { echo "回归失败 ✗"; exit 1; }
echo "全部 PASS ✓(${npass}/${ntot})"
exit 0
