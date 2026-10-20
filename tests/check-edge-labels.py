"""Check the actual exported SVG backgrounds, not only Preview CSS."""
import json
import re
from pathlib import Path
from zipfile import ZipFile
from lxml import etree as ET
from pypdf import PdfReader

root = Path(__file__).resolve().parents[1]
results = []
def contrast(a, b):
    def luminance(color):
        if color.startswith('rgb'):
            values = [float(v) / 255 for v in re.findall(r'[\d.]+', color)[:3]]
        else:
            color = color.lstrip('#')
            if len(color) == 3: color = ''.join(c * 2 for c in color)
            values = [int(color[i:i+2], 16) / 255 for i in (0, 2, 4)]
        values = [v / 12.92 if v <= .04045 else ((v + .055) / 1.055) ** 2.4 for v in values]
        return sum(v * w for v, w in zip(values, (.2126, .7152, .0722)))
    a, b = luminance(a), luminance(b)
    return (max(a, b) + .05) / (min(a, b) + .05)
for arch in ('x64', 'x86'):
    prefix = root / 'build/previews' / f'{arch}-edge-labels'
    reader = PdfReader(str(prefix) + '.pdf')
    text = '\n'.join(page.extract_text() for page in reader.pages)
    for token in ('Text', 'One', 'Two', 'opens', 'renders', 'подпись'):
        assert token in text, token
    assert all(page.extract_text().strip() for page in reader.pages)
    with ZipFile(str(prefix) + '.docx') as package:
        svgs = [ET.fromstring(package.read(name)) for name in package.namelist() if name.endswith('.svg')]
        assert len(svgs) == 4
        backgrounds = []
        for svg in svgs:
            for rect in svg.xpath('//*[local-name()="rect"]'):
                classes = set(rect.get('class', '').split())
                if not classes.intersection({'background', 'box'}):
                    continue
                if not any(set(parent.get('class', '').split()).intersection({'edgeLabel', 'classLabel'}) for parent in rect.iterancestors()):
                    continue
                if float(rect.get('width', 0)) <= 0:
                    continue
                assert rect.get('opacity') == '1', ET.tostring(rect)
                assert rect.get('fill-opacity') == '1'
                backgrounds.append(rect.get('fill').lower())
        assert len(backgrounds) >= 8, backgrounds
        assert '#ffffff' in backgrounds and '#fff2cc' in backgrounds
        clusters = svgs[1].xpath('//*[contains(concat(" ",normalize-space(@class)," ")," cluster ")]')
        assert len(clusters) == 2
        for cluster in clusters:
            paper = cluster.xpath('./*[local-name()="rect"]')[0].get('fill')
            for text_node in cluster.xpath('./*[contains(concat(" ",normalize-space(@class)," ")," cluster-label ")]//*[local-name()="text" or local-name()="tspan"]'):
                assert contrast(text_node.get('fill'), paper) >= 4.5
        nodes = svgs[1].xpath('//*[contains(concat(" ",normalize-space(@class)," ")," node ")]')
        assert len(nodes) == 3
        for node in nodes:
            paper = node.xpath('.//*[local-name()="rect" or local-name()="polygon" or local-name()="circle" or local-name()="path"]')[0].get('fill')
            for text_node in node.xpath('.//*[local-name()="text" or local-name()="tspan"]'):
                assert contrast(text_node.get('fill'), paper) >= 4.5
        connectors = svgs[1].xpath('//*[contains(concat(" ",normalize-space(@class)," ")," flowchart-link ")]')
        assert len(connectors) == 2
        by_id = {element.get('id'): element for element in svgs[1].iter() if element.get('id')}
        for connector in connectors:
            assert contrast(connector.get('stroke'), '#fff2cc') >= 3
            marker_id = re.search(r'#([^\s\)"\x27]+)', connector.get('marker-end', '')).group(1)
            marker = by_id[marker_id]
            for tip in marker.xpath('.//*[contains(concat(" ",normalize-space(@class)," ")," arrowMarkerPath ")]'):
                assert contrast(tip.get('fill'), '#fff2cc') >= 3
        results.append({'architecture': arch, 'pdf_pages': len(reader.pages), 'docx_svg': len(svgs), 'opaque_label_backgrounds': len(backgrounds), 'fills': sorted(set(backgrounds))})
report = {'version': (root / 'VERSION').read_text().strip(), 'status': 'PASS', 'files': results}
(root / 'docs/research/edge-labels-export-validation.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print(json.dumps(report, indent=2))
