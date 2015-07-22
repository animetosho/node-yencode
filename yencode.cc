
#include <node.h>
#include <node_buffer.h>
#include <v8.h>
#include <stdlib.h>

using namespace v8;

// TODO: alignment?
unsigned char escapeLUT[256];
uint16_t escapedLUT[256];
// combine two 8-bit ints into a 16-bit one
// TODO: support big endian
/*#if (!*(unsigned char *)&(uint16_t){1})
// big endian
#define UINT16_PACK(a, b) (((a) << 8) | (b))
#else*/
// little endian
#define UINT16_PACK(a, b) ((a) | ((b) << 8))
//#endif

#ifdef __SSE2__
// may be different for MSVC
#include <x86intrin.h>
#define XMM_SIZE 16 /*== (signed int)sizeof(__m128i)*/
#endif

// runs at around 320MB/s on 2.4GHz Silvermont
static inline unsigned long do_encode(int line_size, int col, unsigned char* src, unsigned char* dest, unsigned long len) {
	unsigned char *p = dest;
	unsigned long i = 0;
	unsigned char c, ec;
	
	#ifdef __SSE2__
	#define MM_FILL_BYTES(b) _mm_set_epi8(b,b,b,b,b,b,b,b,b,b,b,b,b,b,b,b)
	__m128i mm_42 = MM_FILL_BYTES(42);
	#ifdef __SSE4_2__
	__m128i mm_mask = _mm_set_epi8(0,'\n','\r','=', 0,0,0,0, 0,0,0,0, 0,0,0,0);
	#else
	__m128i mm_null = _mm_setzero_si128(),
	        mm_lf = MM_FILL_BYTES('\n'),
	        mm_cr = MM_FILL_BYTES('\r'),
	        mm_eq = MM_FILL_BYTES('=');
	#endif
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
		if (i >= len) goto end;
		
		skip_first_char:
		// main line
		#ifdef __SSE2__
		while (len-i-1 > XMM_SIZE && line_size-col-1 > XMM_SIZE*2) {
			unsigned char* sp = p;
			__m128i data = _mm_add_epi8(
				_mm_loadu_si128((__m128i *)(src + i)), // TODO: consider alignment
				mm_42
			);
			// search for special chars
			#ifdef __SSE4_2__
			int mask = _mm_cmpestri(data, sizeof(__m128i), mm_mask, 4, 0b0000);
			// may wish to use proper mask instead of an index to be consistent with SSE2 version
			#else
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
			#endif
			if (mask != 0) {
				// special characters exist
				// revert to slow algo for now
				#define DO_THING(n) \
					c = src[i+n], ec = escapeLUT[c]; \
					if (ec) \
						*(p++) = ec; \
					else { \
						*(uint16_t*)p = escapedLUT[c]; \
						p += 2; \
					}
				// iterations need to be == XMM_SIZE
				DO_THING(0);
				DO_THING(1);
				DO_THING(2);
				DO_THING(3);
				DO_THING(4);
				DO_THING(5);
				DO_THING(6);
				DO_THING(7);
				DO_THING(8);
				DO_THING(9);
				DO_THING(10);
				DO_THING(11);
				DO_THING(12);
				DO_THING(13);
				DO_THING(14);
				DO_THING(15);
				
			} else {
				_mm_storeu_si128((__m128i*)p, data);
				p += XMM_SIZE;
			}
			
			i += XMM_SIZE;
			col += p - sp;
		}
		#else
		while (len-i-1 > 8 && line_size-col-1 > 16) {
			// fast 8 cycle unrolled version
			unsigned char* sp = p;
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
		#endif
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
		
		if (i >= len) goto end;
		*(uint16_t*)p = UINT16_PACK('\r', '\n');
		p += 2;
	}
	
	end:
	return p - dest;
}


/*
// simple naive implementation - most yEnc encoders I've seen do something like the following
// runs at around 144MB/s on 2.4GHz Silvermont
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
	
	unsigned long dest_len = arg_len * 2 + (arg_len / 32) + 2;
	
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
	
	unsigned long dest_len = arg_len * 2 + (arg_len / 32) + 2;
	
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
