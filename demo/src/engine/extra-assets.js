// Gerado por tools/build-extra-assets.mjs — não editar à mão.
// Assets extras (pontes, construções, marcos, criatura e armas) e o manifesto de origem de cada um.
import MANIFEST from '../../assets/extra-props/extra-props-manifest.json';
import ponte_pedra from '../../assets/extra-props/ponte_pedra.glb';
import ponte_quebrada from '../../assets/extra-props/ponte_quebrada.glb';
import poco from '../../assets/extra-props/poco.glb';
import casa_palha from '../../assets/extra-props/casa_palha.glb';
import ferraria from '../../assets/extra-props/ferraria.glb';
import torre_a from '../../assets/extra-props/torre_a.glb';
import torre_b from '../../assets/extra-props/torre_b.glb';
import moinho from '../../assets/extra-props/moinho.glb';
import arvore_marco from '../../assets/extra-props/arvore_marco.glb';
import cao_vazio from '../../assets/extra-props/cao_vazio.glb';
import casa_s32_a from '../../assets/extra-props/casa_s32_a.glb';
import casa_s32_b from '../../assets/extra-props/casa_s32_b.glb';
import casa_s32_c from '../../assets/extra-props/casa_s32_c.glb';
import casa_pedra from '../../assets/extra-props/casa_pedra.glb';
import armazem from '../../assets/extra-props/armazem.glb';
import torre_escombros from '../../assets/extra-props/torre_escombros.glb';
import banca from '../../assets/extra-props/banca.glb';
import doca from '../../assets/extra-props/doca.glb';
import muro_a from '../../assets/extra-props/muro_a.glb';
import muro_b from '../../assets/extra-props/muro_b.glb';
import arma_espada from '../../assets/extra-props/arma_espada.glb';
import arma_machado from '../../assets/extra-props/arma_machado.glb';
import arma_martelo from '../../assets/extra-props/arma_martelo.glb';
import arma_lanca from '../../assets/extra-props/arma_lanca.glb';
import arma_adaga from '../../assets/extra-props/arma_adaga.glb';
import arma_foice from '../../assets/extra-props/arma_foice.glb';
import arma_arco from '../../assets/extra-props/arma_arco.glb';
import arma_manopla from '../../assets/extra-props/arma_manopla.glb';
import arma_tomo from '../../assets/extra-props/arma_tomo.glb';
import arma_cajado from '../../assets/extra-props/arma_cajado.glb';

export const EXTRA_BINS = { ponte_pedra, ponte_quebrada, poco, casa_palha, ferraria, torre_a, torre_b, moinho, arvore_marco, cao_vazio, casa_s32_a, casa_s32_b, casa_s32_c, casa_pedra, armazem, torre_escombros, banca, doca, muro_a, muro_b, arma_espada, arma_machado, arma_martelo, arma_lanca, arma_adaga, arma_foice, arma_arco, arma_manopla, arma_tomo, arma_cajado };
export const EXTRA_META = Object.fromEntries(MANIFEST.assets.map((a) => [a.id, a]));
