// Replaces only the diagnostic log block at JNI 0x12870..0x128b4.
// Incoming sp is the stock frame; w24 is PCM bufferSize. No calls or stack changes.
.text
.global tuning
tuning:
    cbnz w24, done
    ldr s0, [sp, #0x3c]
    fcmp s0, #0.0
    b.le done // also skips unordered NaN (N != V)
    fmov s0, #2.0 // stock service step for a 100 Hz request
    str s0, [sp, #0x38]
    .rept 11
    nop
    .endr
done:
