#!/usr/bin/env python3
"""Генератор именованной карты cCharParamEnemy для рантайма.

ЗАЧЕМ. Блок `cCharParamEnemy` (320 B) лежит в теле гоблина ДВАЖДЫ:
`+0x5870` и `+0x59B0`. Это загруженный в память файл `em0100_cmn.prp`,
у которого мы знаем схему: 72 поля с японскими именами. До сих пор в
логах эти байты выглядели как безымянные смещения — а на деле каждое
из них подписано.

Особый интерес для охоты за темпом:

    +0x0050  耐のろま               сопротивление ТОРПОРУ
    +0x0120  拘束スローレート Lv1    множитель замедления при захвате
    +0x0124  拘束スローレート Lv2
    +0x0128  拘束スローレート Ex

Три «слоу-рейта» доказывают, что понятие «множитель замедления существа»
у движка есть и хранится в данных особи. Причём в файле они равны 1.0,
а движок в рантайме их ОБНУЛЯЕТ (замечено в EnemyTuner при подборе
сигнатуры) — то есть это живые поля, а не константы ресурса.

ВЫВОД: `src/CharParamEnemy.Generated.h` — таблица {смещение, тип, имя}
только в ASCII: имена печатаются в лог и в панель ImGui, а кириллицу и
кандзи дефолтный шрифт рисует как '?'.

Запуск:
    python3 tools/prp_to_header.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xfs_dump

PRP = 'resources/extracted_assets/em0100/charparam/em/em0100_cmn.prp'
OUT = 'src/CharParamEnemy.Generated.h'

# Японское имя -> ASCII. Смысл сохранён, длина ограничена: строки идут
# в лог столбиком.
NAMES = {
    '経験値': 'exp',
    '攻撃力': 'attack',
    '防御力': 'defense',
    '魔法攻撃力': 'magick_attack',
    '魔法防御力': 'magick_defense',
    '体重': 'weight',
    '大きさ': 'size_class',
    '拘束タイプ': 'restraint_type',
    '投げられ挙動': 'thrown_behaviour',
    '風圧ダメージ': 'wind_pressure_dmg',
    '地面揺れ有効': 'ground_shake',
    'ヒュージブル無効': 'hugeable_disabled',
    'その場拘束タイプ': 'inplace_restraint_type',
    '耐炎': 'res_fire',
    '耐氷': 'res_ice',
    '耐雷': 'res_thunder',
    '耐聖': 'res_holy',
    '耐魔': 'res_dark',
    '耐斬': 'res_slash',
    '耐打': 'res_strike',
    '耐毒': 'res_poison',
    '耐のろま': 'res_TORPOR',
    '耐暗闇': 'res_blind',
    '耐睡眠': 'res_sleep',
    '耐油まみれ': 'res_tarred',
    '耐ずぶ濡れ': 'res_drenched',
    '耐敵化': 'res_possession',
    '耐沈黙': 'res_silence',
    '耐スキル封印': 'res_skill_stifling',
    '耐呪い': 'res_curse',
    '耐延焼': 'res_burning',
    '耐氷漬け': 'res_frozen',
    '耐落雷': 'res_thunderstruck',
    '耐久聖': 'res_holy_ex',
    '耐久魔': 'res_dark_ex',
    '耐石化': 'res_petrify',
    '耐攻撃力ダウン': 'res_atk_down',
    '耐防御力ダウン': 'res_def_down',
    '耐魔法攻撃力ダウン': 'res_matk_down',
    '耐魔法防御力ダウン': 'res_mdef_down',
    'のけぞりガード': 'flinch_guard',
    'ぶっとびガード': 'knockdown_guard',
    'ContType': 'cont_type',
    'JointType': 'joint_type',
    'Offset': 'offset',
    '基点タイプ': 'base_point_type',
    '参照関節番号': 'ref_joint_index',
    '基点オフセット': 'base_point_offset',
    '経路サイズ': 'path_size',
    'OBJ補正サイズ': 'obj_correct_size',
    '吸い込みサイズ': 'suction_size',
    '人間敵 HP': 'human_hp',
    '人間敵 のけぞり耐久値': 'human_flinch_endur',
    '人間敵 ぶっ飛び耐久値': 'human_knockdown_endur',
    '近距離カメラ番号': 'cam_near_index',
    '近距離カメラになる距離': 'cam_near_dist',
    '中距離カメラ番号': 'cam_mid_index',
    '中距離カメラになる距離': 'cam_mid_dist',
    '遠距離カメラ番号': 'cam_far_index',
    '遠距離カメラになる距離': 'cam_far_dist',
    'リターンテリトリー発動タイム': 'return_activate_time',
    'リターンテリトリー継続タイム': 'return_duration_time',
    '落下になる高さ': 'fall_height',
    'ダメージを受ける高さ': 'fall_damage_height',
    '一撃死になる高さ': 'fall_death_height',
    'ダメージ値(最大体力の指定％)': 'fall_damage_pct',
    '死亡沈み時、死体状態になる距離': 'sink_corpse_dist',
    '沈み中にブクブク SE KEY_OFF する距離': 'sink_se_off_dist',
    '拘束スローレート Lv1': 'RESTRAINT_SLOW_LV1',
    '拘束スローレート Lv2': 'RESTRAINT_SLOW_LV2',
    '拘束スローレート Ex': 'RESTRAINT_SLOW_EX',
    'スケール値': 'scale',
}


def main():
    obj = xfs_dump.parse(PRP)
    cls = obj['classes'][0]
    props = cls['props']

    # РАНТАЙМ-РАСКЛАДКА != РАСКЛАДКА ФАЙЛА.
    #
    # Первый живой дамп это доказал. Сверка по характерным значениям
    # (exp 65, attack 250, 耐魔 0.6, 耐敵化 10000, базовая точка 15/-20/0,
    # камеры 500/800/1200) дала три зоны:
    #
    #   +0x00  указатель на vtable       - объект MtObject, а не голые данные
    #   +0x04  целое 1                   - служебное поле
    #   +0x08 … +0xA0   = файл + 8       - проверено 30 полями подряд
    #   +0xA4 … +0xBC   расходится       - в рантайме здесь указатель, а
    #                                      двух «гардов» из файла нет
    #   +0xC0 … +0x12F  = файл           - vector3 выровнялся по 16 и
    #                                      «съел» те самые 8 байт
    #
    # Поэтому смещения считаем по зонам, а спорную середину не подписываем
    # вовсе: лучше дырка в карте, чем ложная подпись.
    SHIFT_END = 0x9C     # файл: первое поле расходящейся зоны (のけぞりガード)
    SAME_FROM = 0x00C0   # файл: 基点オフセット и дальше — один в один

    rows = []
    unknown = []
    skipped = []
    for p in props:
        fo = p['packedOffset']
        if fo < SHIFT_END:
            mo = fo + 8
        elif fo >= SAME_FROM:
            mo = fo
        else:
            skipped.append((fo, p['name']))
            continue
        name = NAMES.get(p['name'])
        if not name:
            name = 'field_%03X' % fo
            unknown.append(p['name'])
        rows.append((mo, p['type'], p['bytes'], name))

    lines = []
    a = lines.append
    a('// CharParamEnemy.Generated.h - AUTOGENERATED from %s' % PRP)
    a('// python3 tools/prp_to_header.py')
    a('// Не редактировать руками: правь таблицу имён в генераторе.')
    a('#pragma once')
    a('#include <stdint.h>')
    a('')
    a('// cCharParamEnemy - загруженный в память em0100_cmn.prp (320 B).')
    a('// В теле uEm0100 лежит ДВУМЯ копиями подряд: +0x5870 и +0x59B0.')
    a('// Имена только ASCII: они идут в лог и в панель ImGui.')
    a('namespace CharParamEnemy {')
    a('')
    a('static const uint32_t kSize      = %d;   // sizeof структуры' % cls['sizeof'])
    a('static const uint32_t kCopy0Off  = 0x5870; // первая копия в теле uEm0100')
    a('static const uint32_t kCopy1Off  = 0x59B0; // вторая (шаг = kSize)')
    a('')
    a('// Типы XFS, которые встречаются в этом классе.')
    a('enum FieldType { kF32 = 0, kU32 = 1, kS32 = 2, kBool = 3, kVec3 = 4, kOther = 5 };')
    a('')
    a('// off - смещение в РАНТАЙМ-структуре (не в файле!).')
    a('struct Field { uint16_t off; uint8_t type; uint8_t size; const char* name; };')
    a('')
    a('static const int kCount = %d;' % (len(rows) + 2))
    a('static const Field kFields[kCount] = {')
    a('    { 0x0000, kU32  ,  4, "_vtable" },')
    a('    { 0x0004, kU32  ,  4, "_flag" },')
    tmap = {'f32': 'kF32', 'u32': 'kU32', 's32': 'kS32', 'bool': 'kBool',
            'vector3': 'kVec3'}
    for off, t, size, name in rows:
        a('    { 0x%04X, %-6s, %2d, "%s" },' % (off, tmap.get(t, 'kOther'), size, name))
    a('};')
    a('')
    a('// Имя поля по смещению внутри структуры. Пустая строка = смещение')
    a('// не совпадает с началом поля (середина vector3, дырка выравнивания).')
    a('inline const char* LabelAt(uint32_t off)')
    a('{')
    a('    for (int i = 0; i < kCount; ++i)')
    a('        if (kFields[i].off == off) return kFields[i].name;')
    a('    return "";')
    a('}')
    a('')
    a('} // namespace CharParamEnemy')
    a('')

    with open(OUT, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))

    print('%s: %d полей (+2 служебных)' % (OUT, len(rows)))
    if skipped:
        print('пропущена расходящаяся зона (%d): %s'
              % (len(skipped), ', '.join('+0x%03X %s' % (o, n) for o, n in skipped)))
    if unknown:
        print('без перевода (%d): %s' % (len(unknown), ', '.join(unknown)))


if __name__ == '__main__':
    main()
