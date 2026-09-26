# Projeto Game — demo de recorte

Demo 3D jogável e **offline** do recorte descrito no capítulo 16 do documento-mestre, com a Região Turbulenta (cap. 09) ligada por portal. Nomes, números e regras são provisórios.

## Jogar

Abra `dist/Projeto_Game_Demo.html` no navegador (Chrome, Edge ou Firefox recentes). Não precisa de internet nem de servidor. Requer teclado e mouse.

| Ação | Como |
| --- | --- |
| Andar | WASD (relativo à câmera) |
| Correr | Shift + WASD (ao clicar num inimigo, o personagem já vai correndo) |
| Agachar | segure Ctrl (passo curto, sem corrida) |
| Pular | Espaço |
| Salto impulsionado | Shift + Espaço (mais alto, com impulso à frente; gasta mais vigor) |
| Atacar | clique no inimigo: aproxima e ataca até ele cair |
| Usar (mercado, recurso, pessoa, portal, montaria) | clique nele, ou F no mais próximo |
| Mirar | segure o botão direito: o ponteiro trava, a mira vai para o centro da tela com retículo dinâmico e o mouse gira a câmera nos dois eixos. A vista aproxima por cima do ombro com mola anti-colisão (spring arm); o corpo e a coluna inclinam em 3D acompanhando a altura do alvo, projéteis voam em trajetória 3D com dano crítico na cabeça e hitmarker sonoro/visual. Se o navegador não travar o ponteiro, a mira segue o cursor e a borda gira a câmera |
| Trocar ombro da mira | V (alterna entre visão sobre o ombro direito e esquerdo) |
| Esquivar | C |
| Técnicas da arma | Q e E |
| Poção / montaria | 1 / R, ou clique na barra de ações |
| Menu (Inventário, Livro, Mapa, Intenções) | Tab, ao soltar (atalhos I inventário, B Livro, J intenções) |
| Distância da câmera | segure Tab e aperte 1 (distante), 2 (média) ou 3 (próxima) |
| Minimapa | sempre visível; M (ou o botão +) amplia e volta ao normal |
| Pausa / fechar | Esc |
| Girar / inclinar a câmera | arraste o chão com o botão esquerdo (lados giram, cima/baixo inclina) ou use as setas, até olhar acima do horizonte; o “N” do minimapa recentraliza. A câmera possui sistema de mola suave contra obstáculos e terreno. Ajustes de velocidade e inversão no menu de pausa (Esc) |
| Zoom | roda do mouse (zoom suave e contínuo) |

O progresso é salvo no navegador a cada 20 s de jogo ("Continuar trajetória" na tela inicial).

**Teclado protegido.** Agachado, apertar W vira Ctrl+W, que fecha a aba em qualquer página comum. Ao
entrar no jogo, a demo pede tela cheia e trava o teclado (Keyboard Lock API): assim Ctrl+W, Ctrl+T e
Ctrl+Tab chegam ao jogo e não fecham nem trocam a aba. Esc abre a pausa; segurar Esc sai da tela cheia.
A trava só existe no Chrome e no Edge, com o HTML aberto direto (não dentro de outra página, como o
Artifact); nos outros casos o navegador pergunta antes de sair. A opção fica no menu de pausa.

**Pulo, agachar e mira com o arco.** As poses vêm de clipes do Mixamo (FBX Binary, 30 fps) em
`modelos 3d animados/animacoes`: `Pulo_Jump.fbx`, `Salto_Impulsionado_RunningJump.fbx`,
`Agachado_Parado_CrouchIdle.fbx`, `Agachado_Andando_CrouchWalk.fbx` (com "In Place") e
`Mira_Arco_ShootingArrow.fbx`. Mirando com o arco, o personagem ergue e puxa o arco, segura puxado e
solta a corda no instante em que a flecha sai; andando, o tronco e os braços seguem o clipe e as pernas a
caminhada. Ao trocar algum desses arquivos: `node tools/process-animations.mjs --extra` e
`node build.mjs`. Sem os arquivos, as mecânicas continuam funcionando com a pose normal.

A interface usa um desenho de peças forjadas — molduras de ferro com fio de bronze, rebites nos cantos, placas gravadas e barras em canaleta —, e o Livro continua em velino, agora com cantoneiras de metal. O mundo é montado numa tela de carregamento própria, que mostra as cinco fases, o avanço e as regras das zonas enquanto trabalha; a tela inicial só aparece quando tudo está pronto.

