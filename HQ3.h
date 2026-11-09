/*
 * HQ3.h - Silicon Graphics Impact Graphics (MGRAS) HQ3/HQ4 Command Engine
 *
 * Command Engine, Host Bus Interface, FIFO Controller, and Graphics TLB for SGI Impact (GR4).
 *
 * Reverse-engineered documentation and single source of truth for IRIS emulator.
 * Synthesized from:
 *   - SGI IRIX PROM (stand/arcs/lib/libsk/graphics/MGRAS)
 *   - SGI IDE diagnostics (stand/arcs/ide/isddevices/graphics/MGRAS/mgras_hq3.c, mgras_hq3_cp.c, mgras_host_hqdma.c)
 *   - IRIX 6.5.0 kernel modules (mgras.o, mgras_init.o, mgras_dma.o)
 *   - Existing IRIS emulator implementation (~/gits/iris/src/dev/mgras/mod.rs)
 */

#ifndef _SGI_HQ3_H_
#define _SGI_HQ3_H_

#include <stdint.h>
#include "MGRAS.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * ARCHITECTURE OVERVIEW: HQ3 / HQ4 COMMAND ENGINE
 * ============================================================================
 *
 * Gate Count:      ~150,000 gates
 * Operating Clock: 33 MHz (GIO64 on Indigo2) / 100 MHz (XIO Crosstalk on Octane HQ4)
 * Microcode RAM:   6,144 instructions x 24 bits (loaded from hq3.bin)
 *
 * HQ3 is the master interface ASIC connecting the host CPU to the Impact graphics
 * subsystem. It coordinates data transfers, provides command buffering, runs
 * microcoded context switching, and drives the peripheral Display Control Bus (DCB).
 *
 * Key Functional Blocks:
 *
 * 1. HOST BUS INTERFACE:
 *    - Decodes the 4 MB GIO64 graphics slot at 0x1F000000 (Indigo2) or XIO widget (Octane).
 *    - Provides endian conversion, bus arbitration, and burst handling.
 *    - Maps hardware apertures:
 *        0x00000 - 0x3FFFF : GIO identification word (0x00050190)
 *        0x40000 - 0x45FFF : Microcode RAM (6K x 24-bit words)
 *        0x46000           : Microcode Program Counter (HQPC)
 *        0x47000           : Command Processor Data (CP_DATA)
 *        0x4C000           : RSS Diagnostic register
 *        0x50000 - 0x5FFFF : Privileged registers, FIFOs, TLB, Context RAMs
 *        0x60000 - 0x67FFF : Display Control Bus (DCB) device windows
 *        0x68000 - 0x6FFFF : DCB protocol timing registers
 *        0x70000 - 0x7BFFF : User control registers and status flags
 *        0x7C000 - 0x7DFFF : Direct RSS raster register file (+0x1000: execute)
 *        0x80000 - 0xFFFFF : Fast Command FIFO window (CFIFO)
 *
 * 2. COMMAND PROCESSOR & MICROCODE SEQUENCER:
 *    - Programmable RISC engine running high-level command sequences (hq3.bin).
 *    - Manages context switching between OpenGL clients and the X server.
 *    - Dispatches tokens to the GE11 Geometry Engines and Raster Subsystem (RSS).
 *    - Handles display list execution, vertex unpacking, and buffer swaps.
 *
 * 3. MULTI-TIER FIFO SUBSYSTEM:
 *    - CFIFO (Command FIFO): Main FIFO for rendering tokens and register writes.
 *      Has programmable high/low watermarks (CFIFO_HW, CFIFO_LW) and stall delay counters.
 *    - DFIFO (DMA FIFO): Buffers incoming DMA packets between system RAM and graphics.
 *    - BFIFO (Buffer FIFO): Serializes DCB bus transactions and buffer swap events.
 *
 * 4. GRAPHICS TLB (GTLB) & VIRTUAL DMA:
 *    - 32-entry fully-associative TLB for Virtual DMA transfers.
 *    - Translates 4 KB virtual pages to physical memory addresses during Host-to-Device (H2D)
 *      and Device-to-Host (D2H) DMA without CPU intervention.
 *
 * 5. HOST ADDRESS GENERATOR (HAG):
 *    - Hardware DMA engine capable of walking rectangular 2D image regions, stride skipping,
 *      and display list indirect chaining.
 *
 * 6. FORMATTER / CONVERTER (FC):
 *    - Converts host pixel formats (1, 2, 4 components, 8/16/32-bit, nibbles) into canonical
 *      graphics representations with hardware endian and byte swizzling.
 */

/*
 * ============================================================================
 * REGISTER BYTE OFFSETS (Relative to Slot Base 0x1F000000)
 * ============================================================================
 */

/* Microcode RAM & Command Processor */
#define HQ3_UCODE_BASE              0x40000
#define HQ3_UCODE_SIZE              0x06000     /* 24 KB (6K x 32-bit word entries) */
#define HQ3_PC                      0x46000     /* Microcode Program Counter */
#define HQ3_CP_DATA                 0x47000     /* Command Processor Data Register */
#define HQ3_RSS_DIAG                0x4C000     /* RSS Diagnostic Register */

/* Privileged Control Registers (0x50000 - 0x5FFFF) */
#define HQ3_CONFIG                  0x50000     /* HQ3 Master Configuration */
#define HQ3_FLAG_CTX                0x50004     /* Context Flag Register */
#define HQ3_FLAG_SET_PRIV           0x50008     /* Privileged Flag Set Register */
#define HQ3_FLAG_CLR_PRIV           0x5000C     /* Privileged Flag Clear Register */
#define HQ3_FLAG_ENAB_SET           0x50010     /* Interrupt Enable Set */
#define HQ3_FLAG_ENAB_CLR           0x50014     /* Interrupt Enable Clear */
#define HQ3_INTR_ENAB_SET           0x50018     /* Interrupt Mask Set */
#define HQ3_INTR_ENAB_CLR           0x5001C     /* Interrupt Mask Clear */

/* FIFO Watermarks and Delays */
#define HQ3_CFIFO_HW                0x50020     /* CFIFO High Watermark (Stall host) */
#define HQ3_CFIFO_LW                0x50024     /* CFIFO Low Watermark (Resume host) */
#define HQ3_CFIFO_DELAY             0x50028     /* CFIFO Delay Count */
#define HQ3_DFIFO_HW                0x5002C     /* DFIFO High Watermark */
#define HQ3_DFIFO_LW                0x50030     /* DFIFO Low Watermark */
#define HQ3_DFIFO_DELAY             0x50034     /* DFIFO Delay Count */

/* Geometry Engine Diagnostics */
#define HQ3_GE0_DIAG_D              0x50040     /* GE11 ASIC 0 Diagnostic Data */
#define HQ3_GE0_DIAG_A              0x50044     /* GE11 ASIC 0 Diagnostic Address */
#define HQ3_GE1_DIAG_D              0x50048     /* GE11 ASIC 1 Diagnostic Data */
#define HQ3_GE1_DIAG_A              0x5004C     /* GE11 ASIC 1 Diagnostic Address */

/* Command FIFO Privileged Port */
#define HQ3_CTXSW                   0x50050     /* Context switch request: microcode address of the save routine (0x4FC), see CONTEXT MANAGEMENT */
#define HQ3_CFIFO_PRIV              0x50080     /* Privileged CFIFO port (64-bit; word at +4) */
#define HQ3_CTX_DATA                0x50100     /* Context Switch Data port */

/* Buffer FIFO & Bus Config */
#define HQ3_BFIFO_HW                0x50108     /* BFIFO High Watermark */
#define HQ3_BFIFO_LW                0x5010C     /* BFIFO Low Watermark */
#define HQ3_BFIFO_DELAY             0x50110     /* BFIFO Delay Count */
#define HQ3_GIO_CONFIG              0x50114     /* GIO Bus Interface Config */

/* Context Save/Restore RAMs (Internal SRAM blocks) */
#define HQ3_REIF_CTX                0x50200     /* RE Interface Context RAM (0x100 B) */
#define HQ3_HAG_CTX                 0x50300     /* Host Address Generator Context (0x200 B) */
#define HQ3_HIF_CTX                 0x50500     /* Host Interface Context (0x100 B) */
#define HQ3_GIO_CTX                 0x50600     /* GIO Bus Context (0x100 B) */

/* Virtual DMA / Graphics TLB (GTLB) */
#define HQ3_TLB_BASE                0x50800     /* TLB Entry Array (32 entries x 4B) */
#define HQ3_TLB_CURR_ADDR           0x50880     /* Current Faulting Virtual Address */
#define HQ3_TLB_VALIDS              0x50900     /* TLB Entry Valid Bitmask (write 0 to flush) */

/* User Registers (0x70000 - 0x7BFFF) */
#define HQ3_STATUS                  0x70000     /* User Status Register (idle, vblank, etc.) */
#define HQ3_FIFO_STATUS             0x70004     /* FIFO Status (depths, valid count) */
#define HQ3_FLAG_SET                0x70008     /* User Flag Set Register */
#define HQ3_FLAG_CLR                0x7000C     /* User Flag Clear Register */
#define HQ3_REBUS_READ_HI           0x70010     /* RE Bus Readback High 32 bits */
#define HQ3_REBUS_READ_LO           0x70014     /* RE Bus Readback Low 32 bits */
#define HQ3_CFIFO_USER              0x70080     /* User CFIFO port (64-bit; word at +4) */
#define HQ3_CFIFO_GL                0x74080     /* libGLcore immediate-mode command words */
#define HQ3_GIO_STATUS              0x70100     /* GIO Error & Arbitration Status (BFIFO depth) */
#define HQ3_BUSY_DMA                0x70104     /* DMA Busy Status Register */

/* Direct RSS Register Space */
#define HQ3_RSS_BASE                0x7C000     /* Direct RSS register access (offset 0..0x3FF) */
#define HQ3_RSS_EXEC_ALIAS          0x7D000     /* Direct RSS register access + EXECUTE IR */
#define HQ3_CFIFO_FAST_WINDOW       0x80000     /* Fast Command FIFO window (512 KB) */

/*
 * ============================================================================
 * REGISTER BIT DEFINITIONS & MASKS
 * ============================================================================
 */

/* HQ3 Master Configuration (HQ3_CONFIG: 0x50000) */
#define HQ3_CONFIG_REV_MASK         0x000000F0  /* ASIC Revision (rev 1 = 0x10) */
#define HQ3_CONFIG_REV_SHIFT        4
#define HQ3_CONFIG_ID_MASK          0xFFFFFF00  /* ASIC ID (0x48513300 = "HQ3\0") */
#define HQ3_CONFIG_ID_VAL           0x48513300
#define HQ3_CONFIG_BIGENDIAN        BIT(2)      /* 1 = Host is Big Endian */
#define HQ3_CONFIG_UCODE_STALL      BIT(3)      /* 1 = Stall microcode execution */
#define HQ3_CONFIG_GEDIAG_MASK      0x00000030
#define HQ3_CONFIG_DIAG_READ_GE0    (2 << 4)    /* Route diagnostics to GE0 */
#define HQ3_CONFIG_DIAG_READ_GE1    (3 << 4)    /* Route diagnostics to GE1 */
#define HQ3_CONFIG_DIAG_GIO_CP      BIT(7)      /* Loopback diagnostic path */
#define HQ3_CONFIG_DEFAULT          0x0000008E  /* Default boot configuration */

