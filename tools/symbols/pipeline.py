#!/usr/bin/env python3
# Full cross-symbolizer pipeline: name tfc.dll functions from tfc.so.
#   A  export-name anchoring      (conf 1.00)
#   B  vtable alignment           (conf 0.98)
#   C  fingerprint + call-graph    (conf tiered)
import subprocess, json, re, os, struct
from collections import defaultdict
from extract import Img
from match import load_so_names, qual_of, msvc_qual
from vtables import so_vtables, dll_vtables

SO  = os.environ.get("TFC_SO", "/mnt/f/steam/steamapps/common/Half-Life/tfc/dlls/tfc.so")
DLL = os.environ.get("TFC_DLL", "/mnt/f/steam/steamapps/common/Half-Life/tfc/dlls/tfc.dll")
HERE = os.path.dirname(os.path.abspath(__file__))

so_funcs = json.load(open(os.path.join(HERE,"so_funcs.json")))
dll_blob = json.load(open(os.path.join(HERE,"dll_funcs.json")))
dll_funcs = dll_blob["funcs"]
exports   = dll_blob["exports"]

def stage_c(matched, so_funcs, dll_funcs, so_mang, so_dem):
    """Fingerprint (strings/floats) + call-graph matching for unnamed funcs.
    Precision-first: greedy global 1:1 by score, with absolute + margin gates."""
    used_so = {v["so_addr"] for v in matched.values()}
    so_pool  = {a:f for a,f in so_funcs.items() if a not in used_so}
    dll_un   = {a:f for a,f in dll_funcs.items() if a not in matched}

    def index(pool, key):
        d = defaultdict(list)
        for a,f in pool.items():
            for v in f.get(key,[]): d[v].append(a)
        return d
    sstr, dstr = index(so_pool,"strefs"), index(dll_un,"strefs")
    sflt, dflt = index(so_pool,"fconst"), index(dll_un,"fconst")

    # ---- candidate pair scores from shared strings + floats
    pair = defaultdict(float)     # (da,sa) -> score
    for s, dlist in dstr.items():
        slist = sstr.get(s)
        if not slist: continue
        w = 3.0/(len(slist)+len(dlist))
        for da in dlist:
            for sa in slist:
                pair[(da,sa)] += w
    for fv, dlist in dflt.items():
        slist = sflt.get(fv)
        if not slist: continue
        w = 2.0/(len(slist)+len(dlist))
        for da in dlist:
            for sa in slist:
                pair[(da,sa)] += w
    # size-closeness + distinctive-int tie-breakers on existing candidate pairs
    for (da,sa) in list(pair.keys()):
        df, sf = dll_un[da], so_pool[sa]
        dz, sz = df.get("size",0), sf.get("size",0)
        if dz and sz:
            r = min(dz,sz)/max(dz,sz)
            if r >= 0.7: pair[(da,sa)] += 0.3*r
        di = set(i for i in df.get("iconst",[]) if 0x100 <= i < 0x7fffffff)
        si = set(i for i in sf.get("iconst",[]) if 0x100 <= i < 0x7fffffff)
        pair[(da,sa)] += 0.1*len(di & si)

    def greedy_assign(pairscore, t_abs, margin, conf_fn, how):
        # best + second score per da for the margin test
        best = {}; second = {}
        for (da,sa),sc in pairscore.items():
            b = best.get(da, (-1.0, None))
            if sc > b[0]:
                second[da] = b
                best[da] = (sc, sa)
            elif sc > second.get(da, (-1.0, None))[0]:
                second[da] = (sc, sa)
        ranked = sorted(((sc,da,sa) for (da,sa),sc in pairscore.items()), reverse=True)
        n=0
        for sc,da,sa in ranked:
            if sc < t_abs: break
            if da in matched: continue
            if sa in used_so: continue
            # margin: best for this da must beat its second by margin
            bsc,bsa = best.get(da,(-1,None))
            ssc,_   = second.get(da,(-1,None))
            if bsa != sa:          # only assign the da's own best
                continue
            if bsc - ssc < margin and bsc < 3.0:
                continue
            matched[da] = dict(so_addr=sa, mangled=so_mang.get(sa,"?"),
                               demangled=so_dem.get(sa,"?"), how=how, conf=round(conf_fn(sc),2))
            used_so.add(sa); n+=1
        return n

    def conf_fn(sc):
        if sc >= 3.0: return 0.90
        if sc >= 1.8: return 0.82
        return 0.72
    n1 = greedy_assign(pair, t_abs=1.2, margin=0.15, conf_fn=conf_fn, how="fingerprint")

    # ---- call-graph propagation rounds (each pass sees newly-assigned names)
    def dll_callnames(f):
        out=[]
        for ca in f.get("callee_addrs",[]):
            m = matched.get("%x"%ca) if isinstance(ca,int) else matched.get(ca)
            if m: out.append(m["mangled"])
        return out
    n2 = 0
    for _ in range(4):
        cg = defaultdict(float)
        dll_un2  = {a:f for a,f in dll_funcs.items() if a not in matched}
        so_pool2 = {a:f for a,f in so_funcs.items() if a not in used_so}
        cname2so = defaultdict(list)
        for a,f in so_pool2.items():
            for nm in set(f.get("callee_names",[])):
                cname2so[nm].append(a)
        for da,f in dll_un2.items():
            names = set(dll_callnames(f))
            if len(names) < 2: continue
            cnt = defaultdict(int)
            for nm in names:
                for sa in cname2so.get(nm,[]):
                    cnt[sa]+=1
            for sa,c in cnt.items():
                if c >= 2:
                    cg[(da,sa)] += 0.5*c
        got = greedy_assign(cg, t_abs=1.0, margin=0.25, conf_fn=lambda sc:0.68, how="callgraph")
        n2 += got
        if got == 0: break
    return n1 + n2

CLONE = re.compile(r"\s*\[clone[^\]]*\]")
def strip_clone(s): return CLONE.sub("", s)
def label_of(demangled):
    d = strip_clone(demangled)
    i = d.find("(")
    if i >= 0 and not d[:i].rstrip().endswith("operator"):
        d = d[:i]
    return d.strip()

def emit(matched, crossmap, addr2so, so_dem, so_mang, dll_funcs):
    out = os.path.join(HERE, "out"); os.makedirs(out, exist_ok=True)
    # ---- symbols JSON (flat)
    flat = {}
    for da, v in sorted(matched.items(), key=lambda kv:int(kv[0],16)):
        full = strip_clone(v["demangled"])
        flat["0x%s"%da] = dict(name=label_of(v["demangled"]), signature=full,
                               so_mangled=v["mangled"], so_addr="0x%s"%v["so_addr"],
                               how=v["how"], conf=v["conf"])
    folded = {}
    for da, saset in addr2so.items():
        if len(saset) > 1 and da not in matched:
            folded["0x%s"%da] = sorted({label_of(so_dem.get(s,"?")) for s in saset})
    json.dump(dict(functions=flat, icf_folded=folded), open(os.path.join(out,"tfc_dll_symbols.json"),"w"), indent=0)

    # ---- ImportSymbolsScript names file (conf >= 0.8)
    with open(os.path.join(out,"tfc_dll.names"),"w") as f:
        for k,v in flat.items():
            if v["conf"] >= 0.8:
                f.write("%s %s f\n" % (v["name"].replace(" ",""), k))

    # ---- Ghidra Jython apply script (all tiers + folded annotations)
    with open(os.path.join(out,"apply_tfc_names.py"),"w") as f:
        f.write('# Ghidra (Jython) script: apply tfc.so-derived names onto tfc.dll.\n')
        f.write('# Run from the Script Manager with tfc.dll open. conf<0.8 are labels+comments only.\n')
        f.write('from ghidra.program.model.symbol import SourceType\n')
        f.write('fm = currentProgram.getFunctionManager()\n')
        f.write('NAMES = [\n')
        for k,v in flat.items():
            nm = v["name"].replace('"','')
            f.write('  (%s, %r, %.2f, %r),\n' % (k, nm, v["conf"], v["how"]))
        f.write(']\n')
        f.write('FOLDED = [\n')
        for k,c in folded.items():
            f.write('  (%s, %r),\n' % (k, ", ".join(c)[:220]))
        f.write(']\n')
        f.write('''
for addr, name, conf, how in NAMES:
    a = toAddr(addr)
    try:
        fn = fm.getFunctionAt(a)
        if fn is None:
            fn = createFunction(a, None)
        if fn is not None and conf >= 0.8:
            fn.setName(name, SourceType.USER_DEFINED)
        else:
            createLabel(a, name.replace("::","__"), True)
    except Exception as e:
        print("skip", addr, e)
    setPlateComment(a, "tfc.so: %s  (conf %.2f, %s)" % (name, conf, how))
for addr, cands in FOLDED:
    setPlateComment(toAddr(addr), "ICF-folded (shared stub). candidates: " + cands)
print("applied %d names, %d folded notes" % (len(NAMES), len(FOLDED)))
''')

    # ---- VTABLES.md cross-map (per class: DLL slot | SO slot | name | DLL addr)
    byc = defaultdict(list)
    for cls, j, da_h, sa_h, dem in crossmap:
        byc[cls].append((j, da_h, sa_h, dem))
    with open(os.path.join(out,"VTABLES.md"),"w") as f:
        f.write("# tfc.dll <-> tfc.so vtable cross-map\n\n")
        f.write("Per class: the DLL (MSVC) vtable slot index, the method name (from tfc.so), ")
        f.write("and the DLL function address. Hook a virtual by its **DLL slot** index.\n\n")
        f.write("The DLL has one destructor slot where tfc.so has two (complete+deleting); ")
        f.write("every slot after the destructor is therefore tfc.so_slot - 1.\n\n")
        for cls in sorted(byc):
            rows = sorted(byc[cls])
            f.write("## %s  (%d virtuals)\n\n" % (cls, len(rows)))
            f.write("| DLL slot | method | DLL addr |\n|---:|---|---|\n")
            for j, da_h, sa_h, dem in rows:
                f.write("| %d | `%s` | 0x%s |\n" % (j, label_of(dem), da_h))
            f.write("\n")
    return len(flat), len(folded)

