/*
 * TE1.h - Silicon Graphics Impact Graphics (MGRAS) TE1 Texture Engine & TR1 TETRAM
 *
 * Texture Engine 1 (TE1), TETRAM Controller (TR1), and Texture Subsystem for SGI Impact (GR4).
 *
 * Reverse-engineered documentation and single source of truth for IRIS emulator.
 * Synthesized from:
 *   - SGI IRIX PROM (stand/arcs/lib/libsk/graphics/MGRAS)
 *   - SGI IDE diagnostics (stand/arcs/ide/isddevices/graphics/MGRAS/mgras_tetr.c, mgras_tetramfunc.c, mgras_tex_load.c)
 *   - IRIX 6.5.0 kernel modules (mgras_tex.o, mgras_tex_io.o, mgras_texsr.o)
 *   - SGI OpenGL core driver (libGLcore.so) DWARF metadata (mgras_texture.c, mgras_texman.c)
 */

#ifndef _SGI_TE1_H_
#define _SGI_TE1_H_

#include <stdint.h>
#include "MGRAS.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * ARCHITECTURE OVERVIEW: TE1 TEXTURE ENGINE & TR1 TETRAM
 * ============================================================================
 *
 * Gate Count:      ~250,000 gates
 * Operating Clock: 50 MHz
 *
 * Introduced on the SGI High Impact and Maximum Impact in 1995, the TE1/TR1
 * subsystem was the industry's first desktop workstation hardware texture engine
 * providing single-pass real-time trilinear mipmapped 2D and 3D texture filtering.
 *
 * Key Architectural Components:
 *
 * 1. PERSPECTIVE COORDINATE INTERPOLATION:
 *    - Iterates homogeneous texture coordinates: S/W, T/W, R/W (for 3D volume textures),
 *      and inverse homogeneous coordinate 1/W across primitive spans.
 *    - Performs fast perspective division to recover true texture coordinates (s, t, r)
 *      at each fragment.
 *
 * 2. MIPMAPPING & LEVEL-OF-DETAIL (LOD) PIPELINE:
 *    - Computes continuous Level-of-Detail (lambda) from partial derivatives
 *      (ds/dx, ds/dy, dt/dx, dt/dy).
 *    - Fetches texels from two adjacent mipmap levels in TETRAM simultaneously.
 *    - Performs dual-bilinear filtering followed by linear interpolation between levels
 *      (true trilinear filtering in a single clock cycle).
 *
 * 3. 3D VOLUME TEXTURING & 4D LUTs:
 *    - Supports 3D volumetric textures (s, t, r) for medical imaging, volume rendering,
 *      and scientific visualization.
 *    - Hardware 4D color lookup tables (LUT4D) for pseudo-color and transfer function mapping.
 *
 * 4. DEDICATED TETRAM MEMORY DAUGHTERCARD:
 *    - High-speed Rambus DRAM dedicated solely to texture storage.
 *    - Configurations:
 *      - 1 MB TRAM: Single bank (High Impact standard).
 *      - 4 MB TRAM: Quad bank (High Impact max, Maximum Impact standard).
 *    - Fast downloading via Host-to-Device Memory-to-Device DMA (MDDMA).
 *
 * 5. TEXTURE ENVIRONMENTS:
 *    - MODULATE: C = Cf * Ct
 *    - DECAL:    C = Cf * (1 - At) + Ct * At
 *    - BLEND:    C = Cf * (1 - Ct) + Cc * Ct
 *    - REPLACE:  C = Ct
 */

