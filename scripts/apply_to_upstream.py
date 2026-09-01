#!/usr/bin/env python3
import argparse, pathlib, re, shutil, sys

MARK_INCLUDE = '#include "extensions/zpaqfranz_ext.hpp"'
MARK_HOOK = '/* ZPAQFRANZ_OEC_DISPATCH */'
OLD_MARK_HOOK = '/* ZPAQFRANZ_TRUNKEC_DISPATCH */'

def find_main_match(text: str):
    m = re.search(r'\bint\s+main\s*\(', text)
    if not m:
        raise RuntimeError('cannot find int main(...)')
    return m

def find_main_open_brace(text: str) -> int:
    m = find_main_match(text)
    i = text.find('(', m.start())
    depth = 0
    in_str = in_chr = False
    esc = False
    while i < len(text):
        c = text[i]
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
            elif c=='(': depth += 1
            elif c==')':
                depth -= 1
                if depth == 0:
                    break
        i += 1
    if depth != 0:
        raise RuntimeError('unterminated main() signature')
    brace = text.find('{', i+1)
    if brace < 0:
        raise RuntimeError('cannot find opening brace for main()')
    return brace

def main():
    ap=argparse.ArgumentParser(description='Inject OEC (Optimize + Error Correction) extension into zpaqfranz monolithic source')
    ap.add_argument('upstream', help='path to upstream zpaqfranz.cpp')
    ap.add_argument('--out', help='output cpp (default: patch in place)')
    ap.add_argument('--extension-dir', default=None, help='destination extension dir (default: sibling extensions/)')
    args=ap.parse_args()

    src=pathlib.Path(args.upstream).resolve()
    if not src.exists():
        raise SystemExit(f'not found: {src}')
    out=pathlib.Path(args.out).resolve() if args.out else src
    extdir=pathlib.Path(args.extension_dir).resolve() if args.extension_dir else out.parent/'extensions'
    extdir.mkdir(parents=True, exist_ok=True)

    here=pathlib.Path(__file__).resolve().parent.parent/'src'
    shutil.copy2(here/'zfec.hpp', extdir/'zfec.hpp')
    shutil.copy2(here/'zpaqfranz_ext.hpp', extdir/'zpaqfranz_ext.hpp')

    text=src.read_text(errors='surrogateescape')
    original=text

    # IMPORTANT: never include the extension at line 1. zpaqfranz establishes
    # Windows/POSIX feature macros and compatibility wrappers before its normal
    # headers/functions. Pulling <cstdio> from zfec.hpp in before those defines
    # changes MinGW/UCRT declarations (notably fseeko) and can make upstream's
    # own fseeko(FP,int64_t,...) wrapper ambiguous. Put the extension immediately
    # before main(), after the monolith's platform compatibility layer is complete.
    include_re = re.compile(r'(?m)^[ \t]*#include[ \t]+"extensions/zpaqfranz_ext\.hpp"[ \t]*(?:\r?\n|$)')
    text = include_re.sub('', text)
    main_match = find_main_match(text)
    main_start = main_match.start()
    prefix = '' if main_start == 0 or text[main_start-1] == '\n' else '\n'
    text = text[:main_start] + prefix + MARK_INCLUDE + '\n' + text[main_start:]

    # Migrate the 0.1.x marker in place. The actual hook call is unchanged,
    # so already-patched upstream sources do not get a second dispatcher.
    if OLD_MARK_HOOK in text:
        text = text.replace(OLD_MARK_HOOK, MARK_HOOK)

    if MARK_HOOK not in text:
        brace=find_main_open_brace(text)
        hook='\n  '+MARK_HOOK+'\n  { const int zfext_rc = zfext::dispatch(argc, argv); if (zfext_rc != zfext::kNotHandled) return zfext_rc; }\n'
        text=text[:brace+1]+hook+text[brace+1:]

    changed = (text != original)
    out.write_text(text, errors='surrogateescape')
    print(f'{"patched" if changed else "already patched"}: {out}')
    print(f'extensions: {extdir}')

if __name__=='__main__':
    main()
