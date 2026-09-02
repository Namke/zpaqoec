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

def find_jidac_add_ranges(text: str):
    """Return textual Jidac::add(...) definitions, tolerating signature drift.

    zpaqfranz is a single translation unit and upstream has refactored add()
    repeatedly.  Do not key the deep hook to an exact no-argument signature.
    We only accept a candidate when a function body opens before any ';'.
    """
    out=[]
    for m in re.finditer(r'(?m)^[ \t]*[^\n;{}]*\bJidac::add\s*\(', text):
        line_start=m.start()
        par=text.find('(', m.start())
        if par < 0:
            continue
        depth=0; in_str=False; in_chr=False; esc=False; i=par
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
                    if depth==0:
                        break
            i+=1
        if depth != 0:
            continue
        semi=text.find(';', i+1)
        brace=text.find('{', i+1)
        if brace < 0 or (semi >= 0 and semi < brace):
            continue
        # Match the complete function body using the existing brace scanner.
        r=find_function_body_range(text, re.escape(text[line_start:brace]).replace(r'\\ ', r'[ \\t]+'))
        if r:
            out.append(r)
            continue
        # Fallback body scan when the signature contains unusual whitespace.
        d=0; in_str=False; in_chr=False; esc=False; j=brace
        while j < len(text):
            c=text[j]
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
                elif c=='{': d+=1
                elif c=='}':
                    d-=1
                    if d==0:
                        out.append((line_start,brace,j+1)); break
            j+=1
    # De-duplicate candidates that a permissive regexp found twice.
    dedup=[]; seen=set()
    for r in out:
        if r[0] not in seen:
            seen.add(r[0]); dedup.append(r)
    return dedup


def inject_commit_before_final_return(body: str):
    """Insert commit before the final return in the add() body.

    We intentionally do not hard-code `return errors;`: upstream has renamed
    locals across revisions.  The final return is the normal completed path;
    exceptions/aborts still do not publish the active IDX generation.
    """
    matches=list(re.finditer(r'(?m)^([ \t]*)return\b[^;]*;', body))
    if not matches:
        return body,0
    # Prefer the historical authoritative success return when present.
    preferred=[m for m in matches if re.search(r'\breturn\s+errors\s*;', m.group(0))]
    m=preferred[-1] if preferred else matches[-1]
    indent=m.group(1)
    hook=indent+'/* ZPAQOEC_DEEP_COMMIT */ htinv.commit();\n'
    return body[:m.start()]+hook+body[m.start():],1


def patch_deep_jidac(text: str):
    # Remove previous deep include/hooks for idempotence.  Revert only the
    # declaration token, preserving upstream constructor arguments verbatim.
    text=re.sub(r'(?m)^[ \t]*#include[ \t]+"extensions/oec_deep\.hpp"[ \t]*(?:\r?\n|$)','',text)
    text=re.sub(r'(?m)^[ \t]*/\* ZPAQOEC_DEEP_COMMIT \*/[ \t]*htinv\.commit\(\);[ \t]*(?:\r?\n|$)','',text)
    text=re.sub(r'\bOecHybridHTIndex(?=\s+htinv\s*\()', 'HTIndex', text)

    ranges=find_jidac_add_ranges(text)
    if not ranges:
        return text,0

    patched=0
    include_pos=None
    # Patch backwards so source offsets remain stable.  In normal 64.x there
    # is one add() definition, but tolerate platform/feature variants.
    for start,brace,end in reversed(ranges):
        body=text[start:end]
        # Semantic anchor: the native inverse hash index named htinv.  Replace
        # ONLY the type token; do not rewrite argument expressions from upstream.
        mm=re.search(r'\bHTIndex(?=\s+htinv\s*\()', body)
        if not mm:
            continue
        body=body[:mm.start()]+'OecHybridHTIndex'+body[mm.end():]
        body,n=inject_commit_before_final_return(body)
        if not n:
            continue
        text=text[:start]+body+text[end:]
        include_pos=start
        patched+=1

    if patched==0:
        return text,0

    # Re-locate the first patched add() after edits and include immediately
    # before it. HT/HTIndex are therefore already defined, while the adapter is
    # visible at the replacement declaration.
    first=text.find('OecHybridHTIndex')
    if first < 0:
        return text,0
    line=text.rfind('\n',0,first)+1
    # Walk back to the Jidac::add signature rather than including inside body.
    sig=text.rfind('Jidac::add',0,first)
    if sig >= 0:
        line=text.rfind('\n',0,sig)+1
    text=text[:line]+'#include "extensions/oec_deep.hpp"\n'+text[line:]
    return text,patched

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
