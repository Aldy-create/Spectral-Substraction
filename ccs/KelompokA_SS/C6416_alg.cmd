/****************************************************************************/
/*  C6416_alg.cmd -- Linker Command File Kelompok A (TMS320C6416T DSK)      */
/*  Berbasis C6416.cmd workspace_v6_2 dengan pemisahan .algtext & .algdata  */
/****************************************************************************/

MEMORY
{
    L2RAM:     o = 0x00000000  l = 0x00100000  /* 1MB L2 Internal RAM */
    EMIFBCE0:  o = 0x60000000  l = 0x04000000  /* 64MB EMIFB CE0      */
    EMIFBCE1:  o = 0x64000000  l = 0x04000000  /* 64MB EMIFB CE1      */
    EMIFBCE2:  o = 0x68000000  l = 0x04000000  /* 64MB EMIFB CE2      */
    EMIFBCE3:  o = 0x6C000000  l = 0x04000000  /* 64MB EMIFB CE3      */
    EMIFACE0:  o = 0x80000000  l = 0x10000000  /* 256MB EMIFA CE0 / SDRAM */
    EMIFACE1:  o = 0x90000000  l = 0x10000000  /* 256MB EMIFA CE1     */
    EMIFACE2:  o = 0xA0000000  l = 0x10000000  /* 256MB EMIFA CE2     */
    EMIFACE3:  o = 0xB0000000  l = 0x10000000  /* 256MB EMIFA CE3     */
}

SECTIONS
{
    .vectors       >  L2RAM, align = 1024  /* Tabel interrupt (dipasang ke ISTP) */
    .text          >  L2RAM
    .algtext       >  L2RAM                /* Kode algoritma Kelompok A          */
    .stack         >  L2RAM
    .bss           >  L2RAM
    .cio           >  L2RAM
    .const         >  L2RAM
    .data          >  L2RAM
    .algdata       >  L2RAM                /* State, buffer, & LUT algoritma     */
    .switch        >  L2RAM
    .sysmem        >  L2RAM
    .far           >  L2RAM
    .args          >  L2RAM
    .ppinfo        >  L2RAM
    .ppdata        >  L2RAM

    /* COFF sections */
    .pinit         >  L2RAM
    .cinit         >  L2RAM

    /* Buffer besar untuk klip uji Q-DIG di SDRAM (EMIFA CE0) */
    .extdata       >  EMIFACE0
}
