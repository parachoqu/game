#!/usr/bin/env python3
"""Gera a galeria offline e o catálogo sem modificar as imagens de origem.

Execute somente após a conclusão dos 14 PNGs:
  /home/https/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/bin/python3 \
    fontes/gerar_catalogo.py
"""

from __future__ import annotations

import html
import json
import re
from pathlib import Path
from urllib.parse import quote
from xml.sax.saxutils import escape

from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4, landscape
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.utils import ImageReader
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfgen import canvas
from reportlab.platypus import Paragraph


ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "fontes" / "manifesto.json"
EXPECTED_COUNT = 14
SOURCE = "Projeto_Game_Documento_Mestre_v2.1.pdf"
DISCLAIMER = (
    "Referências de conceito para orientar modelagem 3D, materiais e direção de arte; "
    "não são projeções técnicas certificadas. Todo detalhe não fixado no PDF é uma "
    "proposta visual. Escala, encaixes, anatomia e consistência entre vistas devem ser "
    "resolvidos durante a modelagem."
)
ART_DIRECTION = (
    "Cores e silhuetas legíveis inspiradas em League of Legends, com seriedade, "
    "materialidade e realismo inspirados em Skyrim. As identidades visuais são originais."
)
FAMILIES = {
    "01": ("ambientes", "Ambientes"),
    "02": ("ambientes", "Ambientes"),
    "03": ("ambientes", "Ambientes"),
    "04": ("ambientes", "Ambientes"),
    "05": ("ambientes", "Ambientes"),
    "06": ("ambientes", "Ambientes"),
    "07": ("personagens", "Personagens"),
    "08": ("personagens", "Personagens"),
    "09": ("personagens", "Personagens"),
    "10": ("armas", "Armas"),
    "11": ("armas", "Armas"),
    "12": ("montaria", "Montaria"),
    "13": ("objetos", "Objetos"),
    "14": ("magia", "Magia"),
}


def load_assets() -> list[dict]:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    assets = manifest.get("assets", [])
    if len(assets) != EXPECTED_COUNT:
        raise ValueError(f"O manifesto deve conter {EXPECTED_COUNT} pranchas; contém {len(assets)}.")
    seen = set()
    missing = []
    for asset in assets:
        for key in ("id", "slug", "title", "pages", "note"):
            if not isinstance(asset.get(key), str) or not asset[key].strip():
                raise ValueError(f"Campo ausente ou inválido no manifesto: {key}.")
        if not re.fullmatch(r"[0-9]{2}", asset["id"]):
            raise ValueError("O identificador da prancha deve conter dois dígitos.")
        if not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", asset["slug"]):
            raise ValueError(f"Nome de arquivo inválido: {asset['slug']}.")
        if asset["id"] in seen:
            raise ValueError(f"Identificador repetido: {asset['id']}.")
        seen.add(asset["id"])
        asset["path"] = ROOT / "imagens" / f"{asset['id']}-{asset['slug']}.png"
        asset["relative"] = asset["path"].relative_to(ROOT).as_posix()
        asset["family"], asset["family_label"] = FAMILIES.get(asset["id"], ("outros", "Outros"))
        if not asset["path"].is_file():
            missing.append(asset["relative"])
        else:
            image = ImageReader(str(asset["path"]))
            asset["size"] = image.getSize()
    if missing:
        raise FileNotFoundError("A coleção ainda não está completa. Faltam:\n" + "\n".join(missing))
    return sorted(assets, key=lambda item: item["id"])


