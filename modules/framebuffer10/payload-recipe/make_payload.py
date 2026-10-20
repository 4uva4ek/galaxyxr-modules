"""Rebuild the Framebuffer10 compositor payload from YOUR stock library.

Author: 4uva4ek. Project-authored recipe; it contains no vendor code. The
output is a modified copy of Samsung's libvulkan_compositor.so: keep it local,
never redistribute it.

    python make_payload.py --stock libvulkan_compositor.so --tools DIR --out payload.so

--stock  /system/lib64/libvulkan_compositor.so from Galaxy XR I610UEU2AZF3
         (SHA256 54f9c32c...). Any other build is refused.
--tools  Directory with SPIRV-Tools executables spirv-as, spirv-dis, spirv-val
         (.exe suffix optional). Assembly is deterministic; the result must
         match PAYLOAD_SHA256, otherwise nothing is written.

The 10-bit framebuffer (A2B10G10R10_UNORM) does not sRGB-encode on store,
unlike the stock R8G8B8A8_SRGB target. Native code requests encoding with the
output flag 0x400000000, but seven fragment shaders ignore it, write linear
values or blend already-encoded values (dark, oversaturated output):

Two additional distortionmesh shaders encode before mask effects; this recipe
moves their encoding to the final output. Also repair 544 registered precompiled
variants that ignored the output flag (precompiled.json), for 553 slots total.
The 128 node/QUAD variants encode unassociated RGB and then reapply alpha,
preserving premultiplied output for blending into the encoded UNORM target.

  camera standalone (constants)  SpecId 16 missing
  camera standalone (uniforms)   VariantUniforms member 16 never read
  projection+camera merged       camera flag unused, projections encoded pre-mix
  two projections+camera merged  same, Lo/Hi projections
  AMRP reprojection (constants)  SpecId 11 missing
  AMRP reprojection (uniforms)   VariantUniforms member 11 never read
  two projections merged         Hi/Lo encoded separately, then blended
                                 (quad-view inset: Steam Link's sharp square)

Each gets the exact piecewise sRGB OETF under the flag the native code already
sets; merged shaders encode once after the in-shader composition. Flag off,
the code is canonically identical to the original. Registered sizes, ELF layout
and native code are unchanged. Reflection metadata used by Shader::Init (every
OpName, Block member names, decorations) is preserved; only OpSource* is
dropped and nested member labels are shortened (uber) or omitted (precompiled)
to make room. All original precision decorations are retained. Free words are
filled with OpNop before the unique OpReturn.
"""
import argparse
import difflib
import hashlib
import json
import re
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

STOCK_SHA256 = '54f9c32c6a80198dff06ae8cda56a0f81d87b660c67f62bc452725f241e0733e'
PAYLOAD_SHA256 = 'f8e5cfeb7aefeabf922315eb8ca13a71a95df686ac85e3b6742d3ddb2964c142'

SRGB_CONSTANTS = '''%gxrThreshold = OpConstant %float 0.0031308
%gxrSlope = OpConstant %float 12.92
%gxrScale = OpConstant %float 1.055
%gxrOffset = OpConstant %float 0.055
%gxrExponent = OpConstant %float 0.4166666666666667
%gxrZero3 = OpConstantComposite %v3float %float_0 %float_0 %float_0
%gxrOne3 = OpConstantComposite %v3float %float_1 %float_1 %float_1
%gxrThreshold3 = OpConstantComposite %v3float %gxrThreshold %gxrThreshold %gxrThreshold
%gxrOffset3 = OpConstantComposite %v3float %gxrOffset %gxrOffset %gxrOffset
%gxrExponent3 = OpConstantComposite %v3float %gxrExponent %gxrExponent %gxrExponent
'''


