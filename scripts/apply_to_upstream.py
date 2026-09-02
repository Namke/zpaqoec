#!/usr/bin/env python3
import argparse, pathlib, re, shutil

MARK_INCLUDE = '#include "extensions/zpaqfranz_ext.hpp"'
MARK_BRIDGE_DECL = '/* ZPAQOEC_BRIDGE_DECL */'
MARK_BRIDGE_DEF = '/* ZPAQOEC_BRIDGE_DEF */'
MARK_HOOK = '/* ZPAQFRANZ_OEC_DISPATCH */'
OLD_MARK_HOOK = '/* ZPAQFRANZ_TRUNKEC_DISPATCH */'
SENTINEL = '-777777'


def find_function_matches(text: str, name: str):
    # Intentionally textual. zpaqfranz is a monolith with platform-conditional
    # entry points. Injecting all textual main() definitions is safer than
    # guessing which one MinGW/Linux will compile.
    return list(re.finditer(r'(?m)^[ \t]*int\s+' + re.escape(name) + r'\s*\(', text))


def find_function_open_brace(text: str, match) -> int:
    i = text.find('(', match.start())
    depth = 0
    in_str = in_chr = False
    esc = False
    while i < len(text):
        c = text[i]
        if in_str:
            if esc: esc = False
            elif c == '\\': esc = True
            elif c == '"': in_str = False
        elif in_chr:
            if esc: esc = False
            elif c == '\\': esc = True
            elif c == "'": in_chr = False
        else:
            if c == '"': in_str = True
            elif c == "'": in_chr = True
            elif c == '(': depth += 1
            elif c == ')':
                depth -= 1
                if depth == 0:
                    break
        i += 1
    if depth != 0:
        raise RuntimeError('unterminated function signature')
    brace = text.find('{', i + 1)
    if brace < 0:
        raise RuntimeError('cannot find opening brace for function')
    return brace


def remove_legacy(text: str) -> str:
    include_re = re.compile(r'(?m)^[ \t]*#include[ \t]+"extensions/zpaqfranz_ext\.hpp"[ \t]*(?:\r?\n|$)')
    text = include_re.sub('', text)

    bridge_decl_re = re.compile(
        r'(?m)^[ \t]*/\* ZPAQOEC_BRIDGE_DECL \*/[ \t]*\r?\n'
        r'[ \t]*int zfext_oec_dispatch_bridge\(int argc, const char\* const\* argv\);[ \t]*(?:\r?\n|$)'
    )
    text = bridge_decl_re.sub('', text)
    bridge_def_re = re.compile(
        r'(?m)^[ \t]*/\* ZPAQOEC_BRIDGE_DEF \*/[ \t]*\r?\n'
        r'[ \t]*int zfext_oec_dispatch_bridge\(int argc, const char\* const\* argv\)[ \t]*\{[ \t]*return zfext::dispatch_const\(argc, argv\);[ \t]*\}[ \t]*(?:\r?\n|$)'
    )
    text = bridge_def_re.sub('', text)

    # Remove prior two-line dispatcher blocks robustly. Older revisions used
    # slightly different calls/sentinel expressions, so line-based removal is
    # deliberately tolerant.
    lines = text.splitlines(True)
    kept = []
    i = 0
    while i < len(lines):
        if 'ZPAQFRANZ_OEC_DISPATCH' in lines[i] or 'ZPAQFRANZ_TRUNKEC_DISPATCH' in lines[i]:
            i += 1
            if i < len(lines) and 'zfext_rc' in lines[i]:
                i += 1
            continue
        kept.append(lines[i])
        i += 1
    text = ''.join(kept)
    return text


def function_signature(text: str, match):
    brace = find_function_open_brace(text, match)
    return text[match.start():brace], brace


def signature_has_argc_argv(signature: str) -> bool:
    # Only inject a bridge call when the function really exposes argc/argv.
    # zpaqfranz contains platform-conditional `int main()` definitions with no
    # parameters; injecting `argc, argv` into those bodies breaks MinGW builds.
    return (re.search(r'\bargc\b', signature) is not None and
            re.search(r'\bargv\b', signature) is not None)


