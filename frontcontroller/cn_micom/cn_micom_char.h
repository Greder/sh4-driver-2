/****************************************************************************
 *
 * cn_micom_char.h
 *
 * (c) 2009 Dagobert@teamducktales
 * (c) 2010 Schischu & konfetti: Add irq handling
 * (c) 2021 Audioniek: enlarged charactersets
 *
 * Largely based on cn_micom, enhanced and ported to Opticum HD 9600 (TS)
 * by Audioniek.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 ****************************************************************************
 *
 * Changes
 *
 * Date     By              Description
 * --------------------------------------------------------------------------
 * 20090306 W.Y.Yi          Original Crenova Micom version
 * 20201222 Audioniek       Initial version, based on cn_micom.
 *
 ****************************************************************************/
#ifndef __CN_MICOM_CHAR_H__	// {
#define __CN_MICOM_CHAR_H__


/****************************************************************************

  << 7-Segment display (Opticum HD 9600 mini) >>
  Model : MINILINE
  Front Micom : Version 4.xx.xx

        a
    ---------
   |         |
   |         |
 f |         | b
   |         |
   |    g    |
    ---------
   |         |
   |         |
 e |         | c
   |         |
   |         |
    ---------
        d
                          7 6 5 4  3 2 1 0 
 Segment to bit mapping: (x|g|f|e  d|c|b|a)
 Bit 7 is aways zero.

*****************************************************************************/

// Note: font is the same as on Fortis LED models
#define _7SEG_NULL		"\x00"  // space
#define _7SEG_ASC21		"\x06"  // '!'
#define _7SEG_ASC22		"\x22"  // '"'
#define _7SEG_ASC23		"\x5c"  // '#'
#define _7SEG_ASC24		"\x6d"  // '$'
#define _7SEG_ASC25		"\x52"  // '%'
#define _7SEG_ASC26		"\x7d"  // '&'
#define _7SEG_ASC27		"\x02"  // '
#define _7SEG_ASC28		"\x39"  // '('
#define _7SEG_ASC29		"\x0f"  // ')'
#define _7SEG_ASC2A		"\x76"  // '*'
#define _7SEG_ASC2B		"\x46"  // '+'
#define _7SEG_ASC2C		"\x0c"  // ','
#define _7SEG_ASC2D		"\x40"  // '-'
#define _7SEG_ASC2E		"\x08"  // '.'
#define _7SEG_ASC2F		"\x52"  // '/'
#define _7SEG_NUM_0		"\x3F"  // '0'
#define _7SEG_NUM_1		"\x06"  // '1'
#define _7SEG_NUM_2		"\x5B"  // '2'
#define _7SEG_NUM_3		"\x4F"  // '3
#define _7SEG_NUM_4		"\x66"  // '4'
#define _7SEG_NUM_5		"\x6D"  // '5'
#define _7SEG_NUM_6		"\x7D"  // '6'
#define _7SEG_NUM_7		"\x07"  // '7'
#define _7SEG_NUM_8		"\x7F"  // '8'
#define _7SEG_NUM_9		"\x6F"  // '9'
#define _7SEG_ASC3A		"\x10"  // ':'
#define _7SEG_ASC3B		"\x0c"  // ';'
#define _7SEG_ASC3C		"\x58"  // '<'
#define _7SEG_ASC3D		"\x48"  // '='
#define _7SEG_ASC3E		"\x7c"  // '>'
#define _7SEG_ASC3F		"\x07"  // '?'
#define _7SEG_ASC40		"\x7B"  // '@'
#define _7SEG_CH_A		"\x77"
#define _7SEG_CH_B		"\x7C"
#define _7SEG_CH_C		"\x39"
#define _7SEG_CH_D		"\x5E"
#define _7SEG_CH_E		"\x79"
#define _7SEG_CH_F		"\x71"
#define _7SEG_CH_G		"\x3D"
#define _7SEG_CH_H		"\x76"
#define _7SEG_CH_I		"\x06"
#define _7SEG_CH_J		"\x1E"
#define _7SEG_CH_K		"\x7a"
#define _7SEG_CH_L		"\x38"
#define _7SEG_CH_M		"\x55"
#define _7SEG_CH_N		"\x37"
#define _7SEG_CH_O		"\x3F"
#define _7SEG_CH_P		"\x73"
#define _7SEG_CH_Q		"\x67"
#define _7SEG_CH_R		"\x77"
#define _7SEG_CH_S		"\x6D"
#define _7SEG_CH_T		"\x78"
#define _7SEG_CH_U		"\x3E"
#define _7SEG_CH_V		"\x3E"
#define _7SEG_CH_W		"\x7E"
#define _7SEG_CH_X		"\x76"
#define _7SEG_CH_Y		"\x67"
#define _7SEG_CH_Z		"\x5B"
#define _7SEG_ASC5B		"\x39"  // '['
#define _7SEG_ASC5C		"\x64"  // '\'
#define _7SEG_ASC5D		"\x0F"  // ']'
#define _7SEG_ASC5E		"\x23"  // '^'
#define _7SEG_ASC5F		"\x08"  // '_'
#define _7SEG_ASC60		"\x20"  // '`'
#define _7SEG_CH_a		"\x5F"
#define _7SEG_CH_b		"\x7c"
#define _7SEG_CH_c		"\x58"
#define _7SEG_CH_d		"\x5E"
#define _7SEG_CH_e		"\x7B"
#define _7SEG_CH_f		"\x71"
#define _7SEG_CH_g		"\x6F"
#define _7SEG_CH_h		"\x74"
#define _7SEG_CH_i		"\x04"
#define _7SEG_CH_j		"\x0E"
#define _7SEG_CH_k		"\x78"
#define _7SEG_CH_l		"\x38"
#define _7SEG_CH_m		"\x55"
#define _7SEG_CH_n		"\x54"
#define _7SEG_CH_o		"\x5C"
#define _7SEG_CH_p		"\x73"
#define _7SEG_CH_q		"\x67"
#define _7SEG_CH_r		"\x50"
#define _7SEG_CH_s		"\x6D"
#define _7SEG_CH_t		"\x78"
#define _7SEG_CH_u		"\x1C"
#define _7SEG_CH_v		"\x1C"
#define _7SEG_CH_w		"\x1C"
#define _7SEG_CH_x		"\x76"
#define _7SEG_CH_y		"\x6E"
#define _7SEG_CH_z		"\x5B"
#define _7SEG_ASC7B		"\x39"  // '{'
#define _7SEG_ASC7C		"\x06"  // '|'
#define _7SEG_ASC7D		"\x0F"  // '}'
#define _7SEG_ASC7E		"\x40"  // '~'
#define _7SEG_ASC7F		"\x5C"  // DEL

