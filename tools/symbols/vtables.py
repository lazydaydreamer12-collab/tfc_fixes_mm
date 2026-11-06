#!/usr/bin/env python3
# Read vtables from both binaries and study how their slots line up.
#  SO  : _ZTV symbols, Itanium address-point +8, slots are .text pointers.
#  DLL : MSVC RTTI  TypeDescriptor(.data ".?AV..") -> COL(.rdata) -> vtable(.rdata).
import subprocess, json, re, os, struct
from collections import defaultdict
from extract import Img
from match import load_so_names, qual_of

SO   = os.environ.get("TFC_SO", "/mnt/f/steam/steamapps/common/Half-Life/tfc/dlls/tfc.so")
DLL  = os.environ.get("TFC_DLL", "/mnt/f/steam/steamapps/common/Half-Life/tfc/dlls/tfc.dll")
HERE = os.path.dirname(os.path.abspath(__file__))
IMGBASE = 0x10000000

# ---------------- SO vtables
def so_vtables(so, so_dem):
    # _ZTV symbol -> vtable VA
    vts = {}
    for line in subprocess.check_output(["nm", SO]).decode(errors="replace").splitlines():
        p = line.split()
        if len(p) >= 3 and p[2].startswith("_ZTV"):
            vts[p[2]] = int(p[0], 16)
    # demangle vtable symbols to class names in bulk
    names = list(vts.keys())
    dem = subprocess.run(["c++filt"], input="\n".join(names), capture_output=True, text=True).stdout.splitlines()
    sym2cls = {}
    for sym, d in zip(names, dem):
        d = d.strip()
        if d.startswith("vtable for "):
            sym2cls[sym] = d[len("vtable for "):]
    out = {}  # class -> [(so_addr, name)]
    for sym, va in vts.items():
        cls = sym2cls.get(sym)
        if not cls: continue
        slots = []
        i = 0
        while True:
            w = so.u32(va + 8 + i*4)
            if w is None or not so.in_text(w):
                break
            slots.append((w, so_dem.get("%x"%w, "sub_%x"%w)))
            i += 1
            if i > 300: break
        if slots:
            out[cls] = slots
    return out

# ---------------- DLL vtables via RTTI
def dll_vtables(dll):
    # index dwords in .rdata and .data by value -> [loc_va]
    val2loc = defaultdict(list)
    for name, sva, size, off in dll.secs:
        if name not in (".rdata", ".data"): continue
        for p in range(0, size-3, 4):
            v = struct.unpack_from("<I", dll.data, off+p)[0]
            val2loc[v].append(sva + p)
    # find type descriptors: ".?AV<cls>@@" / ".?AU..." strings
    classes = {}  # td_va -> cls
    for name, sva, size, off in dll.secs:
        if name not in (".data", ".rdata"): continue
        blob = dll.data[off:off+size]
        for m in re.finditer(rb"\.\?A[VU][\w@?$]+@@", blob):
            sva_str = sva + m.start()
            td_va = sva_str - 8
            raw = m.group().decode()
            # .?AVCFoo@@ -> CFoo (strip prefix + @@ ; take leading scope token)
            body = raw[4:-2]
            cls = body.split("@")[0]
            classes[td_va] = (cls, raw)
    out = {}  # class -> [dll_addr]
    for td_va, (cls, raw) in classes.items():
        # COLs referencing this TD (pTypeDescriptor at COL+12)
        for loc in val2loc.get(td_va, []):
            col_va = loc - 12
            sig = dll.u32(col_va)
            if sig != 0:  # 32-bit COL signature is 0
                continue
            # vtables whose [-1] points to this COL
            for vloc in val2loc.get(col_va, []):
                vt_va = vloc + 4
                slots = []
                i = 0
                while True:
                    w = dll.u32(vt_va + i*4)
                    if w is None or not dll.in_text(w): break
                    slots.append(w); i += 1
                    if i > 300: break
                if len(slots) >= 1:
                    # keep the longest vtable found for the class (main vftable)
                    if cls not in out or len(slots) > len(out[cls]):
                        out[cls] = slots
    return out

def main():
    so = Img(SO); dll = Img(DLL)
    so_mang, so_dem = load_so_names()
    sv = so_vtables(so, so_dem)
    dv = dll_vtables(dll)
    matched = json.load(open(os.path.join(HERE,"matched_A.json")))  # dll_addr -> {so_addr,...}
    dll2so = {da:(v["so_addr"]) for da,v in matched.items()}

    print("SO classes with vtable:", len(sv))
    print("DLL classes with vtable:", len(dv))
    common = sorted(set(sv) & set(dv))
    print("classes in both:", len(common))

    def dtor_idx(so_slots, cls):
        bare = cls.split("::")[-1]
        for i,(a,n) in enumerate(so_slots):
            tail = n.split("::")[-1]
            if tail.startswith("~"):
                return i
        return None

    diff_hist = defaultdict(int)
    dtor_cnt = 0; both_dtor2 = 0
    examples = []
    for cls in common:
        so_slots = sv[cls]; dll_slots = dv[cls]
        d = dtor_idx(so_slots, cls)
        diff = len(so_slots) - len(dll_slots)
        diff_hist[diff] += 1
        # is the SO dtor a 2-slot pair?
        two = False
        if d is not None:
            dtor_cnt += 1
            if d+1 < len(so_slots) and so_slots[d+1][1].split("::")[-1].startswith("~"):
                two = True; both_dtor2 += 1
        if len(examples) < 20:
            examples.append((cls, len(so_slots), len(dll_slots), diff, d, two))

    print("\nslot-count diff (soN - dllN) histogram:", dict(sorted(diff_hist.items())))
    print("common classes with a ~dtor slot in SO:", dtor_cnt, "(2-slot pairs:", both_dtor2, ")")
    print("\nclass                          soN dllN diff dtorIdx 2slot")
    for cls,sn,dn,df,d,two in examples:
        print("%-30s %3d %3d %+4d  %s   %s" % (cls, sn, dn, df, d, two))

    # spot-check CBasePlayer alignment against known facts (TakeHealth 14/13, conc 63/62)
    cls = "CBasePlayer"
    if cls in sv and cls in dv:
        so_slots = sv[cls]; dll_slots = dv[cls]
        d = dtor_idx(so_slots, cls)
        print("\nCBasePlayer: soN=%d dllN=%d dtorIdx=%s" % (len(so_slots), len(dll_slots), d))
        for i,(a,n) in enumerate(so_slots):
            if any(k in n for k in ("TakeHealth","TakeConcussion","Concuss","~")):
                print("  SO slot %d = %s" % (i, n.split("::",0) and n))

if __name__ == "__main__":
    main()