def encode_rgb(cond, value):
    """Encode RGB of value (clamped to [0,1]) when cond holds; keep alpha."""
    return f'''OpSelectionMerge %gxrDone None
OpBranchConditional {cond} %gxrEncode %gxrKeep
%gxrEncode = OpLabel
%gxrRgb = OpVectorShuffle %v3float {value} {value} 0 1 2
%gxrClamped = OpExtInst %v3float %1 FClamp %gxrRgb %gxrZero3 %gxrOne3
%gxrLow = OpVectorTimesScalar %v3float %gxrClamped %gxrSlope
%gxrPower = OpExtInst %v3float %1 Pow %gxrClamped %gxrExponent3
%gxrScaled = OpVectorTimesScalar %v3float %gxrPower %gxrScale
%gxrHigh = OpFSub %v3float %gxrScaled %gxrOffset3
%gxrIsLow = OpFOrdLessThanEqual %v3bool %gxrClamped %gxrThreshold3
%gxrEncoded = OpSelect %v3float %gxrIsLow %gxrLow %gxrHigh
%gxrR = OpCompositeExtract %float %gxrEncoded 0
%gxrG = OpCompositeExtract %float %gxrEncoded 1
%gxrB = OpCompositeExtract %float %gxrEncoded 2
%gxrA = OpCompositeExtract %float {value} 3
%gxrColor = OpCompositeConstruct %v4float %gxrR %gxrG %gxrB %gxrA
OpStore %FragColor %gxrColor
OpBranch %gxrDone
%gxrKeep = OpLabel
OpStore %FragColor {value}
OpBranch %gxrDone
%gxrDone = OpLabel'''


# AMRP: vec4 encode in place before the unique OpReturn (smaller code; the
# constants module has little room). Negative lanes clamp to 0; >1 passes as in
# the vendor's own encode and saturates on the UNORM store.
AMRP_CONSTANTS = '''%gxrV4bool = OpTypeVector %bool 4
%gxrThreshold = OpConstant %float 0.0031308
%gxrSlope = OpConstant %float 12.92
%gxrScale = OpConstant %float 1.055
%gxrOffset = OpConstant %float 0.055
%gxrExponent = OpConstant %float 0.4166666666666667
%gxrThreshold4 = OpConstantComposite %v4float %gxrThreshold %gxrThreshold %gxrThreshold %gxrThreshold
%gxrOffset4 = OpConstantComposite %v4float %gxrOffset %gxrOffset %gxrOffset %gxrOffset
%gxrExponent4 = OpConstantComposite %v4float %gxrExponent %gxrExponent %gxrExponent %gxrExponent
'''

AMRP_BLOCK = '''OpSelectionMerge %gxrDone None
OpBranchConditional %gxrEncodeOutput %gxrEncode %gxrDone
%gxrEncode = OpLabel
%gxrIn = OpLoad %v4float %FragColor
%gxrPos = OpExtInst %v4float %1 FMax %gxrIn ZERO4
%gxrLow = OpVectorTimesScalar %v4float %gxrPos %gxrSlope
%gxrPower = OpExtInst %v4float %1 Pow %gxrPos %gxrExponent4
%gxrScaled = OpVectorTimesScalar %v4float %gxrPower %gxrScale
%gxrHigh = OpFSub %v4float %gxrScaled %gxrOffset4
%gxrIsLow = OpFOrdLessThanEqual %gxrV4bool %gxrPos %gxrThreshold4
%gxrEncoded = OpSelect %v4float %gxrIsLow %gxrLow %gxrHigh
%gxrOut = OpVectorShuffle %v4float %gxrEncoded %gxrIn 0 1 2 7
OpStore %FragColor %gxrOut
OpBranch %gxrDone
%gxrDone = OpLabel
OpReturn
'''


def insert_before_main(text, block):
    at = text.index('       %main = OpFunction')
    return text[:at] + block + text[at:]


def patch_camera(text):
    assert 'SpecId 16' not in text and text.count('OpStore %FragColor') == 1
    assert 'OpStore %FragColor %5409' in text
    at = text.index('               OpDecorate ')
    text = text[:at] + '               OpDecorate %gxrEncodeOutput SpecId 16\n' + text[at:]
    text = insert_before_main(text, '%gxrEncodeOutput = OpSpecConstantFalse %bool\n' + SRGB_CONSTANTS)
    return text.replace('OpStore %FragColor %5409', encode_rgb('%gxrEncodeOutput', '%5409'))


