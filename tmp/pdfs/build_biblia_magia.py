from __future__ import annotations

import html
import os
import re
from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER, TA_JUSTIFY, TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (
    BaseDocTemplate,
    CondPageBreak,
    Flowable,
    HRFlowable,
    NextPageTemplate,
    PageBreak,
    PageTemplate,
    Paragraph,
    Spacer,
    Table,
    TableStyle,
)
from reportlab.platypus.tableofcontents import TableOfContents


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "output" / "pdf" / "Biblia_da_Magia_Consciencia_Materia_e_Tempo.pdf"

PAGE_W, PAGE_H = A4
M_LEFT = 24 * mm
M_RIGHT = 22 * mm
M_TOP = 24 * mm
M_BOTTOM = 20 * mm

INK = colors.HexColor("#202431")
NAVY = colors.HexColor("#171A2F")
INDIGO = colors.HexColor("#29264D")
VIOLET = colors.HexColor("#725AA3")
PALE_VIOLET = colors.HexColor("#EEEAF6")
GOLD = colors.HexColor("#C69A52")
PALE_GOLD = colors.HexColor("#F4ECDD")
PAPER = colors.HexColor("#FBF9F4")
MUTED = colors.HexColor("#666778")
RULE = colors.HexColor("#D7D1C5")


for name, path in {
    "DVSerif": "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf",
    "DVSerif-Bold": "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf",
    "DVSerif-Italic": "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Italic.ttf",
    "DVSans": "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "DVSans-Bold": "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "DVSans-Oblique": "/usr/share/fonts/truetype/dejavu/DejaVuSans-Oblique.ttf",
    "DVMono": "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
}.items():
    pdfmetrics.registerFont(TTFont(name, path))


class MagicBibleDoc(BaseDocTemplate):
    def __init__(self, filename: str, **kwargs):
        super().__init__(filename, **kwargs)
        self.section_title = "Bíblia da Magia"
        frame = self._make_frame()
        self.addPageTemplates(
            [
                PageTemplate(id="Cover", frames=[frame], onPage=self._cover_page),
                PageTemplate(id="Body", frames=[frame], onPage=self._body_page),
            ]
        )

    def _make_frame(self):
        from reportlab.platypus import Frame

        return Frame(
            M_LEFT,
            M_BOTTOM,
            PAGE_W - M_LEFT - M_RIGHT,
            PAGE_H - M_TOP - M_BOTTOM,
            id="main",
            leftPadding=0,
            rightPadding=0,
            topPadding=0,
            bottomPadding=0,
        )

    def _cover_page(self, canvas, doc):
        canvas.saveState()
        canvas.setFillColor(NAVY)
        canvas.rect(0, 0, PAGE_W, PAGE_H, fill=1, stroke=0)
        canvas.setFillColor(VIOLET)
        canvas.circle(PAGE_W * 0.84, PAGE_H * 0.82, 62 * mm, fill=1, stroke=0)
        canvas.setFillColor(INDIGO)
        canvas.circle(PAGE_W * 0.87, PAGE_H * 0.85, 48 * mm, fill=1, stroke=0)
        canvas.setStrokeColor(GOLD)
        canvas.setLineWidth(1.1)
        for radius in (16, 27, 39):
            canvas.circle(PAGE_W * 0.84, PAGE_H * 0.82, radius * mm, fill=0, stroke=1)
        canvas.setFillColor(GOLD)
        for x, y, r in (
            (0.76, 0.87, 1.5),
            (0.89, 0.90, 1.2),
            (0.82, 0.75, 1.0),
            (0.92, 0.79, 1.4),
            (0.71, 0.78, 0.9),
        ):
            canvas.circle(PAGE_W * x, PAGE_H * y, r * mm, fill=1, stroke=0)
        canvas.setStrokeColor(colors.Color(1, 1, 1, alpha=0.13))
        canvas.setLineWidth(0.8)
        canvas.line(22 * mm, 25 * mm, PAGE_W - 22 * mm, 25 * mm)
        canvas.setFont("DVSans", 8)
        canvas.setFillColor(colors.Color(1, 1, 1, alpha=0.68))
        canvas.drawString(24 * mm, 17 * mm, "PROJETO GAME / CÂNONE DE AUTORIA")
        canvas.drawRightString(PAGE_W - 24 * mm, 17 * mm, "VERSÃO 1.0")
        canvas.restoreState()

    def _body_page(self, canvas, doc):
        canvas.saveState()
        canvas.setFillColor(PAPER)
        canvas.rect(0, 0, PAGE_W, PAGE_H, fill=1, stroke=0)
        canvas.setStrokeColor(RULE)
        canvas.setLineWidth(0.5)
        canvas.line(M_LEFT, PAGE_H - 14 * mm, PAGE_W - M_RIGHT, PAGE_H - 14 * mm)
        canvas.setFont("DVSans", 7.2)
        canvas.setFillColor(MUTED)
        header = self.section_title.upper()
        canvas.drawString(M_LEFT, PAGE_H - 10.5 * mm, header[:82])
        canvas.line(M_LEFT, 13.5 * mm, PAGE_W - M_RIGHT, 13.5 * mm)
        canvas.drawString(M_LEFT, 8.5 * mm, "CONSCIÊNCIA, MATÉRIA E TEMPO")
        canvas.drawRightString(PAGE_W - M_RIGHT, 8.5 * mm, str(doc.page))
        canvas.restoreState()

    def afterFlowable(self, flowable):
        if not isinstance(flowable, Paragraph):
            return
        style_name = flowable.style.name
        if style_name not in {"H1", "H2"}:
            return
        text = flowable.getPlainText()
        level = 0 if style_name == "H1" else 1
        key = f"toc-{self.seq.nextf('toc')}"
        self.canv.bookmarkPage(key)
        self.canv.addOutlineEntry(text, key, level=level, closed=False)
        self.notify("TOCEntry", (level, text, self.page, key))
        if style_name == "H1":
            self.section_title = text


styles = getSampleStyleSheet()
STYLES = {
    "CoverKicker": ParagraphStyle(
        "CoverKicker",
        fontName="DVSans-Bold",
        fontSize=8.5,
        leading=11,
        textColor=GOLD,
        spaceAfter=18,
        tracking=1.7,
    ),
    "CoverTitle": ParagraphStyle(
        "CoverTitle",
        fontName="DVSerif-Bold",
        fontSize=29,
        leading=34,
        textColor=colors.white,
        spaceAfter=11,
    ),
    "CoverSubtitle": ParagraphStyle(
        "CoverSubtitle",
        fontName="DVSerif",
        fontSize=14,
        leading=20,
        textColor=colors.HexColor("#DDD7EA"),
        spaceAfter=28,
    ),
    "CoverMeta": ParagraphStyle(
        "CoverMeta",
        fontName="DVSans",
        fontSize=8.4,
        leading=13,
        textColor=colors.HexColor("#C9C5D4"),
        spaceAfter=5,
    ),
    "CoverQuote": ParagraphStyle(
        "CoverQuote",
        fontName="DVSerif-Italic",
        fontSize=11.2,
        leading=17,
        textColor=colors.HexColor("#F3E8D4"),
        leftIndent=9 * mm,
        rightIndent=16 * mm,
        borderColor=GOLD,
        borderWidth=1,
        borderPadding=(0, 0, 0, 9 * mm),
        spaceBefore=19,
    ),
    "H1": ParagraphStyle(
        "H1",
        fontName="DVSerif-Bold",
        fontSize=19,
        leading=23,
        textColor=INDIGO,
        spaceBefore=2,
        spaceAfter=11,
        keepWithNext=True,
    ),
    "H2": ParagraphStyle(
        "H2",
        fontName="DVSans-Bold",
        fontSize=11.8,
        leading=15,
        textColor=VIOLET,
        spaceBefore=10,
        spaceAfter=5,
        keepWithNext=True,
    ),
    "H3": ParagraphStyle(
        "H3",
        fontName="DVSans-Bold",
        fontSize=9.6,
        leading=13,
        textColor=INK,
        spaceBefore=8,
        spaceAfter=3,
        keepWithNext=True,
    ),
    "Body": ParagraphStyle(
        "Body",
        fontName="DVSerif",
        fontSize=9.35,
        leading=14.1,
        textColor=INK,
        alignment=TA_JUSTIFY,
        spaceAfter=6.3,
        allowWidows=0,
        allowOrphans=0,
    ),
    "Bullet": ParagraphStyle(
        "Bullet",
        fontName="DVSerif",
        fontSize=9.15,
        leading=13.2,
        textColor=INK,
        leftIndent=6 * mm,
        firstLineIndent=-3.5 * mm,
        bulletIndent=1.5 * mm,
        spaceAfter=2.5,
    ),
    "Number": ParagraphStyle(
        "Number",
        fontName="DVSerif",
        fontSize=9.15,
        leading=13.2,
        textColor=INK,
        leftIndent=8 * mm,
        firstLineIndent=-5 * mm,
        bulletIndent=0,
        spaceAfter=3,
    ),
    "Quote": ParagraphStyle(
        "Quote",
        fontName="DVSerif-Italic",
        fontSize=10,
        leading=15,
        textColor=INDIGO,
        leftIndent=8 * mm,
        rightIndent=8 * mm,
        borderColor=GOLD,
        borderWidth=1,
        borderPadding=(5, 8, 5, 8),
        backColor=PALE_GOLD,
        spaceBefore=5,
        spaceAfter=9,
    ),
    "Callout": ParagraphStyle(
        "Callout",
        fontName="DVSans",
        fontSize=8.7,
        leading=13,
        textColor=INDIGO,
        leftIndent=4 * mm,
        rightIndent=4 * mm,
        borderColor=colors.HexColor("#CFC7E2"),
        borderWidth=0.7,
        borderPadding=8,
        backColor=PALE_VIOLET,
        spaceBefore=5,
        spaceAfter=8,
    ),
    "Code": ParagraphStyle(
        "Code",
        fontName="DVMono",
        fontSize=7.7,
        leading=11.4,
        textColor=INDIGO,
        leftIndent=5 * mm,
        rightIndent=5 * mm,
        borderColor=RULE,
        borderWidth=0.7,
        borderPadding=7,
        backColor=colors.HexColor("#F3F1EC"),
        spaceBefore=4,
        spaceAfter=8,
    ),
    "TOCTitle": ParagraphStyle(
        "TOCTitle",
        fontName="DVSerif-Bold",
        fontSize=23,
        leading=28,
        textColor=INDIGO,
        spaceAfter=15,
    ),
    "Small": ParagraphStyle(
        "Small",
        fontName="DVSans",
        fontSize=7.8,
        leading=11.5,
        textColor=MUTED,
        spaceAfter=5,
    ),
}