/* GIO Configuration Register (HQ3_GIO_CONFIG: 0x50114) */
#define HQ3_GIOCFG_GIO_BIGENDIAN    BIT(0)      /* GIO bus big endian */
#define HQ3_GIOCFG_DCB_BIGENDIAN    BIT(1)      /* DCB bus big endian */
#define HQ3_GIOCFG_EXT_PIPEREG      BIT(2)      /* External pipeline register enable */
#define HQ3_GIOCFG_TLB_UPDATE       BIT(3)      /* Enable HQ3 hardware TLB updates */
#define HQ3_GIOCFG_HQBUS_BYPASS     BIT(6)      /* Bypass HQ bus */
#define HQ3_GIOCFG_HQFIFO_HW        BIT(12)     /* High watermark flag */
#define HQ3_GIOCFG_MST_W_BC_DAT     (0x3 << 16)
#define HQ3_GIOCFG_MST_GNT_AS       (0x3 << 19)
#define HQ3_GIOCFG_MST_R_DAT_AS     (0x1 << 22)
#define HQ3_GIOCFG_MST_W_DAT_AS     (0x3 << 25)
#define HQ3_GIOCFG_MST_FREE_REQ     (0x7 << 28)
#define HQ3_GIOCFG_DEFAULT          0x0000000F

/* Default FIFO Watermarks & Delays */
#define HQ3_CFIFO_HIWATER_DEF       40          /* Stall host at 40 words remaining */
#define HQ3_CFIFO_LOWATER_DEF       20          /* Resume host at 20 words remaining */
#define HQ3_CFIFO_DELAY_DEF         100         /* Clock delay cycles between checks */

#define HQ3_DFIFO_HIWATER_DEF       40
#define HQ3_DFIFO_LOWATER_DEF       20
#define HQ3_DFIFO_DELAY_DEF         100

#define HQ3_BFIFO_MAX               16
#define HQ3_BFIFO_HIWATER_DEF       8
#define HQ3_BFIFO_LOWATER_DEF       2
#define HQ3_BFIFO_DELAY_DEF         100

/* Status Register (HQ3_STATUS: 0x70000) */
#define HQ3_STATUS_CFIFO_EMPTY      BIT(0)      /* CFIFO is completely empty */
#define HQ3_STATUS_CFIFO_FULL       BIT(1)      /* CFIFO is completely full */
#define HQ3_STATUS_VBLANK           BIT(2)      /* Vertical blanking active */
#define HQ3_STATUS_CFIFO_HALF       BIT(3)      /* CFIFO is half full */
#define HQ3_STATUS_DFIFO_EMPTY      BIT(4)      /* DFIFO is empty */
#define HQ3_STATUS_DFIFO_FULL       BIT(5)      /* DFIFO is full */
#define HQ3_STATUS_DFIFO_HALF       BIT(6)      /* DFIFO is half full */
#define HQ3_STATUS_GE_BUSY          BIT(8)      /* GE11 is actively processing */
#define HQ3_STATUS_RE_BUSY          BIT(9)      /* RE4 is actively rasterizing */
#define HQ3_STATUS_HOST_IDLE        BIT(10)     /* Host interface idle */
#define HQ3_STATUS_DMA_BUSY         BIT(12)     /* DMA controller actively transferring */
#define HQ3_STATUS_INTR_PEND        BIT(15)     /* Interrupt asserted to host */
#define HQ3_STATUS_IDLE_MASK        (HQ3_STATUS_CFIFO_EMPTY | HQ3_STATUS_CFIFO_FULL | \
                                     HQ3_STATUS_DFIFO_EMPTY | HQ3_STATUS_DFIFO_HALF)

/* FIFO Status Register (HQ3_FIFO_STATUS: 0x70004) */
#define HQ3_CFIFODEPTH(stat)        ((stat) & 0x7F)             /* CFIFO used word count */
#define HQ3_CFIFOVALID(stat)        (((stat) & 0x7F80) >> 7)     /* Valid entries */
#define HQ3_DFIFODEPTH(stat)        (((stat) & 0x3F8000) >> 15)  /* DFIFO used word count */
#define HQ3_CFIFOHWM(stat)          (((stat) & 0x1FC00000) >> 22)/* CFIFO high watermark */

/* GIO Status Register (HQ3_GIO_STATUS: 0x70100) */
#define HQ3_BFIFODEPTH(stat)        ((stat) & 0x1F)             /* BFIFO used word count */
#define HQ3_STATUS_DCBBUSY          BIT(5)                      /* DCB bus cycle in progress */

/* DMA Busy Register (HQ3_BUSY_DMA: 0x70104) */
#define HQ3_DMA_BUSY_SYNC           BIT(0)
#define HQ3_DMA_BUSY_HIF            BIT(1)
#define HQ3_DMA_BUSY_CFIFO          BIT(2)
#define HQ3_DMA_BUSY_CD             BIT(3)
#define HQ3_DMA_BUSY_REIF           BIT(4)
#define HQ3_DMA_BUSY_MASTER_DMA     BIT(9)
#define HQ3_DMA_BUSY_REIF_DMA_ACTV  BIT(11)
#define HQ3_DMA_BUSY_HAG_DMA        BIT(12)

/*
 * How the IRIX 6.5 drivers use these (decompiled mgras.o, libGLcore):
 *   - GIO interrupt vectors (MgrasSetGioVectors): 0 = MgrasFIFOInterrupt,
 *     1 = MgrasGeneralInterrupt, 2 = MgrasRetraceInterrupt.
 *   - HQ3_INTR_ENAB_SET bits above the flag causes drive vector 0:
 *     bit 26 CFIFO at/above high water, 27 CFIFO below low water, 30 DFIFO
 *     above high water, 31 DFIFO below low water. Boot value 0x4400E200.
 *   - STATUS bits as the drivers test them (the BIT() names above are
 *     older guesses): bit 0 HQ3 has run every command (libGLcore waits
 *     for it before a read DMA), bit 1 whole pipe idle (glFinish spins on
 *     it), bit 3 CFIFO above high water, bit 4 CFIFO below low water,
 *     bit 7 / bit 8 the same for the DFIFO.
 *   - MgrasFIFOInterrupt on high water: polls STATUS bit 3 for up to 10 ms
 *     (50 us for some processes); still high and FIFOSTATUS depth != 0x7F
 *     (0x7F = wedged) -> blocks the writer, swaps enable 26 for 27, arms a
 *     watchdog timeout. On low water (STATUS bit 4) it swaps back and
 *     wakes the writer.
 *   - MgrasFlushHQ3 (buffer swaps, clip/bank validation): polls the CFIFO
 *     depth against a threshold (HWM - k, or 0) or DMABUSY & 0x17 for
 *     ~1-10 ms, then sets CFIFO_LW = threshold + 1, enables bit 27 and
 *     sleeps until the low-water interrupt.
 *   - 6.5.22 programs CFIFO HW/LW = 0x20/0x14, DFIFO HW/LW = 0x28/0x14.
 */

/* Flags & Interrupt Mask Bits (HQ3_FLAG_*, HQ3_INTR_*) */
#define HQ3_FLAG_RETRACE            BIT(0)      /* Vertical retrace event */
#define HQ3_FLAG_CFIFO_LOWATER      BIT(1)      /* CFIFO below low watermark */
#define HQ3_FLAG_CFIFO_HIWATER      BIT(2)      /* CFIFO crossed high watermark */
#define HQ3_FLAG_DFIFO_LOWATER      BIT(3)      /* DFIFO below low watermark */
#define HQ3_FLAG_DMA_DONE           BIT(4)      /* DMA transfer completed */
#define HQ3_FLAG_CONTEXT_LOADED     BIT(6)      /* Context loaded (phase 2 complete) */
#define HQ3_FLAG_TLB_MISS           BIT(7)      /* Graphics TLB miss on virtual DMA */
#define HQ3_FLAG_ERROR              BIT(8)      /* Bus or protocol error */
#define HQ3_FLAG_CP0                BIT(10)     /* CP flag 0: buffer swap occurred */
#define HQ3_FLAG_DONE               BIT(16)     /* Set by 0xE04 command (sync completion) */
#define HQ3_FLAG_GE_DATA            BIT(17)     /* GE has data waiting for readback */
#define HQ3_FLAG_GE_DIAG            BIT(18)     /* GE diagnostic data waiting */
#define HQ3_FLAG_CONTEXT_SAVED      BIT(19)     /* Context saved (phase 1 complete) */
#define HQ3_FLAG_ALL_CAUSES         0x007FFFFF

/* Graphics TLB (GTLB) Constants */
#define HQ3_TLB_NUM_ENTRIES         32          /* 32 fully-associative pages */
#define HQ3_TLB_PAGE_SIZE           4096        /* 4 KB page size */
#define HQ3_TLB_PAGE_SHIFT          12
#define HQ3_TLB_TAG_MASK            0x0007FFFF  /* Virtual page tag mask */
#define HQ3_TLB_PHYS_MASK           0x003FFFFF  /* Physical page frame mask */
#define HQ3_TLB_VALID_BIT           BIT(31)