/*
 * ============================================================================
 * HOW TEXELS GET IN AND OUT (traced from the IDE TRAM tests, the kernel's
 * TRAM page save/restore and libGLcore's texture loader)
 * ============================================================================
 *
 * There is no address/data port. TRAM is written and read only by block
 * transfers through the RE's ordinary transfer engine (XFRSIZE 0x153,
 * XFRCOUNTERS 0x158, XFRMODE 0x159, XFRCONTROL 0x102), the same one that
 * moves pixels for glDrawPixels/glReadPixels. XFRCONTROL picks the far end:
 * its device field is 0 for PP1 (framebuffer) and 1 for the texture side.
 *
 * Download (glTexImage, and the kernel restoring a TRAM page):
 *   - Raster state is bracketed by SAVE_RSS (0x0D2, fill mode 0x01400000)
 *     and RESTORE_RSS (0x0D3), as for glDrawPixels.
 *   - The texture loader registers say where and how texels land:
 *       TL_MIPMAP 0x1A4  TRAM page the level starts at
 *       TL_ADDR   0x1A3  written 0 around TL_MIPMAP
 *       TL_SPEC   0x1A2  log2 S/T sizes, LOD level, max LOD
 *       TL_MODE   0x1A1  component count, component depth, double-buffer
 *                        enable, border handling; bit 14 (0x4000) is set
 *                        on every load (the IDE calls it "bottom-up")
 *       TL_S_SIZE/TL_T_SIZE 0x1A9/0x1AA  block size in texels
 *       TL_S_LEFT/TL_T_BOTTOM 0x1AB/0x1AC  sub-image origin (0 for a full load)
 *       TL_WBUFFER 0x1A0 / TEXRBUFFER 0x18B  write and read bank; the
 *                        driver always programs them as opposites
 *   - TEXMODE1 0x111 (shared RE/TE) carries the TRAM count in bits 6:5
 *     (0, 1, 2 = 1, 2, 4 TRAMs) and the texture component count.
 *   - Texels then arrive as pixel data: from libGLcore through the GE pixel
 *     pipeline (INIT_PIXEL_STATE_ERAM 0x0CD + SEND_PIXELS 0x08D, so pixel
 *     transfer ops and format conversion apply), from the kernel and the IDE
 *     as a raw host DMA. The kernel moves TRAM one page at a time as a
 *     64-texel-wide block (TL_S_SIZE = 0x40, XFRSIZE = rows << 16 | 0x40).
 *
 * Readback (glGetTexImage, libGLcore evicting a texture, the kernel saving a
 * TRAM page when another process needs the space):
 *   - The sampler side is aimed at the page: TXSIZE 0x181, TXMIPMAP 0x191
 *     (page), TXADDR 0x190, TXLOD 0x183 = 0, TXCLAMPSIZE 0x18C; TEXMODE2 is
 *     set to nearest filtering with clamping.
 *   - SAVE_RSS with fill mode 0x01001000, then a block "draw" whose
 *     fragments are the fetched texels; the RE streams them into a read DMA,
 *     like glReadPixels. The kernel starts it with GE token 0x0E5
 *     [rows, xfrmode, 0x400D]; compare GET_PIXELS' 0x4009 for the
 *     framebuffer.
 *   - So readback is by block too, not one word at a time.
 *
 * Drawing:
 *   - The GE's triangle setup produces S/W, T/W and 1/W (R/W for 3D) with
 *     their edge, X and Y slopes into the double-buffered 0x080-0x0CB
 *     registers, alongside the colour and Z slopes for the RE.
 *   - Per fragment the TE divides out W, picks LOD from the derivatives,
 *     fetches and filters from TRAM, and hands the texel to the RE.
 *   - The texture environment is applied in the RE, not the TE: TXENV_RG
 *     0x142 and TXENV_B 0x143 sit among the RE's single-buffered registers,
 *     and TEXMODE1 0x111 carries the environment mode and component/LUT
 *     selection.
 *   - Fog follows (0x144/0x145), then PP1.
 *
 * First real trace (IRIX 6.5.22 libGLcore, 1 TRAM reported): one 8x8 RGBA8
 * texture, GL_NEAREST, GL_REPEAT, GL_REPLACE.
 *
 * Load:
 *   - 0x07C lists: TL_WBUFFER = 0, TEXRBUFFER = 0, TEXMODE1 = 0x40818,
 *     TL_MODE = 0x34E06, TL_SPEC = 0x330.
 *   - TL_SPEC = lod (3:0) | log2 S (7:4) | log2 T (11:8); the IDE's
 *     0x1641/0x2532/0x3423 for levels 1-3 suggest bits 15:12 are the level
 *     again or max LOD.
 *   - TL_MODE: components - 1 in 2:1 (3 = RGBA), bit 14 always set, 0x30000
 *     for RGBA8 in and out (the IDE's comment), 0xE00 not yet known.
 *   - SAVE_RSS (fill mode 0x01400000), INIT_PIXEL_STATE_ERAM and
 *     PIXEL_STATE blocks, XFRMODE = 0xC00080, then SEND_PIXELS with routine
 *     0x511A (glDrawPixels uses 0x49D0): one host-DMA line of 256 bytes, the
 *     texels exactly as the application gave them.
 *   - The ERAM block at 0xDA8 holds [0, 0, 8, 8, 0x80008, 0]: sub-image
 *     origin, size, and XFRSIZE (rows << 16 | columns).
 *   - The ERAM block at 0xDB1 holds the destination: [level width, ?,
 *     level, page | bank << 8, border page, pages]. The last word is the
 *     pages the texture manager gave the level (0 for levels sharing a
 *     page). It counts 64x64-texel tiles of 32 bits: 512x32 RGB8 gets 8
 *     pages, 512x256 32, 256x256 16, 128x128 RGBA4 2 (traced, SGI quake),
 *     so a page is 64 texels wide in TRAM. The border page follows the
 *     level's pages.
 *     For example, 64x64 mipmapped: level 0 [0x40, 0, 0, 0, 1, 1],
 *     level 1 [0x20, 0, 1, 0x102, 2, 1], level 2 [0x10, 0, 2, 3, 3, 0].
 *   - The block at 0xDB7: word 4 (0xDBB) is the host element the GE
 *     unpacks, from __glMgrExtractTypeInfo: 1 bytes; 3 16-bit (16-bit
 *     components, or packed 4-4-4-4 / 5-5-5-1); 4 32-bit (8-8-8-8,
 *     10-10-10-2, 32-bit components, depth). The component scale at 0xD0A
 *     tells packed from wide: 1/255 with element 3 is
 *     GL_UNSIGNED_SHORT_4_4_4_4_EXT (each nibble a component, R on top;
 *     SGI quake's lightmaps), 1/65535 is 16-bit components. 5-5-5-1 also
 *     sets a context flag not yet seen on the bus.
 *   - The host never writes XFRSIZE, XFRCONTROL, TL_MIPMAP or TL_S/T_SIZE
 *     itself: the GE microcode does, from those blocks.
 *   - Token 0x09B ("no texture-loader table") sits where a TL lookup table
 *     would be sent.
 *
 * Draw:
 *   - TEXMODE2 = 0x10000 (nearest, repeat), TEXMODE1 = 0x40819 (bit 0 is
 *     the texture enable), border colour 0.
 *   - TXADDR = 0, then 0x0EC writes TXMIPMAP = 0 (level 0 at page 0).
 *   - TXSIZE = 0x3333 (log2 sizes, four 4-bit fields), TXLOD = 1.
 *   - TXCLAMPSIZE = 0x10008 (the IDE wrote 0x40080 for a 128x64 load, so the
 *     fields are 12 bits wide; here T reads 16 for an 8-high texture, to be
 *     explained).
 *   - Tokens 0x00B and 0x00C bracket the textured primitives.
 *
 * Same trace with 4 TRAMs reported. Only three values change:
 *   - TEXMODE1 0x40818 -> 0x40858 (TRAM count 2 = 4 TRAMs);
 *   - TL_MODE 0x34E06 -> 0x34E26;
 *   - TEXMODE2 0x10000 -> 0x10004.
 * Component depth is TL_MODE bits 6:5 and TEXMODE2 bits 3:2:
 *   0 = 4 bits, 1 = 8 bits, 2 = 12 bits.
 * With 1 TRAM libGLcore stores RGBA8 at 4 bits (16 bpp), with 4 TRAMs at
 * 8 bits (32 bpp).
 *
 * Fields decoded from single-option variants (glprim --scene tex --tex*):
 *
 *   TEXMODE2:
 *     bits 3:2   component depth (above)
 *     bits 6:5   mipmap mode (01 nearest level, 11 blend two levels)
 *     bit 7      magnification linear
 *     bit 8      minification linear (within a level)
 *     bits 11,12 GL_CLAMP on S and T; bit 14 comes with them
 *     bit 16     always set
 *     bit 19     mipmapping on
 *   e.g. NEAREST 0x10004, LINEAR_MIPMAP_LINEAR + LINEAR 0x901E4,
 *   GL_CLAMP 0x15804.
 *
 *   TEXMODE1 (libGLcore mgrTM_SendTexmode1 agrees):
 *     bit 0       texture enable
 *     bits 2:1    environment mode: 0 modulate, 1 decal, 2 blend, 3 used
 *                 for alpha textures (alpha-only modulate)
 *     bits 4:3    components - 1
 *     bits 6:5    TRAM count
 *     bits 11:9   texel class: 4 RGB/RGBA, 2 luminance(-alpha), 1 alpha,
 *                 3 intensity
 *     bits 15:12  texture colour table mode
 *     bits 18:16  blend unit count (4, or 1)
 *     bit 19      colour-index visual
 *   GL_REPLACE does not reach the board: libGLcore (6.5.22,
 *   __glMgrim_TexEnvi) turns it into GL_MODULATE.
 *
 *   TL_MODE bits 2:1   components - 1.
 *
 *   TXENV_RG / TXENV_B: environment colour, 12 bits each
 *     (red 11:0 and green 23:12; blue 11:0).
 *
 *   TXLOD: the last mipmap level (1 without mipmaps).
 *
 *   TXCLAMPSIZE: T size in bits 11:0 and twice the S size from bit 12
 *     (8x8 0x10008, 16 wide x 4 high 0x20004, 4x16 0x8010).
 *
 *   TXSIZE: log2 S in bits 3:0, log2 T in 7:4, repeated in 11:8 / 15:12
 *     (16x4: 0x2424).
 *
 *   TEXMODE2 bit 16: the texture has no border, so clamped fetches read
 *     the border colour. A texture with a border clears it, and its
 *     TL_MODE changes 0x34E26 -> 0x35A26. distort's mipmapped, bordered
 *     texture also sets bit 13.
 *
 *   TXBCOLOR_RG / TXBCOLOR_BA: border colour, 12 bits each, laid out like
 *     the environment colour.
 *
 *   Detail textures (SGIS_detail_texture), from dettex traces on 6.5.22:
 *     TEXMODE2 bit 9    detail on (sent with bits 5, 6 and 19: the base
 *                       must be mipmapped)
 *     TEXMODE2 bit 10   detail image 128 x 128 (clear: 256 x 256); the
 *                       driver declines other sizes
 *     TXSIZE bits 18:16 6 + L, L = DETAIL_TEXTURE_LEVEL; L above -2 is sent
 *                       as -2 (D = 4)
 *     TXDETAIL 0x192    bits 7:0, TRAM page of the detail image
 *     DETAILSCALE 0x194 a TXADDR-indexed table of 32 five-bit weights
 *                       (31 = 31/32 on the board), entry k for
 *                       LOD = log2(k / 32)
 *     TXLOD bits 15:10  twice the last nonzero DETAILSCALE entry (0 when
 *                       the table is all zero)
 *   The detail image loads as a level twice as wide as high (TL_SPEC
 *   0x890 for 256, 0x780 for 128) of which only the left half is written,
 *   in the base texture's format. The driver builds the table from the
 *   GL detail function by putting each point at k = round(32 2^lod) and
 *   interpolating linearly in k (not in LOD), weights rounded to nearest
 *   with halves down (0.5 gives 15), held past the last point. The
 *   default table is 31 31 31 29 28 ... 2 1.
 *   ADD, MODULATE and the COLOR / ALPHA filters all send the same
 *   registers: the board has one detail operation on every component.
 *   Quake's surfaces: TEXMODE2 0x907e4, TXSIZE 0x2xxxx, TXLOD 0xf80n,
 *   TXDETAIL 0.
 *   Measured on a real High IMPACT (IP28, 6.5.22; dettex --fill/--func,
 *   test/gltest/dettexsweep.sh), not base + F (2 detail - 1):
 *     out = base + w (detail' - 2185) / 32, in 12-bit units, the texels
 *     widened to 12 bits by replication (16 d + d >> 4), rounded; w the
 *     table blend at k = 32 2^lambda (8-bit fraction between entries).
 *     Uniform base 64..192 against detail 0..255 and w 1..31: 161 of 165
 *     points exact, the rest one level off. The zero sits at detail 136.6
 *     of 255, the slope is half the extension's.
 *     No detail from the TXLOD index on: entries 15 and 16 at 31 with
 *     TXLOD 32 give full weight at k = 15.52, none at k = 16; TXLOD 62
 *     gives full weight at k = 30.1, none at 31.0.
 *     The detail coordinate is the level-0 one times 2^-L, no offset,
 *     bilinear; components in order.
 *     L = -2 (D = 4) is broken: vertical seams and a tear along the
 *     quad's triangle edge, at any base size.
 *
 *   Texture readback: GE token 0x0F0 [width, height, s offset, t offset
 *     (0.5, texel centres), transfer mode, 0x400D], then raster interface
 *     register 5 = 0x400D starts it (XFRCONTROL read | texture | DMA).
 *
 *   Texgen tokens: TEX_GENIV 0x05A [coord 0x2000 + i, 0x2500, mode],
 *     plane header 0x05B [coord, 0x2501 object / 0x2502 eye], plane data
 *     0x05C, enable 0x075 / disable 0x076 [GL_TEXTURE_GEN_S..Q]. The GE
 *     generates the coordinates; vertices still carry the current ones.
 *
 * Tokens:
 *   - 0x00B / 0x00C switch texturing on and off for the primitives that
 *     follow (0x0B00 / 0x0C00 written as floats by mgrTM_SendTexmode1).
 *   - TXMIPMAP (written with 0x0EC per level) holds page | bank << 8; the
 *     levels alternate banks (the trilinear pair).
 *   - TXBORDER holds a page per level for the level's border data.
 *
 * TRAM pages are 16 KB with 4 TRAMs (the kernel saves 256 pages of
 * 4096 * TRAMs bytes). libGLcore's placement, 128x128 RGBA8 mipmapped:
 *   level 0 at page 0, its border at 4;
 *   level 1 at 5 (bank 1), border 6;
 *   level 2 at 7, border 7;
 *   levels 3..7 all at page 8, banks alternating.
 * Levels larger than 16x16 start their own page; all levels of 16x16 and
 * smaller share one page (256x256 and 64x64 luminance place the same way).
 *
 * TEXMODE1 fields seen so far:
 *   - bit 0: texture enable;
 *   - bits 4:3: components - 1;
 *   - bits 6:5: TRAM count (0, 1, 2 = 1, 2, 4).
 *
 * TRAM count on the board: the kernel decodes board version byte 0 as RB
 * revision (6:4, 7 = no RB board), RB TRAMs (3:2) and RA TRAMs (1:0), each
 * as log2 of the TRAM count. Byte 1 is product (6:5) and GE count (1:0).
 *
 * Level tables: TXADDR 0x190 is an index, not a texel address. The driver
 * writes TXADDR = 0 and then streams one TXMIPMAP 0x191 value per level
 * (page | bank << 8) and one TXBORDER 0x193 value per level, through GE
 * token 0x0EC [shadow, ?, register, value]. So TE1 holds a small
 * per-level table of TRAM page bases.
 *
 * Detail texture (SGIS_detail_texture): single pass, inside the sampler.
 * TXDETAIL 0x192 places it; TXSIZE carries the detail-to-base size ratio;
 * TEXMODE2 has a mag_detail enable. DETAILSCALE 0x194 is written
 * repeatedly in a row (the IDE writes it nine times), so it looks like a
 * table loaded through one port: the detail function, weight against LOD.
 *
 * 3D textures: there is no R/W iterator anywhere in the register map
 * (0x080-0x0CF, mirrored 64-bit at 0x280-0x2CF), only TEXMODE2's
 * 3D-iterate enable and the R clamp/size fields. Unconfirmed guess: in 3D
 * mode the per-level table holds R slices, and the trilinear path blends
 * two adjacent slices instead of two LODs. Needs a 3D-texture trace.
 *
 * Context:
 *   - GE tokens 0x0E9 (4 words) and 0x0EA save and restore the texture mode
 *     around texture loads and reads.
 *   - The TRAM space is shared between processes. The kernel allocates it
 *     (MgrasAddTexSpace/MgrasDeleteTexSpace) and swaps pages out and back
 *     in with the transfers above.
 *
 * BORDERS AND CLAMPING
 *
 * Wrap modes in TEXMODE2 (libGLcore __glMgrim_TexParameter, confirmed by
 * traces):
 *   - bit 11 clamps S, bit 12 T, bit 13 R (distort's 0x879E4 clamps all
 *     three). Clear = GL_REPEAT.
 *   - bit 14 marks GL_CLAMP (OpenGL 1.0/1.1): coordinates held to [0, 1].
 *     Nearest filtering therefore always reads an edge texel (the "last
 *     texel replicated" behaviour). Linear filtering at the very edge mixes
 *     half the edge texel and half the border.
 *   - GL_CLAMP_TO_BORDER_SGIS (0x812D, SGIS_texture_border_clamp) sets the
 *     axis bit without bit 14. Coordinates may pass the edge, so samples
 *     there are pure border.
 *   - There is no GL_CLAMP_TO_EDGE: libGLcore does not offer
 *     SGIS_texture_edge_clamp, which OpenGL 1.2 later adopted. Pure edge
 *     replication under linear filtering needs a border holding copies of
 *     the edge texels.
 *
 * "The border" is one of two things:
 *   - Border texels: a texture specified with border = 1 (GL 1.0). Every
 *     border texel has its own value, not one colour per edge. The image
 *     loads from (-1, -1) (TL_MODE 0x35A26 rather than 0x34E26), and the
 *     border texels go to the level's border page (TXBORDER table, one
 *     page per level, reserved next to the level by libGLcore). TEXMODE2
 *     bit 16 clear says the texture has one.
 *   - Border colour: TEXMODE2 bit 16 set (no border texels); clamped
 *     fetches past the edge read TXBCOLOR_RG/BA (GL_TEXTURE_BORDER_COLOR,
 *     12 bits per component).
 *
 * Loader border modes (IDE mgras_hqpio_redma.c names TL_MODE fields; bit
 * positions unknown):
 *   - tlsoutbrd_en: store borders at the outer edge;
 *   - tlsoutrep_en: border replication (hardware edge extension while
 *     loading?);
 *   - tlsinbrd_en: use border data at the S boundary between the two texel
 *     groups;
 *   - tltbrd_en: border stored on top.
 *   The IDE border tests (mgras_tex_bordertall / borderwide, with expected
 *   images) exercise these; NOT MODELLED.
 *
 * OpenGL texture extensions in IMPACT's libGLcore (6.5.22, the extension
 * string of the textured configuration):
 *   EXT_texture        internal formats (sized formats, see the sweep)
 *   EXT_texture3D      3D textures (no perspective, no mipmaps on IMPACT)
 *   EXT_texture_object texture objects (glBindTexture)
 *   EXT_subtexture     glTexSubImage (32-texel rule above)
 *   EXT_copy_texture   glCopyTexImage / glCopyTexSubImage (through host)
 *   EXT_abgr           GL_ABGR_EXT pixels (the HQ formatter's swizzle)
 *   EXT_packed_pixels  packed pixel types
 *   SGIS_texture_border_clamp  GL_CLAMP_TO_BORDER_SGIS
 *   SGIS_texture_lod   base/max level and LOD clamps
 *   SGIS_texture_select   more texels in TRAM by selecting components
 *                          (TEXMODE1's TexReadSelect?)
 *   SGIS_texture_filter4  4x4 (bicubic-like) filter, done in software
 *                          (__glMgrNeedSoftFilter4)
 *   SGIS_detail_texture   additive detail texture (TXDETAIL, DETAILSCALE)
 *   SGI_texture_color_table   RE4's three 256-entry texture LUTs (shared
 *                             with fog)
 *   SGIX_texture_scale_bias   post-filter scale and bias (MAX_PIXEL_*?)
 *   SGIX_pixel_texture        pixel path through a 4D texture
 *   SGIX_texture_multi_buffer double-buffered texture loads (TL_WBUFFER /
 *                             TEXRBUFFER)
 * Modelled so far: EXT_texture (all sized formats),
 * EXT_texture_object, EXT_subtexture, EXT_copy_texture, EXT_abgr,
 * SGIS_texture_border_clamp. The rest are not yet.
 *
 * Sub-images and copies (traced, IRIX 6.5.22):
 *   - libGLcore rejects glTexSubImage2D and glCopyTexSubImage2D with
 *     GL_INVALID_VALUE unless the sub-image covers the level's full width,
 *     or is at least 32 texels wide and ends on a 32-texel boundary or at
 *     the level's edge (__glMgrim_TexSubImage2DEXT). This is real IMPACT
 *     behaviour, not an emulation fault.
 *   - A sub-image loads like a level: the same 0x511A SEND_PIXELS, with
 *     ERAM block 0xDA8's origin and size.
 *   - glCopyTexImage2D reads the screen to host memory (GET_PIXELS,
 *     3-byte RGB, transfer mode 0xC00070), then loads it as a texture
 *     image. There is no on-board screen-to-TRAM path in libGLcore.
 *   - glGetTexImage transfer modes per internal format: format 9 for one
 *     component, 0xA for two, 7 for RGB, 8 for RGBA; type 1 for 16-bit
 *     components (12-bit textures). Raster interface register 2 is bytes
 *     per row. Two 8-bit components (LA8) travel as one 16-bit unit, alpha
 *     in the high byte; LA12 comes L, A.
 *   - Sized internal formats (libGLcore 6.5.22, 4 TRAMs):
 *       *4, R3_G3_B2, RGBA2                 -> 4-bit components
 *       *8, RGB5, RGB5_A1, L6A2              -> 8-bit
 *       RGB10, RGB10_A2, *12, *16, L12A4     -> 12-bit
 *     12-bit textures set TEXMODE1's blend unit count (18:16) to 1
 *     instead of 4: fewer 12-bit blenders in RE4.
 *   - IMPACT has no depth textures (no SGIX_depth_texture). Depth moves
 *     only with glReadPixels / glCopyPixels.
 *
 * From the OCTANE Technical Report, section 4 (ignore/gr4/docs):
 *   - TE1 iterates spans of texture coordinates on its own, in step with
 *     RE4's spans (shared set-up), so neither blocks the other. It does
 *     the perspective divide and LOD, and sends addresses to the TRAM.
 *   - The TRAM ASIC fetches and filters (point, bilinear, trilinear: 8
 *     texels a pixel) and returns up to two filtered texels per clock to
 *     RE4. Trilinear mipmapping is the fastest mode; point or bilinear
 *     sampling under minification can be slower (TRAM caching).
 *   - RE4 does the texture environment, fog and alpha blending. 12-bit
 *     components use fewer, wider blenders, so they are slower, and fog
 *     shares the blenders.
 *   - RE4 has three 256-entry, 8-bit texture colour tables applied after
 *     filtering, to luminance or RGB textures (contrast adjustment). They
 *     are shared with the fog lookup table, so they cannot be used with
 *     fog at the same time. Fog is computed per pixel with 8 bits through
 *     that table, so the fog factor is probably looked up per pixel from
 *     an iterated value. NOT MODELLED: the tables, and fog through them
 *     (the model fogs from a GE-computed plane).
 *   - Textures have 1-4 components of 4, 8 or 12 bits (texels up to 48
 *     bits). The 4 MB TRAM is needed for 3- and 4-component 8-bit texels
 *     and 2-, 3- and 4-component 12-bit texels.
 *   - 1D and 2D textures are perspective-corrected and mipmapped. 3D
 *     (volume) textures are neither, which fits the missing R/W
 *     iterator.
 *   - Detail texturing is additive.
 *   - Pixel textures (SGIX): the pixel path looks up a 4D texture with
 *     RGBA as STRQ.
 *   - TRAM is banked so that sampling and loading of different textures
 *     can overlap. 60 Mtexels/s load; 12-bit textures other than
 *     luminance cannot be sampled and loaded at the same time.
 *
 * TEXTURE SELECT (SGIS_texture_select)
 *
 * TRAM is addressed in cells of four components, each 4, 8 or 12 bits
 * deep. A cell takes the same room whatever the texture's own component
 * count: RGB8, LA8 and L8 cells are all 32 bits, a 16 KB page holds 4096
 * cells (8192 at 4 bits). libGLcore's page counts say so: a 256x256 L8
 * texture gets 16 pages, the same as RGBA8.
 *
 * Luminance, alpha and intensity textures (one component) and
 * luminance-alpha textures (two) would leave most of each cell empty, so
 * libGLcore packs several into the same pages, each in its own select
 * slot:
 *   - one component: four slots, cell component 0, 1, 2 or 3;
 *   - two components: two slots, cell components 0-1 or 2-3;
 *   - three or four components: the texture fills the cell; the select
 *     bits mean nothing (TyrQuake draws RGBA textures with TEXMODE1
 *     0x409D9, bits 8:7 = 3).
 * The slot is TL_MODE bits 4:3 when the texture is loaded and TEXMODE1
 * bits 8:7 when it is sampled; RestoreData and DoSaveData set the same
 * per-texture field in both. The texture's components are then read from
 * (or written to) those cell components and returned as the texture's
 * own (luminance in the first, as for an unpacked texture).
 *
 * Loading needs no masking anywhere upstream: the host DMA carries only
 * the texture's own texels (one byte each for L8), and the loader writes
 * just the selected cell components, leaving the textures in the other
 * slots alone. A texture is reloaded on its own, with its own slot.
 *
 * Traced (TyrQuake, which re-uploads whole 256x256 GL_LUMINANCE lightmaps
 * with glTexImage2D): four lightmaps share pages 223-238 (TL_MIPMAP 0xDF),
 * TL_MODE 0x34020, 0x34028, 0x34030, 0x34038 for slots 0-3, each upload
 * 64x256 chunks of 16384 bytes; drawn with TEXMODE1 0x40, 0xC0, 0x140,
 * 0x1C0 (bits 8:7 = 0-3).
 *
 * Why four-component cells (inference, no wiring documentation): with 4
 * TRAMs (4 RAMBUS devices), one component per device, interleaved, a cell
 * is one access that delivers a whole RGBA texel at four times one
 * device's bandwidth, and a select slot is simply one device. That fits:
 *   - the page counts above (cells, not bytes);
 *   - two slots for two components (two devices each), none for RGB/RGBA;
 *   - the Octane report's 1 MB (single-TRAM) option being limited to
 *     16-bit textures: one device delivers about one 16-bit texel an
 *     access;
 *   - its note that 12-bit textures other than luminance cannot be
 *     sampled and loaded at the same time: their components need more than
 *     one access per texel, so loading competes with sampling.
 *
 * TEXTURE MANAGER SAVE AND RESTORE
 *
 * libGLcore's texture manager treats TRAM as a cache. When it needs room
 * it saves pages to a host "save arena" and later restores them, possibly
 * elsewhere (mgrTM_SR_TexRead / mgrTM_SR_TexLoad); textures it moves never
 * pass through the ordinary load again. The pages travel raw, so any
 * TRAM layout survives as long as save and restore copy bytes 1:1.
 *   - Save: TXSIZE = 0xD6D6 (64 x 8192), TXMIPMAP[0] = the first page,
 *     TEXMODE1 = 0x58 (four components), then token 0x0E5 [rows,
 *     XFRMODE 0x400080, 0x400D]: a TE read of rows of 64 32-bit texels.
 *     The host arms a board-to-host DMA (one line of rows * 256 bytes) and
 *     starts the read with raster interface register 5 = 0x400D.
 *   - Restore: TL_MODE = 0x4026 (four 8-bit components, no format
 *     conversion bits), TL_SPEC = 0xD60 (64 x 8192), TL_MIPMAP = the page
 *     (0x0EC), 0xDA8 = [0, first row, 64, rows, rows << 16 | 64], 0xDB1 =
 *     [0, 0] (no destination level), then 0x09D _WRITE_DMAGESETUP [0, n,
 *     0, 0x511A] and a list-mode host DMA (see HQ3.h) of up to 32 KB.
 *   - Large textures that do not stay resident are staged at the top of
 *     TRAM, ending at page 255 (the console background at 223 with 32
 *     pages, a 256x256 at 239 with 16, lightmaps at 253 with 2), and
 *     reloaded each time they are bound.
 *   - A slot texture is saved through its slot: TXSIZE 0xD6D6, TEXMODE1
 *     with the slot in bits 8:7 (0x140 = slot 2), XFRMODE 0x400090 (one
 *     8-bit component), 0x400 rows; reloaded with TL_MODE's slot bits
 *     (0x34038 = slot 3) and TL_NO_TABLE (0x09B).
 *
 * TyrQuake after an overnight demo loop (glq.log, 2026-10-10; cause still
 * open, libGLcore bookkeeping or emulation). Its water/lava warp is
 * rebuilt every frame: two GET_PIXELS_BITMAP_PIO reads of the screen, then
 * a TL load (TL_MODE 0x34024 RGB, TL_SPEC 0x880, TL_MIPMAP 0x28) in four
 * 64 x 256 strips (0xDA8 at s = 0, 0x40, 0x80, 0xC0), sampled as TXSIZE
 * 0x8888 from page 0x28, which takes 16 pages (0x28-0x37). In the same
 * frames about 20 wall textures (TXSIZE 0x4444-0x7878) were bound to pages
 * inside 0x28-0x37, so every frame the warp overwrote them: walls showed
 * the lava. A fresh run keeps the warp's pages to itself. The overlap is
 * libGLcore's own placement; the manager was also restoring about six
 * pages a frame (TRAM overcommitted). Real hardware behaviour unknown.
 *
 * CAUTION: the register bitfield layouts (the *u unions) are not in the
 * sources we have. The TEXMODE2/TXSIZE/TXLOD bit positions below are
 * guesses; mgras_const.h's TEXMODE2_* defines are field values, not bit
 * numbers. Values seen in practice:
 *   - TEXMODE2 = 0x11800 | (compdepth + 1) << 2 | db_enable << 4
 *   - TXSIZE  = 0xD6D6 for a whole-page read
 */

