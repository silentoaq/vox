import argparse
import io
import json
import re
import tempfile
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
CATALOG = ROOT / 'ui' / 'js' / 'fonts.json'
OUT = ROOT / 'ui' / 'fonts'
GOOGLE_CSS = 'https://fonts.googleapis.com/css2'
WHOLE_FILE_AGENT = 'curl/8.0'
HEIGHT = 48
SIZE = 34
PAD = 4


def pick_weight(weights):
    return '700' if '700' in weights else '400' if '400' in weights else weights[0]


def fetch(url, agent=WHOLE_FILE_AGENT):
    request = urllib.request.Request(url, headers={'User-Agent': agent})
    with urllib.request.urlopen(request) as response:
        return response.read()


def google_subset(font, cache):
    weight = pick_weight(font['weights'])
    target = cache / f"{font['id']}-{weight}-preview.ttf"
    if not target.exists():
        axis = '' if weight == '400' else f':wght@{weight}'
        css = fetch(f"{GOOGLE_CSS}?family={font['gfont']}{axis}&text={urllib.parse.quote(font['label'])}").decode()
        url = re.search(r'url\((https://[^)]+)\)', css).group(1)
        target.write_bytes(fetch(url))
    return target


def whole_file(font, cache):
    weight = pick_weight(list(font['files']))
    member = font.get('zip', {}).get(weight)
    target = cache / f"{font['id']}-{weight}{Path(member or urllib.parse.urlparse(font['files'][weight]).path).suffix}"
    if target.exists():
        return target
    data = fetch(font['files'][weight])
    if member:
        archive = cache / f"{font['id']}.zip"
        archive.write_bytes(data)
        with zipfile.ZipFile(archive) as bundle:
            target.write_bytes(bundle.read(member))
        archive.unlink()
    else:
        target.write_bytes(data)
    return target


def source_of(font, cache):
    if 'gfont' in font:
        return google_subset(font, cache)
    return whole_file(font, cache)


def label_only(font_file, label):
    font = TTFont(str(font_file), fontNumber=0)
    options = subset.Options()
    options.hinting = False
    options.layout_features = ['*']
    subsetter = subset.Subsetter(options)
    subsetter.populate(text=label)
    subsetter.subset(font)
    data = io.BytesIO()
    font.save(data)
    return data


def render(font_file, label, out):
    trimmed = label_only(font_file, label)
    size = SIZE
    while True:
        trimmed.seek(0)
        face = ImageFont.truetype(trimmed, size)
        left, top, right, bottom = face.getbbox(label, anchor='lm')
        if bottom - top <= HEIGHT - 2 * PAD:
            break
        size -= 1
    image = Image.new('RGBA', (right - left + 2 * PAD, HEIGHT), (0, 0, 0, 0))
    ImageDraw.Draw(image).text((PAD - left, HEIGHT / 2), label, font=face, fill=(255, 255, 255, 255), anchor='lm')
    image.save(out, 'WEBP', lossless=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cache', type=Path, default=Path(tempfile.gettempdir()) / 'vox-font-previews')
    parser.add_argument('--only', nargs='*')
    args = parser.parse_args()
    args.cache.mkdir(parents=True, exist_ok=True)
    OUT.mkdir(exist_ok=True)
    for font in json.loads(CATALOG.read_text(encoding='utf-8')):
        if font['id'] == 'system' or (args.only and font['id'] not in args.only):
            continue
        render(source_of(font, args.cache), font['label'], OUT / f"{font['id']}.webp")
        print(font['id'], flush=True)


if __name__ == '__main__':
    main()