def main():
    so = Img(SO); dll = Img(DLL)
    so_mang, so_dem = load_so_names()
    # qual -> [so_addr]
    qual2so = defaultdict(list)
    for a, dem in so_dem.items():
        qual2so[qual_of(dem)].append(a)

    matched = {}  # dll_addr_hex -> dict(so_addr, mangled, demangled, how, conf)
    def put(da, sa, how, conf):
        if da in matched and matched[da]["conf"] >= conf:
            return
        matched[da] = dict(so_addr=sa, mangled=so_mang.get(sa,"?"),
                           demangled=so_dem.get(sa,"?"), how=how, conf=conf)

    # ---- Stage A: exports
    for da, mname in exports.items():
        q = msvc_qual(mname)
        c = qual2so.get(q, [])
        if len(c) == 1:
            put(da, c[0], "export", 1.00)
    nA = len(matched)

    # ---- Stage B: vtables
    sv = so_vtables(so, so_dem)
    dv = dll_vtables(dll)
    def dtor_idx(slots):
        for i,(a,n) in enumerate(slots):
            if n.split("::")[-1].startswith("~"): return i
        return None
    crossmap = []                      # (cls, dll_slot, dll_addr, so_addr, so_name)
    addr2so = defaultdict(set)         # dll_addr_hex -> {so_addr}
    for cls in set(sv) & set(dv):
        so_slots = sv[cls]; dll_slots = dv[cls]
        d = dtor_idx(so_slots); diff = len(so_slots)-len(dll_slots)
        twoslot = d is not None and d+1 < len(so_slots) and so_slots[d+1][1].split("::")[-1].startswith("~")
        mapping = {}
        if diff == 1 and twoslot:
            for j in range(len(dll_slots)):
                si = j if j < d else (d if j == d else j+1)
                if si < len(so_slots): mapping[j] = so_slots[si][0]
        elif diff == 0 and d is None:
            for j in range(len(dll_slots)):
                mapping[j] = so_slots[j][0]
        else:
            continue  # irregular: leave to Stage C
        for j, da in enumerate(dll_slots):
            sa = mapping.get(j)
            if sa is None: continue
            sa_h = "%x"%sa
            da_h = "%x"%da
            crossmap.append((cls, j, da_h, sa_h, so_dem.get(sa_h,"?")))
            addr2so[da_h].add(sa_h)

    # fold analysis: how many distinct source funcs share each DLL address
    foldhist = defaultdict(int)
    for da_h, saset in addr2so.items():
        foldhist[len(saset)] += 1
    clean = {da for da,s in addr2so.items() if len(s)==1}
    folded = {da for da,s in addr2so.items() if len(s)>1}

    # name only CLEAN (unambiguous) vtable addresses into the flat map
    vt_named = 0
    for da_h in clean:
        sa = next(iter(addr2so[da_h]))
        if da_h in matched:            # already an export addr (also possible via ICF)
            continue
        put(da_h, sa, "vtable", 0.98); vt_named += 1
    nB = len(matched)

    # ---- Stage C: fingerprint + call-graph (for the still-unnamed)
    cN = stage_c(matched, so_funcs, dll_funcs, so_mang, so_dem)
    nC = len(matched)

    sample_folded = sorted(folded, key=lambda d: len(addr2so[d]), reverse=True)[:6]
    json.dump(matched, open(os.path.join(HERE,"matched.json"),"w"))
    json.dump([list(r) for r in crossmap], open(os.path.join(HERE,"crossmap.json"),"w"))
    nflat, nfold = emit(matched, crossmap, addr2so, so_dem, so_mang, dll_funcs)
    print("emitted out/: %d flat names, %d folded notes" % (nflat, nfold))
    tot = len(dll_funcs)
    print("DLL functions total:", tot)
    print("Stage A (exports):     %4d  (%.1f%%)" % (nA, 100*nA/tot))
    print("vtable cross-map rows (class,slot):", len(crossmap))
    print("distinct DLL vtable addrs:", len(addr2so),
          " clean(1 src):", len(clean), " folded(>1 src):", len(folded))
    print("fold histogram {#src:count}:", dict(sorted(foldhist.items())))
    print("Stage B flat names added (clean only):", vt_named, " -> total named:", nB,
          "(%.1f%%)" % (100*nB/tot))
    print("Stage C fingerprint/callgraph added:", nC-nB, " -> total named:", nC,
          "(%.1f%%)" % (100*nC/tot))
    print("remaining unnamed:", tot - nC)
    print("\nmost-folded DLL addrs (addr: nsrc, insn_size, sample names):")
    for da in sample_folded:
        sz = dll_funcs.get(da,{}).get("size","?")
        names = sorted(so_dem.get(s,"?").split("(")[0] for s in addr2so[da])[:4]
        print("  %s: nsrc=%d size=%s  %s" % (da, len(addr2so[da]), sz, names))

if __name__ == "__main__":
    main()
