"""Запекание текстур робота: уникальная развёртка и «боевой» вид.

python3 walker/tools/bake_walker.py <папка модели в данных игры>

Папка — models/players/<робот> (walker — K7, а также strizh и molot из make_robots.py);
единицы модели как у K7: 1 единица = 14 единиц Quake (asset.extras.q3_scale 14).
Окраски — те, для которых есть lower_<окраска>.skin.

В папке должны лежать lower.glb, upper.glb, head.glb, исходные текстуры материалов и
файлы .skin всех окрасок (их пишут make_botfiles.py и копия assets/baseq3/models/players/walker).

Что делается для каждой части (lower, upper, head):
  1. все примитивы собираются в одну сетку в позе покоя, xatlas строит уникальную развёртку;
  2. каждый тексель атласа получает точку на поверхности, нормаль, материал и исходные UV;
  3. цвет = текстура материала окраски, затем:
       - затенение в углах и стыках (ambient occlusion, лучи через embree);
       - потёртости краски на выпуклых рёбрах (до голого металла), светлый кант рёбер;
       - мелкие царапины, пыль сверху, потёки на вертикальных стенках, грязь снизу;
  4. атлас пишется в <часть>_<окраска>.jpg, в GLB подменяются UV (вершины на швах развёртки
     дублируются), .skin всех окрасок указывают на шейдеры с атласом,
     scripts/<робот>_player.shader (у K7 — walker_player.shader) описывает материалы
     (краска, металл, свечение, стекло).

Нужны: numpy, scipy, Pillow, xatlas, embreex (pip install xatlas embreex).
"""
import json, os, struct, sys
import numpy as np
from PIL import Image
from scipy import ndimage
from scipy.spatial import cKDTree
import xatlas
from embreex import rtcore_scene as rtcs
from embreex.mesh_construction import TriangleMesh
from rod_shader import rod_stages

MODEL_DIR = sys.argv[1]
DATA_ROOT = os.path.normpath(os.path.join(MODEL_DIR, '..', '..', '..'))
ROBOT = os.path.basename(os.path.normpath(MODEL_DIR))
SHADER_PREFIX = 'models/players/' + ROBOT
ENV_SHINE = 'models/players/walker/env_shine.jpg'
ENV_GLINT = 'models/players/walker/env_glint.jpg'
ROD_STAGES = rod_stages()     # общая модель полированного штока (rod_shader.py)
PARTS = {'lower': 1024, 'upper': 1024, 'head': 512}
# K7 - главный робот: атласы вдвое крупнее (2048), каждая точка - один тексель
# (столько же выборок, сколько у 1024 с SUPERSAMPLE 2), подробный износ (26.11.2026)
DETAILED = ROBOT == 'walker'
if DETAILED:
    PARTS = {'lower': 2048, 'upper': 2048, 'head': 1024}
SCHEMES = [scheme for scheme in ['default', 'red', 'blue', 'desert', 'urban']
           if os.path.exists(os.path.join(MODEL_DIR, f'lower_{scheme}.skin'))]
AO_RAYS = 40
AO_DISTANCE = 0.55          # единицы модели (1 единица = 14 единиц Quake)
SUPERSAMPLE = 1 if DETAILED else 2   # цвет считается в 2x2 точках на тексель (K7: 1)
FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-Bold.ttf'   # надписи-трафареты
# надписи на заданных местах K7 (часть, строки, точка у поверхности и наружу от неё, ширина в
# единицах модели): инвентарный номер на месте убранной голубой лампы (k7_side_lamp.py)
# трафаретом (stencil_text.py: заглавные, жирные узкие, с перемычками), белым по зелёной броне
FIXED_DECALS = [('upper', ['ИНВ.№', '50-458'], (-1.16, 0.12, -1.085), (-1.0, 0.0, 0.0), 0.22)]

# рельеф K7 (карта наклонов <часть>_bump.tga, 27.11.2026), единицы модели (1 = 14 единиц Quake)
SEAM_DEPTH = 0.006          # канавка шва
SEAM_ROLL = 0.004           # кромка панели скруглена к шву
RIVET_OFFSET = 0.032        # заклёпки вдоль швов: от шва
RIVET_HEIGHT = 0.0045
BOLT_HEIGHT = 0.006         # шестигранные болты в углах панелей
BOLT_OFFSET = 0.05          # центр болта от обоих швов
GRILLE_DEPTH = 0.009        # решётки: жалюзи в части панелей
GRILLE_PITCH = 0.03         # шаг пластин решётки
GRILLE_MARGIN = 0.075       # от швов до рамки решётки
PAINT_THICKNESS = 0.0012    # сколы краски чуть ниже окрашенной поверхности
BUMP_RANGE = 0.006          # наибольший наклон в карте: единиц модели на тексель

# окраски команд ярче: краска (насыщенные пиксели нужного оттенка) насыщеннее и светлее,
# металл и стекло (серые) не меняются; (диапазон оттенка 0..1, насыщенность x, яркость x)
TEAM_PAINT = {'red': ((0.93, 0.06), 1.9, 1.35), 'blue': ((0.50, 0.70), 2.6, 1.45)}


def team_paint(image, scheme):
    (hue_low, hue_high), saturation_gain, value_gain = TEAM_PAINT[scheme]
    hsv = np.array(image.convert('HSV')).astype(float) / 255
    hue, saturation, value = hsv[..., 0], hsv[..., 1], hsv[..., 2]
    in_range = (hue >= hue_low) | (hue <= hue_high) if hue_low > hue_high else (hue >= hue_low) & (hue <= hue_high)
    # плавно: чем насыщеннее пиксель, тем сильнее
    weight = in_range * np.clip((saturation - 0.12) / 0.15, 0, 1)
    hsv[..., 1] = np.clip(saturation * (1 + (saturation_gain - 1) * weight), 0, 1)
    hsv[..., 2] = np.clip(value * (1 + (value_gain - 1) * weight), 0, 1)
    return Image.fromarray((hsv * 255).astype(np.uint8), 'HSV').convert('RGB')


# материалы K7 и роботов make_robots.py
PAINTED = {'camo_green', 'camo_dark', 'panel_light', 'marking', 'trim_yellow', 'orange', 'paint', 'accent',
           'hydraulic_blue'}
METAL = {'metal_gray', 'metal_dark', 'chrome', 'black', 'metal', 'dark'}
GLOWING = {'engine_glow', 'light_cyan', 'sensor_purple', 'glow', 'indicator_red', 'indicator_green'}
GLASS = {'canopy_glass', 'glass'}
# стекло кабины (28.11.2026): тонированное, прозрачное - сквозь него видно кабину (blend);
# поверх слабое отражение окружения и яркий точечный блик (env_glint.jpg - точка солнца
# на чёрном, по карте окружения: на мелкой сетке стекла он остаётся точкой)
GLASS_OPACITY = 0.45
GLASS_REFLECTION = 0.12
HYDRAULIC = {'chrome'}      # блестящие штоки гидравлики: с них течёт масло

# ================================================================ GLB

COMPONENT = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
WIDTH = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}


def read_glb(path):
    data = open(path, 'rb').read()
    json_length = struct.unpack('<I', data[12:16])[0]
    document = json.loads(data[20:20 + json_length])
    offset = 20 + json_length
    bin_length = struct.unpack('<I', data[offset:offset + 4])[0]
    binary = data[offset + 8:offset + 8 + bin_length]
    return document, binary


def accessor(document, binary, index):
    info = document['accessors'][index]
    view = document['bufferViews'][info['bufferView']]
    dtype = np.dtype(COMPONENT[info['componentType']])
    width = WIDTH[info['type']]
    start = view.get('byteOffset', 0) + info.get('byteOffset', 0)
    stride = view.get('byteStride', 0) or dtype.itemsize * width
    count = info['count']
    raw = np.frombuffer(binary, dtype=np.uint8, count=stride * (count - 1) + dtype.itemsize * width, offset=start)
    rows = np.lib.stride_tricks.as_strided(raw, shape=(count, dtype.itemsize * width), strides=(stride, 1))
    return np.ascontiguousarray(rows).view(dtype).reshape(count, width) if width > 1 else np.ascontiguousarray(rows).view(dtype).reshape(count)


