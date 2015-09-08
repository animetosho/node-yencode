
#include <node.h>
#include <node_buffer.h>
#include <node_version.h>
#include <v8.h>
#include <stdlib.h>

using namespace v8;

// MSVC compatibility
#if (defined(_M_IX86_FP) && _M_IX86_FP == 2) || defined(_M_X64)
#define __SSE2__ 1
#endif
#ifdef _MSC_VER
#define __BYTE_ORDER__ 1234
#define __ORDER_BIG_ENDIAN__ 4321
#include <intrin.h>
#endif

#if defined(__x86_64__) || defined(__i386__) || defined(_MSC_VER)
#define _IS_X86 1
#endif

static unsigned char escapeLUT[256]; // whether or not the character is critical
static uint16_t escapedLUT[256]; // escaped sequences for characters that need escaping
// combine two 8-bit ints into a 16-bit one
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define UINT16_PACK(a, b) (((a) << 8) | (b))
#else
#define UINT16_PACK(a, b) ((a) | ((b) << 8))
#endif

#ifdef __SSE2__
#include <emmintrin.h>
#define XMM_SIZE 16 /*== (signed int)sizeof(__m128i)*/

#ifdef _MSC_VER
#define ALIGN_16(v) __declspec(align(16)) v
#else
#define ALIGN_16(v) v __attribute__((aligned(16)))
#endif

#endif

