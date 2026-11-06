#!/bin/sh
# Rebuild the tfc.dll <-> tfc.so symbol map. Run under WSL/Linux (needs
# binutils objdump/nm/c++filt + python3). Override the binaries with
# TFC_SO / TFC_DLL; defaults point at the local Half-Life install.
cd "$(dirname "$0")"
: "${TFC_SO:=/mnt/f/steam/steamapps/common/Half-Life/tfc/dlls/tfc.so}"
: "${TFC_DLL:=/mnt/f/steam/steamapps/common/Half-Life/tfc/dlls/tfc.dll}"
export TFC_SO TFC_DLL
echo "disassembling (objdump)..."
objdump -d --no-show-raw-insn "$TFC_SO"  > so.dis
objdump -d --no-show-raw-insn "$TFC_DLL" > dll.dis
python3 extract.py
python3 pipeline.py
echo
echo "deliverables in $(pwd)/out/ :"
ls -1 out/
