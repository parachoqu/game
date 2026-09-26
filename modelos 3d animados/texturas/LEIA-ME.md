# Biblioteca de texturas — terreno e natureza

Preparada em 2026-09-24T00:21:04.322894+00:00. Originais PNG 2K da Poly Haven; 48 mapas selecionados, sem malhas 3D.

## Licença e origem

Todos os assets são CC0-1.0: uso, adaptação e redistribuição, inclusive comercial. Créditos são preservados no manifesto como procedência, embora a licença não exija atribuição.

- Licença oficial: https://polyhaven.com/license
- Manifesto verificável: `manifesto.json` (URLs exatas, autores, dimensões, MD5 oficial e SHA-256 local por arquivo).

## Materiais e aplicação

| Categoria / material | Uso | Escala física por repetição | Fonte |
| --- | --- | --- | --- |
| terreno/sparse_grass | Campos com grama irregular e terra exposta | 2 m × 2 m | [Sparse Grass](https://polyhaven.com/a/sparse_grass) |
| terreno/rocky_trail_02 | Trilhas de terra compactada e cascalho | 2 m × 2 m | [Rocky Trail 02](https://polyhaven.com/a/rocky_trail_02) |
| terreno/forrest_ground_01 | Chão de bosque com folhas, gravetos e matéria orgânica | 2 m × 2 m | [Forest Ground 01](https://polyhaven.com/a/forrest_ground_01) |
| rochas/rock_3 | Rochas cinzentas, encostas e superfícies erodidas | 1.5 m × 1.5 m | [Rock 3](https://polyhaven.com/a/rock_3) |
| rochas/dark_rock | Variações rochosas dos Ermos e da Região Turbulenta | 2.4212 m × 2.4212 m | [Dark Rock](https://polyhaven.com/a/dark_rock) |
| terreno/river_small_rocks | Seixos e transições nas margens do rio | 2.9 m × 2.9 m | [River Small Rocks](https://polyhaven.com/a/river_small_rocks) |
| cascas/bark_brown_02 | Casca de árvores de copa ampla | 0.999999 m × 0.999999 m | [Bark Brown 02](https://polyhaven.com/a/bark_brown_02) |
| cascas/pine_bark | Casca dos pinheiros | 2 m × 2 m | [Pine Bark](https://polyhaven.com/a/pine_bark) |
| folhagem/island_tree_03 | Folhas largas para reconstruir copas | Atlas: ajustar cada recorte à escala botânica; dimensões do modelo no manifesto | [Island Tree 03](https://polyhaven.com/a/island_tree_03) |
| folhagem/pine_tree_01 | Ramos com agulhas para pinheiros | Atlas: ajustar cada recorte à escala botânica; dimensões do modelo no manifesto | [Pine Tree 01](https://polyhaven.com/a/pine_tree_01) |

## Uso dos mapas

- `diff`: cor/base color, interpretar em sRGB; não embutir iluminação adicional.
- `nor_gl`: relevo de iluminação no padrão OpenGL; interpretar como dados lineares, não como cor.
- `rough`: rugosidade, dados lineares; branco mais fosco, preto mais liso.
- `disp`: altura para refinamento controlado, dados lineares; preservar o chão usado pelo movimento e as colisões.
- `ao`: oclusão ambiente, dados lineares; aplicar moderadamente para não duplicar o escurecimento da iluminação.
- `alpha`: máscara de recorte das folhas/ramos, dados lineares; aplicar o mesmo recorte na superfície e nas sombras.

Superfícies têm os cinco mapas pedidos. Folhagem inclui apenas alpha, cor, normal OpenGL e rugosidade. Os mapas de modelos não incluem malhas, troncos, galhos sólidos ou outros subconjuntos.

## Preservação e integração

Esta pasta contém os arquivos originais com os nomes publicados. Gere cópias otimizadas em uma pasta própria de recursos da demo, sem sobrescrever estes originais. As imagens de folhagem são atlas: seus recortes precisam de proporções naturais e não devem aparecer como planos retangulares. As dimensões dos modelos de árvores no manifesto não correspondem ao tamanho de uma folha nem de um ramo individual.

## Verificação

- Arquivos verificados: 48 de 48.
- Total dos PNG verificados: 495,072,130 bytes.
- Cada arquivo aprovado confere com o tamanho e o MD5 fornecidos pela API oficial.
- Todos os PNG aprovados passaram pela verificação de integridade e pela decodificação completa com Pillow; dimensão máxima 2048 px.
- SHA-256 local, dimensões exatas e modo de cor constam no manifesto.
- Falhas: 0.