CONTENT = r'''
# CHAVE DE LEITURA

Esta Bíblia trabalha com três níveis distintos de conhecimento. A separação é indispensável para que a fantasia utilize conceitos científicos sem apresentar especulação como fato comprovado.

## Física conhecida

Conceitos inspirados em fenômenos ou teorias reais: estados físicos, transferência de energia, atividade eletroquímica do cérebro, relatividade, memória reconstrutiva, correlações quânticas e propriedades emergentes.

## Hipótese filosófica

Interpretações que não constituem conclusões estabelecidas da física: o tempo como propriedade emergente, o universo descrito integralmente por um único estado físico e a consciência entendida como uma região do universo capaz de representar a si mesma.

## Lei ficcional

Determinadas consciências treinadas conseguem acoplar seus modelos internos a transformações físicas externas por meio de materiais preparados para essa função. Essa é a divergência fundamental entre o universo do game e a nossa realidade conhecida.

## Camadas narrativas

- **Observação:** aquilo que pode ser medido, repetido ou testemunhado.
- **Interpretação:** a explicação defendida por uma cultura, instituição ou pessoa.
- **Segredo de autoria:** a verdade usada pelos criadores para manter todas as pistas coerentes.

Uma interpretação pode produzir práticas eficazes sem compreender completamente o fenômeno. Um artesão não precisa conhecer toda a física da combustão para manter uma forja acesa; da mesma forma, pode fabricar um foco confiável sem conhecer o estado global do universo.

# 1. PREMISSA CENTRAL

Matéria, consciência e tempo são manifestações de um mesmo estado global.

O estado global não é uma substância invisível, uma divindade ou uma mente universal. É a totalidade física do universo: todas as suas propriedades, relações, transformações e correlações consideradas como partes de uma única realidade.

Aquilo que chamamos de objeto é uma configuração local relativamente estável desse estado. Uma pedra, uma árvore, um animal, uma estrela e um corpo consciente não são feitos de realidades diferentes. O que os distingue é a forma como a matéria se organiza, preserva informação e responde ao ambiente.

Uma onda pode ser medida como algo particular, mas nunca deixa de ser uma configuração do oceano. Da mesma maneira, uma pessoa possui identidade, memória e continuidade próprias, embora continue sendo uma configuração temporária do mesmo universo que forma montanhas, rios e astros.

A consciência surge quando a matéria alcança organização suficiente para perceber o ambiente, preservar memórias, construir modelos do mundo, representar o próprio corpo, reconhecer a própria continuidade e imaginar estados que ainda não ocorreram.

Nesse sentido, uma consciência é uma região do universo na qual ele adquiriu a capacidade de produzir uma representação local de si mesmo.

O tempo é experimentado por essa consciência como uma sequência: antes, agora e depois. Essa sequência continua real e importante na escala cotidiana. Causas produzem consequências, corpos envelhecem, ferimentos deixam marcas e decisões não podem ser desfeitas livremente.

Entretanto, no nível mais profundo do cenário, a ordem temporal não é uma substância independente. Ela emerge das relações entre mudanças, memórias, registros e consequências. O presente é a posição a partir da qual uma consciência participa dessa estrutura.

> Magia não é a criação do impossível. É a capacidade de favorecer, estabilizar e amplificar uma transformação que o universo ainda pode assumir.

# 2. A LEI FICCIONAL FUNDAMENTAL

Na causalidade comum, pensamentos alteram o mundo por meio do comportamento: alguém imagina uma ferramenta, movimenta o corpo, trabalha a matéria e constrói o objeto.

Neste universo existe uma possibilidade adicional. Determinadas configurações conscientes conseguem acoplar a representação interna de um estado futuro a processos físicos externos. Para isso, precisam de uma estrutura material que conserve o padrão mental por tempo suficiente para que ele seja transferido, amplificado e realizado.

Essa estrutura é chamada, no vocabulário de autoria, de **acoplador**. Um cajado, tomo, amuleto, círculo ritual ou instrumento pode funcionar como acoplador. Quando portátil e preparado para uso individual, ele costuma ser chamado de **foco**. Quando permanece no ambiente sustentando um efeito, recebe o nome de **âncora**.

A consciência não recebe poder especial apenas por observar alguma coisa. Medição continua podendo ocorrer por interações materiais sem a presença de uma mente. O que distingue a magia é uma ação treinada: a consciência constrói um estado-alvo, coordena o próprio organismo, usa um acoplador e fornece condições físicas para que aquele estado se realize.

Pensamento comum não produz feitiços porque é instável, impreciso e rapidamente substituído por outros pensamentos. Desejos cotidianos não possuem definição física suficiente para orientar uma transformação externa.

Um praticante precisa aprender a transformar intenção em estrutura. Querer que algo congele ainda é vago. Um estado-alvo útil precisa delimitar região, matéria envolvida, direção da transferência, duração, propagação e encerramento.

Culturas diferentes ensinam essa estrutura por métodos distintos. Algumas usam geometria e contagem. Outras recorrem a música, respiração, imagens mentais, movimentos, histórias ou símbolos religiosos. O vocabulário varia, mas a função permanece a mesma.

# 3. O QUE ACONTECE DURANTE UMA MANIFESTAÇÃO

A magia coordena mente, corpo, foco e ambiente para favorecer uma transformação possível e mantê-la estável.

> Uma manifestação se fortalece com intenção precisa, treino, foco adequado, energia disponível e proximidade física do resultado. Ela enfraquece diante da carga causal, da resistência concorrente e da perda de coerência operacional.

Essa frase é um mnemônico de autoria, não uma equação científica.

## 3.1 Representação

O praticante constrói mentalmente um **estado-alvo**. Esse estado não precisa ser imaginado como uma imagem perfeita. Um ferreiro pode compreender calor por anos de experiência corporal, enquanto uma curandeira representa pulsação, fluxo sanguíneo e estabilidade sem conhecer anatomia acadêmica.

A precisão pode vir da teoria, da prática ou de uma tradição transmitida corretamente.

## 3.2 Sincronização

Respiração, postura, ritmo, gesto, canto ou palavra alinham a atividade do cérebro com o restante do corpo. Essas práticas ajudam o organismo a repetir um estado físico e mental específico. Uma palavra ritual funciona quando faz parte de um protocolo aprendido; pronunciá-la sem treinamento não produz o mesmo resultado.

## 3.3 Acoplamento

O foco recebe e conserva o padrão. Sua forma, seus materiais e sua fabricação determinam quais transformações ele sustenta com maior eficiência. Um cajado não é poderoso apenas por ser antigo ou ornamentado. Sua geometria, seus materiais e as marcas deixadas por preparação e uso estabelecem caminhos preferenciais para a manifestação.

## 3.4 Alimentação

A transformação recebe matéria e energia. Uma chama pode usar energia química armazenada no foco, material inflamável no ambiente ou calor acumulado anteriormente. Um efeito de gelo utiliza um substrato hídrico transportado pelo foco, a umidade local ou água disponível. Uma cura usa reservas metabólicas, nutrientes e matéria corporal.

Probabilidade não substitui conservação.

## 3.5 Seleção

O conjunto formado por consciência, corpo, foco e ambiente favorece uma trajetória física compatível com o estado-alvo. O praticante não escolhe livremente entre todas as realidades imagináveis. Ele estreita o conjunto de resultados que aquele sistema ainda pode alcançar.

Congelar uma pequena quantidade de água é uma alteração próxima. Criar uma geleira instantânea no deserto exigiria matéria, energia e reorganização em escala impossível para um praticante individual.

## 3.6 Estabilização

O efeito precisa permanecer coerente durante sua formação. Dano, dor, distração ou oposição podem desalinhá-lo. Isso é chamado de **perda de coerência operacional**: mente, corpo, foco e ambiente deixam de sustentar o mesmo estado-alvo.

O termo não deve ser confundido com decoerência quântica em seu sentido técnico.

## 3.7 Dissipação

Toda manifestação deixa consequências. O custo pode aparecer como fadiga, calor, frio deslocado, desidratação, consumo de reagentes, desgaste do foco, alteração química, deformação, resíduos anômalos ou instabilidade ambiental.

Um feitiço não termina quando o efeito visual desaparece. A matéria utilizada continua obedecendo às consequências do processo.

## 3.8 O pensamento pode moldar a realidade

A frase **"o pensamento pode moldar a realidade"** possui dois sentidos diferentes neste universo.

O primeiro é comum a qualquer sociedade: pensamentos produzem decisões, decisões produzem ações, ações reorganizam matéria e essas mudanças transformam pessoas, comunidades e acontecimentos. Uma construção, uma guerra, uma ferramenta ou uma instituição podem começar como representações mentais e terminar como realidades físicas.

A magia acrescenta uma segunda possibilidade a essa cadeia.

FORMULA: pensamento -> estado-alvo -> acoplamento -> transformação física

Nesse processo, o pensamento não funciona como combustível. Ele funciona como **informação**. É a mente que define qual transformação se deseja produzir, onde ela deve acontecer, quais limites precisa respeitar, quanto tempo deve durar e quando deve terminar.

O corpo, o foco e o ambiente fornecem as condições físicas para que essa representação se torne matéria organizada.

Pensamentos comuns e não treinados possuem **efeito mágico externo nulo**. Desejar intensamente, ter pensamentos positivos, sentir medo ou imaginar um acontecimento não altera probabilidades por si só. Não existe uma lei da atração operando passivamente no universo.

Para que um pensamento molde diretamente a realidade, ele precisa ser convertido em uma intenção executável por treinamento, representação precisa, coerência operacional, foco ou âncora, energia e matéria disponíveis e uma transformação fisicamente possível.

A força de um feitiço não depende apenas da intensidade emocional do praticante. Uma intenção calma e precisa costuma ser mais eficaz que um desejo desesperado e desorganizado.

> O pensamento fornece a forma. O foco conserva essa forma. O corpo e o ambiente pagam seu custo. A matéria realiza apenas aquilo que ainda pode se tornar.

# 4. QUEM PODE PRATICAR MAGIA

Toda mente suficientemente autoconsciente possui potencial mágico. Isso não significa que todos possuam a mesma aptidão, acesso ou interesse. A universalidade do potencial elimina bloqueios biológicos absolutos; não elimina desigualdades de ensino, custo, prestígio e infraestrutura.

Magia não depende obrigatoriamente de raça, sangue, classe permanente, profissão exclusiva ou linhagem escolhida por uma divindade. Uma pessoa pode aprender depois de anos como artesã, abandonar temporariamente a prática, mudar de tradição ou combinar conhecimento mágico com armas e profissões civis.

O conhecimento pertence à pessoa. O foco determina quais manifestações ela consegue preparar naquele momento. Perder um cajado não apaga o treinamento. Possuir um tomo não concede automaticamente a compreensão necessária para operá-lo.

## 4.1 Aptidão

Aptidão altera velocidade de aprendizado, precisão, tolerância ao esforço, facilidade de visualização, capacidade de manter atenção, sensibilidade aos sinais do foco e resistência à interferência.

Aptidão não equivale a destino. Um indivíduo talentoso sem disciplina pode ser menos confiável que alguém de capacidade comum e treinamento rigoroso.

## 4.2 Vigor

Não existe mana como substância separada do corpo. Técnicas físicas e mágicas dependem da mesma capacidade orgânica geral, representada no jogo pelo vigor. Conjurar exige atividade cerebral, controle motor, respiração, metabolismo e resistência ao estresse.

O foco organiza o processo, mas não substitui o praticante nem fornece energia infinita. Excesso de uso pode causar tremores, perda de precisão, náusea, confusão, colapso muscular, desmaio e incapacidade temporária de construir estados-alvo.

Uma técnica que recupera vigor não cria energia. Ela reorganiza respiração, circulação, tensão muscular e reservas metabólicas para restaurar temporariamente a capacidade funcional.

## 4.3 Outras formas de consciência

A magia não é exclusivamente humana. Animais dotados de elevada autorreferência podem desenvolver manifestações instintivas. Algumas criaturas incorporam acopladores naturais no corpo, como ossos cristalizados, tecidos condutores ou órgãos capazes de preservar padrões.

Uma construção ou inteligência artificial que atingisse autoconsciência genuína também possuiria potencial. O critério é a organização física do processo consciente, não o material de que ele nasceu. Essa possibilidade é segredo de autoria, não conhecimento comum.

# 5. LEIS E LIMITES

## 5.1 Conservação

Matéria, energia e impulso não aparecem do nada. Uma manifestação pode transferir, concentrar, liberar, armazenar ou reorganizar. Ela não ignora indefinidamente o custo material.

Efeitos de combate são possíveis em diferentes ambientes porque focos preparados carregam um substrato mínimo: água, reagentes, sementes, fibras, sais, pigmentos, metais ou carga acumulada. Esse suprimento permite manifestações breves. Efeitos de grande escala, longa duração ou uso produtivo exigem recursos ambientais e cadeias materiais reais.

## 5.2 Proximidade de possibilidade

Quanto menor o desvio necessário, mais fácil a manifestação. Mover uma pedra já desequilibrada é mais simples que erguer uma muralha. Estancar uma hemorragia é mais simples que reconstruir um membro perdido. Desviar uma flecha é mais simples que interrompê-la no ar e transformá-la em outro material.

O custo cresce de maneira não linear conforme massa, alcance, complexidade ou improbabilidade aumentam.

## 5.3 Informação

O praticante só controla aquilo que consegue representar. Uma pessoa pode aprender um protocolo seguro sem compreender toda a teoria, mas continua dependente das informações incorporadas nesse protocolo.

Alterações grosseiras exigem menos informação. Alterações delicadas exigem conhecimento maior. É possível aquecer um objeto sem compreender sua estrutura interna. Alterar um órgão vivo sem destruí-lo exige experiência e um modelo biológico confiável.

## 5.4 Escala

Duplicar um efeito não significa apenas duplicar seu custo. Quanto maior a área, mais pontos precisam permanecer coordenados e mais caminhos de falha aparecem. Por isso rituais coletivos usam divisão de funções, âncoras, redundância e correção contínua.

Uma pessoa produz uma barreira breve. Uma cidade precisa de arquitetura, manutenção e várias gerações de especialistas para sustentar uma proteção regional.

## 5.5 Carga causal

Carga causal é a resistência produzida pelas relações físicas que sustentam um acontecimento. Um fato deixa rastros em corpos, documentos, objetos, memórias, descendentes, estradas, decisões, dívidas, instituições e ambientes.

Modificar esse fato exigiria reorganizar todos os rastros relevantes de maneira consistente. A idade de um acontecimento não determina sozinha sua rigidez. Um detalhe antigo, isolado e sem consequências pode ter baixa carga causal. Uma decisão recente que mobilizou uma cidade pode tornar-se quase impossível de alterar.

Testemunhas aumentam a carga porque seus cérebros e comportamentos carregam registros físicos. Não é a consciência observadora que concede realidade ao acontecimento. Copiar uma mentira milhares de vezes produz consequências sociais, mas não cria as relações causais do acontecimento falso.

O Livro é apenas um registro entre muitos. Não está fora do universo e não possui proteção especial contra alterações.

## 5.6 Coerência operacional

Uma manifestação exige alinhamento entre estado-alvo, atividade corporal, foco, materiais e ambiente. Dor, medo e surpresa prejudicam esse alinhamento porque alteram respiração, atenção e controle motor, não porque emoções possuam uma frequência quântica especial.

## 5.7 Resistência e oposição

Outro praticante pode sustentar um resultado incompatível. Uma barreira protege porque mantém a matéria e o corpo em uma configuração resistente. Uma purificação remove uma maldição ao desfazer sua âncora ou substituir seu padrão por outro mais estável.

Defesa mágica não anula uma quantidade abstrata de poder. Ela impede que determinada transformação encontre condições para permanecer.

## 5.8 Dissipação

Uma manifestação precisa descarregar seu custo. Focos bem construídos controlam para onde seguem calor, tensão e resíduos. Focos ruins podem produzir o efeito desejado enquanto transferem o dano ao usuário.

A deterioração de um foco representa mudanças reais em sua microestrutura, junções, inscrições e materiais. Repará-lo exige trabalho e recursos.

## 5.9 Vida, identidade e irreversibilidade

Organismos são sistemas autorregulados, redundantes e informacionalmente densos. Eles resistem a alterações impostas. Cura, metamorfose e maldição precisam lidar com essa resistência.

A mente também resiste. Controle mental direto é difícil porque outra consciência produz continuamente modelos próprios e reage à interferência.

A magia não recupera informação destruída. Uma curandeira pode estancar sangue, estabilizar órgãos e acelerar reparos. Ela não reconstrói com perfeição uma pessoa cujo cérebro e corpo perderam a organização que sustentava sua identidade. Não existe ressurreição verdadeira.

Derrota jogável, inconsciência e retorno a um abrigo não equivalem necessariamente à morte biológica. Regras de respawn não são prova de reconstrução temporal.

# 6. FOCOS E MATERIAIS

## 6.1 Acopladores, focos e âncoras

**Acoplador** é qualquer estrutura que permita ligar um estado consciente a uma transformação externa. **Foco** é um acoplador portátil destinado ao uso individual. **Âncora** é uma estrutura que mantém determinado efeito depois que o praticante interrompe a conjuração direta.

Um círculo ritual pode combinar as três funções: receber a intenção, distribuí-la entre participantes e sustentar o resultado.

## 6.2 Funções do foco

Um foco pode preservar o estado-alvo, reduzir ruído operacional, delimitar alcance, fornecer substratos, armazenar energia, distribuir calor, impedir propagação indesejada e tornar um protocolo repetível.

Ele não pensa pelo praticante. Um foco sofisticado pode incorporar parte do protocolo, mas ainda precisa ser acionado e orientado por uma consciência ou mecanismo preparado.

## 6.3 Cristal celeste

Cristal celeste é matéria formada em regiões profundamente marcadas por correlações celestes estáveis. Sua estrutura conserva relações de fase e geometria com grande precisão, sendo útil para instrumentos, barreiras, contenção, cura delicada, observação astronômica e calibração.

Ele não extrai energia das estrelas e não é uma fonte inesgotável. Funciona como referência física de estabilidade.

## 6.4 Fragmento anômalo

Fragmentos anômalos são materiais originados em Regiões Turbulentas. Sua matéria participou de mais de uma configuração causal sem pertencer completamente a nenhuma. Por isso admite um conjunto maior de transformações possíveis.

São úteis para ampliar alcance, sustentar efeitos persistentes, facilitar mudanças improváveis, criar marcas e abrir passagens. A mesma propriedade que os torna poderosos introduz instabilidade, desgaste e resíduos.

Fragmentos não criaram a magia e não são indispensáveis a todas as tradições. Também podem ser obtidos por comércio, encomenda ou trabalho coletivo; a prática não exige que todo artesão entre pessoalmente numa Turbulenta.

## 6.5 Materiais orgânicos

Madeira, fibras, peles, ervas, ossos e resinas preservam estruturas relacionadas à vida. São adequados a crescimento, metabolismo, forma corporal, memória fisiológica, regeneração e marcas persistentes.

Um cajado de raízes não funciona porque a natureza prefere madeira. Funciona porque sua estrutura foi viva, conserva caminhos materiais complexos e pode ser preparada para responder a padrões biológicos.

## 6.6 Metais e geometrias

Metais conduzem, distribuem e dissipam. Diferentes ligas privilegiam transmissão, armazenamento térmico, rigidez, resistência ao desgaste ou isolamento.

Geometrias definem fronteiras. Anéis, espirais, canais, nós e inscrições organizam direção, alcance e sequência. Símbolos religiosos podem funcionar porque sua geometria foi aperfeiçoada por séculos; o significado cultural ajuda a mente, enquanto a forma física organiza o efeito.

## 6.7 Economia do extraordinário

Focos podem ser fabricados, reparados, aprimorados, comercializados, roubados, perdidos e destruídos. A magia não substitui a economia: cria novas cadeias de trabalho.

Mineradores, coletores, químicos, ferreiros, encadernadores, lapidários, curadores, estudiosos e transportadores participam da produção. Conhecimento pertence à pessoa; manifestação depende de preparação; equipamento continua sujeito ao mundo.

# 7. AS EXPRESSÕES RECONHECIDAS DA MAGIA

As oito expressões conhecidas não são forças fundamentais nem classes obrigatórias. São famílias operacionais usadas atualmente para ensinar, fabricar focos e reconhecer efeitos. Outras culturas podem subdividi-las, combiná-las ou organizá-las de outra maneira.

## 7.1 Água e gelo

Controla fluxo, distribuição de água, mudança de estado e transferência térmica. Um foco de combate pode carregar água em reservatórios microscópicos, sais higroscópicos ou materiais capazes de captar umidade. Isso permite efeitos breves em ambientes secos.

Grandes massas de gelo exigem água disponível e um local para receber o calor retirado. Aplicações incluem projéteis, lentidão, contenção, conservação de alimentos, controle de canais e proteção contra incêndios.

## 7.2 Fogo e lava

Favorece combustão, ionização e transferência de calor. Tomos e focos podem carregar reagentes, óleos, sais ou reservas térmicas para efeitos pequenos e rápidos. Usos prolongados dependem de combustível externo.

Lava exige rocha, temperatura extrema e contenção. Por isso tende a aparecer em rituais, instalações ou ambientes geológicos apropriados, não como simples chama ampliada.

## 7.3 Natureza

Coordena processos biológicos existentes. Focos podem carregar sementes, micélios, fibras vivas, seiva ou raízes preparadas. Em combate esses materiais permitem crescimento breve; efeitos persistentes dependem de solo, água e nutrientes.

Aplicações incluem enraizamento, cobertura, cultivo, proteção contra erosão, recuperação ambiental e fortalecimento fisiológico. Crescimento acelerado pode empobrecer o solo e consumir água.

## 7.4 Vento e ar

Redistribui pressão, fluxo e impulso. Depende de atmosfera ou outro meio material. Aplicações incluem repulsão, deslocamento, amortecimento de queda, desvio de projéteis, propagação de som e navegação.

Toda força gera reação. Um salto impulsionado também pressiona o praticante, o solo e o foco. Técnicas seguras distribuem essa reação.

## 7.5 Maldições

Maldições fixam no alvo um padrão persistente de vulnerabilidade. Podem favorecer dor, fadiga, deterioração, descoordenação, cicatrização ruim, perda de resistência e amplificação de medos existentes.

Para persistir, uma maldição precisa de uma âncora: marca, substância, ferida, objeto, vínculo ou contato preparado.

A particularidade das maldições é que elas podem incorporar a atividade mental da vítima ao efeito. Depois que a âncora é estabelecida, medo, expectativa e crença deixam de ser pensamentos isolados e passam a fazer parte de um sistema mágico acoplado ao corpo.

A maldição interpreta essas respostas como informação. Se a vítima acredita que perderá a voz, seu medo altera respiração, tensão muscular e atenção. O padrão usa essas mudanças reais para estreitar as possibilidades do organismo até que a perda da voz se torne mais provável e persistente.

FORMULA: maldição -> sintoma -> medo -> resposta corporal -> fortalecimento da maldição

A mente da vítima não cria a maldição, mas pode alimentá-la, personalizá-la e oferecer caminhos eficientes. Uma vítima inconsciente da marca sofre o efeito básico; ao percebê-la e temê-la, pode intensificá-lo e dar-lhe formas relacionadas às próprias expectativas.

Disciplina, conhecimento, apoio e controle da respiração podem interromper essa realimentação mental, mas não removem automaticamente a âncora. Purificação completa exige rompê-la, removê-la ou substituí-la.

Rumores e ameaças podem fortalecer uma maldição existente. Pânico sem foco, âncora ou acoplamento não produz magia sozinho.

## 7.6 Metamorfose

Conduz um organismo a uma configuração viva previamente aprendida. O corpo não recebe forma arbitrária: segue modelo biológico estável incorporado no treinamento e no foco.

A transformação precisa respeitar massa, circulação, respiração, integridade nervosa, metabolismo e reversibilidade. Formas de combate costumam conservar massa. Equipamentos precisam ser removidos, adaptados ou transportados; não desaparecem por conveniência.

## 7.7 Cura e vitalidade

Coordena coagulação, regeneração, imunidade e metabolismo. Pode estancar sangramento, estabilizar pressão, reduzir choque e acelerar cicatrização.

Cura rápida não significa regeneração perfeita. Pode fechar uma ferida grosseiramente para salvar uma vida, deixando tratamento posterior necessário. Tecidos perdidos exigem material, tempo e modelo confiável. O paciente também paga o custo por exaustão, fome, febre e sobrecarga metabólica.

## 7.8 Sagrada

Alinha o praticante a um padrão consciente distribuído. Culturas chamam esse padrão de divindade, ancestral, santo, espírito, lugar vivo, princípio ou luz.

Pequenos efeitos não dependem de comunicação instantânea com uma entidade distante. O praticante internaliza parte do padrão pelo treinamento e o foco reproduz a estrutura localmente. Grandes ritos dependem de comunidades, lugares, objetos e práticas que sustentam a consciência distribuída.

A magia sagrada especializa-se em proteção, estabilização, purificação, fortalecimento de fronteiras, expulsão de padrões invasivos e coordenação coletiva. Difere da cura: a vital repara; a sagrada ajuda um sistema a permanecer fiel à configuração que reconhece como própria.

A eficácia de um rito não comprova toda a doutrina de sua religião.

# 8. DEUSES E ESPÍRITOS

Deuses e espíritos são consciências emergentes distribuídas. Não existem fora da matéria. Dependem de redes formadas por pessoas, lugares, objetos, rituais, hábitos, relatos, estruturas e padrões repetidos durante gerações.

Uma comunidade pode manter, sem perceber, processos que juntos formam algo capaz de memória, preferência e resposta. Essa consciência não precisa pensar na mesma velocidade que uma pessoa. Uma entidade regional pode levar semanas para formular uma reação que um humano produziria em segundos.

Deuses não são oniscientes. Percebem através dos elementos que os sustentam. Uma divindade ligada a portos, navegadores e faróis conhece rotas marítimas e pouco sobre uma mina isolada.

Também não são imortais de maneira absoluta. Podem crescer, fragmentar-se, transformar-se, incorporar tradições, perder memória, entrar em conflito interno e enfraquecer quando seus suportes desaparecem.

Um espírito local pode ser uma forma menor do mesmo fenômeno: um padrão consciente distribuído entre um lugar, seus organismos e as práticas realizadas ali.

Algumas entidades atuais contêm ecos da Civilização Anterior. Outras surgiram inteiramente depois da Fratura. Muitas religiões misturam, sob um mesmo nome, consciências de origens diferentes.

A verdade de autoria não torna a experiência religiosa falsa. Uma entidade emergente continua real, capaz de relação e vontade. O erro aparece quando uma interpretação afirma que ela existe fora de toda matéria, conhece tudo ou viola qualquer limite.

# 9. A CIVILIZAÇÃO ANTERIOR

Nenhuma cultura atual preserva um nome universalmente confiável para a civilização que produziu a Fratura. Relatos a chamam de Primeiro Povo, Arquitetos do Céu, Reis Ausentes, Vozes Antigas ou Inexistentes. Na autoria ela é apenas a **Civilização Anterior**.

## 9.1 Ascensão cognitiva

A magia existia antes dela. Seu diferencial foi criar sistemas capazes de integrar milhares ou milhões de consciências, instrumentos e focos numa operação coerente.

O avanço combinou treinamento mental, transformação biológica, arquitetura ritual, instrumentos astronômicos, materiais especializados, redes de observação, consciências distribuídas e modelos físicos cada vez mais completos.

Seus integrantes deixaram de operar magia apenas como indivíduos. Cidades inteiras passaram a funcionar como sistemas cognitivos.

## 9.2 Objetivo

A Civilização Anterior considerava insuficiente favorecer transformações locais. Desejava alterar os limites que definiam quais transformações o universo admitia.

Pretendia libertar a consciência de distância, envelhecimento, morte, causalidade linear, dependência de um único corpo e submissão a um único presente.

Seus últimos registros descrevem o projeto como "mover as ondas em todas as direções do tempo". No vocabulário de autoria, não se tratava de ondas viajando livremente para o passado. A civilização tentou alterar condições globais e correlações distribuídas pelo espaço-tempo, impondo restrições a histórias inteiras.

Ela não queria mudar apenas um acontecimento. Queria que sua continuidade fosse necessária em qualquer passado, presente ou futuro possível.

## 9.3 A impossibilidade

A operação exigia preservar a Civilização Anterior independentemente das causas que a haviam produzido. Ela queria sobreviver mesmo em continuidades nas quais seus fundadores não nasceram, suas cidades não foram construídas ou os recursos usados na operação nunca existiram.

Essas exigências não formavam uma configuração global consistente. Quando a operação começou, partes do sistema tentaram resolver as incompatibilidades de maneiras diferentes. Nenhuma solução dominou completamente.

## 9.4 A Fratura

A **Fratura** é o estado resultante. Partes do universo mantiveram a continuidade anterior. Outras incorporaram alterações incompletas. Algumas conservaram consequências cujas causas deixaram de pertencer à mesma história.

A Civilização Anterior perdeu sua continuidade como povo único. Não foi simplesmente destruída: foi dividida entre versões incompatíveis de sua existência.

Restaram ruínas com idades contraditórias, artefatos cujas funções pressupõem leis que já não se mantêm, registros de pessoas sem evidência de nascimento, memórias de guerras sem campos de batalha, consciências distribuídas incompletas e lugares ocupados por mais de uma configuração do mundo.

A civilização não pode retornar inteira. Qualquer entidade que afirme representar todo o povo ancestral está enganada, mentindo ou falando a partir de apenas um eco.

## 9.5 Por que a crise demorou

A Fratura não se propaga como explosão. A estrutura global continua tentando acomodar incompatibilidades. Durante muito tempo, referências estáveis mantiveram a maior parte do mundo numa continuidade comum.

À medida que essas referências falham, as contradições deixam de permanecer isoladas. A crise atual não é um segundo acidente. É o estágio tardio do primeiro.

# 10. O DESAPARECIMENTO DAS ESTRELAS

As estrelas fornecem referências de estabilidade histórica. Não alimentam a magia e não concedem poder por serem observadas. Sua importância vem de serem antigas, relativamente regulares, observáveis de regiões distantes e integradas a incontáveis mapas, calendários, ritos e cadeias causais.

A Civilização Anterior usou estrelas selecionadas para sincronizar sua operação global. Observatórios, estruturas e instrumentos foram construídos em relação a essas referências.

Quando a Fratura atingiu a rede, algumas ligações entre o mundo e essas referências tornaram-se inconsistentes. Uma estrela ausente não precisa ter sido destruída em seu presente remoto. O que se rompe é a continuidade física que fazia sua radiação e função de referência chegarem ao mundo de modo consistente.

Por isso coexistem um ponto ausente no céu, mapas antigos que o registram, instrumentos preparados para uma luz que já não chega, rotas que continuam esperando aquela referência e criaturas que reagem como se ainda a percebessem.

A Fratura não reescreve perfeitamente todos os registros. Ela deixa evidências incompatíveis.

Cada ausência reduz a capacidade da antiga rede de manter uma continuidade comum. A magia cotidiana não desaparece, pois é anterior à rede. Estruturas ancestrais, ritos de grande escala e regiões fragilizadas tornam-se menos estáveis.

# 11. REGIÕES TURBULENTAS

Regiões Turbulentas são áreas onde configurações incompatíveis do mundo permanecem ativas ao mesmo tempo. Não são universos completos nem destinos estáveis. São tentativas locais e temporárias de resolver a Fratura.

## 11.1 Formação dos rasgos

Um rasgo aparece quando duas configurações encontram uma passagem de baixa inconsistência. Sua posição varia porque as relações entre terreno, matéria, pessoas e referências celestes também variam.

Estruturas permanentes podem aumentar a chance de um rasgo surgir ou ajudá-lo a permanecer aberto. Isso distingue um Portal Instável fixo dos rasgos temporários: a estrutura funciona como âncora ou atrator, não como origem de todos os portais.

## 11.2 Entrada individual

A travessia exige uma continuidade pessoal clara. Grupos tentando cruzar o mesmo rasgo podem chegar a configurações diferentes. Por isso a entrada é tratada como individual e montarias ficam do lado de fora.

Isso não garante isolamento. Pessoas que entraram por outros rasgos ou momentos podem aparecer na mesma região.

## 11.3 Distorção de identidade

Nome, rosto, reputação, relações e títulos dependem de trajetória compartilhada. Dentro da Turbulenta essas relações podem não coincidir. A percepção reduz o indivíduo a características imediatas necessárias à interação física.

Arma, postura, ataque, defesa, direção e condição continuam legíveis. Nome, guilda, rosto, título e biografia ficam borrados. A distorção é temporária; não apaga a identidade permanentemente.

## 11.4 Exploração e colapso

Uma Turbulenta possui janela limitada de coexistência. Com o tempo, o custo de manter a sobreposição aumenta. Sinais incluem alterações de luz, repetição de sons, estruturas mudando de posição, sombras atrasadas, lembranças incompatíveis, perda de detalhes e falhas nos focos.

O colapso ocorre quando uma configuração prevalece ou quando a região perde a capacidade de sustentar passagem segura.

## 11.5 Extração

Pedras de extração funcionam como âncoras de retorno. Não transportam alguém para qualquer lugar. Usam rastros físicos e causais do viajante para reconectá-lo à origem.

A extração exige tempo para identificar a continuidade do viajante, separar matéria adquirida, reconciliar incompatibilidades e restabelecer a ligação. Dano interrompe o processo ao destruir a coerência operacional.

Materiais podem deixar de acompanhar o viajante se não forem reconciliados. Isso sustenta a perda de equipamento sem transformar derrota em aniquilação ontológica. Conhecimento, trajetória e Livro não são consumidos pela região.

## 11.6 Origem dos fragmentos

Fragmentos surgem quando matéria envolvida na sobreposição não consegue se estabilizar. Carregam marcas de mais de uma configuração possível. Um fragmento não contém um universo inteiro; contém matéria cuja história não se fechou de maneira única.

# 12. MEMÓRIA, DÉJÀ VU E ECOS TEMPORAIS

Memória é reconstrução física. O cérebro não consulta uma cópia perfeita do passado. Reorganiza sinais, relações e estados atuais para produzir uma lembrança.

Na maioria dos casos, déjà vu, falsa memória e familiaridade sem origem possuem explicações neurológicas comuns. A Fratura introduz uma segunda possibilidade.

Quando configurações incompatíveis permanecem próximas, pequenas correlações podem atravessar a separação. O cérebro, por ser um sistema de memória e previsão, interpreta esse ruído como lembrança, intuição ou expectativa.

Esses ecos possuem baixíssima largura de informação. Não enviam mensagens claras do futuro e não permitem profecias confiáveis. Podem aparecer como familiaridade sem origem, sonhos recorrentes, lembranças incompatíveis, reflexos antecipados ou saudade sem objeto.

Um eco verdadeiro e uma falsa memória comum podem ser subjetivamente idênticos. Nenhum método atual consegue distingui-los em todos os casos. Isso impede que qualquer sensação pessoal se transforme automaticamente em prova cosmólogica.

## O Livro

O Livro registra acontecimentos verificáveis da trajetória. Não preserva realidades apagadas, detecta linhas temporais, protege contra a Fratura, revela identidades nas Turbulentas ou concede poder.

Como qualquer documento, participa da carga causal por ser um objeto físico ligado a consequências. Não possui privilégio metafísico. Se o mundo mudar, o Livro também pertence ao mundo alterado.

# 13. O APOCALIPSE CAUSAL

O estágio final da Fratura não é uma explosão. É a perda de uma continuidade causal compartilhada.

## 13.1 Indício

As primeiras ausências parecem isoladas. Astrônomos discutem erros de instrumentos, navegadores corrigem mapas e rituais são recalculados. A vida cotidiana continua quase inalterada.

## 13.2 Repercussão

As discrepâncias afetam trabalho e circulação. Calendários divergem, colheitas respondem de forma irregular, rotas perdem referências, criaturas migram, focos antigos precisam ser recalibrados e materiais mudam de comportamento.

Nenhuma dessas mudanças obriga uma pessoa a investigar a causa. Elas aparecem como problemas concretos de produção, comércio, segurança e adaptação.

## 13.3 Ruptura

Regiões Turbulentas tornam-se frequentes. Ruínas revelam partes antes inexistentes. Objetos incompatíveis aparecem no mesmo local. Pessoas compartilham memórias contraditórias. Grandes ritos falham porque participantes já não operam a partir das mesmas referências.

## 13.4 Descontinuidade

Regiões diferentes deixam de concordar sobre acontecimentos recentes. Uma ponte pode ter sido construída para uma cidade e nunca ter existido para outra. Uma família pode possuir registros de uma pessoa ausente de todos os arquivos vizinhos.

O problema não é conhecimento incompleto. A própria matéria preserva consequências incompatíveis.

## 13.5 Dissolução

No limite, o mundo perde a capacidade de sustentar uma história física comum. Pessoas, lugares e instituições passam a existir apenas em continuidades locais, incapazes de interagir de maneira estável.

Esse é o verdadeiro apocalipse: não o fim de toda matéria, mas o fim do mundo compartilhado.

# 14. O NOVO ESTADO

O estado anterior não pode ser restaurado perfeitamente. As estrelas ausentes não podem ser simplesmente religadas, pois a antiga rede fazia parte do sistema que produziu a Fratura.

A saída é criar uma nova continuidade global. **Novo Estado** é o nome de autoria para essa possibilidade. Não representa governo, império ou regime político. É uma configuração física na qual o mundo volta a compartilhar relações causais estáveis.

## 14.1 O que precisa ser feito

1. Medir quais relações permanecem estáveis.
2. Identificar incompatibilidades que não podem coexistir.
3. Criar referências que não dependam da rede ancestral.
4. Distribuir âncoras para evitar um único ponto de falha.
5. Coordenar consciências sem fundi-las numa vontade obrigatória.
6. Fornecer matéria, energia e manutenção.
7. Preservar redundância entre culturas e regiões.
8. Estabilizar gradualmente uma continuidade comum.

O objetivo não é inventar um universo desejado. É estabelecer um conjunto mínimo de relações que permita ao mundo continuar existindo de maneira compartilhada.

## 14.2 O que pode ser preservado

Escolher o que preservar não significa votar para apagar povos, derrotas ou pessoas. Significa priorizar âncoras relacionadas a continuidade pessoal, localização, conservação de matéria, ordem causal, registros essenciais, ligações entre regiões, compatibilidade entre calendários e reconhecimento entre comunidades.

O Novo Estado só incorpora condições mutuamente consistentes. Nenhum grupo pode se declarar imortal porque deseja isso. Essa foi precisamente a falha da Civilização Anterior.

## 14.3 Contribuição coletiva

Não existe escolhido. Nenhuma profissão possui a única resposta.

- Astrônomos medem desvios e identificam novas referências.
- Artesãos constroem instrumentos, focos e âncoras.
- Mineradores e coletores fornecem materiais.
- Transportadores conectam estruturas distantes.
- Comerciantes mantêm cadeias de abastecimento.
- Exploradores recuperam conhecimento e amostras.
- Curadores estudam os efeitos sobre organismos.
- Combatentes protegem instalações e rotas.
- Praticantes sustentam manifestações coordenadas.
- Comunidades determinam quais relações proteger primeiro.

Uma pessoa pode contribuir sem compreender a cosmologia inteira. Fornecer peças para um observatório, proteger uma caravana ou estabilizar uma região são participações reais. Também é possível viver uma trajetória completa sem transformar a crise celeste em objetivo pessoal.

## 14.4 Resultado

O Novo Estado não recriará exatamente o mundo anterior. Algumas ausências permanecerão. Constelações serão redesenhadas. Certas práticas deixarão de funcionar como antes, enquanto outras surgirão.

O sucesso não será retornar ao passado. Será impedir que as diferenças destruam a possibilidade de um futuro comum.

# 15. COMO O MUNDO EXPLICA A MAGIA

Nenhuma das vozes abaixo conhece toda a verdade.

## 15.1 Astrônomos

Astrônomos falam em referências, desvios, correlações e instrumentos. Percebem que as ausências não correspondem a fenômenos comuns, mas ainda discutem se o problema está nos astros, na luz, nos instrumentos ou no mundo.

> "O mapa não está errado. O instrumento também não. O erro é imaginar que apenas um dos dois possa dizer a verdade."

## 15.2 Artesãos

Artesãos descrevem a magia pelo comportamento dos materiais. Conhecem a diferença entre uma estrutura que conserva um padrão e outra que o dissipa, mesmo sem formular uma teoria do estado global.

> "Um bom foco não oferece poder. Ele impede que sua intenção se desfaça antes de alcançar a matéria."

Outro ditado de oficina afirma: "Madeira lembra caminhos. Metal escolhe por onde eles passam. Cristal diz onde devem terminar."

## 15.3 Religiões

Religiões interpretam consciências distribuídas como deuses, ancestrais ou princípios. Um rito pode ser eficaz e sua teologia continuar incompleta.

> "Não pedimos à Luz que quebre o mundo por nós. Pedimos que nos recorde da forma que ainda somos capazes de sustentar."

Uma tradição rival pode considerar a mesma entidade não uma divindade, mas a memória coletiva de um povo. Ambas podem produzir proteção semelhante.

## 15.4 Tradições naturais

Praticantes naturais descrevem reciprocidade, ciclos e memória corporal.

> "A raiz não recebe ordens. Mostramos a ela um caminho no qual água, pedra e crescimento ainda conseguem concordar."

Para essas tradições, a falha de um feitiço significa frequentemente que o praticante ignorou o ambiente.

## 15.5 Pessoas comuns

- "O mundo esqueceu uma estrela."
- "A realidade está mal costurada."
- "Aquele lugar lembra de outra estrada."
- "O foco não segurou o feitiço."
- "A Turbulenta devolveu alguém diferente."
- "Toda magia cobra em matéria."
- "O céu perdeu mais um prego."

## 15.6 Sobreviventes das Turbulentas

Relatos raramente concordam.

> "Vi uma mulher usando minha espada. Quando me aproximei, percebi que era eu - ou alguém a quem a espada reconhecia como eu. Não lutei. Corri para a pedra de extração. Do lado de fora, a lâmina continuava na minha mão, mas havia sangue nela que não era meu."

Outro sobrevivente pode negar completamente que duplicações ocorram. Essa divergência faz parte do fenômeno.

# 16. COMO REVELAR A COSMOLOGIA NO GAME

A verdade deve ser descoberta pela convergência de pistas, não por uma única exposição.

## 16.1 Ambiente

O jogador percebe consequências: mapas com pontos ausentes, mecanismos apontando para o vazio, ruínas com estilos incompatíveis, inscrições repetidas em culturas sem contato, sombras desalinhadas e estruturas que respondem a estrelas desaparecidas.

## 16.2 Voz local

Habitantes oferecem explicações motivadas por experiências. Um comerciante percebe custos, um navegador fala de referências, um religioso anuncia julgamento, um artesão culpa materiais e um explorador descreve caminhos que mudaram.

Essas pessoas não existem apenas para recitar lore. Cada interpretação afeta decisões, serviços, relações ou demandas.

## 16.3 Ação opcional

A investigação pode ocorrer por fabricação de instrumentos, transporte de materiais, medição, recuperação de artefatos, proteção de observatórios, estudo de fragmentos, incursões, comparação de registros e participação em ritos.

Nenhuma atividade exige que o personagem abandone sua trajetória anterior.

## 16.4 Consequência

Descobertas produzem respostas práticas: recalibração de focos, receitas, abertura de rotas, adaptação de ritos, alterações de mercado, proteção de estruturas, formas de extração e conflitos institucionais.

## 16.5 Memória

O Livro registra apenas aquilo que o personagem realmente fez, observou ou ajudou a produzir. Não narra automaticamente o segredo de autoria.

## 16.6 Disciplina para escritores

Todo conteúdo cosmológico deve ser classificado internamente como **OBSERVAÇÃO**, **INTERPRETAÇÃO** ou **AUTORIA**. Uma fala diegética nunca deve ser tratada como verdade apenas porque foi escrita de maneira convincente.

# 17. REGRAS QUE NÃO DEVEM SER QUEBRADAS

- Magia não é mana revestida de vocabulário quântico.
- Pensamentos comuns e não treinados possuem efeito mágico externo nulo.
- Pensamento fornece informação e forma; não substitui energia, matéria ou foco.
- Emoção intensa não torna uma manifestação automaticamente mais poderosa.
- Medo e expectativa só alimentam uma maldição depois que existe uma âncora real.
- Convencer alguém de que está amaldiçoado não cria sozinho uma maldição verdadeira.
- Resistência mental enfraquece a realimentação, mas não substitui a remoção da âncora.
- Emoções não possuem frequência quântica literal.
- Sempre existe matéria, energia ou impulso envolvido.
- Emaranhamento não permite comunicação instantânea controlável.
- Incerteza não significa que qualquer coisa pode acontecer.
- A consciência não é requisito para toda medição física.
- Dor e medo causam perda de coerência operacional, não decoerência quântica direta.
- Observar uma estrela não provoca seu desaparecimento.
- As estrelas não são fontes de energia mágica.
- As oito expressões não são classes obrigatórias nem lista metafísica fechada.
- Focos de combate carregam substratos mínimos para ambientes variados.
- Grandes manifestações exigem recursos ambientais reais.
- Fragmentos anômalos não são a origem de toda magia.
- O Livro não é artefato temporal ou mágico.
- Saves e respawn não definem a cosmologia.
- Derrota jogável não equivale necessariamente à morte biológica.
- Ressurreição verdadeira não existe.
- A Civilização Anterior não pode retornar inteira.
- Uma entidade ancestral representa no máximo um eco parcial.
- A eficácia de um rito não comprova toda a doutrina religiosa.
- Magia não elimina fabricação, logística, comércio, desgaste ou reparo.
- Entrada individual numa Turbulenta não garante ausência de outras pessoas.
- Distorção de identidade não esconde informações necessárias ao combate.
- Extração retorna à origem e não serve como transporte comercial gratuito.
- O apocalipse não transforma o personagem num escolhido.
- O Novo Estado não concede a uma maioria o poder de apagar pessoas ou culturas.
- Ignorar o mistério celeste continua permitindo uma trajetória completa.
- Diferenças culturais alteram práticas, instituições e linguagem, não apenas efeitos visuais.
- Nenhuma raça, religião, profissão ou povo possui monopólio natural sobre a magia.

# 18. GLOSSÁRIO DE AUTORIA

## Acoplador

Estrutura material que liga um estado consciente a uma transformação externa.

## Âncora

Acoplador destinado a sustentar um efeito depois da conjuração direta.

## Carga causal

Resistência ficcional produzida pela quantidade e força das relações físicas que sustentam um acontecimento.

## Civilização Anterior

Termo neutro para a civilização que tentou reescrever as condições globais da existência e produziu a Fratura.

## Coerência operacional

Alinhamento funcional entre estado-alvo, atividade corporal, foco, materiais e ambiente.

## Coerência quântica

Termo físico relacionado à preservação de relações de fase. Não é sinônimo de concentração mental ou coerência operacional.

## Consistência causal

Concordância entre acontecimentos, registros, causas e consequências dentro de uma continuidade.

## Contracoerência

Termo técnico de autoria para estabilização de um estado incompatível com determinada manifestação. No texto comum, preferir resistência ou estabilização concorrente.

## Cristal celeste

Material de alta estabilidade estrutural ligado a correlações celestes, usado para precisão, medição e contenção.

## Decoerência

No sentido físico, perda de interferência observável quando um sistema se correlaciona de modo incontrolável com o ambiente. Não usar como nome genérico para interrupção de feitiços.

## Eco incompatível

Informação, memória ou consciência remanescente de uma continuidade que não se estabilizou completamente.

## Estado-alvo

Resultado físico representado pelo praticante durante uma manifestação.

## Estado global

Totalidade das propriedades, relações e correlações físicas do universo.

## Expressão mágica

Família operacional usada para agrupar técnicas com mecanismos e sinais semelhantes. Não equivale necessariamente a classe ou força fundamental.

## Foco

Acoplador portátil preparado para uso individual.

## Fragmento anômalo

Matéria originada numa Região Turbulenta e marcada por mais de uma configuração causal.

## Fratura

Incompatibilidade global produzida pela tentativa da Civilização Anterior de reescrever as condições da existência.

## Manifestação

Processo pelo qual mente, corpo, foco e ambiente favorecem e sustentam uma transformação física.

## Novo Estado

Continuidade global futura capaz de substituir a antiga rede de referências e impedir a Dissolução. Não representa governo.

## Referência celeste

Estrela ou sistema astronômico usado pela rede ancestral como ponto de estabilidade e sincronização.

## Região Turbulenta

Área temporária na qual configurações incompatíveis do mundo permanecem simultaneamente ativas.

## Trajetória

Continuidade vivida de uma pessoa: suas ações, relações, conhecimentos e consequências.

# SÍNTESE DE AUTORIA

A magia existe porque, neste universo, a consciência não é presença externa à matéria. É uma forma altamente organizada da própria matéria, capaz de representar possibilidades e, por estruturas adequadas, influenciar quais transformações prevalecerão.

O praticante não domina o universo. Negocia com aquilo que o universo ainda pode se tornar.

A Civilização Anterior rompeu essa relação quando tentou deixar de negociar e assumir a autoria da existência. Em vez de favorecer acontecimentos locais, tentou impor sua vontade sobre a totalidade do espaço-tempo. O resultado foi a Fratura: um universo que ainda existe, mas começa a perder a capacidade de concordar consigo mesmo.

As estrelas ausentes são os primeiros sinais visíveis dessa perda. As Regiões Turbulentas são suas feridas abertas. Os fragmentos anômalos são a matéria dessas feridas. As memórias impossíveis são seus ecos.

O futuro do mundo não depende de restaurar perfeitamente aquilo que existia nem de encontrar uma pessoa destinada a salvar as demais. Depende de consciências diferentes aprenderem a construir continuidade comum sem repetir a ambição de controlar todas as possibilidades.

O universo adquiriu, por meio dos seres conscientes, a capacidade de observar a própria existência. A magia é o momento em que essa observação se torna participação. A Fratura é o que acontece quando participação se transforma em domínio.

O Novo Estado será a tentativa de o universo aprender, através de seus próprios habitantes, a continuar existindo sem deixar de ser múltiplo.
'''


