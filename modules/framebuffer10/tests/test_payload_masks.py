"""Offline regression and candidate generation; requires the pinned stock and SPIRV-Tools."""
import argparse
import hashlib
import math
import re
from pathlib import Path
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'payload-recipe'))
import make_payload as m

p = argparse.ArgumentParser()
p.add_argument('--stock', type=Path, required=True)
p.add_argument('--tools', type=Path, required=True)
p.add_argument('--work', type=Path, required=True)
a = p.parse_args()
a.work.mkdir(parents=True, exist_ok=True)
stock = a.stock.read_bytes()
assert hashlib.sha256(stock).hexdigest() == m.STOCK_SHA256
lib = bytearray(stock)
scripts = []
precompiled = {s['offset']: s for s in m.PRECOMPILED}


def check_quad_math(text, value):
    """Execute the injected arithmetic and check compositing invariants."""
    constants = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 5 and parts[2:4] == ['OpConstant', '%float']:
            constants[parts[0]] = float(parts[4])
    for line in text.splitlines():
        parts = line.split()
        if len(parts) > 4 and parts[2] == 'OpConstantComposite' and parts[0].startswith('%gxr'):
            constants[parts[0]] = tuple(constants[x] for x in parts[4:])
    block = text[text.index('%gxrAlpha ='):text.index('OpStore %FragColor %gxrOut')]

    def execute(rgba):
        values = {**constants, value: rgba}
        for line in block.splitlines():
            dst, _, op, typ, *args = line.split()
            v = lambda x: values[x]
            if op == 'OpCompositeExtract':
                out = v(args[0])[int(args[1])]
            elif op == 'OpFOrdGreaterThan':
                out = v(args[0]) > v(args[1])
            elif op == 'OpCompositeConstruct':
                out = tuple(map(v, args))
            elif op == 'OpVectorShuffle':
                both = v(args[0]) + v(args[1])
                out = tuple(both[int(i)] for i in args[2:])
            elif op == 'OpVectorTimesScalar':
                out = tuple(x * v(args[1]) for x in v(args[0]))
            elif op == 'OpExtInst':
                fn = {'FMax': max, 'Pow': pow}[args[1]]
                out = tuple(map(fn, v(args[2]), v(args[3])))
            elif op == 'OpSelect':
                cond, yes, no = map(v, args)
                out = tuple(y if c else n for c, y, n in zip(cond, yes, no)) if isinstance(cond, tuple) else yes if cond else no
            else:
                fn = {'OpFDiv': lambda x, y: x / y, 'OpFSub': lambda x, y: x - y,
                      'OpFOrdLessThanEqual': lambda x, y: x <= y}[op]
                out = tuple(map(fn, v(args[0]), v(args[1])))
            values[dst] = out
        return values['%gxrOut']

    def encode(x):
        x = max(0, x)
        return 12.92 * x if x <= 0.0031308 else 1.055 * x ** (1 / 2.4) - 0.055

    for alpha in (0, 1e-8, 0.01, 0.25, 0.5, 0.99, 1):
        for colour in (0, 0.001, 0.0031308, 0.18, 0.5, 1):
            rgba = execute((alpha * colour,) * 3 + (alpha,))
            # An overlay of the same colour must not make its background brighter.
            blended = rgba[0] + (1 - alpha) * encode(colour)
            assert math.isclose(blended, encode(colour), abs_tol=2e-7), (alpha, colour, blended)
            assert rgba[3] == alpha
    # Zero-alpha additive/emissive RGB remains finite and retains old encoding.
    rgba = execute((0.1, 0.2, 0.3, 0))
    assert all(math.isclose(x, encode(y), abs_tol=2e-7) for x, y in zip(rgba[:3], (0.1, 0.2, 0.3)))
    # The previous formula fails the same-colour invariant.
    assert encode(0.5 * 0.18) + 0.5 * encode(0.18) > encode(0.18) + 0.1


