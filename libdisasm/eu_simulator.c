/*
 * Copyright © 2015 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <stdarg.h>
#include <math.h>
#include <assert.h>
#include <immintrin.h>

#include "gen_disasm.h"
#include "../ksim.h"

#define BRW_HW_REG_TYPE_UD  0
#define BRW_HW_REG_TYPE_D   1
#define BRW_HW_REG_TYPE_UW  2
#define BRW_HW_REG_TYPE_W   3
#define BRW_HW_REG_TYPE_F   7
#define GEN8_HW_REG_TYPE_UQ 8
#define GEN8_HW_REG_TYPE_Q  9

#define BRW_HW_REG_NON_IMM_TYPE_UB  4
#define BRW_HW_REG_NON_IMM_TYPE_B   5
#define GEN7_HW_REG_NON_IMM_TYPE_DF 6
#define GEN8_HW_REG_NON_IMM_TYPE_HF 10

#define BRW_HW_REG_IMM_TYPE_UV  4 /* Gen6+ packed unsigned immediate vector */
#define BRW_HW_REG_IMM_TYPE_VF  5 /* packed float immediate vector */
#define BRW_HW_REG_IMM_TYPE_V   6 /* packed int imm. vector; uword dest only */
#define GEN8_HW_REG_IMM_TYPE_DF 10
#define GEN8_HW_REG_IMM_TYPE_HF 11

enum opcode {
   /* These are the actual hardware opcodes. */
   BRW_OPCODE_MOV =	1,
   BRW_OPCODE_SEL =	2,
   BRW_OPCODE_NOT =	4,
   BRW_OPCODE_AND =	5,
   BRW_OPCODE_OR =	6,
   BRW_OPCODE_XOR =	7,
   BRW_OPCODE_SHR =	8,
   BRW_OPCODE_SHL =	9,
   BRW_OPCODE_ASR =	12,
   BRW_OPCODE_CMP =	16,
   BRW_OPCODE_CMPN =	17,
   BRW_OPCODE_CSEL =	18,  /**< Gen8+ */
   BRW_OPCODE_F32TO16 = 19,  /**< Gen7 only */
   BRW_OPCODE_F16TO32 = 20,  /**< Gen7 only */
   BRW_OPCODE_BFREV =	23,  /**< Gen7+ */
   BRW_OPCODE_BFE =	24,  /**< Gen7+ */
   BRW_OPCODE_BFI1 =	25,  /**< Gen7+ */
   BRW_OPCODE_BFI2 =	26,  /**< Gen7+ */
   BRW_OPCODE_JMPI =	32,
   BRW_OPCODE_IF =	34,
   BRW_OPCODE_IFF =	35,  /**< Pre-Gen6 */
   BRW_OPCODE_ELSE =	36,
   BRW_OPCODE_ENDIF =	37,
   BRW_OPCODE_DO =	38,
   BRW_OPCODE_WHILE =	39,
   BRW_OPCODE_BREAK =	40,
   BRW_OPCODE_CONTINUE = 41,
   BRW_OPCODE_HALT =	42,
   BRW_OPCODE_MSAVE =	44,  /**< Pre-Gen6 */
   BRW_OPCODE_MRESTORE = 45, /**< Pre-Gen6 */
   BRW_OPCODE_PUSH =	46,  /**< Pre-Gen6 */
   BRW_OPCODE_GOTO =	46,  /**< Gen8+    */
   BRW_OPCODE_POP =	47,  /**< Pre-Gen6 */
   BRW_OPCODE_WAIT =	48,
   BRW_OPCODE_SEND =	49,
   BRW_OPCODE_SENDC =	50,
   BRW_OPCODE_MATH =	56,  /**< Gen6+ */
   BRW_OPCODE_ADD =	64,
   BRW_OPCODE_MUL =	65,
   BRW_OPCODE_AVG =	66,
   BRW_OPCODE_FRC =	67,
   BRW_OPCODE_RNDU =	68,
   BRW_OPCODE_RNDD =	69,
   BRW_OPCODE_RNDE =	70,
   BRW_OPCODE_RNDZ =	71,
   BRW_OPCODE_MAC =	72,
   BRW_OPCODE_MACH =	73,
   BRW_OPCODE_LZD =	74,
   BRW_OPCODE_FBH =	75,  /**< Gen7+ */
   BRW_OPCODE_FBL =	76,  /**< Gen7+ */
   BRW_OPCODE_CBIT =	77,  /**< Gen7+ */
   BRW_OPCODE_ADDC =	78,  /**< Gen7+ */
   BRW_OPCODE_SUBB =	79,  /**< Gen7+ */
   BRW_OPCODE_SAD2 =	80,
   BRW_OPCODE_SADA2 =	81,
   BRW_OPCODE_DP4 =	84,
   BRW_OPCODE_DPH =	85,
   BRW_OPCODE_DP3 =	86,
   BRW_OPCODE_DP2 =	87,
   BRW_OPCODE_LINE =	89,
   BRW_OPCODE_PLN =	90,  /**< G45+ */
   BRW_OPCODE_MAD =	91,  /**< Gen6+ */
   BRW_OPCODE_LRP =	92,  /**< Gen6+ */
   BRW_OPCODE_NENOP =	125, /**< G45 only */
   BRW_OPCODE_NOP =	126,
};

enum brw_message_target {
   BRW_SFID_NULL                     = 0,
   BRW_SFID_MATH                     = 1, /* Only valid on Gen4-5 */
   BRW_SFID_SAMPLER                  = 2,
   BRW_SFID_MESSAGE_GATEWAY          = 3,
   BRW_SFID_DATAPORT_READ            = 4,
   BRW_SFID_DATAPORT_WRITE           = 5,
   BRW_SFID_URB                      = 6,
   BRW_SFID_THREAD_SPAWNER           = 7,
   BRW_SFID_VME                      = 8,

   GEN6_SFID_DATAPORT_SAMPLER_CACHE  = 4,
   GEN6_SFID_DATAPORT_RENDER_CACHE   = 5,
   GEN6_SFID_DATAPORT_CONSTANT_CACHE = 9,

   GEN7_SFID_DATAPORT_DATA_CACHE     = 10,
   GEN7_SFID_PIXEL_INTERPOLATOR      = 11,
   HSW_SFID_DATAPORT_DATA_CACHE_1    = 12,
   HSW_SFID_CRE                      = 13,
};