// runs at around 380MB/s on 2.4GHz Silvermont (worst: 125MB/s, best: 440MB/s)
static inline size_t do_encode(int line_size, int col, const unsigned char* src, unsigned char* dest, size_t len) {
	unsigned char *p = dest; // destination pointer
	unsigned long i = 0; // input position
	unsigned char c, escaped; // input character; escaped input character
	
	#ifdef __SSE2__
	#define MM_FILL_BYTES(b) _mm_set_epi8(b,b,b,b,b,b,b,b,b,b,b,b,b,b,b,b)
	__m128i mm_42 = MM_FILL_BYTES(42);
	__m128i mm_null = _mm_setzero_si128(),
	        mm_lf = MM_FILL_BYTES('\n'),
	        mm_cr = MM_FILL_BYTES('\r'),
	        mm_eq = MM_FILL_BYTES('=');
	ALIGN_16(uint32_t mmTmp[4]);
	#endif
	
	if (col > 0) goto skip_first_char;
	while(1) {
		// first char in line
		c = src[i++];
		if (escapedLUT[c]) {
			*(uint16_t*)p = escapedLUT[c];
			p += 2;
			col = 2;
		} else {
			*(p++) = escapeLUT[c];
			col = 1;
		}
		if (i >= len) break;
		
		skip_first_char:
		unsigned char* sp = NULL;
		// main line
		#ifdef __SSE2__
		while (len-i-1 > XMM_SIZE && line_size-col-1 > XMM_SIZE) {
			sp = p;
			__m128i data = _mm_add_epi8(
				_mm_loadu_si128((__m128i *)(src + i)), // probably not worth the effort to align
				mm_42
			);
			// search for special chars
			__m128i cmp = _mm_or_si128(
				_mm_or_si128(
					_mm_or_si128(
						_mm_cmpeq_epi8(data, mm_null),
						_mm_cmpeq_epi8(data, mm_lf)
					),
					_mm_cmpeq_epi8(data, mm_cr)
				),
				_mm_cmpeq_epi8(data, mm_eq)
			);
			
			unsigned int mask = _mm_movemask_epi8(cmp);
			if (mask != 0) {
				// special characters exist
				_mm_store_si128((__m128i*)mmTmp, data);
				#define DO_THING(n) \
					c = src[i+n], escaped = escapeLUT[c]; \
					if (escaped) \
						*(p+n) = escaped; \
					else { \
						*(uint16_t*)(p+n) = escapedLUT[c]; \
						p++; \
					}
				#define DO_THING_4(n) \
					if(mask & (0xF << n)) { \
						DO_THING(n); \
						DO_THING(n+1); \
						DO_THING(n+2); \
						DO_THING(n+3); \
					} else { \
						*(uint32_t*)(p+n) = mmTmp[n>>2]; \
					}
				DO_THING_4(0);
				DO_THING_4(4);
				DO_THING_4(8);
				DO_THING_4(12);
			} else {
				_mm_storeu_si128((__m128i*)p, data);
			}
			
			p += XMM_SIZE;
			i += XMM_SIZE;
			col += (int)(p - sp);
		}
		if(sp && col >= line_size-1) {
			// we overflowed - need to revert and use slower method :(
			col -= (int)(p - sp);
			p = sp;
			i -= XMM_SIZE;
		}
		
		/*
		// NOTE: this code doesn't work
		// perhaps try moving remaining parts w/ 32-bit copies?  doesn't seem to have any improvement :|
		if (len-i-1 > XMM_SIZE && line_size-col-1 > 8) {
			__m128i data = _mm_add_epi8(
				_mm_loadu_si128((__m128i *)(src + i)),
				mm_42
			);
			_mm_store_si128((__m128i*)mmTmp, data);
			// search for special chars
			__m128i cmp = _mm_or_si128(
				_mm_or_si128(
					_mm_or_si128(
						_mm_cmpeq_epi8(data, mm_null),
						_mm_cmpeq_epi8(data, mm_lf)
					),
					_mm_cmpeq_epi8(data, mm_cr)
				),
				_mm_cmpeq_epi8(data, mm_eq)
			);
			
			unsigned int mask = _mm_movemask_epi8(cmp);
			
			#define DO_THING_4b(n) \
				if(line_size - col - 1 > n+8) { \
					if(mask & (0xF << (n/4))) { \
						sp = p; \
						DO_THING(n); \
						DO_THING(n+1); \
						DO_THING(n+2); \
						DO_THING(n+3); \
						col += p - sp; \
					} else { \
						*(uint32_t*)(p + n) = mmTmp[n/4]; \
					}
			#define DO_THING_4c(n) \
				} else { \
					i += n; \
					p += n; \
					col += n; \
				}
			DO_THING_4b(0);
				DO_THING_4b(4);
					DO_THING_4b(8);
						DO_THING_4b(12);
							i += 16;
							p += 16;
							col += 16;
						DO_THING_4c(12);
					DO_THING_4c(8);
				DO_THING_4c(4);
			DO_THING_4c(0);
		}
		*/
		#else
		while (len-i-1 > 8 && line_size-col-1 > 8) {
			// 8 cycle unrolled version
			sp = p;
			#define DO_THING(n) \
				c = src[i+n], escaped = escapeLUT[c]; \
				if (escaped) \
					*(p++) = escaped; \
				else { \
					*(uint16_t*)p = escapedLUT[c]; \
					p += 2; \
				}
			DO_THING(0);
			DO_THING(1);
			DO_THING(2);
			DO_THING(3);
			DO_THING(4);
			DO_THING(5);
			DO_THING(6);
			DO_THING(7);
			
			i += 8;
			col += (int)(p - sp);
		}
		if(sp && col >= line_size-1) {
			// we overflowed - need to revert and use slower method :(
			col -= (int)(p - sp);
			p = sp;
			i -= 8;
		}
		#endif
		// handle remaining chars
		while(col < line_size-1) {
			c = src[i++], escaped = escapeLUT[c];
			if (escaped) {
				*(p++) = escaped;
				col++;
			}
			else {
				*(uint16_t*)p = escapedLUT[c];
				p += 2;
				col += 2;
			}
			if (i >= len) goto end;
		}
		
		// last line char
		if(col < line_size) { // this can only be false if the last character was an escape sequence (or line_size is horribly small), in which case, we don't need to handle space/tab cases
			c = src[i++];
			if (escapedLUT[c] && c != '.'-42) {
				*(uint16_t*)p = escapedLUT[c];
				p += 2;
			} else {
				*(p++) = escapeLUT[c];
			}
		}
		
		if (i >= len) break;
		*(uint16_t*)p = UINT16_PACK('\r', '\n');
		p += 2;
	}
	
	end:
	return p - dest;
}


