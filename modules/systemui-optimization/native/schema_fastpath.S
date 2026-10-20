// Call-site-specific fast paths. Every validation decision remains in vendor code.
// First input is a verified reflection::KeyValue key's FlatBuffers String data.
.text
.global schema_key_fastpath
.global schema_number_fastpath
.global original_strncmp
.global original_parse_int
schema_key_fastpath:
    ldur w9, [x0, #-4]
    // Preserve the original 12-byte prefix comparison, including longer keys.
    cmp w9, #12
    b.lo original_strncmp
    ldr x9, [x0]
    ldr x10, [x1]
    cmp x9, x10
    b.ne .Lmismatch
    ldr w9, [x0, #8]
    ldr w10, [x1, #8]
    cmp w9, w10
    cset w0, ne
    ret
.Lmismatch:
    mov w0, #1
    ret
.balign 16
schema_number_fastpath:
    // Exact decimal parser for one/two ASCII digits, all other cases use original.
    cmp x1, #1
    b.lo original_parse_int
    cmp x1, #2
    b.hi original_parse_int
    ldrb w9, [x0]
    sub w9, w9, #48
    cmp w9, #9
    b.hi original_parse_int
    cmp x1, #1
    b.eq .Lstore
    ldrb w10, [x0, #1]
    sub w10, w10, #48
    cmp w10, #9
    b.hi original_parse_int
    mov w11, #10
    madd w9, w9, w11, w10
.Lstore:
    str w9, [x2]
    mov w0, #1
    ret