#define BRW_MATH_FUNCTION_INV                              1
#define BRW_MATH_FUNCTION_LOG                              2
#define BRW_MATH_FUNCTION_EXP                              3
#define BRW_MATH_FUNCTION_SQRT                             4
#define BRW_MATH_FUNCTION_RSQ                              5
#define BRW_MATH_FUNCTION_SIN                              6
#define BRW_MATH_FUNCTION_COS                              7
#define BRW_MATH_FUNCTION_SINCOS                           8 /* gen4, gen5 */
#define BRW_MATH_FUNCTION_FDIV                             9 /* gen6+ */
#define BRW_MATH_FUNCTION_POW                              10
#define BRW_MATH_FUNCTION_INT_DIV_QUOTIENT_AND_REMAINDER   11
#define BRW_MATH_FUNCTION_INT_DIV_QUOTIENT                 12
#define BRW_MATH_FUNCTION_INT_DIV_REMAINDER                13
#define GEN8_MATH_FUNCTION_INVM                            14
#define GEN8_MATH_FUNCTION_RSQRTM                          15

#define BRW_ARCHITECTURE_REGISTER_FILE    0
#define BRW_GENERAL_REGISTER_FILE         1
#define BRW_MESSAGE_REGISTER_FILE         2
#define BRW_IMMEDIATE_VALUE               3

#define BRW_ARF_NULL                  0x00
#define BRW_ARF_ADDRESS               0x10
#define BRW_ARF_ACCUMULATOR           0x20
#define BRW_ARF_FLAG                  0x30
#define BRW_ARF_MASK                  0x40
#define BRW_ARF_MASK_STACK            0x50
#define BRW_ARF_MASK_STACK_DEPTH      0x60
#define BRW_ARF_STATE                 0x70
#define BRW_ARF_CONTROL               0x80
#define BRW_ARF_NOTIFICATION_COUNT    0x90
#define BRW_ARF_IP                    0xA0
#define BRW_ARF_TDR                   0xB0
#define BRW_ARF_TIMESTAMP             0xC0

#define BRW_ALIGN_1   0
#define BRW_ALIGN_16  1

#define BRW_ADDRESS_DIRECT                        0
#define BRW_ADDRESS_REGISTER_INDIRECT_REGISTER    1

#define BRW_3SRC_TYPE_F  0
#define BRW_3SRC_TYPE_D  1
#define BRW_3SRC_TYPE_UD 2
#define BRW_3SRC_TYPE_DF 3

enum {
   BRW_CONDITIONAL_NONE = 0,
   BRW_CONDITIONAL_Z    = 1,
   BRW_CONDITIONAL_NZ   = 2,
   BRW_CONDITIONAL_EQ   = 1,	/* Z */
   BRW_CONDITIONAL_NEQ  = 2,	/* NZ */
   BRW_CONDITIONAL_G    = 3,
   BRW_CONDITIONAL_GE   = 4,
   BRW_CONDITIONAL_L    = 5,
   BRW_CONDITIONAL_LE   = 6,
   BRW_CONDITIONAL_R    = 7,    /* Gen <= 5 */
   BRW_CONDITIONAL_O    = 8,
   BRW_CONDITIONAL_U    = 9,
};

struct inst_common {
   uint32_t opcode;
   uint32_t access_mode;
   uint32_t no_dd_clear;
   uint32_t no_dd_check;
   uint32_t nib_control;
   uint32_t qtr_control;
   uint32_t thread_control;
   uint32_t pred_control;
   uint32_t pred_inv;
   uint32_t exec_size;
   uint32_t math_function;
   uint32_t cond_modifier;
   uint32_t acc_wr_control;
   uint32_t branch_control;
   uint32_t cmpt_control;
   uint32_t debug_control;
   uint32_t saturate;
   uint32_t flag_subreg_nr;
   uint32_t flag_nr;
   uint32_t mask_control;
};

struct inst_dst {
   uint32_t type;
   uint32_t file;
   uint32_t num;
   uint32_t da1_subnum;
   uint32_t da16_subnum;
   uint32_t ia_subnum;
   uint32_t hstride;
   uint32_t address_mode;
   uint32_t writemask;
};

struct inst_src {
   uint32_t vstride;
   uint32_t width;
   uint32_t swiz_w;
   uint32_t swiz_z;
   uint32_t hstride;
   uint32_t address_mode;
   uint32_t negate;
   uint32_t abs;

   uint32_t ia_subnum;
   uint32_t num;
   uint32_t da16_subnum;
   uint32_t da1_subnum;

   uint32_t swiz_x;
   uint32_t swiz_y;

   uint32_t type;
   uint32_t file;
};

struct inst_send {
   uint32_t sfid;
   uint32_t function_control;
   uint32_t header_present;
   uint32_t rlen;
   uint32_t mlen;
   uint32_t eot;
};

struct inst {
   uint64_t qw[2];
};

static inline bool
is_integer(int type)
{
   switch (type) {
   case BRW_HW_REG_TYPE_UD:
   case BRW_HW_REG_TYPE_D:
   case BRW_HW_REG_TYPE_UW:
   case BRW_HW_REG_TYPE_W:
   case BRW_HW_REG_NON_IMM_TYPE_UB:
   case BRW_HW_REG_NON_IMM_TYPE_B:
   case GEN8_HW_REG_TYPE_UQ:
   case GEN8_HW_REG_TYPE_Q:
      return true;
   case BRW_HW_REG_TYPE_F:
   case GEN8_HW_REG_NON_IMM_TYPE_HF:
   case GEN7_HW_REG_NON_IMM_TYPE_DF:
      return false;
   }

   printf("unknown type\n");
   return false;
}

static inline bool
is_float(int type)
{
   return !is_integer(type);
};

static int
type_size(int type)
{
   switch (type) {
   case BRW_HW_REG_TYPE_UD:
   case BRW_HW_REG_TYPE_D:
   case BRW_HW_REG_TYPE_F:
      return 4;
   case BRW_HW_REG_TYPE_UW:
   case BRW_HW_REG_TYPE_W:
   case GEN8_HW_REG_NON_IMM_TYPE_HF:
      return 2;
   case BRW_HW_REG_NON_IMM_TYPE_UB:
   case BRW_HW_REG_NON_IMM_TYPE_B:
      return 1;
   case GEN7_HW_REG_NON_IMM_TYPE_DF:
   case GEN8_HW_REG_TYPE_UQ:
   case GEN8_HW_REG_TYPE_Q:
      return 8;
   default:
      return -1; /* ksim_assert */
   }
}