/*
// simple naive implementation - most yEnc encoders I've seen do something like the following
// runs at around 145MB/s on 2.4GHz Silvermont (worst: 135MB/s, best: 158MB/s)
static inline unsigned long do_encode(int line_size, int col, const unsigned char* src, unsigned char* dest, size_t len) {
	unsigned char *p = dest;
	
	for (unsigned long i = 0; i < len; i++) {
		unsigned char c = (src[i] + 42) & 0xFF;
		switch(c) {
			case '.':
				if(col > 0) break;
			case '\t': case ' ':
				if(col > 0 && col < line_size-1) break;
			case '\0': case '\r': case '\n': case '=':
				*(p++) = '=';
				c += 64;
				col++;
		}
		*(p++) = c;
		col++;
		if(col >= line_size && i+1 < len) {
			*(uint16_t*)p = UINT16_PACK('\r', '\n');
			p += 2;
			col = 0;
		}
	}
	
	return p - dest;
}
*/


union crc32 {
	uint32_t u32;
	unsigned char u8a[4];
};

#define PACK_4(arr) (((uint_fast32_t)arr[0] << 24) | ((uint_fast32_t)arr[1] << 16) | ((uint_fast32_t)arr[2] << 8) | (uint_fast32_t)arr[3])
#define UNPACK_4(arr, val) { \
	arr[0] = (unsigned char)(val >> 24) & 0xFF; \
	arr[1] = (unsigned char)(val >> 16) & 0xFF; \
	arr[2] = (unsigned char)(val >>  8) & 0xFF; \
	arr[3] = (unsigned char)val & 0xFF; \
}

#include "./crcutil-1.0/examples/interface.h"
crcutil_interface::CRC* crc = NULL;

#if defined(_IS_X86) && (!defined(_MSC_VER) || _MSC_VER >= 1600)
bool x86_cpu_has_pclmulqdq = false;
#define X86_PCLMULQDQ_CRC
#include "crc_folding.c"
#else
#define x86_cpu_has_pclmulqdq false
#define crc_fold() 0
#endif

static inline void do_crc32(const void* data, size_t length, unsigned char out[4]) {
	// if we have the pclmulqdq instruction, use the insanely fast folding method
	if(x86_cpu_has_pclmulqdq) {
		uint32_t tmp = crc_fold((const unsigned char*)data, (long)length);
		UNPACK_4(out, tmp);
	} else {
		if(!crc) {
			crc = crcutil_interface::CRC::Create(
				0xEDB88320, 0, 32, true, 0, 0, 0, 0, NULL);
			// instance never deleted... oh well...
		}
		crcutil_interface::UINT64 tmp = 0;
		crc->Compute(data, length, &tmp);
		UNPACK_4(out, tmp);
	}
}

crcutil_interface::CRC* crcI = NULL;
static inline void do_crc32_incremental(const void* data, size_t length, unsigned char init[4]) {
	if(!crcI) {
		crcI = crcutil_interface::CRC::Create(
			0xEDB88320, 0, 32, false, 0, 0, 0, 0, NULL);
		// instance never deleted... oh well...
	}
	
	if(x86_cpu_has_pclmulqdq) {
		// TODO: think of a nicer way to do this than a combine
		crcutil_interface::UINT64 crc1_ = PACK_4(init);
		crcutil_interface::UINT64 crc2_ = crc_fold((const unsigned char*)data, (long)length);
		crcI->Concatenate(crc2_, 0, length, &crc1_);
		UNPACK_4(init, crc1_);
	} else {
		crcutil_interface::UINT64 tmp = PACK_4(init) ^ 0xffffffff;
		crcI->Compute(data, length, &tmp);
		tmp ^= 0xffffffff;
		UNPACK_4(init, tmp);
	}
}

