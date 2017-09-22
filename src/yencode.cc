
#include <node.h>
#include <node_buffer.h>
#include <node_version.h>
#include <v8.h>
#include <stdlib.h>

#include "encoder.h"
#include "decoder.h"

using namespace v8;



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

#include "interface.h"
crcutil_interface::CRC* crc = NULL;

#ifdef X86_PCLMULQDQ_CRC
bool x86_cpu_has_pclmulqdq = false;
#include "crc_folding.c"
#else
#define x86_cpu_has_pclmulqdq false
#define crc_fold(a, b) 0
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

static inline void do_crc32_zeros(unsigned char crc1[4], size_t len) {
	if(!crc) {
		crc = crcutil_interface::CRC::Create(
			0xEDB88320, 0, 32, true, 0, 0, 0, 0, NULL);
		// instance never deleted... oh well...
	}
	crcutil_interface::UINT64 crc_ = 0;
	crc->CrcOfZeroes(len, &crc_);
	UNPACK_4(crc1, crc_);
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

#define YENC_MAX_SIZE(len, line_size) ( \
		  len * 2    /* all characters escaped */ \
		+ ((len*4) / line_size) /* newlines, considering the possibility of all chars escaped */ \
		+ 2 /* allocation for offset and that a newline may occur early */ \
		+ 32 /* allocation for XMM overflowing */ \
	)


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
		line_size = args[1]->ToInteger()->Value();
		if (line_size < 1) line_size = 128;
		if (args.Length() >= 3) {
			col = args[2]->ToInteger()->Value();
			if (col >= line_size) col = 0;
		}
	}
	
	// allocate enough memory to handle worst case requirements
	size_t dest_len = YENC_MAX_SIZE(arg_len, line_size);
	
	unsigned char *result = (unsigned char*) malloc(dest_len);
	size_t len = do_encode(line_size, &col, (const unsigned char*)node::Buffer::Data(args[0]), result, arg_len);
	result = (unsigned char*)realloc(result, len);
	//isolate->AdjustAmountOfExternalAllocatedMemory(len);
	args.GetReturnValue().Set( BUFFER_NEW((char*)result, len, free_buffer, (void*)len) );
}

static void EncodeTo(const FunctionCallbackInfo<Value>& args) {
	Isolate* isolate = Isolate::GetCurrent();
	HandleScope scope(isolate);
	
	if (args.Length() < 2 || !node::Buffer::HasInstance(args[0]) || !node::Buffer::HasInstance(args[1])) {
		isolate->ThrowException(Exception::Error(
			String::NewFromUtf8(isolate, "You must supply two Buffers"))
		);
		return;
	}
	
	size_t arg_len = node::Buffer::Length(args[0]);
	if (arg_len == 0) {
		args.GetReturnValue().Set( Integer::New(isolate, 0) );
		return;
	}
	
	int line_size = 128, col = 0;
	if (args.Length() >= 3) {
		// TODO: probably should throw errors instead of transparently fixing these...
		line_size = args[2]->ToInteger()->Value();
		if (line_size < 1) line_size = 128;
		if (args.Length() >= 4) {
			col = args[3]->ToInteger()->Value();
			if (col >= line_size) col = 0;
		}
	}
	
	// check that destination buffer has enough space
	size_t dest_len = YENC_MAX_SIZE(arg_len, line_size);
	if(node::Buffer::Length(args[1]) < dest_len) {
		args.GetReturnValue().Set( Integer::New(isolate, 0) );
		return;
	}
	
	size_t len = do_encode(line_size, &col, (const unsigned char*)node::Buffer::Data(args[0]), (unsigned char*)node::Buffer::Data(args[1]), arg_len);
	args.GetReturnValue().Set( Integer::New(isolate, len) );
}

static void Decode(const FunctionCallbackInfo<Value>& args) {
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
	
	unsigned char *result = (unsigned char*) malloc(arg_len);
	size_t len = do_decode((const unsigned char*)node::Buffer::Data(args[0]), result, arg_len, NULL, true);
	result = (unsigned char*)realloc(result, len);
	//isolate->AdjustAmountOfExternalAllocatedMemory(len);
	args.GetReturnValue().Set( BUFFER_NEW((char*)result, len, free_buffer, (void*)len) );
}

static void DecodeTo(const FunctionCallbackInfo<Value>& args) {
	Isolate* isolate = Isolate::GetCurrent();
	HandleScope scope(isolate);
	
	if (args.Length() < 2 || !node::Buffer::HasInstance(args[0]) || !node::Buffer::HasInstance(args[1])) {
		isolate->ThrowException(Exception::Error(
			String::NewFromUtf8(isolate, "You must supply two Buffers"))
		);
		return;
	}
	
	size_t arg_len = node::Buffer::Length(args[0]);
	if (arg_len == 0) {
		args.GetReturnValue().Set( Integer::New(isolate, 0) );
		return;
	}
	
	// check that destination buffer has enough space
	if(node::Buffer::Length(args[1]) < arg_len) {
		args.GetReturnValue().Set( Integer::New(isolate, 0) );
		return;
	}
	
	size_t len = do_decode((const unsigned char*)node::Buffer::Data(args[0]), (unsigned char*)node::Buffer::Data(args[1]), arg_len, NULL, true);
	args.GetReturnValue().Set( Integer::New(isolate, len) );
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

static void CRC32Zeroes(const FunctionCallbackInfo<Value>& args) {
	Isolate* isolate = Isolate::GetCurrent();
	HandleScope scope(isolate);
	
	if (args.Length() < 1) {
		isolate->ThrowException(Exception::Error(
			String::NewFromUtf8(isolate, "At least 1 argument required"))
		);
		return;
	}
	
	union crc32 crc1;
	size_t len = (size_t)args[0]->ToInteger()->Value();
	do_crc32_zeros(crc1.u8a, len);
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
		line_size = args[1]->ToInteger()->Value();
		if (line_size < 1) line_size = 128;
		if (args.Length() >= 3) {
			col = args[2]->ToInteger()->Value();
			if (col >= line_size) col = 0;
		}
	}
	
	// allocate enough memory to handle worst case requirements
	size_t dest_len = YENC_MAX_SIZE(arg_len, line_size);
	
	unsigned char *result = (unsigned char*) malloc(dest_len);
	size_t len = do_encode(line_size, &col, (const unsigned char*)node::Buffer::Data(args[0]), result, arg_len);
	result = (unsigned char*)realloc(result, len);
	V8::AdjustAmountOfExternalAllocatedMemory(len);
	ReturnBuffer(node::Buffer::New((char*)result, len, free_buffer, (void*)len), len, 0);
}

static Handle<Value> EncodeTo(const Arguments& args) {
	HandleScope scope;
	
	if (args.Length() < 2 || !node::Buffer::HasInstance(args[0]) || !node::Buffer::HasInstance(args[1])) {
		return ThrowException(Exception::Error(
			String::New("You must supply two Buffers"))
		);
	}
	
	size_t arg_len = node::Buffer::Length(args[0]);
	if (arg_len == 0) {
		return scope.Close(Integer::New(0));
	}
	
	int line_size = 128, col = 0;
	if (args.Length() >= 3) {
		// TODO: probably should throw errors instead of transparently fixing these...
		line_size = args[2]->ToInteger()->Value();
		if (line_size < 1) line_size = 128;
		if (args.Length() >= 4) {
			col = args[3]->ToInteger()->Value();
			if (col >= line_size) col = 0;
		}
	}
	
	// check that destination buffer has enough space
	size_t dest_len = YENC_MAX_SIZE(arg_len, line_size);
	if(node::Buffer::Length(args[1]) < dest_len) {
		return scope.Close(Integer::New(0));
	}
	
	size_t len = do_encode(line_size, &col, (const unsigned char*)node::Buffer::Data(args[0]), (unsigned char*)node::Buffer::Data(args[1]), arg_len);
	return scope.Close(Integer::New(len));
}

static Handle<Value> Decode(const Arguments& args) {
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
	
	unsigned char *result = (unsigned char*) malloc(arg_len);
	size_t len = do_decode((const unsigned char*)node::Buffer::Data(args[0]), result, arg_len, NULL, true);
	result = (unsigned char*)realloc(result, len);
	V8::AdjustAmountOfExternalAllocatedMemory(len);
	ReturnBuffer(node::Buffer::New((char*)result, len, free_buffer, (void*)len), len, 0);
}

static Handle<Value> DecodeTo(const Arguments& args) {
	HandleScope scope;
	
	if (args.Length() < 2 || !node::Buffer::HasInstance(args[0]) || !node::Buffer::HasInstance(args[1])) {
		return ThrowException(Exception::Error(
			String::New("You must supply two Buffers"))
		);
	}
	
	size_t arg_len = node::Buffer::Length(args[0]);
	if (arg_len == 0) {
		return scope.Close(Integer::New(0));
	}
	
	// check that destination buffer has enough space
	if(node::Buffer::Length(args[1]) < arg_len) {
		return scope.Close(Integer::New(0));
	}
	
	size_t len = do_decode((const unsigned char*)node::Buffer::Data(args[0]), (unsigned char*)node::Buffer::Data(args[1]), arg_len, NULL, true);
	return scope.Close(Integer::New(len));
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

static Handle<Value> CRC32Zeroes(const Arguments& args) {
	HandleScope scope;
	
	if (args.Length() < 1) {
		return ThrowException(Exception::Error(
			String::New("At least 1 argument required"))
		);
	}
	union crc32 crc1;
	size_t len = (size_t)args[0]->ToInteger()->Value();
	
	do_crc32_zeros(crc1.u8a, len);
	ReturnBuffer(node::Buffer::New((char*)crc1.u8a, 4), 4, 0);
}
#endif

void init(Handle<Object> target) {
	NODE_SET_METHOD(target, "encode", Encode);
	NODE_SET_METHOD(target, "encodeTo", EncodeTo);
	NODE_SET_METHOD(target, "decode", Decode);
	NODE_SET_METHOD(target, "decodeTo", DecodeTo);
	NODE_SET_METHOD(target, "crc32", CRC32);
	NODE_SET_METHOD(target, "crc32_combine", CRC32Combine);
	NODE_SET_METHOD(target, "crc32_zeroes", CRC32Zeroes);
	
	
#ifdef X86_PCLMULQDQ_CRC
	x86_cpu_has_pclmulqdq = (flags & 0x80202) == 0x80202; // SSE4.1 + SSSE3 + CLMUL
#endif
	
	encoder_init();
	decoder_init();
}

NODE_MODULE(yencode, init);