## Gráficos

Os elementos do mapa (terreno, vegetação, rochas, água, céu e construções) usam materiais PBR. Solo, trilhas, chão de bosque, rochas, margens de rio, cascas e folhagens combinam texturas procedurais com materiais fotográficos CC0 da Poly Haven preparados localmente. Os derivados ficam em `assets/nature/` em WebP (cor sRGB, normal e um mapa de dados com altura no vermelho e rugosidade no verde; a folhagem leva o recorte no alfa), somam 4,8 MB e são embutidos no HTML final: nada é baixado durante o jogo. Há céu atmosférico com nuvens, iluminação a partir do céu, névoa de distância, água que reflete o céu, árvores com folhagem e grama que balançam com o vento, oclusão de ambiente e anti-serrilhado.

Personagens, criaturas e montaria usam modelos realistas com textura (PBR) da pasta `modelos 3d animados`, embutidos no próprio HTML:

- **Jogador, NPCs, guardas e sombras da Turbulenta:** personagens do Mixamo (Kachujin e Eve) com biomecânica realista e sincronização contínua de fase entre caminhada e corrida (eliminando passos fora de compasso ou pés cruzados). Passos em marcha ré ao recuar/mirar para trás, inclinação lateral em strafe, inércia dinâmica (tronco avança na arrancada e recua com amortecimento de quadril na frenagem), postura ociosa viva ("Living Idle") com respiração multi-articular (abdômen, tórax e ombros), transferência natural de peso entre os pés e micro-olhares orgânicos. Estabilização vestíbulo-ocular que mantém a cabeça nivelada com o horizonte durante curvas e aclives/declives. Clipes mocap reais de combate (golpes, estocada, arco, magia, rolamento e queda) preservados com interpolação de envelopes sem estalos. Guardas e saqueadores têm tons próprios; as sombras da Turbulenta viram silhuetas violeta.
- **Garras:** leoa com animações de respiração e andar, bote, agachamento procedural e inclinação dinâmica (banking) em curvas e encostas. A líder é maior e mais escura; a pálida, mais clara.
- **Montaria:** cavalo de carga com sela, animações de parado e andar, com alinhamento de encosta e inclinação centrípeta suave em curvas.
- **Armas:** espada, arco e martelo em PBR com as texturas procedurais, presas ao osso da mão.

A qualidade (Alta, Média, Baixa) fica no menu de pausa (Esc). Sem escolha salva, o jogo mede os primeiros segundos e desce um nível sozinho se ficar lento.

Otimizações de desempenho:

- O terreno é dividido em 16 blocos de 256 m, subdivididos em peças de 64 m; cada peça alterna entre duas resoluções conforme a distância, e os blocos fora de alcance saem da cena.
- A Região Turbulenta só entra na cena quando a câmera se aproxima dela.
- Grama, flores, cogumelos, seixos e galhos são gerados por células de 32 m em volta da câmera (84 m em Alta, 68 em Média, 48 em Baixa), com semente própria por célula e malhas reaproveitadas de um pool; longe dali eles não existem.
- Árvores, arbustos e rochas escolhem o nível de detalhe instância por instância (L0 até 28 m, L1 até 65 m, L2 até 150 m em Alta); os blocos de 128 m servem só ao recorte de visão. Além disso as árvores viram impostores — duas fotos da própria árvore do kit, tiradas no carregamento, em dois quads cruzados de 4 triângulos — até 700 m. As árvores nunca rareiam com a qualidade: Média e Baixa só encurtam as distâncias de cada nível.
- Os modelos do kit têm três níveis orçados em triângulos; o abeto em detalhe máximo só aparece nos primeiros 28 m.
- Os colisores do cenário vêm dos volumes medidos na preparação dos ativos: nenhuma consulta a triângulos durante o jogo.
- Só os personagens próximos projetam sombra.
- Modelos 3D reduzidos no pipeline: malhas simplificadas (7–9 mil triângulos por personagem), texturas WebP 1024, animações a 30 qps sem canais parados, sem os ossos dos dedos nem os ossos sem peso, compressão meshopt (1,8 MB no total).
- Cada personagem tem esqueleto próprio, com geometria e texturas compartilhadas. O esqueleto é atualizado uma vez por quadro, e personagens distantes atualizam a animação a cada 3 ou 6 quadros.
- As camadas de terreno usam 512 px: o material repete a cada ~2 m e a memória de vídeo cai à quarta parte.
- Os mipmaps da folhagem são calculados na CPU preservando a área coberta pelo recorte; sem isso a copa some com a distância.
- A entrada do bloom é limitada: sem o teto, o disco do sol (milhares em HDR) se espalha como um véu branco sobre a tela.
- O mapa de sombra é desenhado uma vez por quadro.
- A oclusão de ambiente roda em meia resolução e ignora céu, água e toda a vegetação instanciada.
- A sombra da vegetação vem de cópias no nível mais simples (L2), desenhadas só na passada de sombra e só até 55 m (45 m em Média, sem sombra em Baixa); as árvores visíveis não projetam.
- As peças de arquitetura da mesma superfície viram uma malha só, com a cor de cada peça nos vértices.
- Os sombreadores são compilados na tela de carregamento, para não travar ao chegar numa área nova.
- Com o jogo pausado, a cena para de ser redesenhada.