/*
 * ============================================================================
 * CONTEXT MANAGEMENT (IRIX 6.5 kernel: mgras.o MgrasPcxSwap / MgrasCtxsw /
 * MgrasGlContextAlloc, rrm.o, gfx.o; checked against live traces)
 * ============================================================================
 *
 * Who switches, and when
 *   Every client of the pipe (the X server, each OpenGL / IRIS GL context,
 *   and the kernel's own context) has a hardware context. IRIX's generic
 *   graphics layer (RRM) owns the policy; the board driver only does the
 *   switch. Switching is lazy and fault-driven, and preemptive at process
 *   granularity:
 *   - The FIFO page is mapped only into the process that owns the pipe. When
 *     the owner is descheduled (rrmSuspend) it gives up pipe ownership; when
 *     a process resumes and someone else has taken the pipe, its mapping is
 *     removed (rrmResume -> rrmDoUnMapGfx).
 *   - A process that touches the FIFO without the mapping faults
 *     (gfx_fault -> RRM_ValidFault): it waits for the pipe semaphore, waits
 *     while the FIFO is backed up, revalidates its clip / buffers / retrace
 *     state, and if the board's loaded context is not its own calls the
 *     driver's PcxSwap (MgrasPcxSwap -> MgrasCtxsw), then gets the page
 *     mapped back.
 *   - gfx_disp (scheduler hook) is flow control, not switching: a thread
 *     whose pipe is backed up (the FIFO high-water interrupt marked it) is
 *     not dispatched until the FIFO drains.
 *   So nothing in the stream asks for a switch, and a client can lose the
 *   pipe between any two of its FIFO stores.
 *
 * Granularity: a switch replaces the whole context, never part of it.
 *   Before asking for one the kernel waits for the HQ to go idle (HQ3
 *   busy-DMA bits SYNC / HIF / CFIFO clear, or HQ3_FLAG_DONE; an active
 *   master DMA is waited out on its interrupt). Everything already in the
 *   FIFO executes; nothing queued is saved. What can be in flight is a
 *   half-received command (the owner was descheduled between the words of
 *   one command): the host-interface state (HQ3_HIF_CTX) is part of the
 *   saved context, so the command finishes when its owner gets the pipe
 *   back. A model must keep the FIFO parser per context.
 *
 * The switch (MgrasCtxsw, outgoing -> incoming)
 *   1. Save into the outgoing context's kernel record: HQ3_CONFIG,
 *      HQ3_GIO_CONFIG, the interrupt enables and the flag enables.
 *   2. Wait for the HQ as above.
 *   3. Enable the CONTEXT_SAVED interrupt and write the save routine's
 *      microcode address (0x4FC in 6.5.22) to HQ3_CTXSW. The microcode saves
 *      the outgoing context on the board (see Where the state lives) and
 *      raises HQ3_FLAG_CONTEXT_SAVED. The kernel sleeps on it; after 15 s
 *      (mgras_ctxswtimeout) the switch fails ("MgrasCtxsw failed"), the
 *      caller reports a graphics error and the client is killed.
 *   4. Enable CONTEXT_LOADED, restore HQ3_GIO_CONFIG, and write the incoming
 *      context's 63-word image through the user FIFO port (0x70084) by PIO,
 *      polling the FIFO level, no DMA. The microcode consumes the image as
 *      the load phase (it is not a command stream), restores the incoming
 *      context and raises HQ3_FLAG_CONTEXT_LOADED.
 *   5. Clear image word 1's one-shot bits (below), keep HQ3_FLAG bit 5 of
 *      the outgoing context in its record, clear flags 7:4, restore the
 *      incoming HQ3_CONFIG, interrupt enables (the top six interrupt bits
 *      stay the board's) and flag enables.
 *   6. Switching to the kernel's own context, the kernel sends
 *      CP_GE_NOSAVE_NEXT_SWITCH (0xF4): the kernel changes no GE state, so
 *      the next switch away need not save it.
 *
 * Where the state lives
 *   - GE / HQ state (the GL attribute state of a context): on the board, in
 *     the GE11s' external RAM (ERAM, 64K words per GE11, managed by a buddy
 *     allocator in the driver). The low part holds fixed-size context slots
 *     (MgrasGlContextAlloc / Free, a bitmap of slots); the rest holds
 *     resources the clients load (display list / evaluator / texture data,
 *     MgrasEramRead / Write / ValidateEramRes), with a movable boundary
 *     between them (MgrasMoveDemarcationUp / Down). Image word 0 is the
 *     context's slot address: traced 6.5.22 slots are 0x17C7 words apart from
 *     0xA9B (0xA9B the kernel's context, then 0x2262, 0x3A29, 0x51F0,
 *     0x69B7, ...), so about ten contexts are resident at once. The save
 *     routine writes the outgoing GE state to its slot; the load restores
 *     the incoming one's.
 *   - Host memory only on eviction: when no slot is free the driver picks a
 *     resident context, copies its slot to a host buffer (MgrasEramRead) and
 *     frees the slot; a context coming back with saved state gets a slot
 *     (possibly a different one) and its state is copied in again
 *     (MgrasEramWrite). Both run in the kernel's context: ERAM is reached
 *     with the GE pass-through tokens (CP_PASS_THROUGH_GE_WRAM_DELAYED and
 *     friends, an ERAM address and a word count), the data moving by HQ DMA
 *     through the kernel's DMA page table, once per GE11. Unlike GR2 / XZ,
 *     an ordinary switch moves no GE state over the bus.
 *     Two GE11s (Maximum Impact, traced 6.5.22): no token selects the GE;
 *     the transfers simply come in GE order. Save: CP_PASS_THROUGH_GE_
 *     WRAM_DELAYED (S, 0x17C8), then (S - 1, 0x17C8), each feeding a
 *     0x17C7-word DMA into consecutive halves of one host buffer (so GE1's
 *     read output runs one word late). Restore: half 0 to S in 900-word
 *     chunks (WRAM / FIFO_3), WRAM_4 (end), half 1 to S, WRAM_4.
 *   - Raster (RSS) state: not saved by the hardware as such. The GE
 *     regenerates a GL context's raster registers from its GL state. The X
 *     server drives the raster registers directly (0x1000 commands) and
 *     keeps a copy of what it has set in a shadow region of its own memory,
 *     registered with the driver (MgrasSetShadowRgn, 0x9F8 bytes). When the
 *     pipe leaves the X server, the driver flushes the HQ and copies the X
 *     server's window state out of that shadow into the X server's image
 *     (MgrasThrowManagerRssShadow), so the load phase puts it back when X
 *     gets the pipe again. The X server still has an ERAM slot like any
 *     context (0x2262 in the traces above); its image arrives with word 1
 *     bit 29 set on every load, the window state freshly taken from the
 *     shadow (origin 0x2FF0000, mode 0 on a 768-line screen).
 *   - Window state of a GL context (origin, screen masks, DRB pointers):
 *     image words 2-15, written by MgrasValidateClip into the image when the
 *     context is not loaded (see HQ_TOK_KERN_WINDOW for the layout), or sent
 *     as KERN_WINDOW when it is.
 *
 * The 63-word image (kernel record + 0x34), as far as known
 *   word 0       ERAM slot of the context's GE state (-1 in the kernel
 *                record while not resident)
 *   word 1       flags (traced: X server 0x20000000, a GL context loaded
 *                after X 0x0C000000 or 0x2C000000): bit 31 first load (the
 *                context starts from GL's
 *                initial state; slots are recycled), bit 30 nothing saved
 *                yet (clear: the state is in host memory and is copied back
 *                into ERAM before the switch), bit 29 window state in words
 *                2-15 changed, bit 27 words 18-39 carry the X server's
 *                raster shadow (latched by the driver into a board-global
 *                copy before the switch), bit 26 unknown. The kernel clears
 *                bits 31, 30, 29 and 26 after a load (not for its own
 *                context), and bit 27 when it latches.
 *   words 2-15   window state (HQ_TOK_KERN_WINDOW); masks that word 3
 *                (window mode) leaves disabled can hold junk (traced:
 *                mask 2 = 0x3955EFA8 / 0x20D37000 with mode 0x11)
 *   words 16-17  unknown (1, 1 in a GL context after its window changed)
 *   words 18-39  the X server's screen-wide raster registers, taken from
 *                its shadow (the shadow is indexed by RSS register number
 *                times 4), in this order: 0x046, 0x047, 0x153, 0x110
 *                FILLMODE, 0x140, 0x141, 0x112 CONFIG, 0x161 PP1FILLMODE,
 *                0x162, 0x163, 0x164, 0x176, 0x177, 0x178, 0x160, 0x16F,
 *                0x068, 0x069, 0x113, 0x114, 0x157, 0x156. Traced on
 *                6.5.22 at 1024x768: 0x4601BC 0x5601CB 0x100011 0x100000
 *                0x7B0810 0x5C0 0xCAC 0xC006204 0 0xFFFFFF 0xFFFFFF 0 0 0 0
 *                0 0 0 0x3FF 0x2FF 0xFFFFFF 0xFFFFFF. Present in every GL
 *                context's image (word 1 bit 26 set) and the kernel's, zero
 *                in the X server's own: a context loads with X's
 *                screen-wide setup (pixel format, CONFIG, plane masks,
 *                screen limits), presumably written to the RSS by the load
 *                microcode when bit 26 is set.
 *   words 40-62  zero in all traced images
 *
 * Raster state across a switch, then: the load writes the incoming
 * context's window registers (and, for GL, X's screen-wide registers);
 * a GL context's other raster state is regenerated by the GE from its
 * saved GL state; the X server sets its mode registers itself with each
 * operation (traced: right after its load it writes ir_alias, fillmode,
 * pp1fillmode and the colour masks before drawing). Nothing reads raster
 * registers back to save them. Traced word 1 values: X 0x20000000, GL
 * 0x04000000 (0x0C000000 / 0x2C000000 with new X state or a new window),
 * the kernel's context 0x90000000 (first load every time, bit 28 unknown;
 * the kernel does not clear bits on its own image).
 *
 * Kernel and X versus GL: each has its own hardware context, so they never
 * share loaded state; the kernel does its own pipe work (ERAM transfers,
 * swaps) after switching to its context. Within one context nothing needs
 * saving: a GL context does not use the X server's raster registers.
 */
/*
 * ============================================================================
 * HOST ADDRESS GENERATOR (HAG) DMA REGISTERS & DESCRIPTORS
 * ============================================================================
 *
 * Mapped in HAG_CTX (0x50300) with command base 0x800:
 */
#define HAG_CTX_CMD_BASE            0x800

/*
 * Display lists (traced, IRIX 6.5.22 libGLcore mgras_dldma.c; glprim's
 * dlist scene):
 *   Storage: host memory only. libGLcore compiles a list into a pool of
 *   pinned pages it registers with the kernel, which writes DL_TABLE_BASE
 *   (low two bits flags, the kernel writes ...d; one 32-bit page frame
 *   number per 4 KB page, as for the image tables) and MAX_DL_PTR_OFFSET.
 *   GE11 ERAM is not used for lists (libGLcore puts imaging state there:
 *   histogram, colour tables).
 *   Calling: glCallList is a write of DL_JUMP_NO_WAIT (0x80C) from the user
 *   FIFO, two words: the first segment's byte offset in the list space and
 *   its length in words. libGLcore falls back to replaying a list in
 *   immediate mode (its software interpreter) when it cannot be DMAed:
 *   when the call depth would reach 64 (libGLcore's check), when the list was
 *   compiled for the other colour mode, or has unresolved calls.
 *   Segments: a list is one or more segments; each starts with its link
 *   and the body (ordinary FIFO commands) follows. The engine runs the
 *   body, then the link:
 *     DL_RETURN (0x83904, 1 word: a flag)     end: back to the last pushed
 *                                             segment, or to the FIFO
 *     DL_JUMP_NO_WAIT / _WAIT (0x80C08 /      continue at another segment
 *       0x80E08, offset and length)           (long lists, about 1 K words
 *                                             a segment, 4 KB apart)
 *     DL_PUSH (0x837, offset and length)      call: push the segment to
 *       then DL_JUMP to the called list       return to, then branch
 *   Padding words are 0xE0100 (DMA sync, no data). Example (glprim dlist):
 *   list 1 at 0x0 = [RETURN] quad; list 2 = 0x1000 [PUSH 0x40, JUMP list
 *   1] push matrix, translate, scale, red; 0x40 [PUSH 0x80, JUMP list 1]
 *   translate, blue; 0x80 [PUSH 0x2000, JUMP list 1] translate, white;
 *   0x2000 [RETURN] pop matrix. ideas keeps each material (four MATERIAL
 *   tokens) in a one-segment list, [RETURN] + 28 words, 0x80 bytes apart.
 */

/*
 * Page-list DMA (traced, libGLcore's texture restore, mgrTM_SR_TexLoad):
 * IM_PG_LIST1..4 hold eight 16-bit page indices into the pool's table,
 * bit 15 marking the last one used. The transfer is one run: from
 * IM_ROW_OFFSET within the first listed page, IM_FIRST_SP_WIDTH bytes
 * there, whole 4 KB pages in between, IM_LAST_SP_WIDTH bytes of the last
 * (both the run's length when only one page is listed); IM_LINE_CNT = 1,
 * start word 0xA3 (pool 1). Ordinary transfers leave the list at
 * 0x80000000 (index 0, last) and walk the pool's pages in order by row
 * start, stride and line count.
 */
#define HAG_REG_IM_PG_LIST1         0x00        /* Image page list 1 */
#define HAG_REG_IM_PG_LIST2         0x01        /* Image page list 2 */
#define HAG_REG_IM_PG_LIST3         0x02        /* Image page list 3 */
#define HAG_REG_IM_PG_LIST4         0x03        /* Image page list 4 */
#define HAG_REG_IM_PG_WIDTH         0x04        /* Image page width (in pixels) */
#define HAG_REG_IM_ROW_OFFSET       0x05        /* Row byte offset in system RAM */
#define HAG_REG_IM_ROW_START_ADDR   0x06        /* Row physical start address */
#define HAG_REG_IM_LINE_CNT         0x07        /* Number of lines to transfer */
#define HAG_REG_IM_FIRST_SP_WIDTH   0x08        /* First span pixel width */
#define HAG_REG_IM_LAST_SP_WIDTH    0x09        /* Last span pixel width */
#define HAG_REG_IM_Y_DEC_FACTOR     0x0A        /* Y decimation factor */
#define HAG_REG_IM_DMA_CNTL_STAT    0x0B        /* DMA control and status */
#define HAG_REG_DL_JUMP_NO_WAIT     0x0C        /* Display list branch (immediate) */
#define HAG_REG_DL_JUMP_WAIT        0x0E        /* Display list branch (wait for pipe) */
#define HAG_REG_STACK_RAM_WD0_SAVE  0x10
#define HAG_REG_STACK_RAM_WD1_SAVE  0x18
#define HAG_REG_IM_TABLE_BASE_1     0x20        /* Image table 1 base physical address */
#define HAG_REG_IM_TABLE_BASE_2     0x22        /* Image table 2 base physical address */
#define HAG_REG_IM_TABLE_BASE_3     0x24        /* Image table 3 base physical address */
#define HAG_REG_GL_TABLE_BASE       0x26        /* GL table base physical address */
#define HAG_REG_DL_TABLE_BASE       0x28        /* Display list table base physical address */
#define HAG_REG_DMA_PG_SIZE         0x2A        /* DMA page granularity */
#define HAG_REG_DMA_PG_RANGE_IM1    0x2B        /* Image 1 page count */
#define HAG_REG_DMA_PG_RANGE_IM2    0x2C        /* Image 2 page count */
#define HAG_REG_DMA_PG_RANGE_IM3    0x2D        /* Image 3 page count */
#define HAG_REG_DMA_PG_RANGE_GL     0x2E        /* GL table page count */
#define HAG_REG_DMA_PG_RANGE_DL     0x2F        /* Display list page count */
/* Seen on IP28 6.5.22: a ~400 KB glReadPixels through image pool 1 with
 * DMA_PG_RANGE_IM1 = 2 and MAX_IM1_PTR_OFFSET = 4, table entries 0..3
 * filled and 4 on zero, LINE_CNT 256 lines of 1542 bytes, ROW_START
 * advancing by 0x608 between transfers (0x500, 0xB08, 0x110). So the
 * kernel maps a window of pages; past it the HAG must take a graphics TLB
 * miss (flag bit 7) for the kernel to refill. Not yet traced on a board. */
