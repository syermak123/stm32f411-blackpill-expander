#!/usr/bin/env python3
"""
Markdown → PDF з підтримкою української (TTF DejaVu / Liberation).

Пандок + LaTeX за замовчанням дає «квадратики», якщо не вказати UTF-8 двигун
і шрифт з кирилицею, наприклад:

  pandoc I2C_PROTOCOL_AND_PINS.md -o out.pdf --pdf-engine=xelatex \\
    -V mainfont="DejaVu Sans" -V monofont="DejaVu Sans Mono"
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import cm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (
    Paragraph,
    Preformatted,
    SimpleDocTemplate,
    Spacer,
    Table,
    TableStyle,
)

ROOT = Path(__file__).resolve().parent

# Типові шляхи на Linux (пакет fonts-dejavu-core або fonts-liberation)
_DEJAVU = Path("/usr/share/fonts/truetype/dejavu")
_LIBERATION = Path("/usr/share/fonts/truetype/liberation")


def _register_ttf(name: str, path: Path) -> None:
    pdfmetrics.registerFont(TTFont(name, str(path)))


def register_cyrillic_fonts() -> tuple[str, str]:
    """
    Реєструє сімейство з normal/bold (+ italic) для тега <b> у Paragraph.
    Повертає (family_name, mono_font_name).
    """
    sans_regular = _DEJAVU / "DejaVuSans.ttf"
    sans_bold = _DEJAVU / "DejaVuSans-Bold.ttf"
    sans_italic = _DEJAVU / "DejaVuSans-Oblique.ttf"
    sans_bold_it = _DEJAVU / "DejaVuSans-BoldOblique.ttf"
    mono_path = _DEJAVU / "DejaVuSansMono.ttf"

    if sans_regular.is_file() and sans_bold.is_file():
        _register_ttf("CYRSans", sans_regular)
        _register_ttf("CYRSans-Bold", sans_bold)
        it = sans_italic if sans_italic.is_file() else sans_regular
        bit = sans_bold_it if sans_bold_it.is_file() else sans_bold
        _register_ttf("CYRSans-Italic", it)
        _register_ttf("CYRSans-BoldItalic", bit)
        family = "CYRSans"
        pdfmetrics.registerFontFamily(
            family,
            normal="CYRSans",
            bold="CYRSans-Bold",
            italic="CYRSans-Italic",
            boldItalic="CYRSans-BoldItalic",
        )
        mono_name = "CYRSansMono"
        if mono_path.is_file():
            _register_ttf(mono_name, mono_path)
        else:
            mono_name = "Courier"
        return family, mono_name

    # Liberation Sans
    reg = _LIBERATION / "LiberationSans-Regular.ttf"
    bold = _LIBERATION / "LiberationSans-Bold.ttf"
    italic = _LIBERATION / "LiberationSans-Italic.ttf"
    bold_it = _LIBERATION / "LiberationSans-BoldItalic.ttf"
    mono_p = _LIBERATION / "LiberationMono-Regular.ttf"

    if reg.is_file() and bold.is_file():
        _register_ttf("CYRSans", reg)
        _register_ttf("CYRSans-Bold", bold)
        _register_ttf("CYRSans-Italic", italic if italic.is_file() else reg)
        _register_ttf("CYRSans-BoldItalic", bold_it if bold_it.is_file() else bold)
        family = "CYRSans"
        pdfmetrics.registerFontFamily(
            family,
            normal="CYRSans",
            bold="CYRSans-Bold",
            italic="CYRSans-Italic",
            boldItalic="CYRSans-BoldItalic",
        )
        mono_name = "CYRSansMono"
        if mono_p.is_file():
            _register_ttf(mono_name, mono_p)
        else:
            mono_name = "Courier"
        return family, mono_name

    print(
        "Не знайдено шрифтів з кирилицею. Встановіть пакет:\n"
        "  sudo apt install fonts-dejavu-core\n"
        "або fonts-liberation.",
        file=sys.stderr,
    )
    sys.exit(1)


def build_styles(family: str, mono_name: str):
    base = getSampleStyleSheet()
    bold_face = f"{family}-Bold"
    body = ParagraphStyle(
        "BodyCyr",
        parent=base["BodyText"],
        fontName=family,
        fontSize=10,
        leading=13,
        spaceAfter=4,
    )
    h1 = ParagraphStyle(
        "H1Cyr",
        parent=base["Heading1"],
        fontName=bold_face,
        fontSize=18,
        leading=22,
        spaceAfter=12,
    )
    h2 = ParagraphStyle(
        "H2Cyr",
        parent=base["Heading2"],
        fontName=bold_face,
        fontSize=14,
        leading=18,
        spaceAfter=10,
    )
    h3 = ParagraphStyle(
        "H3Cyr",
        parent=base["Heading3"],
        fontName=bold_face,
        fontSize=12,
        leading=15,
        spaceAfter=8,
    )
    code_font = ParagraphStyle(
        "CodeCMono",
        parent=base["Code"],
        fontName=mono_name,
        fontSize=8,
        leading=10,
        leftIndent=8,
    )
    return {"h1": h1, "h2": h2, "h3": h3, "body": body, "code": code_font}


def md_inline(s: str) -> str:
    s = s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    s = re.sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", s)
    s = s.replace("`", "")
    return s


def is_table_sep_row(cells: list[str]) -> bool:
    if not cells:
        return False
    for c in cells:
        t = c.strip().replace(" ", "")
        if not t or not set(t) <= {"-", ":"}:
            return False
    return True


def parse_md(md_path: Path, story: list, st: dict) -> None:
    h1, h2, h3, body, code_font = st["h1"], st["h2"], st["h3"], st["body"], st["code"]
    lines = md_path.read_text(encoding="utf-8").splitlines()
    i = 0
    in_code = False
    code_lines: list[str] = []

    while i < len(lines):
        line = lines[i]
        if line.strip() == "```":
            in_code = not in_code
            if not in_code and code_lines:
                story.append(Preformatted("\n".join(code_lines), code_font))
                story.append(Spacer(1, 8))
                code_lines = []
            i += 1
            continue
        if in_code:
            code_lines.append(line)
            i += 1
            continue

        if line.startswith("# "):
            story.append(Paragraph(md_inline(line[2:]), h1))
            story.append(Spacer(1, 14))
        elif line.startswith("## "):
            story.append(Paragraph(md_inline(line[3:]), h2))
            story.append(Spacer(1, 10))
        elif line.startswith("### "):
            story.append(Paragraph(md_inline(line[4:]), h3))
            story.append(Spacer(1, 8))
        elif line.strip() == "---":
            story.append(Spacer(1, 14))
        elif line.strip().startswith("|"):
            rows_raw: list[list[str]] = []
            while i < len(lines) and lines[i].strip().startswith("|"):
                row = [c.strip() for c in lines[i].strip().strip("|").split("|")]
                rows_raw.append(row)
                i += 1
            if len(rows_raw) >= 2 and is_table_sep_row(rows_raw[1]):
                rows_raw = [rows_raw[0]] + rows_raw[2:]
            if rows_raw:
                data = [[Paragraph(md_inline(c), body) for c in row] for row in rows_raw]
                tbl = Table(data, repeatRows=1)
                tbl.setStyle(
                    TableStyle(
                        [
                            ("BACKGROUND", (0, 0), (-1, 0), colors.lightgrey),
                            ("GRID", (0, 0), (-1, -1), 0.5, colors.grey),
                            ("VALIGN", (0, 0), (-1, -1), "TOP"),
                            ("LEFTPADDING", (0, 0), (-1, -1), 4),
                            ("RIGHTPADDING", (0, 0), (-1, -1), 4),
                            ("TOPPADDING", (0, 0), (-1, -1), 3),
                            ("BOTTOMPADDING", (0, 0), (-1, -1), 3),
                        ]
                    )
                )
                story.append(tbl)
                story.append(Spacer(1, 12))
            continue
        elif re.match(r"^\s*-\s+", line):
            story.append(Paragraph("• " + md_inline(re.sub(r"^\s*-\s+", "", line)), body))
            story.append(Spacer(1, 4))
        elif line.strip() == "":
            story.append(Spacer(1, 4))
        else:
            story.append(Paragraph(md_inline(line), body))
            story.append(Spacer(1, 4))
        i += 1


def main() -> None:
    md = ROOT / "I2C_PROTOCOL_AND_PINS.md"
    pdf = ROOT / "I2C_PROTOCOL_AND_PINS.pdf"
    if not md.is_file():
        print("Missing:", md, file=sys.stderr)
        sys.exit(1)
    family, mono = register_cyrillic_fonts()
    st = build_styles(family, mono)
    story: list = []
    parse_md(md, story, st)
    doc = SimpleDocTemplate(
        str(pdf),
        pagesize=A4,
        leftMargin=2 * cm,
        rightMargin=2 * cm,
        topMargin=2 * cm,
        bottomMargin=2 * cm,
    )
    doc.build(story)
    print("Wrote", pdf)


if __name__ == "__main__":
    main()
