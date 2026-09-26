# Prompt operacional para Claude — integrar o lote Mixamo de combate

Trabalhe no repositório `/home/https/Área de trabalho/workspace/rpg`.

## Missão

Integre ao pipeline e ao runtime do demo **somente** as 20 animações FBX que já estão em:

`modelos 3d animados/animacoes/mixamo-combate-2026-09-26/`

O objetivo é preencher as lacunas de movimentação direcional, esquiva, impacto, morte, recuperação e transições de postura de personagens humanos, dando prioridade ao combate. Não redesenhe o combate nem altere regras de gameplay para se adequarem aos clipes. A animação deve seguir o estado do jogo; ela não passa a controlar física, dano, janelas de acerto, colisão ou invulnerabilidade.

Não baixe arquivos adicionais e não renomeie, edite ou substitua os FBXs de origem.

## Leia e registre antes de editar

1. Leia `AGENTS.md` e todas as instruções locais que ele referenciar, se existirem no checkout.
2. Inspecione a árvore e rode `git status --short`, `git branch --show-current` e `git diff --stat`.
3. Registre um baseline SHA-256 dos 20 FBXs. Ao terminar, confira que todos os hashes continuam idênticos.
4. Leia, no mínimo:
   - `demo/tools/process-animations.mjs`
   - `demo/src/engine/extra-clips.js`
   - `demo/src/engine/models.js`
   - `demo/src/engine/characters.js`
   - `demo/src/game/player.js`
   - `demo/src/game/enemies.js`
   - `demo/src/game/combat.js`
   - `demo/src/game/npcs.js`
   - os testes e scripts de build existentes em `demo/`
5. Antes de mudar comportamento, escreva testes de caracterização para as seleções e contratos que serão afetados.

## Estado de entrada confirmado

- O lote tem exatamente 20 arquivos FBX Binary.
- Todos foram baixados do Mixamo com `Without Skin`, `30 FPS` e `Keyframe Reduction: none`.
- Os 20 usam o mesmo esqueleto Mixamo/Eve, contêm uma hierarquia `Hips` e não repetem dados binários ou dados de clipe entre si.
- Uma validação estrutural externa converteu e abriu os 20 arquivos: `20/20` válidos, `0` erros.
- Atenção: o `FBX2glTF` instalado converte animação em aproximadamente 24 FPS quando a taxa não é informada. Para preservar os 30 FPS de origem, use explicitamente `--anim-framerate bake30`.
- O processador atual só descobre FBXs diretamente em `modelos 3d animados/animacoes/`; o subdiretório datado acima ainda não é processado.
- O processador atual também pode capturar falhas por arquivo e terminar com mensagem de sucesso, deixando GLBs antigos. Para este lote, falha parcial deve encerrar com código diferente de zero e nunca ser disfarçada por artefato antigo.

## Mapeamento obrigatório

Use nomes de chave e de saída estáveis. Se a arquitetura existente exigir outro padrão, mantenha uma tabela equivalente, documente a diferença e preserve todas as 20 entradas.

