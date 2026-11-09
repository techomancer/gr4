/*
 * PP1.h - Silicon Graphics Impact Graphics (MGRAS) PP1 Pixel Processor
 *
 * Pixel Processor 1 (PP1), Rambus RDRAM, and RAC Architecture for SGI Impact (GR4).
 *
 * Reverse-engineered documentation and single source of truth for IRIS emulator.
 * Synthesized from:
 *   - SGI IRIX PROM (stand/arcs/lib/libsk/graphics/MGRAS)
 *   - SGI IDE diagnostics (stand/arcs/ide/isddevices/graphics/MGRAS/mgras_pp1.c, mgras_pp1func.c, mgras_rdram_rac.c)
 *   - IRIX 6.5.0 kernel modules (mgras.o, mgras_init.o, mgras_ccv.o)
 *   - Existing IRIS emulator implementation (~/gits/iris/src/dev/mgras)
 */

#ifndef _SGI_PP1_H_
#define _SGI_PP1_H_

#include <stdint.h>
#include "MGRAS.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * ARCHITECTURE OVERVIEW: PP1 PIXEL PROCESSOR & RDRAM
 * ============================================================================
 *
 * Gate Count:      ~120,000 gates per chip
 * Operating Clock: 50 MHz core / 500 MHz Rambus channel
 *
 * PP1 is the backend memory interface ASIC in the Raster Subsystem (RSS).
 *
 * Key Architecture:
 * 1. MULTI-CHIP INTERLEAVED RAMBUS INTERFACE:
 *    - Two PP1s per RE4: Solid/High Impact (one RE4) have 2 PP1 and 12MB of
 *      RDRAM; Super Solid/Maximum Impact (two RE4s) have 4 PP1 and 24MB
 *      (Octane technical report, Table 4.1; IDE diags PP_PER_RSS = 2,
 *      RDRAM_PER_PP = 3: three 2MB RDRAMs per PP1).
 *    - Each PP1 manages an independent 9-bit Rambus Channel connecting to
 *      high-speed RDRAM devices (Rambus DRAM). Pixels and scanlines are interleaved
 *      across PP1 chips to maximize parallel memory bandwidth.
 *
 * 2. RAC (RAMBUS ASIC CELL) CALIBRATION:
 *    - High-frequency Rambus channels require precise current control (CCR) and
 *      impedance matching calibration on boot.
 *    - The kernel module mgras_ccv.o executes PhaseOne and PhaseTwo current calibration,
 *      adjusting driving strength and finding the median tap for reliable signaling.
 *
 * 3. BACKEND PER-FRAGMENT OPERATIONS PIPELINE:
 *    - Scissor & Screen Mask Test (passed from RE4)
 *    - Window ID / CID comparison (pp1winmode)
 *    - Alpha Test: Afuncmode register (comparison against reference alpha)
 *    - Stencil Test: Stencilmode & Stencilmask registers (8-bit stencil buffer)
 *    - Depth / Z-Buffer Test: Zmode register (comparison of 24-bit integer or floating Z)
 *    - Alpha Blending: BlendFactor, BlendcolorRG, BlendcolorBA
 *    - Dithering & Color Clamping
 *    - Logic Operations (ROP): 16 standard OpenGL/X11 raster operations
 *    - Framebuffer Write Masks: ColorMaskMSBs, ColorMaskLSBsA, ColorMaskLSBsB
 */

/*
 * ============================================================================
 * PP1 CONTROL REGISTERS (Mapped in RSS Space 0x0160 - 0x017F)
 * ============================================================================
 */
