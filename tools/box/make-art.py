#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Copyright (c) 2026 abdulahmad
"""Fills the box art folders from your own scans: make-art.py <data/box> <scans dir> <art dir>.
Each scan named in <data/box>/art-map.txt is copied as <art dir>/<game>/<key>.<ext>, for each game whose scene or code uses the key
(<art dir> is usually local/box-art, which the ports copy beside the program). The art stays on your machine; never commit it."""
import os, re, shutil, sys
data, src, out = sys.argv[1:4]
code = {'pc94': (r'^(man\d+|disk\d+|blank|line\w+)$',), 'gen94': (r'^genManual\d*$',)}
used = {}
for g in code:
    keys = set()
    for line in open(os.path.join(data, g, 'scene.txt')):
        w = line.split()
        if len(w) > 3 and w[0] == 'tex' and w[2] in ('art', 'notch') and w[3] != '-': keys.add(w[3])
    used[g] = keys
n = 0
for line in open(os.path.join(data, 'art-map.txt')):
    k, f = line.split(None, 1)
    f = f.strip()
    p = os.path.join(src, f)
    if not os.path.exists(p):
        print('missing', f); continue
    games = [g for g in code if k in used[g] or any(re.match(r, k) for r in code[g])] or list(code)
    for g in games:
        os.makedirs(os.path.join(out, g), exist_ok=True)
        shutil.copyfile(p, os.path.join(out, g, k + os.path.splitext(f)[1].lower())); n += 1
print(n, 'files')