def patch_camera_uniforms(text):
    assert 'OpMemberName %VariantUniforms 16 "srgbInverseEotf"' in text
    assert '%2144 = OpLoad %VariantUniforms %2143' in text
    assert text.count('OpStore %FragColor %5674') == 1
    text = insert_before_main(text, SRGB_CONSTANTS)
    flag = ('%gxrFlag = OpCompositeExtract %uint %2144 16\n'
            '%gxrEncodeOutput = OpINotEqual %bool %gxrFlag %uint_0\n')
    return text.replace('OpStore %FragColor %5674', flag + encode_rgb('%gxrEncodeOutput', '%5674'))


def patch_merged(flags, unused=1):
    """unused: how many of the flags the vendor declared but never branched on."""
    def patch(text):
        for f in flags:
            assert f in text
        disabled = 0
        for f in flags:
            old = 'OpBranchConditional %' + f + ' '
            disabled += text.count(old)
            text = text.replace(old, 'OpBranchConditional %gxrFalse ')
        assert disabled == len(flags) - unused
        constants = '\n%gxrFalse = OpConstantFalse %bool\n' + SRGB_CONSTANTS
        cond = '%' + flags[0]
        for i, f in enumerate(flags[1:]):
            nxt = '%gxrEncode' + str(i)
            constants += f'{nxt} = OpSpecConstantOp %bool LogicalOr {cond} %{f}\n'
            cond = nxt
        text = insert_before_main(text, constants)
        last = list(re.finditer(r'OpStore %FragColor (%\w+)', text))[-1]
        assert re.fullmatch(r'\s*OpReturn\s*OpFunctionEnd\s*', text[last.end():])
        return text[:last.start()] + encode_rgb(cond, last[1]) + text[last.end():]
    return patch


def patch_distortion(kind):
    """Keep masks/highlights in linear light; encode once after the final store."""
    def patch(text):
        cond = '%kSrgbInverseEotf' if kind == 'constants' else '%2016'
        if kind == 'uniforms':
            assert 'OpMemberName %VariantUniforms 17 "srgbInverseEotf"' in text
            assert '%2015 = OpCompositeExtract %uint %1963 17' in text
            assert '%2016 = OpINotEqual %bool %2015 %uint_0' in text
        branch = 'OpBranchConditional ' + cond + ' '
        assert text.count(branch) == 1
        text = text.replace(branch, 'OpBranchConditional %gxrFalse ')
        text = insert_before_main(text, '%gxrFalse = OpConstantFalse %bool\n' + SRGB_CONSTANTS)
        assert text.count('OpReturn\n') == 1
        last = list(re.finditer(r'OpStore %FragColor (%\w+)', text))[-1]
        assert re.fullmatch(r'\s*OpReturn\s*OpFunctionEnd\s*', text[last.end():])
        return text[:last.start()] + encode_rgb(cond, last[1]) + text[last.end():]
    return patch