#define PP1_REG_PIXCMD              0x0160      /* Pixel command / execute */
#define PP1_REG_FILLMODE            0x0161      /* PP1 fill mode & buffer routing */
#define PP1_REG_COLORMASK_MSB       0x0162      /* Color write mask MSBs (RGB) */
#define PP1_REG_COLORMASK_LSB_A     0x0163      /* Color write mask LSBs (Buffer A) */
#define PP1_REG_COLORMASK_LSB_B     0x0164      /* Color write mask LSBs (Buffer B) */
#define PP1_REG_BLEND_FACTOR        0x0165      /* Source & dest alpha blend factors */
#define PP1_REG_STENCIL_MODE        0x0166      /* Stencil test function & opcodes */
#define PP1_REG_STENCIL_MASK        0x0167      /* Stencil read/write mask */
#define PP1_REG_Z_MODE              0x0168      /* Z-buffer test function & write enable */
#define PP1_REG_AFUNC_MODE          0x0169      /* Alpha test function & reference value */
#define PP1_REG_ACC_MODE            0x016A      /* Accumulation buffer control */
#define PP1_REG_BLEND_COLOR_RG      0x016B      /* Constant blend color (Red & Green) */
#define PP1_REG_BLEND_COLOR_BA      0x016C      /* Constant blend color (Blue & Alpha) */
#define PP1_REG_DRB_POINTERS        0x016D      /* Display Read Buffer (DRB) pointers */
#define PP1_REG_DRB_SIZE            0x016E      /* DRB line buffer size */
#define PP1_REG_TAG_MODE            0x016F      /* Tag mode control */
#define PP1_REG_TAG_DATA_R          0x0170      /* Tag readback data Red */
#define PP1_REG_TAG_DATA_G          0x0171      /* Tag readback data Green */
#define PP1_REG_TAG_DATA_B          0x0172      /* Tag readback data Blue */
#define PP1_REG_TAG_DATA_A          0x0173      /* Tag readback data Alpha */
#define PP1_REG_TAG_DATA_Z          0x0174      /* Tag readback data Depth */
#define PP1_REG_PIX_COORD           0x0175      /* Direct pixel coordinate (Y:16, X:16) */
#define PP1_REG_PIX_COLOR_R         0x0176      /* Direct pixel Red */
#define PP1_REG_PIX_COLOR_G         0x0177      /* Direct pixel Green */
#define PP1_REG_PIX_COLOR_B         0x0178      /* Direct pixel Blue */
#define PP1_REG_PIX_COLOR_A         0x0179      /* Direct pixel Alpha */
#define PP1_REG_PIX_COLOR_ACC       0x017A      /* Direct pixel Accumulation */
#define PP1_REG_WINMODE             0x017B      /* PP1 Window mode */
#define PP1_REG_MAIN_BUF_SEL        0x017E      /* Main display buffer select (front/back) */
#define PP1_REG_OLAY_BUF_SEL        0x017F      /* Overlay buffer select */

/*
 * ============================================================================
 * PP1 WINDOW MODE AND CLIP IDS (PP1_REG_WINMODE: 0x017B)
 * ============================================================================
 *
 * SGI's field names: WINxLSBs, WINyLSBs, CIDmatch, CIDdata, CIDmask.
 *
 * Clip IDs (CIDs) are two bits per pixel, one set for the whole screen
 * (not per buffer or per window). The X server gives a window an ID (1..3,
 * FindFreeCID) when its visible region needs more rectangles than the four
 * screen masks hold (mgrasNeedCID), and paints the region with it
 * (mgrasDrawCID): PP1 fill mode 0x142600 (draw field 0x50, PP1_FM_DRAW_CID),
 * the ID in the fast-fill red (0x176), ColorMaskMSBs 0xFF, pp1winmode
 * 0xC00, then one block per clip rectangle. At startup it clears the screen
 * to ID 0 the same way. It leaves pp1winmode at 0xC00 afterwards; with
 * CIDmatch 0 its own drawing is not checked.
 *
 * The kernel (MgrasValidateClip) gives a GL window pp1winmode
 *   (x & 3) | (y & 3) << 2 | (window has a CID ? 1 << (4 + id) : 0)
 * with x, y the window origin (y bottom-up), and sends it in the window
 * state: context image word 4 (after origin and winmode) and word 2 of the
 * CP_WINDOW (0xE4) token, whose second doubleword is winmode << 32 |
 * pp1winmode. Traced (6.5.22, a GL root-window background under a control
 * panel): X painted ID 1, the context carried 0x20.
 *
 * CIDmatch is one bit per ID: a pixel whose stored ID is n is drawn only if
 * bit 4 + n is set; all four clear means no check (the PROM and the X server
 * keep it 0). CIDmask at bits 11:10 write-enabling the two ID planes is our
 * reading of 0xC00; CIDdata (9:8 by elimination) has not been seen nonzero.
 * Where the IDs sit in RDRAM is not known: X draws them with DRBpointers at
 * the main buffer.
 */