#define HAG_REG_MAX_IM1_PTR_OFFSET  0x30        /* Max pointer offset table 1 */
#define HAG_REG_MAX_IM2_PTR_OFFSET  0x31        /* Max pointer offset table 2 */
#define HAG_REG_MAX_IM3_PTR_OFFSET  0x32        /* Max pointer offset table 3 */
#define HAG_REG_MAX_GL_PTR_OFFSET   0x33        /* Max pointer offset GL table */
#define HAG_REG_MAX_DL_PTR_OFFSET   0x34        /* Max pointer offset DL table */
#define HAG_REG_STATE_FLAGS         0x35
#define HAG_REG_STATE_REMAINDER     0x36
#define HAG_REG_DL_PUSH             0x37        /* Display list call (push PC) */
#define HAG_REG_DL_RETURN           0x39        /* Display list return (pop PC) */
#define HAG_REG_RESTORE_CFIFO       0x40
#define HAG_REG_GIO_RESTORE_ADDRS   0x41

/*
 * ============================================================================
 * FORMATTER / CONVERTER (FC) MODE REGISTER
 * ============================================================================
 */
#define HQ_FC_ADDR_PIXEL_FORMAT_MODE 0xC00

#define HQFORM_COMPONENT_SIZE_MASK  0x03
#define HQFORM_NIBBLE               0
#define HQFORM_BYTE                 1
#define HQFORM_SHORT                2
#define HQFORM_WORD                 3

#define HQFORM_COMP_PER_PIXEL_MASK  (0x03 << 2)
#define HQFORM_1_COMP_PER_PIXEL     (0 << 2)
#define HQFORM_2_COMP_PER_PIXEL     (1 << 2)
#define HQFORM_3_COMP_PER_PIXEL     (2 << 2)
#define HQFORM_4_COMP_PER_PIXEL     (3 << 2)

#define HQFORM_SRC_DST_ENDIAN_MASK  (0x03 << 4)
#define HQFORM_BE_TO_BE             (0 << 4)
#define HQFORM_LE_TO_BE             (1 << 4)
#define HQFORM_BE_TO_LE             (2 << 4)

#define HQFORM_SWAP_BYTE_MASK       BIT(6)
#define HQFORM_SWIZZLE_MASK         BIT(7)

#define HQFORM_NIBBLE_EXPANSION_MASK (0x03 << 8)
#define HQFORM_NIBBLE_EXP_ZERO      (0 << 8)
#define HQFORM_NIBBLE_EXP_REPL      (1 << 8)
#define HQFORM_NIBBLE_EXP_NONE      (2 << 8)

#define HQFORM_ZERO_ALIGN_MASK      BIT(10)
#define HQFORM_INVALIDATE_TRAILING  BIT(11)
#define HQFORM_NIBBLE_SWAP_MASK     BIT(12)

#define HQFORM_CANONICAL            (HQFORM_BE_TO_BE | HQFORM_NIBBLE_EXP_NONE | \
                                     HQFORM_ZERO_ALIGN_MASK | HQFORM_INVALIDATE_TRAILING)

/*
 * Traced (IRIX 6.5.22 libGLcore):
 *   - The formatter mode stays at canonical (0xE00) except around a pixel
 *     DMA that needs reordering. GL_ABGR_EXT texture images load with
 *     0xE8D (byte components, 4 per pixel, swizzle) or 0xE8E (16-bit
 *     components), written just before SEND_PIXELS and put back to 0xE00
 *     after it.
 *   - Swizzle reverses the order of the components within each pixel;
 *     each component keeps its own byte order. So ABGR in host memory
 *     reaches the board as RGBA.
 *   - The command stream is otherwise identical to an RGBA load: the GE
 *     never sees the component order.
 */

/*
 * Pixel transfer host formats seen (raster transfer mode XFRMODE: format
 * in bits 7:4, component type in 3:0):
 *   (8, 0)  RGBA, 1 byte a component       (8, 1)  RGBA, 16-bit components
 *   (7, 0)  RGB, 1 byte (glCopyTexImage's screen read)
 *   (7, 1)  RGB, 16-bit (glCopyPixels' read)
 *   (2, 3)  depth, 32 bits full scale (glReadPixels / glCopyPixels)
 *   (9, x)  one component, (0xA, x) two (texture readback)
 *
 * glCopyPixels goes through host memory:
 *   1. The image is read in strips (GET_PIXELS, RGB16 or depth), each a
 *      host DMA.
 *   2. It is drawn back with SEND_PIXELS (routine 0x49D0) in chunks about
 *      80 pixels wide. The GE pixel pipeline widens RGB16 to the transfer
 *      mode's RGBA16. Host rows are padded to whole words: an RGB16 strip
 *      of 93 pixels is 558 bytes sent as 140 words (SEND_PIXELS words 0 +
 *      1). The 0xDA8 width gives the pixel count; the row's word count
 *      does not divide by it.
 * For a pixel draw, ERAM block 0xDA8 is the chunk's place in the image
 * [x0, y0, x1, y1, transfer size (rows << 16 | columns), 1], offset from
 * the raster position. (x0, y0) is the first pixel sent and (x1, y1) the
 * last, so the order gives the direction. Upright images run y0 = 0 up to
 * y1 = rows - 1. With glPixelZoom(1, -1) (Mirai 1.0's splash, traced) the
 * block is [0, -1, w - 1, -rows]: the first row lies just below the raster
 * position and the image runs down. The SEND_PIXELS token itself looks
 * the same either way; only this block shows the flip. A negative x zoom
 * presumably gives x1 < x0 the same way (not traced).
 * libGLcore also sends the raster engine's zoom registers through
 * RSS_REG_SHADOW: XFRFACTOR 0x155 / XFRINITFACTOR 0x154, seen as
 * 0x101423AC / 0 (Mirai, zoom (1, -1)) and 0x3BFC00 / 0x1010090 (Avid
 * Matador, zoom 1). Not decoded.
 *
 * Applications that keep their two buffers in step by hand (Mirai 1.0):
 * after each swap they read the region just changed from the front buffer
 * (READ_BUFFER GL_FRONT, GET_PIXELS) and draw it into the back
 * (SEND_PIXELS). They check glIsEnabled(GL_SCISSOR_TEST), which libGLcore
 * asks the GE (GE address 0xB9C, see Readbacks), and turn scissoring off
 * for the copy only if it is on. A wrong answer leaves the copy clipped,
 * and the back buffer goes stale wherever the copy did not reach.
 */

/*
 * ============================================================================
 * PIXEL OPERATIONS (mapped with test/gltest/pixtest.c, 6.5.22, 2026-10)
 * ============================================================================
 * The GE program and its modules: GE11.h "IMAGING ENGINE". Raster
 * transfer formats: RE4.h XFRMODE.
 *
 * Pixel data commands (bit 31 set): byte count in 19:0, initial alignment
 *   in 24:22. glDrawPixels with UNPACK_ALIGNMENT 1 or 2 sends rows that do
 *   not start on a word: the command then carries ceil((count + align) /
 *   4) words, rounded up to an even number (doublewords), and the first
 *   `align` bytes are filler. Getting this wrong desynchronises the whole
 *   FIFO (the next command word is read from pixel data).
 *
 * State blocks whose data comes later: 0x080 (GE state) and 0x0CD
 *   (INIT_PIXEL_STATE_ERAM) with a header but no data words, and 0x089
 *   INIT_COLOR_TABLE (glPixelMap tables), take their values from the next
 *   pixel data command. The block is [?, ?, bytes, address, 0, 0, n,
 *   values, n, values ...]: a count, then that many words, repeated.
 *
 * Readbacks are per context: the GE_DATA flag (bit 17 of the flags the
 *   host polls) and the answer word belong to the context that asked. Two
 *   GL clients interleaved by context switches must not see each other's
 *   flag; a global flag hangs the second client.
 *
 * glReadPixels: SAVE_RSS, the raster register lists, then GET_PIXELS
 *   (0x0DB) with
 *     0x4009  DMA read: the host arms a board-to-host DMA for the block.
 *     0x400A  PIO read (fill-mode block kind 2): the host reads the
 *             pixels itself from the raster engine's character registers
 *             (RE4.h), a doubleword at a time, high word first. libGLcore
 *             reads one row per GET_PIXELS this way for small packed-type
 *             reads (GetPixelsPackedPIO) and packs them on the CPU.
 *   PIO reads of RGB(A) come in the reverse of the DMA order: an RGBA
 *   read (RGBorder set) arrives alpha first, an ABGR read (RGBorder
 *   clear) red first, unless XFRMODE bit 18 is set (RE4.h; the X
 *   server's XGetImage sets it and wants the DMA order). libGLcore's packer (__glMgr_PackPixels) expects
 *   this for 4_4_4_4 and 8_8_8_8. Its 5_5_5_1, 10_10_10_2 (read as 16-bit
 *   components) and 3_3_2 packers put the last wire component in the top
 *   bits whatever the order, so those reads come out in a reversed layout
 *   on the real board too.
 *   SAVE_RSS word 3 selects the planes: bits 24:22 for reads (word 4 = 2),
 *   bits 20:18 for draws (word 4 = 3); value 6 = stencil, 7 = depth.
 *
 * The HQ formatter (0xC00 convert control): with byte-sized components
 *   the swap bit exchanges the bytes of each 16-bit pair (UNPACK /
 *   PACK_SWAP_BYTES on 16-bit data); for 32-bit components it reverses
 *   each word.
 *
 * Large glDrawPixels images come by host DMA in column chunks: 156
 *   RGBA or 144 RGB pixels wide (traced with pixtest zoombig, matching
 *   atomchild411's traces in PR #221). Before each chunk 0xDA8 gives its
 *   rectangle relative to the raster position, already zoomed (60 rows at
 *   y zoom 3.2: 192 high; 156 columns at x zoom 1.2: 187 wide), and the
 *   DMA (LINE_BYTES 0x804 = the image's row bytes) brings the chunk's
 *   rows unzoomed. Under a fractional x zoom XFRSIZE's width is the
 *   GE-replicated count (ceil(zx) times the pixels: 0x138 for 156 at 1.2).
 *   Tiny textures come by DMA too, each row padded to whole words (a 2 x 2
 *   GL_RGB texture: LINE_BYTES 6, 8 bytes a row arrive).
 *
 * Texture loads (SEND_PIXELS routine 0x511A) go through the GE program
 *   like a draw and end in TRAM; glGetTexImage is READ_TEXTURE (0x0F0:
 *   width, height, 0.5, 0.5, xfrmode, 0x400D). Transfer formats 9 (one
 *   component) and 10 (two) are the texel formats.
 *
 * Where libGLcore itself departs from GL 1.1 (the board does as told;
 * pixtest reports these as XFAIL):
 *   - glTexEnv GL_REPLACE is sent as MODULATE (__glMgrim_TexEnviv).
 *   - glReadPixels applies no pixel transfer (scale, bias, maps).
 *   - glBitmap starts at UNPACK_SKIP_PIXELS / 8 bytes and drops the
 *     remaining skip_pixels % 8 bits.
 *   - glGetTexImage(GL_ALPHA) fails with GL_INVALID_OPERATION.
 *   - glTexSubImage2D / glCopyTexSubImage2D (mgrTM_TexSubImage2D_setup)
 *     fail with GL_INVALID_VALUE unless the sub-image is at least 32
 *     texels wide or the level's full width, and ends on a multiple of
 *     32 texels or at the level's right edge. Applications that update
 *     narrow pieces of a texture get nothing on IMPACT.
 *   - Packed 5_5_5_1, 10_10_10_2 and 3_3_2 reads come back in reversed
 *     layouts (above).
 * GENERAL_BITMAP format word: bit 17 = UNPACK_LSB_FIRST, bits 16:14 = row
 * padding.
 *
 * Colour-index visuals (pixtest --ci 8 / --ci 12, 2026-10-13): both GL
 *   CI visuals draw with PP1 fill mode 0xC006604 (pixel type 6); only the
 *   plane masks differ, ColorMaskLSBs 0xFF for 8 bits and 0xFFF for 12
 *   (INDEX_MASK 0x03D carries the same). Draws go out in XFRMODE format
 *   0 with the host type's component type (0 u8, 4 s8, 1 u16, 5 s16,
 *   3 / 7 32-bit); 16-bit rows are padded to words, so 0xDA8's transfer
 *   width, not the padded row, is the pixel count. UNSIGNED_BYTE indices
 *   reach the raster engine as bytes, so a shifted index past 255 wraps
 *   even on a 12-bit visual. Every glReadPixels(GL_COLOR_INDEX) reads
 *   32-bit indices (XFRMODE 0x...03) and libGLcore converts them on the
 *   CPU: plain truncation for the signed types (GL wants a mask to
 *   2^(n-1) - 1), and no read transfer. The board returns the 12-bit index
 *   field; on an 8-bit visual planes 11:8 keep whatever was there, and
 *   nothing masks them off (our model; unverified on hardware).
 *   glReadPixels(GL_RGBA) from a CI visual fails with
 *   GL_INVALID_OPERATION in libGLcore.
 */

