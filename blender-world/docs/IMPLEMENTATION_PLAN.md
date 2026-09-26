# Mundo procedural no Blender — plano de implementação

Fonte de autoridade: plano aprovado pelo usuário em 24 de setembro de 2026.

## Restrições globais

- Não modificar `demo/`.
- Entregar `MUNDO_MESTRE.blend` com as cenas `ATLAS_MUNDO`, `REGIAO_COMERCIAL`, `TURBULENTA_01`, `BIBLIOTECA_LOCAL` e `VALIDACAO`.
- Usar Blender 5.0.1 do Kali e seeds `26092401`, `26092402`, `26092403`.
- Tratar reinos, fronteiras e identidades como `PROPOSTA`.
- Manter fontes procedurais separadas do acabamento manual.

## Etapas

1. Registrar hashes da demo, simular e instalar Blender, validar background/render/GLB.
2. Implementar e testar o núcleo determinístico de configuração, terreno, hidrologia, biomas e rotas.
3. Construir o atlas continental e a camada política proposta.
4. Construir a região comercial de 1.024 x 1.024 m em 16 blocos.
5. Construir a Turbulenta de 256 x 256 m e a biblioteca de ativos em detalhe médio.
6. Gerar o arquivo mestre, exports, mapas, manifesto, renders e relatório.
7. Reexecutar testes, validar determinismo e comparar hashes da demo.