#define PP1_WINMODE_WINX_LSBS       0x00000003  /* window origin x & 3 */
#define PP1_WINMODE_WINY_LSBS       0x0000000C  /* window origin y & 3 */
#define PP1_WINMODE_CIDMATCH(id)    BIT(4 + (id)) /* draw where CID == id */
#define PP1_WINMODE_CIDMATCH_MASK   0x000000F0
#define PP1_WINMODE_CIDDATA_MASK    0x00000300  /* by elimination, unseen */
#define PP1_WINMODE_CIDMASK_MASK    0x00000C00  /* CID plane write enables */

/*
 * ============================================================================
 * PP1 FILL MODE REGISTER BITS (PP1_REG_FILLMODE: 0x0161)
 * ============================================================================
 */
#define PP1_FM_LOGICOP_EN           BIT(2)      /* Enable logic operation */

/* Pixel Types (bits 10:8) */
#define PP1_FM_PIXTYPE_MASK         (0x7 << 8)
#define PP1_FM_PIXTYPE_CI8          (0 << 8)    /* 8-bit Color Index */
#define PP1_FM_PIXTYPE_RGB12        (1 << 8)    /* 12-bit RGB (4:4:4) */
#define PP1_FM_PIXTYPE_RGBA8888     (2 << 8)    /* 32-bit RGBA (8:8:8:8) */
#define PP1_FM_PIXTYPE_RGB121212    (4 << 8)    /* 36-bit High-Color RGB */
#define PP1_FM_PIXTYPE_RGBA9999     (5 << 8)    /* 36-bit RGBA */
#define PP1_FM_PIXTYPE_CI12         (6 << 8)    /* 12-bit Color Index */
#define PP1_FM_PIXTYPE_ZST32        (8 << 8)    /* 24-bit Depth + 8-bit Stencil */

/*
 * 12-bit pixel pairs (traced 6.5.22, octahedra / electropaint / glprim
 * 4,4,4,4 and 5,5,5,1 visuals, and the X server's 12-bit windows): with
 * pixel type 0 or 1 and bit 13 clear, a 24-bit word holds two 12-bit
 * pixels, buffer A in bits 11:0 and buffer B in bits 23:12, each red in
 * bits 3:0, green 7:4, blue 11:8 (electropaint's dump matched every drawn
 * colour this way; swapping red and blue matched 3%). This is how 12-bit
 * visuals double-buffer when there is no room for a second 24-bit page
 * (1280x1024 HighImpact), and they do it at 1024x768 too, where a second
 * page exists. Draw field 1 = A, 2 = B, 3 = both; the colour is written
 * to both halves and ColorMaskLSBs picks them (X: 0xFFF for A, 0xFFF000
 * for B, 0xFFFFFF for both). Read field 1 reads B. DRBpointers stays at
 * A's page for both buffers; reading "the back buffer" from the window's
 * second page returns unrelated memory. Drawn values are the 8-bit
 * component >> 4 (X's fills are n << 4); our scanout widens a nibble n to
 * n * 0x11 before the DAC gamma (replication, unconfirmed on hardware).
 * Plane masks: ColorMaskLSBsA / B (0x163 / 0x164) are planes 31:0 of a
 * 36-bit pixel, ColorMaskMSBs (0x162) planes 35:32 (and the overlay index
 * mask). libGLcore's InitFormatValues keeps a 32-bit mask per buffer and
 * writes a third, small value straight to 0x162: 0 for 8,8,8,8 (masks all
 * ones), 0xF for 12,12,12 (masks all ones, all 36 planes), 0x70 or 7 for
 * colour-index visuals. The layouts by component depth:
 *   4 bits  (RGBA 4,4,4,4)  format 0      A 0x0F000FFF  B 0xF0FFF000
 *           RGB 4:4:4 in 11:0 / 23:12, alpha nibble in 27:24 / 31:28
 *   5 bits  (RGBA 5,5,5,1)  format 0x100  A 0x40007FFF  B 0xBFFF8000
 *           RGB 5:5:5 in 14:0 / 29:15, alpha bit 30 / 31
 *   8 bits  (RGBA 8,8,8,8)  format 0x2200, masks all ones (one buffer a
 *           page)
 *   12 bits (RGBA 12,12,12,0) format 0x2300 (pixel type 3, bit 13),
 *           masks all ones + MSBs 0xF: 36-bit RGB. The X server fills its
 *           24-bit windows with this pixel type too (fill mode 0xC006304,
 *           mask 0xFFFFFF, 12-bit fill colours), so the top 8 bits of
 *           each component sit in the 8-8-8 places (7:0, 15:8, 23:16)
 *           and the low nibbles in planes 35:24 (red 27:24, green 31:28,
 *           blue 35:32 in our guess). Double buffered on a second page,
 *           DRAW_BUFFER [4, 1].
 *   0x2400  masks 0xFFF / 0xFFF000: the 12-bit "rgba 12,0,0,0" pbuffer
 *           configs (pixel type 4), meaning open.
 *   colour index: CI12 format 0x400, masks 0xFFF / 0xFFF000; CI8 0x600
 *           (or 0x500) with 0xFF / 0xFF000
 * So 5,5,5,1 is real (15-bit halves), not 4:4:4; the component order
 * inside the 15 bits is not known (red lowest by analogy). iris models
 * formats 0 and 0x100 alike as 4:4:4 halves without alpha (2026-10-10).
 * GL_DITHER (HQ3 token 0x067) is sent, but no fill mode bit for dither
 * has been seen; our model dithers GL's 12-bit writes with REX3's 4x4
 * Bayer matrix (a model-private bit 3).
 */

