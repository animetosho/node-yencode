
#include <node.h>
#include <node_buffer.h>
#include <v8.h>
#include <stdlib.h>

using namespace v8;

// TODO: alignment?
unsigned char escapeLUT[256];
// combine two 8-bit ints into a 16-bit one
// TODO: support big endian
/*#if (!*(unsigned char *)&(uint16_t){1})
// big endian
#define UINT16_PACK(a, b) (((a) << 8) | (b))
#else*/
// little endian
#define UINT16_PACK(a, b) ((a) | ((b) << 8))
//#endif

// runs at around 225MB/s on 2.4GHz Silvermont
static inline unsigned long do_encode(int line_size, int col, unsigned char* src, unsigned char* dest, unsigned long len) {
	unsigned char *p = dest;
	
	for (unsigned long i = 0; i < len; i++) {
		unsigned char c = src[i], ec = escapeLUT[c];
		if (ec) {
			*(p++) = ec;
			col++;
		}
		else {
			if(((col > 0 && col < line_size) && (c == ' '+214 || c == '\t'+214)) || (col > 0 && c == '.'-42)) {
				*(p++) = (c + 42) & 0xFF;
				col++;
			} else {
				*(uint16_t*)p = UINT16_PACK('=', (c + 42+64) & 0xFF);
				p += 2;
				col += 2;
			}
		}
		if(col >= line_size) {
			*(uint16_t*)p = UINT16_PACK('\r', '\n');
			p += 2;
			col = 0;
		}
	}
	
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
				if(col > 0 && col < line_size) break;
			case '\0': case '\r': case '\n': case '=':
				*(p++) = '=';
				c += 64;
				col++;
		}
		*(p++) = c;
		col++;
		if(col >= line_size) {
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
		if (args.Length() >= 3)
			col = args[2]->ToInteger()->Value();
	}
	
	unsigned long dest_len = arg_len * 2 + (arg_len / 32);
	
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
		if (args.Length() >= 3)
			col = args[2]->ToInteger()->Value();
	}
	
	unsigned long dest_len = arg_len * 2 + (arg_len / 32);
	
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
	}
	escapeLUT[214 + '\0'] = 0;
	escapeLUT[214 + '\r'] = 0;
	escapeLUT[214 + '\n'] = 0;
	escapeLUT['=' - 42	] = 0;
	escapeLUT[214 + '\t'] = 0;
	escapeLUT[214 + ' ' ] = 0;
	escapeLUT['.' - 42	] = 0;
	
	NODE_SET_METHOD(target, "encode", Encode);
}

NODE_MODULE(yencode, init);