/*
 * ============================================================================
 * TE1 REGISTER BYTE OFFSETS (Within RSS Space 0x7C000 - 0x7C3FF)
 * ============================================================================
 */

/* Double-Buffered Texture Coordinate Registers (0x0080 - 0x00CB) */
/* Non-Warp Mode (WARP_ENABLE = 0: Perspective Texture Mapping) */
#define TE1_REG_SW_U                0x0080      /* S/W coordinate high 16 bits */
#define TE1_REG_SW_L                0x0081      /* S/W coordinate low 16 bits */
#define TE1_REG_TW_DTX_U            0x0082      /* T/W coordinate / dS/dX high */
#define TE1_REG_TW_DTX_L            0x0083      /* T/W coordinate / dS/dX low */
#define TE1_REG_WI_U                0x0084      /* 1/W coordinate high */
#define TE1_REG_WI_L                0x0085      /* 1/W coordinate low */
#define TE1_REG_DWIE_U              0x0086      /* d(1/W)/dEdge high */
#define TE1_REG_DWIE_L              0x0087      /* d(1/W)/dEdge low */
#define TE1_REG_DWIX_U              0x0088      /* d(1/W)/dX high */
#define TE1_REG_DWIX_L              0x0089      /* d(1/W)/dX low */
#define TE1_REG_DWIY_U              0x008A      /* d(1/W)/dY high */
#define TE1_REG_DWIY_L              0x008B      /* d(1/W)/dY low */
#define TE1_REG_TXSCALE             0x008C      /* Texture scale factor */