/* Buffer Configuration (bits 13:11) */
#define PP1_FM_BUFCOUNT_MASK        (0x3 << 11) /* Number of color buffers */
#define PP1_FM_BUFSIZE_MASK         BIT(13)     /* Single vs Double buffer size */

/* Draw Buffer Targets (bits 20:14) */
#define PP1_FM_DRAWBUF_MASK         (0x7F << 14)
#define PP1_FM_DRAW_NONE            (0 << 14)
#define PP1_FM_DRAW_FRONT_A         (1 << 14)   /* Front Buffer A */
#define PP1_FM_DRAW_BACK_A          (2 << 14)   /* Back Buffer A */
#define PP1_FM_DRAW_FRONT_B         (4 << 14)   /* Front Buffer B (Stereo) */
#define PP1_FM_DRAW_BACK_B          (8 << 14)   /* Back Buffer B (Stereo) */
#define PP1_FM_DRAW_OVERLAY         (16 << 14)  /* Overlay Planes */
#define PP1_FM_DRAW_CID             0x142600    /* Window ID Planes */
/*
 * As the 6.5.22 X server and libGLcore use the draw field: 1 buffer A
 * (DRBpointers 9:0), 2 buffer B (the high 12-bit half for pixel pairs, the
 * second page otherwise), 3 both; 4 buffer B as DRBpointers' second page
 * (19:10) and 5 A plus that page, for 24-bit double buffering: GL's
 * DRAW_BUFFER [4, 1], and the X server's double-buffered 24-bit windows
 * (System Manager's panes: fill modes 0xC012204 / 0xC016A04, swapped by
 * XMAP BUF_SELECT; traced 2026-10-13). Taking 4 for A drew those windows'
 * back buffers over their front and left B stale: a GL program's last
 * frame showed through after it exited.
 */

/* Read Buffer Targets (bits 25:21) */
/*
 * Overlay access as the 6.5.22 X server does it (traced): writes with
 * PP1 fill mode 0xC13F504 (pixel type 5, draw field 0x4F), ColorMaskLSBsA
 * 0 and ColorMaskMSBs 0xF0 (overlay visuals, values 0-15) or 0x70 (popup
 * menus, values 0-3: their colormap has entries 0 transparent, 1 grey, 2
 * black, 3 white) -- the high nibble masking overlay planes 3:0 in our
 * reading. Reads of the overlay back to the host (popup menu bodies are
 * copied through host memory) use fill mode 0x802500: read field 4, draw
 * field 0, DRBpointers at the overlay's pages.
 */
