"""Гидроцилиндр колена K7: соединяет бедро и голень (WALKER, 28.11.2026).

python3 walker/tools/k7_knee_actuator.py <lower.glb> [out.glb]

По эскизу: на бедре сзади утолщение - обтекатель (камуфляж бедра, сходит в бедро клином), в нём верхний палец
и верх тёмно-синего корпуса (там подразумевается подвод масла); корпус выходит из низа
короба, полированный шток (материал chrome: класс polished в bake_walker.py, узкий яркий
блик) идёт к проушине на голени сзади под коленом. Корпус - узел бедра, шток - узел голени;
в каждом ключе анимации корпус поворачивается к проушине, шток - к пальцу в коробе.

До этого (тот же день) один цилиндр голеностопа шёл от короба на голени к пятке, раньше -
два тонких по углам голени (k7_hydraulics.py); их узлы (hydraulic_*, ankle_actuator_*,
knee_actuator_*) и анимация удаляются, повторный запуск ставит цилиндр заново.

Где пальцы (перебор по анимациям): колено K7 смотрит вперёд, сзади его угол закрывается
при сгибе (в живых анимациях 45-120 градусов), и цилиндр между серединой бедра и голенью
сжимался бы почти до нуля. Поэтому верхний палец высоко у тазобедренного шарнира и далеко
за бедром, нижний - под коленом: расстояние меняется от 0.58 до 1.01. Проушина голени
ниже места, через которое при сгибе проходит низ бедра (до -0.31 при беге). Короб не
достаёт до низа корпуса робота (зазор 0.12 при махе ноги). Без приседа цилиндр ничего не
касается; в приседе шток заходит в низ бедра до 0.55 единицы Quake, в гибели больше.

Длины по живым анимациям: шток при самом длинном расстоянии в корпусе хотя бы на
ROD_OVERLAP, при самом коротком его верх остаётся в корпусе выше пальца (в коробе); в
анимациях гибели шток остаётся в корпусе хотя бы на ROD_OVERLAP_DEATH.
"""
import sys, math
import numpy as np
import k7_hydraulics as base
import rod_shader

UPPER_PIN = (-0.05, -0.55)          # (y, z) в пространстве бедра: в коробе у тазобедренного шарнира
LOWER_PIN = (-0.40, -0.25)          # (y, z) в пространстве колена (голени): сзади под коленом
# обтекатель на бедре (28.11.2026, вместо коробки - смотрелась инородно): профиль (y, z) в
# пространстве бедра, вытянут по x на +-FAIRING_HALF_WIDTH. Сверху продолжает верх бедра,
# сзади срезанные углы, низ скошен вперёд-вниз и сходит на заднюю грань бедра - обтекатель
# сужается в бедро клином. Материал - камуфляж бедра: запекание кладёт на него те же швы и
# заклёпки. Внутри - верх корпуса цилиндра при любом наклоне (до 34 градусов назад)
FAIRING = [(0.03, -0.23), (0.08, -0.40), (0.09, -0.60), (0.02, -0.73), (-0.20, -0.73),
           (-0.27, -0.64), (-0.31, -0.45), (-0.52, -0.23)]
FAIRING_HALF_WIDTH = 0.13           # бедро +-0.17
FAIRING_BOTTOM = -0.28              # низ обтекателя под пальцем (y): отсюда виден корпус
SHIN_REAR = -0.18                   # задняя грань голени (z в пространстве колена)
BARREL_RADIUS = 0.068
BARREL_ABOVE = 0.11                 # корпус выше пальца (в коробе)
BARREL_SHARE = 0.45                 # доля корпуса в видимой части при обычном расстоянии
ROD_RADIUS = 0.042
ROD_OVERLAP = 0.13                  # в живых анимациях
ROD_OVERLAP_DEATH = 0.05
REMOVE_PREFIXES = ('hydraulic_', 'ankle_actuator_', 'knee_actuator_')