/*
 * ============================================================================
 * COMMAND FIFO (CFIFO): PORTS, WORD FORMATS, COMMAND SPACE
 * ============================================================================
 *
 * Ports (MGRAS.h mgras_hw): the user FIFO at 0x70080 and the privileged FIFO
 * at 0x50080 are 64-bit ports. A doubleword store pushes two words, high
 * word first; a word store to +4 pushes one. libGLcore also stores words at
 * 0x74080 (cfifo_gl).
 *
 * The FIFO carries a stream of 32-bit words: a command word, then the data
 * words its byte count announces (rounded up to whole words). Bit 31 picks
 * the kind of command word.
 *
 * Control command (bit 31 = 0), as SGI's diagnostics build it
 * (BUILD_CONTROL_COMMAND):
 *   bits  7:0   byte count of the data that follows
 *   bits 20:8   command (see COMMAND SPACE)
 *   bit  21     sync
 *   bit  22     set by the kernel and the diagnostics; the X server leaves it 0
 *   bits 29:23  convert (formatter control)
 *
 * Pixel command (bit 31 = 1, BUILD_PIXEL_COMMAND): raw pixel data follows,
 * sent as doublewords.
 *   bits 19:0   byte count
 *   bits 21:20  destination address select
 *   bits 24:22  initial alignment (byte offset into the first doubleword)
 *   bit  25     packed
 *   bit  26     last
 */
#define HQ_CTL_BYTES(w)             ((w) & 0xFF)
#define HQ_CTL_CMD(w)               (((w) >> 8) & 0x1FFF)
#define HQ_CTL_SYNC                 BIT(21)
#define HQ_CTL_CONTROL              BIT(22)
#define HQ_CTL_CONVERT(w)           (((w) >> 23) & 0x7F)
#define HQ_CTL_WORD(cmd, bytes)     ((((cmd) & 0x1FFF) << 8) | ((bytes) & 0xFF))

#define HQ_PIX_CMD                  BIT(31)
#define HQ_PIX_BYTES(w)             ((w) & 0xFFFFF)
#define HQ_PIX_ADDR(w)              (((w) >> 20) & 0x3)
#define HQ_PIX_ALIGN(w)             (((w) >> 22) & 0x7)
#define HQ_PIX_PACKED               BIT(25)
#define HQ_PIX_LAST                 BIT(26)

/*
 * COMMAND SPACE (the 13-bit command field)
 *
 *   0x000-0x1FF  command-processor tokens: run by the HQ3 microcode (hq3.u),
 *                which hands most of them to the GE11s. The OpenGL driver's
 *                protocol (HQ_TOK_* below).
 *   0x800-0x9FF  host DMA engine (HAG) registers; data = the register value
 *                (HAG_REG_*; an 8-byte register takes two words, high first)
 *   0xA00-0xA0F  raster-interface (RE bus) registers
 *   0xC00        formatter / converter mode (HQ_FC_ADDR_PIXEL_FORMAT_MODE)
 *   0xE01        DMA sync (libGLcore __glMgrDLDMASync, __glMgrWaitForDMARead)
 *   0xE04        set the DONE flag (HQ3_FLAG_DONE) once everything before it
 *                has executed
 *   0x1000-0x1FFF  raster register write, bypassing the GE (HQ_CMD_RSS_*):
 *                bits 9:0 the RSS register, bit 10 (0x400) execute the IR
 *                after the write, bit 11 (0x800) set by every driver. Two
 *                data words write the register and the next one.
 */
#define HQ_CMD_RSS_BASE             0x1000
#define HQ_CMD_RSS_EXECUTE          0x0400
#define HQ_CMD_RSS_ALWAYS           0x0800
#define HQ_CMD_RSS_WRITE(reg)       (HQ_CMD_RSS_BASE | HQ_CMD_RSS_ALWAYS | ((reg) & 0x3FF))
#define HQ_CMD_RSS_EXEC(reg)        (HQ_CMD_RSS_WRITE(reg) | HQ_CMD_RSS_EXECUTE)

#define HQ_CMD_HAG_BASE             0x0800
#define HQ_CMD_RE_IF_BASE           0x0A00
#define HQ_CMD_DMA_SYNC             0x0E01
#define HQ_CMD_SET_FLAG_DONE        0x0E04

/*
 * Command-processor tokens of SGI's diagnostic and bring-up microcode
 * (cp_ge_sim_tokens.h in the IDE sources). The production kernel sends some
 * of them while initialising the GE11s and around context switches.
 */
#define CP_DUMP_FOREVER             0xF3
#define CP_GE_NOSAVE_NEXT_SWITCH    0xF4    /* kernel, after switching to its own context: skip the GE save at the next switch */
#define CP_GE_FTOF                  0xF5
#define CP_GE_DMA_HEADER_SIZE       0xF6
#define CP_DELAY                    0xF7
#define CP_SET_CP_FLAGS             0xF8
/*
 * GE11 ERAM access (traced, 6.5.22; kernel MgrasEramRead / MgrasEramWrite,
 * and the X server once at start-up):
 *   write: CP_PASS_THROUGH_GE_WRAM (0xFD) with the word count rounded up to
 *          even; the words, as FIFO pixel data (PIO) or by a host to board
 *          DMA started after it (no raster transfer armed); then
 *          CP_PASS_THROUGH_GE_FIFO_3 (0xFB) with the ERAM address and the
 *          real count; the kernel sends chunks of at most 900 words and
 *          ends with CP_PASS_THROUGH_GE_WRAM_4 (0xFA).
 *   read:  a board to host DMA armed first (start word bit 3, one line of
 *          count * 4 bytes, through the kernel's page table installed in
 *          pool 2), then CP_PASS_THROUGH_GE_WRAM_DELAYED (0xFC) with the
 *          ERAM address and the count rounded up to even, then token 0x001.
 *   Seen: slot evictions read 0x17C8 words from a slot into a per-context
 *   host buffer (0x8000 bytes apart); the X server writes one word at
 *   0xA9A, just below the first slot. Data is big-endian words in host
 *   memory. With two GE11s the kernel repeats each transfer per GE (how the
 *   second GE is picked is not known).
 */
#define CP_PASS_THROUGH_GE_WRAM_5   0xF9
#define CP_PASS_THROUGH_GE_WRAM_4   0xFA    /* kernel GE init, no data */
#define CP_PASS_THROUGH_GE_FIFO_3   0xFB    /* kernel GE init, 2 words */
#define CP_PASS_THROUGH_GE_WRAM_DELAYED 0xFC
#define CP_PASS_THROUGH_GE_WRAM     0xFD    /* kernel GE init, 1 word, then pixel data */
#define CP_PASS_THROUGH_GE_FIFO     0xFE
#define CP_GE_SAVE_IDLE_HQ_STATE    0xFF    /* kernel, around context switches, 1 word */

/*
 * Tokens the IRIX 6.5 kernel sends that no other source names yet.
 */
#define HQ_TOK_KERN_7C              0x07C   /* 8 bytes (0x1, 0x4), during board init */
#define HQ_TOK_KERN_8C              0x08C   /* 4 bytes (0x0), before a context switch */
#define HQ_TOK_SCHEDULE_SWAP        0x037   /* buffer swap at the next retrace; raises CP flag 0 */

/*
 * The OpenGL protocol: the first command word each libGLcore function stores
 * to the FIFO (6.5.0 libGLcore.so, decompiled), with the byte count it sends
 * and the functions that send it. The GE11 microcode gives them meaning;
 * names follow the GL entry points.
 */
