"""Поручни на бёдрах K7 ближе к середине и закреплены на бедре (WALKER, 28.11.2026).

python3 walker/tools/k7_handrail.py <lower.glb> [out.glb]

В исходной модели перед каждым бедром стоит П-образный поручень: две боковые трубы
(металл metal_gray, радиус 0.03) и перемычка внизу спереди. Боковые трубы шли по x = ±0.25 в
координатах бедра, а бедро шириной ±0.17: концы труб, загнутые назад к бедру (z = 0.15),
висели в воздухе сбоку от него, верхний конец к тому же выше верха бедра (y = 0.05 при
верхе 0.03).

Скрипт:
  - переносит боковые трубы на x = ±HANDRAIL_X (внутри плоской передней грани бедра) и
    сжимает их по высоте так, чтобы оба конца входили в переднюю грань (сверху ниже фаски,
    снизу выше нижней фаски); сечение трубы сохраняется: сдвигается ось, вершины - от оси
    по нормали;
  - укорачивает перемычку до новых труб и поднимает её с нижними концами;
  - переносит голубой фонарь на внешней трубе вместе с ней;
  - добавляет к каждому концу трубы опорную пластину на передней грани бедра (metal_dark).
Повторный запуск ничего не находит: трубы ищутся по старому месту (x = ±0.25).
"""
import sys
import numpy as np
import k7_hydraulics as glb

OLD_X = 0.25
HANDRAIL_X = 0.10
PIPE_RADIUS = 0.03
OLD_LOW, OLD_TOP = -0.861, 0.069        # ось трубы: нижний и верхний участки
NEW_LOW, NEW_TOP = -0.80, -0.10
THIGH_FRONT = 0.25                      # передняя грань бедра (z)
PLATE_HALF = 0.04
PLATE_DEPTH = (THIGH_FRONT - 0.015, THIGH_FRONT + 0.02)


def remap_height(height):
    return NEW_LOW + (height - OLD_LOW) * (NEW_TOP - NEW_LOW) / (OLD_TOP - OLD_LOW)


def primitive_arrays(document, binary, primitive):
    attributes = primitive['attributes']
    return (glb.accessor(document, binary, attributes['POSITION']).astype(float),
            glb.accessor(document, binary, attributes['NORMAL']).astype(float))


def set_positions(document, binary, primitive, positions):
    primitive['attributes']['POSITION'] = glb.add_accessor(document, binary, np.array(positions, np.float32), 'VEC3', 34962)


def main():
    source = sys.argv[1]
    target = sys.argv[2] if len(sys.argv) > 2 else source
    document, binary = glb.read_glb(source)
    materials = [material['name'] for material in document['materials']]
    material_index = {name: index for index, name in enumerate(materials)}
    names = [node.get('name', '') for node in document['nodes']]
    hips = [names.index('hip_L'), names.index('hip_R')]
    report = []
    for hip in hips:
        side_pipes, crossbars, lamps = [], [], []
        for child in document['nodes'][hip].get('children', []):
            node = document['nodes'][child]
            if 'mesh' not in node:
                continue
            primitives = document['meshes'][node['mesh']]['primitives']
            kinds = {materials[primitive['material']] for primitive in primitives}
            position = node.get('translation', [0, 0, 0])
            if kinds == {'metal_gray'} and 'translation' not in node:
                points, _ = primitive_arrays(document, binary, primitives[0])
                width = points[:, 0].max() - points[:, 0].min()
                centre_x = (points[:, 0].max() + points[:, 0].min()) / 2
                if width < 0.08 and abs(abs(centre_x) - OLD_X) < 0.01:
                    side_pipes.append((child, 1.0 if centre_x > 0 else -1.0))
                elif abs(points[:, 0].max() - OLD_X) < 0.01 and abs(points[:, 0].min() + OLD_X) < 0.01:
                    crossbars.append(child)
            elif kinds <= {'light_cyan', 'metal_dark'} and abs(abs(position[0]) - OLD_X) < 0.03 \
                    and abs(position[2] - 0.52) < 0.01:
                lamps.append(child)
        if len(side_pipes) != 2 or len(crossbars) != 1:
            raise SystemExit(f'hip {names[hip]}: handrail not found at x = ±{OLD_X} (already moved?)')

        plate_part = glb.Part()
        for child, sign in side_pipes:
            primitive = document['meshes'][document['nodes'][child]['mesh']]['primitives'][0]
            points, normals = primitive_arrays(document, binary, primitive)
            axis = points - normals * PIPE_RADIUS
            axis[:, 0] += sign * (HANDRAIL_X - OLD_X)
            axis[:, 1] = remap_height(axis[:, 1])
            set_positions(document, binary, primitive, axis + normals * PIPE_RADIUS)
            # концы трубы - самые задние точки оси; на каждом пластина на грани бедра
            for end_height in (NEW_TOP, NEW_LOW):
                near = axis[np.abs(axis[:, 1] - end_height) < 0.06]
                end = near[np.argmin(near[:, 2])]
                glb.box(plate_part, (end[0] - PLATE_HALF, end[1] - PLATE_HALF, PLATE_DEPTH[0]),
                        (end[0] + PLATE_HALF, end[1] + PLATE_HALF, PLATE_DEPTH[1]))
        primitive = document['meshes'][document['nodes'][crossbars[0]]['mesh']]['primitives'][0]
        points, _ = primitive_arrays(document, binary, primitive)
        points[:, 0] *= HANDRAIL_X / OLD_X
        points[:, 1] += remap_height(OLD_LOW) - OLD_LOW
        set_positions(document, binary, primitive, points)
        for child in lamps:
            node = document['nodes'][child]
            sign = 1.0 if node['translation'][0] > 0 else -1.0
            node['translation'][0] += sign * (HANDRAIL_X - OLD_X)
            node['translation'][1] = float(remap_height(node['translation'][1]))
        # пластины - новая деталь бедра
        document['meshes'].append({'name': 'handrail_plates', 'primitives': [{
            'attributes': {
                'POSITION': glb.add_accessor(document, binary, np.array(plate_part.positions, np.float32), 'VEC3', 34962),
                'NORMAL': glb.add_accessor(document, binary, np.array(plate_part.normals, np.float32), 'VEC3', 34962),
                'TEXCOORD_0': glb.add_accessor(document, binary, np.array(plate_part.uvs, np.float32), 'VEC2', 34962)},
            'indices': glb.add_accessor(document, binary, np.array(plate_part.indices, np.uint16), 'SCALAR', 34963),
            'material': material_index['metal_dark']}]})
        document['nodes'].append({'name': f'handrail_plates_{names[hip][-1]}', 'mesh': len(document['meshes']) - 1})
        document['nodes'][hip]['children'].append(len(document['nodes']) - 1)
        report.append(f'{names[hip]}: pipes {[child for child, _ in side_pipes]}, crossbar {crossbars}, lamps {lamps}')
    glb.write_glb(target, document, binary)
    print(f'{target}: ' + '; '.join(report))


if __name__ == '__main__':
    main()