## O mundo

O cenário vem da base de produção do Blender (`../blender-world`), preparada para a demo por
`tools/build-world-runtime.mjs`. A região comercial ocupa 1.024 × 1.024 m (1 unidade = 1 metro), em
16 blocos de 256 m; a Região Turbulenta tem 256 × 256 m e fica tecnicamente deslocada em X, longe da
região principal, para as duas cenas nunca se sobreporem.

O relevo é um heightfield determinístico de 1 m: a mesma fonte responde ao chão que o jogador pisa e
à malha que aparece na tela. Estradas, arquitetura, pontes, rio e pontos de interesse vêm do arquivo
autoral; a vegetação reproduz as posições registradas no Blender e adensa o restante seguindo o mapa
de biomas da própria fonte.

O rio principal é refeito ao carregar, sobre o próprio heightfield (`src/world/river.js`): o curso
serpenteia dentro do vale, a largura (de 6 a 18 m) e a profundidade variam, a margem externa das curvas
vira barranco e a interna, praia de cascalho, e o terreno logo fora da água fica sempre acima da
lâmina. Nas pontes o canal passa pelo vão do tabuleiro; nada é cavado sobre estradas nem perto de
construções. A lâmina plana de 1 km e os dois canais retos de irrigação da exportação não são mais
desenhados.

A maior parte da região é floresta (cerca de 22 mil árvores): o campo de densidade de
`src/world/vegetation-fields.js` forma massas de mata com bordas irregulares, touceiras, clareiras
pequenas e campos abertos, e cada árvore tem escala, altura, inclinação e tom próprios. Estradas,
pontes, construções, serviços, pontos de encontro, portais e nós de coleta ficam livres
(`src/world/functional-areas.js`), com uma orla de arbustos, flores e grama em volta para não
parecerem clareiras recortadas.

A natureza — árvores, arbustos, grama, flores, cogumelos, pedras, penhascos, troncos, tocos, o
acabamento do chão e a água — vem do kit **Ultimate Nature – Starter** (Innerverse Interactive),
preparado por `tools/build-nature-kit.mjs`. Cada modelo entra com três níveis de detalhe orçados em
triângulos, e o HDRI de 250 MB do pacote virou um envmap de 39 KB que alimenta a luz de ambiente do
dia. Construções, pontes e pontos de interesse seguem com os materiais PBR fotográficos.

Detalhes da integração, medições e limites conhecidos estão em `WORLD_INTEGRATION_REPORT.md`.

## O que está no recorte

- **Entreposto do Vale** (protegido): mercado, bancada de fabricação e reparo, instrutor, armazém, estábulo e quadro de relatos.
- **Bosque das Forjas**: minério, madeira e erva-lume.
- **Passagem Estreita** (Fronteira) × **Estrada Longa** (protegida e mais longa) até o **Entreposto Alto**, ligadas pelas duas pontes sobre o Rio Largo.
- **Boca da Mina** e **Caverna do Oeste**, no fim da trilha que sai da ponte principal.
- **Acampamento das Garras** com estados Estabelecido → Pressionado → Deslocado → Em recuperação.
- **Evento regional** de reabertura da Passagem, com contribuições civis e de combate e o painel das quatro perguntas.
- **Ermos Quebrados** (Full Loot) e **Observatório Antigo**, onde se registra o desaparecimento das estrelas.
- **Portal Instável** e a **Região Turbulenta**: portal temporário, entrada individual, silhuetas anônimas, objetivo central, duas saídas, extração e colapso.
- **O Livro**: História, Feitos, Descobertas, Legado e Perfil; registros agregados, pessoal/público e anotações.

