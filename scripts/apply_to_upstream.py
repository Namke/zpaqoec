#!/usr/bin/env python3
import argparse, pathlib, re, shutil, sys

MARK_INCLUDE = '#include "extensions/zpaqfranz_ext.hpp"'
MARK_HOOK = '/* ZPAQFRANZ_TRUNKEC_DISPATCH */'

def find_main_open_brace(text: str) -> int:
    m = re.search(r'\bint\s+main\s*\(', text)
    if not m:
        raise RuntimeError('cannot find int main(...)')
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
    ap=argparse.ArgumentParser(description='Inject trunk+EC extension into zpaqfranz monolithic source')
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
    changed=False
    if MARK_INCLUDE not in text:
        # Put it before the monolith so the extension remains isolated.
        text = MARK_INCLUDE + '\n' + text
        changed=True
    if MARK_HOOK not in text:
        brace=find_main_open_brace(text)
        hook='\n  '+MARK_HOOK+'\n  { const int zfext_rc = zfext::dispatch(argc, argv); if (zfext_rc != zfext::kNotHandled) return zfext_rc; }\n'
        text=text[:brace+1]+hook+text[brace+1:]
        changed=True
    out.write_text(text, errors='surrogateescape')
    print(f'{"patched" if changed else "already patched"}: {out}')
    print(f'extensions: {extdir}')

if __name__=='__main__':
    main()
