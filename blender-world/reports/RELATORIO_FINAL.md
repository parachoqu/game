# Relatório final — mundo procedural no Blender

Data: 24 de setembro de 2026  
Blender: 5.0.1  
Arquivo mestre: `MUNDO_MESTRE.blend`

## Resultado entregue

Foi criada uma base de produção procedural e autocontida no Blender, separada da demo. O arquivo mestre representa dois continentes em escala de atlas, uma região comercial de 1.024 × 1.024 m em alta definição relativa, uma Região Turbulenta de 256 × 256 m, uma biblioteca local de ativos e uma cena própria de validação.

A direção combina terreno e escala plausíveis, materiais naturais e atmosfera séria com paleta controlada, silhuetas simplificadas e acentos fantásticos localizados. O nível de acabamento é deliberadamente médio: serve para composição, renders, escultura e substituição posterior por modelos finais.

## Arquitetura

| Cena | Conteúdo | Escala |
|---|---|---|
| `ATLAS_MUNDO` | Dois continentes, seis famílias ambientais, hidrografia, rotas, marcos, risco, política e anomalias | 220 × 140 km; 1 BU = 1 km |
| `REGIAO_COMERCIAL` | Vale habitado, entrepostos, rio, pontes, garganta, desvio seguro, mina, caverna, acampamento, observatório, Ermos, campos e canais | 1.024 × 1.024 m; 1 BU = 1 m |
| `TURBULENTA_01` | Terreno fraturado, duas rotas, entrada, duas saídas, ruínas, raízes, fragmentos e portal ciano | 256 × 256 m; 1 BU = 1 m |
| `BIBLIOTECA_LOCAL` | Vegetação, rochas, ponte, cais, paredes, arcos, telhados, torres, tendas, barracas, mina, observatório, acampamento e ruínas | Métrica |
| `VALIDACAO` | Referências humanas, materiais, iluminação e câmeras | Métrica |

As coleções `FONTES_PROCEDURAIS`, `BASE_EDITAVEL`, `ACABAMENTO_MANUAL` e `PREPARACAO_EXPORTACAO` mantêm separados o gerador, a base consolidada, o trabalho autoral e as cópias de saída. As gerações são versionadas por semente; uma nova versão não substitui acabamento manual.

## Parâmetros fixos

| Domínio | Semente | Amostras | Hash SHA-256 do mapa de altura |
|---|---:|---:|---|
| Atlas | 26092401 | 1.024 × 640 | `ebe8cfabb5aba286a1067e8571529f90adb7e6a17c9f92fb901a7582b0e2d56c` |
| Região | 26092402 | 1.025 × 1.025 | `6d4c803145c4df73a5b9663225ef68cb7745f83492f113c601cd9c1cff685f61` |
| Turbulenta | 26092403 | 513 × 513 | `6a70931fcb3535015450917b90e3e45311845b72257e858d2c84b7dd9ce549f4` |

Os seis territórios políticos são propostas revisáveis: Valdora, Serrabruma, Verdeferro, Marévia, Pedrassol e Altavela. O observatório regional e o observatório do segundo continente são estruturas diferentes.

## Inventário técnico

- 226 objetos no mestre.
- 34 materiais.
- 24 imagens PBR de 2K empacotadas no `.blend`.
- Cinco grupos de Geometry Nodes.
- 18 famílias de ativos ambientais e arquitetônicos.
- 16 blocos regionais de 256 × 256 m com bordas compartilhadas.
- 2.610 instâncias regionais verificadas; vegetação extensa permanece instanciada.
- 24 GLBs, além de mapas, máscaras, rotas, pontos de interesse, instâncias e inventários JSON.
- 12 imagens de validação em 2.560 × 1.440.
- Três imagens finais em 3.840 × 2.160.

O manifesto principal está em `exports/manifest.json`. A demo não consome esse arquivo nesta etapa.

## Validação

