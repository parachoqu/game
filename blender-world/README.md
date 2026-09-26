# Mundo procedural — base de produção no Blender

Esta pasta contém uma entrega independente da demo: um mundo procedural editável, os dados que o regeneram, exportações por bloco e imagens de validação. O arquivo principal é `MUNDO_MESTRE.blend`.

## Conteúdo principal

- `MUNDO_MESTRE.blend`: arquivo autocontido com cinco cenas.
- `config/world_config.json`: sementes, escalas, biomas, âncoras, rotas, reinos propostos, hidrologia e distribuição.
- `exports/manifest.json`: contrato de exportação com limites, blocos, mapas, rotas, instâncias, materiais e pontos de interesse.
- `exports/maps/`: mapas de altura EXR e PNG de 16 bits, água e biomas.
- `exports/region/chunks/`: 16 blocos do terreno regional, cada um em GLB próprio.
- `exports/region/`, `exports/turbulent/` e `exports/library/`: conjuntos funcionais, instâncias, rotas e inventário de ativos.
- `renders/validation/`: 12 imagens em 2.560 × 1.440 e folha de contato.
- `renders/final/`: três imagens em 3.840 × 2.160 e folha de contato.
- `reports/RELATORIO_FINAL.md`: inventário, resultados de validação e limites conhecidos.

## Cenas do arquivo mestre

1. `ATLAS_MUNDO`: dois continentes em uma área de 220 × 140 km; escala de 1 unidade Blender por quilômetro.
2. `REGIAO_COMERCIAL`: área detalhada de 1.024 × 1.024 m em escala métrica, dividida em 16 blocos com bordas compartilhadas.
3. `TURBULENTA_01`: área anômala atravessável de 256 × 256 m, entrada, duas saídas, ruínas e portal.
4. `BIBLIOTECA_LOCAL`: 18 famílias de ativos ambientais e arquitetônicos de detalhe médio.
5. `VALIDACAO`: referências humanas e de escala, materiais, iluminação e câmeras de revisão.

Cada cena possui camadas para fontes procedurais, base editável, acabamento manual e preparação de exportação. As fontes preservadas usam coleções versionadas como `GEN_REGION__seed_26092402__v001`. Uma regeneração cria uma nova versão e não escreve sobre `BASE_EDITAVEL` ou `ACABAMENTO_MANUAL`.

## Sementes fixas

- Atlas: `26092401`
- Região: `26092402`
- Turbulenta: `26092403`

## Regenerar e validar

Os comandos abaixo devem ser executados a partir de `/home/https/Área de trabalho/workspace/rpg`.

```bash
python3 blender-world/tools/worldgen_io.py
blender --background --factory-startup --python-exit-code 1 --python blender-world/tools/build_master.py
blender --background blender-world/MUNDO_MESTRE.blend --python-exit-code 1 --python blender-world/tools/export_world.py
blender --background blender-world/MUNDO_MESTRE.blend --python-exit-code 1 --python blender-world/tools/render_world.py
python3 -m unittest discover -s blender-world/tests -v
python3 blender-world/tools/validate_production.py
blender --background blender-world/MUNDO_MESTRE.blend --python-exit-code 1 --python blender-world/tools/validate_blend.py
blender --background --factory-startup --python-exit-code 1 --python blender-world/tools/validate_glbs.py
```

`regenerate_sources.py` testa a política de versionamento em memória e não salva o resultado:

```bash
blender --background blender-world/MUNDO_MESTRE.blend --python-exit-code 1 --python blender-world/tools/regenerate_sources.py
```

## Regras de edição

- Ajustes autorais devem ser feitos nas coleções `ACABAMENTO_MANUAL` de cada cena.
- A base consolidada pode ser esculpida em `BASE_EDITAVEL`.
- Fontes procedurais e sementes ficam em `FONTES_PROCEDURAIS` e no arquivo de configuração.
- Vegetação extensa permanece instanciada; `instances.json` registra as posições para reconstrução externa.
- O atlas é uma representação completa do mundo, mas não transforma os dois continentes inteiros em terreno jogável.
- Os nomes e fronteiras de Valdora, Serrabruma, Verdeferro, Marévia, Pedrassol e Altavela são propostas revisáveis.
- A demo não lê o manifesto nesta etapa e nenhum adaptador de integração foi criado.

## Limites conhecidos

- Edifícios, mina e caverna possuem exteriores e acessos; interiores completos não fazem parte desta entrega.
- O pacote Blender do Kali não expôs um dispositivo HIP confiável. As imagens finais foram geradas em Eevee.
- A biblioteca Draco não está presente no pacote do sistema. Os GLBs foram exportados sem compressão.
- Os GLBs de natureza contêm as malhas-fonte; as distribuições densas estão em arquivos JSON de instâncias.
- Uma reinicialização normal do sistema é recomendada, pois o gerenciador de pacotes adiou reinícios de serviços durante a sessão ativa.