def build_html(assets: list[dict]) -> str:
    esc = html.escape
    cards = []
    family_buttons = []
    for family, label in dict((a["family"], a["family_label"]) for a in assets).items():
        family_buttons.append(
            f'<button type="button" data-filter="{esc(family)}" aria-pressed="false">{esc(label)}</button>'
        )
    for asset in assets:
        src = quote(asset["relative"])
        cards.append(f'''
        <article class="card" id="prancha-{esc(asset['id'])}" data-family="{esc(asset['family'])}">
          <a class="art" href="{src}" aria-label="Abrir imagem original: {esc(asset['title'])}">
            <img src="{src}" alt="Prancha conceitual: {esc(asset['title'])}" loading="lazy"
                 width="{asset['size'][0]}" height="{asset['size'][1]}">
          </a>
          <div class="card-body">
            <div class="eyebrow"><span>{esc(asset['id'])} / {esc(asset['family_label'])}</span><span>Proposta visual</span></div>
            <h2>{esc(asset['title'])}</h2>
            <p class="reference">Documento v2.1 · páginas {esc(asset['pages'])}</p>
            <p class="note">{esc(asset['note'])}</p>
            <a class="original" href="{src}">Abrir PNG original <span aria-hidden="true">↗</span></a>
          </div>
        </article>''')
    template = '''<!doctype html>
<html lang="pt-BR">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Projeto Game — Biblioteca de conceitos 3D</title>
  <meta name="description" content="14 pranchas de conceito visual baseadas no Documento-Mestre v2.1, com referências para modelagem 3D.">
  <style>
    :root{color-scheme:dark;--bg:#101c1c;--surface:#172828;--line:#344746;--ink:#f1eee4;--muted:#b4c4bd;--accent:#e4b675;--radius:14px}
    *{box-sizing:border-box}html{scroll-behavior:smooth}body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.65 system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif}
    a{color:inherit}a:focus-visible,button:focus-visible{outline:3px solid var(--accent);outline-offset:5px}
    .skip{position:absolute;top:-80px;left:20px;padding:10px;background:var(--accent);color:#152120;z-index:10}.skip:focus{top:10px}
    .shell{width:min(1440px,calc(100% - 80px));margin:auto}.topbar{display:flex;justify-content:space-between;gap:20px;padding:26px 0;border-bottom:1px solid var(--line);font-size:12px;letter-spacing:.13em;text-transform:uppercase;color:var(--muted)}
    header{padding:62px 0 36px;max-width:1100px}.kicker{color:var(--accent);text-transform:uppercase;letter-spacing:.14em;font-size:12px;font-weight:700}
    h1{font-family:Georgia,"Times New Roman",serif;font-size:clamp(38px,5.2vw,76px);line-height:1.03;letter-spacing:-.04em;font-weight:400;margin:20px 0 25px}
    .intro{max-width:850px;font-size:19px;color:var(--muted)}.actions{display:flex;gap:14px;flex-wrap:wrap;margin-top:27px}.button{display:inline-flex;align-items:center;padding:10px 17px;border:1px solid var(--line);border-radius:7px;text-decoration:none;font-size:14px}.button.primary{background:var(--accent);border-color:var(--accent);color:#172321;font-weight:700}
    .context{display:grid;grid-template-columns:1fr 1fr;gap:28px;padding:24px 0 28px;border-top:1px solid var(--line);border-bottom:1px solid var(--line);margin-bottom:32px}.context p{margin:0;color:var(--muted);font-size:14px}.context strong{color:var(--ink);font-weight:600;display:block;margin-bottom:6px}
    .toolbar{display:flex;justify-content:space-between;align-items:center;gap:20px;margin-bottom:24px}.filters{display:flex;flex-wrap:wrap;gap:8px}button{font:inherit;font-size:13px;border:1px solid var(--line);background:transparent;color:var(--muted);padding:8px 13px;border-radius:100px;cursor:pointer}button[aria-pressed="true"]{color:var(--bg);background:var(--ink);border-color:var(--ink)}.count{color:var(--muted);font-size:13px;white-space:nowrap}
    .grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:24px}.card{background:var(--surface);border:1px solid var(--line);border-radius:var(--radius);overflow:hidden;display:flex;flex-direction:column}.card[hidden]{display:none}.art{display:block;aspect-ratio:3/2;background:#dddeda;overflow:hidden}.art img{display:block;width:100%;height:100%;object-fit:contain}.card-body{padding:23px;display:flex;flex-direction:column;flex:1}.eyebrow{display:flex;justify-content:space-between;gap:10px;font-size:10px;letter-spacing:.08em;text-transform:uppercase;color:var(--accent)}h2{font-family:Georgia,"Times New Roman",serif;font-size:25px;line-height:1.16;font-weight:400;margin:15px 0 12px}.reference{font-size:11px;color:var(--muted);margin:0 0 14px}.note{font-size:14px;color:var(--muted);margin:0 0 24px}.original{margin-top:auto;display:flex;justify-content:space-between;font-size:13px;text-decoration:none;border-top:1px solid var(--line);padding-top:14px}.original:hover,.button:hover{color:var(--accent)}.button.primary:hover{color:#172321;filter:brightness(1.08)}footer{border-top:1px solid var(--line);padding:25px 0 40px;margin-top:52px;color:var(--muted);font-size:12px;display:flex;justify-content:space-between;gap:25px}
    @media(max-width:1100px){.grid{grid-template-columns:repeat(2,minmax(0,1fr))}.shell{width:calc(100% - 48px)}}
    @media(max-width:680px){.shell{width:calc(100% - 32px)}.topbar{font-size:10px;gap:12px}header{padding-top:40px}.intro{font-size:17px}.context{grid-template-columns:1fr;gap:18px}.toolbar{align-items:flex-start;flex-direction:column;gap:12px}.grid{grid-template-columns:1fr;gap:20px}.card-body{padding:20px}footer{flex-direction:column;gap:6px}h1{font-size:43px}}
    @media(prefers-reduced-motion:reduce){html{scroll-behavior:auto}}
    @media print{body{background:white;color:#152321}.shell{width:100%}.topbar,.toolbar,.actions,.original{display:none}.grid{grid-template-columns:repeat(2,minmax(0,1fr))}.card{break-inside:avoid;background:white;border-color:#aaa}.card-body{padding:12px}.context p,.note,.reference,footer{color:#354b48}.context strong{color:#152321}header{padding:15px 0}.eyebrow{color:#785329}h1{font-size:38px}}
  </style>
</head>
<body>
<a class="skip" href="#colecao">Ir para a coleção</a>
<div class="shell">
  <div class="topbar"><span>Projeto Game / desenvolvimento visual</span><span>23 setembro 2026</span></div>
  <header>
    <div class="kicker">14 pranchas · mundo, personagens e objetos</div>
    <h1>Um mundo para<br>ganhar forma.</h1>
    <p class="intro">Biblioteca de conceitos para modelagem 3D baseada na documentação atual do game. @@ART_DIRECTION@@</p>
    <nav class="actions" aria-label="Arquivos da coleção">
      <a class="button primary" href="Catalogo_Conceitos_3D.pdf">Abrir catálogo em PDF</a>
      <a class="button" href="GUIA_DE_ARTE_E_MODELAGEM.md">Guia de arte e modelagem</a>
      <a class="button" href="PROMPTS_GERACAO.md">Prompts das imagens</a>
    </nav>
  </header>
  <aside class="context" aria-label="Como usar esta coleção">
    <p><strong>Base documental</strong>@@SOURCE@@ · 25 páginas, incluindo o adendo final sobre armas, magia e especialização. As notas de cada prancha distinguem a base registrada das propostas de aparência.</p>
    <p><strong>Do conceito ao modelo</strong>@@DISCLAIMER@@</p>
  </aside>
  <main id="colecao">
    <div class="toolbar">
      <div class="filters" aria-label="Filtrar por família">
        <button type="button" data-filter="all" aria-pressed="true">Todas</button>
        @@FILTERS@@
      </div>
      <span class="count" id="count" role="status" aria-live="polite">14 pranchas</span>
    </div>
    <div class="grid">@@CARDS@@</div>
  </main>
  <footer><span>Referências de conceito · propostas visuais para um universo original</span><span>Imagens inteiras, disponíveis em PNG</span></footer>
</div>
<script>
  const buttons = Array.from(document.querySelectorAll('[data-filter]'));
  const cards = Array.from(document.querySelectorAll('.card'));
  const count = document.getElementById('count');
  buttons.forEach(button => button.addEventListener('click', () => {
    const selected = button.dataset.filter;
    buttons.forEach(item => item.setAttribute('aria-pressed', String(item === button)));
    let visible = 0;
    cards.forEach(card => {
      card.hidden = selected !== 'all' && card.dataset.family !== selected;
      if (!card.hidden) visible += 1;
    });
    count.textContent = `${visible} ${visible === 1 ? 'prancha' : 'pranchas'}`;
  }));
</script>
</body>
</html>
'''
    return (template.replace("@@SOURCE@@", esc(SOURCE))
            .replace("@@ART_DIRECTION@@", esc(ART_DIRECTION))
            .replace("@@DISCLAIMER@@", esc(DISCLAIMER))
            .replace("@@FILTERS@@", "\n".join(family_buttons))
            .replace("@@CARDS@@", "\n".join(cards)))