#define PP1_FM_READBUF_MASK         (0x1F << 21)
#define PP1_FM_READ_FRONT_A         (0 << 21)
#define PP1_FM_READ_BACK_A          (1 << 21)
#define PP1_FM_READ_OVERLAY         (2 << 21)

/* Logic Operations (bits 29:26) */
#define PP1_FM_LOGICOP_MASK         (0xF << 26)
#define PP1_LOGICOP_CLEAR           (0x0 << 26) /* 0 */
#define PP1_LOGICOP_AND             (0x1 << 26) /* s & d */
#define PP1_LOGICOP_AND_REVERSE     (0x2 << 26) /* s & ~d */
#define PP1_LOGICOP_COPY            (0x3 << 26) /* s */
#define PP1_LOGICOP_AND_INVERTED    (0x4 << 26) /* ~s & d */
#define PP1_LOGICOP_NOOP            (0x5 << 26) /* d */
#define PP1_LOGICOP_XOR             (0x6 << 26) /* s ^ d */
#define PP1_LOGICOP_OR              (0x7 << 26) /* s | d */
#define PP1_LOGICOP_NOR             (0x8 << 26) /* ~(s | d) */
#define PP1_LOGICOP_EQUIV           (0x9 << 26) /* ~(s ^ d) */
#define PP1_LOGICOP_INVERT          (0xA << 26) /* ~d */
#define PP1_LOGICOP_OR_REVERSE      (0xB << 26) /* s | ~d */
#define PP1_LOGICOP_COPY_INVERTED   (0xC << 26) /* ~s */
#define PP1_LOGICOP_OR_INVERTED     (0xD << 26) /* ~s | d */
#define PP1_LOGICOP_NAND            (0xE << 26) /* ~(s & d) */
#define PP1_LOGICOP_SET             (0xF << 26) /* 1 */

/*
 * ============================================================================
 * ALPHA BLENDING FACTORS (PP1_REG_BLEND_FACTOR: 0x0165)
 * ============================================================================
 */
#define PP1_BLEND_SRC_SHIFT         0
#define PP1_BLEND_SRC_MASK          0x0F
#define PP1_BLEND_DST_SHIFT         4
#define PP1_BLEND_DST_MASK          0xF0

/* Blend Factor Encodings */
#define PP1_BLEND_ZERO              0x0
#define PP1_BLEND_ONE               0x1
#define PP1_BLEND_DST_COLOR         0x2
#define PP1_BLEND_ONE_MINUS_DST_CLR 0x3
#define PP1_BLEND_SRC_ALPHA         0x4
#define PP1_BLEND_ONE_MINUS_SRC_ALP 0x5
#define PP1_BLEND_DST_ALPHA         0x6
#define PP1_BLEND_ONE_MINUS_DST_ALP 0x7
#define PP1_BLEND_SRC_ALPHA_SATURATE 0x8
#define PP1_BLEND_CONST_COLOR       0x9
#define PP1_BLEND_ONE_MINUS_CONST_CLR 0xA
#define PP1_BLEND_CONST_ALPHA       0xB
#define PP1_BLEND_ONE_MINUS_CONST_ALP 0xC

/*
 * Subtract blends on a real High IMPACT (IP28, 6.5.22, blendtest -a, one
 * cell a legal GL factor pair; ADD, MIN, MAX and the logic ops all follow
 * GL). In GL terms:
 *   SUBTRACT drops the destination term when the source factor is an alpha
 *   or constant one (SRC_ALPHA, DST_ALPHA, CONSTANT_COLOR, CONSTANT_ALPHA,
 *   their ONE_MINUS forms, SRC_ALPHA_SATURATE) and the destination factor
 *   is ONE_MINUS of a different one of those (SATURATE: also against
 *   ONE_MINUS_SRC_COLOR). SRC_ALPHA / ONE_MINUS_SRC_ALPHA and the colour
 *   factors are fine.
 *   REVERSE_SUBTRACT with source factor ONE_MINUS_{SRC_ALPHA, DST_ALPHA,
 *   CONSTANT_COLOR, CONSTANT_ALPHA} uses the factor without the ONE_MINUS
 *   and drops the destination term under the same conflict or for
 *   SRC_COLOR; ONE_MINUS_DST_COLOR as source factor drops it for SRC_COLOR
 *   and the alpha / constant factors.
 *   309 of 312 cells; REVERSE_SUBTRACT with an inverted source factor over
 *   SRC_COLOR matches no factor pair. How the GE programs the equation
 *   into this register is not known (iris writes its own encoding).
 */