#define TE1_REG_DSWE_U              0x00C0      /* d(S/W)/dEdge high */
#define TE1_REG_DSWE_L              0x00C1      /* d(S/W)/dEdge low */
#define TE1_REG_DTWE_U              0x00C2      /* d(T/W)/dEdge high */
#define TE1_REG_DTWE_L              0x00C3      /* d(T/W)/dEdge low */
#define TE1_REG_DSWX_U              0x00C4      /* d(S/W)/dX high */
#define TE1_REG_DSWX_L              0x00C5      /* d(S/W)/dX low */
#define TE1_REG_DTWX_U              0x00C6      /* d(T/W)/dX high */
#define TE1_REG_DTWX_L              0x00C7      /* d(T/W)/dX low */
#define TE1_REG_DSWY_U              0x00C8      /* d(S/W)/dY high */
#define TE1_REG_DSWY_L              0x00C9      /* d(S/W)/dY low */
#define TE1_REG_DTWY_U              0x00CA      /* d(T/W)/dY high */
#define TE1_REG_DTWY_L              0x00CB      /* d(T/W)/dY low */

/* Warp Mode (WARP_ENABLE = 1: Non-Perspective Image Warping) */
#define TE1_REG_DSX_U               0x0080
#define TE1_REG_DSX_L               0x0081
#define TE1_REG_DTX_U               0x0082
#define TE1_REG_DTX_L               0x0083
#define TE1_REG_SWARP_U             0x0084
#define TE1_REG_SWARP_L             0x0085
#define TE1_REG_DSE_U               0x0086
#define TE1_REG_DSE_L               0x0087
#define TE1_REG_DTE_U               0x0088
#define TE1_REG_DTE_L               0x0089
#define TE1_REG_TWARP_U             0x008A
#define TE1_REG_TWARP_L             0x008B