def strip(document):
    """убрать узлы с REMOVE_PREFIXES, их сетки и каналы анимации"""
    nodes = document['nodes']
    removed = {index for index, node in enumerate(nodes) if node.get('name', '').startswith(REMOVE_PREFIXES)}
    keep = [index for index in range(len(nodes)) if index not in removed]
    remap = {old: new for new, old in enumerate(keep)}
    new_nodes = [nodes[index] for index in keep]
    for node in new_nodes:
        if 'children' in node:
            node['children'] = [remap[child] for child in node['children'] if child in remap]
    document['nodes'] = new_nodes
    for scene in document['scenes']:
        scene['nodes'] = [remap[index] for index in scene['nodes'] if index in remap]
    for animation in document['animations']:
        channels, samplers, sampler_remap = [], [], {}
        for channel in animation['channels']:
            if channel['target']['node'] in removed:
                continue
            old = channel['sampler']
            if old not in sampler_remap:
                sampler_remap[old] = len(samplers)
                samplers.append(animation['samplers'][old])
            channels.append({'sampler': sampler_remap[old],
                             'target': {'node': remap[channel['target']['node']], 'path': channel['target']['path']}})
        animation['channels'], animation['samplers'] = channels, samplers
    used = sorted({node['mesh'] for node in new_nodes if 'mesh' in node})
    mesh_remap = {old: new for new, old in enumerate(used)}
    document['meshes'] = [document['meshes'][index] for index in used]
    for node in new_nodes:
        if 'mesh' in node:
            node['mesh'] = mesh_remap[node['mesh']]
    return len(removed)


def blue_material(document):
    """тёмно-синий материал корпуса (текстура hydraulic_blue.jpg рядом с моделью)"""
    names = [material['name'] for material in document['materials']]
    if 'hydraulic_blue' in names:
        return names.index('hydraulic_blue')
    document['images'].append({'uri': 'hydraulic_blue.jpg', 'mimeType': 'image/jpeg'})
    document['textures'].append({'source': len(document['images']) - 1})
    document['materials'].append({'name': 'hydraulic_blue', 'pbrMetallicRoughness': {
        'baseColorFactor': [1, 1, 1, 1], 'metallicFactor': 0.35, 'roughnessFactor': 0.45,
        'baseColorTexture': {'index': len(document['textures']) - 1}}})
    return len(document['materials']) - 1


def rotate_x(angle, vector):
    y_value, z_value = vector
    return np.array([y_value * math.cos(angle) - z_value * math.sin(angle), y_value * math.sin(angle) + z_value * math.cos(angle)])


def knee_angles(knee_rotation, knee_translation):
    """поворот корпуса (ось -Y на проушину голени), штока (ось +Y на палец в коробе) и
    расстояние между ними; колено поворачивается только вокруг X"""
    qx, qy, qz, qw = knee_rotation
    if abs(qy) > 1e-5 or abs(qz) > 1e-5:
        raise SystemExit('knee rotation not about X')
    angle = 2 * math.atan2(qx, qw)
    offset = np.array([knee_translation[1], knee_translation[2]])
    lower_in_thigh = offset + rotate_x(angle, LOWER_PIN)
    down = lower_in_thigh - np.array(UPPER_PIN)
    upper_in_shin = rotate_x(-angle, np.array(UPPER_PIN) - offset)
    up = upper_in_shin - np.array(LOWER_PIN)
    return math.atan2(-down[1], -down[0]), math.atan2(up[1], up[0]), float(np.linalg.norm(down))


def fairing(part, profile, half_width):
    """призма: профиль (y, z) по кругу, вытянутый по x; бока - веером от середины профиля"""
    profile = [np.array(point, float) for point in profile]
    centre = sum(profile) / len(profile)
    low_y, high_y = min(p[0] for p in profile), max(p[0] for p in profile)
    low_z, high_z = min(p[1] for p in profile), max(p[1] for p in profile)

    def uv(point):
        return ((point[1] - low_z) / (high_z - low_z), (point[0] - low_y) / (high_y - low_y))
    for side in (-1, 1):
        x_value = side * half_width
        positions = [(x_value, centre[0], centre[1])] + [(x_value, p[0], p[1]) for p in profile]
        normals = [(side, 0, 0)] * len(positions)
        uvs = [uv(centre)] + [uv(p) for p in profile]
        faces = [(0, 1 + index, 1 + (index + 1) % len(profile)) for index in range(len(profile))]
        part.add(positions, normals, uvs, faces)
    for index in range(len(profile)):
        first, second = profile[index], profile[(index + 1) % len(profile)]
        edge = second - first
        normal = np.array([edge[1], -edge[0]]); normal /= np.linalg.norm(normal)
        if np.dot(normal, (first + second) / 2 - centre) < 0:
            normal = -normal
        length = np.linalg.norm(edge)
        positions = [(-half_width, first[0], first[1]), (half_width, first[0], first[1]),
                     (half_width, second[0], second[1]), (-half_width, second[0], second[1])]
        normals = [(0, normal[0], normal[1])] * 4
        uvs = [(0, 0), (1, 0), (1, length), (0, length)]
        part.add(positions, normals, uvs, [(0, 1, 2), (0, 2, 3)])