| FBX de origem | Chave sugerida no runtime | GLB sugerido | Uso previsto |
|---|---|---|---|
| `Ataque_Alto_Espada_Escudo_SwordAndShieldAttack.fbx` | `swordAttackHigh` | `anim_sword_attack_high.glb` | ataque alto de espada/escudo, apenas em contexto compatível |
| `Caminhada_Re_WalkingBackwards.fbx` | `walkBack` | `anim_walk_back.glb` | locomoção para trás |
| `Combate_Lateral_Direita_WalkStrafeRight.fbx` | `strafeRight` | `anim_strafe_right.glb` | deslocamento lateral direito |
| `Combate_Lateral_Esquerda_WalkStrafeLeft.fbx` | `strafeLeft` | `anim_strafe_left.glb` | deslocamento lateral esquerdo |
| `Combo_Espada_Escudo_SwordAndShieldSlash.fbx` | `swordShieldSlash` | `anim_sword_shield_slash.glb` | combo de espada/escudo, apenas em contexto compatível |
| `Defesa_Escudo_SwordAndShieldBlockIdle.fbx` | `blockIdle` | `anim_block_idle.glb` | postura de bloqueio, apenas quando houver estado visual compatível |
| `Defesa_Impacto_SwordAndShieldImpact.fbx` | `blockImpact` | `anim_block_impact.glb` | impacto absorvido por bloqueio/escudo |
| `Esquiva_Frontal_StandingDodgeForward.fbx` | `dodgeForward` | `anim_dodge_forward.glb` | esquiva para frente |
| `Esquiva_Lateral_Direita_StandingDodgeRight.fbx` | `dodgeRight` | `anim_dodge_right.glb` | esquiva para a direita |
| `Esquiva_Lateral_Esquerda_StandingDodgeLeft.fbx` | `dodgeLeft` | `anim_dodge_left.glb` | esquiva para a esquerda |
| `Impacto_Direita_StandingReactLargeFromRight.fbx` | `hitRight` | `anim_hit_right.glb` | reação a impacto vindo da direita |
| `Impacto_Esquerda_StandingReactLargeFromLeft.fbx` | `hitLeft` | `anim_hit_left.glb` | reação a impacto vindo da esquerda |
| `Impacto_Frontal_StandingReactLargeFromFront.fbx` | `hitFront` | `anim_hit_front.glb` | reação a impacto frontal |
| `Impacto_Traseiro_StandingReactLargeFromBack.fbx` | `hitBack` | `anim_hit_back.glb` | reação a impacto traseiro |
| `Levantar_GettingUp.fbx` | `getUp` | `anim_get_up.glb` | recuperação depois de derrubada |
| `Morte_Frontal_StandingReactDeathForward.fbx` | `deathForward` | `anim_death_forward.glb` | morte com queda para frente |
| `Morte_Traseira_StandingReactDeathBackward.fbx` | `deathBackward` | `anim_death_backward.glb` | morte com queda para trás |
| `Recuo_Arco_StandingDodgeBackward.fbx` | `dodgeBackward` | `anim_dodge_backward.glb` | esquiva/recuo para trás |
| `Transicao_Agachado_EmPe_CrouchedToStanding.fbx` | `crouchToStand` | `anim_crouch_to_stand.glb` | transição de agachado para em pé |
| `Transicao_EmPe_Agachado_StandingToCrouched.fbx` | `standToCrouch` | `anim_stand_to_crouch.glb` | transição de em pé para agachado |

Os clipes `Standing Dodge Forward/Left/Right` encontrados no Mixamo são variantes com arco. Confira visualmente a postura dos braços com cada equipamento atual. Se um clipe causar interseção ou postura incompatível, mantenha-o carregado e validado, mas use o `roll` atual como fallback naquele contexto. Não force um resultado visual ruim só para afirmar que o clipe está ativo.

## Limites inegociáveis

- Mantenha `blender-world/` somente leitura.
- Derivados do mundo continuam exclusivamente em `demo/assets/world-runtime/`; esta tarefa não deve alterar o mundo.
- Preserve controles, movimentação física, combate, dano, hitboxes, economia, inventário, UI, portais, salvamentos e regras atuais.
- Preserve o contrato de coordenadas do mundo: Blender/JSON `(x, y, height)` para Three.js `(x, height, -y)`, com `PLAN_Z_SIGN=-1`.
- Não aplique animações humanas a leoa, cavalo ou qualquer criatura que use `animateBeast`.
- O jogo hoje não possui uma família visual completa de escudo para todos os personagens. Não invente um escudo, um novo equipamento ou uma nova mecânica. Os quatro clipes de espada/escudo devem ser expostos pelo pipeline; só os ative quando um estado e um modelo existentes tornarem a animação coerente. Caso contrário, deixe o uso opcional e documente a condição.
- Não retire clipes atuais nem quebre seus fallbacks.
- Não edite manualmente `demo/src/engine/extra-clips.js` sem atualizar o gerador correspondente.
- **Não rode `demo/tools/build-assets.mjs`**: esse script limpa recursivamente o diretório de saída e pode apagar assets adicionais de natureza/mundo. Use apenas o processador direcionado de animações e o build normal do demo.
- Não faça commit, push ou publicação sem pedido explícito.

## Implementação do pipeline