/* Single-Buffered Control Registers (0x0180 - 0x01AF) */
#define TE1_REG_TEXMODE2            0x0180      /* Texture mode 2 (format & dimensions) */
#define TE1_REG_TXSIZE              0x0181      /* Texture width & height powers of two */
#define TE1_REG_TXTILE              0x0182      /* Texture tile & page layout */
#define TE1_REG_TXLOD               0x0183      /* Level-of-Detail (LOD) control & bias */
#define TE1_REG_TXBCOLOR_RG         0x0184      /* Texture border color Red/Green */
#define TE1_REG_TXBCOLOR_BA         0x0185      /* Texture border color Blue/Alpha */
#define TE1_REG_TXBASE              0x0186      /* Texture base address in TRAM */
#define TE1_REG_TRAM_CNTRL          0x0187      /* TRAM chip timing & refresh */
#define TE1_REG_MAX_PIXEL_RG        0x0188      /* Maximum pixel clamp Red/Green */
#define TE1_REG_MAX_PIXEL_BA        0x0189      /* Maximum pixel clamp Blue/Alpha */
#define TE1_REG_MDDMA_CNTRL         0x018A      /* Memory-to-Device DMA control */
#define TE1_REG_TEXRBUFFER          0x018B      /* Texture readback buffer */
#define TE1_REG_TXCLAMPSIZE         0x018C      /* Clamp size boundaries */
#define TE1_REG_TXADDR              0x0190      /* Texture memory address pointer */
#define TE1_REG_TXMIPMAP            0x0191      /* Mipmap level addresses & offsets */
#define TE1_REG_TXDETAIL            0x0192      /* Detail texture control */
#define TE1_REG_TXBORDER            0x0193      /* Border mode control */
#define TE1_REG_DETAILSCALE         0x0194      /* Detail texture scale factor */
#define TE1_REG_TE_TOGGLECNTX       0x0195      /* Context toggle register */
#define TE1_REG_TEVERSION           0x0196      /* Silicon revision */