static inline void do_crc32_combine(unsigned char crc1[4], const unsigned char crc2[4], size_t len2) {
	if(!crc) {
		crc = crcutil_interface::CRC::Create(
			0xEDB88320, 0, 32, true, 0, 0, 0, 0, NULL);
		// instance never deleted... oh well...
	}
	crcutil_interface::UINT64 crc1_ = PACK_4(crc1), crc2_ = PACK_4(crc2);
	crc->Concatenate(crc2_, 0, len2, &crc1_);
	UNPACK_4(crc1, crc1_);
}

void free_buffer(char* data, void* _size) {
#if !NODE_VERSION_AT_LEAST(0, 11, 0)
	int size = (int)(size_t)_size;
	V8::AdjustAmountOfExternalAllocatedMemory(-size);
#endif
	//Isolate::GetCurrent()->AdjustAmountOfExternalAllocatedMemory(-size);
	free(data);
}

// TODO: encode should return col num for incremental processing
//       line limit + return input consumed
//       async processing?

// encode(str, line_size, col)
// crc32(str, init)
#if NODE_VERSION_AT_LEAST(0, 11, 0)

#if NODE_VERSION_AT_LEAST(3, 0, 0) // iojs3
#define BUFFER_NEW(...) node::Buffer::New(isolate, __VA_ARGS__).ToLocalChecked()
#else
#define BUFFER_NEW(...) node::Buffer::New(isolate, __VA_ARGS__)
#endif

// node 0.12 version
static void Encode(const FunctionCallbackInfo<Value>& args) {
	Isolate* isolate = Isolate::GetCurrent();
	HandleScope scope(isolate);
	
	if (args.Length() == 0 || !node::Buffer::HasInstance(args[0])) {
		isolate->ThrowException(Exception::Error(
			String::NewFromUtf8(isolate, "You must supply a Buffer"))
		);
		return;
	}
	
	size_t arg_len = node::Buffer::Length(args[0]);
	if (arg_len == 0) {
		args.GetReturnValue().Set( BUFFER_NEW(0) );
		return;
	}
	
	int line_size = 128, col = 0;
	if (args.Length() >= 2) {
		// TODO: probably should throw errors instead of transparently fixing these...
		line_size = args[1]->ToInt32()->Value();
		if (line_size < 1) line_size = 128;
		if (args.Length() >= 3) {
			col = args[2]->ToInt32()->Value();
			if (col >= line_size) col = 0;
		}
	}
	
	// allocate enough memory to handle worst case requirements
	size_t dest_len =
		  arg_len * 2    // all characters escaped
		+ ((arg_len*4) / line_size) // newlines, considering the possibility of all chars escaped
		+ 2 // allocation for offset and that a newline may occur early
		+ 32 // allocation for XMM overflowing
	;
	
	unsigned char *result = (unsigned char*) malloc(dest_len);
	size_t len = do_encode(line_size, col, (const unsigned char*)node::Buffer::Data(args[0]), result, arg_len);
	result = (unsigned char*)realloc(result, len);
	//isolate->AdjustAmountOfExternalAllocatedMemory(len);
	args.GetReturnValue().Set( BUFFER_NEW((char*)result, len, free_buffer, (void*)len) );
}

#if NODE_VERSION_AT_LEAST(3, 0, 0)
// for whatever reason, iojs 3 gives buffer corruption if you pass in a pointer without a free function
#define RETURN_CRC(x) do { \
	Local<Object> buff = BUFFER_NEW(4); \
	*(uint32_t*)node::Buffer::Data(buff) = x.u32; \
	args.GetReturnValue().Set( buff ); \
} while(0)
#else
#define RETURN_CRC(x) args.GetReturnValue().Set( BUFFER_NEW((char*)x.u8a, 4) )
#endif