def inject_hook_at_matches(text: str, name: str, require_argc_argv: bool = True):
    matches = find_function_matches(text, name)
    # Decide eligibility before mutating text so match offsets stay meaningful.
    eligible = []
    skipped = 0
    for m in matches:
        sig, brace = function_signature(text, m)
        if require_argc_argv and not signature_has_argc_argv(sig):
            skipped += 1
            continue
        eligible.append((m, brace))

    # Insert backwards so offsets remain valid.
    count = 0
    for m, brace in reversed(eligible):
        hook = ('\n  ' + MARK_HOOK + '\n'
                '  { const int zfext_rc = zfext_oec_dispatch_bridge(argc, argv); '
                'if (zfext_rc != ' + SENTINEL + ') return zfext_rc; }\n')
        text = text[:brace + 1] + hook + text[brace + 1:]
        count += 1
    return text, count, skipped



def find_function_body_range(text: str, pattern: str):
    m=re.search(pattern,text,re.M)
    if not m: return None
    brace=text.find('{',m.end())
    if brace<0: return None
    depth=0; in_str=False; in_chr=False; esc=False
    i=brace
    while i<len(text):
        c=text[i]
        if in_str:
            if esc: esc=False
            elif c=='\\': esc=True
            elif c=='"': in_str=False
        elif in_chr:
            if esc: esc=False
            elif c=='\\': esc=True
            elif c=="'": in_chr=False
        else:
            if c=='"': in_str=True
            elif c=="'": in_chr=True
            elif c=='{': depth+=1
            elif c=='}':
                depth-=1
                if depth==0: return m.start(), brace, i+1
        i+=1
    return None

def scan_matching_brace(text: str, brace: int):
    """Return one-past matching '}' for a brace, ignoring strings/comments."""
    if brace < 0 or brace >= len(text) or text[brace] != '{':
        return None
    depth=0; i=brace
    in_str=in_chr=in_line=in_block=False; esc=False
    raw_end=None
    while i < len(text):
        c=text[i]; n=text[i+1] if i+1 < len(text) else ''
        if raw_end is not None:
            j=text.find(raw_end,i)
            if j < 0: return None
            i=j+len(raw_end); raw_end=None; continue
        if in_line:
            if c=='\n': in_line=False
        elif in_block:
            if c=='*' and n=='/': in_block=False; i+=1
        elif in_str:
            if esc: esc=False
            elif c=='\\': esc=True
            elif c=='"': in_str=False
        elif in_chr:
            if esc: esc=False
            elif c=='\\': esc=True
            elif c=="'": in_chr=False
        else:
            if c=='/' and n=='/': in_line=True; i+=1
            elif c=='/' and n=='*': in_block=True; i+=1
            elif c=='R' and n=='"':
                # Basic C++ raw-string support: R"delim(... )delim"
                op=text.find('(',i+2)
                if op >= 0 and op-i < 32:
                    delim=text[i+2:op]; raw_end=')'+delim+'"'; i=op
                else: in_str=True
            elif c=='"': in_str=True
            elif c=="'": in_chr=True
            elif c=='{': depth+=1
            elif c=='}':
                depth-=1
                if depth==0: return i+1
        i+=1
    return None


def find_jidac_add_ranges(text: str):
    """Return Jidac::add definition ranges, independent of line formatting.

    64.x is frequently reformatted/refactored.  `Jidac`, `::`, `add`, the
    argument list, qualifiers and return type are allowed to span lines.  A
    candidate is accepted only when a body brace appears before a declaration
    semicolon after the balanced argument list.
    """
    out=[]
    # Do not require Jidac::add to live on one line. This is the key 64.8 fix.
    for m in re.finditer(r'\bJidac\s*::\s*add\s*\(', text, re.M):
        par=text.find('(',m.start())
        if par < 0: continue
        depth=0; i=par; in_str=in_chr=False; esc=False
        while i < len(text):
            c=text[i]
            if in_str:
                if esc: esc=False
                elif c=='\\': esc=True
                elif c=='"': in_str=False
            elif in_chr:
                if esc: esc=False
                elif c=='\\': esc=True
                elif c=="'": in_chr=False
            else:
                if c=='"': in_str=True
                elif c=="'": in_chr=True
                elif c=='(': depth+=1
                elif c==')':
                    depth-=1
                    if depth==0: break
            i+=1
        if depth != 0: continue
        # Allow noexcept/attributes/trailing return decorations, but reject a
        # declaration if ';' is encountered before the body.
        semi=text.find(';',i+1)
        brace=text.find('{',i+1)
        if brace < 0 or (semi >= 0 and semi < brace): continue
        end=scan_matching_brace(text,brace)
        if end is None: continue
        out.append((m.start(),brace,end))
    dedup=[]; seen=set()
    for r in out:
        if r[1] not in seen:
            seen.add(r[1]); dedup.append(r)
    return dedup