def build_parts(barrel_below, rod_length):
    parts = {name: {} for name in ('housing', 'barrel', 'rod', 'lower_mount')}

    def get(node, material):
        return parts[node].setdefault(material, base.Part())
    # обтекатель на бедре (в пространстве бедра, узел в начале координат бедра)
    fairing(get('housing', 'camo_green'), FAIRING, FAIRING_HALF_WIDTH)
    # корпус: ось -Y от пальца; верх и палец в коробе, у выхода штока тёмная букса
    base.cylinder(get('barrel', 'hydraulic_blue'), 'y', (0, 0, 0), BARREL_RADIUS, BARREL_ABOVE, -(barrel_below - 0.03), (True, False))
    base.cylinder(get('barrel', 'metal_dark'), 'y', (0, 0, 0), BARREL_RADIUS + 0.006, -(barrel_below - 0.03), -barrel_below)
    base.cylinder(get('barrel', 'metal_dark'), 'x', (0, 0, 0), 0.03, -0.09, 0.09)     # палец (в коробе)
    # шток: проушина, наконечник, полированный шток (rod_shader.ROD_MIN_SIDES = 24 грани: блик посередине считается в
    # вершинах, на 10 гранях полоса блика ложилась бы ломаной)
    base.cylinder(get('rod', 'metal_dark'), 'x', (0, 0, 0), 0.05, -0.03, 0.03)
    base.cylinder(get('rod', 'metal_dark'), 'y', (0, 0, 0), 0.05, 0.035, 0.11, (False, True))
    segments, base.SEGMENTS = base.SEGMENTS, rod_shader.ROD_MIN_SIDES
    base.cylinder(get('rod', 'chrome'), 'y', (0, 0, 0), ROD_RADIUS, 0.11, rod_length, (False, True))
    base.SEGMENTS = segments
    # проушина на задней грани голени: подошва (заходит в грань), две щеки, палец
    face = SHIN_REAR - LOWER_PIN[1]                 # грань голени от пальца по z (+)
    base.box(get('lower_mount', 'metal_dark'), (-0.07, -0.07, face - 0.01), (0.07, 0.07, face + 0.02))
    for sign in (-1, 1):
        low, high = sorted((sign * 0.034, sign * 0.048))
        base.box(get('lower_mount', 'metal_dark'), (low, -0.055, -0.055), (high, 0.055, face))
    base.cylinder(get('lower_mount', 'metal_gray'), 'x', (0, 0, 0), 0.018, -0.052, 0.052)
    return parts


