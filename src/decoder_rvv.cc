#include "common.h"
#ifdef __riscv_vector
#include "decoder_common.h"


#if defined(__riscv_v_intrinsic) && __riscv_v_intrinsic >= 13000
#define RV_vmerge_vxm_u8m2 RV(vmerge_vxm_u8m2)
#define RV_vmerge_vxm_u16m2 RV(vmerge_vxm_u16m2)
#else
#define RV_vmerge_vxm_u8m2(v, x, m, vl) RV(vmerge_vxm_u8m2)(m, v, x, vl)
#define RV_vmerge_vxm_u16m2(v, x, m, vl) RV(vmerge_vxm_u16m2)(m, v, x, vl)
#endif

template<int shift>
static inline vbool4_t mask_lshift(vbool4_t m, unsigned shiftIn, size_t vl) {
	vuint8m1_t mv = RV_VEC_CAST(4, 8m1, m);
	vuint8m1_t mvl = RV(vsll_vx_u8m1)(mv, shift, vl/8);
	vuint8m1_t mvr = RV(vsrl_vx_u8m1)(mv, 8-shift, vl/8);
	mvr = RV(vslide1up_vx_u8m1)(mvr, shiftIn, vl/8);
	
	return RV(vmor_mm_b4)(
		RV_MASK_CAST(4, mvl), RV_MASK_CAST(4, mvr), vl
	);
}

static inline vuint8m2_t set_first_vu8(vuint8m2_t src, uint8_t item, size_t vl) {
#if defined(__riscv_v_intrinsic) && __riscv_v_intrinsic >= 13000
	return RV(vmv_s_x_u8m2_tu)(src, item, vl);
#else
	vuint8m1_t m = RV(vslide1up_vx_u8m1)(RV(vmv_v_x_u8m1)(0, ~0), 1, ~0);
	return RV_vmerge_vxm_u8m2(src, item, RV_MASK_CAST(4, m), vl);
#endif
}
static inline vuint16m2_t set_first_vu16(vuint16m2_t src, uint16_t item, size_t vl) {
#if defined(__riscv_v_intrinsic) && __riscv_v_intrinsic >= 13000
	return RV(vmv_s_x_u16m2_tu)(src, item, vl);
#else
	vuint16m1_t m = RV(vslide1up_vx_u16m1)(RV(vmv_v_x_u16m1)(0, ~0), 1, ~0);
	return RV_vmerge_vxm_u16m2(src, item, RV_MASK_CAST(8, m), vl);
#endif
}