def register_fonts() -> None:
    candidates = [Path("/usr/share/fonts/truetype/dejavu"), Path("/usr/share/fonts/dejavu")]
    for directory in candidates:
        if (directory / "DejaVuSans.ttf").is_file() and (directory / "DejaVuSans-Bold.ttf").is_file():
            pdfmetrics.registerFont(TTFont("DejaVu", str(directory / "DejaVuSans.ttf")))
            pdfmetrics.registerFont(TTFont("DejaVu-Bold", str(directory / "DejaVuSans-Bold.ttf")))
            pdfmetrics.registerFontFamily("DejaVu", normal="DejaVu", bold="DejaVu-Bold")
            return
    raise FileNotFoundError("As fontes DejaVu Sans normal e negrito não foram encontradas.")


PAPER_W, PAPER_H = landscape(A4)
INK = colors.HexColor("#17312e")
MUTED = colors.HexColor("#546763")
CREAM = colors.HexColor("#f3f0e8")
LINE = colors.HexColor("#cfdbd4")
GOLD = colors.HexColor("#ae8045")
MARGIN = 32


def paragraph(c, text, x, top, width, size=10, leading=None, color=INK, bold=False):
    style = ParagraphStyle(
        "catalog", fontName="DejaVu-Bold" if bold else "DejaVu",
        fontSize=size, leading=leading or size * 1.4,
        textColor=color, alignment=TA_LEFT, spaceBefore=0, spaceAfter=0,
    )
    item = Paragraph(escape(text), style)
    _, height = item.wrap(width, PAPER_H)
    item.drawOn(c, x, top - height)
    return height


