// Author: 4uva4ek. Temporary OutPredictor velocity-only M1 payload.
// Entry replaces 0x72c5c. No calls. Code RX, control separate RW at +0x4000.
// Template return is BRK until relocated by build.py. Never install template.
.text
.global tail_entry, tail_return, tail_control
.p2align 4
tail_entry:
    fmadd s9, s9, s31, s23
    sub sp, sp, #0xc0
    stp q0, q1, [sp, #0]
    stp q2, q3, [sp, #32]
    stp q4, q5, [sp, #64]
    stp q6, q7, [sp, #96]
    stp x8, x9, [sp, #128]
    stp x10, x11, [sp, #144]
    stp x12, x13, [sp, #160]
    mrs x9, nzcv
    mrs x10, fpsr
    stp x9, x10, [sp, #176]
    adr x8, tail_control
    ldr x9, [x8]
    ldr x10, magic
    cmp x9, x10
    b.ne restore
    // mode and gain are one aligned 64-bit snapshot.
    ldr x11, [x8, #8]
    lsr x9, x11, #32
    fmov s7, w9
    mov w12, #2 // rejected (0 observe, 1 applied)
    fmov s0, s31
    ldr d1, [x20, #0x88]
    fmov s5, #-1.0 // no valid acceleration norm yet
    // h must be finite and positive. Large finite h is capped.
    fcmp s0, #0.0
    b.le telemetry
    b.vs telemetry
    ldr s6, inf
    fcmp s0, s6
    b.ge telemetry
    ldr d6, dt_min
    fcmp d1, d6
    b.lt telemetry
    b.vs telemetry
    ldr d6, dt_max
    fcmp d1, d6
    b.gt telemetry
    fcvt s1, d1
    // Mean world acceleration: R_src * dv / dt + gravity.
    ldp s4, s5, [x20, #0xb4]
    ldr s6, [x20, #0xbc]
    fmul v2.2s, v25.2s, v4.s[0]
    fmul s3, s26, s4
    fmla v2.2s, v27.2s, v5.s[0]
    fmadd s3, s24, s5, s3
    fmla v2.2s, v28.2s, v6.s[0]
    fmadd s3, s29, s6, s3
    dup v6.2s, v1.s[0]
    fdiv v2.2s, v2.2s, v6.2s
    fdiv s3, s3, s1
    ldr d4, [x20, #0x48]
    ldr s5, [x20, #0x50]
    fadd v2.2s, v2.2s, v4.2s
    fadd s3, s3, s5
    fmul v4.2s, v2.2s, v2.2s
    faddp s4, v4.2s
    fmadd s4, s3, s3, s4
    fsqrt s5, s4
    ldr s6, inf
    fcmp s5, s6
    b.ge telemetry
    b.vs telemetry
    // Mode 0 still collects h/dt/|a|. Unknown mode fails closed.
    cbz w11, observe
    cmp w11, #1
    b.ne telemetry
    fcmp s7, #0.0
    b.lt telemetry
    b.vs telemetry
    fmov s6, #1.0
    fcmp s7, s6
    b.gt telemetry
    // Clamp vector norm, retaining direction.
    ldr s6, accel_max
    fcmp s5, s6
    b.le norm_ok
    fdiv s6, s6, s5
    fmul v2.2s, v2.2s, v6.s[0]
    fmul s3, s3, s6
norm_ok:
    ldr s6, h_max
    fmin s6, s0, s6
    fmul s6, s6, s7
    fmul v2.2s, v2.2s, v6.s[0]
    fmul s3, s3, s6
    // Extra bound: correction norm <= 0.5 m/s (not a quality claim).
    fmul v4.2s, v2.2s, v2.2s
    faddp s4, v4.2s
    fmadd s4, s3, s3, s4
    fsqrt s4, s4
    ldr s6, delta_max
    fcmp s4, s6
    b.le delta_ok
    fdiv s6, s6, s4
    fmul v2.2s, v2.2s, v6.s[0]
    fmul s3, s3, s6
delta_ok:
    fadd v6.2s, v22.2s, v2.2s
    fadd s7, s9, s3
    // Reject the whole correction if any resulting component is nonfinite.
    fabs v4.2s, v6.2s
    fmaxp s4, v4.2s
    fabs s3, s7
    fmax s4, s4, s3
    ldr s3, inf
    fcmp s4, s3
    b.ge telemetry
    b.vs telemetry
    fmov d22, d6
    fmov s9, s7
    mov w12, #1
    b telemetry
observe:
    mov w12, #0
telemetry:
    // Best-effort, one try: concurrent callers never wait in the predictor.
    add x9, x8, #16
    ldaxr w10, [x9]
    cbnz w10, unlock_monitor
    mov w10, #1
    stxr w13, w10, [x9]
    cbnz w13, unlock_monitor
    ldr x10, [x8, #32]
    add x10, x10, #1
    str x10, [x8, #32]
    add x10, x8, #40
    ldr x13, [x10, x12, lsl #3]
    add x13, x13, #1
    str x13, [x10, x12, lsl #3]
    // Histogram invalid/negative values have dedicated overflow bin.
    fmov s6, s31
    ldr s4, thousand
    fmul s6, s6, s4
    mov w12, #36
    fcmp s6, #0.0
    b.lt hist_h
    b.vs hist_h
    ldr s3, h_bins
    fcmp s6, s3
    b.ge hist_h
    fcvtzu w12, s6
hist_h:
    add x10, x8, #128
    ldr x13, [x10, x12, lsl #3]
    add x13, x13, #1
    str x13, [x10, x12, lsl #3]
    ldr d1, [x20, #0x88]
    fcvt s6, d1
    fmul s6, s6, s4
    mov w12, #101
    fcmp s6, #0.0
    b.lt hist_dt
    b.vs hist_dt
    ldr s3, dt_bins
    fcmp s6, s3
    b.ge hist_dt
    fcvtzu w12, s6
hist_dt:
    add x10, x8, #448
    ldr x13, [x10, x12, lsl #3]
    add x13, x13, #1
    str x13, [x10, x12, lsl #3]
    // Acceleration bins start after 102 uint64 dt bins, at 1280.
    mov w12, #41
    fcmp s5, #0.0
    b.lt hist_a
    b.vs hist_a
    ldr s3, a_bins
    fcmp s5, s3
    b.ge hist_a
    fcvtzu w12, s5
hist_a:
    add x10, x8, #1280
    ldr x13, [x10, x12, lsl #3]
    add x13, x13, #1
    str x13, [x10, x12, lsl #3]
    stlr wzr, [x9]
    b restore
unlock_monitor:
    clrex
restore:
    ldp x9, x10, [sp, #176]
    msr nzcv, x9
    msr fpsr, x10
    ldp q0, q1, [sp, #0]
    ldp q2, q3, [sp, #32]
    ldp q4, q5, [sp, #64]
    ldp q6, q7, [sp, #96]
    ldp x8, x9, [sp, #128]
    ldp x10, x11, [sp, #144]
    ldp x12, x13, [sp, #160]
    add sp, sp, #0xc0
tail_return:
    brk #0xa11
.p2align 3
magic: .quad 0x31564c4941545847
inf: .word 0x7f800000
thousand: .float 1000.0
accel_max: .float 40.0
h_max: .float 0.035
delta_max: .float 0.5
h_bins: .float 36.0
dt_bins: .float 101.0
a_bins: .float 41.0
.p2align 3
dt_min: .double 0.002
dt_max: .double 0.100
.org 0x4000
tail_control:
    .quad 0x31564c4941545847
    .word 0
    .float 1.0
    .zero 2032
