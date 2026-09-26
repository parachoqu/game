// Manifesto do lote Mixamo de combate de 26/09/2026 — fonte única de verdade do lote.
//
// `tools/process-animations.mjs --combat` converte exatamente estas 20 entradas e gera, a partir
// delas, o registro do runtime `src/engine/combat-clips.js`. Os testes de animação conferem o
// registro gerado, os GLB e os hashes das fontes contra esta lista.
//
// Todos os FBX vieram do Mixamo em FBX Binary, Without Skin, 30 fps, Keyframe Reduction: none, sobre
// o mesmo esqueleto Mixamo/Eve. As fontes são somente leitura: o `sha256` fixa o arquivo recebido e o
// processador recusa uma fonte alterada.
//
//   key             chave da ação no runtime (M.anims[key])
//   glb             derivado em demo/assets/ e em modelos 3d animados/animacoes/glb/
//   contactMode     'feet': o pé mais baixo do clipe encosta no chão da pose ociosa;
//                   'body': poses deitadas (morte, levantar) — o osso mais baixo do corpo encosta
//   locomotionMode  'cycle': ciclo de locomoção sincronizado com a caminhada; 'pose': postura em laço;
//                   'oneshot': ação tocada uma vez pelo estado do jogo
export const COMBAT_BATCH = 'COMBAT_2026_09_26';
export const COMBAT_DIR = 'mixamo-combate-2026-09-26';

