# Registro de execução

- Diretório de produção: `/home/https/Área de trabalho/workspace/rpg/blender-world`.
- Isolamento: o diretório RPG não possui repositório Git; a implementação foi mantida em uma pasta nova e a demo foi acompanhada por snapshots SHA-256.
- Blender instalado: `5.0.1+dfsg-5`, pacote Kali.
- Transação do sistema: 79 pacotes atualizados, 45 novos e 0 removidos; `dpkg --audit` limpo.
- Render final: Eevee, porque o teste Cycles encontrou apenas CPU e não expôs HIP.
- Exportação GLB: sem Draco, porque o pacote não inclui `libextern_draco.so`.

## Núcleo procedural

- Configuração declarativa concluída com sementes, escalas, biomas, distribuição, hidrologia, rotas, âncoras, materiais e seis reinos marcados como proposta.
- Sementes: atlas `26092401`, região `26092402`, Turbulenta `26092403`.
- Hash de altura do atlas: `ebe8cfabb5aba286a1067e8571529f90adb7e6a17c9f92fb901a7582b0e2d56c`.
- Hash de altura da região: `6d4c803145c4df73a5b9663225ef68cb7745f83492f113c601cd9c1cff685f61`.
- Hash de altura da Turbulenta: `6a70931fcb3535015450917b90e3e45311845b72257e858d2c84b7dd9ce549f4`.
- Duas execuções produziram hashes idênticos para alturas, água, umidade, inclinação e biomas.

## Arquivo mestre

- Cinco cenas concluídas: `ATLAS_MUNDO`, `REGIAO_COMERCIAL`, `TURBULENTA_01`, `BIBLIOTECA_LOCAL` e `VALIDACAO`.
- Contagem validada: 226 objetos, 34 materiais, 24 imagens empacotadas e cinco grupos de Geometry Nodes.
- Região: 16 blocos, rio contínuo, quatro rotas, dois entrepostos, mina, caverna, acampamento, observatório, Ermos, duas travessias, campos e canais.
- Biblioteca: 18 famílias de ativos ambientais e arquitetônicos; personagens e animais não foram incluídos.
- Regeneração v002 criada somente em memória; os hashes de `BASE_EDITAVEL` e `ACABAMENTO_MANUAL` permaneceram idênticos.

## Exportações e imagens

- 24 GLBs concluídos e reabertos no Blender.
- Tamanho total dos GLBs: 855.374.496 bytes.
- Pico de memória durante a reabertura dos GLBs: 1.089.724 KiB, abaixo do limite de 12 GiB.
- Arquivo mestre: 351.298.658 bytes, abaixo do limite aproximado de 2 GB.
- 12 imagens de validação em 2.560 × 1.440.
- Três imagens finais em 3.840 × 2.160.

## Testes

- 23 testes Python aprovados.
- Estrutura interna do Blender aprovada.
- Determinismo, bordas dos blocos, hidrologia, grafo de rotas, larguras, inclinações e exclusões de instâncias aprovados.
- 2.610 instâncias regionais verificadas sem colisões com rotas, pontes, entradas, praças ou áreas reservadas.
- Todos os mapas de altura PNG usam 16 bits e a faixa completa de valores.

## Integridade da demo

- Snapshot inicial: 1.993 arquivos, agregado `74749444d244a19fe97e73dd4d6dbc5f6b96e2d885d7f5bf027d3c32943e5bbc`.
- A verificação final encontrou os mesmos 1.993 caminhos, sem adições ou remoções, mas 14 arquivos da demo têm conteúdo posterior ao snapshot inicial.
- Nenhum gerador desta entrega possui saída em `demo/`; a divergência foi preservada, registrada em `reports/demo-integrity.json` e não foi revertida para evitar apagar trabalho paralelo.
- Um segundo snapshot foi criado após a detecção para verificar estabilidade até o fechamento da entrega.
