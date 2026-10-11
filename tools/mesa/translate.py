#!/usr/bin/env python3
#
# tools/mesa/translate.py — compile Mesa's sources for a TessronOS process
# (docs/tessronos/13-gpu.md 13.6)
#
# Copyright (C) 2026 satromi
# This software is distributed under the T-License 2.2.
#
# meson describes the build (tools/mesa/build.sh setup) with the process's
# compiler and headers, for a Linux system. This takes the compile commands
# of the objects the three libraries of OpenGL ES are made of (libEGL,
# libGLESv2 and the Gallium library with its drivers), as build.ninja links
# them, keeps what each source asks for (its defines, include paths,
# language level) and compiles it again with the flags of a TessronOS
# program: no position independent code, a section for each function so
# that a program's link keeps only what it reaches, and no debug
# information unless asked for.
#
#   translate.py --build DIR --obj DIR [--jobs N] [--objlist FILE]
#
# An object is compiled again only when its command or a file it was made
# from changed. A failure leaves <object>.fail with its first errors.

import argparse
import concurrent.futures as cf
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys

# The shared libraries whose objects make up the static library
ROOTS = (r'src/egl/libEGL\.so', r'src/mesa/glapi/es2api/libGLESv2\.so',
         r'src/gallium/targets/dri/libgallium-.*\.so')

# Flags of meson's commands that do not belong to a static program
DROP_EXACT = ('-fPIC', '-fpic', '-pthread', '-Winvalid-pch', '-pipe', '-MD', '-MMD', '-c',
              '-fdiagnostics-color=always', '-g')
DROP_PREFIX = ('-Werror', '-O', '-fvisibility', '-fstack-protector', '-D_FORTIFY_SOURCE')
DROP_WITH_ARG = ('-o', '-MF', '-MQ', '-MT', '-c')

ERRLINES = 6

# Flags added for some sources: a path ending, then the flags. The GLSL
# lexer takes LLONG_MAX from <limits.h>, which glibc's headers include on
# their way and newlib's do not. newlib declares the POSIX regular
# expressions but its C library has none: driconf matches no program by
# name, as where Mesa has no <regex.h>
FILE_FLAGS = (
    ('glsl_lexer.cpp', ['-include', 'climits']),
    ('src/util/xmlconfig.c', ['-DNO_REGEX']),
)


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument('--build', required=True, help='meson build directory')
    p.add_argument('--obj', required=True, help='where the objects go')
    p.add_argument('--jobs', type=int, default=8)
    p.add_argument('--objlist', help='write the list of objects here')
    p.add_argument('--opt', default='-O2')
    p.add_argument('--debug', action='store_true', help='keep debug information (-g)')
    p.add_argument('--list', action='store_true', help='print what would be compiled')
    p.add_argument('--force', action='store_true')
    return p.parse_args()


def ninja_builds(path):
    """{output: (rule, explicit inputs, implicit inputs)} of build.ninja."""
    text = open(path, encoding='utf-8').read().replace('$\n', ' ')
    out = {}
    for line in text.splitlines():
        if not line.startswith('build '):
            continue
        head, _, rest = line[6:].partition(': ')
        rest = rest.split('||')[0]
        expl, _, impl = rest.partition(' | ')
        toks = expl.split()
        if not toks:
            continue
        for o in head.split():
            out[o.replace('$ ', ' ')] = (toks[0], toks[1:], impl.split())
    return out


def wanted_objects(builds):
    """The objects linked into the roots, through the static libraries they link."""
    roots = [o for o in builds if any(re.fullmatch(r + r'[.0-9]*', o) for r in ROOTS)]
    if len(roots) != len(ROOTS):
        sys.exit('translate: build.ninja lacks some of %s (found %s)' % (ROOTS, roots))
    objs = set()
    seen = set()
    todo = list(roots)
    while todo:
        t = todo.pop()
        if t in seen or t not in builds:
            continue
        seen.add(t)
        rule, expl, impl = builds[t]
        for i in expl + impl:
            if i.endswith('.o'):
                objs.add(i)
            elif i.endswith('.a') and i in builds:
                todo.append(i)
    return objs