1. Estenda `demo/tools/process-animations.mjs` com uma lista explícita para este lote e um modo direcionado `--combat`. Evite descoberta recursiva implícita: cada um dos 20 arquivos esperados deve aparecer em um manifesto verificável.
2. Torne os modos inequívocos e mutuamente exclusivos: modo padrão = biblioteca principal + extras; `--extra` = somente extras; `--combat` = somente o manifesto `COMBAT_2026_09_26`. O modo `--combat` não pode reprocessar a biblioteca antiga nem reescrever `extra-clips.js`, salvo se provar que o arquivo permanece semanticamente idêntico.
3. Passe `--anim-framerate bake30` ao `FBX2glTF`.
4. Remova meshes, materiais e skins do derivado; mantenha somente a animação e os nós necessários ao retarget.
5. Remova canais estáticos quando seguro, preserve a rotação dos ossos e normalize o nome do clipe para a chave do manifesto.
6. Preserve a altura de repouso de `Hips` a partir do `refY` do esqueleto fonte para **todos** os clipes novos, sobretudo agachar, morte e levantar. Não reutilize uma altura genérica que cause salto vertical. Use um registro único de metadados por clipe, no mínimo `{ key, bin, refY, contactMode, locomotionMode }`, e faça a extração de `Hips`/`refY` servir tanto à biblioteca existente quanto ao lote novo, sem criar uma segunda convenção divergente.
7. Torne o clipe estritamente in-place: X/Z de `Hips` devem ficar constantes em **todos os samples**, e não apenas terminar no ponto inicial depois de subtrair uma interpolação entre primeiro e último frame. A movimentação continua sendo autoridade do gameplay. Preserve apenas o componente vertical indispensável à pose e teste todos os samples do track.
8. Se a calibração de contato existente (`CONTACT` ou equivalente) não cobrir poses baixas, estenda-a de modo explícito para agachar, morte e levantar. Não esconda o problema com deslocamento global arbitrário.
9. Escreva os GLBs otimizados nos diretórios esperados pela arquitetura atual (`modelos 3d animados/animacoes/glb/` e/ou `demo/assets/`, conforme o contrato existente) sem apagar arquivos alheios.
10. Gere o registro/import map pelo mesmo script. Pode haver um novo registro gerado específico de combate, desde que exista uma única fonte de verdade e o runtime não dependa de imports manuais divergentes.
11. Faça o processo falhar imediatamente se um FBX esperado estiver ausente, se a conversão falhar, se o GLB estiver vazio ou se não houver exatamente um clipe não vazio. Não aceite GLB antigo como substituto silencioso.
12. Ao final do modo `--combat`, valide e informe: 20 fontes, 20 saídas, zero falhas, zero duplicatas inesperadas, zero mesh/skin e nomes de clipe correspondentes ao manifesto.

## Integração no runtime

1. Em `demo/src/engine/models.js`, carregue, parseie e retargete as 20 animações para os três humanos existentes (`paladina`, `kachujin` e `eve`). Os 20 derivados são obrigatórios no build: fonte ou GLB físico ausente deve falhar no pipeline/build. O fallback de runtime vale para uma ação opcional que não esteja disponível no registro resolvido, testada por injeção/mock de registro parcial — nunca apagando um GLB importado estaticamente.
2. Em `demo/src/engine/characters.js`, adicione seleção e blend sem desmontar o sistema atual:
   - `walkBack` quando o componente longitudinal for negativo;
   - `strafeLeft`/`strafeRight` pelo sinal do componente lateral;
   - blend coerente nas diagonais, preservando normalização de velocidade e fase;
   - esquiva direcional pelos quatro clipes, com `roll` como fallback;
   - impacto pelo lado de origem;
   - morte direcional e `getUp` quando o estado existente realmente retornar de derrubada. Hoje `down` volta diretamente a `free` depois do resgate ou evolui para `dead`, sem estado `gettingUp`; portanto, se `getUp` for acionado, implemente-o como camada **puramente visual**, disparada uma vez no resgate e desacoplada de `P.state`, sem atrasar controle, física ou timers. Se isso não for visualmente seguro, deixe o clipe carregado/validado e não acionado, documentando a limitação;
   - transições de agachar sem atrasar a resposta dos controles;
   - ataques e bloqueios de espada/escudo apenas nos contextos compatíveis descritos acima.
3. Em `demo/src/game/player.js`, forneça ao animador a direção necessária sem alterar os tempos atuais. O deslocamento da esquiva continua por `P.dodgeYaw`; preserve a janela física de `0.34 s`, a invulnerabilidade até `0.28 s` e o fim do estado em `0.42 s`, salvo se o código atual comprovar valores diferentes no momento da implementação.
4. Em `demo/src/game/combat.js`, registre de forma transitória o vetor/lado do impacto a partir das informações já fornecidas a `hurtPlayer` e `hurtEnemy`. Crie estado visual separado e de disparo único, por exemplo `hitDir`, `hitAnimT` e `deathDir`, definido no instante do dano/morte, com fallback determinístico quando `src`/`from` não existir. Classifique o vetor **da vítima até a origem do golpe** no referencial da própria vítima: escolha o eixo dominante; origem à direita seleciona `hitRight`, à esquerda seleciona `hitLeft`, à frente seleciona `hitFront` e atrás seleciona `hitBack`. Confirme visualmente que `From Right/Left/Front/Back` do Mixamo corresponde a essa semântica; se houver divergência, inverta somente a tabela clipe → lado, nunca o cálculo geométrico nem o significado armazenado. Não mude cálculo de dano, knockback ou stagger.
5. Em `demo/src/game/enemies.js`, use reação e morte direcionais somente em inimigos/guardas humanoides. Separe inequivocamente a pose recuperável `down` da morte direcional antes de chamar o animador; hoje ambos podem chegar como `down: true`. Criaturas continuam no sistema de beasts. O ataque melee genérico deve conservar o fallback atual quando não houver correspondência visual segura.
6. Em `demo/src/game/npcs.js`, preserve `idle`, `walk` e `craft`. Este lote não inclui novas animações utilitárias; não tente reaproveitar clipes de combate como trabalho social.
7. Um clipe não deve reiniciar a cada frame. Preserve crossfades, timeScale, sincronização e liberação de recursos do mixer.