def run(tool, *args):
    exe = a.tools / (tool + '.exe')
    if not exe.exists():
        exe = a.tools / tool
    r = subprocess.run([str(exe), *map(str, args)], capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr


def diag14(data):
    ins = list(m.rows(data))
    glob = [v[1] for op, v in ins if op == 59 and v[2] != 7]
    out = bytearray(data[:20])
    struct.pack_into('<I', out, 4, 0x10400)
    for op, v in ins:
        if op == 15:
            j = 2
            while 0 not in struct.pack('<I', v[j]):
                j += 1
            v = v[:j + 1] + list(dict.fromkeys(v[j + 1:] + glob))
        out += m.pack(op, v)
    return out


def canonical(text, stem):
    # Uniform flag fixed to false models the stock output path.
    # A constant must be global; substitute uses while retaining the original definition.
    if '%2016 = OpINotEqual' in text:
        text = m.insert_before_main(text, '%testFalse = OpConstantFalse %bool\n')
        text = text.replace('OpBranchConditional %2016 ', 'OpBranchConditional %testFalse ')
    asm, spv, out = (a.work / (stem + s) for s in ('.asm', '.spv', '-canon.spv'))
    asm.write_text(text)
    run('spirv-as', '--target-env', 'spv1.3', asm, '-o', spv)
    run('spirv-opt', '--skip-validation', '--strip-debug', '--freeze-spec-const',
        '--fold-spec-const-op-composite', '--eliminate-dead-branches',
        '--eliminate-dead-code-aggressive', '--merge-blocks',
        '--eliminate-dead-code-aggressive', '--eliminate-dead-const', '--compact-ids',
        spv, '-o', out)
    return out.read_bytes()[20:]


for name, start, length, target, patch in m.SHADERS:
    original = stock[start:start + length]
    if not name.startswith('precompiled-'):
        assert struct.unpack_from('<I', stock, start + length)[0] == length
    stem = f'{start:x}'
    src, asm, raw, patched = (a.work / (stem + s) for s in ('.spv', '.asm', '-raw.asm', '-fixed.spv'))
    src.write_bytes(original)
    run('spirv-dis', src, '-o', asm)
    run('spirv-dis', '--raw-id', src, '-o', raw)
    friendly = asm.read_text()
    fixed_text = patch(friendly)
    raw.write_text(m.to_raw(fixed_text, m.id_map(friendly, raw.read_text())))
    run('spirv-as', '--target-env', target, '--preserve-numeric-ids', raw, '-o', patched)
    compiled = patched.read_bytes()
    fixed = m.fit(compiled[:8] + original[8:12] + compiled[12:], length, name)
    assert m.names(fixed) == m.names(original)
    ops = m.make_script(original, fixed)
    assert m.apply_script(original, ops) == fixed
    scripts.append((start, length, ops))
    lib[start:start + length] = fixed
    if name.startswith('precompiled-'):
        peer = precompiled[start]['paired_offset']
        assert original == stock[peer:peer + length]
        diagnostic = a.work / (stem + '-validated.spv')
        diagnostic.write_bytes(fixed)
        run('spirv-val', '--scalar-block-layout', '--target-env', 'vulkan1.1', diagnostic)
        # Every original executable instruction, type, constant, resource and
        # interface/precision decoration survives verbatim and in order.
        # Only debug text may be removed.
        def semantic(data):
            return [(op, v) for op, v in m.rows(data)
                    if op not in (0, 2, 3, 4, 6)]
        remaining = iter(semantic(fixed))
        for instruction in semantic(original):
            assert any(row == instruction for row in remaining), (name, instruction)
        value = re.search(r'OpStore %FragColor (%\w+)', friendly)[1]
        encoded = '%gxrAssociated' if precompiled[start]['name'].startswith('node_') else '%gxrEncoded'
        assert f'%gxrOut = OpVectorShuffle %v4float {encoded} {value} 0 1 2 6' in fixed_text
        assert fixed_text.index('%gxrEncoded =') > fixed_text.index('OpStore %FragColor ' + value)
        if encoded == '%gxrAssociated':
            check_quad_math(fixed_text, value)
    if name.startswith('distortion-'):
        for tag, data in [('original', original), ('fixed', fixed)]:
            diagnostic = a.work / (name + '-' + tag + '-diag14.spv')
            diagnostic.write_bytes(diag14(data))
            run('spirv-val', '--scalar-block-layout', '--target-env', 'vulkan1.1spv1.4', diagnostic)
        assert canonical(friendly, name + '-off-original') == canonical(fixed_text, name + '-off-fixed')
        # All original mask operations survive; the old encode branch is disabled.
        assert 'OpBranchConditional %gxrFalse ' in fixed_text
        assert fixed_text.index('%gxrEncoded =') > fixed_text.rindex('OpExtInst %v3float %1 FMix')
        assert '%gxrA = OpCompositeExtract %float' in fixed_text
        print(name, 'PASS: validation, flag-off equivalence, final RGB encode, reflection, edit script')

for s in m.PRECOMPILED:
    peer, length = s['paired_offset'], s['length']
    assert lib[peer:peer + length] == stock[peer:peer + length], 'unflagged shader changed'
cursor = 0
for _, start, length, _, _ in sorted(m.SHADERS, key=lambda s: s[1]):
    assert cursor <= start and lib[cursor:start] == stock[cursor:start]
    cursor = start + length
assert lib[cursor:] == stock[cursor:]
print('PASS: 544 precompiled shaders validated; original instructions and alpha preserved; '
      'unflagged counterparts and all bytes outside 553 slots unchanged')
result = a.work / 'libvulkan_compositor.mask-fix.so'
result.write_bytes(lib)
m.write_inc(a.work / 'payload_patch.inc', scripts)
print('PAYLOAD_SHA256', hashlib.sha256(lib).hexdigest())
