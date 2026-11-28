"""Общая модель полированного штока для всех роботов WALKER (28.11.2026).

Шток гидроцилиндра у любого робота рисуется одинаково: середина, обращённая к зрителю,
светлая, к краям цилиндр плавно темнеет, по центру светлой полосы узкий яркий блик. Свет
сцены на эти слои не влияет — только угол между нормалью и направлением на зрителя
(alphaGen viewFacing <минимум> <степень>, renderer/tr_shade_calc.c).

Слои после основного (текстура с rgbGen lightingDiffuse):
  1. затемнение краёв: умножение на минимум + (1 - минимум) * cos^степень;
  2. мягкий блеск: сложение, цвет SHEEN_LEVEL, cos^SHEEN_EXPONENT;
  3. яркий блик: сложение, цвет GLINT_LEVEL, cos^GLINT_EXPONENT.

Геометрия штока: гладкий цилиндр с радиальными нормалями, не меньше ROD_MIN_SIDES граней
(иначе полоса блика дробится по граням). Используют bake_walker.py (K7, класс polished)
и make_mech_test.py (mech); новые роботы со штоками берут слои отсюда же.
"""

EDGE_MINIMUM = 0.1
EDGE_EXPONENT = 1.6
SHEEN_LEVEL = 0.55
SHEEN_EXPONENT = 10     # половина яркости при отклонении 21 градус от направления на зрителя
GLINT_LEVEL = 0.9
GLINT_EXPONENT = 20     # половина яркости при 15 градусах
ROD_MIN_SIDES = 24


def rod_stages():
    """слои шейдера после основного, с табуляцией как в .shader"""
    return (f'\t{{\n\t\tmap $whiteimage\n\t\tblendFunc GL_ZERO GL_SRC_ALPHA\n'
            f'\t\talphaGen viewFacing {EDGE_MINIMUM:g} {EDGE_EXPONENT:g}\n\t}}\n'
            f'\t{{\n\t\tmap $whiteimage\n\t\tblendFunc GL_SRC_ALPHA GL_ONE\n'
            f'\t\trgbGen const ( {SHEEN_LEVEL:g} {SHEEN_LEVEL:g} {SHEEN_LEVEL - 0.01:g} )\n'
            f'\t\talphaGen viewFacing 0 {SHEEN_EXPONENT:g}\n\t}}\n'
            f'\t{{\n\t\tmap $whiteimage\n\t\tblendFunc GL_SRC_ALPHA GL_ONE\n'
            f'\t\trgbGen const ( {GLINT_LEVEL:g} {GLINT_LEVEL:g} {GLINT_LEVEL - 0.02:g} )\n'
            f'\t\talphaGen viewFacing 0 {GLINT_EXPONENT:g}\n\t}}\n')
