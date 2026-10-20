"""Check printed geometry of HTML images after the height-attribute regression."""
from pathlib import Path
import json
import pdfplumber

root = Path(__file__).resolve().parents[1]
results = []
for arch in ['x64', 'x86']:
    path = root / 'build/previews' / f'{arch}-pdf-image-sizes.pdf'
    with pdfplumber.open(path) as pdf:
        assert len(pdf.pages) == 1, 'This compact fixture should fit on one A4 page'
        page = pdf.pages[0]
        pink, blue = [], []
        for shape in page.curves + page.rects:
            color = shape.get('non_stroking_color')
            if not color or len(color) < 3:
                continue
            size = (shape['x1'] - shape['x0'], shape['bottom'] - shape['top'])
            if color[0] > .99 and abs(color[1] - .2118) < .001 and abs(color[2] - .4392) < .001:
                pink.append(size)
            if abs(color[0] - .0353) < .001 and abs(color[1] - .4118) < .001 and abs(color[2] - .8549) < .001:
                blue.append(size)
        assert len(pink) == 1 and all(abs(v - 112.5) < 1 for v in pink[0]), pink
        assert len(blue) == 5, blue
        # The square logo occupies 150 CSS px (112.5 pt), not the page width.
        # SVG with both HTML dimensions retains its internal preserveAspectRatio.
        for width, height in [(90, 18), (150, 30), (120, 24), (360, 72)]:
            assert any(abs(w-width) < 1 and abs(h-height) < 1 for w, h in blue), (width, height, blue)
        wide = max(blue)
        assert 500 < wide[0] < 513 and abs(wide[0] / wide[1] - 5) < .03, wide
        assert 'LOGO_HEIGHT_150' in page.extract_text()
        results.append({'file': path.name, 'pages': 1, 'logo_points': pink[0], 'other_images_points': blue})
print(json.dumps({'status': 'PASS', 'checks': '150px logo, authored dimensions, responsive scaling, one A4 page', 'files': results}, indent=2))
