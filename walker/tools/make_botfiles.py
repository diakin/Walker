"""Bot files for WALKER: our own characters, weapon and item configuration, chat lines.

python3 walker/tools/make_botfiles.py <output folder> <path to the repository code folder>

Writes botfiles/ (read by the botlib), scripts/bots.txt (bot list for the menu and addbot)
and two extra paint schemes of the walker model (desert, urban) so the bots look different.
The header files chars.h and inv.h are copied from code/game (GPL).
"""
import os, shutil, sys
import numpy as np
from PIL import Image

OUT = sys.argv[1]
CODE = sys.argv[2]
MODEL = os.path.join(os.path.dirname(CODE), 'assets', 'baseq3', 'models', 'players', 'walker')


def write(path, text):
    full = os.path.join(OUT, path)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, 'w', encoding='ascii', newline='\n') as file:
        file.write(text)


# name, paint scheme, gender, personality: aggression, self preservation, camper, alertness,
# base accuracy (skill 1 -> skill 5 interpolated from it), reaction time at skill 1
BOTS = [
    ('Anvil',   'default', 'it', 0.85, 0.30, 0.10, 0.60, 0.45, 2.0),
    ('Bastion', 'urban',   'it', 0.55, 0.70, 0.45, 0.80, 0.55, 2.2),
    ('Cinder',  'desert',  'it', 0.95, 0.20, 0.05, 0.50, 0.40, 1.8),
    ('Drake',   'default', 'it', 0.70, 0.45, 0.20, 0.70, 0.50, 2.0),
    ('Ember',   'desert',  'it', 0.80, 0.35, 0.15, 0.65, 0.48, 1.9),
    ('Flint',   'urban',   'it', 0.60, 0.60, 0.35, 0.75, 0.52, 2.1),
    ('Granite', 'default', 'it', 0.65, 0.55, 0.30, 0.70, 0.50, 2.3),
    ('Hornet',  'desert',  'it', 0.90, 0.25, 0.05, 0.55, 0.42, 1.7),
    ('Jackal',  'urban',   'it', 0.75, 0.40, 0.20, 0.65, 0.47, 1.9),   # пятый в команде для захвата маяков
]

CHAT_LINES = {
    'game_enter': ['Unit online.', 'Systems green. Moving out.', 'Reactor at full output.'],
    'game_exit': ['Returning to hangar.', 'Unit offline.'],
    'level_start': ['Weapons free.', 'Calibrating targeting array.'],
    'level_end': ['Engagement over.', 'Logging combat data.'],
    'level_end_victory': ['Victory recorded.', 'Field secured.'],
    'level_end_lose': ['Damage report pending.', 'Next time.'],
    'hit_talking': ['Not while I am transmitting!'],
    'kill_insult': ['Scrap metal.', 'Target neutralized.', 'Send a recovery crew.'],
    'kill_praise': ['Clean shot.'],
    'death_insult': ['Armor breached...', 'Lucky round.'],
    'death_praise': ['Good shooting.'],
    'random_misc': ['Servo temperature nominal.', 'Water in the left knee again.', 'Scanning the islands.'],
}

# the AI asks for these chat types; empty lists are fine, but every type must parse
EMPTY_TYPES = ['death_telefrag', 'death_cratered', 'death_lava', 'death_slime', 'death_drown', 'death_suicide',
               'death_gauntlet', 'death_rail', 'death_bfg', 'death_kamikaze', 'kill_gauntlet', 'kill_rail',
               'kill_telefrag', 'kill_suicide', 'kill_kamikaze', 'enemy_suicide', 'hit_nodeath', 'hit_nokill',
               'random_insult', 'level_end', 'game_enter', 'game_exit']


def fmt_float(value):
    return f'{value:.3f}'


