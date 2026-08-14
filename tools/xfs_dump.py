#!/usr/bin/env python3
"""XFS (MT Framework property container) schema dumper — DDDA edition.

Layout reverse-engineered 14.08.2026 from em0100 (Goblin) assets:

  0x00  char[4]  'XFS\0'
  0x04  u16      version   (0x0109 on DDDA)
  0x06  u8/u8    minor/flags
  0x08  u32      instanceCount   (root objects serialised in the data blob)
  0x0C  u32      classCount
  0x10  u32      dataOffset      (absolute; start of serialised values)
  0x14  u32[classCount]  classOffset[]   <-- RELATIVE to 0x14
  ...   class records:
          u32 hash        (MtDTI property-list hash of the C++ class)
          u32 sizeof      (sizeof of the runtime struct in bytes)
          u32 propCount
          propCount x 24 bytes:
            u32 nameOffset   <-- RELATIVE to 0x14
            u32 typeword     -> type = b0, attr = b1, fieldBytes = w >> 16
            u8[16] reserved  (always 0 in DDDA)
  ...   string table (ASCII, NUL-terminated)
  ...   data blob at dataOffset

Everything offset-like in the file is relative to 0x14 (end of the fixed
header) — that is the single trick that makes the format fall open.
"""
import struct, sys, json

TYPES = {
 0:'undefined',1:'class',2:'classref',3:'bool',4:'u8',5:'u16',6:'u32',7:'u64',
 8:'s8',9:'s16',10:'s32',11:'s64',12:'f32',13:'f64',14:'string',15:'color',
 16:'point',17:'size',18:'rect',19:'matrix44',20:'vector3',21:'vector4',
 22:'quaternion',23:'property',24:'event',25:'group',26:'pagebegin',
 27:'pageend',28:'event32',29:'array',30:'propertylist',31:'groupend',
 32:'cstring',33:'time',34:'float2',35:'float3',36:'float4',37:'float3x3',
 38:'float4x3',39:'float4x4',40:'easecurve',41:'line',42:'linesegment',
 43:'ray',44:'plane',45:'sphere',46:'capsule',47:'aabb',48:'obb',
 49:'cylinder',50:'triangle',51:'cone',52:'torus',53:'ellipsoid',54:'range',
 55:'rangef',56:'rangeu16',57:'hermitecurve',58:'enumlist',59:'float3x4',
 60:'linesegment4',61:'aabb4',62:'oscillator',63:'variable',64:'vector2',
 65:'matrix33',66:'rect3d_xz',67:'rect3d',68:'rect3d_collision',
 69:'plane_xz',70:'ray_y',71:'pointf',72:'sizef',73:'rectf',74:'event64',
}
BASE = 0x14

def cstr(d, off):
    e = d.find(b'\0', off)
    if e < 0:
        return ''
    b = d[off:e]
    for enc in ('ascii', 'utf-8', 'cp932', 'shift_jis'):
        try:
            return b.decode(enc)
        except UnicodeDecodeError:
            continue
    return b.decode('latin1')

def find_xfs(d):
    """Some containers (.prp) wrap XFS behind a small magic+hash header."""
    i = d.find(b'XFS\0')
    return i if i >= 0 else None

def parse(path):
    raw = open(path, 'rb').read()
    start = find_xfs(raw)
    if start is None:
        raise ValueError('%s: no XFS block' % path)
    d = raw[start:]
    ver, minor, flags = struct.unpack_from('<HBB', d, 4)
    ninst, nclass, dataOff = struct.unpack_from('<III', d, 8)
    o = {'file': path, 'fileSize': len(raw), 'xfsAt': start,
         'version': '0x%04X.%d.%d' % (ver, minor, flags),
         'instanceCount': ninst, 'classCount': nclass,
         'dataOffset': dataOff, 'classes': []}
    offs = struct.unpack_from('<%dI' % nclass, d, BASE)
    for i, co in enumerate(offs):
        p = BASE + co
        h, size, np = struct.unpack_from('<III', d, p); p += 12
        cls = {'index': i, 'at': '0x%X' % (BASE + co), 'hash': '0x%08X' % h,
               'sizeof': size, 'propCount': np, 'props': []}
        rel = 0
        for j in range(np):
            noff, tw = struct.unpack_from('<II', d, p); p += 24
            t, attr, fsz = tw & 0xFF, (tw >> 8) & 0xFF, tw >> 16
            align = 16 if fsz >= 16 else (fsz if fsz in (1, 2, 4, 8) else 4)
            rel = (rel + align - 1) & ~(align - 1)
            cls['props'].append({'index': j, 'name': cstr(d, BASE + noff),
                                 'type': TYPES.get(t, 'type%d' % t), 'typeId': t,
                                 'attr': '0x%02X' % attr, 'bytes': fsz,
                                 'packedOffset': rel})
            rel += fsz
        cls['packedSize'] = rel
        o['classes'].append(cls)
    return o

