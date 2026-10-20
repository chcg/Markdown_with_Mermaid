"""Semantic and geometry checks on PDFs produced by the real WebView2 test host."""
import json
import re
from pathlib import Path
import pdfplumber
from pypdf import PdfReader

root = Path(__file__).resolve().parents[1]
results = []
for arch in ("x64", "x86"):
    prefix = root / "build" / "previews" / arch
    diagrams = Path(str(prefix) + "-pdf-mermaid.pdf")
    reduced = Path(str(prefix) + "-pdf-mermaid-50.pdf")
    document = Path(str(prefix) + "-pdf-document.pdf")
    long_code = Path(str(prefix) + "-pdf-long-code.pdf")
    regression = Path(str(prefix) + "-pdf-regression.pdf")
    texts = {}
    paths = [diagrams, reduced, document, long_code]
    if regression.exists():
        paths.append(regression)
    for path in paths:
        reader = PdfReader(path)
        text = "\n".join(page.extract_text() for page in reader.pages)
        texts[path] = text
        assert "CHANGED_AFTER_PDF_SNAPSHOT" not in text
        assert all(abs(float(page.mediabox.width) - 595.28) < 1 for page in reader.pages)
        assert all(abs(float(page.mediabox.height) - 841.89) < 1 for page in reader.pages)
        for number, page in enumerate(reader.pages, 1):
            assert len(page.extract_text().strip()) > 20, f"Empty page {number} in {path.name}"
        results.append({"file": path.name, "pages": len(reader.pages), "bytes": path.stat().st_size})
    assert texts[diagrams] == texts[reduced], "Preview zoom/theme changed PDF text"
    with pdfplumber.open(diagrams) as a, pdfplumber.open(reduced) as b:
        assert len(a.pages) == len(b.pages)
        for pa, pb in zip(a.pages, b.pages):
            ca = [(c["text"], round(c["x0"], 1), round(c["top"], 1)) for c in pa.chars]
            cb = [(c["text"], round(c["x0"], 1), round(c["top"], 1)) for c in pb.chars]
            assert ca == cb, "Preview zoom/theme changed PDF layout"
        assert sum(len(p.curves) + len(p.lines) for p in a.pages) > 50, "Missing vector diagram geometry"
    text = texts[document]
    for token in ("PDF_TEXT_LAYER", "проверка", "Продолжаем экспорт", "Image unavailable"):
        assert token in text, token
    for row in range(1, 121):
        assert re.search(rf"\bTABLE_ROW_{row}\b", text), f"Missing table row {row}"
    for kind, count in (("PLAIN", 160), ("HIGHLIGHTED", 100)):
        for row in range(1, count + 1):
            assert re.search(rf"\b{kind}_CODE_ROW_{row}\b", texts[long_code]), f"Missing {kind} code row {row}"
    assert "END_LONG_CODE_DOCUMENT" in texts[long_code]
    assert not list(document.parent.glob(arch + "*.tmp.pdf"))
print(json.dumps({"status": "PASS", "checks": "A4, text, Cyrillic, 120 table rows, 260 code rows, no empty pages, SVG geometry, identical layout at 204% dark / 50% light", "files": results}, indent=2))