/****************************************************************************

  << 14-Segment display (Opticum HD 9600 Prima) >>
  Model 		: FLEXLINE, ECOLINE
  Front Micom : Version 3.xx.xx

          0
     -----------
   |\     |     /|
   | \    |8   / |
  5|  \7  |   /9 | 1
   |   \  |  /   |
   | 6  \ | /  10|
     ---- * ----    Middle point is bit 13
   |    / | \    |
   | 12/  |  \11 |
  4|  /   |   \  | 2
   | /    |8   \ |
   |/     |     \|
     -----------
          3

 Data format : (7|6|5|4  3|2|1|0) (x|x|13|12  11|10|9|8)
 Bits marked with x should always be zero.

 NOTE on Opticum HS (TS) 9600 non-Prima models: these early versions cannot
 control the individual segments in the display, but instead have a fixed
 set of character definitions built into the front processor (see below).

*****************************************************************************/

#define _14SEG_NULL		"\x00\x00"
#define _14SEG_ASC21	"\x00\x00"
#define _14SEG_ASC22	"\x00\x00"
#define _14SEG_ASC23	"\x00\x00"
#define _14SEG_ASC24	"\x00\x00"
#define _14SEG_ASC25	"\x00\x00"
#define _14SEG_ASC26	"\x00\x00"
#define _14SEG_ASC27	"\x00\x00"
#define _14SEG_ASC28	"\x00\x00"
#define _14SEG_ASC29	"\x00\x00"
#define _14SEG_ASC2A	"\x00\x00"
#define _14SEG_ASC2B	"\x00\x00"
#define _14SEG_ASC2C	"\x00\x00"
#define _14SEG_ASC2D	"\x3F\x32"
#define _14SEG_ASC2E	"\x00\x00"
#define _14SEG_ASC2F	"\x00\x00"
#define _14SEG_NUM_0	"\x3F\x32"
#define _14SEG_NUM_1	"\x06\x00"
#define _14SEG_NUM_2	"\x5B\x24"
#define _14SEG_NUM_3	"\x4F\x24"
#define _14SEG_NUM_4	"\x66\x24"
#define _14SEG_NUM_5	"\x69\x28"
#define _14SEG_NUM_6	"\x7D\x24"
#define _14SEG_NUM_7	"\x07\x00"
#define _14SEG_NUM_8	"\x7F\x24"
#define _14SEG_NUM_9	"\x6F\x24"
#define _14SEG_ASC3A	"\x00\x00"
#define _14SEG_ASC3B	"\x00\x00"
#define _14SEG_ASC3C	"\x00\x00"
#define _14SEG_ASC3D	"\x00\x00"
#define _14SEG_ASC3E	"\x00\x00"
#define _14SEG_ASC3F	"\x00\x00"
#define _14SEG_ASC40	"\x00\x00"
#define _14SEG_CH_A		"\x77\x24"
#define _14SEG_CH_B		"\x0F\x25"
#define _14SEG_CH_C		"\x39\x00"
#define _14SEG_CH_D		"\x0F\x21"
#define _14SEG_CH_E		"\x79\x24"
#define _14SEG_CH_F		"\x71\x24"
#define _14SEG_CH_G		"\x3D\x24"
#define _14SEG_CH_H		"\x76\x24"
#define _14SEG_CH_I		"\x09\x21"
#define _14SEG_CH_J		"\x1E\x00"
#define _14SEG_CH_K		"\x70\x2A"
#define _14SEG_CH_L		"\x38\x00"
#define _14SEG_CH_M		"\xB6\x22"
#define _14SEG_CH_N		"\xB6\x28"
#define _14SEG_CH_O		"\x3F\x00"
#define _14SEG_CH_P		"\x73\x24"
#define _14SEG_CH_Q		"\x3F\x08"
#define _14SEG_CH_R		"\x73\x2C"
#define _14SEG_CH_S		"\x6D\x24"
#define _14SEG_CH_T		"\x01\x21"
#define _14SEG_CH_U		"\x3E\x00"
#define _14SEG_CH_V		"\x30\x32"
#define _14SEG_CH_W		"\x36\x38"
#define _14SEG_CH_X		"\x80\x3A"
#define _14SEG_CH_Y		"\x6E\x24"
#define _14SEG_CH_Z		"\x09\x32"
#define _14SEG_ASC5B	"\x00\x00"
#define _14SEG_ASC5C	"\x00\x00"
#define _14SEG_ASC5D	"\x00\x00"
#define _14SEG_ASC5E	"\x00\x00"
#define _14SEG_ASC5F	"\x00\x00"
#define _14SEG_ASC60	"\x00\x00"
#define _14SEG_CH_a		"\x77\x24"
#define _14SEG_CH_b		"\x0F\x25"
#define _14SEG_CH_c		"\x39\x00"
#define _14SEG_CH_d		"\x0F\x21"
#define _14SEG_CH_e		"\x79\x24"
#define _14SEG_CH_f		"\x71\x24"
#define _14SEG_CH_g		"\x3D\x24"
#define _14SEG_CH_h		"\x76\x24"
#define _14SEG_CH_i		"\x09\x21"
#define _14SEG_CH_j		"\x1E\x00"
#define _14SEG_CH_k		"\x70\x2A"
#define _14SEG_CH_l		"\x38\x00"
#define _14SEG_CH_m		"\xB6\x22"
#define _14SEG_CH_n		"\xB6\x28"
#define _14SEG_CH_o		"\x3F\x00"
#define _14SEG_CH_p		"\x73\x24"
#define _14SEG_CH_q		"\x3F\x08"
#define _14SEG_CH_r		"\x73\x2C"
#define _14SEG_CH_s		"\x6D\x24"
#define _14SEG_CH_t		"\x01\x21"
#define _14SEG_CH_u		"\x3E\x00"
#define _14SEG_CH_v		"\x30\x32"
#define _14SEG_CH_w		"\x36\x38"
#define _14SEG_CH_x		"\x80\x3A"
#define _14SEG_CH_y		"\x6E\x24"
#define _14SEG_CH_z		"\x09\x32"
#define _14SEG_ASC7B	"\x00\x00"
#define _14SEG_ASC7C	"\x00\x00"
#define _14SEG_ASC7D	"\x00\x00"
#define _14SEG_ASC7E	"\x00\x00"
#define _14SEG_ASC7F	"\xff\x3f"