def contain_image(c, asset, x, y, width, height):
    iw, ih = asset["size"]
    scale = min(width / iw, height / ih)
    draw_w, draw_h = iw * scale, ih * scale
    c.drawImage(str(asset["path"]), x + (width - draw_w) / 2, y + (height - draw_h) / 2,
                width=draw_w, height=draw_h, preserveAspectRatio=True, mask="auto")


def footer(c, number, index_link=True):
    c.setStrokeColor(LINE)
    c.setLineWidth(0.5)
    c.line(MARGIN, 27, PAPER_W - MARGIN, 27)
    c.setFont("DejaVu", 7)
    c.setFillColor(MUTED)
    c.drawString(MARGIN, 15, "PROJETO GAME · Conceitos para modelagem 3D · 23.09.2026")
    if index_link:
        c.drawRightString(PAPER_W - 65, 15, "Índice visual")
        c.linkRect("", "indice-1", (PAPER_W - 130, 10, PAPER_W - 60, 25), relative=0, thickness=0)
    c.drawRightString(PAPER_W - MARGIN, 15, f"{number:02d}")


def build_pdf(assets: list[dict], destination: Path):
    register_fonts()
    c = canvas.Canvas(str(destination), pagesize=(PAPER_W, PAPER_H), pageCompression=1)
    c.setTitle("Projeto Game — Catálogo de conceitos para modelagem 3D")
    c.setAuthor("Projeto Game · Desenvolvimento visual")
    c.setSubject("14 propostas visuais baseadas no Documento-Mestre v2.1, incluindo o adendo final")
    c.setCreator("Catálogo de conceitos do Projeto Game")

    # Capa: uma imagem inteira e o contexto de uso, sem recorte ou alteração.
    c.setFillColor(CREAM)
    c.rect(0, 0, PAPER_W, PAPER_H, fill=1, stroke=0)
    c.bookmarkPage("capa")
    c.addOutlineEntry("Capa", "capa", level=0)
    paragraph(c, "PROJETO GAME / DESENVOLVIMENTO VISUAL", MARGIN, PAPER_H - 30, PAPER_W - 2 * MARGIN,
              size=9, color=GOLD, bold=True)
    paragraph(c, "Conceitos para modelagem 3D", MARGIN, PAPER_H - 58, PAPER_W - 2 * MARGIN,
              size=28, leading=34, bold=True)
    paragraph(c, "14 pranchas · 23 setembro 2026 · Documento-Mestre v2.1", MARGIN, PAPER_H - 103,
              PAPER_W - 2 * MARGIN, size=10, color=MUTED)
    image_x = 308
    image_y = 151
    image_w = PAPER_W - image_x - MARGIN
    image_h = 298
    contain_image(c, assets[0], image_x, image_y, image_w, image_h)
    top = 438
    top -= paragraph(c, "Mundo, trajetórias e memória", MARGIN, top, 250, size=19, leading=25, bold=True) + 17
    top -= paragraph(c, ART_DIRECTION, MARGIN, top, 240, size=10.5, leading=15.5) + 16
    paragraph(c, "Ambientes, arquitetura, recursos, personagens, armas, montaria, objetos e magia.",
              MARGIN, top, 240, size=10.5, leading=15.5, color=MUTED)
    paragraph(c, DISCLAIMER, MARGIN, 131, PAPER_W - 2 * MARGIN, size=9, leading=13, color=MUTED)
    paragraph(c, f"Fonte: {SOURCE} · 25 páginas, incluindo o adendo final.",
              MARGIN, 73, PAPER_W - 2 * MARGIN, size=8, color=MUTED)
    footer(c, 1, index_link=False)
    c.showPage()

    # Dois índices com oito/seis miniaturas, usando os próprios PNGs.
    page_number = 2
    for batch_no, start in enumerate(range(0, len(assets), 8), 1):
        c.bookmarkPage(f"indice-{batch_no}")
        c.addOutlineEntry(f"Índice visual {batch_no}", f"indice-{batch_no}", level=0)
        paragraph(c, f"Índice visual / {batch_no} de 2", MARGIN, PAPER_H - 30,
                  PAPER_W - 2 * MARGIN, size=23, leading=28, bold=True)
        paragraph(c, "Selecione uma miniatura para abrir a prancha no catálogo.",
                  MARGIN, PAPER_H - 66, PAPER_W - 2 * MARGIN, size=9, color=MUTED)
        gap_x, gap_y = 16, 16
        cell_w = (PAPER_W - 2 * MARGIN - gap_x * 3) / 4
        cell_h = 211
        for index, asset in enumerate(assets[start:start + 8]):
            col, row = index % 4, index // 4
            x = MARGIN + col * (cell_w + gap_x)
            top = PAPER_H - 103 - row * (cell_h + gap_y)
            thumb_h = 124
            c.setFillColor(CREAM)
            c.rect(x, top - thumb_h, cell_w, thumb_h, fill=1, stroke=0)
            contain_image(c, asset, x, top - thumb_h, cell_w, thumb_h)
            c.linkRect("", f"prancha-{asset['id']}", (x, top - cell_h, x + cell_w, top),
                       relative=0, thickness=0)
            title_top = top - thumb_h - 8
            title_h = paragraph(c, f"{asset['id']}  {asset['title']}", x, title_top,
                                cell_w, size=9.1, leading=12, bold=True)
            paragraph(c, f"PDF-base: p. {asset['pages']}", x, title_top - title_h - 5,
                      cell_w, size=7.5, leading=10, color=MUTED)
        footer(c, page_number, index_link=batch_no > 1)
        c.showPage()
        page_number += 1

    for asset in assets:
        c.bookmarkPage(f"prancha-{asset['id']}")
        c.addOutlineEntry(f"{asset['id']} · {asset['title']}", f"prancha-{asset['id']}", level=0)
        paragraph(c, f"{asset['id']} / {asset['family_label'].upper()} / PROPOSTA VISUAL", MARGIN,
                  PAPER_H - 25, PAPER_W - 2 * MARGIN, size=8, color=GOLD, bold=True)
        title_h = paragraph(c, asset["title"], MARGIN, PAPER_H - 44, PAPER_W - 2 * MARGIN,
                            size=21, leading=26, bold=True)
        note_text = f"Base documental: páginas {asset['pages']}. {asset['note']}"
        note_style = ParagraphStyle("note", fontName="DejaVu", fontSize=8.5, leading=12, textColor=MUTED)
        note = Paragraph(escape(note_text), note_style)
        _, note_h = note.wrap(PAPER_W - 2 * MARGIN, 150)
        note.drawOn(c, MARGIN, 41)
        image_bottom = 41 + note_h + 12
        image_top = PAPER_H - 44 - title_h - 12
        if image_top - image_bottom < 200:
            raise ValueError(f"Texto excessivo para a prancha {asset['id']}.")
        contain_image(c, asset, MARGIN, image_bottom, PAPER_W - 2 * MARGIN, image_top - image_bottom)
        footer(c, page_number)
        c.showPage()
        page_number += 1
    c.save()


def main():
    assets = load_assets()
    html_content = build_html(assets)
    pdf_final = ROOT / "Catalogo_Conceitos_3D.pdf"
    html_final = ROOT / "GALERIA.html"
    pdf_temp = pdf_final.with_suffix(".pdf.tmp")
    html_temp = html_final.with_suffix(".html.tmp")
    try:
        build_pdf(assets, pdf_temp)
        html_temp.write_text(html_content, encoding="utf-8")
        pdf_temp.replace(pdf_final)
        html_temp.replace(html_final)
    finally:
        pdf_temp.unlink(missing_ok=True)
        html_temp.unlink(missing_ok=True)
    print(f"Galeria criada: {html_final}")
    print(f"Catálogo criado: {pdf_final}")
    print(f"{len(assets)} imagens originais incluídas; nenhuma imagem foi modificada.")


if __name__ == "__main__":
    main()