def character(name, gender, aggression, preservation, camper, alertness, accuracy, reaction):
    blocks = []
    for skill in (1, 4, 5):
        level = {1: 0.0, 4: 0.75, 5: 1.0}[skill]
        aim = min(0.95, accuracy + (0.92 - accuracy) * level)
        values = {
            0: f'"{name}"', 1: f'"{gender}"',
            2: fmt_float(0.3 + 0.65 * level),               # attack skill
            3: '"bots/walker_w.c"',
            4: fmt_float(0.35 + 0.6 * level),               # view factor
            5: fmt_float(90 + 270 * level),                 # view max change
            6: fmt_float(max(0.0, reaction * (1 - level))),
            7: fmt_float(aim), 8: fmt_float(aim), 9: fmt_float(aim * 0.8),
            10: fmt_float(aim * 0.8), 11: fmt_float(aim * 0.8), 12: fmt_float(aim * 0.8),
            13: fmt_float(aim * 0.8), 14: fmt_float(aim * 0.8), 15: fmt_float(aim * 0.8),
            16: fmt_float(0.3 + 0.6 * level), 17: fmt_float(0.3 + 0.6 * level), 18: fmt_float(0.3 + 0.6 * level),
            19: fmt_float(0.3 + 0.6 * level), 20: fmt_float(0.3 + 0.6 * level),
            21: '"bots/walker_t.c"', 22: f'"{name}"',
            23: '400',                                      # chat characters per minute (integer)
            24: fmt_float(0.2), 25: fmt_float(0.2), 26: fmt_float(0.3), 27: fmt_float(0.3),
            28: fmt_float(0.2), 29: fmt_float(0.2), 30: fmt_float(0.1), 31: fmt_float(0.1),
            32: fmt_float(0.1), 33: fmt_float(0.1), 34: fmt_float(0.05), 35: fmt_float(0.0),
            36: fmt_float(0.0),                             # croucher: robots do not crouch
            37: fmt_float(0.0),                             # jumper: robots do not jump
            38: fmt_float(0.0), 39: fmt_float(0.0),
            40: '"bots/walker_i.c"',
            41: fmt_float(aggression), 42: fmt_float(preservation), 43: fmt_float(0.5 + 0.3 * aggression),
            44: fmt_float(camper), 45: fmt_float(0.5), 46: fmt_float(alertness * (0.7 + 0.3 * level)),
            47: fmt_float(0.1 + 0.2 * (1 - level)),         # fire throttle
            48: fmt_float(0.0),                             # walker: no slow walking
        }
        lines = [f'skill {skill}', '{']
        lines += [f'\t{index}\t{value}' for index, value in sorted(values.items())]
        lines.append('}')
        blocks.append('\n'.join(lines))
    return '// WALKER bot character, generated by walker/tools/make_botfiles.py\n' + '\n\n'.join(blocks) + '\n'


def chat_file():
    parts = ['// WALKER bot chat, generated by walker/tools/make_botfiles.py']
    for name, *_ in BOTS:
        parts.append(f'chat "{name}"')
        parts.append('{')
        types = dict(CHAT_LINES)
        for empty in EMPTY_TYPES:
            types.setdefault(empty, [])
        for chat_type, lines in types.items():
            parts.append(f'\ttype "{chat_type}"')
            parts.append('\t{')
            for line in lines:
                parts.append(f'\t\t"{line}";')
            parts.append('\t}')
        parts.append('}')
    return '\n'.join(parts) + '\n'


WEAPONS = """// WALKER bot weapon configuration: one weapon, the autocannon (game weapon WP_MACHINEGUN)
#include "inv.h"

projectileinfo
{
	name			"autocannon_shell"
	damage			7
	damagetype		1
}

weaponinfo
{
	name			"Autocannon"
	number			2
	projectile		"autocannon_shell"
	numprojectiles	1
	hspread			1
	vspread			1
	speed			0
	weaponindex		INVENTORY_MACHINEGUN
	ammoindex		INVENTORY_BULLETS
	ammoamount		1
	reload			0.1
}
"""

ITEMS = """// WALKER bot item configuration (the maps have no pickups; only the cannon is listed)
#include "inv.h"

iteminfo "weapon_machinegun"
{
	name		"Autocannon"
	model		"models/weapons2/machinegun/machinegun.md3"
	modelindex	MODELINDEX_MACHINEGUN
	type		1
	index		INVENTORY_MACHINEGUN
	respawntime	30
	mins		{-15,-15,-15}
	maxs		{15,15,15}
}
"""

WEAPON_WEIGHTS = """// WALKER bot weapon weights
#include "inv.h"

weight "Autocannon"
{
	return 100;
}
"""

ITEM_WEIGHTS = """// WALKER bot item weights
#include "inv.h"

weight "Autocannon"
{
	return 10;
}
"""