//러시아어 이름은 로마자 표기 임 (ref wiki)
#if defined(_BUYER_SATCOM_RUSSIA) || defined(_BUYER_SOGNO_GERMANY)
#define _14SEG_RUS_a 		"\x77\x24"
#define _14SEG_RUS_b 		"\x7d\x24"
#define _14SEG_RUS_v 		"\x79\x2A"
#define _14SEG_RUS_g 		"\x31\x00"
#define _14SEG_RUS_d 		"\x0F\x21"
#define _14SEG_RUS_ye 		"\x79\x24"
#define _14SEG_RUS_yo 		"\x79\x24"
#define _14SEG_RUS_zh 		"\xC0\x3F"
#define _14SEG_RUS_z 		"\x09\x2A"
#define _14SEG_RUS_i 		"\x36\x32"
#define _14SEG_RUS_ekra 	"\x36\x32"
#define _14SEG_RUS_k 		"\x70\x2A"
#define _14SEG_RUS_el 		"\x06\x32"
#define _14SEG_RUS_m 		"\xB6\x22"
#define _14SEG_RUS_n 		"\x76\x24"
#define _14SEG_RUS_o 		"\x3F\x00"
#define _14SEG_RUS_p 		"\x37\x00"
#define _14SEG_RUS_r 		"\x73\x24"
#define _14SEG_RUS_s 		"\x39\x00"
#define _14SEG_RUS_t 		"\x01\x21"
#define _14SEG_RUS_u 		"\x6E\x24"
#define _14SEG_RUS_f 		"\x3F\x21"
#define _14SEG_RUS_kh 		"\x80\x3A"
#define _14SEG_RUS_ts 		"\x38\x21"
#define _14SEG_RUS_ch 		"\x66\x24"
#define _14SEG_RUS_sh 		"\x3E\x21"
#define _14SEG_RUS_shch 	"\x3E\x21"
#define _14SEG_RUS_tvyor 	"\x0D\x25"
#define _14SEG_RUS_y 		"\x7E\x21"
#define _14SEG_RUS_myah 	"\x0C\x25"
#define _14SEG_RUS_eobeo 	"\x0F\x24"
#define _14SEG_RUS_yu 		"\x76\x2A"
#define _14SEG_RUS_ya		"\x27\x34"

