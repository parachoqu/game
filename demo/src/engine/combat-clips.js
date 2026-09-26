// Gerado por tools/process-animations.mjs --combat a partir de tools/combat-manifest.mjs — não editar à mão.
// Lote COMBAT_2026_09_26: 20 clipes Mixamo de combate, obrigatórios no build.
import swordAttackHigh from '../../assets/anim_sword_attack_high.glb';
import walkBack from '../../assets/anim_walk_back.glb';
import strafeRight from '../../assets/anim_strafe_right.glb';
import strafeLeft from '../../assets/anim_strafe_left.glb';
import swordShieldSlash from '../../assets/anim_sword_shield_slash.glb';
import blockIdle from '../../assets/anim_block_idle.glb';
import blockImpact from '../../assets/anim_block_impact.glb';
import dodgeForward from '../../assets/anim_dodge_forward.glb';
import dodgeRight from '../../assets/anim_dodge_right.glb';
import dodgeLeft from '../../assets/anim_dodge_left.glb';
import hitRight from '../../assets/anim_hit_right.glb';
import hitLeft from '../../assets/anim_hit_left.glb';
import hitFront from '../../assets/anim_hit_front.glb';
import hitBack from '../../assets/anim_hit_back.glb';
import getUp from '../../assets/anim_get_up.glb';
import deathForward from '../../assets/anim_death_forward.glb';
import deathBackward from '../../assets/anim_death_backward.glb';
import dodgeBackward from '../../assets/anim_dodge_backward.glb';
import crouchToStand from '../../assets/anim_crouch_to_stand.glb';
import standToCrouch from '../../assets/anim_stand_to_crouch.glb';

export const COMBAT_BATCH = 'COMBAT_2026_09_26';
export default [
  { key: 'swordAttackHigh', bin: swordAttackHigh, glb: 'anim_sword_attack_high.glb', source: 'Ataque_Alto_Espada_Escudo_SwordAndShieldAttack.fbx', contactMode: 'feet', locomotionMode: 'oneshot', motion: [0.133,1.067] },
  { key: 'walkBack', bin: walkBack, glb: 'anim_walk_back.glb', source: 'Caminhada_Re_WalkingBackwards.fbx', contactMode: 'feet', locomotionMode: 'cycle', motion: null },
  { key: 'strafeRight', bin: strafeRight, glb: 'anim_strafe_right.glb', source: 'Combate_Lateral_Direita_WalkStrafeRight.fbx', contactMode: 'feet', locomotionMode: 'cycle', motion: [0.2,1.467] },
  { key: 'strafeLeft', bin: strafeLeft, glb: 'anim_strafe_left.glb', source: 'Combate_Lateral_Esquerda_WalkStrafeLeft.fbx', contactMode: 'feet', locomotionMode: 'cycle', motion: null },
  { key: 'swordShieldSlash', bin: swordShieldSlash, glb: 'anim_sword_shield_slash.glb', source: 'Combo_Espada_Escudo_SwordAndShieldSlash.fbx', contactMode: 'feet', locomotionMode: 'oneshot', motion: [0.433,3.267] },
  { key: 'blockIdle', bin: blockIdle, glb: 'anim_block_idle.glb', source: 'Defesa_Escudo_SwordAndShieldBlockIdle.fbx', contactMode: 'feet', locomotionMode: 'pose', motion: null },
  { key: 'blockImpact', bin: blockImpact, glb: 'anim_block_impact.glb', source: 'Defesa_Impacto_SwordAndShieldImpact.fbx', contactMode: 'feet', locomotionMode: 'oneshot', motion: null },
  { key: 'dodgeForward', bin: dodgeForward, glb: 'anim_dodge_forward.glb', source: 'Esquiva_Frontal_StandingDodgeForward.fbx', contactMode: 'feet', locomotionMode: 'oneshot', motion: [0.267,0.967] },
  { key: 'dodgeRight', bin: dodgeRight, glb: 'anim_dodge_right.glb', source: 'Esquiva_Lateral_Direita_StandingDodgeRight.fbx', contactMode: 'feet', locomotionMode: 'oneshot', motion: [0.167,0.833] },
  { key: 'dodgeLeft', bin: dodgeLeft, glb: 'anim_dodge_left.glb', source: 'Esquiva_Lateral_Esquerda_StandingDodgeLeft.fbx', contactMode: 'feet', locomotionMode: 'oneshot', motion: [0.2,0.9] },
  { key: 'hitRight', bin: hitRight, glb: 'anim_hit_right.glb', source: 'Impacto_Direita_StandingReactLargeFromRight.fbx', contactMode: 'feet', locomotionMode: 'oneshot', motion: [0.2,1.4] },
  { key: 'hitLeft', bin: hitLeft, glb: 'anim_hit_left.glb', source: 'Impacto_Esquerda_StandingReactLargeFromLeft.fbx', contactMode: 'feet', locomotionMode: 'oneshot', motion: [0.167,1.2] },
  { key: 'hitFront', bin: hitFront, glb: 'anim_hit_front.glb', source: 'Impacto_Frontal_StandingReactLargeFromFront.fbx', contactMode: 'feet', locomotionMode: 'oneshot', motion: [0.2,1.033] },
  { key: 'hitBack', bin: hitBack, glb: 'anim_hit_back.glb', source: 'Impacto_Traseiro_StandingReactLargeFromBack.fbx', contactMode: 'feet', locomotionMode: 'oneshot', motion: [0.133,1.233] },
  { key: 'getUp', bin: getUp, glb: 'anim_get_up.glb', source: 'Levantar_GettingUp.fbx', contactMode: 'body', locomotionMode: 'oneshot', motion: null },
  { key: 'deathForward', bin: deathForward, glb: 'anim_death_forward.glb', source: 'Morte_Frontal_StandingReactDeathForward.fbx', contactMode: 'body', locomotionMode: 'oneshot', motion: [0.267,2.533] },
  { key: 'deathBackward', bin: deathBackward, glb: 'anim_death_backward.glb', source: 'Morte_Traseira_StandingReactDeathBackward.fbx', contactMode: 'body', locomotionMode: 'oneshot', motion: [1.433,2.533] },
  { key: 'dodgeBackward', bin: dodgeBackward, glb: 'anim_dodge_backward.glb', source: 'Recuo_Arco_StandingDodgeBackward.fbx', contactMode: 'feet', locomotionMode: 'oneshot', motion: [0.267,1.2] },
  { key: 'crouchToStand', bin: crouchToStand, glb: 'anim_crouch_to_stand.glb', source: 'Transicao_Agachado_EmPe_CrouchedToStanding.fbx', contactMode: 'feet', locomotionMode: 'oneshot', motion: [0.1,0.433] },
  { key: 'standToCrouch', bin: standToCrouch, glb: 'anim_stand_to_crouch.glb', source: 'Transicao_EmPe_Agachado_StandingToCrouched.fbx', contactMode: 'feet', locomotionMode: 'oneshot', motion: [0.067,0.333] },
];
