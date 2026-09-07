/*
 * avg.h - Atari Analog Vector Generator (Tempest variant)
 *
 * Port of MAME's avgdvg.cpp (BSD-3-Clause, Mathis Rosenhauer et al.) to plain C.
 * The AVG is a small sequencer driven by a 256-byte state PROM. It walks vector
 * RAM (CPU 0x2000-0x2FFF) and vector ROM (0x3000-0x3FFF) and emits beam positions.
 *
 * Tempest differs from the Star Wars AVG this was first written for in three ways:
 *
 *  - Colour is an index into sixteen bytes of colour RAM the CPU writes at 0x0800,
 *    rather than a value carried in the vector stream. Intensity is four bits.
 *
 *  - The monitor is mounted rotated, so the generator's x and y are swapped on the
 *    way out.
 *
 *  - It never halts. Tempest leaves the AVG running in an endless loop and the CPU
 *    updates vector RAM underneath it; the list ends when the sequencer jumps back
 *    to address zero. That is where a frame boundary comes from here, which is why
 *    this is avg_run_frame() and not the run-to-halt call Star Wars uses.
 */
#ifndef AVG_H
#define AVG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AVG_MAX_POINTS 2560

/* Beam coordinate space: 0..AVG_XMAX horizontally, 0..AVG_YMAX vertically (MAME's visarea for
 * Tempest). The generator's own units are what set this - shrinking it here would just draw
 * everything at the wrong scale, so the renderer scales to the panel instead. */
#define AVG_XMAX 580
#define AVG_YMAX 570

typedef struct {
    int32_t x, y;        /* 16.16 fixed point, beam space */
    uint8_t r, g, b;     /* colour from the colour RAM, already decoded */
    uint8_t intensity;   /* 0 = move only, else brightness 0..255 */
} avg_point_t;

typedef struct {
    /* configuration */
    const uint8_t *prom;                       /* 256-byte state PROM */
    const uint8_t *ram;                        /* vector RAM, CPU 0x0000-0x2FFF */
    const uint8_t *rom;                        /* vector ROM, CPU 0x3000-0x3FFF */
    const uint8_t *colorram;                   /* sixteen bytes the CPU writes at 0x0800 */

    /* state (kept in a small struct so avg_go can work on a register copy) */
    struct avg_state {
        uint16_t pc;
        uint8_t  sp;
        uint16_t dvx, dvy;
        uint16_t stack[4];
        uint16_t data;
        uint8_t  state_latch;
        uint8_t  scale;
        uint8_t  intensity;
        uint8_t  op;
        uint8_t  halt;
        int32_t  xpos, ypos;
        uint8_t  dvy12;
        uint16_t timer;
        uint8_t  int_latch;
        uint8_t  bin_scale;
        uint8_t  color;
    } st;

    /* output */
    avg_point_t points[AVG_MAX_POINTS];
    int npoints;
    int overflow;
    int frame_done;      /* the sequencer wrapped to zero: the points so far are a frame */
    uint32_t steps;      /* state machine iterations in the last list */
} avg_t;

void avg_init(avg_t *avg, const uint8_t *prom, const uint8_t *vector_ram,
              const uint8_t *vector_rom, const uint8_t *colorram);
void avg_reset(avg_t *avg);          /* VGRST */
void avg_go(avg_t *avg);             /* VGGO: restart the sequencer at address zero */

/*
 * Run the sequencer until it wraps to address zero - one frame's worth of vectors, left in
 * points[0..npoints) - or until `max_cycles` of the 12.096 MHz AVG clock have been spent,
 * whichever comes first. Returns the cycles actually consumed; sets *frame if a whole frame
 * came out, so a caller that ran out of budget knows not to draw a half-finished list.
 */
uint32_t avg_run_frame(avg_t *avg, uint32_t max_cycles, int *frame);
int avg_halted(const avg_t *avg);

#ifdef __cplusplus
}
#endif

#endif