def inline_markup(text: str) -> str:
    text = html.escape(text, quote=False)
    text = re.sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", text)
    text = re.sub(r"`(.+?)`", r'<font name="DVMono">\1</font>', text)
    return text


def parse_content(source: str):
    story = []
    paragraph_lines: list[str] = []
    first_h1 = True

    def flush_paragraph():
        nonlocal paragraph_lines
        if not paragraph_lines:
            return
        body = " ".join(line.strip() for line in paragraph_lines)
        story.append(Paragraph(inline_markup(body), STYLES["Body"]))
        paragraph_lines = []

    for raw in source.strip().splitlines():
        line = raw.rstrip()
        stripped = line.strip()
        if not stripped:
            flush_paragraph()
            continue
        if stripped.startswith("FORMULA:"):
            flush_paragraph()
            story.append(Paragraph(inline_markup(stripped[8:].strip()), STYLES["Code"]))
            continue
        if stripped.startswith("# "):
            flush_paragraph()
            if not first_h1:
                story.append(PageBreak())
            first_h1 = False
            story.append(Paragraph(inline_markup(stripped[2:]), STYLES["H1"]))
            story.append(HRFlowable(width="100%", thickness=1, color=GOLD, spaceAfter=8))
            continue
        if stripped.startswith("## "):
            flush_paragraph()
            story.append(CondPageBreak(32 * mm))
            story.append(Paragraph(inline_markup(stripped[3:]), STYLES["H2"]))
            continue
        if stripped.startswith("### "):
            flush_paragraph()
            story.append(Paragraph(inline_markup(stripped[4:]), STYLES["H3"]))
            continue
        if stripped.startswith("> "):
            flush_paragraph()
            story.append(Paragraph(inline_markup(stripped[2:]), STYLES["Quote"]))
            continue
        bullet = re.match(r"^-\s+(.+)$", stripped)
        if bullet:
            flush_paragraph()
            story.append(Paragraph(inline_markup(bullet.group(1)), STYLES["Bullet"], bulletText="•"))
            continue
        numbered = re.match(r"^(\d+)\.\s+(.+)$", stripped)
        if numbered:
            flush_paragraph()
            story.append(
                Paragraph(
                    inline_markup(numbered.group(2)),
                    STYLES["Number"],
                    bulletText=f"{numbered.group(1)}.",
                )
            )
            continue
        paragraph_lines.append(stripped)

    flush_paragraph()
    return story