static void CRC32(const FunctionCallbackInfo<Value>& args) {
	Isolate* isolate = Isolate::GetCurrent();
	HandleScope scope(isolate);
	
	if (args.Length() == 0 || !node::Buffer::HasInstance(args[0])) {
		isolate->ThrowException(Exception::Error(
			String::NewFromUtf8(isolate, "You must supply a Buffer"))
		);
		return;
	}
	// TODO: support string args??
	
	union crc32 init;
	init.u32 = 0;
	if (args.Length() >= 2) {
		if (!node::Buffer::HasInstance(args[1]) || node::Buffer::Length(args[1]) != 4) {
			isolate->ThrowException(Exception::Error(
				String::NewFromUtf8(isolate, "Second argument must be a 4 byte buffer"))
			);
			return;
		}
		init.u32 = *(uint32_t*)node::Buffer::Data(args[1]);
		do_crc32_incremental(
			(const void*)node::Buffer::Data(args[0]),
			node::Buffer::Length(args[0]),
			init.u8a
		);
	} else {
		do_crc32(
			(const void*)node::Buffer::Data(args[0]),
			node::Buffer::Length(args[0]),
			init.u8a
		);
	}
	RETURN_CRC(init);
}

static void CRC32Combine(const FunctionCallbackInfo<Value>& args) {
	Isolate* isolate = Isolate::GetCurrent();
	HandleScope scope(isolate);
	
	if (args.Length() < 3) {
		isolate->ThrowException(Exception::Error(
			String::NewFromUtf8(isolate, "At least 3 arguments required"))
		);
		return;
	}
	if (!node::Buffer::HasInstance(args[0]) || node::Buffer::Length(args[0]) != 4
	|| !node::Buffer::HasInstance(args[1]) || node::Buffer::Length(args[1]) != 4) {
		isolate->ThrowException(Exception::Error(
			String::NewFromUtf8(isolate, "You must supply a 4 byte Buffer for the first two arguments"))
		);
		return;
	}
	
	union crc32 crc1, crc2;
	size_t len = (size_t)args[2]->ToInteger()->Value();
	
	crc1.u32 = *(uint32_t*)node::Buffer::Data(args[0]);
	crc2.u32 = *(uint32_t*)node::Buffer::Data(args[1]);
	
	do_crc32_combine(crc1.u8a, crc2.u8a, len);
	RETURN_CRC(crc1);
}
#else
// node 0.10 version
#define ReturnBuffer(buffer, size, offset) return scope.Close(Local<Object>::New((buffer)->handle_))

static Handle<Value> Encode(const Arguments& args) {
	HandleScope scope;
	
	if (args.Length() == 0 || !node::Buffer::HasInstance(args[0])) {
		return ThrowException(Exception::Error(
			String::New("You must supply a Buffer"))
		);
	}
	
	size_t arg_len = node::Buffer::Length(args[0]);
	if (arg_len == 0) {
		ReturnBuffer(node::Buffer::New(0), 0, 0);
	}
	
	int line_size = 128, col = 0;
	if (args.Length() >= 2) {
		// TODO: probably should throw errors instead of transparently fixing these...
		line_size = args[1]->ToInt32()->Value();
		if (line_size < 1) line_size = 128;
		if (args.Length() >= 3) {
			col = args[2]->ToInt32()->Value();
			if (col >= line_size) col = 0;
		}
	}
	
	// allocate enough memory to handle worst case requirements
	size_t dest_len =
		  arg_len * 2    // all characters escaped
		+ ((arg_len*4) / line_size) // newlines, considering the possibility of all chars escaped
		+ 2 // allocation for offset and that a newline may occur early
		+ 32 // allocation for XMM overflowing
	;
	
	unsigned char *result = (unsigned char*) malloc(dest_len);
	size_t len = do_encode(line_size, col, (const unsigned char*)node::Buffer::Data(args[0]), result, arg_len);
	result = (unsigned char*)realloc(result, len);
	V8::AdjustAmountOfExternalAllocatedMemory(len);
	ReturnBuffer(node::Buffer::New((char*)result, len, free_buffer, (void*)len), len, 0);
}