def patch_precompiled(text, associated_alpha=False):
    """This slot is selected only for SRGB_INVERSE_EOTF; encode final RGB."""
    assert text.count('OpReturn\n') == 1
    assert '%FragColor = OpVariable' in text
    assert '%1 = OpExtInstImport "GLSL.std.450"' in text
    extra = ''
    boolean = '%bool'
    if 'OpTypeBool' not in text:
        boolean = '%gxrBool'
        extra += '%gxrBool = OpTypeBool\n'
    vector_bool = re.search(r'(%\w+) = OpTypeVector ' + boolean + r' 3\b', text)
    vb = vector_bool[1] if vector_bool else '%gxrV3bool'
    if not vector_bool:
        extra += f'{vb} = OpTypeVector {boolean} 3\n'
    zero = re.search(r'(%\w+) = OpConstant %float 0\s*$', text, re.M)
    zero_id = zero[1] if zero else '%gxrZero'
    if not zero:
        extra += '%gxrZero = OpConstant %float 0\n'
    one_id = None
    if associated_alpha:
        one = re.search(r'(%\w+) = OpConstant %float 1\s*$', text, re.M)
        one_id = one[1] if one else '%gxrOne'
        if not one:
            extra += '%gxrOne = OpConstant %float 1\n'
    extra += '\n'.join(line for line in SRGB_CONSTANTS.replace('%float_0', zero_id).splitlines()
                      if not line.startswith('%gxrOne3')) + '\n'
    text = insert_before_main(text, extra)
    stores = list(re.finditer(r'OpStore %FragColor (%\w+)', text))
    assert len(stores) == 1
    value = stores[0][1]
    # Reuse the final SSA value, rather than loading output memory again.
    # Values >1 saturate when stored to UNORM; only negatives need clamping
    # before Pow (as in the existing AMRP correction).
    block = f'''%gxrRgb = OpVectorShuffle %v3float {value} {value} 0 1 2
%gxrClamped = OpExtInst %v3float %1 FMax %gxrRgb %gxrZero3
%gxrLow = OpVectorTimesScalar %v3float %gxrClamped %gxrSlope
%gxrPower = OpExtInst %v3float %1 Pow %gxrClamped %gxrExponent3
%gxrScaled = OpVectorTimesScalar %v3float %gxrPower %gxrScale
%gxrHigh = OpFSub %v3float %gxrScaled %gxrOffset3
%gxrIsLow = OpFOrdLessThanEqual {vb} %gxrClamped %gxrThreshold3
%gxrEncoded = OpSelect %v3float %gxrIsLow %gxrLow %gxrHigh
%gxrOut = OpVectorShuffle %v4float %gxrEncoded {value} 0 1 2 6
OpStore %FragColor %gxrOut
OpReturn
'''
    if associated_alpha:
        # QUAD outputs are associated (premultiplied) RGB, irrespective of
        # whether the input buffer was already premultiplied. Convert the
        # straight colour, then associate it again in the destination space.
        # Zero-alpha emissive RGB keeps its previous behaviour; no divide by 0.
        prefix = f'''%gxrAlpha = OpCompositeExtract %float {value} 3
%gxrHasAlpha = OpFOrdGreaterThan {boolean} %gxrAlpha {zero_id}
%gxrWeight = OpSelect %float %gxrHasAlpha %gxrAlpha {one_id}
%gxrWeights = OpCompositeConstruct %v3float %gxrWeight %gxrWeight %gxrWeight
'''
        block = prefix + block.replace(
            '%gxrClamped = OpExtInst %v3float %1 FMax %gxrRgb',
            '%gxrStraight = OpFDiv %v3float %gxrRgb %gxrWeights\n'
            '%gxrClamped = OpExtInst %v3float %1 FMax %gxrStraight')
        block = block.replace('%gxrOut = OpVectorShuffle',
                              '%gxrAssociated = OpVectorTimesScalar %v3float %gxrEncoded %gxrWeight\n'
                              '%gxrOut = OpVectorShuffle').replace(
                                  '%v4float %gxrEncoded ' + value,
                                  '%v4float %gxrAssociated ' + value)
    return text.replace('               OpReturn\n', block)


def patch_quad(text):
    assert 'OpName %NodeFrag "NodeFrag"' in text
    return patch_precompiled(text, associated_alpha=True)


