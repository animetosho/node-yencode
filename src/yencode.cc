
#include <node.h>
#include <node_buffer.h>
#include <node_version.h>
#include <v8.h>
#include <stdlib.h>
#include <string.h>

#include "encoder.h"
#include "decoder.h"
#include "crc.h"

using namespace v8;

union crc32 {
	uint32_t u32;
	unsigned char u8a[4];
};

static void free_buffer(char* data, void* _size) {
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

static inline size_t YENC_MAX_SIZE(size_t len, size_t line_size) {
	size_t ret = len * 2    /* all characters escaped */
		+ 2 /* allocation for offset and that a newline may occur early */
#if !defined(YENC_DISABLE_AVX256)
		+ 64 /* allocation for YMM overflowing */
#else
		+ 32 /* allocation for XMM overflowing */
#endif
	;
	/* add newlines, considering the possibility of all chars escaped */
	if(line_size == 128) // optimize common case
		return ret + 2 * (len >> 6);
	return ret + 2 * ((len*2) / line_size);
}



#if NODE_VERSION_AT_LEAST(0, 11, 0)
// for node 0.12.x
# define FUNC(name) static void name(const FunctionCallbackInfo<Value>& args)
# define FUNC_START \
	Isolate* isolate = args.GetIsolate(); \
	HandleScope scope(isolate)

# if NODE_VERSION_AT_LEAST(8, 0, 0)
#  define NEW_STRING(s) String::NewFromOneByte(isolate, (const uint8_t*)(s), NewStringType::kNormal).ToLocalChecked()
#  define RETURN_ERROR(e) { isolate->ThrowException(Exception::Error(String::NewFromOneByte(isolate, (const uint8_t*)(e), NewStringType::kNormal).ToLocalChecked())); return; }
#  define ARG_TO_INT(a) (a).As<Integer>()->Value()
#  define ARG_TO_BOOL(a) (a).As<Boolean>()->Value()
# else
#  define NEW_STRING(s) String::NewFromUtf8(isolate, s)
#  define RETURN_ERROR(e) { isolate->ThrowException(Exception::Error(String::NewFromUtf8(isolate, e))); return; }
#  define ARG_TO_INT(a) (a)->ToInteger()->Value()
#  define ARG_TO_BOOL(a) (a)->ToBoolean()->Value()
# endif
# define NEW_OBJECT Object::New(isolate)
# if NODE_VERSION_AT_LEAST(3, 0, 0) // iojs3
#  define NEW_BUFFER(...) node::Buffer::New(ISOLATE __VA_ARGS__).ToLocalChecked()
# else
#  define NEW_BUFFER(...) node::Buffer::New(ISOLATE __VA_ARGS__)
# endif

# define RETURN_VAL(v) { args.GetReturnValue().Set(v); return; }
# define RETURN_UNDEF return
# define ISOLATE isolate,
//# define MARK_EXT_MEM isolate->AdjustAmountOfExternalAllocatedMemory
# define MARK_EXT_MEM(x)

#else
// for node 0.10.x
#define FUNC(name) static Handle<Value> name(const Arguments& args)
#define FUNC_START HandleScope scope
#define NEW_STRING String::New
#define NEW_OBJECT Object::New()
#define NEW_BUFFER(...) Local<Object>::New(node::Buffer::New(ISOLATE __VA_ARGS__)->handle_)
#define ARG_TO_INT(a) (a)->ToInteger()->Value()
#define ARG_TO_BOOL(a) (a)->ToBoolean()->Value()

#define RETURN_ERROR(e) \
	return ThrowException(Exception::Error( \
		String::New(e)) \
	)
#define RETURN_VAL(v) return scope.Close(v)
#define RETURN_UNDEF RETURN_VAL( Undefined() )
#define ISOLATE
#define MARK_EXT_MEM V8::AdjustAmountOfExternalAllocatedMemory
#endif

#if NODE_VERSION_AT_LEAST(12, 0, 0)
# define SET_OBJ(obj, key, val) (obj)->Set(isolate->GetCurrentContext(), NEW_STRING(key), val).Check()
#else
# define SET_OBJ(obj, key, val) (obj)->Set(NEW_STRING(key), val)
#endif


// encode(str, line_size, col)
FUNC(Encode) {
	FUNC_START;
	
	if (args.Length() == 0 || !node::Buffer::HasInstance(args[0]))
		RETURN_ERROR("You must supply a Buffer");
	
	size_t arg_len = node::Buffer::Length(args[0]);
	if (arg_len == 0)
		RETURN_VAL(NEW_BUFFER(0));
	
	int line_size = 128, col = 0;
	if (args.Length() >= 2) {
		line_size = (int)ARG_TO_INT(args[1]);
		if (line_size == 0) line_size = 128; // allow this case
		if (line_size < 0)
			RETURN_ERROR("Line size must be at least 1 byte");
		if (args.Length() >= 3) {
			col = (int)ARG_TO_INT(args[2]);
			if (col > line_size || col < 0)
				RETURN_ERROR("Column offset cannot exceed the line size and cannot be negative");
			if (col == line_size) col = 0; // allow this case
		}
	}
	
	// allocate enough memory to handle worst case requirements
	size_t dest_len = YENC_MAX_SIZE(arg_len, line_size);
	
	unsigned char *result = (unsigned char*) malloc(dest_len);
	size_t len = do_encode(line_size, &col, (const unsigned char*)node::Buffer::Data(args[0]), result, arg_len, true);
	result = (unsigned char*)realloc(result, len);
	MARK_EXT_MEM(len);
	RETURN_VAL( NEW_BUFFER((char*)result, len, free_buffer, (void*)len) );
}

FUNC(EncodeTo) {
	FUNC_START;
	
	if (args.Length() < 2 || !node::Buffer::HasInstance(args[0]) || !node::Buffer::HasInstance(args[1]))
		RETURN_ERROR("You must supply two Buffers");
	
	size_t arg_len = node::Buffer::Length(args[0]);
	if (arg_len == 0)
		RETURN_VAL(Integer::New(ISOLATE 0));
	
	int line_size = 128, col = 0;
	if (args.Length() >= 3) {
		line_size = (int)ARG_TO_INT(args[2]);
		if (line_size == 0) line_size = 128; // allow this case
		if (line_size < 0)
			RETURN_ERROR("Line size must be at least 1 byte");
		if (args.Length() >= 4) {
			col = (int)ARG_TO_INT(args[3]);
			if (col > line_size || col < 0)
				RETURN_ERROR("Column offset cannot exceed the line size and cannot be negative");
			if (col == line_size) col = 0; // allow this case
		}
	}
	
	// check that destination buffer has enough space
	size_t dest_len = YENC_MAX_SIZE(arg_len, line_size);
	if(node::Buffer::Length(args[1]) < dest_len)
		RETURN_ERROR("Destination buffer does not have enough space (use `maxSize` to compute required space)");
	
	size_t len = do_encode(line_size, &col, (const unsigned char*)node::Buffer::Data(args[0]), (unsigned char*)node::Buffer::Data(args[1]), arg_len, true);
	RETURN_VAL( Integer::New(ISOLATE len) );
}

FUNC(EncodeIncr) {
	FUNC_START;
	
	if (args.Length() == 0 || !node::Buffer::HasInstance(args[0]))
		RETURN_ERROR("You must supply a Buffer");
	
	int line_size = 128, col = 0;
	bool allocResult = true;
	unsigned char* result;
	size_t arg_len = node::Buffer::Length(args[0]);
	if(args.Length() > 1) {
		int argp = 1;
		if(node::Buffer::HasInstance(args[1])) {
			// grab destination
			allocResult = false;
			// check that destination buffer has enough space
			size_t dest_len = YENC_MAX_SIZE(arg_len, line_size);
			if(node::Buffer::Length(args[1]) < dest_len)
				RETURN_ERROR("Destination buffer does not have enough space (use `maxSize` to compute required space)");
			result = (unsigned char*)node::Buffer::Data(args[1]);
			argp++;
		}
		if (args.Length() > argp) {
			line_size = (int)ARG_TO_INT(args[argp]);
			if (line_size == 0) line_size = 128; // allow this case
			if (line_size < 0)
				RETURN_ERROR("Line size must be at least 1 byte");
			argp++;
			if (args.Length() > argp) {
				col = (int)ARG_TO_INT(args[argp]);
				if (col > line_size || col < 0)
					RETURN_ERROR("Column offset cannot exceed the line size and cannot be negative");
				if (col == line_size) col = 0; // allow this case
			}
		}
	}
	
	Local<Object> ret = NEW_OBJECT;
	
	if (arg_len == 0) {
		SET_OBJ(ret, "written", Integer::New(ISOLATE 0));
		// TODO: set 'output'?
		SET_OBJ(ret, "col", Integer::New(ISOLATE col));
		RETURN_VAL( ret );
	}
	
	if(allocResult) {
		// allocate enough memory to handle worst case requirements
		size_t dest_len = YENC_MAX_SIZE(arg_len, line_size);
		result = (unsigned char*) malloc(dest_len);
	}
	
	size_t len = do_encode(line_size, &col, (const unsigned char*)node::Buffer::Data(args[0]), result, arg_len, false);
	
	SET_OBJ(ret, "written", Integer::New(ISOLATE len));
	if(allocResult) {
		result = (unsigned char*)realloc(result, len);
		SET_OBJ(ret, "output", NEW_BUFFER((char*)result, len, free_buffer, (void*)len));
		MARK_EXT_MEM(len);
	}
	SET_OBJ(ret, "col", Integer::New(ISOLATE col));
	RETURN_VAL( ret );
}

FUNC(Decode) {
	FUNC_START;
	
	if (args.Length() == 0 || !node::Buffer::HasInstance(args[0]))
		RETURN_ERROR("You must supply a Buffer");
	
	size_t arg_len = node::Buffer::Length(args[0]);
	if (arg_len == 0)
		RETURN_VAL( NEW_BUFFER(0) );
	
	bool isRaw = false;
	if (args.Length() > 1)
		isRaw = ARG_TO_BOOL(args[1]);
	
	unsigned char *result = (unsigned char*) malloc(arg_len);
	size_t len = (isRaw ? do_decode<true> : do_decode<false>)((const unsigned char*)node::Buffer::Data(args[0]), result, arg_len, NULL);
	result = (unsigned char*)realloc(result, len);
	MARK_EXT_MEM(len);
	RETURN_VAL( NEW_BUFFER((char*)result, len, free_buffer, (void*)len) );
}

FUNC(DecodeTo) {
	FUNC_START;
	
	if (args.Length() < 2 || !node::Buffer::HasInstance(args[0]) || !node::Buffer::HasInstance(args[1]))
		RETURN_ERROR("You must supply two Buffers");
	
	size_t arg_len = node::Buffer::Length(args[0]);
	if (arg_len == 0)
		RETURN_VAL( Integer::New(ISOLATE 0) );
	
	// check that destination buffer has enough space
	if(node::Buffer::Length(args[1]) < arg_len)
		RETURN_VAL( Integer::New(ISOLATE 0) );
	
	bool isRaw = false;
	if (args.Length() > 2)
		isRaw = ARG_TO_BOOL(args[2]);
	
	size_t len = (isRaw ? do_decode<true> : do_decode<false>)((const unsigned char*)node::Buffer::Data(args[0]), (unsigned char*)node::Buffer::Data(args[1]), arg_len, NULL);
	RETURN_VAL( Integer::New(ISOLATE len) );
}


FUNC(DecodeIncr) {
	FUNC_START;
	
	if (args.Length() == 0 || !node::Buffer::HasInstance(args[0]))
		RETURN_ERROR("You must supply a Buffer");
	
	size_t arg_len = node::Buffer::Length(args[0]);
	if (arg_len == 0)
		// handled properly in Javascript
		RETURN_UNDEF;
	
	YencDecoderState state = YDEC_STATE_CRLF;
	unsigned char *result = NULL;
	bool allocResult = true;
	if (args.Length() > 1) {
		state = (YencDecoderState)(ARG_TO_INT(args[1]));
		if (args.Length() > 2 && node::Buffer::HasInstance(args[2])) {
			if(node::Buffer::Length(args[2]) < arg_len)
				RETURN_ERROR("Destination buffer does not have enough space");
			result = (unsigned char*)node::Buffer::Data(args[2]);
			allocResult = false;
		}
	}
	
	const unsigned char* src = (const unsigned char*)node::Buffer::Data(args[0]);
	const unsigned char* sp = src;
#ifdef DBG_ALIGN_SOURCE
	void* newSrc = valloc(arg_len);
	memcpy(newSrc, src, arg_len);
	sp = (const unsigned char*)newSrc;
#endif
	
	if(allocResult) result = (unsigned char*) malloc(arg_len);
	unsigned char* dp = result;
	YencDecoderEnd ended = do_decode_end(&sp, &dp, arg_len, &state);
	size_t len = dp - result;
	if(allocResult) result = (unsigned char*)realloc(result, len);
	
#ifdef DBG_ALIGN_SOURCE
	free(newSrc);
#endif
	
	Local<Object> ret = NEW_OBJECT;
	SET_OBJ(ret, "read", Integer::New(ISOLATE sp - src));
	SET_OBJ(ret, "written", Integer::New(ISOLATE len));
	if(allocResult) {
		SET_OBJ(ret, "output", NEW_BUFFER((char*)result, len, free_buffer, (void*)len));
		MARK_EXT_MEM(len);
	}
	SET_OBJ(ret, "ended", Integer::New(ISOLATE (int)ended));
	SET_OBJ(ret, "state", Integer::New(ISOLATE state));
	RETURN_VAL( ret );
}


#if NODE_VERSION_AT_LEAST(3, 0, 0)
// for whatever reason, iojs 3 gives buffer corruption if you pass in a pointer without a free function
#define RETURN_CRC(x) do { \
	Local<Object> buff = NEW_BUFFER(4); \
	memcpy(node::Buffer::Data(buff), &x.u32, sizeof(uint32_t)); \
	args.GetReturnValue().Set( buff ); \
} while(0)
#else
#define RETURN_CRC(x) RETURN_VAL( NEW_BUFFER((char*)x.u8a, 4) )
#endif


