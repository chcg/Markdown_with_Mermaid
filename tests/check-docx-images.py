"""Verify that DOCX reuses visible preview images without another HTTP request."""
from pathlib import Path
from zipfile import ZipFile
from lxml import etree
import json
import argparse
from collections import Counter

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--include-readme', action='store_true', help='Also check freshly generated optional README exports')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
ns = {'w': 'http://schemas.openxmlformats.org/wordprocessingml/2006/main',
      'wp': 'http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing'}
results = []
for arch in ['x64', 'x86']:
    for kind in ['cache-smoke', 'http-images'] + (['readme'] if args.include_readme else []):
        path = root / 'build/previews' / f'{arch}-docx-{kind}.docx'
        if kind == 'readme' and not path.exists():
            continue
        with ZipFile(path) as package:
            assert package.testzip() is None
            document = etree.fromstring(package.read('word/document.xml'))
            names = document.xpath('//wp:docPr/@descr', namespaces=ns)
            text = '\n'.join(document.xpath('//w:t/text()', namespaces=ns))
            assert 'CHANGED_AFTER_DOCX_SNAPSHOT' not in text
            warnings = [''.join(p.xpath('.//w:t/text()', namespaces=ns))
                        for p in document.xpath('//w:p', namespaces=ns)
                        if 'Image unavailable' in ''.join(p.xpath('.//w:t/text()', namespaces=ns))]
            if kind == 'cache-smoke':
                assert names == ['ONCE'] and not warnings, (names, warnings)
            elif kind == 'http-images':
                assert set(names) == {'NO_CORS_BADGE', 'REMOTE_redirect', 'AFTER_ERRORS'}, names
                assert len(warnings) == 5, warnings
                for reason in ['HTTP 404', 'HTML page', '16 MiB', 'Local file not found']:
                    assert reason in text, reason
                loop_warning = next(w for w in warnings if 'REMOTE_loop' in w)
                # A TCP reset can terminate the loop before the redirect limit.
                assert 'redirects' in loop_warning or 'Windows network error' in loop_warning, loop_warning
                # The authored height=16 halves this 144x32 image to 72x16.
                first = document.xpath('(//wp:extent)[1]', namespaces=ns)[0]
                assert int(first.get('cx')) == 72 * 9525 and int(first.get('cy')) == 16 * 9525
            else:
                assert 'Build CI Status' in names
                assert 'Banner for "The Official Guide to Mermaid.js" book' in names
                assert sum('header.png' in w for w in warnings) == 1, warnings
                # Public services can time out independently of the exporter.
                # The two originally broken images must still be embedded.
                assert all('header.png' in w or 'Windows network error' in w or 'timed out' in w for w in warnings), warnings
            results.append({'file': path.name, 'drawings': len(names), 'warnings': warnings})

requests = [json.loads(line) for line in (root / 'build/image-http-requests.jsonl').read_text().splitlines() if line.strip()]
counts = Counter(r['path'] for r in requests if r['path'].startswith('/once.svg?'))
assert len(counts) >= 2 and all(n == 1 for n in counts.values()), counts
assert all(not r['authorization'] and not r['cookie'] for r in requests)
print(json.dumps({'status': 'PASS', 'once_image_requests': dict(counts), 'files': results}, ensure_ascii=False, indent=2))
