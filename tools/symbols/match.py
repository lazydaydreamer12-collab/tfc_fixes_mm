#!/usr/bin/env python3
# Match tfc.dll functions to tfc.so names. Stages:
#   A  export-name anchoring (MSVC demangle -> qualified name -> SO name)
#   B  vtable alignment (names virtuals that aren't exported)
#   C  fingerprint matching (strings/consts) + call-graph propagation
import subprocess, json, re, os, sys, struct
from collections import defaultdict

SO  = os.environ.get("TFC_SO", "/mnt/f/steam/steamapps/common/Half-Life/tfc/dlls/tfc.so")
HERE = os.path.dirname(os.path.abspath(__file__))
so_funcs  = json.load(open(os.path.join(HERE,"so_funcs.json")))
dll_blob  = json.load(open(os.path.join(HERE,"dll_funcs.json")))
dll_funcs = dll_blob["funcs"]
exports   = dll_blob["exports"]   # dll_addr_hex -> mangled export name

# ---- SO addr -> (mangled, demangled, qual)
def load_so_names():
    mang = {}
    for line in subprocess.check_output(["nm","--defined-only",SO]).decode(errors="replace").splitlines():
        p = line.split()
        if len(p)>=3 and p[1] in "TtWw":
            mang[p[0].lower().lstrip("0") or "0"] = p[2]
    dem = {}
    for line in subprocess.check_output(["nm","-C","--defined-only",SO]).decode(errors="replace").splitlines():
        p = line.split(" ",2)
        if len(p)>=3 and p[1] in "TtWw":
            a = p[0].lower().lstrip("0") or "0"
            dem[a] = p[2].strip()
    return mang, dem

def qual_of(demangled):
    # strip argument list and trailing qualifiers -> Scope::...::name
    s = demangled
    # cut at first '(' that is not part of operator()
    i = s.find("(")
    if i >= 0 and not s[:i].rstrip().endswith("operator"):
        s = s[:i]
    s = s.strip()
    for suf in (" const"," volatile"):
        if s.endswith(suf): s = s[:-len(suf)]
    return s.strip()

# ---- MSVC mangled -> qualified name (Scope::...::name); best-effort
SPECIAL = {"0":"$ctor","1":"$dtor","_G":"$dtor","_E":"$dtor","2":"operator new",
           "3":"operator delete","4":"operator=","5":"operator>>","6":"operator<<",
           "7":"operator!","8":"operator==","9":"operator!=","_7":"$vftable","_8":"$vbtable"}
def msvc_qual(m):
    if not m.startswith("?"):
        return m  # C export / entity-factory name
    body = m[1:]
    # special name?
    name = None
    if body.startswith("?"):
        rest = body[1:]
        key2 = rest[:2]; key1 = rest[:1]
        if rest.startswith("_") and len(rest)>=2 and rest[:2] in SPECIAL:
            name = SPECIAL[rest[:2]]; rest = rest[2:]
        elif key1 in SPECIAL:
            name = SPECIAL[key1]; rest = rest[1:]
        else:
            return m  # unknown special -> leave raw
        body = rest
    else:
        j = body.find("@")
        if j < 0: return m
        name = body[:j]; body = body[j+1:]
    # scopes until '@@'
    end = body.find("@@")
    if end < 0: return m
    scope_str = body[:end]
    scopes = [s for s in scope_str.split("@") if s and not s.startswith("?")]
    scopes = [s for s in scopes if not s.isdigit()]
    cls = scopes[0] if scopes else None
    if name == "$ctor":  name = cls or "ctor"
    elif name == "$dtor": name = "~"+(cls or "dtor")
    qn = "::".join(reversed(scopes))
    return (qn+"::"+name) if qn else name

def main():
    so_mang, so_dem = load_so_names()
    # qual -> [so_addr]
    qual2so = defaultdict(list)
    so_qual = {}
    for a, dem in so_dem.items():
        q = qual_of(dem)
        so_qual[a] = q
        qual2so[q].append(a)

    matched = {}   # dll_addr -> dict(so_addr, so_name, qual, how, conf)
    amb = 0; nomatch = 0; nomatch_names = []
    for da, mname in exports.items():
        q = msvc_qual(mname)
        cands = qual2so.get(q, [])
        if len(cands) == 1:
            sa = cands[0]
            matched[da] = dict(so_addr=sa, so_name=so_mang.get(sa,"?"),
                               qual=q, how="export", conf=1.0)
        elif len(cands) > 1:
            amb += 1
        else:
            nomatch += 1
            if len(nomatch_names) < 15: nomatch_names.append((mname,q))

    print("== Stage A: export-name anchoring ==")
    print("exports:", len(exports))
    print("unique-name matches:", len(matched))
    print("ambiguous (overloaded name, N SO candidates):", amb)
    print("no SO match:", nomatch)
    print("sample unmatched export -> qual:")
    for mn,q in nomatch_names: print("   ", mn, "->", q)
    # how many distinct qual names have overloads on SO side
    over = sum(1 for q,l in qual2so.items() if len(l)>1)
    print("SO qual names with overloads:", over)

    json.dump({k:v for k,v in matched.items()}, open(os.path.join(HERE,"matched_A.json"),"w"))

if __name__ == "__main__":
    main()