template<bool isRaw, bool searchEnd>
HEDLEY_ALWAYS_INLINE void do_decode_rvv(const uint8_t* src, long& len, unsigned char*& outp, unsigned char& escFirst, uint16_t& nextMask) {
	HEDLEY_ASSUME(escFirst == 0 || escFirst == 1);
	HEDLEY_ASSUME(nextMask == 0 || nextMask == 1 || nextMask == 2);
	
	size_t vl2 = RV(vsetvlmax_e8m2)();
	
	vuint8m2_t yencOffset = RV(vmv_v_x_u8m2)(42, vl2);
	if(escFirst) yencOffset = set_first_vu8(yencOffset, 42+64, vl2);
	vuint8m2_t lfCompare = RV(vmv_v_x_u8m2)('\n', vl2);
	if(nextMask && isRaw) {
		lfCompare = RV(vreinterpret_v_u16m2_u8m2)(
			set_first_vu16(RV(vreinterpret_v_u8m2_u16m2)(lfCompare), nextMask == 1 ? 0x0a2e /*".\n"*/ : 0x2e0a /*"\n."*/, vl2/2)
		);
	}
	
	// mask where only the highest bit is set
	vbool4_t lastBit = RV(vmseq_vx_u8m2_b4)(
		RV(vslide1down_vx_u8m2)(RV(vmv_v_x_u8m2)(0, vl2), 1, vl2),
		1, vl2
	);
	
	decoder_set_nextMask<isRaw>(src, len, nextMask);
	
	// TODO: consider exploiting partial vector capability
	long inpos;
	for(inpos = -len; inpos; inpos += vl2) {
		vuint8m2_t data = RV(vle8_v_u8m2)(src + inpos, vl2);
		
		// search for special chars
		vbool4_t cmpEq = RV(vmseq_vx_u8m2_b4)(data, '=', vl2);
		vbool4_t cmpCr = RV(vmseq_vx_u8m2_b4)(data, '\r', vl2);
		vbool4_t cmp = RV(vmor_mm_b4)(
			RV(vmor_mm_b4)(cmpEq, cmpCr, vl2),
			RV(vmseq_vv_u8m2_b4)(data, lfCompare, vl2),
			vl2
		);
		
		if(RV(vcpop_m_b4)(cmp, vl2) > 0) {
			// dot-unstuffing + end detection
			if((isRaw || searchEnd) && RV(vcpop_m_b4)(RV(vmxor)(cmp, cmpEq, vl2), vl2)) {
				uint32_t nextWord;
				if(!searchEnd) {
					memcpy(&nextWord, src + inpos + vl2, 2);
				} else {
					memcpy(&nextWord, src + inpos + vl2, 4);
				}
				vuint8m2_t nextData2 = RV(vreinterpret_v_u16m2_u8m2)(RV(vslide1down_vx_u16m2)(RV(vreinterpret_v_u8m2_u16m2)(data), nextWord, vl2/2));
				
				vbool4_t match2Cr_Dot, match3EqY;
				vuint8m2_t nextData3;
				if(isRaw) {
					match2Cr_Dot = RV(vmand_mm_b4)(cmpCr, RV(vmseq_vx_u8m2_b4)(nextData2, '.', vl2), vl2);
				}
				
				if(searchEnd) {
					nextData3 = RV(vslide1down_vx_u8m2)(nextData2, nextWord>>16, vl2);
					match3EqY = RV(vmand_mm_b4)(
						RV(vmseq_vx_u8m2_b4)(nextData2, '=', vl2),
						RV(vmseq_vx_u8m2_b4)(nextData3, 'y', vl2),
						vl2
					);
				}
				
				// find patterns of \r_.
				if(isRaw && LIKELIHOOD(0.001, RV(vcpop_m_b4)(match2Cr_Dot, vl2) > 0)) {
					// find \r\n.
					vuint8m2_t nextData1 = RV(vslide1down_vx_u8m2)(data, nextWord, vl2);
					vbool4_t match1Lf = RV(vmseq_vx_u8m2_b4)(nextData1, '\n', vl2);
					vbool4_t match2NlDot = RV(vmand_mm_b4)(match2Cr_Dot, match1Lf, vl2);
					
					if(searchEnd) {
						vbool4_t match1Nl = RV(vmand_mm_b4)(cmpCr, match1Lf, vl2);
						
						vuint8m2_t nextData4 = RV(vreinterpret_v_u32m2_u8m2)(RV(vslide1down_vx_u32m2)(RV(vreinterpret_v_u8m2_u32m2)(data), nextWord, vl2/4));
						
						// match instances of \r\n.\r\n and \r\n.=y
						vbool4_t match4Nl = RV(vmand_mm_b4)(
							RV(vmseq_vx_u8m2_b4)(nextData3, '\r', vl2),
							RV(vmseq_vx_u8m2_b4)(nextData4, '\n', vl2),
							vl2
						);
						vbool4_t match4EqY = RV(vmand_mm_b4)(
							RV(vmseq_vx_u8m2_b4)(nextData3, '=', vl2),
							RV(vmseq_vx_u8m2_b4)(nextData4, 'y', vl2),
							vl2
						);
						
						// merge \r\n and =y matches
						vbool4_t match4End = RV(vmor_mm_b4)(match4Nl, match4EqY, vl2);
						// merge with \r\n.
						match4End = RV(vmand_mm_b4)(match4End, match2NlDot, vl2);
						// merge \r\n=y
						vbool4_t match3End = RV(vmand_mm_b4)(match1Nl, match3EqY, vl2);
						
						vbool4_t matchEnd = RV(vmor_mm_b4)(match4End, match3End, vl2);
						
						// combine match sequences
						if(LIKELIHOOD(0.001, RV(vcpop_m_b4)(matchEnd, vl2) > 0)) {
							// terminator found
							len += inpos;
							nextMask = decoder_set_nextMask<isRaw>(src+inpos, RV(vmv_x_s_u8m1_u8)(RV_VEC_CAST(4, 8m1, cmp)));
							break;
						}
					}
					
					// shift match2NlDot by 2
					cmp = RV(vmor_mm_b4)(cmp, mask_lshift<2>(match2NlDot, 0, vl2), vl2);
					
					vuint8mf4_t nextNlDot = RV(vslidedown_vx_u8mf4)(
#if !defined(__riscv_v_intrinsic) || __riscv_v_intrinsic < 13000
						RV(vmv_v_x_u8mf4)(0, vl2/8),
#endif
						RV_VEC_CAST(4, 8mf4, match2NlDot), vl2/8-1, vl2/8
					);
					nextNlDot = RV(vsrl_vx_u8mf4)(nextNlDot, 6, vl2/8);
					lfCompare = RV_vmerge_vxm_u8m2(RV(vmv_v_x_u8m2)('\n', vl2), '.', RV_MASK_CAST(4, nextNlDot), vl2);
				} else if(searchEnd) {
					if(LIKELIHOOD(0.001, RV(vcpop_m_b4)(match3EqY, vl2) != 0)) {
						vuint8m2_t nextData1 = RV(vslide1down_vx_u8m2)(data, nextWord, vl2);
						vbool4_t match1Lf = RV(vmseq_vx_u8m2_b4)(nextData1, '\n', vl2);
						vbool4_t matchEnd = RV(vmand_mm_b4)(RV(vmand_mm_b4)(match3EqY, cmpCr, vl2), match1Lf, vl2);
						if(LIKELIHOOD(0.001, RV(vcpop_m_b4)(matchEnd, vl2) > 0)) {
							len += inpos;
							nextMask = decoder_set_nextMask<isRaw>(src+inpos, RV(vmv_x_s_u8m1_u8)(RV_VEC_CAST(4, 8m1, cmp)));
							break;
						}
					}
					if(isRaw)
						lfCompare = RV(vmv_v_x_u8m2)('\n', vl2);
				} else if(isRaw) // no \r_. found
					lfCompare = RV(vmv_v_x_u8m2)('\n', vl2);
			}
			
			// the second character in an escape sequence
			vbool4_t cmpEqShift1 = mask_lshift<1>(cmpEq, escFirst, vl2);
			
			// a spec compliant encoder should never generate sequences: ==, =\n and =\r, but we'll handle them to be spec compliant
			// the yEnc specification requires any character following = to be unescaped, not skipped over, so we'll deal with that
			// firstly, check for invalid sequences of = (we assume that these are rare, as a spec compliant yEnc encoder should not generate these)
			if(LIKELIHOOD(0.0001, RV(vcpop_m_b4)(RV(vmand_mm_b4)(cmp, cmpEqShift1, vl2), vl2) != 0)) {
				// note: we assume that uintptr_t corresponds with __riscv_xlen
				#if __riscv_xlen == 64
				vuint64m1_t cmpEqW = RV_VEC_CAST(4, 64m1, cmpEq);
				#else
				vuint32m1_t cmpEqW = RV_VEC_CAST(4, 32m1, cmpEq);
				#endif
				size_t nextShiftDown = (vl2 > sizeof(uintptr_t)*8 ? sizeof(uintptr_t)*8 : vl2) - 1;
				size_t wvl = (vl2 + sizeof(uintptr_t)*8 -1) / (sizeof(uintptr_t)*8);
				for(size_t w=0; w<vl2; w+=sizeof(uintptr_t)*8) {
					// extract bottom word
					#if __riscv_xlen == 64
					uintptr_t maskW = RV(vmv_x_s_u64m1_u64)(cmpEqW);
					#else
					uintptr_t maskW = RV(vmv_x_s_u32m1_u32)(cmpEqW);
					#endif
					
					// fix it
					maskW = fix_eqMask<uintptr_t>(maskW & ~(uintptr_t)escFirst);
					uint8_t nextEscFirst = (maskW >> nextShiftDown) & 1;
					
					// shift it up (will be used for cmpEqShift1)
					maskW = (maskW<<1) | escFirst; // TODO: should this be done using mask_lshift<1> instead?
					escFirst = nextEscFirst;
					
					// slide the new value in from the top
					#if __riscv_xlen == 64
					cmpEqW = RV(vslide1down_vx_u64m1)(cmpEqW, maskW, wvl);
					#else
					cmpEqW = RV(vslide1down_vx_u32m1)(cmpEqW, maskW, wvl);
					#endif
				}
				cmpEqShift1 = RV_MASK_CAST(4, cmpEqW);
				cmp = RV(vmorn_mm_b4)(cmpEqShift1, cmp, vl2); // ~(cmp & ~cmpEqShift1)
			} else {
				// no invalid = sequences found - don't need to fix up cmpEq
				escFirst = RV(vcpop_m_b4)(RV(vmand_mm_b4)(cmpEq, lastBit, vl2), vl2);
				cmp = RV(vmnot_m_b4)(cmp, vl2);
			}
			data = RV(vsub_vv_u8m2)(data, RV_vmerge_vxm_u8m2(yencOffset, 64+42, cmpEqShift1, vl2), vl2);
			yencOffset = set_first_vu8(yencOffset, 42 | (escFirst<<6), vl2);
			
			// all that's left is to remove unwanted chars
#if defined(__riscv_v_intrinsic) && __riscv_v_intrinsic >= 13000
			data = RV(vcompress_vm_u8m2)(data, cmp, vl2);
#else
			data = RV(vcompress_vm_u8m2)(cmp, data, data, vl2);
#endif
			RV(vse8_v_u8m2)(outp, data, vl2);
			outp += RV(vcpop_m_b4)(cmp, vl2);
		} else {
			data = RV(vsub_vv_u8m2)(data, yencOffset, vl2);
			RV(vse8_v_u8m2)(outp, data, vl2);
			outp += vl2;
			// TODO: should these be done at LMUL=1? or, it might not be worth this strategy (e.g. do an additional OR instead), considering the cost of LMUL=2
			yencOffset = RV(vmv_v_x_u8m2)(42, vl2);
			if(isRaw) lfCompare = RV(vmv_v_x_u8m2)('\n', vl2);
			escFirst = 0;
		}
	}
}

size_t decoder_rvv_width() {
	return RV(vsetvlmax_e8m2)();
}

void decoder_set_rvv_funcs() {
	// TODO: VL sizing??
	_do_decode = &do_decode_simd<false, false, decoder_rvv_width, do_decode_rvv<false, false> >;
	_do_decode_raw = &do_decode_simd<true, false, decoder_rvv_width, do_decode_rvv<true, false> >;
	_do_decode_end_raw = &do_decode_simd<true, true, decoder_rvv_width, do_decode_rvv<true, true> >;
	_decode_isa = ISA_LEVEL_RVV;
}
#else
void decoder_set_rvv_funcs() {}
#endif
