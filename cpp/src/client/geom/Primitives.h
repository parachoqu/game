#pragma once
// Primitivas do cenário grey-box e dos marcadores: caixa, cilindro, cone, esfera, cápsula, anel.
// Todas brancas (a cor vem da instância) e com normais por face ou suaves, conforme a forma.
#include "client/geom/MeshData.h"

namespace rpg::client::prim {

MeshData box();                              // cubo unitário centrado na origem
MeshData cylinder(int segments = 16);        // raio 1, y de 0 a 1, com tampas
MeshData cone(int segments = 16);            // base raio 1 em y = 0, ponta em y = 1
MeshData sphere(int rings = 10, int segments = 16);  // raio 1
MeshData capsule(double radius, double height, int rings = 6, int segments = 14);  // pés em y = 0
MeshData ring(double inner, double outer, int segments = 32);  // no plano XZ, normal +Y

}  // namespace rpg::client::prim