## Testes e validação obrigatórios

Crie testes pequenos e determinísticos no padrão de `demo/tests/` para, no mínimo:

- manifesto completo com exatamente 20 entradas e arquivos existentes;
- mapeamento arquivo → chave → GLB sem colisões;
- escolha de `walkBack`, `strafeLeft` e `strafeRight`, inclusive diagonais;
- escolha das quatro esquivas e fallback para `roll`;
- classificação do impacto como frente/trás/esquerda/direita no referencial do personagem;
- reação/morte humana sem atingir `animateBeast`;
- fallback quando uma chave opcional está ausente de um registry parcial injetado/mockado, sem remover GLB estaticamente importado;
- contrato in-place de `Hips` e preservação do `refY`;
- falha do processador diante de fonte ou saída ausente/inválida;
- hashes dos 20 FBXs de origem inalterados.

Rode e registre, ajustando apenas se o repositório tiver comandos oficiais mais específicos:

```bash
node demo/tools/process-animations.mjs --combat
node demo/tools/run-animation-tests.mjs
node demo/tools/run-world-tests.mjs
node demo/build.mjs
```

Crie `demo/tools/run-animation-tests.mjs` ou estenda de forma comprovável o runner oficial para incluir os novos testes. `run-world-tests.mjs` atualmente empacota apenas os testes do mundo e, sozinho, não comprova os contratos de animação acima.

Depois, faça QA no navegador servindo o demo por HTTP local. Use um comando reproduzível, por exemplo `python3 -m http.server 8934 --directory demo/dist`, registre a URL e teste, no mínimo:

- boot sem erro e console limpo;
- `paladina`, `kachujin` e `eve`;
- guardas/bandidos humanoides;
- frente, trás, esquerda, direita e diagonais;
- quatro direções de esquiva, incluindo fallback quando a pose de arco não for compatível;
- impactos vindos das quatro direções;
- as duas mortes e a recuperação, somente onde o fluxo de estado permitir;
- entrar e sair de agachamento;
- ataques atuais e, quando houver contexto coerente, os clipes de espada/escudo;
- registry parcial injetado/mockado para confirmar o fallback sem remover um arquivo obrigatório;
- UI, controles, colisão, portais e salvamento sem regressão aparente;
- criaturas sem receber animações humanas.

Se o artefato distribuído suporta `file://`, faça também essa checagem offline; se não suporta, registre a limitação em vez de inventar sucesso. Build e testes estruturais não contam como prova visual ou de gameplay.

## Critérios de aceite

- Os 20 FBXs originais permanecem byte a byte idênticos.
- O pipeline dirigido produz 20 GLBs válidos em `bake30`, um clipe não vazio por arquivo, sem mesh/skin e com X/Z de `Hips` constante em todos os samples.
- O registro gerado e os imports do runtime têm uma fonte de verdade verificável.
- A movimentação direcional, as quatro esquivas e as quatro reações escolhem clipes coerentes, com fallbacks testados.
- Morte, levantar e agachar não causam salto vertical, afundamento ou teleporte horizontal. Se `getUp` não puder tocar sem alterar a state machine, ele permanece carregado e validado, mas seu acionamento fica explicitamente adiado.
- A autoridade de gameplay e todos os subsistemas preservados continuam intactos.
- Humanos recebem os novos clipes; beasts não.
- Testes do lote executados pelo runner de animação, testes do mundo e build passam em execuções frescas.
- A validação visual informa com honestidade o que foi realmente observado.

## Entrega final

Ao terminar, responda com:

1. lista exata dos arquivos alterados/criados;
2. tabela final FBX → chave → GLB → estados que realmente usam o clipe;
3. comprovação de que os hashes das fontes não mudaram;
4. comandos executados, códigos de saída e contagens de testes;
5. evidência separada de build, testes estruturais, navegador e QA de gameplay;
6. fallbacks ainda ativos e clipes carregados, mas não acionados por falta de contexto visual seguro;
7. problemas restantes, sem declarar como validado aquilo que não foi executado.