static uint32_t
_3src_type_to_type(uint32_t _3src_type)
{
   switch (_3src_type) {
   case BRW_3SRC_TYPE_F: return BRW_HW_REG_TYPE_F;
   case BRW_3SRC_TYPE_D: return BRW_HW_REG_TYPE_D;
   case BRW_3SRC_TYPE_UD: return BRW_HW_REG_TYPE_UD;
   case BRW_3SRC_TYPE_DF: return GEN7_HW_REG_NON_IMM_TYPE_DF;
   default: assert(0);
   }
}

static inline uint32_t
get_inst_bits(struct inst *inst, int start, int end)
{
   uint32_t mask;

   assert(end + 1 - start < 64);
   mask = ~0U >> (31 - end + start);
   if (start < 64) {
      return (inst->qw[0] >> start) & mask;
   } else {
      return (inst->qw[1] >> (start - 64)) & mask;
   }
}

static inline struct inst_common
unpack_inst_common(struct inst *packed)
{
   return (struct inst_common) {
      .opcode                   = get_inst_bits(packed,  0,   6),
      .access_mode              = get_inst_bits(packed,  8,   8),
      .no_dd_clear              = get_inst_bits(packed,  9,   9),
      .no_dd_check              = get_inst_bits(packed, 10,  10),
      .nib_control              = get_inst_bits(packed, 11,  11),
      .qtr_control              = get_inst_bits(packed, 12,  13),
      .thread_control           = get_inst_bits(packed, 14,  15),
      .pred_control             = get_inst_bits(packed, 16,  19),
      .pred_inv                 = get_inst_bits(packed, 20,  20),
      .exec_size                = get_inst_bits(packed, 21,  23),
      .math_function            = get_inst_bits(packed, 24,  27),
      .cond_modifier            = get_inst_bits(packed, 24,  27),
      .acc_wr_control           = get_inst_bits(packed, 28,  28),
      .branch_control           = get_inst_bits(packed, 28,  28),
      .cmpt_control             = get_inst_bits(packed, 29,  29),
      .debug_control            = get_inst_bits(packed, 30,  30),
      .saturate                 = get_inst_bits(packed, 31,  31),
      .flag_subreg_nr           = get_inst_bits(packed, 32,  32),
      .flag_nr                  = get_inst_bits(packed, 32,  32),
      .mask_control             = get_inst_bits(packed, 34,  34)
   };
}

static inline struct inst_send
unpack_inst_send(struct inst *packed)
{
   return (struct inst_send) {
      .sfid                     = get_inst_bits(packed,  24,  27),
      .function_control         = get_inst_bits(packed,  96,  114),
      .header_present           = get_inst_bits(packed,  115,  115),
      .rlen                     = get_inst_bits(packed,  116,  120),
      .mlen                     = get_inst_bits(packed,  121,  124),
      .eot                      = get_inst_bits(packed,  127,  127),
   };
}

static inline struct inst_dst
unpack_inst_2src_dst(struct inst *packed)
{
   return (struct inst_dst) {
      .file                     = get_inst_bits(packed,   35,   36),
      .type                     = get_inst_bits(packed,   37,   40),
      .da1_subnum               = get_inst_bits(packed,   48,   52),
      .writemask                = get_inst_bits(packed,   48,   51),
      .da16_subnum              = get_inst_bits(packed,   52,   52),
      .num                      = get_inst_bits(packed,   53,   60),
      .ia_subnum                = get_inst_bits(packed,   57,   60),
      .hstride                  = get_inst_bits(packed,   61,   63),
      .address_mode             = get_inst_bits(packed,   63,   63),
   };
}

static inline struct inst_src
unpack_inst_2src_src0(struct inst *packed)
{
   return (struct inst_src) {
      .vstride                  = (1 << get_inst_bits(packed, 85, 88)) >> 1,
      .width                    = 1 << get_inst_bits(packed, 82, 84),
      .swiz_w                   = get_inst_bits(packed, 82, 83),
      .swiz_z                   = get_inst_bits(packed, 80, 81),
      .hstride                  = (1 << get_inst_bits(packed, 80, 81)) >> 1,
      .address_mode             = get_inst_bits(packed, 79, 79),
      .negate                   = get_inst_bits(packed, 78, 78),
      .abs                      = get_inst_bits(packed, 77, 77),
      .ia_subnum                = get_inst_bits(packed, 73, 76),
      .num                      = get_inst_bits(packed, 69, 76),
      .da16_subnum              = get_inst_bits(packed, 68, 68),
      .da1_subnum               = get_inst_bits(packed, 64, 68),
      .swiz_x                   = get_inst_bits(packed, 66, 67),
      .swiz_y                   = get_inst_bits(packed, 64, 65),
      .type                     = get_inst_bits(packed, 43, 46),
      .file                     = get_inst_bits(packed, 41, 42)
   };
}

static inline struct inst_src
unpack_inst_2src_src1(struct inst *packed)
{
   return (struct inst_src) {
      .file                     = get_inst_bits(packed,  89,  90),
      .type                     = get_inst_bits(packed,  43,  45),
      .da1_subnum               = get_inst_bits(packed,  96, 100),
      .da16_subnum              = get_inst_bits(packed, 100, 100),
      .num                      = get_inst_bits(packed, 101, 108),
      .ia_subnum                = get_inst_bits(packed, 105, 108),
      .abs                      = get_inst_bits(packed, 109, 109),
      .negate                   = get_inst_bits(packed, 110, 110),
      .address_mode             = get_inst_bits(packed, 111, 111),
      .hstride                  = (1 << get_inst_bits(packed, 112, 113)) >> 1,
      .swiz_z                   = get_inst_bits(packed, 112, 113),
      .swiz_w                   = get_inst_bits(packed, 114, 115),
      .width                    = 1 << get_inst_bits(packed, 114, 116),
      .vstride                  = (1 << get_inst_bits(packed, 117, 120)) >> 1
   };
}