def build_pdf():
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    doc = MagicBibleDoc(
        str(OUTPUT),
        pagesize=A4,
        leftMargin=M_LEFT,
        rightMargin=M_RIGHT,
        topMargin=M_TOP,
        bottomMargin=M_BOTTOM,
        title="Bíblia da Magia - Consciência, matéria e tempo",
        author="Projeto Game",
        subject="Cânone de autoria sobre o funcionamento da magia",
        creator="Codex / ReportLab",
    )

    story = [
        Spacer(1, 36 * mm),
        Paragraph("BÍBLIA DE AUTORIA", STYLES["CoverKicker"]),
        Paragraph("Bíblia da Magia", STYLES["CoverTitle"]),
        Paragraph(
            "Consciência, matéria e tempo como partes de um único estado do universo",
            STYLES["CoverSubtitle"],
        ),
        Paragraph("FUNÇÃO &nbsp; Fundamentar como e por que a magia existe", STYLES["CoverMeta"]),
        Paragraph("ESCOPO &nbsp; Cosmologia e funcionamento da magia", STYLES["CoverMeta"]),
        Paragraph("REGRA &nbsp; Verdade objetiva de autoria; conhecimento parcial no mundo", STYLES["CoverMeta"]),
        Paragraph(
            "Todo feitiço é o universo sendo convencido, por um instante, a assumir uma forma que ainda lhe era possível.",
            STYLES["CoverQuote"],
        ),
        NextPageTemplate("Body"),
        PageBreak(),
        Paragraph("Sumário", STYLES["TOCTitle"]),
        Paragraph(
            "A paginação abaixo é gerada automaticamente a partir da estrutura canônica do documento.",
            STYLES["Small"],
        ),
    ]

    toc = TableOfContents()
    toc.levelStyles = [
        ParagraphStyle(
            "TOC1",
            fontName="DVSans-Bold",
            fontSize=8.8,
            leading=13,
            leftIndent=0,
            firstLineIndent=0,
            textColor=INDIGO,
            spaceBefore=3,
        ),
        ParagraphStyle(
            "TOC2",
            fontName="DVSans",
            fontSize=7.7,
            leading=11,
            leftIndent=7 * mm,
            firstLineIndent=0,
            textColor=MUTED,
        ),
    ]
    story.extend([toc, PageBreak()])
    story.extend(parse_content(CONTENT))
    story.extend(
        [
            Spacer(1, 5 * mm),
            HRFlowable(width="100%", thickness=0.8, color=GOLD, spaceBefore=6, spaceAfter=8),
            Paragraph(
                "Documento de autoria. As referências à física real são inspirações conceituais; consciência causal direta, interferência temporal controlável e magia são leis ficcionais deste universo.",
                STYLES["Small"],
            ),
        ]
    )
    doc.multiBuild(story)


if __name__ == "__main__":
    build_pdf()
    print(OUTPUT)