def read_values(path, o=None):
    """Decode the serialised value blob. Layout per instance:
         u32 count, u32 hash, then for each property: u32 count + count*bytes.
       No padding anywhere: the stream is byte-tight."""
    o = o or parse(path)
    raw = open(path, 'rb').read()
    d = raw[o['xfsAt']:]
    p = BASE + o['dataOffset']
    inst = {'headerCount': struct.unpack_from('<I', d, p)[0],
            'headerHash': '0x%08X' % struct.unpack_from('<I', d, p + 4)[0],
            'values': []}
    p += 8
    for pr in o['classes'][0]['props']:
        cnt = struct.unpack_from('<I', d, p)[0]; p += 4
        n = pr['bytes']; blob = d[p:p + cnt * n]; p += cnt * n
        t = pr['type']
        def one(b):
            if t == 'f32':  return round(struct.unpack('<f', b[:4])[0], 6)
            if t in ('u32', 's32'): return struct.unpack('<i', b[:4])[0]
            if t in ('u16', 's16'): return struct.unpack('<h', b[:2])[0]
            if t in ('u8', 's8'):   return b[0]
            if t == 'bool': return bool(b[0])
            if t in ('vector3', 'vector4'):
                return [round(x, 4) for x in struct.unpack('<4f', b[:16])]
            if t in ('string', 'cstring'):
                return struct.unpack('<I', b[:4])[0]
            return b.hex()
        vals = [one(blob[i * n:(i + 1) * n]) for i in range(cnt)]
        inst['values'].append({'name': pr['name'], 'type': t, 'count': cnt,
                               'value': vals[0] if cnt == 1 else vals})
    inst['streamEnd'] = p
    inst['exact'] = (p == len(d))
    return inst


def report(path, quiet=False, values=False):
    o = parse(path)
    if quiet:
        return o
    print('=' * 78)
    print('%s  (%d B)  XFS@0x%X  ver %s  classes=%d instances=%d  data@0x%X' %
          (o['file'], o['fileSize'], o['xfsAt'], o['version'],
           o['classCount'], o['instanceCount'], o['dataOffset']))
    for c in o['classes']:
        print('\n-- class[%d] @%s hash %s  sizeof=%d  props=%d  packed=%d' %
              (c['index'], c['at'], c['hash'], c['sizeof'],
               c['propCount'], c['packedSize']))
        for pr in c['props']:
            print('   +0x%04X  %-24s %-9s %3dB  attr %s' %
                  (pr['packedOffset'], pr['name'], pr['type'],
                   pr['bytes'], pr['attr']))
    if values:
        try:
            v = read_values(path, o)
            print('\n-- VALUES  (stream end 0x%X, %s)' %
                  (v['streamEnd'], 'byte-exact' if v['exact'] else 'MISMATCH'))
            for e in v['values']:
                print('   %-26s %-8s %s' % (e['name'][:26], e['type'], e['value']))
            o['instance'] = v
        except Exception as ex:
            print('   values: %s' % ex)
    return o

if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    q = '--json' in sys.argv
    vals = '--values' in sys.argv
    out = []
    for a in args:
        try:
            o = report(a, quiet=q, values=vals)
            if q and vals:
                o['instance'] = read_values(a, o)
            out.append(o)
        except Exception as e:
            print('%s: %s' % (a, e), file=sys.stderr)
    if q:
        json.dump(out, sys.stdout, indent=1)
