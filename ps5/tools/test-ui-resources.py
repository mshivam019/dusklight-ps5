#!/usr/bin/env python3
"""Validate the German catalog and reproduce the independent CC0 button artwork.

Requires Pillow, as does ps5/art/make-glyphs.py. Does not read a game disc.
Console tests are still required for actual PAL texture matching and UI layout.
"""
from pathlib import Path
import json
import shutil
import string
import subprocess
import sys
import tempfile

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]

def unique_pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result: raise ValueError('Duplicate translation: ' + key)
        result[key] = value
    return result

catalog = json.loads((ROOT/'res/locales/de.json').read_text(encoding='utf-8'),
                     object_pairs_hook=unique_pairs)
formatter = string.Formatter()
for key, value in catalog.items():
    assert isinstance(value, str) and value.strip(), key
    fields = lambda text: [field for _, field, _, _ in formatter.parse(text) if field is not None]
    assert fields(key) == fields(value), 'Changed placeholders: ' + key
    assert '<' not in value and '>' not in value, 'Text catalogs must not inject RML'
for key in ['Play','Settings','Quit','Select Disc Image','Menu language',
            'No disc image found.','Disc ready.','Verifying disc image']:
    assert key in catalog, key

with tempfile.TemporaryDirectory(prefix='dusklight-ui-art-') as temporary:
    art = Path(temporary)
    shutil.copy2(ROOT/'ps5/art/make-glyphs.py', art/'make-glyphs.py')
    shutil.copy2(ROOT/'ps5/art/button-textures.json', art/'button-textures.json')
    shutil.copytree(ROOT/'ps5/art/kenney', art/'kenney')
    subprocess.run([sys.executable,str(art/'make-glyphs.py')],check=True)
    shipped = sorted((ROOT/'res/ps5-glyphs').glob('*.png'))
    manifest = json.loads((art/'button-textures.json').read_text(encoding='utf-8'))
    expected = {i['key']+'.png' for i in manifest if i['style']!='Backing'}
    assert len(expected) == 20
    assert {p.name for p in shipped} == expected
    assert {p.name for p in shipped} == {p.name for p in (art/'glyphs').glob('*.png')}
    assert (art/'generated-button-textures.inc').read_bytes() == (ROOT/'ps5/art/generated-button-textures.inc').read_bytes()
    by_name = {i['resource']:i for i in manifest}
    for name,expected_button in [('font_04.bti','l2'),('font_05.bti','r2'),('font_06.bti','r1')]:
        assert by_name[name]['symbol'] == 'playstation_trigger_'+expected_button+'_outline'
    for path in shipped:
        with Image.open(path) as a, Image.open(art/'glyphs'/path.name) as b:
            a,b = a.convert('RGBA'), b.convert('RGBA')
            assert a.size == b.size and a.tobytes() == b.tobytes(), path.name
            assert a.getchannel('A').getextrema() == (0,255), path.name
            # Every button is complete artwork: solid dark cap, white outline/symbol,
            # transparent outside. A mere letter substitution does not satisfy this.
            assert any(px[:3] == (35,38,44) and px[3]==255 for px in a.getdata()), path.name
print('German catalog and reproducible CC0 PS5 artwork: PASS')