static inline struct inst_dst
unpack_inst_3src_dst(struct inst *packed)
{
   uint32_t type = _3src_type_to_type(get_inst_bits(packed, 46, 48));

   return (struct inst_dst) {
      .file                     = BRW_GENERAL_REGISTER_FILE,
      .type                     = type,
      .da1_subnum               = 0,
      .writemask                = get_inst_bits(packed,   49,   52),
      .da16_subnum              = get_inst_bits(packed,   53,   55) * type_size(type),
      .num                      = get_inst_bits(packed,   56,   63),
      .ia_subnum                = 0,
      .hstride                  = 1,
      .address_mode             = BRW_ADDRESS_DIRECT
   };
}

static inline struct inst_src
unpack_inst_3src_src0(struct inst *packed)
{
   uint32_t type = _3src_type_to_type(get_inst_bits(packed,  43,  45));

   return (struct inst_src) {
      .file                     = BRW_GENERAL_REGISTER_FILE,
      .type                     = type,
      .abs                      = get_inst_bits(packed,  37,  37),
      .negate                   = get_inst_bits(packed,  38,  38),
      .hstride                  = get_inst_bits(packed,  64,  64) ? 0 : 1,
      .width                    = get_inst_bits(packed,  64,  64) ? 1 : 4,
      .vstride                  = get_inst_bits(packed,  64,  64) ? 0 : 4,
      .swiz_x                   = get_inst_bits(packed,  65,  66),
      .swiz_y                   = get_inst_bits(packed,  67,  68),
      .swiz_z                   = get_inst_bits(packed,  69,  70),
      .swiz_w                   = get_inst_bits(packed,  71,  72),
      .da16_subnum              = get_inst_bits(packed,  73,  75) * type_size(type),
      .num                      = get_inst_bits(packed,  76,  83),
      .da1_subnum               = 0,
      .ia_subnum                = 0,
      .address_mode             = BRW_ADDRESS_DIRECT,
   };
}

static inline struct inst_src
unpack_inst_3src_src1(struct inst *packed)
{
   uint32_t type = _3src_type_to_type(get_inst_bits(packed,  43,  45));

   return (struct inst_src) {
      .file                     = BRW_GENERAL_REGISTER_FILE,
      .type                     = type,
      .abs                      = get_inst_bits(packed,  39,  39),
      .negate                   = get_inst_bits(packed,  40,  40),
      .hstride                  = get_inst_bits(packed,  85,  85) ? 0 : 1,
      .width                    = get_inst_bits(packed,  85,  85) ? 1 : 4,
      .vstride                  = get_inst_bits(packed,  85,  85) ? 0 : 4,
      .swiz_x                   = get_inst_bits(packed,  86,  87),
      .swiz_y                   = get_inst_bits(packed,  88,  89),
      .swiz_z                   = get_inst_bits(packed,  90,  91),
      .swiz_w                   = get_inst_bits(packed,  92,  93),
      .da16_subnum              = get_inst_bits(packed,  94,  96) * type_size(type),
      .num                      = get_inst_bits(packed,  97, 104),
      .da1_subnum               = 0,
      .ia_subnum                = 0,
      .address_mode             = BRW_ADDRESS_DIRECT,
   };
}

static inline struct inst_src
unpack_inst_3src_src2(struct inst *packed)
{
   uint32_t type = _3src_type_to_type(get_inst_bits(packed,  43,  45));

   return (struct inst_src) {
      .file                     = BRW_GENERAL_REGISTER_FILE,
      .type                     = type,
      .abs                      = get_inst_bits(packed,  41,  41),
      .negate                   = get_inst_bits(packed,  42,  42),
      .hstride                  = get_inst_bits(packed, 106, 106) ? 0 : 1,
      .width                    = get_inst_bits(packed, 106, 106) ? 1 : 4,
      .vstride                  = get_inst_bits(packed, 106, 106) ? 0 : 4,
      .swiz_x                   = get_inst_bits(packed, 107, 108),
      .swiz_y                   = get_inst_bits(packed, 109, 110),
      .swiz_z                   = get_inst_bits(packed, 111, 112),
      .swiz_w                   = get_inst_bits(packed, 113, 114),
      .da16_subnum              = get_inst_bits(packed, 115, 117) * type_size(type),
      .num                      = get_inst_bits(packed, 118, 125),
      .da1_subnum               = 0,
      .ia_subnum                = 0,
      .address_mode             = BRW_ADDRESS_DIRECT,
   };
}

struct inst_imm {
   int32_t d;
   uint32_t ud;
   float f;
   float vf[4];
};

static inline float
u32_to_float(uint32_t ud)
{
   return ((union { float f; uint32_t ud; }) { .ud = ud }).f;
}

static inline float
vf_to_float(unsigned char vf)
{
   /* ±0.0f is special cased. */
   if (vf == 0x00 || vf == 0x80)
      return u32_to_float(vf << 24);
   else
      return u32_to_float(((vf & 0x80) << 24) | ((vf & 0x7f) << (23 - 4)));
}

static inline struct inst_imm
unpack_inst_imm(struct inst *packed)
{
   return (struct inst_imm) {
      .d                        = get_inst_bits(packed,  96,  127),
      .ud                       = get_inst_bits(packed,  96,  127),
      .f                        = u32_to_float(get_inst_bits(packed,  96,  127)),
      .vf = {
         vf_to_float(get_inst_bits(packed,   96,  103)),
         vf_to_float(get_inst_bits(packed,  104,  111)),
         vf_to_float(get_inst_bits(packed,  112,  119)),
         vf_to_float(get_inst_bits(packed,  120,  127)),
      }
   };
}

union alu_reg {
   __m256i d;
   __m256 f;
   uint32_t u32[8];
};

static void
store_type(struct thread *t, uint32_t v, int type, int offset)
{
   void *address = ((void *) t->grf + offset);
   switch (type) {
   case BRW_HW_REG_TYPE_UD:
   case BRW_HW_REG_TYPE_D:
   case BRW_HW_REG_TYPE_F:
      *(uint32_t *) address = v;
      break;
   case BRW_HW_REG_TYPE_UW:
   case BRW_HW_REG_TYPE_W:
   case GEN8_HW_REG_NON_IMM_TYPE_HF:
      *(uint16_t *) address = v;
      break;
   case BRW_HW_REG_NON_IMM_TYPE_UB:
   case BRW_HW_REG_NON_IMM_TYPE_B:
      *(uint8_t *) address = v;
      break;
   case GEN7_HW_REG_NON_IMM_TYPE_DF:
   case GEN8_HW_REG_TYPE_UQ:
   case GEN8_HW_REG_TYPE_Q:
      *(uint64_t *) address = v;
      break;
   }
}