## Reconstruir

Os pacotes já acompanham a demo. Quando as exportações do Blender em `../blender-world/exports`
mudarem, reconstrua o pacote de runtime:

```bash
node demo/tools/build-world-runtime.mjs
```

Quando o kit de natureza em `../modelos 3d animados/Ultimate Nature Starter.unitypackage` mudar:

```bash
node demo/tools/build-nature-kit.mjs
```

Ele lê o `.unitypackage` sem alterá-lo, converte os modelos com o FBX2glTF, monta três níveis de
detalhe por tipo, recomprime as texturas em WebP e reduz o HDRI do céu. A opção `--no-sky` pula a
redução do HDRI, que é o passo lento.

Ele valida o manifesto de origem, confere o heightfield contra os 16 blocos exportados e grava
`assets/world-runtime/` (manifesto compacto, heightfields, máscaras e GLB otimizados com meshopt).
Nunca escreve em `blender-world/`. Os testes dos contratos do mundo rodam com:

```bash
node demo/tools/run-world-tests.mjs
```

Quando os originais em `../modelos 3d animados/texturas/` forem alterados, reconstrua os derivados
de natureza:

```bash
node demo/tools/build-nature-assets.mjs
```

Depois gere os HTMLs offline:

```bash
node demo/build.mjs
```

Gera `dist/Projeto_Game_Demo.html` (documento completo) e `dist/projeto-game-demo.html` (mesmo conteúdo sem `<html>`/`<head>`, usado para publicar o preview). O build usa o esbuild local; outro caminho pode ser indicado em `ESBUILD_BIN`. O código-fonte fica em `src/` (`engine/`, `game/`, `ui/`) e o Three.js r185 (MIT), com os addons usados (céu, pós-processamento), em `vendor/three/`.

Os modelos vêm de `assets/*.glb`, gerados a partir da pasta `modelos 3d animados` por:

```bash
cd demo/tools && npm install && node build-assets.mjs
```

O pipeline converte os FBX com FBX2glTF e otimiza com glTF-Transform, meshoptimizer e sharp (ver `tools/build-assets.mjs`). O build do jogo embute os GLBs no HTML.

Adicionar `?debug=1` ao endereço expõe `window.__demo` (teleporte, itens, forçar portal, noite, estrelas, câmera livre e estado do mundo) e desenha os limites, âncoras, rotas e colisores do cenário.

## Créditos dos modelos 3D

- Natureza do cenário (árvores, arbustos, grama, flores, cogumelos, pedras, penhascos, troncos, tocos, camadas do terreno, água e o HDRI do céu): **Ultimate Nature – Starter**, de [Innerverse Interactive](https://tinyurl.com/InnerverseInteractive), distribuído pela Unity Asset Store. Os arquivos originais não são alterados nem redistribuídos; o que entra no HTML são derivados convertidos e recomprimidos. O uso fora da Unity depende da licença adquirida.
- Materiais fotográficos de terreno, rochas, cascas e folhagens: [Poly Haven](https://polyhaven.com), licença [CC0](https://creativecommons.org/publicdomain/zero/1.0/). A relação de arquivos, URLs de origem e hashes fica em `../modelos 3d animados/texturas/manifesto.json`.
- “Kachujin G Rosales”, “Eve By J.Gonzales” e a animação “Unarmed Walk Forward”: [Mixamo](https://www.mixamo.com) (Adobe).
- “HORSE - Realistic 3D Model (DEMO FREE)” e “LIONESS - Realistic 3D Model (DEMO FREE)”: [WildMesh 3D](https://sketchfab.com/WildMesh_3D), licença [CC BY-NC 4.0](http://creativecommons.org/licenses/by-nc/4.0/). Uso não comercial, com crédito ao autor. Os modelos foram reduzidos e comprimidos para esta demo.
