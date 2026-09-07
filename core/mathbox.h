/*
 * mathbox.h - Atari math box (Battlezone / Red Baron / Tempest)
 *
 * A small fixed-point arithmetic unit on its own board. The CPU writes an operand to one of
 * thirty-two addresses; the address is the opcode. Most of them just load half of a register,
 * but a handful kick off a multiply, a divide or the perspective step, and the sixteen-bit
 * result is read back a byte at a time.
 *
 * Ported from MAME's mathbox.cpp (BSD-3-Clause, Eric Smith). The real board is a bit-slice
 * ALU driven by microcode PROMs; MAME simulates the arithmetic rather than the microcode, and
 * so does this - which is why the ROM set's math box PROMs are not needed here.
 */
#ifndef MATHBOX_H
#define MATHBOX_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int16_t reg[16];
    int16_t result;
} mathbox_t;

void mathbox_reset(mathbox_t *mb);
void mathbox_go(mathbox_t *mb, uint8_t opcode, uint8_t data);
static inline uint8_t mathbox_status_r(const mathbox_t *mb) { (void)mb; return 0x00; }  /* always done */
static inline uint8_t mathbox_lo_r(const mathbox_t *mb) { return (uint8_t)(mb->result & 0xff); }
static inline uint8_t mathbox_hi_r(const mathbox_t *mb) { return (uint8_t)((mb->result >> 8) & 0xff); }

#ifdef __cplusplus
}
#endif
#endif
