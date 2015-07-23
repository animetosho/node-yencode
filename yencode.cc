
#include <node.h>
#include <node_buffer.h>
#include <v8.h>
#include <stdlib.h>

using namespace v8;

unsigned char escapeLUT[256];
uint16_t escapedLUT[256];
// combine two 8-bit ints into a 16-bit one
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define UINT16_PACK(a, b) (((a) << 8) | (b))
#else
#define UINT16_PACK(a, b) ((a) | ((b) << 8))
#endif

#ifdef __SSE2__
// may be different for MSVC
#include <x86intrin.h>
#define XMM_SIZE 16 /*== (signed int)sizeof(__m128i)*/
#endif

// runs at around 380MB/s on 2.4GHz Silvermont (worst: 125MB/s, best: 440MB/s)
static inline unsigned long do_encode(int line_size, int col, unsigned char* src, unsigned char* dest, unsigned long len) {
	unsigned char *p = dest;
	unsigned long i = 0;
	unsigned char c, ec;
	
	#ifdef __SSE2__
	#define MM_FILL_BYTES(b) _mm_set_epi8(b,b,b,b,b,b,b,b,b,b,b,b,b,b,b,b)
	__m128i mm_42 = MM_FILL_BYTES(42);
	__m128i mm_null = _mm_setzero_si128(),
	        mm_lf = MM_FILL_BYTES('\n'),
	        mm_cr = MM_FILL_BYTES('\r'),
	        mm_eq = MM_FILL_BYTES('=');
	uint32_t mmTmp[4] __attribute__((aligned(16)));
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
			
			int mask = _mm_movemask_epi8(cmp);
			if (mask != 0) {
				// special characters exist
				_mm_store_si128((__m128i*)mmTmp, data);
				#define DO_THING(n) \
					c = src[i+n], ec = escapeLUT[c]; \
					if (ec) \
						*(p+n) = ec; \
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
			col += p - sp;
		}
		if(sp && col >= line_size-1) {
			// we overflowed - need to revert and use slower method :(
			col -= p - sp;
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
			
			int mask = _mm_movemask_epi8(cmp);
			
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
				c = src[i+n], ec = escapeLUT[c]; \
				if (ec) \
					*(p++) = ec; \
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
			col += p - sp;
		}
		if(sp && col >= line_size-1) {
			// we overflowed - need to revert and use slower method :(
			col -= p - sp;
			p = sp;
			i -= 8;
		}
		#endif
		// handle remaining chars
		while(col < line_size-1) {
			c = src[i++], ec = escapeLUT[c];
			if (ec) {
				*(p++) = ec;
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
static inline unsigned long do_encode(int line_size, int col, unsigned char* src, unsigned char* dest, unsigned long len) {
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



void free_buffer(char* data, void* ignore) {
	// TODO: AdjustAmountOfExternalAllocatedMemory
	free(data);
}

// encode(str, line_size, col)
#ifndef NODE_010
// node 0.12 version
static void Encode(const FunctionCallbackInfo<Value>& args) {
	Isolate* isolate = Isolate::GetCurrent();
	
	if (args.Length() == 0 || !node::Buffer::HasInstance(args[0])) {
		isolate->ThrowException(Exception::Error(
			String::NewFromUtf8(isolate, "You must supply a Buffer"))
		);
		return;
	}
	
	unsigned long arg_len = node::Buffer::Length(args[0]);
	if (arg_len == 0) {
		args.GetReturnValue().Set( node::Buffer::New(isolate, 0) );
		return;
	}
	
	int line_size = 128, col = 0;
	if (args.Length() >= 2) {
		line_size = args[1]->ToInteger()->Value();
		if (line_size < 1) line_size = 128;
		if (args.Length() >= 3) {
			col = args[2]->ToInteger()->Value();
			if (col >= line_size) col = 0;
		}
	}
	
	// allocate enough memory to handle worst case requirements
	unsigned long dest_len =
		  arg_len * 2    // all characters escaped
		+ ((arg_len*4) / line_size) // newlines, considering the possibility of all chars escaped
		+ 2 // allocation for offset and that a newline may occur early
		+ 32 // allocation for XMM overflowing
	;
	
	unsigned char *result = (unsigned char*) malloc(dest_len);
	unsigned long len = do_encode(line_size, col, (unsigned char*)node::Buffer::Data(args[0]), result, arg_len);
	realloc(result, len);
	//isolate->AdjustAmountOfExternalAllocatedMemory(sizeof(node::Buffer) + len);
	args.GetReturnValue().Set( node::Buffer::New(isolate, (char*)result, len, free_buffer, NULL) );
}
#else
// node 0.10 version
// convert SlowBuffer to JS Buffer object and return it
#define ReturnBuffer(buffer, size, offset) {\
	Handle<Value> _tmp[] = {(buffer)->handle_, Integer::New(size), Integer::New(offset)}; \
	return scope.Close(Local<Function>::Cast(Context::GetCurrent()->Global()->Get(String::New("Buffer")))->NewInstance(3, _tmp)); \
}

static Handle<Value> Encode(const Arguments& args) {
	HandleScope scope;
	
	if (args.Length() == 0 || !node::Buffer::HasInstance(args[0])) {
		return ThrowException(Exception::Error(
			String::New("You must supply a Buffer"))
		);
	}
	
	unsigned long arg_len = node::Buffer::Length(args[0]);
	if (arg_len == 0) {
		ReturnBuffer(node::Buffer::New(0), 0, 0);
	}
	
	int line_size = 128, col = 0;
	if (args.Length() >= 2) {
		line_size = args[1]->ToInteger()->Value();
		if (line_size < 1) line_size = 128;
		if (args.Length() >= 3) {
			col = args[2]->ToInteger()->Value();
			if (col >= line_size) col = 0;
		}
	}
	
	// allocate enough memory to handle worst case requirements
	unsigned long dest_len =
		  arg_len * 2    // all characters escaped
		+ ((arg_len*4) / line_size) // newlines, considering the possibility of all chars escaped
		+ 2 // allocation for offset and that a newline may occur early
		+ 32 // allocation for XMM overflowing
	;
	
	unsigned char *result = (unsigned char*) malloc(dest_len);
	unsigned long len = do_encode(line_size, col, (unsigned char*)node::Buffer::Data(args[0]), result, arg_len);
	realloc(result, len);
	//V8::AdjustAmountOfExternalAllocatedMemory(sizeof(node::Buffer) + len);
	ReturnBuffer(node::Buffer::New((char*)result, len, free_buffer, NULL), len, 0);
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
}

NODE_MODULE(yencode, init);