#define HQ_TOK_VERTEX4F                   0x000  /*  16 bytes: __glMgrim_Vertex4f, __glMgrim_Vertex4fv */
#define HQ_TOK_COLOR4F                    0x002  /*  16 bytes: __glMgrim_Color4f, __glMgrim_Color4fv */
#define HQ_TOK_SIM_OPEN_GRAPHICS          0x005  /*   0 bytes: simOpenGraphics */
#define HQ_TOK_MAKE_CURRENT_READ          0x006  /*   0 bytes: MakeCurrentRead, simAllocHWContext */
#define HQ_TOK_TEX_COORD4F                0x008  /*  16 bytes: __glMgrim_TexCoord4f, __glMgrim_TexCoord4fv */
#define HQ_TOK_INIT_RGB                   0x009  /*   0 bytes: __glMgrInitRGB */
#define HQ_TOK_ENABLE_LIGHTING            0x00D  /*   0 bytes: __glMgrEnableLighting */
#define HQ_TOK_DISABLE_LIGHTING           0x00E  /*   0 bytes: __glMgrDisableLighting */
#define HQ_TOK_EDGE_FLAG                  0x010  /*   0 bytes: __glMgrim_EdgeFlag, __glMgrim_EdgeFlagv */
#define HQ_TOK_MATERIALF                  0x011  /*  24 bytes: __glMgrim_Materialf, __glMgrim_Materialfv, __glMgrim_Materiali ... */
#define HQ_TOK_LIGHTFV                    0x012  /*  20 bytes: __glMgrim_Lightfv, __glMgrim_Lightiv */
#define HQ_TOK_CLEAR                      0x015  /*   0 bytes: __glMgrim_Clear, Clear */
#define HQ_TOK_BEGIN_POINTS               0x016  /*   0 bytes: __glMgrBeginPoints, Store, StoreSpan ... */
#define HQ_TOK_BEGIN_LINES                0x017  /*   0 bytes: __glMgrBeginLines */
#define HQ_TOK_BEGIN_LSTRIP               0x018  /*   0 bytes: __glMgrBeginLStrip */
#define HQ_TOK_BEGIN_LLOOP                0x019  /*   0 bytes: __glMgrBeginLLoop */
#define HQ_TOK_BEGIN_TRIANGLES            0x01A  /*   0 bytes: __glMgrBeginTriangles */
#define HQ_TOK_BEGIN_TSTRIP               0x01B  /*   0 bytes: __glMgrBeginTStrip */
#define HQ_TOK_BEGIN_TFAN                 0x01C  /*   0 bytes: __glMgrBeginTFan */
#define HQ_TOK_BEGIN_QUADS                0x01D  /*   0 bytes: __glMgrBeginQuads */
#define HQ_TOK_BEGIN_QSTRIP               0x01E  /*   0 bytes: __glMgrBeginQStrip */
#define HQ_TOK_BEGIN_POLYGON              0x01F  /*   0 bytes: __glMgrBeginPolygon */
#define HQ_TOK_END_POINTS                 0x020  /*   0 bytes: __glMgrEndPoints */
#define HQ_TOK_END_LINES                  0x021  /*   0 bytes: __glMgrEndLines */
#define HQ_TOK_END_LSTRIP                 0x022  /*   0 bytes: __glMgrEndLStrip */
#define HQ_TOK_END_LLOOP                  0x023  /*   0 bytes: __glMgrEndLLoop */
#define HQ_TOK_END_TRIANGLES              0x024  /*   0 bytes: __glMgrEndTriangles */
#define HQ_TOK_END_TSTRIP                 0x025  /*   0 bytes: __glMgrEndTStrip */
#define HQ_TOK_END_TFAN                   0x026  /*   0 bytes: __glMgrEndTFan */
#define HQ_TOK_END_QUADS                  0x027  /*   0 bytes: __glMgrEndQuads */
#define HQ_TOK_END_QSTRIP                 0x028  /*   0 bytes: __glMgrEndQStrip */
#define HQ_TOK_END_POLYGON                0x029  /*   0 bytes: __glMgrEndPolygon */
#define HQ_TOK_MATRIX_MODE                0x02A  /*   4 bytes: __glMgrim_MatrixMode */
#define HQ_TOK_LOAD_MATRIXD               0x02B  /*  64 bytes: __glMgrim_LoadMatrixd, __glMgrim_LoadMatrixf */
#define HQ_TOK_LOAD_IDENTITY              0x02C  /*   0 bytes: __glMgrim_LoadIdentity */
#define HQ_TOK_MULT_MATRIXD               0x02D  /*  64 bytes: __glMgrim_MultMatrixd, __glMgrim_MultMatrixf */
#define HQ_TOK_POP_MATRIX                 0x02E  /*   0 bytes: __glMgrim_PopMatrix */
#define HQ_TOK_PUSH_MATRIX                0x02F  /*   0 bytes: __glMgrim_PushMatrix */
#define HQ_TOK_ROTATED                    0x030  /*  16 bytes: __glMgrim_Rotated, __glMgrim_Rotatef */
#define HQ_TOK_SCALED                     0x031  /*  12 bytes: __glMgrim_Scaled, __glMgrim_Scalef */
#define HQ_TOK_TRANSLATED                 0x032  /*  12 bytes: __glMgrim_Translated, __glMgrim_Translatef */
#define HQ_TOK_VIEWPORT                   0x033  /*  16 bytes: __glMgrim_Viewport, __glMgrSetSoftHardwareState, Resize */
#define HQ_TOK_FRUSTUM                    0x034  /*  24 bytes: __glMgrim_Frustum */
#define HQ_TOK_ORTHO                      0x035  /*  24 bytes: __glMgrim_Ortho */
#define HQ_TOK_FLUSH                      0x036  /*   0 bytes: __glMgrim_Flush */
#define HQ_TOK_RASTER_POS4F               0x038  /*  16 bytes: __glMgrim_RasterPos4f, __glMgrim_RasterPos4fv */
#define HQ_TOK_SCISSOR                    0x039  /*  16 bytes: __glMgrim_Scissor */
#define HQ_TOK_STENCIL_MASK               0x03A  /*   4 bytes: __glMgrim_StencilMask */
#define HQ_TOK_COLOR_MASK                 0x03B  /*   4 bytes: __glMgrim_ColorMask */
#define HQ_TOK_DEPTH_MASK                 0x03C  /*   4 bytes: __glMgrim_DepthMask */
#define HQ_TOK_INDEX_MASK                 0x03D  /*   4 bytes: __glMgrim_IndexMask */
#define HQ_TOK_ALPHA_FUNC                 0x03E  /*   8 bytes: __glMgrim_AlphaFunc */
#define HQ_TOK_BLEND_FUNC                 0x03F  /*   8 bytes: __glMgrim_BlendFunc */
#define HQ_TOK_LOGIC_OP                   0x040  /*   4 bytes: __glMgrim_LogicOp */
#define HQ_TOK_STENCIL_FUNC               0x041  /*  12 bytes: __glMgrim_StencilFunc */
#define HQ_TOK_STENCIL_OP                 0x042  /*  12 bytes: __glMgrim_StencilOp */
#define HQ_TOK_DEPTH_FUNC                 0x043  /*   4 bytes: __glMgrim_DepthFunc */
#define HQ_TOK_READ_BUFFER                0x044  /*  12 bytes: __glMgrasReadBuffer */
#define HQ_TOK_LINE_WIDTH                 0x045  /*   4 bytes: __glMgrim_LineWidth */
#define HQ_TOK_LINE_STIPPLE               0x046  /*   8 bytes: __glMgrim_LineStipple */
#define HQ_TOK_SHADE_MODEL                0x047  /*   4 bytes: __glMgrim_ShadeModel */
#define HQ_TOK_DEPTH_RANGE                0x048  /*   8 bytes: __glMgrim_DepthRange */
#define HQ_TOK_DRAW_BUFFER                0x049  /*  12 bytes: __glMgrasDrawBuffer */
#define HQ_TOK_CLEAR_                     0x04A  /*   0 bytes: Clear */
#define HQ_TOK_CLEAR__                    0x04C  /*   0 bytes: Clear */
#define HQ_TOK_POINT_SIZE                 0x04D  /*   4 bytes: __glMgrim_PointSize */
#define HQ_TOK_LOAD_POLYGON_STIPPLE_RAM   0x04E  /* 128 bytes: __glMgrLoadPolygonStippleRAM */
#define HQ_TOK_LIGHTF                     0x055  /*   8 bytes: __glMgrim_Lightf, __glMgrim_Lighti */
#define HQ_TOK_FRONT_FACE                 0x056  /*   4 bytes: __glMgrim_FrontFace */
#define HQ_TOK_LIGHT_MODELIV              0x057  /*  12 bytes: __glMgrim_LightModeliv */
#define HQ_TOK_LIGHT_MODELF               0x059  /*   4 bytes: __glMgrim_LightModelf, __glMgrim_LightModelfv, __glMgrim_LightModeli */
#define HQ_TOK_TEX_GENIV                  0x05A  /*  12 bytes: __glMgrim_TexGeniv */
#define HQ_TOK_PASS_THROUGH               0x05D  /*   4 bytes: __glMgrim_PassThrough */
#define HQ_TOK_DISABLE                    0x05F  /*   4 bytes: __glMgrim_Disable, __glMgrim_Enable, send_only_texmode2 */
#define HQ_TOK_BLEND_EQUATION_EXT         0x060  /*   4 bytes: __glMgrim_BlendEquationEXT */
#define HQ_TOK_GET_FEEDBACK_DATA          0x061  /*   0 bytes: __glMgrGetFeedbackData, __glMgrGetSelectData */
#define HQ_TOK_GET_ERROR                  0x063  /*   0 bytes: __glMgrim_GetError */
#define HQ_TOK_SOFT_DISABLE               0x06E  /*   4 bytes: __glMgrim_SoftDisable, __glMgrim_SoftEnable */
#define HQ_TOK_ENABLE_LIGHT_SOURCE        0x071  /*   4 bytes: __glMgrEnableLightSource */
#define HQ_TOK_CULL_FACE                  0x074  /*   4 bytes: __glMgrim_CullFace */
#define HQ_TOK_INIT_TABLES                0x082  /*   4 bytes: InitTables */
#define HQ_TOK_RESET_HISTOGRAM_MEMORY     0x083  /*  12 bytes: __glMgrResetHistogramMemory */
#define HQ_TOK_INIT_CONVOLVE2_DSTATE      0x085  /*  12 bytes: __glMgrInitConvolve2DState, __glMgrInitSeparable2DState */
#define HQ_TOK_FAST_COPY_PIXELS           0x086  /*   0 bytes: __glMgrFastCopyPixels, __glMgrSendPixels */
#define HQ_TOK_NEW_INIT_FAST_PATH         0x088  /*   0 bytes: __glMgrNewInitFastPath */
#define HQ_TOK_INIT_COLOR_TABLE           0x089  /*   4 bytes: __glMgrInitColorTable, __glMgrInitLUTCIST, __glMgrInitLUTRGB */
#define HQ_TOK___MGR_RETURN_HIST_PIO      0x08B  /*   8 bytes: __Mgr_ReturnHistPIO */
#define HQ_TOK_COPY_TILE                  0x08E  /* 112 bytes: __glMgrCopyTile */
#define HQ_TOK_GENERAL_BITMAP             0x091  /*  20 bytes: __glMgrGeneralBitmap */
#define HQ_TOK_BITMAP                     0x094  /*  32 bytes: __glMgrim_Bitmap, __glMgrRenderBitmap */
#define HQ_TOK_COPY_COLOR_TABLE_SGI       0x096  /*   0 bytes: __glMgrim_CopyColorTableSGI, __glMgrim_ReadPixels, __glMgrSlowCopyPixels */
#define HQ_TOK_INIT_FORMAT_VALUES         0x09A  /*   8 bytes: InitFormatValues */
#define HQ_TOK_MGR_TM_DO_TEX_LOAD         0x09B  /*   0 bytes: mgrTM_DoTexLoad */
#define HQ_TOK__WRITE_DMAGESETUP          0x09D  /*  16 bytes: __glMgr_WriteDMAGESetup */
#define HQ_TOK_DISABLE_LIGHT_SOURCE       0x0A0  /*   4 bytes: __glMgrDisableLightSource */
#define HQ_TOK_SPIN_AND_RETURN            0x0A1  /*   4 bytes: spin_and_return */
#define HQ_TOK___MGR_RETURN_MODE          0x0A2  /*  12 bytes: __Mgr_ReturnMode */
/*
 * __MGR_RETURN_MODE (addr, 1, 1) reads back one GE state word: libGLcore
 * clears HQ3_FLAG_GE_DATA (user flag clear 0x7000C), sends the token, polls
 * the user flag register (0x70008) for it, sleeping between polls, then
 * reads the answer at 0x70014. glGet*, glIsEnabled and glFinish use it;
 * glFinish is glGetBooleanv(GL_DITHER) (GE address 0x1FC) followed by
 * polling the status register (0x70000) bit 1.
 */