static int
load_imm(union alu_reg *reg, struct inst *inst, struct inst_src *src)
{
   switch (src->type) {
   case BRW_HW_REG_TYPE_UD:
   case BRW_HW_REG_TYPE_D:
      reg->d = _mm256_set1_epi32(unpack_inst_imm(inst).ud);
      break;
   case BRW_HW_REG_IMM_TYPE_UV:
      break;
   case BRW_HW_REG_TYPE_F:
      reg->f = _mm256_set1_ps(unpack_inst_imm(inst).f);
      break;
   case BRW_HW_REG_TYPE_UW:
   case BRW_HW_REG_TYPE_W:
      reg->d = _mm256_set1_epi32(unpack_inst_imm(inst).ud);
      break;
   case BRW_HW_REG_IMM_TYPE_VF:
      reg->f = _mm256_set_ps(unpack_inst_imm(inst).vf[0],
                             unpack_inst_imm(inst).vf[1],
                             unpack_inst_imm(inst).vf[2],
                             unpack_inst_imm(inst).vf[3],
                             unpack_inst_imm(inst).vf[0],
                             unpack_inst_imm(inst).vf[1],
                             unpack_inst_imm(inst).vf[2],
                             unpack_inst_imm(inst).vf[3]);
      break;
   case BRW_HW_REG_IMM_TYPE_V:
      /* FIXME: What is this? */
      break;
   case GEN8_HW_REG_IMM_TYPE_DF:
   case GEN8_HW_REG_IMM_TYPE_HF:
      /* FIXME: */
      break;
   }
   return 0;
}

static int
load_reg(struct thread *t, union alu_reg *r, struct inst_src *src, int subnum)
{
   switch (src->file) {
   case BRW_ARCHITECTURE_REGISTER_FILE:
      switch (src->num & 0xf0) {
      case BRW_ARF_NULL:
         return -1;
      case BRW_ARF_ADDRESS:
         break;
      case BRW_ARF_ACCUMULATOR:
         break;
      case BRW_ARF_FLAG:
         break;
      case BRW_ARF_MASK:
         break;
      case BRW_ARF_MASK_STACK:
         break;
      case BRW_ARF_STATE:
         break;
      case BRW_ARF_CONTROL:
         break;
      case BRW_ARF_NOTIFICATION_COUNT:
         break;
      case BRW_ARF_IP:
         return -1;
      case BRW_ARF_TDR:
         return -1;
      case BRW_ARF_TIMESTAMP:
         break;
      default:
         break;
      }
      break;
   case BRW_GENERAL_REGISTER_FILE: {
      assert(is_power_of_two(src->width));

      uint32_t shift = __builtin_ffs(src->width) - 1;
      __m256i base = _mm256_set_epi32(7, 6, 5, 4, 3, 2, 1, 0);
      __m256i hoffsets = _mm256_and_si256(base, _mm256_set1_epi32(src->width - 1));
      __m256i voffsets = _mm256_srlv_epi32(base, _mm256_set1_epi32(shift));
      __m256i offsets = _mm256_add_epi32(
         _mm256_mullo_epi32(hoffsets, _mm256_set1_epi32(src->hstride)),
         _mm256_mullo_epi32(voffsets, _mm256_set1_epi32(src->vstride)));

      offsets = _mm256_mullo_epi32(offsets,  _mm256_set1_epi32(type_size(src->type)));
      void *grf_base = (void *) t->grf + src->num * 32 + subnum;
      r->d = _mm256_i32gather_epi32(grf_base, offsets, 1);

      /* FIXME: Mask out bits above type size? Shouldn't matter... */

      break;
   }
   case BRW_MESSAGE_REGISTER_FILE:
      break;
   case BRW_IMMEDIATE_VALUE:
      break;
   }

   return 0;
}

static bool
is_logic_instruction(struct inst *inst)
{
   switch (unpack_inst_common(inst).opcode) {
   case BRW_OPCODE_AND:
   case BRW_OPCODE_NOT:
   case BRW_OPCODE_OR:
   case BRW_OPCODE_XOR:
      return true;
   default:
      return false;
   }
}

static void
load_src(union alu_reg *reg, struct thread *t,
         struct inst *inst, struct inst_src *src)
{
   struct inst_common common = unpack_inst_common(inst);

   if (src->file == BRW_IMMEDIATE_VALUE) {
      load_imm(reg, inst, src);
   } else if (common.access_mode == BRW_ALIGN_1) {
      if (src->address_mode == BRW_ADDRESS_DIRECT) {
         load_reg(t, reg, src, src->da1_subnum);
      } else {
         /* FIXME ia load */
      }
   } else {
      if (src->address_mode == BRW_ADDRESS_DIRECT) {
         load_reg(t, reg, src, src->da16_subnum);
      } else {
         /* not allowed */
      }
   }

   if (src->abs) {
      if (src->type == BRW_HW_REG_TYPE_F) {
         reg->d = _mm256_and_si256(reg->d, _mm256_set1_epi32(0x7fffffff));
      } else {
         reg->d = _mm256_abs_epi32(reg->d);
      }
   }

   if (src->negate) {
      if (is_logic_instruction(inst)) {
         reg->d = _mm256_xor_si256(_mm256_setzero_si256(), reg->d);
      } else if (src->type == BRW_HW_REG_TYPE_F) {
         reg->f = _mm256_sub_ps(_mm256_setzero_ps(), reg->f);
      } else {
         reg->d = _mm256_sub_epi32(_mm256_setzero_si256(), reg->d);
      }
   }

   /* FIXME: swizzles */
}

static int
store_reg(struct thread *t, union alu_reg *r,
          struct inst *inst, struct inst_dst *dst)
{
   struct inst_common common = unpack_inst_common(inst);
   int subnum = common.access_mode == BRW_ALIGN_1 ?
      dst->da1_subnum : dst->da16_subnum;

