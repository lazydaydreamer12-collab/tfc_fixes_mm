#!/usr/bin/env python3
# Extract per-function fingerprints from tfc.so (ELF, symbols+DWARF) and
# tfc.dll (PE, 447 exports). Both are non-PIC / absolute-addressed, so data
# refs resolve directly. Dumps so_funcs.json / dll_funcs.json for the matcher.
import subprocess, struct, re, json, sys, os

SO  = os.environ.get("TFC_SO", "/mnt/f/steam/steamapps/common/Half-Life/tfc/dlls/tfc.so")
DLL = os.environ.get("TFC_DLL", "/mnt/f/steam/steamapps/common/Half-Life/tfc/dlls/tfc.dll")
HERE = os.path.dirname(os.path.abspath(__file__))

# ---------------------------------------------------------------- binary image
class Img:
    def __init__(self, path):
        self.path = path
        self.data = open(path, "rb").read()
        # image base + PE header offset (0 for ELF)
        self.imagebase = 0
        self._peoff = None
        if self.data[:2] == b"MZ":
            self._peoff = struct.unpack_from("<I", self.data, 0x3c)[0]
            self.imagebase = struct.unpack_from("<I", self.data, self._peoff+24+28)[0]
        self.secs = []  # (name, va, size, fileoff)
        out = subprocess.check_output(["objdump", "-h", path]).decode()
        for line in out.splitlines():
            m = re.match(r"\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)\s", line)
            if m:
                name, size, va, off = m.group(1), int(m.group(2),16), int(m.group(3),16), int(m.group(4),16)
                self.secs.append((name, va, size, off))
        self.text = next((s for s in self.secs if s[0]==".text"), None)
    def sec_of(self, va):
        for name, sva, size, off in self.secs:
            if sva and sva <= va < sva+size:
                return name
        return None
    def off_of(self, va):
        for name, sva, size, off in self.secs:
            if sva and sva <= va < sva+size:
                if name in (".bss",): return None
                return off + (va - sva)
        return None
    def in_text(self, va):
        return self.text and self.text[1] <= va < self.text[1]+self.text[2]
    def is_data(self, va):
        return self.sec_of(va) in (".rodata",".data",".data.rel.ro",".rdata",".bss")
    def cstr(self, va):
        off = self.off_of(va)
        if off is None: return None
        end = off
        d = self.data
        while end < len(d) and d[end] != 0 and end-off < 400:
            end += 1
        b = d[off:end]
        if len(b) < 4: return None
        try: s = b.decode("ascii")
        except UnicodeDecodeError: return None
        if not all(32 <= ord(c) < 127 for c in s): return None
        return s
    def u32(self, va):
        off = self.off_of(va)
        if off is None or off+4 > len(self.data): return None
        return struct.unpack_from("<I", self.data, off)[0]
    def f32(self, va):
        off = self.off_of(va)
        if off is None or off+4 > len(self.data): return None
        return struct.unpack_from("<f", self.data, off)[0]
    def f64(self, va):
        off = self.off_of(va)
        if off is None or off+8 > len(self.data): return None
        return struct.unpack_from("<d", self.data, off)[0]

# ---------------------------------------------------------------- disasm parse
LBL = re.compile(r"^([0-9a-fA-F]+) <(.+)>:$")
INS = re.compile(r"^\s*([0-9a-fA-F]+):\t(.*)$")
CALL = re.compile(r"^(?:call|calll|lcall)\s+\*?(?:0x)?([0-9a-fA-F]+)\b")
IMM  = re.compile(r"\$0x([0-9a-fA-F]+)")
# absolute memory operand: 0xADDR not preceded by $ and not followed by ( or hexdigit
ABSMEM = re.compile(r"(?<![\w$])0x([0-9a-fA-F]+)(?![0-9a-fA-F(])")

FLT32 = ("flds","fadds","fsubs","fmuls","fdivs","fcoms","fcomps","fsts","fstps","fsubrs","fdivrs","movss","addss","subss","mulss","divss","comiss","ucomiss","cvtss2sd")
FLT64 = ("fldl","faddl","fsubl","fmull","fdivl","fcoml","fcompl","fstl","fstpl","movsd","addsd","subsd","mulsd","divsd","comisd","ucomisd","cvtsd2ss")

def nice_float(v):
    if v is None: return None
    import math
    if math.isnan(v) or math.isinf(v): return None
    if v == 0: return None
    a = abs(v)
    if a < 1e-9 or a > 1e9: return None
    return round(v, 4)