def find_deep_index_decl(body: str):
    """Find the inverse fragment hash-index object used by add().

    Do not assume the historical variable name `htinv`.  Score HTIndex local
    objects by the operations required by the dedup hot path (`find` and
    `update`) and by nearby Jidac add anchors.
    """
    best=None
    for m in re.finditer(r'\bHTIndex\s+([A-Za-z_]\w*)(?:\s|/\*.*?\*/|//[^\n]*(?:\n|$))*\(', body, re.S):
        var=m.group(1)
        score=0
        if re.search(r'\b'+re.escape(var)+r'\s*\.\s*find\s*\(',body): score+=5
        if re.search(r'\b'+re.escape(var)+r'\s*\.\s*update\s*\(',body): score+=5
        if 'read_archive' in body: score+=2
        if 'writeJidacHeader' in body: score+=2
        if 'sha1result' in body: score+=1
        if 'fragment' in body: score+=1
        if best is None or score > best[0]: best=(score,m,var)
    if best and best[0] >= 10:
        return best[1],best[2]
    return (None,None)


def function_insertion_start(text: str, name_pos: int) -> int:
    """Find a safe file-scope line before a possibly multi-line signature."""
    cur=text.rfind('\n',0,name_pos)+1
    pos=cur
    # Walk up over return type, attributes and continuation lines. Stop after a
    # clear previous declaration/body/preprocessor/blank boundary.
    for _ in range(24):
        if pos <= 0: return 0
        prev_end=pos-1
        prev_start=text.rfind('\n',0,prev_end)+1
        line=text[prev_start:prev_end].strip()
        if (not line or line.startswith('#') or line.endswith(';') or
                line.endswith('}') or line.endswith(':')):
            return pos
        pos=prev_start
    return pos


def inject_commit_before_final_return(body: str, var: str):
    """Insert commit before the final normal return in the add() body."""
    matches=list(re.finditer(r'(?m)^([ \t]*)return\b[^;]*;', body))
    if not matches: return body,0
    preferred=[m for m in matches if re.search(r'\breturn\s+errors\s*;',m.group(0))]
    m=preferred[-1] if preferred else matches[-1]
    indent=m.group(1)
    hook=indent+'/* ZPAQOEC_DEEP_COMMIT */ '+var+'.commit();\n'
    return body[:m.start()]+hook+body[m.start():],1


def deep_include_position(text: str, before_pos: int) -> int:
    """Prefer inserting immediately after the native HTIndex class definition.

    This avoids every possible multi-line/macro return-type formatting issue at
    Jidac::add while guaranteeing both HT and HTIndex are already visible.
    """
    best=None
    for m in re.finditer(r'\b(?:class|struct)\s+HTIndex\b', text[:before_pos]):
        brace=text.find('{',m.end(),before_pos)
        semi=text.find(';',m.end(),before_pos)
        if brace < 0 or (semi >= 0 and semi < brace):
            continue
        end=scan_matching_brace(text,brace)
        if end is None or end > before_pos:
            continue
        s=text.find(';',end,min(before_pos,end+4096))
        if s < 0:
            continue
        best=s+1
    if best is not None:
        # Insert after the class declaration line, not between `}` and `;`.
        nl=text.find('\n',best)
        return len(text) if nl < 0 else nl+1
    return function_insertion_start(text,before_pos)