/*
 * glEnable / glDisable and clears, as libGLcore sends them (mgras_attrib.c,
 * mgras_clear.c): most enables only set bits in the context and are sent
 * later; these go out directly.
 *   0x066 (1 word)  GL_DEPTH_TEST: 1 / 0 (0 also when the visual has no Z)
 *   0x067 (1 word)  GL_DITHER: 3 / 0
 *   0x06E (1 word)  GL_SCISSOR_TEST: 1 / 0
 *   0x06B (1 word)  GL_INDEX_LOGIC_OP / GL_COLOR_LOGIC_OP: 1
 *   0x072 / 0x073   GL_CULL_FACE on / off (no data); CULL_FACE (0x074) takes
 *                   GL_FRONT / GL_BACK / GL_FRONT_AND_BACK, FRONT_FACE
 *                   (0x056) GL_CW (0x900) / GL_CCW (0x901)
 *   0x064 / 0x065   GL_ALPHA_TEST / GL_BLEND: 1 / 0
 *   0x060 (1 word)  glBlendEquationEXT, the GL enum (0x8006 FUNC_ADD ...).
 *                   With GL_LOGIC_OP (0x0BF1), GL_BLEND on enables the
 *                   colour logic op (LOGIC_OP 0x040) instead of blending
 *                   (EXT_blend_logic_op). IRIS GL's logicop() goes this
 *                   way: AutoCAD R13 draws its crosshair as BLEND on,
 *                   LOGIC_OP 6 (XOR), white line strips, then LOGIC_OP 3,
 *                   BLEND off, without ever sending 0x06B (traced
 *                   2026-10-13).
 *   0x06A / 0x06D   GL_LINE_STIPPLE / GL_POLYGON_STIPPLE: 1 / 0
 *   0x06F           GL_STENCIL_TEST: 1 / 0
 * Data of the state tokens (traced): compare functions (ALPHA_FUNC,
 * DEPTH_FUNC, STENCIL_FUNC) as indices 0..7 = NEVER..ALWAYS, not GL enums;
 * ALPHA_FUNC (func, ref * 4096); BLEND_FUNC (src, dst) as GL enums;
 * LOGIC_OP the op index (GL order, = X11 GX order); STENCIL_FUNC (func,
 * ref, mask); STENCIL_OP (fail, zfail, zpass) as indices KEEP 0 ZERO 1
 * REPLACE 2 INCR 3 DECR 4 INVERT 5 (ZERO/DECR/INVERT inferred);
 * COLOR_MASK RGBA in bits 0..3; CLEAR_DEPTH a 24-bit integer (0xFFFFFF =
 * 1.0); LINE_STIPPLE (repeat - 1, pattern bit-reversed: first bit in 15);
 * LINE_WIDTH a float; POLYGON_MODE (face, mode) as GL enums; SCISSOR (x0,
 * x1, y0, y1) inclusive; LOAD_POLYGON_STIPPLE_RAM 32 rows, row 0 first.
 *
 * Context images (see CONTEXT MANAGEMENT): word 0 is the context's ERAM
 * slot, word 1 bit 31 marks its first load (slots are reused).
 *   0x04A           glClear depth part (only with a depth buffer)
 *   0x04C           glClear stencil part
 */
#define HQ_TOK_DEPTH_TEST_ENABLE          0x066
#define HQ_TOK_DITHER_ENABLE              0x067
#define HQ_TOK_CULL_FACE_ENABLE           0x072
#define HQ_TOK_CULL_FACE_DISABLE          0x073
#define HQ_TOK_CLEAR_DEPTH_BUFFER         0x04A
#define HQ_TOK_CLEAR_STENCIL_BUFFER       0x04C
/*
 * Lighting, fog, clip planes (libGLcore mgras_lighting.c, Fogfv; traced):
 *   0x007 normal (3 floats); 0x00D / 0x00E lighting on / off
 *   0x011 glMaterial: face (GL_FRONT/BACK/FRONT_AND_BACK), pname (GL enum),
 *         4 values (shininess in the first)
 *   glLight, light index first: 0x012 ambient, 0x013 diffuse, 0x014
 *         specular (4 floats), 0x04F position (4, object space: the GE
 *         applies the modelview current when it arrives), 0x050 spot
 *         direction (3, object space), 0x051 spot exponent, 0x052 spot
 *         cutoff, 0x053 / 0x054 / 0x055 constant / linear / quadratic
 *         attenuation
 *   0x057 light model ambient (3 floats), 0x058 local viewer, 0x059 two
 *         sided; 0x071 / 0x0A0 light i on / off (index)
 *   0x0BF / 0x0C0 / 0x0C1 glColorMaterial front / back / both (mode enum);
 *         0x0C2 / 0x0C3 GL_COLOR_MATERIAL on / off; 0x0A3 GL_NORMALIZE
 *   0x068 fog on/off; 0x0B9 fog mode (GL enum); 0x0B4 colour, 0x0B5
 *         start, 0x0B6 end, 0x0B7 density, 0x0B8 index
 *   0x0A5 glClipPlane: 0x3000 + plane, a, b, c, d (object space);
 *         0x0A6 / 0x0A7 plane on / off (0x3000 + plane)
 * IRIS GL through IGLOO (mgras_igloo.c): 0x0DC swaptmesh, 0x0DD / 0x0DE
 *   lmcolor(LMC_COLOR) on / off, 0x0DF n3f (3 floats); CLEAR_COLOR with
 *   the words 4, 5, 6, 7 = clear to the current colour (GE state addresses).
 * Readbacks: __MGR_RETURN_MODE addresses 0x3A-0x49 = the current matrix
 *   (IGLOO getmatrix), 0x1FC = dither; 0x2CD-0x2CF the current raster
 *   position (window x, y, z as floats) and 0x2D0 its valid flag, with
 *   0x2C4 (w?) read alongside (IRIS GL getcpos; gr_osview measures its text
 *   with it, our reading of the order); 0xB98-0xB9B the scissor box and
 *   0xB9C glIsEnabled(GL_SCISSOR_TEST), 1 / 0. libGLcore's
 *   __glMgrim_IsEnabled asks the GE for the scissor test, from the
 *   SYMBOL_LOCATION entry just before the box's four. The address is
 *   inferred from Mirai 1.0, which reads it before every front-to-back
 *   copy. 0x0A1 SPIN_AND_RETURN (1 word) waits, then returns the word at
 *   its argument, with the same flag protocol. Seen with 4-7: the current
 *   colour, RGBA floats (IRIS GL getcolor, and Mirai around each widget).
 *   libGLcore takes these addresses from a table filled at run time
 *   (SYMBOL_LOCATION, in .bss), not from constants in its code.
 * Raster position and bitmaps: 0x038 RASTER_POS4F (object x, y [, z, w]);
 *   0x094 BITMAP, 8 words: format (0x1800F seen), row length in bits, rows,
 *   x / y origin, x / y move (floats), data words; the rows follow as FIFO
 *   pixel data, rows padded to bytes, bottom row first, most significant
 *   bit leftmost (glXUseXFont text, IRIS GL charstr). IRIS GL programs set
 *   a colour between bitmaps without a new raster position and expect it
 *   to apply (gr_osview's legend), and libGLcore's glBitmap sends no
 *   colour: the raster colour follows the current colour (our reading).
 * Raster register lists (libGLcore __glMgrFlushImageRegs; token, mode word,
 *   count, then the list as FIFO pixel data padded to doublewords):
 *   0x07F RSS_REG_SET (register, value) pairs; 0x07C RSS_REG_SHADOW
 *   (register, value, GE shadow address) triples, __glMgrRSSRegShadow. At
 *   GL context creation 0x07C carries the context's whole initial raster
 *   state (33 triples: fill mode, texture mode, CONFIG = 0x4, scissors ...)
 *   for the GE's shadow -- not for the raster engine while the X server
 *   owns it (written straight through, X lost its Y-flip). Inside a pixel
 *   operation (SAVE_RSS 0x0D2 .. RESTORE_RSS 0x0D3) the lists set up the
 *   transfer: block (0x46 / 0x47), xfrsize 0x153, zoom factors
 *   0x154 / 0x155 (8.8, 0x600 = 6), masks 0x156 / 0x157, counters 0x158,
 *   xfrmode 0x159; RESTORE_RSS puts the raster engine back (from the
 *   shadow, presumably).
 * glReadPixels: SAVE_RSS (word 1 = fill mode, 0x1000000 = DMA read block),
 *   the lists above, 0x0DB GET_PIXELS_BITMAP_PIO (1 word, 0x4009); the host
 *   then arms a board-to-host DMA and starts the raster side by writing
 *   0x4009 (xfrcontrol read start in the low bits) to raster interface
 *   register 5. Pixel zoom: GE pixel state 0x192 (token 0x080, a GE state
 *   write: words 3 address, 6 count, then values) holds 1 / zoom.
 *   SAVE_RSS words 2 and 3 say which planes the operation moves (traced,
 *   glprim --read front/depth/stencil on a z24s8 visual, 2026-10-10):
 *     colour   word 2 0xFF        word 3 0          xfrmode format 8 type 0
 *     depth    word 2 0x1E000FF   word 3 0x1C00000  format 2 type 3 (32-bit)
 *     stencil  word 2 0x1E000FF   word 3 0x1800000  format 1 type 0 (8-bit)
 *   A stencil read has the same transfer format as a colour-index read,
 *   so only SAVE_RSS tells them apart; nothing else in the stream differs
 *   (the block, counters and GET_PIXELS are the same). Our reading: word 3
 *   bits 24:22 select the ZST planes (bit 22 the depth part). The stencil
 *   index is the ZST word's top byte (Z 23:0, stencil 31:24, see PP1.h).
 * glDrawPixels (libGLcore __glMgrSendPixels; traced with mandel, colour
 *   index GL_UNSIGNED_SHORT, one 100-pixel row a call): RASTER_POS4F,
 *   LOAD_RASTER_POS_INFO, SAVE_RSS / SAVE_TEXMODE, 0x0CD
 *   INIT_PIXEL_STATE_ERAM (__glMgrInitPixelStateERAM: counts, then lists
 *   of GE pixel-pipeline stage addresses, e.g. 0xDA8, 0xDB7), 0x07F
 *   RSS_REG_SET whose following pixel data is (raster register, value)
 *   pairs (xfrmode 0x159 = 0xC00001 for 16-bit indices, xfrmasklow
 *   0x156), then 0x08D SEND_PIXELS, 8 words: data words per row, two
 *   skips, pixel-state counts (%3, /3), rows (provisional), the GE routine
 *   (0x49D0, host to raster), flags (0x18); the rows follow as FIFO pixel
 *   data, bottom row first; then RESTORE_RSS / RESTORE_TEXMODE.
 *   Texture loads (routine 0x511A): a row is words 0 + 1, not word 0
 *   (traced 2026-10-10, glprim on 6.5.22: 64x64 GL_LUMINANCE_ALPHA sends
 *   [0x10, 0x10, 0, 1, 0x15, ...] with 128-byte rows; 256x256 RGBA comes
 *   as 128-texel strips [0x40, 0x40, 0, 1, 0x55, ...] with 512-byte rows;
 *   rows = word 3 + 3 * word 4 = 64 / 256). The texture manager's TRAM
 *   restores send word 1 as 0 ([0x40, 0, 0, 0x80, 0, ...], 256-byte rows
 *   of the 64-wide page view). So word 1 is not a skip there, and
 *   glDrawPixels uses it the same way: Advanced Visualizer's DMA window
 *   restore (156-pixel RGBA strips) sends [0x4E, 0x4E, 0, 0, 0xBD, 1,
 *   0x49D0, 0x88], 624-byte rows; FIFO glDrawPixels sends word 1 as 0. 0x080
 *   (10 words, a 1.0 among them) comes with it, probably zoom. KERN_7C
 *   before it carries 6 words of GE data, not register pairs.
 * Colour-index contexts: 0x00A INIT_CI (1 word, 8 or 12) where an RGB
 *   context sends 0x009 INIT_RGB; colours are then indices (COLOR4F one
 *   value, e.g. a short converted), drawn as 12-bit colour-index pixels;
 *   0x0BD CLEAR_INDEX for glClear.
 * Double buffering: DRAW_BUFFER (0x049, 3 words) carries PP1 fill mode
 *   draw-field values (PP1.h): 1 front (A), 4 back as a second page, 2 back
 *   as the high 12-bit half of A's words, 0 none; 3 / 5 = both (FRONT_AND_
 *   BACK). __glMgrasDrawBuffer sends GL_BACK as [2, 1, 0] when the visual
 *   has fewer than 8 green bits (12-bit visuals: 4444, 5551) and [4, 1, 0]
 *   otherwise (traced: octahedra, electropaint, glprim). The kernel sends
 *   VALIDATE_BANKS (0x098, [n, n]: the bank the next frame draws) and
 *   SCHEDULE_SWAP (0x037, MgrasSchedSwapBuf), then flips XMAP BUF_SELECT
 *   (select 3, index 0: bit = window ID shows buffer B) at retrace. For a
 *   12-bit visual buffer B is the high half of the same page, whatever the
 *   window's DRBpointers / DIB second field say (CMAP.h, PP1.h).
 * Pixel format: INIT_FORMAT_VALUES (0x09A, 2 words, the same value twice)
 *   gives the context's PP1 pixel format, fill mode bits 13 and 10:8
 *   (traced on 6.5.22 per visual): 0 RGBA 4,4,4,4 (electropaint), 0x100
 *   RGBA 5,5,5,1 (octahedra), 0x2200 RGBA 8,8,8,8, 0x2300 and 0x2400 the
 *   12,12,12 visuals. Bit 13 clear = 12-bit pixels stored as word halves.
 *   What 0x100 stores (5,5,5,1 cannot fit 12 bits) is open.
 *
 * Control word data conversion (bits 29:23), from libGLcore's glColor /
 * glVertex / glNormal / glIndex forms: bit 5 convert to float, bit 4
 * signed, bit 3 normalise (GL rules), bits 2:0 source size (0 32-bit, 1
 * byte, 2 short, 4 double); bit 6 appends a fourth component of 1.0.
 * Examples: glColor4ub 0x29, glColor3f 0x40, glVertex3s 0x72, glNormal3b
 * 0x39, glVertex4d 0x24.
 */
