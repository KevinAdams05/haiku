/*
 * Copyright 2010 Advanced Micro Devices, Inc.
 * Copyright 2026 Kevin Adams <kevinadams05@gmail.com>.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Alex Deucher
 */
#ifndef __NI_REG_H__
#define __NI_REG_H__

/* northern islands - DCE5 */

#define NI_INPUT_GAMMA_CONTROL                         0x6840
#       define NI_GRPH_INPUT_GAMMA_MODE(x)             (((x) & 0x3) << 0)
#       define NI_INPUT_GAMMA_USE_LUT                  0
#       define NI_INPUT_GAMMA_BYPASS                   1
#       define NI_INPUT_GAMMA_SRGB_24                  2
#       define NI_INPUT_GAMMA_XVYCC_222                3
#       define NI_OVL_INPUT_GAMMA_MODE(x)              (((x) & 0x3) << 4)

#define NI_PRESCALE_GRPH_CONTROL                       0x68b4
#       define NI_GRPH_PRESCALE_BYPASS                 (1 << 4)

#define NI_PRESCALE_OVL_CONTROL                        0x68c4
#       define NI_OVL_PRESCALE_BYPASS                  (1 << 4)

#define NI_INPUT_CSC_CONTROL                           0x68d4
#       define NI_INPUT_CSC_GRPH_MODE(x)               (((x) & 0x3) << 0)
#       define NI_INPUT_CSC_BYPASS                     0
#       define NI_INPUT_CSC_PROG_COEFF                 1
#       define NI_INPUT_CSC_PROG_SHARED_MATRIXA        2
#       define NI_INPUT_CSC_OVL_MODE(x)                (((x) & 0x3) << 4)

#define NI_OUTPUT_CSC_CONTROL                          0x68f0
#       define NI_OUTPUT_CSC_GRPH_MODE(x)              (((x) & 0x7) << 0)
#       define NI_OUTPUT_CSC_BYPASS                    0
#       define NI_OUTPUT_CSC_TV_RGB                    1
#       define NI_OUTPUT_CSC_YCBCR_601                 2
#       define NI_OUTPUT_CSC_YCBCR_709                 3
#       define NI_OUTPUT_CSC_PROG_COEFF                4
#       define NI_OUTPUT_CSC_PROG_SHARED_MATRIXB       5
#       define NI_OUTPUT_CSC_OVL_MODE(x)               (((x) & 0x7) << 4)

#define NI_DEGAMMA_CONTROL                             0x6960
#       define NI_GRPH_DEGAMMA_MODE(x)                 (((x) & 0x3) << 0)
#       define NI_DEGAMMA_BYPASS                       0
#       define NI_DEGAMMA_SRGB_24                      1
#       define NI_DEGAMMA_XVYCC_222                    2
#       define NI_OVL_DEGAMMA_MODE(x)                  (((x) & 0x3) << 4)
#       define NI_ICON_DEGAMMA_MODE(x)                 (((x) & 0x3) << 8)
#       define NI_CURSOR_DEGAMMA_MODE(x)               (((x) & 0x3) << 12)

#define NI_GAMUT_REMAP_CONTROL                         0x6964
#       define NI_GRPH_GAMUT_REMAP_MODE(x)             (((x) & 0x3) << 0)
#       define NI_GAMUT_REMAP_BYPASS                   0
#       define NI_GAMUT_REMAP_PROG_COEFF               1
#       define NI_GAMUT_REMAP_PROG_SHARED_MATRIXA      2
#       define NI_GAMUT_REMAP_PROG_SHARED_MATRIXB      3
#       define NI_OVL_GAMUT_REMAP_MODE(x)              (((x) & 0x3) << 4)

#define NI_REGAMMA_CONTROL                             0x6a80
#       define NI_GRPH_REGAMMA_MODE(x)                 (((x) & 0x7) << 0)
#       define NI_REGAMMA_BYPASS                       0
#       define NI_REGAMMA_SRGB_24                      1
#       define NI_REGAMMA_XVYCC_222                    2
#       define NI_REGAMMA_PROG_A                       3
#       define NI_REGAMMA_PROG_B                       4
#       define NI_OVL_REGAMMA_MODE(x)                  (((x) & 0x7) << 4)


/* DCE 5+ DPG pipe arbitration / latency — per-CRTC, uses the standard
 * EVERGREEN_CRTCn_REGISTER_OFFSET stride. Functionally equivalent to the
 * DCE 4 PIPE_ARBITRATION_CONTROL3 / PIPE_LATENCY_CONTROL pair, but at
 * different addresses and a different stride. SI uses identical offsets
 * (SI_DPG_PIPE_* in si_reg.h); CIK as well. */
#define NI_DPG_PIPE_ARBITRATION_CONTROL3                0x6cc8
#       define NI_LATENCY_WATERMARK_MASK(x)             (((x) & 0x3) << 16)
#define NI_DPG_PIPE_LATENCY_CONTROL                     0x6ccc
#       define NI_LATENCY_LOW_WATERMARK(x)              (((x) & 0xffff) << 0)
#       define NI_LATENCY_HIGH_WATERMARK(x)             (((x) & 0xffff) << 16)

/* DMIF buffer allocation poll (DCE 5+, per CRTC stride). On DCE 4 the
 * equivalent register lives at EVERGREEN_PIPE0_DMIF_BUFFER_CONTROL. */
#define NI_DPG_PIPE_DMIF_BUFFER_CONTROL                 0x0ca0
#       define NI_DMIF_BUFFERS_ALLOCATED(x)             (((x) & 0xf) << 0)
#       define NI_DMIF_BUFFERS_ALLOCATED_COMPLETED      (1 << 4)

#endif