def patch_deep_jidac(text: str):
    # Idempotence across r1/r2/r3.  Revert only injected adapter declarations;
    # the native constructor expression and variable name remain untouched.
    text=re.sub(r'(?m)^[ \t]*#include[ \t]+"extensions/oec_deep\.hpp"[ \t]*(?:\r?\n|$)','',text)
    text=re.sub(r'(?m)^[ \t]*/\* ZPAQOEC_DEEP_COMMIT \*/[ \t]*[A-Za-z_]\w*[ \t]*\.[ \t]*commit\(\);[ \t]*(?:\r?\n|$)','',text)
    text=re.sub(r'\bOecHybridHTIndex\b','HTIndex',text)

    ranges=find_jidac_add_ranges(text)
    patched=[]
    # Patch backwards so source offsets remain valid.
    for name_pos,brace,end in reversed(ranges):
        body=text[name_pos:end]
        mm,var=find_deep_index_decl(body)
        if mm is None: continue
        body=body[:mm.start()]+'OecHybridHTIndex'+body[mm.start()+len('HTIndex'):]
        body,n=inject_commit_before_final_return(body,var)
        if not n: continue
        text=text[:name_pos]+body+text[end:]
        patched.append((name_pos,var))

    # Last-resort semantic fallback: do not depend on the Jidac::add symbol at
    # all.  Across 64.x, the invariant we actually need is the inverse HTIndex
    # local used for both find() and update().  Patch it only when that semantic
    # candidate is unique, otherwise refuse rather than guess.
    if not patched:
        candidates=[]
        for m in re.finditer(r'\bHTIndex\s+([A-Za-z_]\w*)(?:\s|/\*.*?\*/|//[^\n]*(?:\n|$))*\(',text,re.S):
            var=m.group(1); lo=max(0,m.start()-131072); hi=min(len(text),m.start()+524288)
            w=text[lo:hi]
            score=0
            if re.search(r'\b'+re.escape(var)+r'\s*\.\s*find\s*\(',w): score+=5
            if re.search(r'\b'+re.escape(var)+r'\s*\.\s*update\s*\(',w): score+=5
            if 'writeJidacHeader' in w: score+=2
            if 'read_archive' in w: score+=2
            if 'sha1result' in w: score+=1
            if score>=10: candidates.append((score,m,var,lo,hi))
        # Highest scoring candidate must be unique at its score. This handles
        # unrelated HTIndex uses without taking a risky first-match shortcut.
        if candidates:
            candidates.sort(key=lambda x:x[0],reverse=True)
            top=candidates[0][0]
            topc=[c for c in candidates if c[0]==top]
            if len(topc)==1:
                _,m,var,lo,hi=topc[0]
                text=text[:m.start()]+'OecHybridHTIndex'+text[m.start()+len('HTIndex'):]
                patched.append((m.start(),var))
                # Best effort explicit commit if the historical add symbol is
                # still discoverable. If not, leaving this generation
                # uncommitted is safe: next add catches up from authoritative
                # HT/.000 before opening its new active generation.
                anchors=list(re.finditer(r'Jidac\s*::\s*add\s*\(',text[lo:m.start()],re.S))
                if anchors:
                    apos=lo+anchors[-1].start()
                    brace=text.find('{',apos,m.start()+len('OecHybridHTIndex')+1)
                    if brace>=0:
                        fend=scan_matching_brace(text,brace)
                        if fend and fend>m.start():
                            body=text[apos:fend]
                            if 'ZPAQOEC_DEEP_COMMIT' not in body:
                                body,n=inject_commit_before_final_return(body,var)
                                if n: text=text[:apos]+body+text[fend:]

    if not patched:
        return text,0

    # Include after the native HTIndex class whenever possible. This remains
    # valid regardless of how the add function's return type/signature is split.
    first=min(p[0] for p in patched)
    inc=deep_include_position(text,first)
    text=text[:inc]+'#include "extensions/oec_deep.hpp"\n'+text[inc:]
    return text,len(patched)