def write_glb(path, document, binary):
    json_bytes = json.dumps(document, separators=(',', ':')).encode('utf-8')
    json_bytes += b' ' * (-len(json_bytes) % 4)
    binary = bytes(binary) + b'\0' * (-len(binary) % 4)
    total = 12 + 8 + len(json_bytes) + 8 + len(binary)
    with open(path, 'wb') as file:
        file.write(struct.pack('<III', 0x46546C67, 2, total))
        file.write(struct.pack('<II', len(json_bytes), 0x4E4F534A) + json_bytes)
        file.write(struct.pack('<II', len(binary), 0x004E4942) + binary)


def quat_matrix(quat):
    x, y, z, w = quat
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def node_matrix(node):
    if 'matrix' in node:
        return np.array(node['matrix'], dtype=float).reshape(4, 4).T
    matrix = np.eye(4)
    rotation = quat_matrix(node.get('rotation', [0, 0, 0, 1]))
    matrix[:3, :3] = rotation * np.array(node.get('scale', [1, 1, 1]))[None, :]
    matrix[:3, 3] = node.get('translation', [0, 0, 0])
    return matrix


def world_matrices(document):
    result = {}
    def walk(index, parent):
        node = document['nodes'][index]
        matrix = parent @ node_matrix(node)
        result[index] = matrix
        for child in node.get('children', []):
            walk(child, matrix)
    for root in document['scenes'][document.get('scene', 0)]['nodes']:
        walk(root, np.eye(4))
    return result


# ================================================================ шум в пространстве модели

PERMUTATION = np.random.default_rng(7).permutation(256)
PERMUTATION = np.concatenate([PERMUTATION, PERMUTATION])
LATTICE = np.random.default_rng(8).random(256)


def value_noise(points):
    """гладкий шум по точкам (N, 3), значения в [0, 1]."""
    cell = np.floor(points).astype(np.int64)
    frac = points - cell
    fade = frac * frac * (3 - 2 * frac)
    cell &= 255
    result = 0.0
    for corner in range(8):
        offset = np.array([(corner >> 0) & 1, (corner >> 1) & 1, (corner >> 2) & 1])
        index = PERMUTATION[PERMUTATION[PERMUTATION[(cell[:, 0] + offset[0]) & 255] + ((cell[:, 1] + offset[1]) & 255)] + ((cell[:, 2] + offset[2]) & 255)]
        weight = np.prod(np.where(offset[None, :] == 1, fade, 1 - fade), axis=1)
        result = result + weight * LATTICE[index]
    return result


def fbm(points, octaves=4, gain=0.5):
    total, amplitude, norm = 0.0, 1.0, 0.0
    for octave in range(octaves):
        total = total + amplitude * value_noise(points * (2 ** octave) + octave * 17.31)
        norm += amplitude
        amplitude *= gain
    return total / norm


def smoothstep(edge0, edge1, value):
    ratio = np.clip((value - edge0) / (edge1 - edge0), 0, 1)
    return ratio * ratio * (3 - 2 * ratio)


# ================================================================ одна часть