#define HQ_TOK_KERN_WINDOW                0x0E4  /*  60 bytes: GL window state, kernel */
/*
 * A GL context's window, from MgrasValidateClip. Sent as KERN_WINDOW when
 * the context is current; otherwise parked in the context's image (the 63
 * words the kernel writes back through the FIFO at a context switch, see
 * HQ3_CTXSW) with bit 29 of image word 1 set, and loaded with the image.
 *   KERN_WINDOW words           image words
 *   0  origin x | y << 16       2   (y bottom-up: screen h - (top + h))
 *   1  buffer select bits       4   ((x & 3) | (y & 3) << 2 | stereo)
 *   2  window mode              3   (RE4 winmode: masks on 3:0, inside 7:4)
 *   3,4   screen mask 4 x, y    5,6 (RE4 scrmsk ranges, min << 16 | max)
 *   5,6   screen mask 3         7,8
 *   7,8   screen mask 2         9,10
 *   9,10  screen mask 1         11,12
 *   11 DRB pointers             13
 *   12 0
 *   13 second origin            14
 *   14 second DRB pointers      15
 * Seen (6.5.22, a 400x300 window at 32,32 on 1024x768): origin 0x1B40020,
 * mode 0x11, mask 1 0x2001AF / 0x1B402DF, DRB pointers 0xB81C0.
 */
#define HQ_TOK_CLIP_PLANE                 0x0A5  /*  20 bytes: __glMgrim_ClipPlane, __glMgrim_PassClipPlane */
#define HQ_TOK_POLYGON_MODE               0x0A8  /*   8 bytes: __glMgrim_PolygonMode */
#define HQ_TOK_EVAL_MESH1_DGE11           0x0A9  /*  12 bytes: __glMgrEvalMesh1DGe11 */
#define HQ_TOK_EVAL_MESH2_DGE11           0x0AA  /*  24 bytes: __glMgrEvalMesh2DGe11 */
#define HQ_TOK_EVAL_COORD1_DGE11          0x0AB  /*   0 bytes: __glMgrEvalCoord1DGe11 */
#define HQ_TOK_EVAL_COORD2_DGE11          0x0AC  /*   0 bytes: __glMgrEvalCoord2DGe11 */
#define HQ_TOK_MAP_GRID1D                 0x0AE  /*  16 bytes: __glMgrim_MapGrid1d, __glMgrim_MapGrid1f, __glMgrUpdateEvalAttributes */
#define HQ_TOK_MAP_GRID2D                 0x0AF  /*  32 bytes: __glMgrim_MapGrid2d, __glMgrim_MapGrid2f */
#define HQ_TOK_CLEAN_UP1_D                0x0B0  /*  48 bytes: __glMgrCleanUp1D */
#define HQ_TOK_CLEAN_UP2_D                0x0B1  /*  68 bytes: __glMgrCleanUp2D */
#define HQ_TOK_SEND_MAP_DATA1_D           0x0B2  /*  24 bytes: __glMgrSendMapData1D */
#define HQ_TOK_SEND_MAP_DATA2_D           0x0B3  /*  36 bytes: __glMgrSendMapData2D */
#define HQ_TOK_FOGIV                      0x0B7  /*   4 bytes: __glMgrim_Fogiv */
#define HQ_TOK_FOGFV                      0x0B9  /*   4 bytes: __glMgrim_Fogfv */
#define HQ_TOK_CLEAR_COLOR                0x0BA  /*  16 bytes: __glMgrim_ClearColor */
#define HQ_TOK_CLEAR_ACCUM                0x0BB  /*  16 bytes: __glMgrim_ClearAccum */
#define HQ_TOK_CLEAR_DEPTH                0x0BC  /*   4 bytes: __glMgrim_ClearDepth */
#define HQ_TOK_CLEAR_INDEX                0x0BD  /*   4 bytes: __glMgrim_ClearIndex */
#define HQ_TOK_CLEAR_STENCIL              0x0BE  /*   4 bytes: __glMgrim_ClearStencil */
#define HQ_TOK_COLOR_MATERIAL             0x0C1  /*   4 bytes: __glMgrim_ColorMaterial */
#define HQ_TOK_INIT_NAMES                 0x0C4  /*   0 bytes: __glMgrim_InitNames */
#define HQ_TOK_LOAD_NAME                  0x0C5  /*   4 bytes: __glMgrim_LoadName */
#define HQ_TOK_POP_NAME                   0x0C6  /*   0 bytes: __glMgrim_PopName */
#define HQ_TOK_PUSH_NAME                  0x0C7  /*   4 bytes: __glMgrim_PushName */
#define HQ_TOK_RENDER_MODE                0x0C9  /*   0 bytes: __glMgrim_RenderMode */
#define HQ_TOK_END_GENERIC                0x0CC  /*   0 bytes: __glMgrEndGeneric */
#define HQ_TOK_SAVE_RSS                   0x0D2  /*  20 bytes: __glMgrSaveRSS */
#define HQ_TOK_RESTORE_RSS                0x0D3  /*   0 bytes: __glMgrRestoreRSS */
#define HQ_TOK_SEND_HISTOGRAM_FILLS       0x0D4  /*  24 bytes: __glMgrSendHistogramFills */
#define HQ_TOK_SEND_MINMAX_FILLS          0x0D5  /*  36 bytes: __glMgrSendMinmaxFills */
#define HQ_TOK_UPDATE_RASTER_POS          0x0D6  /*   8 bytes: __glMgrUpdateRasterPos */
#define HQ_TOK_LOAD_RASTER_POS_INFO       0x0D7  /*   0 bytes: __glMgrLoadRasterPosInfo */
#define HQ_TOK_BLEND_COLOR_EXT            0x0D9  /*  16 bytes: __glMgrim_BlendColorEXT */
#define HQ_TOK_GET_PIXELS_BITMAP_PIO      0x0DB  /*   4 bytes: GetPixelsBitmapPIO, GetPixelsDMA, GetPixelsFloatDMA ... */
#define HQ_TOK_SWAPTMESH                  0x0DC  /*   0 bytes: __glMgrim_Swaptmesh */
#define HQ_TOK_LMCOLOR                    0x0DE  /*   0 bytes: __glMgrim_lmcolor */
#define HQ_TOK_DRAW_DUMB_POINT            0x0E0  /*  28 bytes: __glMgrim_DrawDumbPoint */
#define HQ_TOK_POLYGON_OFFSET             0x0E7  /*  12 bytes: __glMgrim_PolygonOffset, __glMgrim_PolygonOffsetEXT */
#define HQ_TOK_SAVE_TEXMODE               0x0E9  /*  16 bytes: __glMgrSaveTexmode */
#define HQ_TOK_RESTORE_TEXMODE            0x0EA  /*   0 bytes: __glMgrRestoreTexmode */
/*
 * Traced 2026-10-16 with test/gltest/toktest.c (one GL feature a step) on
 * IRIX 6.5.22, Maximum Impact:
 *   0x06C [1] / [0]  glEnable / glDisable(GL_POINT_SMOOTH) (also [0] from
 *                    the pick code's soft-path setup).
 *   0x0E9 [1, 0, 0, 0] ... 0x0EA  around glDrawPixels, glCopyPixels,
 *                    glTexImage2D, glTexSubImage2D and glCopyTexImage2D:
 *                    the texture mode is kept aside and the operation
 *                    drawn untextured (glDrawPixels with GL_TEXTURE_2D on
 *                    is sent this way too).
 *   0x09B            a texture load with no lookup table, where
 *                    mgrTM_DoTexLoad would otherwise send the TL table.
 *   0x09C            after each SEND_PIXELS of a texture load (end of a
 *                    sub-load).
 *   0x0EB [0], twice, then TEX_OFF (0x00C)  before every texture load;
 *                    meaning not known.
 *   0x05F [0]        three per texture load (texture manager, after
 *                    __glMgrSaveTexmode); the data is a GLenum from
 *                    __glMgrim_Enable/Disable elsewhere. Meaning of [0] not
 *                    known.
 *   0x097, 0x0DA, 0x096 (0 bytes)  in glCopyPixels and glCopyTexImage2D;
 *                    meaning not known.
 *   __MGR_RETURN_MODE [3, 0, 0]  before every texture load and pixel
 *                    operation; the host polls for it and reads the answer
 *                    but does not use it: a round trip that waits for the
 *                    pipeline. GE word 3's contents are not known.
 *   glDrawPixels / glCopyPixels draws carry no depth on the bus: the RE4
 *                    gets Zmode (test, function, write mask) and the
 *                    fragments must take the raster position's z, which
 *                    the GE holds (readback 0x2CF); LOAD_RASTER_POS_INFO
 *                    (0x0D7) comes right before each such draw.
 */
#define HQ_TOK_DETAIL_TEX_FUNC_SGIS       0x0EC  /*  16 bytes: __glMgrim_DetailTexFuncSGIS, RestoreData, mgrTM_DoTexRead ... */
#define HQ_TOK_SET_DATA                   0x0ED  /*   8 bytes: __glMgrasSetData */
#define HQ_TOK_GET_MATERIALFV             0x0EE  /*   8 bytes: __glMgrim_GetMaterialfv, __glMgrim_GetMaterialiv */
#define HQ_TOK_GET_LIGHTFV                0x0EF  /*   8 bytes: __glMgrim_GetLightfv, __glMgrim_GetLightiv */
#define HQ_TOK_GET_HISTOGRAM_EXT          0x0F2  /*   0 bytes: __glMgrim_GetHistogramEXT, __glMgrim_GetMinmaxEXT, __glMgr_AltWriteDMAHQSetup ... */
#define HQ_TOK_COPY_PIXELS                0x100  /*   4 bytes: __glMgrim_CopyPixels, __glMgrim_DrawPixels */

/*
 * Primitive delimiters: Begin (0x016 + GL mode) and End (0x020 + GL mode),
 * GL_POINTS .. GL_POLYGON, as in the table above.
 */
#define HQ_TOK_BEGIN(glmode)        (0x016 + (glmode))
#define HQ_TOK_END(glmode)          (0x020 + (glmode))

#ifdef __cplusplus
}
#endif

#endif /* _SGI_HQ3_H_ */