def patch_amrp(kind):
    def patch(text):
        assert text.count('OpReturn\n') == 1 and text.count('OpFunction ') == 1
        assert '%1 = OpExtInstImport "GLSL.std.450"' in text
        assert 'OpTypeVector %bool 4' not in text and 'SpecId 11' not in text
        zero = re.search(r'^\s*(%\w+) = OpConstantComposite %v4float %float_0 %float_0 %float_0 %float_0$', text, re.M)
        zero4, extra = (zero.group(1), '') if zero else (
            '%gxrZero4', '%gxrZero4 = OpConstantComposite %v4float %float_0 %float_0 %float_0 %float_0\n')
        if kind == 'constants':
            assert re.search(r'OpDecorate %kSdrEotf SpecId 10\n', text)
            at = text.index('               OpDecorate ')
            text = text[:at] + '               OpDecorate %gxrEncodeOutput SpecId 11\n' + text[at:]
            consts, flag = '%gxrEncodeOutput = OpSpecConstantFalse %bool\n' + AMRP_CONSTANTS, ''
        else:
            assert 'OpMemberName %VariantUniforms 11 "srgbInverseEotf"' in text
            assert '%356 = OpAccessChain %_ptr_Uniform_VariantUniforms %_ %int_0' in text
            consts = '%gxrUint11 = OpConstant %uint 11\n' + AMRP_CONSTANTS
            flag = ('%gxrFlagPtr = OpAccessChain %_ptr_Uniform_uint %356 %gxrUint11\n'
                    '%gxrFlag = OpLoad %uint %gxrFlagPtr\n'
                    '%gxrEncodeOutput = OpINotEqual %bool %gxrFlag %uint_0\n')
        text = insert_before_main(text, consts + extra)
        return text.replace('               OpReturn\n', flag + AMRP_BLOCK.replace('ZERO4', zero4))
    return patch


# name, offset, registered length, assembler target, patch
SHADERS = [
    ('camera', 0x271fae0, 42820, 'vulkan1.1', patch_camera),
    ('camera-uniforms', 0x272a228, 43640, 'spv1.3', patch_camera_uniforms),
    ('projpass', 0x83a7a4, 399288, 'spv1.3', patch_merged(['kSrgbInverseEotf', 'kPassthroughSrgbInverseEotf'])),
    ('twoprojpass', 0x27557c0, 190332, 'spv1.3',
     patch_merged(['kLoSrgbInverseEotf', 'kHiSrgbInverseEotf', 'kPassthroughSrgbInverseEotf'])),
    ('amrp-constants', 0xa8230, 12076, 'spv1.3', patch_amrp('constants')),
    ('amrp-uniforms', 0xab160, 13188, 'spv1.3', patch_amrp('uniforms')),
    ('twoproj', 0x2794980, 123960, 'spv1.3', patch_merged(['kLoSrgbInverseEotf', 'kHiSrgbInverseEotf'], unused=0)),
    ('distortion-constants', 0x8aba34, 81820, 'spv1.3', patch_distortion('constants')),
    ('distortion-uniforms', 0x8bf9d4, 85664, 'spv1.3', patch_distortion('uniforms')),
]

PRECOMPILED = json.loads(Path(__file__).with_name('precompiled.json').read_text())
assert len(PRECOMPILED) == 544
SHADERS += [('precompiled-' + s['name'], s['offset'], s['length'], 'spv1.3',
             patch_quad if s['name'].startswith('node_') else patch_precompiled)
            for s in PRECOMPILED]


def rows(data):
    p = 20
    while p < len(data):
        w = struct.unpack_from('<I', data, p)[0]
        n, op = w >> 16, w & 65535
        assert n and p + n * 4 <= len(data)
        yield op, list(struct.unpack_from('<' + 'I' * (n - 1), data, p + 4))
        p += n * 4
    assert p == len(data)


def pack(op, v):
    return struct.pack('<' + 'I' * (len(v) + 1), ((len(v) + 1) << 16) | op, *v)


