"""Validate actual DOCX exports from WebView2, including editable OOXML and media."""
import json
import re
from io import BytesIO
from pathlib import Path
from zipfile import ZipFile
from lxml import etree as ET
from PIL import Image

root = Path(__file__).resolve().parents[1]
ns = {"w": "http://schemas.openxmlformats.org/wordprocessingml/2006/main", "a": "http://schemas.openxmlformats.org/drawingml/2006/main"}
results = []
for arch in ("x64", "x86"):
    for kind in ("mermaid", "document", "long-code", "regression"):
        path = root / "build" / "previews" / f"{arch}-docx-{kind}.docx"
        if kind == "regression" and not path.exists():
            continue
        with ZipFile(path) as package:
            assert package.testzip() is None
            for name in package.namelist():
                if name.endswith((".xml", ".rels")):
                    ET.fromstring(package.read(name))
            document = ET.fromstring(package.read("word/document.xml"))
            text = "\n".join(document.xpath("//w:t/text()", namespaces=ns))
            assert "CHANGED_AFTER_DOCX_SNAPSHOT" not in text
            assert not document.xpath("//w:altChunk", namespaces=ns), "Must be editable OOXML, not HTML altChunk"
            pngs = [name for name in package.namelist() if name.endswith(".png")]
            svgs = [name for name in package.namelist() if name.endswith(".svg")]
            for name in pngs:
                image = Image.open(BytesIO(package.read(name)))
                assert image.width > 5 and image.height > 5
            for name in svgs:
                svg = ET.fromstring(package.read(name))
                assert not svg.xpath("//*[local-name()='style' or local-name()='script' or local-name()='foreignObject']")
                assert not re.search(rb"(?:var\(|hsl\()", package.read(name)), "SVG colors/styles must be resolved for Office"
            if kind == "mermaid":
                assert len(svgs) == 6 and len(pngs) == 6
                assert len(document.xpath("//w:drawing", namespaces=ns)) == 6
                assert len(document.xpath("//w:pStyle[starts-with(@w:val,'Heading')]", namespaces=ns)) >= 7
            if kind == "document":
                for token in ("DOCX_TEXT_LAYER", "Русский текст", "☑", "☐", "DOCX_CODE_LINE", "Image unavailable", "Could not render the Mermaid diagram"):
                    assert token in text, token
                assert len(document.xpath("//w:tbl", namespaces=ns)) == 1
                assert document.xpath("//w:tblHeader", namespaces=ns)
                assert document.xpath("//w:numPr", namespaces=ns)
                assert document.xpath("//w:b", namespaces=ns) and document.xpath("//w:i", namespaces=ns) and document.xpath("//w:strike", namespaces=ns)
                bookmarks = set(document.xpath("//w:bookmarkStart/@w:name", namespaces=ns))
                anchors = set(document.xpath("//w:hyperlink/@w:anchor", namespaces=ns))
                assert anchors and anchors <= bookmarks
                assert len(svgs) == 1 and len(pngs) == 2, "Missing local image or diagram fallback"
                for row in range(1, 121):
                    assert re.search(rf"\bDOCX_ROW_{row}\b", text), row
            if kind == "long-code":
                for label, count in (("PLAIN", 160), ("HIGHLIGHTED", 100)):
                    for row in range(1, count + 1):
                        assert re.search(rf"\b{label}_CODE_ROW_{row}\b", text), (label, row)
                assert "END_LONG_CODE_DOCUMENT" in text
            if kind == "regression":
                assert "Merlay" in text and "webview.tsx" in text
            for drawing in document.xpath("//w:drawing", namespaces=ns):
                for extent in drawing.xpath(".//a:xfrm/a:ext", namespaces=ns):
                    assert int(extent.get("cx")) <= 680 * 9525 + 9525
                    assert int(extent.get("cy")) <= 850 * 9525 + 9525
            results.append({"file": path.name, "bytes": path.stat().st_size, "svg": len(svgs), "png": len(pngs)})
print(json.dumps({"status": "PASS", "checks": "valid ZIP/XML, editable text/tables/lists, Unicode, hyperlinks/bookmarks, 120 table rows, 260 code lines, six SVG + PNG diagrams, local image, no HTML altChunk", "files": results}, indent=2))