#endif // END of #if defined(_BUYER_SATCOM_RUSSIA)

#define _7SEG_NULL  "\x00"
#define _14SEG_SPECIAL_1 "\x10\x00"
#define _14SEG_SPECIAL_2 "\x20\x00"
#define _14SEG_SPECIAL_3 "\x01\x00"
#define _14SEG_SPECIAL_4 "\x02\x00"
#define _14SEG_SPECIAL_5 "\x04\x00"
#define _14SEG_SPECIAL_6 "\x08\x00"
#define _14SEG_SPECIAL_7 "\x40\x24"

/****************************************************************************
 *
 * Character mapping of the Opticum HD 9600 non-Prima models
 *
 * The front processor of the non-Prima models cannot control individual
 * display segments, but has a fixed data to character assignment as follows
 * (thnx to corev for this info,
 * see https://www.aaf-digital.info/forum/node/76786):
 *
 * 0x00 = ' '  | 0x10 = 'E'  | 0x20 = 'U'  | 0x30 =      |
 * 0x01 =      | 0x11 = 'F'  | 0x21 = 'V'  | 0x31 =      |
 * 0x02 = '0'  | 0x12 = 'G'  | 0x22 = 'W'  | 0x32 =      |
 * 0x03 = '1'  | 0x13 = 'H'  | 0x23 = 'X'  | 0x33 =      |
 * 0x04 = '2'  | 0x14 = 'I'  | 0x24 = 'Y'  | 0x34 =      |
 * 0x05 = '3'  | 0x15 = 'J'  | 0x25 = 'Z'  | 0x35 =      |
 * 0x06 = '4'  | 0x16 = 'K'  | 0x26 =      | 0x36 =      |
 * 0x07 = '5'  | 0x17 = 'L'  | 0x27 =      | 0x37 =      |
 * 0x08 = '6'  | 0x18 = 'M'  | 0x28 =      | 0x38 =      |
 * 0x09 = '7'  | 0x19 = 'N'  | 0x29 =      | 0x39 =      |
 * 0x0a = '8'  | 0x1a = 'O'  | 0x2a =      | 0x3a =      |
 * 0x0b = '9'  | 0x1b = 'P'  | 0x2b =      | 0x3b =      |
 * 0x0c = 'A'  | 0x1c = 'Q'  | 0x2c =      | 0x3c =      |
 * 0x0d = 'B'  | 0x1d = 'R'  | 0x2d =      | 0x3d =      |
 * 0x0e = 'C'  | 0x1e = 'S'  | 0x2e =      | 0x3e =      |
 * 0x0f = 'D'  | 0x1f = 'T'  | 0x2f =      | 0x3f =      |
 *
 ****************************************************************************/

#endif	// __CN_MICOM_CHAR_H__
// vim:ts=4