| Verificação | Resultado |
|---|---|
| Suíte Python | 23 de 23 testes aprovados |
| Estrutura do `.blend` | Aprovada: cinco cenas e camadas obrigatórias |
| Determinismo | Aprovado para alturas, água, umidade, inclinação e biomas |
| Bordas dos 16 blocos | Erro máximo de altura: 0,0 m |
| Hidrologia regional | Rio contínuo de oeste a leste; subida modelada máxima: 0,0 m |
| Lagos do atlas | Dois, ambos classificados como bacias fechadas |
| Grafo de rotas | Todos os entrepostos, travessias, mina, caverna, acampamento, observatório, Ermos e portal alcançáveis |
| Inclinação das rotas | Dentro dos limites declarados de 12°, 16° e 18° sustentados |
| Exclusões de distribuição | 2.610 instâncias; zero colisões detectadas |
| Mapas PNG de altura | 16 bits, usando a faixa completa |
| Regeneração v002 | Base editável e acabamento manual mantiveram hashes idênticos |
| Reabertura de GLBs | 24 de 24 aprovados |
| Memória na validação do `.blend` | 1.861.572 KiB, abaixo de 12 GiB |
| Memória na reabertura dos GLBs | 1.089.724 KiB, abaixo de 12 GiB |
| Tamanho do mestre | 351.298.658 bytes, abaixo de 2 GB |

Os relatórios completos estão em `reports/production-validation.json`, `reports/blend-validation.json`, `reports/regeneration-safety.json`, `reports/glb-validation.json`, `reports/export-report.json` e `reports/render-report.json`.

## Blender e sistema

O Blender 5.0.1 foi instalado pelo pacote do Kali. A transação atualizou 79 pacotes, instalou 45 e não removeu pacotes. `dpkg --audit` não encontrou configuração pendente. O teste básico confirmou execução normal, Python em segundo plano, salvamento de `.blend`, render em Eevee e exportação GLB.

O teste Cycles encontrou apenas a CPU e não expôs HIP; por isso, as três imagens 4K usam Eevee. A biblioteca Draco também não está presente no pacote do sistema, então os GLBs foram exportados sem compressão. O gerenciador de pacotes adiou reinícios de serviços durante a sessão; recomenda-se uma reinicialização normal do computador.

## Materiais e licenças

Foram reutilizados oito conjuntos PBR ambientais locais associados à Poly Haven: grama, trilha rochosa, solo de floresta, pedras de rio, duas famílias de rocha e duas cascas. O relatório `reports/LICENSES.json` registra a origem, o uso e a licença CC0. Não foram incluídos personagens nem animais com licenças diferentes.

Os recibos individuais dos arquivos baixados não estavam presentes nas pastas locais. A classificação usa os nomes e a organização já existentes no projeto, complementados pela política geral CC0 da editora. Essa limitação permanece registrada.

## Integridade da demo

O snapshot inicial continha 1.993 arquivos e agregado SHA-256 `74749444d244a19fe97e73dd4d6dbc5f6b96e2d885d7f5bf027d3c32943e5bbc`. A comparação de encerramento encontrou os mesmos 1.993 caminhos, sem adições ou remoções, mas 14 arquivos da demo apresentam conteúdo posterior ao snapshot.

Os geradores e validadores desta entrega escrevem somente dentro de `blender-world/`; a divergência foi preservada e documentada em `reports/demo-integrity.json`. Não foi feita uma restauração automática porque isso poderia apagar trabalho paralelo feito na demo durante a longa execução dos renders.

Um segundo snapshot, criado no momento da detecção, permaneceu idêntico até o fechamento: a estabilidade posterior foi aprovada.

## Limites conhecidos

- Os dois continentes estão integralmente representados no atlas, mas apenas a região comercial e a Turbulenta possuem definição regional.
- A região é uma base de produção para refinamento; não contém interiores completos nem acabamento final de todos os ativos.
- As fachadas e acessos de mina e caverna estão presentes; seus interiores ficam para modelagem futura.
- Vegetação densa é entregue como fontes GLB mais arquivos de instâncias JSON.
- O GLB não reproduz todos os efeitos específicos do Blender; o `.blend` permanece a fonte visual e editável.
- Colisão de gameplay, navegação, streaming e integração com o código da demo não fazem parte desta etapa.
- Nomes, identidades e fronteiras dos reinos continuam marcados como proposta.