// crc32(str, init)
FUNC(CRC32) {
	FUNC_START;
	
	if (args.Length() == 0 || !node::Buffer::HasInstance(args[0]))
		RETURN_ERROR("You must supply a Buffer");
	// TODO: support string args??
	
	union crc32 init;
	init.u32 = 0;
	if (args.Length() >= 2) {
		if (!node::Buffer::HasInstance(args[1]) || node::Buffer::Length(args[1]) != 4)
			RETURN_ERROR("Second argument must be a 4 byte buffer");
		memcpy(&init.u32, node::Buffer::Data(args[1]), sizeof(uint32_t));
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

FUNC(CRC32Combine) {
	FUNC_START;
	
	if (args.Length() < 3)
		RETURN_ERROR("At least 3 arguments required");
	if (!node::Buffer::HasInstance(args[0]) || node::Buffer::Length(args[0]) != 4
	|| !node::Buffer::HasInstance(args[1]) || node::Buffer::Length(args[1]) != 4)
		RETURN_ERROR("You must supply a 4 byte Buffer for the first two arguments");
	
	union crc32 crc1, crc2;
	size_t len = (size_t)ARG_TO_INT(args[2]);
	
	memcpy(&crc1.u32, node::Buffer::Data(args[0]), sizeof(uint32_t));
	memcpy(&crc2.u32, node::Buffer::Data(args[1]), sizeof(uint32_t));
	
	do_crc32_combine(crc1.u8a, crc2.u8a, len);
	RETURN_CRC(crc1);
}

FUNC(CRC32Zeroes) {
	FUNC_START;
	
	if (args.Length() < 1)
		RETURN_ERROR("At least 1 argument required");
	
	union crc32 crc1;
	if (args.Length() >= 2) {
		if (!node::Buffer::HasInstance(args[1]) || node::Buffer::Length(args[1]) != 4)
			RETURN_ERROR("Second argument must be a 4 byte buffer");
		memcpy(&crc1.u32, node::Buffer::Data(args[1]), sizeof(uint32_t));
	} else {
		crc1.u32 = 0;
	}
	size_t len = (size_t)ARG_TO_INT(args[0]);
	do_crc32_zeros(crc1.u8a, len);
	RETURN_CRC(crc1);
}

static void init_all() {
	encoder_init();
	decoder_init();
	crc_init();	
}

#if NODE_VERSION_AT_LEAST(10, 7, 0)
// signal context aware module for node if it supports it
# include <uv.h>
static uv_once_t init_once = UV_ONCE_INIT;
NODE_MODULE_INIT(/* exports, module, context */)
#else
void yencode_init(
# if NODE_VERSION_AT_LEAST(4, 0, 0)
 Local<Object> exports,
 Local<Value> module,
 void* priv
# else
 Handle<Object> exports
# endif
)
#endif
{
	NODE_SET_METHOD(exports, "encode", Encode);
	NODE_SET_METHOD(exports, "encodeTo", EncodeTo);
	NODE_SET_METHOD(exports, "encodeIncr", EncodeIncr);
	NODE_SET_METHOD(exports, "decode", Decode);
	NODE_SET_METHOD(exports, "decodeTo", DecodeTo);
	NODE_SET_METHOD(exports, "decodeIncr", DecodeIncr);
	NODE_SET_METHOD(exports, "crc32", CRC32);
	NODE_SET_METHOD(exports, "crc32_combine", CRC32Combine);
	NODE_SET_METHOD(exports, "crc32_zeroes", CRC32Zeroes);
	
#if NODE_VERSION_AT_LEAST(10, 7, 0)
	uv_once(&init_once, init_all);
#else
	init_all();
#endif
}

#if !NODE_VERSION_AT_LEAST(10, 7, 0)
NODE_MODULE(yencode, yencode_init);
#endif