def translate(entry, a):
    cmd = shlex.split(entry['command'])
    toks = []
    i = 1
    while i < len(cmd):
        t = cmd[i]
        if t in DROP_WITH_ARG:
            i += 2
            continue
        if t in DROP_EXACT or any(t.startswith(p) for p in DROP_PREFIX) or t == entry['file']:
            i += 1
            continue
        if t.startswith('-std=c'):
            # newlib hides alloca, LLONG_MAX in C++ and the POSIX names
            # from a strict language level
            t = '-std=gnu' + t[6:]
        toks.append(t)
        i += 1
    obj = os.path.join(a.obj, entry['output'])
    toks += [f for end, fl in FILE_FLAGS if entry['file'].endswith(end) for f in fl]
    argv = [cmd[0]] + toks + [a.opt, '-g' if a.debug else '-g0', '-w',
                               '-ffunction-sections', '-fdata-sections',
                               '-c', entry['file'], '-o', obj]
    return obj, argv


def deps_current(obj, cwd):
    """Whether no file the object was made from (its .d) is newer than it."""
    try:
        t = os.path.getmtime(obj)
        text = open(obj + '.d').read().replace('\\\n', ' ')
    except OSError:
        return False
    for f in text.split(':', 1)[1].split():
        try:
            if os.path.getmtime(os.path.join(cwd, f)) > t:
                return False
        except OSError:
            return False
    return True


def compile_one(job):
    obj, argv, cwd, force = job
    stamp = obj + '.cmd'
    key = hashlib.sha1('\0'.join(argv).encode()).hexdigest()
    if not force and os.path.exists(obj) and os.path.exists(stamp):
        try:
            if open(stamp).read() == key and deps_current(obj, cwd):
                return obj, True, None
        except OSError:
            pass
    os.makedirs(os.path.dirname(obj), exist_ok=True)
    r = subprocess.run(argv + ['-MD', '-MF', obj + '.d'], cwd=cwd, capture_output=True,
                       text=True, errors='replace')
    fail = obj + '.fail'
    if r.returncode != 0:
        errs = [l for l in r.stderr.splitlines() if 'error' in l][:ERRLINES]
        with open(fail, 'w') as f:
            f.write('=== %s\n%s\n' % (argv[argv.index('-c') + 1], '\n'.join(errs)))
        if os.path.exists(obj):
            os.remove(obj)
        return obj, False, '\n'.join(errs)
    if os.path.exists(fail):
        os.remove(fail)
    with open(stamp, 'w') as f:
        f.write(key)
    return obj, True, None


def main():
    a = parse_args()
    a.build = os.path.abspath(a.build)
    a.obj = os.path.abspath(a.obj)
    entries = json.load(open(os.path.join(a.build, 'compile_commands.json')))
    want = wanted_objects(ninja_builds(os.path.join(a.build, 'build.ninja')))
    jobs = []
    for e in entries:
        if e['output'] not in want:
            continue
        obj, argv = translate(e, a)
        jobs.append((obj, argv, a.build, a.force))
    print('translate: %d sources of %d' % (len(jobs), len(entries)))
    if a.list:
        for j in jobs:
            print(j[0])
        return 0
    ok = bad = 0
    fails = []
    with cf.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        for obj, good, err in ex.map(compile_one, jobs):
            if good:
                ok += 1
            else:
                bad += 1
                fails.append((obj, err))
    if a.objlist:
        with open(a.objlist, 'w') as f:
            for j in jobs:
                f.write(j[0] + '\n')
    print('translate: %d compiled or current, %d failed' % (ok, bad))
    for obj, err in sorted(fails):
        first = (err or '').splitlines()[:1]
        print('  %s: %s' % (os.path.relpath(obj, a.obj), first[0] if first else '?'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
