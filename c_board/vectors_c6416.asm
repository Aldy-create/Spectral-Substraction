; vectors_c6416.asm -- Tabel vektor interrupt C6416 (RESET -> _c_int00, INT11 -> _c_int11)
; Dipasang ke ISTP melalui IRQ_setVecs(_vectors) pada mode Q-LIVE.

        .global _vectors
        .global _c_int00
        .global _c_int11

VEC_ENTRY .macro addr
        STW     B0, *--B15
        MVKL    addr, B0
        MVKH    addr, B0
        B       B0
        LDW     *B15++, B0
        NOP     2
        NOP
        NOP
        .endm

VEC_DUMMY .macro
        B       IRP
        NOP     5
        NOP
        NOP
        NOP
        NOP
        NOP
        NOP
        .endm

        .sect ".vectors"
        .align 1024
_vectors:
_vector0:   VEC_ENTRY _c_int00      ; RESET
_vector1:   VEC_DUMMY               ; NMI
_vector2:   VEC_DUMMY               ; Reserved
_vector3:   VEC_DUMMY               ; Reserved
_vector4:   VEC_DUMMY               ; INT4
_vector5:   VEC_DUMMY               ; INT5
_vector6:   VEC_DUMMY               ; INT6
_vector7:   VEC_DUMMY               ; INT7
_vector8:   VEC_DUMMY               ; INT8
_vector9:   VEC_DUMMY               ; INT9
_vector10:  VEC_DUMMY               ; INT10
_vector11:  VEC_ENTRY _c_int11      ; INT11 (McBSP2 Receive RINT2)
_vector12:  VEC_DUMMY               ; INT12
_vector13:  VEC_DUMMY               ; INT13
_vector14:  VEC_DUMMY               ; INT14
_vector15:  VEC_DUMMY               ; INT15