class Part:
    def __init__(self, name, resolution):
        self.name = name
        self.resolution = resolution
        self.path = os.path.join(MODEL_DIR, name + '.glb')
        self.document, self.binary = read_glb(self.path)
        self.collect()

    def collect(self):
        """все примитивы в одну сетку (мировые координаты позы покоя)."""
        document, binary = self.document, self.binary
        matrices = world_matrices(document)
        positions, normals, uvs, faces, face_material, face_primitive = [], [], [], [], [], []
        self.primitives = []    # (mesh, primitive, первая грань, число граней, первая вершина, число вершин)
        vertex_base = face_base = 0
        for node_index, node in enumerate(document['nodes']):
            if 'mesh' not in node:
                continue
            matrix = matrices[node_index]
            normal_matrix = np.linalg.inv(matrix[:3, :3]).T
            for primitive_index, primitive in enumerate(document['meshes'][node['mesh']]['primitives']):
                attributes = primitive['attributes']
                local = accessor(document, binary, attributes['POSITION']).astype(float)
                normal = accessor(document, binary, attributes['NORMAL']).astype(float)
                uv = accessor(document, binary, attributes['TEXCOORD_0']).astype(float)
                if 'indices' in primitive:
                    index = accessor(document, binary, primitive['indices']).astype(np.int64).reshape(-1, 3)
                else:
                    index = np.arange(len(local)).reshape(-1, 3)
                world = local @ matrix[:3, :3].T + matrix[:3, 3]
                world_normal = normal @ normal_matrix.T
                world_normal /= np.linalg.norm(world_normal, axis=1, keepdims=True) + 1e-12
                positions.append(world); normals.append(world_normal); uvs.append(uv)
                faces.append(index + vertex_base)
                face_material += [primitive['material']] * len(index)
                face_primitive += [len(self.primitives)] * len(index)
                self.primitives.append({'mesh': node['mesh'], 'primitive': primitive_index, 'node': node_index,
                                        'first_face': face_base, 'faces': len(index),
                                        'first_vertex': vertex_base, 'local': local, 'normal': normal})
                vertex_base += len(local)
                face_base += len(index)
        self.positions = np.concatenate(positions)
        self.normals = np.concatenate(normals)
        self.uvs = np.concatenate(uvs)
        self.faces = np.concatenate(faces)
        self.face_material = np.array(face_material)
        self.face_primitive = np.array(face_primitive)
        self.material_names = [material['name'] for material in document['materials']]

    def unwrap(self):
        """xatlas по сетке, сваренной по положению вершин внутри каждого примитива."""
        welded_index = np.zeros(len(self.positions), dtype=np.int64)
        welded_positions = []
        base = 0
        for primitive in self.primitives:
            first = primitive['first_vertex']
            count = len(primitive['local'])
            keys = np.round(self.positions[first:first + count] * 2e4).astype(np.int64)
            unique, inverse = np.unique(keys, axis=0, return_inverse=True)
            inverse = inverse.reshape(-1)
            representative = np.zeros(len(unique), dtype=np.int64)
            representative[inverse] = np.arange(count)
            welded_positions.append(self.positions[first + representative])
            welded_index[first:first + count] = inverse + base
            base += len(unique)
        welded_positions = np.concatenate(welded_positions).astype(np.float32)
        welded_faces = welded_index[self.faces].astype(np.uint32)
        atlas = xatlas.Atlas()
        atlas.add_mesh(welded_positions, welded_faces)
        chart_options = xatlas.ChartOptions()
        chart_options.max_iterations = 4
        chart_options.normal_deviation_weight = 4.0
        pack_options = xatlas.PackOptions()
        pack_options.resolution = self.resolution
        pack_options.padding = 6
        pack_options.bilinear = True
        pack_options.bruteForce = False
        atlas.generate(chart_options, pack_options)
        vmapping, atlas_faces, atlas_uv = atlas[0]
        assert len(atlas_faces) == len(self.faces), 'xatlas changed the face count'
        assert np.all(welded_index[self.faces[:, 0]] == vmapping[atlas_faces[:, 0]]), 'xatlas changed the face order'
        self.atlas_faces = atlas_faces.astype(np.int64)
        self.atlas_uv = atlas_uv.astype(float)
        self.texels_per_unit = atlas.texels_per_unit
        print(f'  {self.name}: {len(self.faces)} faces, charts {atlas.chart_count}, utilization {atlas.utilization:.2f}, '
              f'{self.texels_per_unit:.0f} texels per unit')

    def rasterize(self):
        """для каждой точки выборки (SUPERSAMPLE^2 на тексель): грань и барицентрические координаты."""
        size = self.resolution * SUPERSAMPLE
        face_map = np.full((size, size), -1, dtype=np.int64)
        bary_map = np.zeros((size, size, 3))
        corners = self.atlas_uv[self.atlas_faces] * size      # (F, 3, 2) в пикселях выборки
        for face, triangle in enumerate(corners):
            low = np.floor(triangle.min(axis=0)).astype(int)
            high = np.ceil(triangle.max(axis=0)).astype(int)
            low = np.clip(low, 0, size - 1); high = np.clip(high, 0, size)
            if high[0] <= low[0] or high[1] <= low[1]:
                continue
            grid_y, grid_x = np.mgrid[low[1]:high[1], low[0]:high[0]] + 0.5
            (x0, y0), (x1, y1), (x2, y2) = triangle
            area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
            if abs(area) < 1e-9:
                continue
            weight1 = ((grid_x - x0) * (y2 - y0) - (x2 - x0) * (grid_y - y0)) / area
            weight2 = ((x1 - x0) * (grid_y - y0) - (grid_x - x0) * (y1 - y0)) / area
            weight0 = 1 - weight1 - weight2
            inside = (weight0 >= -1e-6) & (weight1 >= -1e-6) & (weight2 >= -1e-6)
            rows = grid_y[inside].astype(int); cols = grid_x[inside].astype(int)
            face_map[rows, cols] = face
            bary_map[rows, cols] = np.stack([weight0[inside], weight1[inside], weight2[inside]], axis=-1)
        self.face_map, self.bary_map = face_map, bary_map
        covered = face_map >= 0
        self.sample_rows, self.sample_cols = np.nonzero(covered)
        faces = face_map[covered]
        bary = bary_map[covered]
        corner_index = self.faces[faces]                      # исходные вершины
        self.sample_face = faces
        self.sample_material = self.face_material[faces]
        self.sample_position = np.einsum('nk,nkd->nd', bary, self.positions[corner_index])
        normal = np.einsum('nk,nkd->nd', bary, self.normals[corner_index])
        self.sample_normal = normal / (np.linalg.norm(normal, axis=1, keepdims=True) + 1e-12)
        self.sample_uv = np.einsum('nk,nkd->nd', bary, self.uvs[corner_index])
        triangle = self.positions[self.faces[faces]]
        face_normal = np.cross(triangle[:, 1] - triangle[:, 0], triangle[:, 2] - triangle[:, 0])
        self.sample_face_normal = face_normal / (np.linalg.norm(face_normal, axis=1, keepdims=True) + 1e-12)
        print(f'  {self.name}: {len(faces)} samples')

    def ambient_occlusion(self):
        scene = rtcs.EmbreeScene()
        TriangleMesh(scene, self.positions[self.faces].astype(np.float32))
        count = len(self.sample_position)
        rng = np.random.default_rng(11)
        normal = self.sample_normal
        # опорный базис для каждой точки
        helper = np.where(np.abs(normal[:, 2:3]) < 0.9, np.array([[0, 0, 1.0]]), np.array([[1.0, 0, 0]]))
        tangent = np.cross(helper, normal)
        tangent /= np.linalg.norm(tangent, axis=1, keepdims=True)
        bitangent = np.cross(normal, tangent)
        origin = (self.sample_position + self.sample_face_normal * 0.004).astype(np.float32)
        visible = np.zeros(count)
        for ray in range(AO_RAYS):
            # косинусное распределение, стратифицированное по номеру луча
            u1 = (ray + rng.random(count)) / AO_RAYS
            u2 = rng.random(count)
            radius = np.sqrt(u1)
            angle = 2 * np.pi * u2
            local_x, local_y, local_z = radius * np.cos(angle), radius * np.sin(angle), np.sqrt(1 - u1)
            direction = (tangent * local_x[:, None] + bitangent * local_y[:, None] + normal * local_z[:, None]).astype(np.float32)
            result = scene.run(origin, direction, output=1)
            distance = result['tfar']
            hit = (result['geomID'] >= 0) & (distance < AO_DISTANCE)
            # ближние препятствия затеняют сильнее дальних
            visible += np.where(hit, (distance / AO_DISTANCE) ** 0.7, 1.0)
        self.sample_ao = visible / AO_RAYS
        print(f'  {self.name}: AO mean {self.sample_ao.mean():.2f}')

    def edges(self):
        """расстояние от каждой точки до ближайшего выпуклого и вогнутого острого ребра."""
        keys = np.round(self.positions * 2e4).astype(np.int64)
        _, vertex_id = np.unique(keys, axis=0, return_inverse=True)
        vertex_id = vertex_id.reshape(-1)
        faces = vertex_id[self.faces]
        triangle = self.positions[self.faces]
        face_normal = np.cross(triangle[:, 1] - triangle[:, 0], triangle[:, 2] - triangle[:, 0])
        face_area = np.linalg.norm(face_normal, axis=1)
        valid = face_area > 1e-10
        face_normal = face_normal / (face_area[:, None] + 1e-12)
        centroid = triangle.mean(axis=1)
        edge_faces = {}
        for face in np.nonzero(valid)[0]:
            for corner in range(3):
                first, second = faces[face, corner], faces[face, (corner + 1) % 3]
                key = (min(first, second), max(first, second))
                edge_faces.setdefault(key, []).append((face, corner))
        convex_points, concave_points = [], []
        for key, users in edge_faces.items():
            if len(users) != 2:
                continue
            (face_a, corner_a), (face_b, _) = users
            cosine = np.dot(face_normal[face_a], face_normal[face_b])
            if cosine > 0.82:          # меньше ~35 градусов — не ребро
                continue
            start = self.positions[self.faces[face_a, corner_a]]
            end = self.positions[self.faces[face_a, (corner_a + 1) % 3]]
            length = np.linalg.norm(end - start)
            steps = max(2, int(length / 0.006))
            points = start[None, :] + (end - start)[None, :] * np.linspace(0, 1, steps)[:, None]
            convex = np.dot(centroid[face_b] - centroid[face_a], face_normal[face_a]) < 0
            (convex_points if convex else concave_points).append(points)
        def distance_to(points):
            if not points:
                return np.full(len(self.sample_position), 1e3)
            tree = cKDTree(np.concatenate(points))
            distance, _ = tree.query(self.sample_position, k=1, workers=-1)
            return distance
        self.sample_convex = distance_to(convex_points)
        self.sample_concave = distance_to(concave_points)
        print(f'  {self.name}: sharp edges convex {len(convex_points)}, concave {len(concave_points)}')

    def masks(self):
        """маски износа, общие для всех окрасок."""
        position = self.sample_position
        normal = self.sample_normal
        texel = 1.0 / self.texels_per_unit
        chip_noise = fbm(position * 38.0, 4)
        wide_noise = fbm(position * 6.0 + 3.1, 3)
        # сколы краски на выпуклых рёбрах: ширина пляшет, местами сколов нет совсем
        width = texel * (0.6 + 4.5 * np.clip(chip_noise - 0.35, 0, 1) * 2.2) * (0.4 + wide_noise)
        self.wear = smoothstep(0.0, 1.0, (width - self.sample_convex) / (texel * 1.2 + 1e-9))
        self.wear *= smoothstep(0.38, 0.55, chip_noise)
        # светлый кант на рёбрах (стёртая до блеска кромка) и тёмная грязь во вогнутых углах
        self.edge_light = np.exp(-self.sample_convex / (texel * 1.5))
        self.crevice = np.exp(-self.sample_concave / (texel * 3.0)) * (0.5 + 0.5 * wide_noise)
        # царапины: тонкие линии там, где шум проходит через середину
        scratch_field = value_noise(position * np.array([90.0, 14.0, 90.0]) + 5.0)
        scratch_mask = value_noise(position * 4.0 + 9.0)
        self.scratch = smoothstep(0.012, 0.0, np.abs(scratch_field - 0.5)) * smoothstep(0.55, 0.75, scratch_mask)
        # высота: у glTF ось Y вверх; ноги грязнее
        height = position[:, 1]
        low, high = self.height_range
        relative = np.clip((height - low) / (high - low + 1e-9), 0, 1)
        dirt_noise = fbm(position * 9.0 + 1.7, 4)
        self.mud = np.clip((self.mud_level - relative) / self.mud_level, 0, 1) ** 1.3 * (0.55 + 0.6 * dirt_noise)
        # потёки на вертикальных стенках
        vertical = 1 - np.abs(normal[:, 1])
        streak = value_noise(position * np.array([60.0, 2.5, 60.0]) + 2.0)
        self.streaks = smoothstep(0.55, 0.85, streak) * vertical * (0.6 + 0.4 * wide_noise)
        # пыль на горизонтальных поверхностях сверху
        self.dust = smoothstep(0.55, 0.95, normal[:, 1]) * (0.35 + 0.65 * fbm(position * 14.0 + 4.4, 3))
        self.grain = fbm(position * 120.0, 2) - 0.5
        self.panel_masks(texel)
        self.detail_masks(texel, chip_noise, wide_noise)
        self.relief(texel, self.panel_gate)

    def detail_masks(self, texel, chip_noise, wide_noise):
        """крупные сколы, масло со штоков, копоть у дюз, надписи; без DETAILED - пусто."""
        count = len(self.sample_position)
        self.chip_big = np.zeros(count); self.chip_ring = np.zeros(count); self.mottle = np.zeros(count)
        self.oil = np.zeros(count); self.oil_film = np.zeros(count); self.soot = np.zeros(count)
        self.decal_color = np.zeros((count, 3)); self.decal_alpha = np.zeros(count)
        if not DETAILED:
            return
        position = self.sample_position
        normal = self.sample_normal
        materials = np.array(self.material_names)[self.sample_material]
        # крупные сколы краски: у рёбер чаще, на больших панелях пятнами; вокруг скола
        # тёмная полоса грунта, внутри голый металл
        big_noise = fbm(position * 13.0 + 7.7, 4)
        edge_near = np.exp(-self.sample_convex / (texel * 9))
        field = big_noise * 0.70 + edge_near * 0.34 * (0.4 + 1.1 * chip_noise)
        self.chip_big = smoothstep(0.716, 0.730, field)
        self.chip_ring = np.clip(smoothstep(0.695, 0.712, field) - self.chip_big, 0, 1)
        # крупные пятна тона краски (выгорание, подкраска)
        self.mottle = fbm(self.sample_position * 2.6 + 5.5, 3) - 0.5
        # масло: течёт вниз со штоков гидравлики по вертикальным стенкам под ними
        hydraulic = np.isin(materials, list(HYDRAULIC))
        if np.any(hydraulic):
            sources = position[hydraulic][::3]
            scale = np.array([1 / 0.06, 1 / 0.75, 1 / 0.06])
            tree = cKDTree(sources * scale)
            distance, index = tree.query(position * scale, k=6, distance_upper_bound=1.0, workers=-1)
            found = np.isfinite(distance)
            index = np.where(found, index, 0)
            below = found & (sources[index, 1] - position[:, 1][:, None] > -0.01)
            nearness = np.max(np.where(below, (1 - np.where(found, distance, 1)) ** 1.5, 0), axis=1)
            vertical = smoothstep(0.15, 0.6, 1 - np.abs(normal[:, 1]))
            streak = smoothstep(0.48, 0.78, value_noise(position * np.array([75.0, 1.4, 75.0]) + 11.0))
            drip = smoothstep(0.35, 0.9, value_noise(position * np.array([140.0, 3.0, 140.0]) + 4.0))
            self.oil = np.clip(nearness * (streak * 0.85 + drip * 0.4) * (0.35 + 0.65 * vertical), 0, 1) * ~hydraulic
            # на самих штоках - масляная плёнка полосами вдоль штока
            film = smoothstep(0.42, 0.72, fbm(position * np.array([28.0, 3.5, 28.0]) + 3.0, 3))
            self.oil_film = film * hydraulic
        # копоть над дюзами (engine_glow): тёмный налёт, гуще у самых дюз и вверх от них
        engine = materials == 'engine_glow'
        if np.any(engine):
            tree = cKDTree(position[engine][::4])
            distance, _ = tree.query(position, k=1, distance_upper_bound=0.4, workers=-1)
            near = np.where(np.isfinite(distance), 1 - distance / 0.4, 0)
            self.soot = np.clip(near ** 1.6 * (0.6 + 0.6 * fbm(position * 18.0 + 2.2, 3)), 0, 1) * ~engine
        self.decals(texel, materials)

    def decals(self, texel, materials):
        """трафаретные надписи, полосы опасности и таблички на плоских окрашенных панелях."""
        from PIL import ImageDraw, ImageFont
        position = self.sample_position
        normal = self.sample_normal
        rng = np.random.default_rng({'lower': 21, 'upper': 22, 'head': 23}[self.name])
        flat = (np.isin(materials, ['camo_dark', 'camo_green', 'panel_light']) & (self.sample_convex > 0.09)
                & (self.sample_concave > 0.07) & (self.sample_ao > 0.72))
        candidates = np.nonzero(flat)[0]
        self.model_center = (self.positions.min(axis=0) + self.positions.max(axis=0)) / 2
        if len(candidates) == 0:
            return
        font_big = ImageFont.truetype(FONT, 150)
        font_small = ImageFont.truetype(FONT, 46)
        def stencil(text, size=(512, 256), color=(226, 222, 206)):
            image = Image.new('RGBA', size, (0, 0, 0, 0))
            draw = ImageDraw.Draw(image)
            draw.text((size[0] // 2, size[1] // 2), text, font=font_big, fill=color + (235,), anchor='mm')
            # трафаретные перемычки
            for x in range(40, size[0], 95):
                draw.rectangle([x, 0, x + 7, size[1]], fill=(0, 0, 0, 0))
            return image
        def hazard(size=(512, 128)):
            image = Image.new('RGBA', size, (232, 184, 30, 245))
            draw = ImageDraw.Draw(image)
            for x in range(-size[1], size[0] + size[1], 64):
                draw.polygon([(x, size[1]), (x + 32, size[1]), (x + 32 + size[1], 0), (x + size[1], 0)], fill=(24, 22, 20, 245))
            return image
        def label(lines, size=(512, 200)):
            image = Image.new('RGBA', size, (214, 172, 40, 240))
            draw = ImageDraw.Draw(image)
            draw.rectangle([6, 6, size[0] - 7, size[1] - 7], outline=(30, 28, 24, 255), width=6)
            for row, text in enumerate(lines):
                draw.text((24, 22 + row * 56), text, font=font_small, fill=(30, 28, 24, 255))
            return image
        def arrow(text, size=(512, 160), color=(226, 222, 206)):
            image = Image.new('RGBA', size, (0, 0, 0, 0))
            draw = ImageDraw.Draw(image)
            draw.polygon([(20, 80), (90, 20), (90, 55), (160, 55), (160, 105), (90, 105), (90, 140)], fill=color + (230,))
            draw.text((190, 80), text, font=font_small, fill=color + (230,), anchor='lm')
            return image
        designs = {'lower': [(stencil('K7'), 0.55), (hazard(), 0.50), (label(['HYDRAULIC', 'HIGH PRESSURE', '350 BAR']), 0.34),
                             (arrow('LIFT POINT'), 0.38), (stencil('07'), 0.45), (hazard(), 0.42), (label(['SERVICE', 'EVERY 40 H']), 0.28)],
                   'upper': [(stencil('K7'), 0.62), (label(['CAUTION', 'BLAST AREA', 'KEEP CLEAR']), 0.36), (hazard(), 0.55),
                             (stencil('07'), 0.48), (arrow('NO STEP'), 0.40), (label(['FUEL', 'JP-8 ONLY']), 0.30), (hazard(), 0.44)],
                   'head': [(label(['SENSOR', 'DO NOT TOUCH']), 0.24), (stencil('7'), 0.22), (hazard((256, 96)), 0.24)]}[self.name]
        order = rng.permutation(candidates)[:400]
        tree = cKDTree(position)
        placed = []
        up_world = np.array([0.0, 1.0, 0.0])
        for design, width in designs:
            for center_index in order:
                center = position[center_index]
                if any(np.linalg.norm(center - other) < 0.6 for other in placed):
                    continue
                near = np.array(tree.query_ball_point(center, width * 0.75), dtype=np.int64)
                if len(near) == 0:
                    continue
                facing = normal[center_index]
                # надпись читается снаружи: нормаль от середины модели
                if np.dot(facing, center - self.model_center) < 0:
                    continue
                up = up_world - facing * np.dot(facing, up_world)
                if np.linalg.norm(up) < 0.35:
                    up = np.array([0.0, 0.0, 1.0]) - facing * facing[2]
                up /= np.linalg.norm(up)
                right = np.cross(up, facing)
                height = width * design.size[1] / design.size[0]
                offset = position[near] - center
                local_x = offset @ right / width + 0.5
                local_y = 0.5 - offset @ up / height
                keep = ((local_x >= 0) & (local_x <= 1) & (local_y >= 0) & (local_y <= 1)
                        & (normal[near] @ facing > 0.88) & (np.abs(offset @ facing) < 0.03))
                inside = near[keep]
                local_x, local_y = local_x[keep], local_y[keep]
                # вся надпись должна лечь на ровную краску
                if len(inside) < 0.8 * (width * height) * self.texels_per_unit ** 2 * SUPERSAMPLE ** 2 \
                        or not np.all(flat[inside] | (self.sample_convex[inside] > 0.05)):
                    continue
                pixels = np.asarray(design).astype(float)
                rows = np.clip((local_y * (design.size[1] - 1)).astype(int), 0, design.size[1] - 1)
                cols = np.clip((local_x * (design.size[0] - 1)).astype(int), 0, design.size[0] - 1)
                rgba = pixels[rows, cols]
                alpha = rgba[:, 3] / 255 * (0.82 + 0.18 * fbm(position[inside] * 40.0, 2))
                self.decal_color[inside] = self.decal_color[inside] * (1 - alpha[:, None]) + rgba[:, :3] * alpha[:, None]
                self.decal_alpha[inside] = np.maximum(self.decal_alpha[inside], alpha)
                placed.append(center)
                break
        print(f'  {self.name}: decals {len(placed)} of {len(designs)}')
        if DETAILED:
            self.fixed_decals(position, normal)

    def fixed_decals(self, position, normal):
        """надписи FIXED_DECALS: светлая краска, строки по центру, на ближайшей к точке грани,
        смотрящей наружу"""
        import stencil_text
        for part_name, lines, point, facing, width in FIXED_DECALS:
            if part_name != self.name:
                continue
            facing = np.array(facing, float); facing /= np.linalg.norm(facing)
            facing_samples = np.nonzero(normal @ facing > 0.9)[0]
            if len(facing_samples) == 0:
                continue
            nearest = facing_samples[np.argmin(np.linalg.norm(position[facing_samples] - np.array(point), axis=1))]
            centre = position[nearest]
            size = (512, 300)
            # белая краска прямо по зелёной броне, без подложки (28.11.2026)
            image = stencil_text.stencil_image(lines, size, color=(234, 232, 224), alpha=240)
            up = np.array([0.0, 1.0, 0.0]) - facing * facing[1]
            up /= np.linalg.norm(up)
            right = np.cross(up, facing)
            height = width * size[1] / size[0]
            near = np.array(cKDTree(position).query_ball_point(centre, max(width, height) * 0.75), dtype=np.int64)
            offset = position[near] - centre
            local_x = offset @ right / width + 0.5
            local_y = 0.5 - offset @ up / height
            keep = ((local_x >= 0) & (local_x <= 1) & (local_y >= 0) & (local_y <= 1)
                    & (normal[near] @ facing > 0.88) & (np.abs(offset @ facing) < 0.03))
            inside = near[keep]
            pixels = np.asarray(image).astype(float)
            rows = np.clip((local_y[keep] * (size[1] - 1)).astype(int), 0, size[1] - 1)
            cols = np.clip((local_x[keep] * (size[0] - 1)).astype(int), 0, size[0] - 1)
            rgba = pixels[rows, cols]
            alpha = rgba[:, 3] / 255 * (0.85 + 0.15 * fbm(position[inside] * 40.0, 2))
            self.decal_color[inside] = self.decal_color[inside] * (1 - alpha[:, None]) + rgba[:, :3] * alpha[:, None]
            self.decal_alpha[inside] = np.maximum(self.decal_alpha[inside], alpha)
            print(f'  {self.name}: fixed decal {" ".join(lines)!r} at {centre.round(3).tolist()}, {len(inside)} texels')

    def panel_masks(self, texel):
        """швы между панелями брони, заклёпки вдоль швов и разный тон соседних панелей.
        Проекция по главной оси нормали; на мелких деталях (рядом с рёбрами) швов нет."""
        position = self.sample_position
        axis = np.argmax(np.abs(self.sample_normal), axis=1)
        first = np.choose(axis, [position[:, 1], position[:, 0], position[:, 0]])
        second = np.choose(axis, [position[:, 2], position[:, 2], position[:, 1]])
        cell_first, cell_second = 0.46, 0.31
        line = np.zeros(len(position)); bevel = np.zeros(len(position)); rivet = np.zeros(len(position))
        tone = np.zeros(len(position))
        rivet_distance = np.full(len(position), 1e3)
        self.panel_geometry = {'axis': axis, 'distance': [], 'present': [], 'index': [], 'coord': []}
        for coord, other, cell, seed in ((first, second, cell_first, 1.0), (second, first, cell_second, 2.0)):
            shifted = coord / cell + 0.37 * seed + axis * 0.21
            index = np.floor(shifted)
            distance = (shifted - np.round(shifted)) * cell          # до ближайшего шва
            # шов есть не по всей длине: кусками по клеткам другой координаты
            segment = np.floor(other / (cell * 1.7) + seed)
            present = value_noise(np.stack([np.round(shifted), segment, axis + seed * 7], axis=1) * 0.999 + 0.5) > 0.32
            # K7 (с рельефом) - шов шире: в несколько текселей, чтобы не пропадал вдали
            seam_width = 2.0 if DETAILED else 1.1
            seam = smoothstep(texel * seam_width, texel * seam_width * 0.4, np.abs(distance)) * present
            line = np.maximum(line, seam)
            if DETAILED:
                bevel_at = texel * seam_width * 1.5
                bevel = np.maximum(bevel, smoothstep(bevel_at * 1.5, bevel_at * 0.75, np.abs(np.abs(distance) - bevel_at)) * present)
            else:
                bevel = np.maximum(bevel, smoothstep(texel * 2.4, texel * 1.2, np.abs(distance - texel * 1.6)) * present)
            # заклёпки: точки в шаге 0.06 вдоль шва, на расстоянии RIVET_OFFSET (без рельефа 0.025) от него
            along = other / 0.06
            offset = RIVET_OFFSET if DETAILED else 0.025
            dot_distance = np.hypot((along - np.round(along)) * 0.06, np.abs(distance) - offset)
            rivet_size = 2.4 if DETAILED else 1.6
            rivet = np.maximum(rivet, smoothstep(texel * rivet_size, texel * rivet_size * 0.5, dot_distance) * present)
            rivet_distance = np.where(present, np.minimum(rivet_distance, dot_distance), rivet_distance)
            tone += value_noise(np.stack([index, axis * 3.0 + seed, np.zeros_like(index)], axis=1) + 0.5) - 0.5
            for key, value in (('distance', distance), ('present', present), ('index', index), ('coord', coord)):
                self.panel_geometry[key].append(value)
        self.panel_geometry['rivet_distance'] = rivet_distance
        gate = smoothstep(0.03, 0.09, self.sample_convex)
        self.panel_line = line * gate
        self.panel_bevel = bevel * gate * (1 - line)
        self.rivet = rivet * gate * (1 - line)
        self.panel_tone = tone * 0.5
        self.panel_gate = gate

    def relief(self, texel, gate):
        """высота рельефа (единицы модели, наружу +) на тех же панелях, что швы и заклёпки в цвете:
        канавки швов со скруглёнными кромками, купола заклёпок, шестигранные болты в углах
        панелей, решётки-жалюзи в части панелей, сколы краски чуть ниже краски.
        Без DETAILED - ровно."""
        count = len(self.sample_position)
        self.height = np.zeros(count); self.grille_shadow = np.zeros(count); self.bolt = np.zeros(count)
        if not DETAILED:
            return
        geometry = self.panel_geometry
        materials = np.array(self.material_names)[self.sample_material]
        painted = np.isin(materials, list(PAINTED))
        metal = np.isin(materials, list(METAL)) & ~np.isin(materials, list(HYDRAULIC))
        surface = (painted | metal) * gate
        distance_a, distance_b = np.abs(geometry['distance'][0]), np.abs(geometry['distance'][1])
        present_a, present_b = geometry['present'][0], geometry['present'][1]
        height = np.zeros(count)
        # швы: кромки панелей скатываются к шву, в самом шве канавка
        for distance, present in ((distance_a, present_a), (distance_b, present_b)):
            roll = 1 - smoothstep(texel * 1.5, texel * 7.0, distance)
            groove = 1 - smoothstep(texel * 0.6, texel * 2.2, distance)
            height -= (SEAM_ROLL * roll + SEAM_DEPTH * groove) * present
        # заклёпки - купола там же, где светлые точки в цвете
        rivet_radius = texel * 2.8
        ratio = np.clip(geometry['rivet_distance'] / rivet_radius, 0, 1)
        height += RIVET_HEIGHT * np.sqrt(1 - ratio ** 2)
        # болты: в углу, где сходятся оба шва, внутри клетки; не во всех углах
        cell_key = np.stack([geometry['index'][0], geometry['index'][1], geometry['axis'] * 5.0 + 0.5], axis=1)
        cell_hash = value_noise(cell_key * 0.999 + 0.5)
        local_x, local_y = distance_a - BOLT_OFFSET, distance_b - BOLT_OFFSET
        hexagon = np.maximum(np.abs(local_x) * 0.866 + np.abs(local_y) * 0.5, np.abs(local_y))
        bolt_radius = max(texel * 4.0, 0.013)
        has_bolt = present_a * present_b * (cell_hash > 0.35)
        self.bolt = smoothstep(bolt_radius, bolt_radius * 0.72, hexagon) * has_bolt * surface
        washer = smoothstep(bolt_radius * 1.45, bolt_radius * 1.2, np.hypot(local_x, local_y)) * has_bolt * surface
        height += BOLT_HEIGHT * self.bolt + BOLT_HEIGHT * 0.25 * np.clip(washer - self.bolt, 0, 1)
        # решётки: клетка целиком (без полей у швов) - жалюзи поперёк одной оси, с рамкой
        inside = np.minimum(distance_a, distance_b)
        chosen = np.where(metal, cell_hash < 0.36, cell_hash < 0.16) & (self.sample_concave > 0.04)
        grille = smoothstep(GRILLE_MARGIN, GRILLE_MARGIN + texel * 1.5, inside) * chosen * gate
        frame = (smoothstep(GRILLE_MARGIN - texel * 4.5, GRILLE_MARGIN - texel * 3.0, inside) * chosen * gate) * (1 - grille)
        phase = np.mod(geometry['coord'][0] / GRILLE_PITCH, 1.0)
        drop = texel / GRILLE_PITCH * 1.5
        louver = phase * (1 - smoothstep(1 - drop, 1.0, phase))       # пластина наклонена, у края обрыв
        height += grille * (-GRILLE_DEPTH * (0.35 + 0.65 * louver)) + frame * BOLT_HEIGHT * 0.5
        self.grille_shadow = grille * smoothstep(0.45, 0.95, louver)
        # сколы краски: до металла - на толщину краски ниже
        height -= PAINT_THICKNESS * np.clip(self.wear + self.chip_big, 0, 1) * painted
        self.height = height * np.where(surface > 0, 1.0, 0.0)
        print(f'  {self.name}: relief, grille share {np.mean(grille > 0.5):.3f}, bolts {np.mean(self.bolt > 0.5):.4f}')

    def base_colors(self, scheme):
        """цвет материала окраски в каждой точке по исходным (повторяющимся) UV."""
        skin = read_skin(os.path.join(MODEL_DIR, f'{self.name}_{scheme}.skin'))
        colors = np.zeros((len(self.sample_position), 3))
        for material_index, material in enumerate(self.material_names):
            selected = self.sample_material == material_index
            if not np.any(selected):
                continue
            texture_name = os.path.basename(skin.get(material, f'{SHADER_PREFIX}/{material}'))
            image = load_texture(texture_name)
            colors[selected] = bilinear(image, self.sample_uv[selected])
        return colors

    def compose(self, scheme):
        base = self.base_colors(scheme)
        materials = np.array(self.material_names)[self.sample_material]
        painted = np.isin(materials, list(PAINTED))
        metal = np.isin(materials, list(METAL))
        glowing = np.isin(materials, list(GLOWING))
        glass = np.isin(materials, list(GLASS))
        ao = self.sample_ao
        color = base.copy()
        # общий износ: царапины и зерно на всём, кроме светящегося и стекла
        rough = ~(glowing | glass)
        color[rough] *= (1 + 0.10 * self.grain[rough])[:, None]
        # краска приглушённая, «армейская»: меньше насыщенности, у панелей разный тон
        gray = color.mean(axis=1, keepdims=True)
        muted = gray + (color - gray) * 0.72
        paint_level = 0.97 if DETAILED else 0.9
        color = np.where(painted[:, None], muted * paint_level * (1 + 0.09 * self.panel_tone[:, None]), color)
        # надписи и таблички - под износом: их тоже скалывает и пачкает
        decal = (self.decal_alpha * painted)[:, None]
        color = color * (1 - decal) + self.decal_color * decal
        # швы панелей: тёмная щель, светлая фаска рядом, заклёпки
        panel = (painted | metal)[:, None]
        color = color * (1 - 0.55 * self.panel_line[:, None] * panel)
        color = color * (1 + 0.12 * self.panel_bevel[:, None] * panel)
        color = color + (np.array([185, 180, 170]) - color) * (0.35 * self.rivet[:, None] * panel)
        # рельеф (relief): болты светлее, щели между пластинами решёток тёмные
        color = color + (np.array([170, 168, 160]) - color) * (0.30 * self.bolt[:, None])
        color = color * (1 - 0.55 * self.grille_shadow[:, None])
        # краска: сколы до голого металла, светлый кант, царапины
        steel = np.array([150, 148, 142]) * (0.8 + 0.4 * self.grain[:, None] + 0.25)
        primer = np.array([58, 56, 52])
        chip = self.wear[:, None] * painted[:, None]
        color = color * (1 - chip) + (steel * 0.75 + primer * 0.25) * chip
        # крупные сколы: тёмный грунт по краю, голый светлый металл внутри
        ring = (self.chip_ring * 0.85)[:, None] * painted[:, None]
        color = color * (1 - ring) + primer * ring
        big = self.chip_big[:, None] * painted[:, None]
        bare_steel = np.array([112, 112, 108]) * (0.9 + 0.3 * self.grain[:, None])
        color = color * (1 - big) + bare_steel * big
        color = color * (1 + 0.22 * self.mottle[:, None] * painted[:, None])
        edge_gain = 0.42 if DETAILED else 0.22
        lighten = (self.edge_light * edge_gain + self.scratch * 0.35)[:, None] * painted[:, None]
        color = color + (np.array([200, 196, 186]) - color) * lighten
        # металл: светлые кромки и царапины до блеска
        shine = (self.edge_light * 0.30 + self.scratch * 0.45)[:, None] * metal[:, None]
        color = color + (np.array([215, 214, 208]) - color) * shine
        # грязь: потёки, вогнутые углы, низ ног, пыль сверху
        grime_color = np.array([46, 40, 32])
        mud_color = np.array([88, 74, 54])
        dust_color = np.array([150, 140, 118])
        streak_gain = 0.45 if DETAILED else 0.30
        grime = np.clip(self.streaks * streak_gain + self.crevice * 0.45, 0, 0.8)[:, None] * rough[:, None]
        color = color * (1 - grime) + grime_color * grime
        mud = np.clip(self.mud, 0, 0.85)[:, None] * rough[:, None]
        color = color * (1 - mud) + mud_color * (0.8 + 0.4 * self.grain[:, None]) * mud
        dust = (self.dust * 0.22)[:, None] * rough[:, None]
        color = color * (1 - dust) + dust_color * dust
        # копоть у дюз и масло: тёмно-янтарные потёки (блестят - см. карту износа)
        soot = (self.soot * 0.75)[:, None] * rough[:, None]
        color = color * (1 - soot) + np.array([22, 20, 18]) * soot
        oil = np.clip(self.oil * 0.9, 0, 0.85)[:, None]
        color = color * (1 - oil) + np.array([34, 22, 10]) * oil
        film = (self.oil_film * 0.45)[:, None]
        color = color * (1 - film) + color * np.array([0.62, 0.50, 0.30]) * film
        # дюзы (engine_glow): не плоский оранжевый, а приглушённое свечение — горячая
        # середина пластины, тёмно-красные края, у самого ребра почти чёрный металл
        texel = 1.0 / self.texels_per_unit
        engine = materials == 'engine_glow'
        if np.any(engine):
            inner = smoothstep(texel * 0.5, texel * 5.0, self.sample_convex[engine])
            flicker = 0.85 + 0.3 * fbm(self.sample_position[engine] * 30.0, 2)
            hot = np.array([236, 118, 52]); warm = np.array([150, 50, 22]); edge = np.array([46, 22, 14])
            glow = edge + (warm - edge) * smoothstep(0.0, 0.35, inner)[:, None] + (hot - warm) * smoothstep(0.35, 1.0, inner)[:, None]
            color[engine] = glow * flicker[:, None] * 0.82
        # затенение: сильное в щелях, светящиеся детали не затеняются
        shade = (0.24 + 0.76 * np.clip(ao, 0, 1) ** 1.4) if DETAILED else (0.22 + 0.78 * np.clip(ao, 0, 1) ** 1.25)
        shade = np.where(glowing, 0.75 + 0.25 * shade, shade)
        color *= shade[:, None]
        return np.clip(color, 0, 255)

    def write_atlas(self, scheme, colors):
        size = self.resolution * SUPERSAMPLE
        image = np.zeros((size, size, 3))
        image[self.sample_rows, self.sample_cols] = colors
        covered = np.zeros((size, size), dtype=bool)
        covered[self.sample_rows, self.sample_cols] = True
        # заливка полей вокруг кусков развёртки ближайшим цветом (чтобы mip-уровни не темнели на швах)
        _, (near_rows, near_cols) = ndimage.distance_transform_edt(~covered, return_indices=True)
        image = image[near_rows, near_cols]
        result = Image.fromarray(image.astype(np.uint8)).resize((self.resolution, self.resolution), Image.LANCZOS)
        if scheme in TEAM_PAINT:
            result = team_paint(result, scheme)
        result.save(os.path.join(MODEL_DIR, f'{self.name}_{scheme}.jpg'), quality=88, optimize=True)

    def to_image(self, values, channels):
        """значения в точках выборки -> картинка атласа (поля вокруг кусков - ближайшим)."""
        size = self.resolution * SUPERSAMPLE
        image = np.zeros((size, size, channels))
        image[self.sample_rows, self.sample_cols] = values.reshape(len(self.sample_rows), channels)
        covered = np.zeros((size, size), dtype=bool)
        covered[self.sample_rows, self.sample_cols] = True
        _, (near_rows, near_cols) = ndimage.distance_transform_edt(~covered, return_indices=True)
        return image[near_rows, near_cols]

    def write_wear(self):
        """<часть>_wear.tga для make_material_pass.py (блеск в один проход, tr_glsl.c):
        r - голый металл (сколы, стёртые кромки, царапины): блестит как металл;
        g - множитель блеска класса материала x128 (грязь и пыль матовые, масло мокрое);
        b - масло: мокрый блеск."""
        materials = np.array(self.material_names)[self.sample_material]
        painted = np.isin(materials, list(PAINTED))
        metal = np.isin(materials, list(METAL))
        bare = np.where(painted, np.clip(self.wear + self.chip_big + self.scratch * 0.6 + self.edge_light * 0.25, 0, 1),
                        np.where(metal, np.clip(self.edge_light * 0.5 + self.scratch * 0.7, 0, 1), 0))
        gloss = np.clip(1.0 - 0.75 * np.clip(self.mud, 0, 1) - 0.5 * self.dust - 0.45 * self.crevice
                        - 0.35 * self.streaks - 0.6 * self.soot + 0.6 * self.oil + 0.5 * self.oil_film, 0.05, 1.99)
        wet = np.clip(self.oil + self.oil_film * 0.7, 0, 1)
        values = np.stack([bare * 255, gloss * 128, wet * 255], axis=1)
        image = self.to_image(values, 3)
        result = Image.fromarray(np.clip(image, 0, 255).astype(np.uint8), 'RGB')
        result = result.resize((self.resolution // 2, self.resolution // 2), Image.LANCZOS)
        result.save(os.path.join(MODEL_DIR, f'{self.name}_wear.tga'))

    def write_bump(self):
        """<часть>_bump.tga - наклоны рельефа по осям атласа: r - вдоль u (столбцы), g - вдоль v
        (строки); 128 - ровно, 128 +- 127 - +-BUMP_RANGE единиц модели на тексель. В <часть>_bump.txt -
        этот предел в единицах Quake: make_material_pass.py пишет его в шейдер (materialBump),
        движок наклоняет нормаль в каждом пикселе (tr_glsl.c). Без DETAILED - нет файла."""
        if not DETAILED:
            return
        image = self.to_image(self.height, 1)[..., 0]
        slope_u = np.zeros_like(image); slope_v = np.zeros_like(image)
        slope_u[:, 1:-1] = (image[:, 2:] - image[:, :-2]) * 0.5
        slope_v[1:-1, :] = (image[2:, :] - image[:-2, :]) * 0.5
        clipped = np.mean((np.abs(slope_u) > BUMP_RANGE) | (np.abs(slope_v) > BUMP_RANGE))
        red = 128 + 127 * np.clip(slope_u / BUMP_RANGE, -1, 1)
        green = 128 + 127 * np.clip(slope_v / BUMP_RANGE, -1, 1)
        pixels = np.stack([red, green, np.full_like(red, 128)], axis=-1)
        Image.fromarray(np.round(pixels).astype(np.uint8), 'RGB').save(os.path.join(MODEL_DIR, f'{self.name}_bump.tga'))
        q3_scale = self.document.get('asset', {}).get('extras', {}).get('q3_scale', 1.0)
        with open(os.path.join(MODEL_DIR, f'{self.name}_bump.txt'), 'w', encoding='ascii') as file:
            file.write(f'{BUMP_RANGE * q3_scale:.6f}\n')
        print(f'  {self.name}: bump map {image.shape[1]}x{image.shape[0]}, clipped slopes {clipped:.4f}')

    def rewrite_glb(self):
        """новые вершины: пара (вершина атласа, исходная вершина); примитивы индексированные."""
        document = self.document
        old_binary = self.binary
        mesh_accessors = set()
        for mesh in document['meshes']:
            for primitive in mesh['primitives']:
                mesh_accessors.update(primitive['attributes'].values())
                if 'indices' in primitive:
                    mesh_accessors.add(primitive['indices'])
        new_binary = bytearray()
        new_views, new_accessors, accessor_remap, view_remap = [], [], {}, {}

        def add_view(data, target=None):
            new_binary.extend(b'\0' * (-len(new_binary) % 4))
            view = {'buffer': 0, 'byteOffset': len(new_binary), 'byteLength': len(data)}
            if target:
                view['target'] = target
            new_binary.extend(data)
            new_views.append(view)
            return len(new_views) - 1

        for index, info in enumerate(document['accessors']):
            if index in mesh_accessors:
                continue
            view_index = info['bufferView']
            if view_index not in view_remap:
                view = document['bufferViews'][view_index]
                start = view.get('byteOffset', 0)
                data = old_binary[start:start + view['byteLength']]
                view_remap[view_index] = add_view(data)
                if 'byteStride' in view:
                    new_views[view_remap[view_index]]['byteStride'] = view['byteStride']
            copy = dict(info)
            copy['bufferView'] = view_remap[view_index]
            accessor_remap[index] = len(new_accessors)
            new_accessors.append(copy)

        def add_accessor(array, kind, target, component=5126):
            array = np.ascontiguousarray(array)
            view = add_view(array.tobytes(), target)
            info = {'bufferView': view, 'componentType': component, 'count': len(array), 'type': kind}
            if kind == 'VEC3' and component == 5126:
                info['min'] = array.min(axis=0).tolist()
                info['max'] = array.max(axis=0).tolist()
            new_accessors.append(info)
            return len(new_accessors) - 1

        for primitive_info in self.primitives:
            first, count = primitive_info['first_face'], primitive_info['faces']
            atlas_corners = self.atlas_faces[first:first + count].reshape(-1)
            original_corners = (self.faces[first:first + count] - primitive_info['first_vertex']).reshape(-1)
            pairs = np.stack([atlas_corners, original_corners], axis=1)
            unique, inverse = np.unique(pairs, axis=0, return_inverse=True)
            inverse = inverse.reshape(-1)
            positions = primitive_info['local'][unique[:, 1]].astype(np.float32)
            normals = primitive_info['normal'][unique[:, 1]].astype(np.float32)
            uvs = self.atlas_uv[unique[:, 0]].astype(np.float32)
            indices = inverse.astype(np.uint16 if len(unique) < 65536 else np.uint32)
            primitive = document['meshes'][primitive_info['mesh']]['primitives'][primitive_info['primitive']]
            primitive['attributes'] = {
                'POSITION': add_accessor(positions, 'VEC3', 34962),
                'NORMAL': add_accessor(normals, 'VEC3', 34962),
                'TEXCOORD_0': add_accessor(uvs, 'VEC2', 34962),
            }
            primitive['indices'] = add_accessor(indices, 'SCALAR', 34963, 5123 if indices.dtype == np.uint16 else 5125)
            primitive.pop('mode', None)

        for animation in document.get('animations', []):
            for sampler in animation['samplers']:
                sampler['input'] = accessor_remap[sampler['input']]
                sampler['output'] = accessor_remap[sampler['output']]
        for skin in document.get('skins', []):
            if 'inverseBindMatrices' in skin:
                skin['inverseBindMatrices'] = accessor_remap[skin['inverseBindMatrices']]
        document['accessors'] = new_accessors
        document['bufferViews'] = new_views
        document['buffers'] = [{'byteLength': len(new_binary)}]
        # все материалы смотрят на атлас окраски по умолчанию (настоящий выбор делают .skin)
        document['images'] = [{'uri': f'{self.name}_default.jpg', 'mimeType': 'image/jpeg'}]
        document['textures'] = [{'source': 0}]
        document.pop('samplers', None)
        for material in document['materials']:
            material.setdefault('pbrMetallicRoughness', {})['baseColorTexture'] = {'index': 0}
            material.setdefault('extras', {})['q3_shader'] = f'{SHADER_PREFIX}/{self.name}_{material_class(material["name"])}_default'
        write_glb(self.path, document, new_binary)


# ================================================================ общие помощники

TEXTURE_CACHE = {}


def load_texture(name):
    if name not in TEXTURE_CACHE:
        path = os.path.join(MODEL_DIR, name + '.jpg')
        TEXTURE_CACHE[name] = np.array(Image.open(path).convert('RGB')).astype(float)
    return TEXTURE_CACHE[name]


def bilinear(image, uv):
    height, width = image.shape[:2]
    x = uv[:, 0] * width - 0.5
    y = uv[:, 1] * height - 0.5
    x0 = np.floor(x).astype(np.int64); y0 = np.floor(y).astype(np.int64)
    fx = (x - x0)[:, None]; fy = (y - y0)[:, None]
    x0 %= width; y0 %= height
    x1 = (x0 + 1) % width; y1 = (y0 + 1) % height
    top = image[y0, x0] * (1 - fx) + image[y0, x1] * fx
    bottom = image[y1, x0] * (1 - fx) + image[y1, x1] * fx
    return top * (1 - fy) + bottom * fy


def read_skin(path):
    mapping = {}
    with open(path, encoding='ascii') as file:
        for line in file:
            if ',' in line:
                surface, shader = line.strip().split(',', 1)
                if shader:
                    mapping[surface] = shader
    return mapping


def material_class(name):
    # полированные штоки (28.11.2026): свой шейдер - как у полированного цилиндра, с какой
    # стороны ни смотри: середина, обращённая к зрителю, яркая, к краям плавно темнее
    # (слои из rod_shader.py, общие для штоков всех роботов); в общий проход материалов (make_material_pass.py) не входит
    if name in HYDRAULIC:
        return 'polished'
    if name in GLOWING:
        return 'glow'
    if name in GLASS:
        return 'glass'
    if name in METAL:
        return 'metal'
    return 'paint'


def write_skins(parts):
    for part in parts:
        for scheme in SCHEMES:
            path = os.path.join(MODEL_DIR, f'{part.name}_{scheme}.skin')
            lines = []
            with open(path, encoding='ascii') as file:
                for line in file:
                    surface = line.strip().split(',', 1)[0]
                    if not surface:
                        continue
                    if surface.startswith('tag_'):
                        lines.append(f'{surface},')
                    else:
                        lines.append(f'{surface},{SHADER_PREFIX}/{part.name}_{material_class(surface)}_{scheme}')
            with open(path, 'w', encoding='ascii', newline='\n') as file:
                file.write('\n'.join(lines) + '\n')


def write_shaders(parts):
    blocks = ['// WALKER robot materials, generated by walker/tools/bake_walker.py',
              '// one baked atlas per part and paint scheme; the material class picks the reflection']
    for part in parts:
        for scheme in SCHEMES:
            atlas = f'{SHADER_PREFIX}/{part.name}_{scheme}.jpg'
            base = f'{SHADER_PREFIX}/{part.name}'
            blocks.append(f"""{base}_paint_{scheme}
{{
	{{
		map {atlas}
		rgbGen lightingDiffuse
	}}
	{{
		map {ENV_SHINE}
		tcGen environment
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen const ( 0.40 0.40 0.40 )
		alphaGen lightingSpecular
	}}
}}
{base}_metal_{scheme}
{{
	{{
		map {atlas}
		rgbGen lightingDiffuse
	}}
	{{
		map {ENV_SHINE}
		tcGen environment
		blendFunc GL_DST_COLOR GL_ONE
		rgbGen const ( 0.55 0.55 0.55 )
	}}
	{{
		map {ENV_SHINE}
		tcGen environment
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen const ( 0.55 0.55 0.55 )
		alphaGen lightingSpecular
	}}
}}
{base}_glass_{scheme}
{{
	{{
		map {atlas}
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen lightingDiffuse
		alphaGen const {GLASS_OPACITY}
	}}
	{{
		map {ENV_SHINE}
		tcGen environment
		blendFunc GL_ONE GL_ONE
		rgbGen const ( {GLASS_REFLECTION} {GLASS_REFLECTION} {GLASS_REFLECTION} )
	}}
	{{
		map {ENV_GLINT}
		tcGen environment
		blendFunc GL_ONE GL_ONE
	}}
}}
{base}_glow_{scheme}
{{
	{{
		map {atlas}
		rgbGen wave sin 0.9 0.08 0 0.4
	}}
}}
{base}_polished_{scheme}
{{
	{{
		map {atlas}
		rgbGen lightingDiffuse
	}}
{ROD_STAGES}}}""")
    path = os.path.join(DATA_ROOT, 'scripts', f'{ROBOT}_player.shader')
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='ascii', newline='\n') as file:
        file.write('\n'.join(blocks) + '\n')


def main():
    parts = []
    for name, resolution in PARTS.items():
        print('part', name)
        part = Part(name, resolution)
        part.unwrap()
        part.rasterize()
        part.ambient_occlusion()
        part.edges()
        parts.append(part)
    # общая высота для грязи: ноги внизу, корпус и голова выше
    lower = parts[0]
    ground = lower.positions[:, 1].min()
    knee = ground + (lower.positions[:, 1].max() - ground) * 0.55
    for part in parts:
        part.height_range = (ground, knee) if part.name == 'lower' else (part.positions[:, 1].min(), part.positions[:, 1].max())
        part.mud_level = 0.85 if part.name == 'lower' else 0.25
        part.masks()
        for scheme in SCHEMES:
            part.write_atlas(scheme, part.compose(scheme))
        part.write_wear()
        part.write_bump()
    used = set()
    for part in parts:
        for scheme in SCHEMES:
            used.update(os.path.basename(shader) for shader in read_skin(os.path.join(MODEL_DIR, f'{part.name}_{scheme}.skin')).values())
    for part in parts:
        part.rewrite_glb()
    write_skins(parts)
    write_shaders(parts)
    # исходные текстуры материалов больше не нужны: всё в атласах
    for name in used:
        path = os.path.join(MODEL_DIR, name + '.jpg')
        if os.path.exists(path):
            os.remove(path)
    print('baked:', ', '.join(f'{part.name} {part.resolution}' for part in parts), 'x', len(SCHEMES), 'schemes')


main()