/* Texture Loader & Video Texture Registers (0x01A0 - 0x01AC) */
#define TE1_REG_TL_WBUFFER          0x01A0      /* Texture loader write buffer */
#define TE1_REG_TL_MODE             0x01A1      /* Texture loader mode */
#define TE1_REG_TL_SPEC             0x01A2      /* Texture loader format spec */
#define TE1_REG_TL_ADDR             0x01A3      /* Texture loader start address */
#define TE1_REG_TL_MIPMAP           0x01A4      /* Texture loader mipmap flags */
#define TE1_REG_TL_BORDER           0x01A5      /* Texture loader border width */
#define TE1_REG_TL_VIDCNTRL         0x01A6      /* Video texture capture control */
#define TE1_REG_TL_BASE             0x01A7      /* Video texture base address */
#define TE1_REG_TL_VIDABORT         0x01A8      /* Video texture capture abort */
#define TE1_REG_TL_S_SIZE           0x01A9      /* Video texture S size */
#define TE1_REG_TL_T_SIZE           0x01AA      /* Video texture T size */
#define TE1_REG_TL_S_LEFT           0x01AB      /* Video texture crop left */
#define TE1_REG_TL_T_BOTTOM         0x01AC      /* Video texture crop bottom */

/*
 * ============================================================================
 * REGISTER BIT DEFINITIONS & ENUMS
 * ============================================================================
 */

