"""Черновая модель нового робота для пробы в игре (WALKER, 28.11.2026).

python3 walker/tools/make_mech_test.py <папка данных>

Та же компоновка, что в mech_blockout.py (ноги, таз, погон, корпус шириной с таз, узкая
угловатая кабина с одним окном, без оружия), но как модель игрока models/players/mech:
lower.glb (таз и ноги с обратной кинематикой, анимации ходьбы, бега, стойки, поворота,
гибели), upper.glb (корпус), head.glb (кабина), animation.cfg, .skin, шейдеры
scripts/walker_mech.shader.

Гидроцилиндр колена: верхний конец красного корпуса спрятан в коробе на бедре у таза, блестящий
шток на голени под коленом, оба поворачиваются в каждом кадре (add_actuators).

Вид - как на картинке черновой модели: каждая группа деталей одним светлым цветом, рёбра
граней тёмной линией того же оттенка. Линия - в текстуре: у четырёхугольной грани углы
текстуры совпадают с углами грани, поэтому рамка текстуры ложится точно по рёбрам; у
многоугольной грани (торцы призм) - веер от середины, и линия только по внешнему краю
(отдельный материал <группа>_cap). Освещение обычное (rgbGen lightingDiffuse).

В игре: cvar g_testModel mech - все роботы на сервере с этой моделью (характеристики
остаются от своего робота). Оружие ставится в tag_weapon1 (справа) и tag_weapon2 (слева)
по бокам корпуса.

Единицы: размеры черновой модели (как у K7, 1 = 14 единиц Quake, земля на 0) переводятся
в координаты Quake (X вперёд, Y влево, Z вверх, земля на Z = -24).
"""
import math, os, sys
import numpy as np
from PIL import Image, ImageDraw

import make_robots as robots
import mech_blockout as blockout
import rod_shader

SCALE = robots.ROBOT_SCALE                  # 14 единиц Quake в единице модели
NAME = 'mech'
GROUND_OFFSET = 1.714                       # земля черновой модели (0) - на 24 единицы ниже точки игрока

GROUP_COLOURS = blockout.GROUP_COLOURS
GROUP_COLOURS = dict(GROUP_COLOURS, window=(60, 90, 120), barrel=(215, 55, 35), rod=(150, 156, 168))

# шаг ног: ступня в опоре идёт назад со скоростью робота; короткой ноге нужен короткий шаг,
# поэтому кадров в секунду больше (длина шага = 110 x 0.55 x кадров / кадров в секунду)
STRIDE = 15.0                               # длина шага ходьбы, единиц Quake
LEGS_FPS = {'LEGS_WALKCR': None, 'LEGS_WALK': None, 'LEGS_RUN': None, 'LEGS_BACK': None, 'LEGS_SWIM': None}


def quake(x_left, y_up, z_forward):
    """точка черновой модели -> Quake (X вперёд, Y влево, Z вверх)"""
    return np.array([z_forward * SCALE, x_left * SCALE, (y_up - GROUND_OFFSET) * SCALE])


# ---------------------------------------------------------------- грани с рамкой

def polygon(part, group, points, centre):
    """плоская грань: points по кругу; нормаль - от центра тела centre"""
    points = [np.asarray(point, float) for point in points]
    normal = np.cross(points[1] - points[0], points[2] - points[0])
    if np.linalg.norm(normal) < 1e-9:
        return
    normal /= np.linalg.norm(normal)
    face_centre = sum(points) / len(points)
    if np.dot(normal, face_centre - centre) < 0:
        points = points[::-1]
        normal = -normal
    if len(points) == 4:
        part.add(group, points, [normal] * 4, [(0, 0), (1, 0), (1, 1), (0, 1)], [(0, 1, 2), (0, 2, 3)])
        return
    if len(points) == 3:
        part.add(f'{group}_cap', [face_centre] + points, [normal] * 4,
                 [(0.5, 0), (0, 1), (1, 1), (0.5, 1)], [(0, 1, 2), (0, 2, 3), (0, 3, 1)])
        return
    positions, uvs, faces = [face_centre], [(0.5, 0.0)], []
    for index, point in enumerate(points):
        following = points[(index + 1) % len(points)]
        base = len(positions)
        positions += [point, following]
        uvs += [(0, 1), (1, 1)]
        faces.append((0, base, base + 1))
    part.add(f'{group}_cap', positions, [normal] * len(positions), uvs, faces)