   switch (dst->file) {
   case BRW_ARCHITECTURE_REGISTER_FILE:
      switch (dst->num & 0xf0) {
      case BRW_ARF_NULL:
         return -1;
      case BRW_ARF_ADDRESS:
         break;
      case BRW_ARF_ACCUMULATOR:
         break;
      case BRW_ARF_FLAG:
         break;
      case BRW_ARF_MASK:
         break;
      case BRW_ARF_MASK_STACK:
         break;
      case BRW_ARF_STATE:
         break;
      case BRW_ARF_CONTROL:
         break;
      case BRW_ARF_NOTIFICATION_COUNT:
         break;
      case BRW_ARF_IP:
         return -1;
      case BRW_ARF_TDR:
         return -1;
      case BRW_ARF_TIMESTAMP:
         break;
      default:
         break;
      }
      break;
   case BRW_GENERAL_REGISTER_FILE: {
      int exec_size = 1 << common.exec_size;
      int size = type_size(dst->type);
      int offset = dst->num * 32 + subnum * size;

      uint32_t out[8];
      _mm256_storeu_si256((void *) out, r->d);

      if (size == 4 && dst->hstride == 1) {
         memcpy((void *) t->grf + offset, out, 4 * exec_size);
      } else {

         for (int i = 0; i < exec_size; i++) {
            store_type(t, out[i], dst->type, offset);
            offset += ((1 << dst->hstride) >> 1) * size;
         }
      }
      break;
   }
   case BRW_MESSAGE_REGISTER_FILE:
      break;
   case BRW_IMMEDIATE_VALUE:
      break;
   }

   return 0;
}


static int
store_dst(struct thread *t, union alu_reg *r,
          struct inst *inst, struct inst_dst *dst)
{
   struct inst_common common = unpack_inst_common(inst);

   /* FIXME: write masks */

   if (common.saturate && dst->type == BRW_HW_REG_TYPE_F) {
      r->f = _mm256_max_ps(r->f, _mm256_set1_ps(1.0f));
      r->f = _mm256_max_ps(r->f, _mm256_set1_ps(0.0f));
   }

   if (dst->address_mode == BRW_ADDRESS_DIRECT) {
      store_reg(t, r, inst, dst);
   } else {
      /* FIXME: indirect align1 (assert !align16) */
   }

   return 0;
}

static inline __m256
do_cmp(struct inst *inst, union alu_reg src0, union alu_reg src1)
{
   struct inst_common common = unpack_inst_common(inst);

   switch (common.cond_modifier) {
   case BRW_CONDITIONAL_NONE:
      /* assert: must have both pred */
      break;
   case BRW_CONDITIONAL_Z:
   case BRW_CONDITIONAL_NZ:
   case BRW_CONDITIONAL_G:
      return _mm256_cmp_ps(src0.f, src1.f, 14);
   case BRW_CONDITIONAL_GE:
      return _mm256_cmp_ps(src0.f, src1.f, 13);
   case BRW_CONDITIONAL_L:
   case BRW_CONDITIONAL_LE:
   case BRW_CONDITIONAL_R:
   case BRW_CONDITIONAL_O:
   case BRW_CONDITIONAL_U:
      return _mm256_set1_ps(0.0f);
   default:
      return _mm256_set1_ps(0.0f);
   }

   return _mm256_set1_ps(0.0f);
}

void
sfid_urb(struct thread *t,
         uint32_t dst, uint32_t src,
         uint32_t function_control,
         bool header_present, int mlen, int rlen);

static const struct {
   int num_srcs;
   bool store_dst;
} opcode_info[] = {
   [BRW_OPCODE_MOV]             = { .num_srcs = 1, .store_dst = true },
   [BRW_OPCODE_SEL]             = { .num_srcs = 2,. store_dst = true },
   [BRW_OPCODE_NOT]             = { .num_srcs = 1, .store_dst = true },
   [BRW_OPCODE_AND]             = { .num_srcs = 2,. store_dst = true },
   [BRW_OPCODE_OR]              = { .num_srcs = 2,. store_dst = true },
   [BRW_OPCODE_XOR]             = { .num_srcs = 2,. store_dst = true },
   [BRW_OPCODE_SHR]             = { .num_srcs = 2,. store_dst = true },
   [BRW_OPCODE_SHL]             = { .num_srcs = 2,. store_dst = true },
   [BRW_OPCODE_ASR]             = { .num_srcs = 2,. store_dst = true },
   [BRW_OPCODE_CMP]             = { .num_srcs = 2,. store_dst = true },
   [BRW_OPCODE_CMPN]            = { },
   [BRW_OPCODE_CSEL]            = { .num_srcs = 3, .store_dst = true },
   [BRW_OPCODE_F32TO16]         = { },
   [BRW_OPCODE_F16TO32]         = { },
   [BRW_OPCODE_BFREV]           = { },
   [BRW_OPCODE_BFE]             = { .num_srcs = 3, .store_dst = true },
   [BRW_OPCODE_BFI1]            = { },
   [BRW_OPCODE_BFI2]            = { .num_srcs = 3, .store_dst = true },
   [BRW_OPCODE_JMPI]            = { .num_srcs = 0, .store_dst = false },
   [BRW_OPCODE_IF]              = { },
   [BRW_OPCODE_IFF]             = { },
   [BRW_OPCODE_ELSE]            = { },
   [BRW_OPCODE_ENDIF]           = { },
   [BRW_OPCODE_DO]              = { .num_srcs = 0, .store_dst = false },
   [BRW_OPCODE_WHILE]           = { },
   [BRW_OPCODE_BREAK]           = { },
   [BRW_OPCODE_CONTINUE]        = { },
   [BRW_OPCODE_HALT]            = { },
   [BRW_OPCODE_MSAVE]           = { },
   [BRW_OPCODE_MRESTORE]        = { },
   [BRW_OPCODE_GOTO]            = { },
   [BRW_OPCODE_POP]             = { },
   [BRW_OPCODE_WAIT]            = { },
   [BRW_OPCODE_SEND]            = { },
   [BRW_OPCODE_SENDC]           = { },
   [BRW_OPCODE_MATH]            = { .num_srcs = 2,. store_dst = true },
   [BRW_OPCODE_ADD]             = { .num_srcs = 2,. store_dst = true },
   [BRW_OPCODE_MUL]             = { .num_srcs = 2,. store_dst = true },
   [BRW_OPCODE_AVG]             = { },
   [BRW_OPCODE_FRC]             = { },
   [BRW_OPCODE_RNDU]            = { },
   [BRW_OPCODE_RNDD]            = { },
   [BRW_OPCODE_RNDE]            = { },
   [BRW_OPCODE_RNDZ]            = { },
   [BRW_OPCODE_MAC]             = { },
   [BRW_OPCODE_MACH]            = { },
   [BRW_OPCODE_LZD]             = { },
   [BRW_OPCODE_FBH]             = { },
   [BRW_OPCODE_FBL]             = { },
   [BRW_OPCODE_CBIT]            = { },
   [BRW_OPCODE_ADDC]            = { },
   [BRW_OPCODE_SUBB]            = { },
   [BRW_OPCODE_SAD2]            = { },
   [BRW_OPCODE_SADA2]           = { },
   [BRW_OPCODE_DP4]             = { },
   [BRW_OPCODE_DPH]             = { },
   [BRW_OPCODE_DP3]             = { },
   [BRW_OPCODE_DP2]             = { },
   [BRW_OPCODE_LINE]            = { },
   [BRW_OPCODE_PLN]             = { },
   [BRW_OPCODE_MAD]             = { .num_srcs = 3, .store_dst = true },
   [BRW_OPCODE_LRP]             = { .num_srcs = 3, .store_dst = true },
   [BRW_OPCODE_NENOP]           = { .num_srcs = 0, .store_dst = false },
   [BRW_OPCODE_NOP]             = { .num_srcs = 0, .store_dst = false },
};