def feats_from_insns(img, insns):
    """insns: list of (addr, text). Returns feature dict."""
    strefs=set(); fconst=set(); iconst=set(); coderefs=set(); callees=[]
    for addr, t in insns:
        mnem = t.split()[0] if t else ""
        m = CALL.match(t)
        if m:
            tgt = int(m.group(1),16)
            if img.in_text(tgt):
                callees.append(tgt)
            continue
        # immediates
        for hx in IMM.findall(t):
            v = int(hx,16)
            if img.in_text(v):
                coderefs.add(v)
            elif img.is_data(v):
                s = img.cstr(v)
                if s and len(s) >= 4:
                    strefs.add(s)
                else:
                    u = img.u32(v)
                    if u is not None: iconst.add(u)
                    f = nice_float(img.f32(v))
                    if f is not None: fconst.add(f)
            else:
                if 8 <= v < 0x7fffffff:
                    iconst.add(v)
        # absolute memory operands
        for hx in ABSMEM.findall(t):
            v = int(hx,16)
            if not img.is_data(v):
                continue
            s = img.cstr(v)
            if s and len(s) >= 4:
                strefs.add(s); continue
            if mnem in FLT64:
                f = nice_float(img.f64(v))
                if f is not None: fconst.add(f)
            elif mnem in FLT32:
                f = nice_float(img.f32(v))
                if f is not None: fconst.add(f)
            else:
                u = img.u32(v)
                if u is not None and u not in (0,): iconst.add(u)
                f = nice_float(img.f32(v))
                if f is not None: fconst.add(f)
    return dict(strefs=sorted(strefs), fconst=sorted(fconst),
                iconst=sorted(iconst), coderefs=sorted(coderefs), callees=callees)

def parse_so(img, dispath):
    """ELF: functions are label-delimited by objdump."""
    funcs = {}  # addr -> {name, insns}
    cur = None
    for line in open(dispath, encoding="utf-8", errors="replace"):
        line = line.rstrip("\n")
        m = LBL.match(line)
        if m:
            addr = int(m.group(1),16); name = m.group(2)
            if img.in_text(addr):
                cur = addr; funcs[addr] = {"name":name, "insns":[]}
            else:
                cur = None
            continue
        if cur is None: continue
        mi = INS.match(line)
        if mi:
            funcs[cur]["insns"].append((int(mi.group(1),16), mi.group(2)))
    return funcs

def parse_dll_stream(img, dispath):
    """PE: no labels. Return sorted list of (addr, text, is_int3) for .text."""
    stream = []
    for line in open(dispath, encoding="utf-8", errors="replace"):
        mi = INS.match(line.rstrip("\n"))
        if not mi: continue
        addr = int(mi.group(1),16); t = mi.group(2)
        if not img.in_text(addr): continue
        stream.append((addr, t))
    stream.sort()
    return stream

def pe_exports(img):
    # export-directory RVA from the PE optional header's data directory [0]
    imagebase = img.imagebase
    expdir_rva = struct.unpack_from("<I", img.data, img._peoff+24+96)[0]
    base_va = imagebase + expdir_rva
    off = img.off_of(base_va)
    d = img.data
    nfunc = struct.unpack_from("<I", d, off+20)[0]
    nname = struct.unpack_from("<I", d, off+24)[0]
    eat   = struct.unpack_from("<I", d, off+28)[0]
    enpt  = struct.unpack_from("<I", d, off+32)[0]
    eord  = struct.unpack_from("<I", d, off+36)[0]
    eat_o  = img.off_of(imagebase+eat)
    enpt_o = img.off_of(imagebase+enpt)
    eord_o = img.off_of(imagebase+eord)
    exports = {}  # va -> name
    for i in range(nname):
        name_rva = struct.unpack_from("<I", d, enpt_o+4*i)[0]
        ordi     = struct.unpack_from("<H", d, eord_o+2*i)[0]
        func_rva = struct.unpack_from("<I", d, eat_o+4*ordi)[0]
        name = img.cstr(imagebase+name_rva)
        va = imagebase+func_rva
        if name and img.in_text(va):
            exports[va] = name
    return exports

