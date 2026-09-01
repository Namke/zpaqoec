#!/usr/bin/env python3
import argparse, pathlib, re, shutil

MARK_INCLUDE = '#include "extensions/zpaqfranz_ext.hpp"'
MARK_HOOK = '/* ZPAQFRANZ_OEC_DISPATCH */'
OLD_MARK_HOOK = '/* ZPAQFRANZ_TRUNKEC_DISPATCH */'


def find_function_match(text: str, name: str):
    m = re.search(r'\bint\s+' + re.escape(name) + r'\s*\(', text)
    if not m:
        return None
    return m


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
    shutil.copy2(here / 'zpaqfranz_ext.hpp', extdir / 'zpaqfranz_ext.hpp')

    text = src.read_text(errors='surrogateescape')
    original = text

    # Fast idempotent path for an already-correct 0.2.1+ patch. Require the
    # include immediately before the common internal entry and the dispatcher
    # marker after that entry starts; this deliberately rejects the old
    # first-main placement used by 0.2.0.
    clean_target = find_function_match(text, 'zpaq_main_internal')
    if clean_target is not None and OLD_MARK_HOOK not in text and text.count(MARK_INCLUDE) == 1 and text.count(MARK_HOOK) == 1:
        inc = text.find(MARK_INCLUDE)
        inc_end = inc + len(MARK_INCLUDE)
        hook = text.find(MARK_HOOK)
        if inc >= 0 and inc < clean_target.start() and text[inc_end:clean_target.start()].strip() == '' and hook > clean_target.start():
            print(f'already patched: {out}')
            print(f'extensions: {extdir}')
            print('OEC dispatch target: zpaq_main_internal')
            return

    # Remove OEC includes from any legacy location. The extension must not be
    # injected at line 1 because that changes MinGW/UCRT feature declarations.
    include_re = re.compile(r'(?m)^[ \t]*#include[ \t]+"extensions/zpaqfranz_ext\.hpp"[ \t]*(?:\r?\n|$)')
    text = include_re.sub('', text)

    # Remove dispatcher blocks emitted by all previous overlay revisions. 0.2.0
    # targeted the first int main(), which can be a platform-conditional entry
    # in the zpaqfranz monolith. The real common command path is
    # zpaq_main_internal(), so relocate the hook there.
    hook_re = re.compile(
        r'(?m)^[ \t]*/\* ZPAQFRANZ_(?:OEC|TRUNKEC)_DISPATCH \*/[ \t]*\r?\n'
        r'[ \t]*\{[ \t]*const int zfext_rc = zfext::dispatch\(argc, argv\);[ \t]*'
        r'if \(zfext_rc != zfext::kNotHandled\) return zfext_rc;[ \t]*\}[ \t]*(?:\r?\n|$)'
    )
    text = hook_re.sub('', text)
    text = text.replace(OLD_MARK_HOOK, MARK_HOOK)

    target = find_function_match(text, 'zpaq_main_internal')
    target_name = 'zpaq_main_internal'
    if target is None:
        # Small test harnesses or old upstream snapshots may not expose the
        # common internal entry. Keep a safe fallback for those only.
        target = find_function_match(text, 'main')
        target_name = 'main'
    if target is None:
        raise RuntimeError('cannot find zpaq_main_internal(...) or int main(...)')

    # Include immediately before the common command entry. At this point the
    # upstream platform compatibility layer has already been declared, while
    # zfext is visible to the function body we patch next.
    start = target.start()
    prefix = '' if start == 0 or text[start - 1] == '\n' else '\n'
    text = text[:start] + prefix + MARK_INCLUDE + '\n' + text[start:]

    # Re-find after insertion, then inject the dispatcher at function entry.
    target = find_function_match(text, target_name)
    brace = find_function_open_brace(text, target)
    hook = ('\n  ' + MARK_HOOK + '\n'
            '  { const int zfext_rc = zfext::dispatch(argc, argv); '
            'if (zfext_rc != zfext::kNotHandled) return zfext_rc; }\n')
    text = text[:brace + 1] + hook + text[brace + 1:]

    changed = (text != original)
    out.write_text(text, errors='surrogateescape')
    print(f'{"patched" if changed else "already patched"}: {out}')
    print(f'extensions: {extdir}')
    print(f'OEC dispatch target: {target_name}')


if __name__ == '__main__':
    main()