/*
 * ============================================================================
 * STENCIL & DEPTH TESTS (PP1_REG_STENCIL_MODE, PP1_REG_Z_MODE)
 *
 * The ZST buffer (z24s8 visuals; the only depth layout 6.5.22 offers):
 * one 32-bit word a pixel, Z in bits 23:0, stencil in 31:24. glClear's
 * depth part fills planes 23:0 (ColorMaskLSBs 0xFFFFFF, MSBs 0), its
 * stencil part planes 31:24 through the stencil write mask (LSBs 0, MSBs
 * = mask). These words are not colours: a 12-bit window's pair packing or
 * a colour-index pixel type must not touch them (our bug, 2026-10-10).
 * glReadPixels of depth returns Z << 8 | Z >> 16 (32 bits full scale); of
 * stencil, the top byte (HQ3.h, SAVE_RSS planes).
 * ============================================================================
 */

/* Stencil Test Functions (bits 2:0) */
#define PP1_TEST_NEVER              0
#define PP1_TEST_LESS               1
#define PP1_TEST_EQUAL              2
#define PP1_TEST_LEQUAL             3
#define PP1_TEST_GREATER            4
#define PP1_TEST_NOTEQUAL           5
#define PP1_TEST_GEQUAL             6
#define PP1_TEST_ALWAYS             7

/* Stencil Operations (Fail, ZFail, ZPass: bits 5:3, 8:6, 11:9) */
#define PP1_STENCIL_OP_KEEP         0
#define PP1_STENCIL_OP_ZERO         1
#define PP1_STENCIL_OP_REPLACE      2
#define PP1_STENCIL_OP_INCR         3
#define PP1_STENCIL_OP_DECR         4
#define PP1_STENCIL_OP_INVERT       5

/* Z Mode Control Bits (PP1_REG_Z_MODE: 0x0168) */
#define PP1_Z_FUNC_MASK             0x07
#define PP1_Z_WRITE_ENABLE          BIT(3)      /* Enable Z-buffer writing */