static Handle<Value> CRC32(const Arguments& args) {
	HandleScope scope;
	
	if (args.Length() == 0 || !node::Buffer::HasInstance(args[0])) {
		return ThrowException(Exception::Error(
			String::New("You must supply a Buffer"))
		);
	}
	// TODO: support string args??
	
	union crc32 init;
	init.u32 = 0;
	if (args.Length() >= 2) {
		if (!node::Buffer::HasInstance(args[1]) || node::Buffer::Length(args[1]) != 4)
			return ThrowException(Exception::Error(
				String::New("Second argument must be a 4 byte buffer"))
			);
		
		init.u32 = *(uint32_t*)node::Buffer::Data(args[1]);
		do_crc32_incremental(
			(const void*)node::Buffer::Data(args[0]),
			node::Buffer::Length(args[0]),
			init.u8a
		);
	} else {
		do_crc32(
			(const void*)node::Buffer::Data(args[0]),
			node::Buffer::Length(args[0]),
			init.u8a
		);
	}
	ReturnBuffer(node::Buffer::New((char*)init.u8a, 4), 4, 0);
}

static Handle<Value> CRC32Combine(const Arguments& args) {
	HandleScope scope;
	
	if (args.Length() < 3) {
		return ThrowException(Exception::Error(
			String::New("At least 3 arguments required"))
		);
	}
	if (!node::Buffer::HasInstance(args[0]) || node::Buffer::Length(args[0]) != 4
	|| !node::Buffer::HasInstance(args[1]) || node::Buffer::Length(args[1]) != 4) {
		return ThrowException(Exception::Error(
			String::New("You must supply a 4 byte Buffer for the first two arguments"))
		);
	}
	
	union crc32 crc1, crc2;
	size_t len = (size_t)args[2]->ToInteger()->Value();
	
	crc1.u32 = *(uint32_t*)node::Buffer::Data(args[0]);
	crc2.u32 = *(uint32_t*)node::Buffer::Data(args[1]);
	
	do_crc32_combine(crc1.u8a, crc2.u8a, len);
	ReturnBuffer(node::Buffer::New((char*)crc1.u8a, 4), 4, 0);
}
#endif

void init(Handle<Object> target) {
	for (int i=0; i<256; i++) {
		escapeLUT[i] = (i+42) & 0xFF;
		escapedLUT[i] = 0;
	}
	escapeLUT[214 + '\0'] = 0;
	escapeLUT[214 + '\r'] = 0;
	escapeLUT[214 + '\n'] = 0;
	escapeLUT['=' - 42	] = 0;
	
	escapedLUT[214 + '\0'] = UINT16_PACK('=', '\0'+64);
	escapedLUT[214 + '\r'] = UINT16_PACK('=', '\r'+64);
	escapedLUT[214 + '\n'] = UINT16_PACK('=', '\n'+64);
	escapedLUT['=' - 42	] =  UINT16_PACK('=', '='+64);
	escapedLUT[214 + '\t'] = UINT16_PACK('=', '\t'+64);
	escapedLUT[214 + ' ' ] = UINT16_PACK('=', ' '+64);
	escapedLUT['.' - 42	] =  UINT16_PACK('=', '.'+64);
	NODE_SET_METHOD(target, "encode", Encode);
	NODE_SET_METHOD(target, "crc32", CRC32);
	NODE_SET_METHOD(target, "crc32_combine", CRC32Combine);
	
	
#ifdef _MSC_VER
	#if _MSC_VER >= 1600 // doesn't work on MSVC 2008, maybe works on 2010?
	int cpuInfo[4];
	__cpuid(cpuInfo, 1);
	x86_cpu_has_pclmulqdq = (cpuInfo[2] & 0x80202) == 0x80202; // SSE4.1 + SSSE3 + CLMUL
	#endif
#elif defined(_IS_X86)
	// conveniently stolen from zlib-ng
	uint32_t flags;

	__asm__ __volatile__ (
		"cpuid"
	: "=c" (flags)
	: "a" (1)
	: "%edx", "%ebx"
	);
	x86_cpu_has_pclmulqdq = (flags & 0x80202) == 0x80202; // SSE4.1 + SSSE3 + CLMUL
#endif
}

NODE_MODULE(yencode, init);
