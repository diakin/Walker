"""Убрать два бачка на левом боковом блоке корпуса K7 (WALKER, 28.11.2026).

python3 walker/tools/k7_side_canisters.py <upper.glb> [out.glb]

В исходной модели сзади слева на верху бокового блока (узел camo_dark) стояли два бачка:
светлый корпус panel_light 0.3 x 0.42 x 0.3, две оранжевые полосы и тёмная крышка, всего
8 узлов без имени (x = -0.92, z = -1.0 и -0.66, выше y = 0.2). По просьбе они убраны.
Узлы ищутся по месту и материалу; повторный запуск ничего не находит.
"""
import sys
import k7_hydraulics as glb

CANISTER_MATERIALS = {'panel_light', 'orange', 'metal_dark'}
CANISTER_X = -0.92
CANISTER_Z = (-1.0, -0.66)


def is_canister(node, materials, document):
    if 'mesh' not in node:
        return False
    position = node.get('translation', [0, 0, 0])
    if abs(position[0] - CANISTER_X) > 0.01 or position[1] < 0.2:
        return False
    if not any(abs(position[2] - depth) < 0.01 for depth in CANISTER_Z):
        return False
    primitives = document['meshes'][node['mesh']]['primitives']
    return all(materials[primitive['material']] in CANISTER_MATERIALS for primitive in primitives)


def main():
    source = sys.argv[1]
    target = sys.argv[2] if len(sys.argv) > 2 else source
    document, binary = glb.read_glb(source)
    materials = [material['name'] for material in document['materials']]
    found = [index for index, node in enumerate(document['nodes']) if is_canister(node, materials, document)]
    if not found:
        raise SystemExit('no side canisters (already removed?)')
    # узлы остаются пустыми (без сетки): номера узлов в анимациях не меняются
    for index in found:
        node = document['nodes'][index]
        node.pop('mesh')
        node['name'] = 'removed_side_canister'
    # сетки без ссылок убрать: их читают запекание и проход материалов (как в k7_side_lamp.py)
    used = sorted({node['mesh'] for node in document['nodes'] if 'mesh' in node})
    remap = {old: new for new, old in enumerate(used)}
    document['meshes'] = [document['meshes'][index] for index in used]
    for node in document['nodes']:
        if 'mesh' in node:
            node['mesh'] = remap[node['mesh']]
    glb.write_glb(target, document, binary)
    print(f'{target}: side canister nodes removed: {found}')


if __name__ == '__main__':
    main()