def boundaries(img, stream, extra_starts):
    """Detect DLL function starts: text start, post-int3, call targets, exports."""
    text_lo = img.text[1]; text_hi = text_lo + img.text[2]
    addrs = [a for a,_ in stream]
    aset = set(addrs)
    starts = set()
    starts.add(text_lo)
    # post-int3 padding only (MSVC pads BETWEEN functions with int3; nop is
    # used for alignment INSIDE a function, so it must not split).
    prev_int3 = False
    for a, t in stream:
        if t.startswith("int3"):
            prev_int3 = True
            continue
        if prev_int3:
            starts.add(a)
        prev_int3 = False
    # call targets
    for a, t in stream:
        m = CALL.match(t)
        if m and (t.startswith("call") or t.startswith("lcall")):
            tgt = int(m.group(1),16)
            if text_lo <= tgt < text_hi and tgt in aset:
                starts.add(tgt)
    for s in extra_starts:
        if text_lo <= s < text_hi and s in aset:
            starts.add(s)
    return sorted(starts)

def main():
    print("loading images...")
    so = Img(SO); dll = Img(DLL)
    print("SO text", hex(so.text[1]), "size", hex(so.text[2]))
    print("DLL text", hex(dll.text[1]), "size", hex(dll.text[2]))

    # ---- SO
    print("parsing SO disasm...")
    sofuncs = parse_so(so, os.path.join(HERE,"so.dis"))
    print("SO functions (text, labeled):", len(sofuncs))
    so_out = {}
    for addr, fn in sofuncs.items():
        f = feats_from_insns(so, fn["insns"])
        # resolve callees to names
        f["callee_names"] = sorted({sofuncs[c]["name"] for c in f["callees"] if c in sofuncs})
        f["size"] = len(fn["insns"])
        f.pop("callees")
        so_out["%x"%addr] = {"name":fn["name"], **f}

    # ---- DLL
    print("parsing DLL disasm...")
    stream = parse_dll_stream(dll, os.path.join(HERE,"dll.dis"))
    print("DLL text instructions:", len(stream))
    exports = pe_exports(dll)
    print("DLL exports resolved:", len(exports))
    starts = boundaries(dll, stream, set(exports.keys()))
    print("DLL detected function starts:", len(starts))
    # slice stream into functions
    idx = {a:i for i,(a,_) in enumerate(stream)}
    dll_out = {}
    start_set = set(starts)
    for si, s in enumerate(starts):
        e = starts[si+1] if si+1 < len(starts) else (dll.text[1]+dll.text[2])
        # collect insns in [s, e), dropping trailing int3/nop padding
        insns = []
        i = idx.get(s)
        if i is None: continue
        j = i
        while j < len(stream) and stream[j][0] < e:
            insns.append(stream[j]); j += 1
        while insns and (insns[-1][1].startswith("int3") or insns[-1][1].startswith("nop")):
            insns.pop()
        if not insns: continue
        f = feats_from_insns(dll, insns)
        f["size"] = len(insns)
        f["callee_addrs"] = f.pop("callees")
        dll_out["%x"%s] = {"export":exports.get(s), **f}

    json.dump(so_out, open(os.path.join(HERE,"so_funcs.json"),"w"))
    json.dump({"funcs":dll_out, "exports":{("%x"%k):v for k,v in exports.items()}},
              open(os.path.join(HERE,"dll_funcs.json"),"w"))

    # ---- stats
    so_withstr = sum(1 for v in so_out.values() if v["strefs"])
    dll_withstr = sum(1 for v in dll_out.values() if v["strefs"])
    so_withcallee = sum(1 for v in so_out.values() if v["callee_names"])
    print("\n== stats ==")
    print("SO funcs with >=1 string:", so_withstr, "/", len(so_out))
    print("SO funcs with >=1 resolved callee:", so_withcallee, "/", len(so_out))
    print("DLL funcs with >=1 string:", dll_withstr, "/", len(dll_out))
    print("DLL detected functions:", len(dll_out))
    print("DLL funcs that are exports:", sum(1 for v in dll_out.values() if v["export"]))
    # sample
    print("\n== sample DLL export function ==")
    for a,v in dll_out.items():
        if v["export"] and v["strefs"]:
            print(a, v["export"]); print("  str:", v["strefs"][:3]); print("  f:", v["fconst"][:5]); print("  i:", v["iconst"][:5])
            break
    print("\n== sample SO function (TakeHealth) ==")
    for a,v in so_out.items():
        if "TakeHealth" in v["name"]:
            print(a, v["name"]); print("  str:", v["strefs"][:3]); print("  f:", v["fconst"][:5]); print("  callees:", v["callee_names"][:5])
            break

if __name__ == "__main__":
    main()