static void
dump_reg(const char *name, union alu_reg reg, int type)
{
   if (TRACE_EU & trace_mask) {
      printf("%s: ", name);
      if (is_float(type)) {
         for (int c = 0; c < 8; c++)
            printf("  %6.2f", reg.f[c]);
      } else {
         for (int c = 0; c < 8; c++)
            printf("  %6d", reg.u32[c]);
      }
      printf("\n");
   }
}

bool
brw_execute_inst(void *inst, struct thread *t);

bool
brw_execute_inst(void *inst, struct thread *t)
{
   struct inst *packed = (struct inst *) inst;
   struct inst_src unpacked_src;
   uint32_t opcode = unpack_inst_common(packed).opcode;
   bool eot = false;

   union alu_reg dst, src0, src1, src2;

   if (opcode_info[opcode].num_srcs == 3) {
      unpacked_src = unpack_inst_3src_src0(inst);
      load_src(&src0, t, packed, &unpacked_src);
      dump_reg("src0", src0, unpacked_src.type);

      unpacked_src = unpack_inst_3src_src1(inst);
      load_src(&src1, t, packed, &unpacked_src);
      dump_reg("src1", src1, unpacked_src.type);

      unpacked_src = unpack_inst_3src_src2(inst);
      load_src(&src2, t, packed, &unpacked_src);
      dump_reg("src2", src2, unpacked_src.type);

   } else if (opcode_info[opcode].num_srcs >= 1) {
      unpacked_src = unpack_inst_2src_src0(inst);
      load_src(&src0, t, packed, &unpacked_src);
      dump_reg("src0", src0, unpacked_src.type);
   }

   if (opcode_info[opcode].num_srcs == 2) {
      unpacked_src = unpack_inst_2src_src1(inst);
      load_src(&src1, t, packed, &unpacked_src);
      dump_reg("src1", src1, unpacked_src.type);
   }

   switch (opcode) {
   case BRW_OPCODE_MOV:
      dst = src0;
      break;
   case BRW_OPCODE_SEL: {
      union alu_reg mask;

      /* assert: must have either pred orcmod */

      mask.f = do_cmp(inst, src0, src1);

      /* AVX2 blendv is opposite of the EU sel order, so we swap src0 and src1
       * operands. */
      dst.d = _mm256_blendv_epi8(src1.d, src0.d, mask.d);
      break;
   }
   case BRW_OPCODE_NOT:
      dst.d = _mm256_xor_si256(_mm256_setzero_si256(), src0.d);
      break;
   case BRW_OPCODE_AND:
      dst.d = _mm256_and_si256(src0.d, src1.d);
      break;
   case BRW_OPCODE_OR:
      dst.d = _mm256_or_si256(src0.d, src1.d);
      break;
   case BRW_OPCODE_XOR:
      dst.d = _mm256_xor_si256(src0.d, src1.d);
      break;
   case BRW_OPCODE_SHR:
      dst.d = _mm256_srlv_epi32(src0.d, src1.d);
      break;
   case BRW_OPCODE_SHL:
      dst.d = _mm256_sllv_epi32(src0.d, src1.d);
      break;
   case BRW_OPCODE_ASR:
      dst.d = _mm256_srav_epi32(src0.d, src1.d);
      break;
   case BRW_OPCODE_CMP:
      dst.f = do_cmp(inst, src0, src1);
      break;
   case BRW_OPCODE_CMPN:
      break;
   case BRW_OPCODE_CSEL:
      break;
   case BRW_OPCODE_F32TO16:
      break;
   case BRW_OPCODE_F16TO32:
      break;
   case BRW_OPCODE_BFREV:
      break;
   case BRW_OPCODE_BFE:
      break;
   case BRW_OPCODE_BFI1:
      break;
   case BRW_OPCODE_BFI2:
      break;
   case BRW_OPCODE_JMPI:
      break;
   case BRW_OPCODE_IF:
      break;
   case BRW_OPCODE_IFF:
      break;
   case BRW_OPCODE_ELSE:
      break;
   case BRW_OPCODE_ENDIF:
      break;
   case BRW_OPCODE_DO:
      break;
   case BRW_OPCODE_WHILE:
      break;
   case BRW_OPCODE_BREAK:
      break;
   case BRW_OPCODE_CONTINUE:
      break;
   case BRW_OPCODE_HALT:
      break;
   case BRW_OPCODE_MSAVE:
      break;
   case BRW_OPCODE_MRESTORE:
      break;
   case BRW_OPCODE_GOTO:
      break;
   case BRW_OPCODE_POP:
      break;
   case BRW_OPCODE_WAIT:
      break;
   case BRW_OPCODE_SEND:
   case BRW_OPCODE_SENDC: {
      struct inst_send send = unpack_inst_send(inst);
      eot = send.eot;
      switch (send.sfid) {
      case BRW_SFID_NULL:
      case BRW_SFID_MATH:
      case BRW_SFID_SAMPLER:
      case BRW_SFID_MESSAGE_GATEWAY:
         break;
      case BRW_SFID_URB:
         sfid_urb(t,
                  unpack_inst_2src_dst(inst).num,
                  unpack_inst_2src_src0(inst).num,
                  send.function_control,
                  send.header_present, send.mlen, send.rlen);
         break;
      case BRW_SFID_THREAD_SPAWNER:
      case GEN6_SFID_DATAPORT_SAMPLER_CACHE:
      case GEN6_SFID_DATAPORT_RENDER_CACHE:
      case GEN6_SFID_DATAPORT_CONSTANT_CACHE:
      case GEN7_SFID_DATAPORT_DATA_CACHE:
      case GEN7_SFID_PIXEL_INTERPOLATOR:
      case HSW_SFID_DATAPORT_DATA_CACHE_1:
      case HSW_SFID_CRE:
         break;
      }
      break;
   }
   case BRW_OPCODE_MATH:
      switch (unpack_inst_common(packed).math_function) {
      case BRW_MATH_FUNCTION_INV:
         break;
      case BRW_MATH_FUNCTION_LOG:
         break;
      case BRW_MATH_FUNCTION_EXP:
         break;
      case BRW_MATH_FUNCTION_SQRT:
         break;
      case BRW_MATH_FUNCTION_RSQ:
         dst.f = _mm256_rsqrt_ps(src0.f);
         break;
      case BRW_MATH_FUNCTION_SIN:
         break;
      case BRW_MATH_FUNCTION_COS:
         break;
      case BRW_MATH_FUNCTION_SINCOS:
         break;
      case BRW_MATH_FUNCTION_FDIV:
         break;
      case BRW_MATH_FUNCTION_POW:
         break;
      case BRW_MATH_FUNCTION_INT_DIV_QUOTIENT_AND_REMAINDER:
         break;
      case BRW_MATH_FUNCTION_INT_DIV_QUOTIENT:
         break;
      case BRW_MATH_FUNCTION_INT_DIV_REMAINDER:
         break;
      case GEN8_MATH_FUNCTION_INVM:
         break;
      case GEN8_MATH_FUNCTION_RSQRTM:
         break;
      default:
         printf("some math function\n");
         break;
      }
      break;
   case BRW_OPCODE_ADD:
      if (is_integer(unpack_inst_2src_dst(inst).type))
         dst.d = _mm256_add_epi32(src0.d, src1.d);
      else
         dst.f = _mm256_add_ps(src0.f, src1.f);
      break;
   case BRW_OPCODE_MUL:
      if (is_integer(unpack_inst_2src_dst(inst).type))
         dst.d = _mm256_mullo_epi32(src0.d, src1.d);
      else
         dst.f = _mm256_mul_ps(src0.f, src1.f);
      break;
   case BRW_OPCODE_AVG:
      break;
   case BRW_OPCODE_FRC:
      break;
   case BRW_OPCODE_RNDU:
      break;
   case BRW_OPCODE_RNDD:
      break;
   case BRW_OPCODE_RNDE:
      break;
   case BRW_OPCODE_RNDZ:
      break;
   case BRW_OPCODE_MAC:
      break;
   case BRW_OPCODE_MACH:
      break;
   case BRW_OPCODE_LZD:
      break;
   case BRW_OPCODE_FBH:
      break;
   case BRW_OPCODE_FBL:
      break;
   case BRW_OPCODE_CBIT:
      break;
   case BRW_OPCODE_ADDC:
      break;
   case BRW_OPCODE_SUBB:
      break;
   case BRW_OPCODE_SAD2:
      break;
   case BRW_OPCODE_SADA2:
      break;
   case BRW_OPCODE_DP4:
      break;
   case BRW_OPCODE_DPH:
      break;
   case BRW_OPCODE_DP3:
      break;
   case BRW_OPCODE_DP2:
      break;
   case BRW_OPCODE_LINE: {
      __m256 p = _mm256_set1_ps(src0.f[0]);
      __m256 q = _mm256_set1_ps(src0.f[3]);

      dst.f = _mm256_add_ps(_mm256_mul_ps(src1.f, p), q);
      break;
   }
   case BRW_OPCODE_PLN: {
      __m256 p = _mm256_set1_ps(src0.f[0]);
      __m256 q = _mm256_set1_ps(src0.f[1]);
      __m256 r = _mm256_set1_ps(src0.f[3]);

      unpacked_src = unpack_inst_2src_src1(inst);
      unpacked_src.num++;
      load_src(&src2, t, packed, &unpacked_src);
      dump_reg("src2", src1, unpacked_src.type);

      dst.f = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(src1.f, p),
                                          _mm256_mul_ps(src2.f, q)), r);
      break;
   }
   case BRW_OPCODE_MAD:
      dst.f = _mm256_add_ps(_mm256_mul_ps(src1.f, src2.f), src0.f);
      break;
   case BRW_OPCODE_LRP:
      break;
   case BRW_OPCODE_NENOP:
      break;
   case BRW_OPCODE_NOP:
      break;
   }

   if (opcode_info[opcode].num_srcs == 3) {
      struct inst_dst _dst = unpack_inst_3src_dst(inst);
      struct inst_src _src = unpack_inst_3src_src0(inst);

      dump_reg("dst", dst, _dst.type);

      if (opcode_info[opcode].store_dst) {
         if (is_integer(_src.type) && is_float(_dst.type))
            dst.f = _mm256_cvtepi32_ps(dst.d);
         else if (is_float(_src.type) && is_integer(_dst.type))
            dst.d = _mm256_cvtps_epi32(dst.f);

         store_dst(t, &dst, inst, &_dst);
      }
   } else {
      struct inst_dst _dst = unpack_inst_2src_dst(inst);
      struct inst_src _src = unpack_inst_2src_src0(inst);

      dump_reg("dst", dst, _dst.type);

      if (opcode_info[opcode].store_dst) {
         if (is_integer(_src.type) && is_float(_dst.type))
            dst.f = _mm256_cvtepi32_ps(dst.d);
         else if (is_float(_src.type) && is_integer(_dst.type))
            dst.d = _mm256_cvtps_epi32(dst.f);

         store_dst(t, &dst, inst, &_dst);
      }
   }

   return eot;
}