def main():
    ap = argparse.ArgumentParser(description='Inject OEC (Optimize + Error Correction) extension into zpaqfranz monolithic source')
    ap.add_argument('upstream', help='path to upstream zpaqfranz.cpp')
    ap.add_argument('--out', help='output cpp (default: patch in place)')
    ap.add_argument('--extension-dir', default=None, help='destination extension dir (default: sibling extensions/)')
    args = ap.parse_args()

    src = pathlib.Path(args.upstream).resolve()
    if not src.exists():
        raise SystemExit(f'not found: {src}')
    out = pathlib.Path(args.out).resolve() if args.out else src
    extdir = pathlib.Path(args.extension_dir).resolve() if args.extension_dir else out.parent / 'extensions'
    extdir.mkdir(parents=True, exist_ok=True)

    here = pathlib.Path(__file__).resolve().parent.parent / 'src'
    shutil.copy2(here / 'zfec.hpp', extdir / 'zfec.hpp')
    shutil.copy2(here / 'oec_idx.hpp', extdir / 'oec_idx.hpp')
    shutil.copy2(here / 'oec_md5.hpp', extdir / 'oec_md5.hpp')
    shutil.copy2(here / 'zpaqfranz_ext.hpp', extdir / 'zpaqfranz_ext.hpp')
    shutil.copy2(here / 'oec_deep.hpp', extdir / 'oec_deep.hpp')

    text = src.read_text(errors='surrogateescape')
    original = text
    text = remove_legacy(text)
    text, deep_count = patch_deep_jidac(text)
    if deep_count == 0:
        raw_add=len(list(re.finditer(r'Jidac\s*::\s*add\s*\(', text, re.S)))
        raw_ht=len(list(re.finditer(r'\bHTIndex\s+[A-Za-z_]\w*', text)))
        print(f'OEC deep scan diagnostics: jidac_add_symbols={raw_add} native_HTIndex_locals={raw_ht}')

    internal = find_function_matches(text, 'zpaq_main_internal')
    mains = find_function_matches(text, 'main')
    if not internal and not mains:
        raise RuntimeError('cannot find zpaq_main_internal(...) or int main(...)')

    # Pure forward declaration at the top: no includes and no feature macros.
    # This allows outer main() functions to call OEC before zpaqfranz validates
    # the command, while keeping the heavy extension header late enough to not
    # perturb MinGW/UCRT platform setup.
    decl = (MARK_BRIDGE_DECL + '\n'
            'int zfext_oec_dispatch_bridge(int argc, const char* const* argv);\n')
    text = decl + text

    # Intercept eligible parser/entry functions first. The bridge only needs a
    # forward declaration at those call sites; its implementation is appended
    # at EOF below. Appending is deliberate: choosing "the first internal
    # function" can put the include/definition inside a platform #if branch,
    # making it disappear on Windows or Linux depending on source layout.

    # Intercept both the common internal parser and eligible textual outer main.
    # OEC commands return before upstream validation. Native commands see the
    # sentinel and continue unchanged. Double interception of native commands is
    # harmless; OEC commands never reach the second hook because the first returns.
    text, internal_count, internal_skipped = inject_hook_at_matches(text, 'zpaq_main_internal')
    text, main_count, main_skipped = inject_hook_at_matches(text, 'main')

    if internal_count == 0 and main_count == 0:
        raise RuntimeError('found entry functions, but none expose argc/argv for OEC dispatch')

    # Put the heavy extension implementation at file scope after the complete
    # upstream translation unit. C/C++ declarations are point-visible, so the
    # late <cstdio>/UCRT declarations cannot perturb overload resolution in
    # earlier zpaqfranz code, and the definition is outside platform branches.
    if text and not text.endswith('\n'):
        text += '\n'
    text += ('\n' + MARK_INCLUDE + '\n' + MARK_BRIDGE_DEF + '\n'
             'int zfext_oec_dispatch_bridge(int argc, const char* const* argv) { '
             'return zfext::dispatch_const(argc, argv); }\n')

    out.write_text(text, errors='surrogateescape')
    changed = (text != original)
    print(f'{"patched" if changed else "already patched"}: {out}')
    print(f'extensions: {extdir}')
    print(f'OEC deep Jidac hook: {deep_count}')
    print(f'OEC dispatch targets: main={main_count} zpaq_main_internal={internal_count} '
          f'(skipped no-argv: main={main_skipped} internal={internal_skipped})')


if __name__ == '__main__':
    main()