/*
 * ============================================================================
 * PIXEL MEMORY LAYOUT AND BUFFER POINTERS
 * ============================================================================
 *
 * There is no linear framebuffer. RDRAM bytes are 9 bits; pixel memory is a
 * set of whole-screen BUFFERS, each one 36-bit word per pixel (four 9-bit
 * bytes), plus one 9-bit overlay buffer (8-bit CI + 1 spare bit). A buffer
 * is a run of RDRAM pages; a pointer is the page number it starts at.
 *
 * Where pixel (x, y) lives (IDE diag _xy_to_rdram, "algorithm 0"):
 *   RE4      = y % numRE4s                 (scanlines interleave over RE4s)
 *   PP1      = x % 2
 *   y_hat    = y / numRE4s
 *   x_hat    = ((x / 12) << 1) | ((x >> 1) & 1)
 *   line     = y_hat % 3
 *   RDRAM    = ((x / 4) % 3 + line) % 3: the chip owning each 4-pixel
 *              group rotates by one every scanline (both diag variants)
 *   tile     = 192 x 16 pixels (per RE4) = one 2KB page per chip:
 *              page = (y_hat / 16) * xtiles + x_hat / 32,
 *              xtiles = ceil(width / 192)  (7 at 1280, 6 at 1024)
 *   word     = ((y_hat & 15) << 5 | (x_hat & 31)), byte address word * 4
 *   device address (RE4 DEVICE_ADDR, indirect) =
 *              PP1 << 28 | (4 + RDRAM) << 24 | (base + page) << 11 | word * 4
 *              (+2 for the high 18-bit halfword; 0x07000000 broadcasts)
 *
 * Pages per 36-bit buffer = xtiles * (height / numRE4s / 16):
 *   1280x1024, one RE4: 7 * 64 = 448     1024x768: 6 * 48 = 288
 * Overlay pages are the same pages read as four 9-bit pixels a word:
 * 768 x 16 pixels each, Xtiles9 = ceil(width / 768) to a row, so an
 * overlay buffer is Xtiles9 * tile rows pages (128 at 1280x1024, 96 at
 * 1024x768). Each RDRAM has 1024 pages (2MB / 2KB): 1280x1024 holds
 * 2 * 448 + 128 = 1024 pages exactly (two 36-bit buffers + overlay), the
 * buffer counts the Octane report's Table 4.2 gives.
 *
 * DRBsize (0x16E), per MgrasSetRSSTiming (libsk mgras_init.c) and the IDE
 * diags (mgras_info.c packs bits 10:6 as "drb_misc = 0x9: DRB_RSS=1,
 * DMAlinecnt=2, AFE=0"; mgras_hqpio_redma.c sets DRB_RSS = numRE4s - 1):
 *   bits 1:0   DRB_Xtiles9  = ceil(width / 768)
 *   bits 5:2   DRB_Xtiles36 = ceil(width / 192)
 *   bits 7:6   DRB_RSS      = raster engines - 1 (PP_1_RSS / PP_2_RSS)
 *   bits 9:8   DMA_line_count_mask (PP_DMA_LCM_1_RSS / _2_RSS; 3 and 2
 *              as traced)
 *   bit  10    AutoFlushEnab_n (0 = auto flush on)
 *   top byte   unknown (6.5.22 writes 0x90)
 * Traced (6.5.22, IP28, 1024x768): High Impact 0x9000031A (6, 2, one
 * RSS, LCM 3); Maximum Impact 0x9000025A (6, 2, two RSS, LCM 2).
 *
 * MAXIMUM IMPACT (two RE4s, each with its own two PP1s and 12MB): every
 * RE4 lays out ITS scanlines (y % 2 == RE4 number, row y / 2) exactly as
 * one RE4 lays out all of them, so a 36-bit buffer is half as many pages
 * per RE4 (1024x768: 6 * 24 = 144) and the same page pointer names the
 * same buffer on both. A tile is 192 x 32 screen rows. Pointers the
 * 6.5.22 X server and kernel use at 1024x768 (XMAP DIB 0x090DC2E0):
 * main 0x2E0 (736), second 0x370 (880 = 736 + 144), overlay 0x090 (144);
 * DRBpointers for X drawing 0xB81C0 as on High Impact. Writes broadcast
 * to both RE4s (config DestRE = CONFIG_RE4_ALL, the PROM's default), and
 * each draws only its own scanlines; the XMAP CONFIG register carries
 * the RSS count too (XMAP_CONFIG_DEFAULT(numRE), bits 21:20). With a
 * one-RE4 layout assumed, buffers overlap and the screen shows bands of
 * 16 rows (seen in iris before the fix).
 *
 * Drawing: both X servers put the target buffer's page in DRBpointers
 * bits 9:0 and switch it for overlay drawing (6.5.0: 0x90240 main /
 * 0x101C0 overlay; 6.5.22: 0xB81C0 / 0x38120); the fill mode DrawBuffer
 * field (diag names: DBUFA = 0x01, DOVERA_OR_B = 0x41; X uses 0x4F for the
 * overlay, 0x50 for window IDs) says how to read that buffer's words.
 *
 * Pointer values (PROM timing tables, 6.5.22 X server):
 *   1280x1024: aux 0x000, overlay 0x1C0 (448), main 0x240 (576)
 *              -> aux 0..447, overlay 448..575, main 576..1023
 *   1024x768:  main 0x1C0 (448), second 0x2E0 (736), overlay 0x120 (288)
 *              -> 0..287, overlay 288..383, 448..735, 736..1023
 *              (384..447 unexplained)
 * PP1_REG_DRB_POINTERS takes a buffer's page pointer: which buffer the
 * PP1 draws into / reads from (the diags write 10 bits, & 0x3FF). A
 * pbuffer or back buffer is another buffer's pages, on the same (x, y)
 * grid as the screen. XMAP DIB register 0 holds the scanout pointers:
 *   [9:0] main, [19:10] second buffer (unconfirmed meaning; 0x320 = 576 +
 *   224 at 1280x1024), [31:20] overlay. 0x1C0C8240 at 1280x1024,
 *   0x120B81C0 at 1024x768.
 */