def words(s):
    b = s.encode() + b'\0'
    b += b'\0' * ((-len(b)) % 4)
    return list(struct.unpack('<' + 'I' * (len(b) // 4), b))


def fit(raw, length, name):
    """Drop OpSource*, shorten nested member labels, pad with OpNop."""
    r = list(rows(raw))
    blocks = {v[0] for op, v in r if op == 71 and v[1] in [2, 3]}
    compact = bytearray(raw[:20])
    for op, v in r:
        if op in [2, 3, 4]:
            continue
        if op == 6 and v[0] not in blocks:
            if name.startswith('precompiled-'):
                # These nested field labels are debug metadata, not resource
                # names. Retain types, offsets, all OpName and Block member names.
                # Unlike removing RelaxedPrecision, this cannot change arithmetic.
                continue
            v = v[:2] + words('m' + str(v[1]))
        compact += pack(op, v)
    assert len(compact) <= length, f'{name}: {len(compact)} > {length}'
    body = list(rows(compact))
    assert sum(op == 253 for op, v in body) == 1
    fixed = bytearray(compact[:20])
    for op, v in body:
        if op == 253:
            fixed += pack(0, []) * ((length - len(compact)) // 4)
        fixed += pack(op, v)
    assert len(fixed) == length
    return bytes(fixed)


def names(data):
    """Every OpName string and Block member name must survive."""
    r = list(rows(data))
    blocks = {v[0] for op, v in r if op == 71 and v[1] in [2, 3]}
    text = lambda v: struct.pack('<%dI' % len(v), *v).split(b'\0')[0]
    return sorted([text(v[1:]) for op, v in r if op == 5] +
                  [b'%d:' % v[1] + text(v[2:]) for op, v in r if op == 6 and v[0] in blocks])


TOKEN = re.compile(r'%[A-Za-z0-9_]+')


def unquoted(line):
    """Tokens outside string literals (even-indexed parts of a '"' split)."""
    return [t for part in line.split('"')[::2] for t in TOKEN.findall(part)]


def id_map(friendly, raw):
    """Map friendly-disassembly ids to the original numeric ids, line by line."""
    f_lines, r_lines = friendly.splitlines(), raw.splitlines()
    assert len(f_lines) == len(r_lines)
    mapping = {}
    for f, r in zip(f_lines, r_lines):
        ft, rt = unquoted(f), unquoted(r)
        assert len(ft) == len(rt), (f, r)
        for a, b in zip(ft, rt):
            assert mapping.setdefault(a, b) == b, (a, b)
    return mapping


def to_raw(text, mapping):
    """Rewrite vendor ids to their original numbers; new %gxr* ids stay symbolic."""
    def sub(m):
        t = m.group(0)
        return t if t.startswith('%gxr') else mapping[t]
    out = []
    for line in text.splitlines():
        parts = line.split('"')
        parts[::2] = [TOKEN.sub(sub, p) for p in parts[::2]]
        out.append('"'.join(parts))
    return '\n'.join(out) + '\n'


# Edit script applied on the device to its own stock library. It holds only the
# inserted project instructions and counts of vendor instructions to copy or
# skip; every unchanged vendor instruction comes from the device's own file.
MAGIC, END, COPY, SKIP, INSERT = 0x30314246, 0, 1, 2, 3


def instructions(data):
    out, p = [], 20
    while p < len(data):
        n = struct.unpack_from('<I', data, p)[0] >> 16
        out.append(struct.unpack_from('<%dI' % n, data, p))
        p += n * 4
    return out


def make_script(original, fixed):
    a, b = instructions(original), instructions(fixed)
    ops = list(struct.unpack('<5I', fixed[:20]))
    for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(None, a, b, autojunk=False).get_opcodes():
        if tag == 'equal':
            ops.append((COPY << 28) | (i2 - i1))
            continue
        if i2 > i1:
            ops.append((SKIP << 28) | (i2 - i1))
        if j2 > j1:
            words = [w for ins in b[j1:j2] for w in ins]
            ops += [(INSERT << 28) | len(words)] + words
    return ops + [END]


def apply_script(original, ops):
    """Reference applier; native/main.cpp implements the same format."""
    src = instructions(original)
    out, i, k = list(ops[:5]), 0, 5
    while ops[k] != END:
        kind, n = ops[k] >> 28, ops[k] & 0x0fffffff
        k += 1
        if kind == COPY:
            out += [w for ins in src[i:i + n] for w in ins]
            i += n
        elif kind == SKIP:
            i += n
        else:
            out += ops[k:k + n]
            k += n
    assert i == len(src)
    return struct.pack('<%dI' % len(out), *out)


def write_inc(path, scripts):
    words = [MAGIC, len(scripts)]
    for start, length, ops in scripts:
        words += [start, length, len(ops)] + ops
    body = ',\n'.join(', '.join('0x%08x' % w for w in words[i:i + 8]) for i in range(0, len(words), 8))
    path.write_text('// Generated by payload-recipe/make_payload.py. Project-authored edit script:\n'
                    "// copy/skip counts over the device's own stock shaders plus inserted\n"
                    '// project instructions. Contains no vendor code.\n'
                    f'static const uint32_t kPayloadPatch[] = {{\n{body}\n}};\n', newline='\n')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--stock', type=Path, required=True)
    ap.add_argument('--tools', type=Path, required=True)
    ap.add_argument('--out', type=Path, help='write the payload (local build)')
    ap.add_argument('--emit-inc', type=Path, help='write the native edit script (native/payload_patch.inc)')
    args = ap.parse_args()

    def exe(name):
        for candidate in (args.tools / name, args.tools / (name + '.exe')):
            if candidate.is_file():
                return str(candidate)
        raise SystemExit(f'{name} not found in {args.tools}')

    def run(name, *a):
        r = subprocess.run([exe(name), *map(str, a)], capture_output=True, text=True)
        if r.returncode:
            raise SystemExit(f'{name} failed: {r.stdout}{r.stderr}')

    stock = args.stock.read_bytes()
    if hashlib.sha256(stock).hexdigest() != STOCK_SHA256:
        raise SystemExit('Unsupported stock library: expected I610UEU2AZF3 SHA256 ' + STOCK_SHA256)
    lib = bytearray(stock)
    scripts = []
    with tempfile.TemporaryDirectory(prefix='fb10-payload-') as tmp:
        work = Path(tmp)
        for name, start, length, target, patch in SHADERS:
            original = stock[start:start + length]
            assert original[:4] == b'\x03\x02\x23\x07'
            if not name.startswith('precompiled-'):
                assert struct.unpack_from('<I', stock, start + length)[0] == length
            src, asm, rawasm, out = (work / (f'{start:x}' + s) for s in ('.spv', '.spvasm', '-raw.spvasm', '-patched.spv'))
            src.write_bytes(original)
            run('spirv-dis', src, '-o', asm)
            run('spirv-dis', '--raw-id', src, '-o', rawasm)
            friendly = asm.read_text()
            # Patch the readable text, then restore every vendor id so that all
            # unchanged vendor instructions stay byte-identical.
            rawasm.write_text(to_raw(patch(friendly), id_map(friendly, rawasm.read_text())), newline='\n')
            run('spirv-as', '--target-env', target, '--preserve-numeric-ids', rawasm, '-o', out)
            assembled = out.read_bytes()
            assembled = assembled[:8] + original[8:12] + assembled[12:]  # vendor generator word
            fixed = fit(assembled, length, name)
            assert fixed[4:8] == original[4:8], name + ': SPIR-V version changed'
            assert names(fixed) == names(original), name + ': reflection names changed'
            ops = make_script(original, fixed)
            assert apply_script(original, ops) == fixed
            scripts.append((start, length, ops))
            lib[start:start + length] = fixed
    digest = hashlib.sha256(lib).hexdigest()
    if digest != PAYLOAD_SHA256:
        raise SystemExit(f'Result {digest} differs from reviewed payload {PAYLOAD_SHA256}; check SPIRV-Tools; nothing written')
    if args.emit_inc:
        write_inc(args.emit_inc, scripts)
        print(f'{args.emit_inc}: {sum(len(s[2]) for s in scripts)} words')
    if args.out:
        tmp_out = args.out.with_name(args.out.name + '.tmp')
        tmp_out.write_bytes(lib)
        shutil.move(str(tmp_out), str(args.out))
        print(f'{args.out}: {digest}')


if __name__ == '__main__':
    main()