def main():
    source = sys.argv[1]
    target = sys.argv[2] if len(sys.argv) > 2 else source
    document, binary = base.read_glb(source)
    removed = strip(document)
    names = [node.get('name', '') for node in document['nodes']]
    hips = {'L': names.index('hip_L'), 'R': names.index('hip_R')}
    knees = {'L': names.index('knee_L'), 'R': names.index('knee_R')}

    lengths, live_lengths = [], []
    for animation in document['animations']:
        channels = {(channel['target']['node'], channel['target']['path']): channel for channel in animation['channels']}
        for side in 'LR':
            rotation = base.accessor(document, binary, animation['samplers'][channels[(knees[side], 'rotation')]['sampler']]['output'])
            translation = base.accessor(document, binary, animation['samplers'][channels[(knees[side], 'translation')]['sampler']]['output'])
            for key in range(len(rotation)):
                length = knee_angles(rotation[key], translation[min(key, len(translation) - 1)])[2]
                lengths.append(length)
                if not animation['name'].startswith('BOTH_'):
                    live_lengths.append(length)
    # шток по живым анимациям; в анимациях гибели нога разгибается сильнее, шток выдвигается
    # дальше, но остаётся в корпусе хотя бы на ROD_OVERLAP_DEATH
    shortest, longest = min(lengths), max(live_lengths)
    hidden = UPPER_PIN[0] - FAIRING_BOTTOM                     # от пальца до низа обтекателя по Y
    typical = float(np.median(live_lengths))
    # не длиннее, чем позволяет самое короткое расстояние (корпус не доходит до проушины)
    barrel_below = min(hidden + BARREL_SHARE * (typical - hidden), min(live_lengths) - 0.09)
    rod_length = longest - barrel_below + ROD_OVERLAP
    top_above_pin = rod_length - shortest
    print(f'pin distance {shortest:.3f} .. {longest:.3f}, barrel below the pin {barrel_below:.3f}, '
          f'rod {rod_length:.3f}, rod top above the pin at the shortest {top_above_pin:.3f}')
    if top_above_pin > BARREL_ABOVE - 0.02:
        raise SystemExit('rod would come out of the top of the barrel')
    death_overlap = rod_length - (max(lengths) - barrel_below)
    print(f'rod in the barrel at the longest of all animations {death_overlap:.3f}')
    if death_overlap < ROD_OVERLAP_DEATH:
        raise SystemExit('rod would come out of the barrel in a death animation')

    material_index = {material['name']: index for index, material in enumerate(document['materials'])}
    material_index['hydraulic_blue'] = blue_material(document)
    parts = build_parts(barrel_below, rod_length)
    new_nodes, animated = [], []
    for side in 'LR':
        for part_name, parent, origin in (('housing', hips[side], (0.0, 0.0)), ('barrel', hips[side], UPPER_PIN),
                                          ('lower_mount', knees[side], LOWER_PIN), ('rod', knees[side], LOWER_PIN)):
            primitives = []
            for material, part in parts[part_name].items():
                primitives.append({
                    'attributes': {
                        'POSITION': base.add_accessor(document, binary, np.array(part.positions, np.float32), 'VEC3', 34962),
                        'NORMAL': base.add_accessor(document, binary, np.array(part.normals, np.float32), 'VEC3', 34962),
                        'TEXCOORD_0': base.add_accessor(document, binary, np.array(part.uvs, np.float32), 'VEC2', 34962)},
                    'indices': base.add_accessor(document, binary, np.array(part.indices, np.uint16), 'SCALAR', 34963),
                    'material': material_index[material]})
            document['meshes'].append({'name': f'knee_actuator_{part_name}', 'primitives': primitives})
            node = {'name': f'knee_actuator_{part_name}_{side}', 'mesh': len(document['meshes']) - 1,
                    'translation': [0.0, origin[0], origin[1]]}
            rest = document['nodes'][knees[side]]
            barrel_angle, rod_angle, _ = knee_angles(rest.get('rotation', [0, 0, 0, 1]), rest.get('translation', [0, 0, 0]))
            if part_name in ('barrel', 'rod'):
                node['rotation'] = base.quat_x(barrel_angle if part_name == 'barrel' else rod_angle)
            document['nodes'].append(node)
            document['nodes'][parent].setdefault('children', []).append(len(document['nodes']) - 1)
            if part_name in ('barrel', 'rod'):
                animated.append((side, len(document['nodes']) - 1, part_name))

    for animation in document['animations']:
        channels = {(channel['target']['node'], channel['target']['path']): channel for channel in animation['channels']}
        for side, node_index, part_name in animated:
            rotation_sampler = animation['samplers'][channels[(knees[side], 'rotation')]['sampler']]
            translation_sampler = animation['samplers'][channels[(knees[side], 'translation')]['sampler']]
            rotation = base.accessor(document, binary, rotation_sampler['output'])
            translation = base.accessor(document, binary, translation_sampler['output'])
            values = []
            for key in range(len(rotation)):
                barrel_angle, rod_angle, _ = knee_angles(rotation[key], translation[min(key, len(translation) - 1)])
                values.append(base.quat_x(barrel_angle if part_name == 'barrel' else rod_angle))
            output = base.add_accessor(document, binary, np.array(values, np.float32), 'VEC4')
            animation['samplers'].append({'input': rotation_sampler['input'], 'output': output,
                                          'interpolation': rotation_sampler.get('interpolation', 'LINEAR')})
            animation['channels'].append({'sampler': len(animation['samplers']) - 1,
                                          'target': {'node': node_index, 'path': 'rotation'}})
    base.write_glb(target, document, binary)
    print(f'{target}: {removed} old actuator nodes removed, {len(animated) * 2} nodes added')


if __name__ == '__main__':
    main()