/* Texture Mode 2 Register (TE1_REG_TEXMODE2: 0x0180) */
#define TE1_TM2_3DITER_EN           BIT(0)      /* Enable 3D coordinate iteration */
#define TE1_TM2_WARP_ENABLE         BIT(1)      /* 1 = Warp mode, 0 = Non-warp mode */
#define TE1_TM2_S_CLAMP             BIT(2)      /* S coordinate clamp to [0, 1] */
#define TE1_TM2_T_CLAMP             BIT(3)      /* T coordinate clamp to [0, 1] */
#define TE1_TM2_R_CLAMP             BIT(4)      /* R coordinate clamp to [0, 1] */
#define TE1_TM2_S_WRAP              0           /* S coordinate repeat / wrap */
#define TE1_TM2_T_WRAP              0           /* T coordinate repeat / wrap */
#define TE1_TM2_R_WRAP              0           /* R coordinate repeat / wrap */
#define TE1_TM2_RACP                BIT(5)
#define TE1_TM2_SACP                BIT(6)
#define TE1_TM2_TACP                BIT(7)

/* Texture Dimensions (bits 9:8) */
#define TE1_TM2_DIM_1D              (0 << 8)
#define TE1_TM2_DIM_2D              (1 << 8)
#define TE1_TM2_DIM_3D              (2 << 8)