export const COMBAT_2026_09_26 = [
  { source: 'Ataque_Alto_Espada_Escudo_SwordAndShieldAttack.fbx', key: 'swordAttackHigh', glb: 'anim_sword_attack_high.glb', contactMode: 'feet', locomotionMode: 'oneshot', use: 'ataque alto de espada/escudo',
    sha256: '705ea338b113d2ceecf51c927b6367f632f506f0bac26b77ff1e86365c5afb05' },
  { source: 'Caminhada_Re_WalkingBackwards.fbx', key: 'walkBack', glb: 'anim_walk_back.glb', contactMode: 'feet', locomotionMode: 'cycle', use: 'locomoção para trás',
    sha256: '0299be02a9f069109bbb5cd0794b0bd4da93499530fed1425ed5eade3dfce1ae' },
  { source: 'Combate_Lateral_Direita_WalkStrafeRight.fbx', key: 'strafeRight', glb: 'anim_strafe_right.glb', contactMode: 'feet', locomotionMode: 'cycle', use: 'deslocamento lateral direito',
    sha256: '974a32a0e76ab0e4bb6d56c79f8d36708e28d2a1a98356d979d5cba7d4a34d8f' },
  { source: 'Combate_Lateral_Esquerda_WalkStrafeLeft.fbx', key: 'strafeLeft', glb: 'anim_strafe_left.glb', contactMode: 'feet', locomotionMode: 'cycle', use: 'deslocamento lateral esquerdo',
    sha256: '7a7f193bf37f5c1fd9752f20888069656e0e91c459b259d8cf15ee2e0aeac617' },
  { source: 'Combo_Espada_Escudo_SwordAndShieldSlash.fbx', key: 'swordShieldSlash', glb: 'anim_sword_shield_slash.glb', contactMode: 'feet', locomotionMode: 'oneshot', use: 'combo de espada/escudo',
    sha256: '182b9010ab66c1eff15b28cfa101da90aff9df3af9bac4476b6d0029132647d2' },
  { source: 'Defesa_Escudo_SwordAndShieldBlockIdle.fbx', key: 'blockIdle', glb: 'anim_block_idle.glb', contactMode: 'feet', locomotionMode: 'pose', use: 'postura de bloqueio',
    sha256: 'e1a06767d4e621678d824a192240f7c0f266dcd4fcfec50ba389b59929482605' },
  { source: 'Defesa_Impacto_SwordAndShieldImpact.fbx', key: 'blockImpact', glb: 'anim_block_impact.glb', contactMode: 'feet', locomotionMode: 'oneshot', use: 'impacto absorvido por bloqueio/escudo',
    sha256: '5582100144ebef8226dfc7410ae30f2cbbf16fcac95263f4a6b682d533c485c8' },
  { source: 'Esquiva_Frontal_StandingDodgeForward.fbx', key: 'dodgeForward', glb: 'anim_dodge_forward.glb', contactMode: 'feet', locomotionMode: 'oneshot', use: 'esquiva para frente',
    sha256: 'dba2721eb61683923325526696b4db7cb660b7f6996e8895118066eeb48a0d5d' },
  { source: 'Esquiva_Lateral_Direita_StandingDodgeRight.fbx', key: 'dodgeRight', glb: 'anim_dodge_right.glb', contactMode: 'feet', locomotionMode: 'oneshot', use: 'esquiva para a direita',
    sha256: '63d2f48a01937321aa31cec1e5e55259c4b68dc4ff6004c86166a812cc5efe39' },
  { source: 'Esquiva_Lateral_Esquerda_StandingDodgeLeft.fbx', key: 'dodgeLeft', glb: 'anim_dodge_left.glb', contactMode: 'feet', locomotionMode: 'oneshot', use: 'esquiva para a esquerda',
    sha256: '53e8398cff5cd13f02b0015b930daaee9d15714d014fe6fa673109fc38b3f97a' },
  { source: 'Impacto_Direita_StandingReactLargeFromRight.fbx', key: 'hitRight', glb: 'anim_hit_right.glb', contactMode: 'feet', locomotionMode: 'oneshot', use: 'reação a impacto vindo da direita',
    sha256: '6718f15dad85eebcd53c35dcaea6d6835b392b729b39957aaf9a845cba03815e' },
  { source: 'Impacto_Esquerda_StandingReactLargeFromLeft.fbx', key: 'hitLeft', glb: 'anim_hit_left.glb', contactMode: 'feet', locomotionMode: 'oneshot', use: 'reação a impacto vindo da esquerda',
    sha256: '918ff7c9111f40c54948c4a1f746a7f5326552b119449a13ccc1032dd2a84606' },
  { source: 'Impacto_Frontal_StandingReactLargeFromFront.fbx', key: 'hitFront', glb: 'anim_hit_front.glb', contactMode: 'feet', locomotionMode: 'oneshot', use: 'reação a impacto frontal',
    sha256: 'd465e6ebd41995bbe3d017a2aa408b857f4ce208ec51ced37382b5484c3157d1' },
  { source: 'Impacto_Traseiro_StandingReactLargeFromBack.fbx', key: 'hitBack', glb: 'anim_hit_back.glb', contactMode: 'feet', locomotionMode: 'oneshot', use: 'reação a impacto traseiro',
    sha256: 'be4aa899eb0eb05d55ad2d4405f3741bbdbf7aaaa4e457c536f555fb34b5c932' },
  { source: 'Levantar_GettingUp.fbx', key: 'getUp', glb: 'anim_get_up.glb', contactMode: 'body', locomotionMode: 'oneshot', use: 'recuperação depois de derrubada',
    sha256: '8854b73765ed3dfdaa7ad44f5d8fadf5aff9c55eaaf2064f9c942f4d94cfa5fc' },
  { source: 'Morte_Frontal_StandingReactDeathForward.fbx', key: 'deathForward', glb: 'anim_death_forward.glb', contactMode: 'body', locomotionMode: 'oneshot', use: 'morte com queda para frente',
    sha256: 'cc946b93eca49625b816b9868b6f0c907d972c2cb97d24b222b6a87a3995f7a7' },
  { source: 'Morte_Traseira_StandingReactDeathBackward.fbx', key: 'deathBackward', glb: 'anim_death_backward.glb', contactMode: 'body', locomotionMode: 'oneshot', use: 'morte com queda para trás',
    sha256: '495c282308bcb69bcd3aefb17f4d41fcf2b0f8b68a6fd92609f93380f1013c3f' },
  { source: 'Recuo_Arco_StandingDodgeBackward.fbx', key: 'dodgeBackward', glb: 'anim_dodge_backward.glb', contactMode: 'feet', locomotionMode: 'oneshot', use: 'esquiva/recuo para trás',
    sha256: '9da126045314080cb7917cd15a53f4e004687c80c52ccaa0ba40dff107ab6219' },
  { source: 'Transicao_Agachado_EmPe_CrouchedToStanding.fbx', key: 'crouchToStand', glb: 'anim_crouch_to_stand.glb', contactMode: 'feet', locomotionMode: 'oneshot', use: 'transição de agachado para em pé',
    sha256: 'ef9dd67d884d0df3ee1bc95cd36e5094650cf2d4781e70b4f18a37123464b1e9' },
  { source: 'Transicao_EmPe_Agachado_StandingToCrouched.fbx', key: 'standToCrouch', glb: 'anim_stand_to_crouch.glb', contactMode: 'feet', locomotionMode: 'oneshot', use: 'transição de em pé para agachado',
    sha256: '3c39b6bae294e7a558333d41e76928c2f39f49b50d52d4ccfbb37abbeeee5e25' },
];
