"""Validate exported selection boundaries, editable OOXML and diagram media."""
import json
from pathlib import Path
from zipfile import ZipFile
from lxml import etree as ET
from pypdf import PdfReader

root = Path(__file__).resolve().parents[1]
ns = {'w': 'http://schemas.openxmlformats.org/wordprocessingml/2006/main'}
results = []
for arch in ('x64', 'x86'):
    for kind in ('selection', 'editor-selection'):
        prefix = root / 'build' / 'previews' / f'{arch}-{kind}'
        pdf = PdfReader(str(prefix) + '.pdf')
        assert len(pdf.pages) == 1
        text = '\n'.join(p.extract_text() for p in pdf.pages)
        assert 'OUTSIDE_' not in text and 'CHANGED_AFTER_SELECTION_SNAPSHOT' not in text
        with ZipFile(str(prefix) + '.docx') as package:
            assert package.testzip() is None
            document = ET.fromstring(package.read('word/document.xml'))
            docx_text = '\n'.join(document.xpath('//w:t/text()', namespaces=ns))
            assert 'OUTSIDE_' not in docx_text and 'CHANGED_AFTER_SELECTION_SNAPSHOT' not in docx_text
            assert not document.xpath('//w:altChunk', namespaces=ns)
            if kind == 'selection':
                for token in ('SELECTED_SECTION', 'SELECTED_PARAGRAPH', 'жирный текст', 'SELECTED_LINK', 'SELECTED_LIST_ONE', 'SELECTED_LIST_TWO', 'SELECTED_CELL'):
                    assert token in text and token in docx_text, token
                assert document.xpath('//w:r[w:rPr/w:b]/w:t[text()="жирный текст"]', namespaces=ns)
                assert document.xpath('//w:r[w:rPr/w:i]/w:t[text()="курсив"]', namespaces=ns)
                assert len(document.xpath('//w:tbl', namespaces=ns)) == 1
                assert len(document.xpath('//w:numPr', namespaces=ns)) == 2
                assert len(document.xpath('//w:hyperlink', namespaces=ns)) == 2
                assert len(document.xpath('//w:hyperlink[@w:anchor]', namespaces=ns)) == 1
                assert '☑ ' in docx_text and '☐ ' in docx_text
                svgs = [name for name in package.namelist() if name.endswith('.svg')]
                pngs = [name for name in package.namelist() if name.endswith('.png')]
                assert len(svgs) == 1 and len(pngs) == 2
                assert b'SELECTED_NODE_A' in package.read(svgs[0])
                assert 'SELECTED_NODE_A' in text and 'SELECTED_NODE_B' in text
                assert 'Image unavailable' not in text + docx_text
            else:
                assert 'EDITOR_SELECTION_TOKEN' in text and 'EDITOR_SELECTION_TOKEN' in docx_text
                assert 'SELECTED_SECTION' not in text + docx_text
                assert text.rstrip().endswith('END_EDITOR_BOUNDARY'), 'PDF editor selection lost its final boundary'
                assert docx_text.rstrip().endswith('END_EDITOR_BOUNDARY'), 'DOCX editor selection lost its final boundary'
            results.append({'architecture': arch, 'kind': kind, 'pdf_pages': len(pdf.pages), 'status': 'PASS'})
report = {'version': (root / 'VERSION').read_text().strip(), 'status': 'PASS', 'files': results}
(root / 'docs/research/selection-validation.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps(report, ensure_ascii=False, indent=2))