#define DRB_PTR_MASK                0x3FF       /* page pointer, 10 bits */
#define DRB_OVERLAY                 0x1C0       /* 1280x1024 overlay buffer (PROM) */
#define DRB_MAIN                    0x240       /* 1280x1024 main buffer (PROM) */
#define DIB_PTR_MAIN(v)             ((v) & 0x3FF)
#define DIB_PTR_SECOND(v)           (((v) >> 10) & 0x3FF)
#define DIB_PTR_OVERLAY(v)          (((v) >> 20) & 0xFFF)

/*
 * ============================================================================
 * RAC (RAMBUS ASIC CELL) REGISTERS & INITIALIZATION
 * ============================================================================
 */
#define RAC_CONTROL                 0x08000000  /* RAC Master Control register */

/* RAC Control Bits */
#define RAC_CTRL_STOPT              BIT(0)      /* Stop transmit clocks */
#define RAC_CTRL_STOPR              BIT(1)      /* Stop receive clocks */
#define RAC_CTRL_PWRUP              BIT(2)      /* Power up internal analog PLL */
#define RAC_CTRL_EXTBE              BIT(3)      /* External byte enable */
#define RAC_CTRL_SOUTI              BIT(4)      /* Scan out invert */
#define RAC_CTRL_RESET              BIT(5)      /* Reset RAC logic */
#define RAC_CTRL_CCT1LD             BIT(6)      /* Load current control register tap */
#define RAC_CTRL_CCT1EN             BIT(7)      /* Enable current control tap calibration */
#define RAC_CTRL_CCTLL_MASK         (0x3F << 8) /* Current control tap value (0..63) */
#define RAC_CTRL_SCANIN             BIT(14)
#define RAC_CTRL_SCANEN             BIT(15)
#define RAC_CTRL_SCANMODE           BIT(16)
#define RAC_CTRL_SCANCLK            BIT(17)
#define RAC_CTRL_BISTMODE           BIT(18)
#define RAC_CTRL_BYPASS             BIT(19)
#define RAC_CTRL_NACKERR            BIT(20)
#define RAC_CTRL_PREINIT            BIT(21)     /* Pre-initialization flag */
#define RAC_CTRL_FORCEBE            BIT(22)
#define RAC_CTRL_RXSEL_MASK         (0xF << 22)
#define RAC_CTRL_TXSEL_MASK         (0xF << 26)

/*
 * ============================================================================
 * RDRAM DIRECT REGISTERS (Per-Device Configuration)
 * ============================================================================
 */
#define RD_DEVICE_TYPE              0x000000    /* Rambus device architecture & type */
#define RD_DEVICE_ID                0x000004    /* Device serial ID */
#define RD_DELAY                    0x000008    /* Input/output pin skew delay */
#define RD_MODE                     0x00000C    /* Read/write timing mode */
#define RD_REF_INTERVAL             0x000010    /* DRAM row refresh interval */
#define RD_REF_ROW                  0x000014    /* Current row under refresh */
#define RD_RAS_INTERVAL             0x000018    /* RAS precharge time */
#define RD_MIN_INTERVAL             0x00001C    /* Minimum cycle time */
#define RD_ADDR_SELECT              0x000020    /* Channel base address decode */
#define RD_DEVICE_MANUF             0x000024    /* Silicon manufacturer code */
#define RD_DUMP                     0x0000D0
#define RD_ROW                      0x000200    /* Active sense amplifier row */

#ifdef __cplusplus
}
#endif

#endif /* _SGI_PP1_H_ */