/* Texture Formats */
#define TE1_FMT_RGBA8               0           /* 32-bit RGBA (8:8:8:8) */
#define TE1_FMT_RGBA12              1           /* 48-bit RGBA (12:12:12:12) */
#define TE1_FMT_RGBA16              2           /* 64-bit RGBA (16:16:16:16) */
#define TE1_FMT_ABGR8               3           /* 32-bit ABGR */
#define TE1_FMT_RGBA10              4           /* 32-bit RGB10A2 */
#define TE1_FMT_SHORT               5           /* 16-bit Luminance / Intensity */
#define TE1_FMT_DCB                 6

/* Texture Size Powers of Two (TE1_REG_TXSIZE: 0x0181) */
#define TE1_TXSIZE_S(log2s)         (((log2s) & 0xF) << 0)
#define TE1_TXSIZE_T(log2t)         (((log2t) & 0xF) << 4)
#define TE1_TXSIZE_R(log2r)         (((log2r) & 0xF) << 8)

/* Level-of-Detail & Filtering (TE1_REG_TXLOD: 0x0183) */
#define TE1_LOD_MAG_NEAREST         (0 << 0)    /* Point sampling on magnification */
#define TE1_LOD_MAG_LINEAR          (1 << 0)    /* Bilinear filtering on magnification */
#define TE1_LOD_MIN_NEAREST         (0 << 1)    /* Point sampling on minification */
#define TE1_LOD_MIN_LINEAR          (1 << 1)    /* Bilinear filtering */
#define TE1_LOD_MIN_NEAREST_MIP_NEAR (2 << 1)   /* Nearest texel, nearest mipmap */
#define TE1_LOD_MIN_LINEAR_MIP_NEAR (3 << 1)    /* Bilinear texel, nearest mipmap */
#define TE1_LOD_MIN_NEAREST_MIP_LIN (4 << 1)    /* Nearest texel, blend mipmaps */
#define TE1_LOD_MIN_TRILINEAR       (5 << 1)    /* True trilinear mipmapped filtering */

/* Silicon Revisions (TE1_REG_TEVERSION: 0x0196) */
#define TE_REV_UNDETERMINED         0
#define TE_REV_B                    1           /* Revision B (Initial Impact) */
#define TE_REV_D                    2           /* Revision D (Octane & late Indigo2) */

#define TE_REV_D_CHECKSUM           0x2AA70D210ULL
#define TE_REV_B_1TRAM_CHECKSUM     0x0BDF48FCULL
#define TE_REV_B_4TRAM_CHECKSUM     0x236379000ULL

#ifdef __cplusplus
}
#endif

#endif /* _SGI_TE1_H_ */