def paint_schemes():
    """desert and urban variants of the walker skins (textures recolored from the default ones)."""
    schemes = {
        'desert': {'tint': np.array([1.22, 1.02, 0.70]), 'gray': 0.55, 'lift': 18},
        'urban': {'tint': np.array([0.92, 0.96, 1.04]), 'gray': 1.0, 'lift': 8},
    }
    target = os.path.join(OUT, 'models', 'players', 'walker')
    os.makedirs(target, exist_ok=True)
    for scheme, setup in schemes.items():
        for texture in ('camo_green', 'camo_dark', 'marking'):
            pixels = np.array(Image.open(os.path.join(MODEL, texture + '.jpg')).convert('RGB')).astype(float)
            luminance = pixels.mean(axis=2, keepdims=True)
            mixed = pixels * (1 - setup['gray']) + luminance * setup['gray']
            result = np.clip(mixed * setup['tint'] + setup['lift'], 0, 255).astype(np.uint8)
            Image.fromarray(result).save(os.path.join(target, f'{texture}_{scheme}.jpg'), quality=90)
        for part in ('head', 'upper', 'lower'):
            with open(os.path.join(MODEL, f'{part}_default.skin'), encoding='ascii') as file:
                text = file.read()
            for texture in ('camo_green', 'camo_dark', 'marking'):
                text = text.replace(f'models/players/walker/{texture}\n', f'models/players/walker/{texture}_{scheme}\n')
            with open(os.path.join(target, f'{part}_{scheme}.skin'), 'w', encoding='ascii', newline='\n') as file:
                file.write(text)
        icon = np.array(Image.open(os.path.join(MODEL, 'icon_default.tga')).convert('RGBA')).astype(float)
        luminance = icon[..., :3].mean(axis=2, keepdims=True)
        icon[..., :3] = np.clip((icon[..., :3] * (1 - setup['gray']) + luminance * setup['gray']) * setup['tint'] + setup['lift'], 0, 255)
        Image.fromarray(icon.astype(np.uint8), 'RGBA').save(os.path.join(target, f'icon_{scheme}.tga'))


def model_shaders():
    """картинка окружения env_shine.jpg для бликов и отражений на роботе (tcGen environment).
    Сами материалы робота (scripts/walker_player.shader) пишет bake_walker.py."""
    target = os.path.join(OUT, 'models', 'players', 'walker')
    os.makedirs(target, exist_ok=True)
    # светлое небо сверху, тёмная земля снизу, пятно солнца
    size = 128
    y, x = np.mgrid[0:size, 0:size] / (size - 1)
    sky = np.clip(1.2 - y * 1.6, 0, 1)
    sun = np.exp(-(((x - 0.35) ** 2 + (y - 0.25) ** 2) / 0.012))
    level = np.clip(0.15 + 0.55 * sky + 0.9 * sun, 0, 1)
    image = np.stack([level * 235, level * 240, level * 250], axis=-1)
    Image.fromarray(image.astype(np.uint8)).save(os.path.join(target, 'env_shine.jpg'), quality=92)
    # блик стекла кабины (28.11.2026): на чёрном только точка, маленькая и резкая, с узким
    # ореолом (складывается со стеклом: чёрное - ничего). Чуть выше середины карты: так
    # колпак, смотрящий вперёд и вверх, ловит её с обычных углов
    size = 256
    y, x = np.mgrid[0:size, 0:size] / (size - 1)
    distance = ((x - 0.45) ** 2 + (y - 0.22) ** 2)
    level = np.clip(np.exp(-distance / 0.0003) + 0.3 * np.exp(-distance / 0.003), 0, 1)
    image = np.stack([level * 255, level * 252, level * 245], axis=-1)
    Image.fromarray(image.astype(np.uint8)).save(os.path.join(target, 'env_glint.jpg'), quality=95)


def main():
    for header in ('chars.h', 'inv.h'):
        source = os.path.join(CODE, 'game', header)
        os.makedirs(os.path.join(OUT, 'botfiles'), exist_ok=True)
        shutil.copyfile(source, os.path.join(OUT, 'botfiles', header))
    write('botfiles/weapons.c', WEAPONS)
    write('botfiles/items.c', ITEMS)
    for empty in ('syn.c', 'rnd.c', 'match.c', 'rchat.c'):
        write(f'botfiles/{empty}', '// WALKER: no chat matching, synonyms or reply chats yet\n')
    write('botfiles/bots/walker_w.c', WEAPON_WEIGHTS)
    write('botfiles/bots/walker_i.c', ITEM_WEIGHTS)
    write('botfiles/bots/walker_t.c', chat_file())
    write('botfiles/bots/default_c.c', character('default', 'it', 0.7, 0.4, 0.2, 0.7, 0.5, 2.0))
    entries = []
    for name, scheme, gender, aggression, preservation, camper, alertness, accuracy, reaction in BOTS:
        file_name = f'bots/{name.lower()}_c.c'
        write('botfiles/' + file_name, character(name, gender, aggression, preservation, camper, alertness, accuracy, reaction))
        model = 'walker' if scheme == 'default' else f'walker/{scheme}'
        entries.append('{\nname "%s"\nmodel "%s"\nheadmodel "%s"\naifile "%s"\n}\n' % (name, model, model, file_name))
    write('scripts/bots.txt', '// WALKER bots, generated by walker/tools/make_botfiles.py\n' + ''.join(entries))
    paint_schemes()
    model_shaders()
    print('bots:', len(BOTS))


main()