def solid(part, group, vertices, faces):
    vertices = [np.asarray(vertex, float) for vertex in vertices]
    centre = sum(vertices) / len(vertices)
    for face in faces:
        polygon(part, group, [vertices[index] for index in face], centre)


def cuboid(part, group, low, high):
    x0, y0, z0 = low; x1, y1, z1 = high
    vertices = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0), (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
    solid(part, group, vertices, [[0, 1, 2, 3], [4, 5, 6, 7], [0, 1, 5, 4], [3, 2, 6, 7], [0, 3, 7, 4], [1, 2, 6, 5]])


def prism(part, group, profile, y_low, y_high):
    """призма поперёк (вдоль Y Quake): профиль - многоугольник (x, z) по кругу"""
    count = len(profile)
    vertices = [(x, y_low, z) for x, z in profile] + [(x, y_high, z) for x, z in profile]
    faces = [list(range(count)), list(range(count, 2 * count))]
    faces += [[index, (index + 1) % count, count + (index + 1) % count, count + index] for index in range(count)]
    solid(part, group, vertices, faces)


def cylinder(part, group, start, end, radius, sides=10):
    start = np.asarray(start, float); end = np.asarray(end, float)
    axis = end - start; axis /= np.linalg.norm(axis)
    helper = np.array([0.0, 0.0, 1.0]) if abs(axis[2]) < 0.9 else np.array([1.0, 0.0, 0.0])
    side_u = np.cross(axis, helper); side_u /= np.linalg.norm(side_u)
    side_v = np.cross(axis, side_u)
    vertices = []
    for point in (start, end):
        for step in range(sides):
            angle = 2 * math.pi * step / sides
            vertices.append(point + radius * (math.cos(angle) * side_u + math.sin(angle) * side_v))
    faces = [list(range(sides)), list(range(sides, 2 * sides))]
    faces += [[step, (step + 1) % sides, sides + (step + 1) % sides, sides + step] for step in range(sides)]
    solid(part, group, vertices, faces)


def smooth_cylinder(part, group, z_low, z_high, radius, sides=16):
    """гладкий цилиндр вдоль Z узла (нормали по радиусу, без рамки текстуры): для шейдинга
    полированного штока по направлению к зрителю (alphaGen viewFacing)"""
    positions, normals, uvs, faces = [], [], [], []
    for step in range(sides + 1):
        angle = 2 * math.pi * step / sides
        direction = (math.cos(angle), math.sin(angle), 0.0)
        for z_value in (z_low, z_high):
            positions.append((direction[0] * radius, direction[1] * radius, z_value))
            normals.append(direction)
            uvs.append((0.5, 0.5))
    for step in range(sides):
        first = step * 2
        faces += [(first, first + 2, first + 1), (first + 1, first + 2, first + 3)]
    part.add(group, positions, normals, uvs, faces)


def beam(part, group, start, end, width, depth):
    """брус от start до end, сечение width (поперёк, по Y) x depth"""
    start = np.asarray(start, float); end = np.asarray(end, float)
    axis = end - start; axis /= np.linalg.norm(axis)
    side_y = np.array([0.0, 1.0, 0.0]); side_y -= axis * (side_y @ axis); side_y /= np.linalg.norm(side_y)
    other = np.cross(axis, side_y)
    vertices = []
    for point in (start, end):
        for sign_side, sign_other in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            vertices.append(point + side_y * sign_side * width / 2 + other * sign_other * depth / 2)
    solid(part, group, vertices, [[0, 1, 2, 3], [4, 5, 6, 7], [0, 1, 5, 4], [1, 2, 6, 5], [2, 3, 7, 6], [3, 0, 4, 7]])


# ---------------------------------------------------------------- модель

def leg_spec():
    """ноги в координатах Quake: таз и узлы бедра/колена/щиколотки (как в make_robots.Legs)"""
    unit = SCALE
    thigh = blockout.THIGH_LENGTH * unit
    shin = blockout.SHIN_LENGTH * unit
    hip_z = (blockout.HIP_Y - GROUND_OFFSET) * unit
    ankle_height = blockout.ANKLE[0] * unit

    def thigh_part(sign):
        part = robots.Part()
        cylinder(part, 'legs', (0, -sign * 0.22 * unit, 0), (0, sign * 0.2 * unit, 0), 0.2 * unit)
        cuboid(part, 'legs', (-0.19 * unit, -0.17 * unit, -thigh), (0.19 * unit, 0.17 * unit, 0))
        # короб гидравлики сзади у тазобедренного шарнира, в нём верхний конец гидроцилиндра
        cuboid(part, 'legs', tuple(value * unit for value in HOUSING[0]), tuple(value * unit for value in HOUSING[1]))
        return part

    def shin_part(sign):
        part = robots.Part()
        cylinder(part, 'legs', (0, -0.2 * unit, 0), (0, 0.2 * unit, 0), 0.15 * unit)
        cuboid(part, 'legs', (-0.12 * unit, -0.13 * unit, -shin), (0.12 * unit, 0.13 * unit, 0))
        # бронеплита спереди голени
        cuboid(part, 'legs', (0.13 * unit, -0.21 * unit, -shin * 0.62), (0.21 * unit, 0.21 * unit, -0.06 * unit))
        # проушина штока сзади под коленом
        cuboid(part, 'legs', (-0.23 * unit, -0.04 * unit, -0.36 * unit), (-0.12 * unit, 0.04 * unit, -0.24 * unit))
        return part

    def foot_part(sign):
        part = robots.Part()
        cylinder(part, 'legs', (0, -0.14 * unit, 0), (0, 0.14 * unit, 0), 0.1 * unit)
        ankle_z = -ankle_height
        for toe in (-1, 1):
            toe_y = toe * 0.11 * unit
            profile = [(x * unit, ankle_z + z * unit) for x, z in
                       ((-0.02, 0.0), (0.52, 0.0), (0.55, 0.06), (0.3, 0.2), (-0.02, 0.24))]
            prism(part, 'legs', profile, toe_y - 0.08 * unit, toe_y + 0.08 * unit)
        profile = [(x * unit, ankle_z + z * unit) for x, z in ((-0.42, 0.0), (-0.02, 0.0), (-0.02, 0.22), (-0.3, 0.1))]
        prism(part, 'legs', profile, -0.1 * unit, 0.1 * unit)
        return part

    # таз: профиль черновой модели (вдоль, высота) относительно оси бёдер
    pelvis = robots.Part()
    drop = blockout.BODY_DROP
    profile = [(z * unit, (y - drop - blockout.HIP_Y) * unit) for z, y in
               ((-0.38, 1.82), (-0.22, 1.7), (0.24, 1.7), (0.42, 1.86), (0.42, 2.12), (-0.38, 2.12))]
    prism(pelvis, 'pelvis', profile, -0.44 * unit, 0.44 * unit)
    ring_low = (2.12 - drop - blockout.HIP_Y) * unit
    ring_high = (2.3 - drop - blockout.HIP_Y) * unit
    cylinder(pelvis, 'pelvis', (-0.05 * unit, 0, ring_low), (-0.05 * unit, 0, ring_high), 0.24 * unit, 12)
    # стопа стоит под щиколоткой: перенос ступни вперёд/назад задаёт походка
    return dict(hip_z=hip_z, hip_y=blockout.HIP_X * unit, torso_z=ring_high, thigh=thigh, shin=shin,
                ankle_height=ankle_height, knee_forward=True, lift=0.22 * unit, bob=0.04 * unit, sway=0.02,
                crouch=0.12 * unit, pelvis_part=pelvis, thigh_part=thigh_part, shin_part=shin_part, foot_part=foot_part)


def upper_body():
    """корпус от погона (tag_torso), кабина - голова на tag_head"""
    unit = SCALE
    half = blockout.TORSO_HALF
    root = robots.Node('upper_root')
    body = robots.Part()
    base = 2.3                                  # низ корпуса в черновой модели (до опускания)
    def local(x_left, y_up, z_forward):
        return (z_forward * unit, x_left * unit, (y_up - base) * unit)
    cuboid(body, 'torso', local(-half, 2.3, -0.7), local(half, 3.02, 0.32))
    cuboid(body, 'torso', local(-half + 0.04, 2.42, -1.02), local(half - 0.04, 2.96, -0.7))
    cuboid(body, 'torso', local(-half + 0.1, 2.5, -1.06), local(half - 0.1, 2.88, -1.02))
    for side in (-1, 1):
        cylinder(body, 'torso', local(side * (half - 0.1), 3.02, -0.55), local(side * (half - 0.1), 3.1, -0.55), 0.05 * unit, 8)
    cylinder(body, 'torso', local(-(half - 0.08), 3.02, -0.3), local(-(half - 0.08), 3.45, -0.3), 0.015 * unit, 6)
    torso = robots.Node('torso', (0, 0, 0), body, root)
    head_point = local(0, 2.3, 0.32)
    robots.Node('tag_head', head_point, None, torso)
    # слоты оружия по бокам корпуса: 1 справа (Y < 0), 2 слева
    robots.Node('tag_weapon1', local(-(half + 0.02), 2.75, -0.1), None, torso)
    robots.Node('tag_weapon2', local(half + 0.02, 2.75, -0.1), None, torso)
    robots.Node('tag_weapon', local(-(half + 0.02), 2.75, -0.1), None, torso)
    robots.Node('tag_torso', (0, 0, 0), None, root)

    head_root = robots.Node('head_root')
    cabin = robots.Part()
    def head_local(x_left, y_up, z_forward):
        return np.array([(z_forward - 0.32) * unit, x_left * unit, (y_up - base) * unit])
    profile = [((z - 0.32) * unit, (y - base) * unit) for z, y in
               ((0.32, 2.3), (0.82, 2.3), (1.0, 2.4), (1.0, 2.58), (0.56, 2.96), (0.32, 2.96))]
    prism(cabin, 'cockpit', profile, -blockout.COCKPIT_HALF * unit, blockout.COCKPIT_HALF * unit)
    # окно на скосе - чуть над поверхностью
    slope_low = np.array([1.0, 2.58]); slope_high = np.array([0.56, 2.96])
    normal = np.array([slope_high[1] - slope_low[1], -(slope_high[0] - slope_low[0])]); normal /= np.linalg.norm(normal)
    def on_slope(share, lift):
        point = slope_low + (slope_high - slope_low) * share + normal * lift
        return point[1], point[0]
    y_low, z_low = on_slope(0.15, 0.01); y_high, z_high = on_slope(0.8, 0.01)
    low_half, high_half = blockout.COCKPIT_HALF - 0.05, blockout.COCKPIT_HALF - 0.09
    window = [head_local(-low_half, y_low, z_low), head_local(low_half, y_low, z_low),
              head_local(high_half, y_high, z_high), head_local(-high_half, y_high, z_high)]
    centre = head_local(0, 2.6, 0.6)            # внутри кабины: окно смотрит наружу
    polygon(cabin, 'window', window, centre)
    robots.Node('head', (0, 0, 0), cabin, head_root)
    robots.Node('tag_head', (0, 0, 0), None, head_root)
    return root, torso, head_root


# ---------------------------------------------------------------- гидроцилиндры

# гидроцилиндр колена (28.11.2026): верхний конец красного корпуса спрятан в коробе на задней
# стороне бедра у тазобедренного шарнира (там подразумевается подвод масла, как у Казака),
# хромированный шток - на проушине голени под коленом. Корпус - узел бедра и в каждом кадре
# смотрит на проушину голени, шток - узел голени и смотрит на корпус. Положения проушин
# подобраны перебором по анимациям: наименьший разброс расстояния между ними и зазор до бедра,
# колена и голени во всех позах, кроме смертей
UPPER_PIN = np.array([-0.4, 0.0, 0.1])          # в системе бедра, доли единицы модели
LOWER_PIN = np.array([-0.18, 0.0, -0.3])        # в системе голени
HOUSING = ((-0.52, -0.13, -0.06), (-0.17, 0.13, 0.24))   # короб на бедре: углы (x, y, z)
DEATH_FRAMES = 15                               # кадров в каждой анимации смерти (они записываются первыми)


def quat_rotate(quat, vector):
    w_part, x_part, y_part, z_part = quat
    axis = np.array([x_part, y_part, z_part])
    vector = np.asarray(vector, float)
    return vector + 2 * np.cross(axis, np.cross(axis, vector) + w_part * vector)


def quat_from_to(start, end):
    start = np.asarray(start, float) / np.linalg.norm(start)
    end = np.asarray(end, float) / np.linalg.norm(end)
    axis = np.cross(start, end)
    cosine = float(np.dot(start, end))
    if cosine < -0.9999:
        return np.array([0.0, 0.0, 1.0, 0.0])
    quat = np.array([1 + cosine, axis[0], axis[1], axis[2]])
    return quat / np.linalg.norm(quat)


def add_actuators(legs):
    """узлы корпуса (на бедре) и штока (на голени) у каждой ноги; повороты - в каждой позе ног"""
    upper = UPPER_PIN * SCALE
    lower = LOWER_PIN * SCALE
    actuators, lengths = [], []
    for hip, knee, ankle, sign in legs.legs:
        side = 'l' if sign > 0 else 'r'
        barrel = robots.Node(f'barrel_{side}', upper, None, hip)
        rod = robots.Node(f'rod_{side}', lower, None, knee)
        actuators.append((knee, barrel, rod))
    original_pose = legs.pose

    def conjugate(quat):
        return np.array([quat[0], -quat[1], -quat[2], -quat[3]])

    def pose(*arguments, **keywords):
        keys = original_pose(*arguments, **keywords)
        for knee, barrel, rod in actuators:
            knee_translation, knee_rotation = keys[id(knee)]
            lower_in_thigh = knee_translation + quat_rotate(knee_rotation, lower)
            keys[id(barrel)] = (upper, quat_from_to((0, 0, -1), lower_in_thigh - upper))
            upper_in_shin = quat_rotate(conjugate(knee_rotation), upper - knee_translation)
            keys[id(rod)] = (lower, quat_from_to((0, 0, 1), upper_in_shin - lower))
            lengths.append(float(np.linalg.norm(lower_in_thigh - upper)))
        return keys

    legs.pose = pose
    return actuators, lengths


def actuator_parts(actuators, lengths):
    """видимая часть - от низа короба до проушины голени; корпус занимает 45% её при типичном
    расстоянии, остальное - шток. Шток на 0.3 длиннее хода, чтобы не выпадал при полном разгибе;
    при сильном сгибе его конец уходит выше оси - корпус продолжается вверх внутри короба"""
    shortest, longest = min(lengths), max(lengths)
    # типичное расстояние - по живым позам (стойка, ходьба), без смертей
    typical = float(np.median(lengths[len(robots.DEATHS) * DEATH_FRAMES * 2:]))
    hidden = (UPPER_PIN[2] - HOUSING[0][2]) * SCALE
    barrel_below = hidden + 0.45 * (typical - hidden)
    rod_length = longest - barrel_below + 0.3
    barrel_above = max(0.4, rod_length - shortest + 0.2)
    barrel_length = barrel_below + barrel_above
    for knee, barrel, rod in actuators:
        barrel.part = robots.Part()
        cylinder(barrel.part, 'barrel', (0, 0, barrel_above), (0, 0, -barrel_below), 0.075 * SCALE, 10)
        rod.part = robots.Part()
        smooth_cylinder(rod.part, 'rod', -0.4, rod_length, 0.038 * SCALE, rod_shader.ROD_MIN_SIDES)
    return shortest, longest, barrel_length, rod_length


# ---------------------------------------------------------------- анимация ног

def legs_animations(legs):
    """как make_robots.legs_animations, но с коротким шагом: кадров в секунду столько, чтобы
    ступня в опоре шла со скоростью робота"""
    spec = legs.spec
    ankle_z = robots.GROUND + spec['ankle_height']
    poses_of, fps_of = {}, {}

    def cycle(name, frames, stride, direction=1, crouch=0.0):
        fps = robots.STRIDE_SPEED * 0.55 * frames / stride
        fps_of[name] = int(round(fps))
        travel = robots.STRIDE_SPEED * (frames / fps_of[name]) * 0.55
        poses = []
        for frame in range(frames):
            phase = frame / frames
            bob = -spec['bob'] * (0.5 + 0.5 * math.cos(4 * math.pi * phase)) - crouch
            feet, toes = [], []
            for offset in (0.0, 0.5):
                x_value, z_value, toe = robots.gait(phase + offset, travel, spec['lift'], ankle_z)
                feet.append((direction * x_value, z_value))
                toes.append(toe * direction)
            poses.append(legs.pose((0, 0, bob), robots.roll(spec['sway'] * math.sin(2 * math.pi * phase)), feet, toes))
        poses_of[name] = poses

    def still(frames, crouch=0.0, breathe=0.0):
        return [legs.pose((0, 0, -crouch - breathe * (0.5 - 0.5 * math.cos(2 * math.pi * frame / frames))), robots.IDENTITY,
                          [(1.5, ankle_z), (-1.5, ankle_z)]) for frame in range(frames)]

    def death(frames, forward, side_tilt):
        poses = []
        for frame in range(frames):
            eased = 1 - (1 - min(1.0, frame / max(1, frames - 1))) ** 2
            drop = spec['hip_z'] - (ankle_z + spec['thigh'] * 0.45)
            rotation = robots.quat_mul(robots.pitch(forward * 0.5 * eased), robots.roll(side_tilt * 0.45 * eased))
            spread = 2 + 3 * eased
            poses.append(legs.pose((0, 0, -drop * eased), rotation, [(spread * 0.6, ankle_z), (-spread, ankle_z)]))
        return poses

    for index, (forward, tilt) in enumerate(((1, 0), (-1, 0), (0.3, 1))):
        poses_of[f'BOTH_DEATH{index + 1}'] = death(DEATH_FRAMES, forward, tilt)
        poses_of[f'BOTH_DEAD{index + 1}'] = poses_of[f'BOTH_DEATH{index + 1}'][-1:]
    cycle('LEGS_WALKCR', 15, STRIDE * 0.8, crouch=spec['crouch'])
    cycle('LEGS_WALK', 15, STRIDE)
    cycle('LEGS_RUN', 10, STRIDE * 1.1)
    cycle('LEGS_BACK', 15, STRIDE, direction=-1)
    cycle('LEGS_SWIM', 15, STRIDE)
    jump = [still(1, crouch=spec['crouch'] * (1 - frame / 7))[0] for frame in range(8)]
    land = [still(1, crouch=spec['crouch'] * math.sin(math.pi * frame / 5))[0] for frame in range(6)]
    poses_of['LEGS_JUMP'] = poses_of['LEGS_JUMPB'] = jump
    poses_of['LEGS_LAND'] = poses_of['LEGS_LANDB'] = land
    poses_of['LEGS_IDLE'] = still(15, breathe=0.3)
    poses_of['LEGS_IDLECR'] = still(15, crouch=spec['crouch'], breathe=0.2)
    turn = []
    for frame in range(8):
        feet = []
        for offset in (0.0, 0.5):
            local = (frame / 8 + offset) % 1.0
            feet.append((1.5 if offset == 0 else -1.5, ankle_z + spec['lift'] * 0.6 * max(0.0, math.sin(2 * math.pi * local))))
        turn.append(legs.pose((0, 0, 0), robots.IDENTITY, feet))
    poses_of['LEGS_TURN'] = turn
    order = [name for name, _ in robots.DEATHS] + [name for name, _, _ in robots.LEGS]
    for name in order:
        legs.record(name, poses_of[name])
    return [(name, len(poses_of[name])) for name in order], fps_of


def animation_cfg(fps_of):
    lines = robots.animation_cfg(NAME).splitlines()
    out = []
    for line in lines:
        for name, fps in fps_of.items():
            if line.endswith(f'// {name}'):
                fields = line.split('\t')
                fields[3] = str(fps)
                line = '\t'.join(fields)
        out.append(line)
    return '\n'.join(out) + '\n'


# ---------------------------------------------------------------- материалы

SATURATED = {'barrel'}           # корпус гидроцилиндра - ярко-красный, как на образце
SHINY = {'rod'}                  # шток блестит


def outline_texture(colour, cap=False, size=128, line=4, saturated=False):
    """светлая заливка и тёмная рамка (у торцевого веера - только по краю v = 1)"""
    fill = colour if saturated else tuple(int(255 - (255 - channel) * 0.25) for channel in colour)
    image = Image.new('RGB', (size, size), fill)
    draw = ImageDraw.Draw(image)
    edge = tuple(int(channel * (0.55 if saturated else 0.8)) for channel in colour)
    if cap:
        draw.rectangle((0, size - line, size - 1, size - 1), fill=edge)
    else:
        draw.rectangle((0, 0, size - 1, size - 1), outline=edge, width=line)
    return image


def write_materials(folder, part_materials):
    shaders = ['// WALKER: blockout robot "mech" for testing, generated by walker/tools/make_mech_test.py\n']
    written = set()
    for materials, _ in part_materials.values():
        for material in materials:
            if material in written:
                continue
            written.add(material)
            group = material[:-4] if material.endswith('_cap') else material
            outline_texture(GROUP_COLOURS[group], material.endswith('_cap'), saturated=group in SATURATED).save(
                os.path.join(folder, f'{material}.tga'))
            shader = f'models/players/{NAME}/{material}'
            stages = f'\t{{\n\t\tmap {shader}.tga\n\t\trgbGen lightingDiffuse\n\t}}\n'
            if group in SHINY:      # полированный шток: середина к зрителю яркая, к краям темнее (как у K7)
                stages += rod_shader.rod_stages()
            shaders.append(f'{shader}\n{{\n{stages}}}\n')
    # окраски команд и ботов (desert, urban - make_botfiles.py): у пробной модели все одинаковые
    for scheme in robots.SCHEMES + ('desert', 'urban'):
        for part, (materials, tags) in part_materials.items():
            lines = [f'{material},models/players/{NAME}/{material}' for material in materials] + [f'{tag},' for tag in tags]
            with open(os.path.join(folder, f'{part}_{scheme}.skin'), 'w') as handle:
                handle.write('\n'.join(lines) + '\n')
        robots.icon_image('M', GROUP_COLOURS['torso']).save(os.path.join(folder, f'icon_{scheme}.tga'))
    return shaders


def main():
    target = sys.argv[1]
    folder = os.path.join(target, 'models', 'players', NAME)
    os.makedirs(folder, exist_ok=True)
    spec = leg_spec()
    legs = robots.Legs(spec)
    actuators, lengths = add_actuators(legs)
    lower_animations, fps_of = legs_animations(legs)
    shortest, longest, barrel_length, rod_length = actuator_parts(actuators, lengths)
    print(f'knee cylinders: pins {shortest:.1f}..{longest:.1f} apart, barrel {barrel_length:.1f}, rod {rod_length:.1f}')
    lower_materials = robots.write_glb(os.path.join(folder, 'lower.glb'), legs.root, lower_animations, SCALE)
    upper_root, torso, head_root = upper_body()
    upper_animations = robots.torso_animations(upper_root, torso)
    upper_materials = robots.write_glb(os.path.join(folder, 'upper.glb'), upper_root, upper_animations, SCALE)
    head_materials = robots.write_glb(os.path.join(folder, 'head.glb'), head_root, [], SCALE)
    with open(os.path.join(folder, 'animation.cfg'), 'w') as handle:
        handle.write(animation_cfg(fps_of))
    upper_tags = [node.name for node in robots.collect(upper_root) if node.name.startswith('tag_')]
    shaders = write_materials(folder, {'lower': (lower_materials, ['tag_torso']), 'upper': (upper_materials, upper_tags),
                                       'head': (head_materials, ['tag_head'])})
    os.makedirs(os.path.join(target, 'scripts'), exist_ok=True)
    with open(os.path.join(target, 'scripts', 'walker_mech.shader'), 'w') as handle:
        handle.write('\n'.join(shaders))
    triangles = sum(node.part.triangles() for root in (legs.root, upper_root, head_root) for node in robots.collect(root) if node.part)
    print(f'{NAME}: {triangles} triangles, legs fps {fps_of}, hip {spec["hip_z"]:.1f}, thigh {spec["thigh"]:.1f}, shin {spec["shin"]:.1f}')


if __name__ == '__main__':
    main()
