This module provides a very fast (as in gigabytes per second), compiled implementation of [yEnc](http://www.yenc.org/yenc-draft.1.3.txt) and CRC32 (IEEE) hash calculation for node.js. The implementations are optimised for speed, and uses x86/ARM SIMD optimised routines if such CPU features are available.

This module should be 1-2 orders of magnitude faster than pure Javascript versions.

Features:
---------

-   fast raw yEnc encoding with the ability to specify line length. A single
    thread can achieve \>450MB/s on a Raspberry Pi 3, or \>5GB/s on a Core-i
    series CPU.
-   fast yEnc decoding, with and without NNTP layer dot unstuffing.
    A single thread can achieve \>300MB/s on a Raspberry Pi 3, or \>4.5GB/s on a Core-i series CPU.
-   SIMD optimised encoding and decoding routines, which can use ARMv7 NEON, ARMv8 ASIMD or the
    following x86 CPU features when available (with dynamic dispatch): SSE2,
    SSSE3, AVX, AVX2, AVX512-BW (128/256-bit), AVX512-VBMI2
-   full yEnc encoding for single and multi-part posts, according to the
    [version 1.3 specifications](http://www.yenc.org/yenc-draft.1.3.txt)
-   full yEnc decoding of posts
-   fast compiled CRC32 implementation via
    [crcutil](https://code.google.com/p/crcutil/) or [PCLMULQDQ
    instruction](http://www.intel.com/content/dam/www/public/us/en/documents/white-papers/fast-crc-computation-generic-polynomials-pclmulqdq-paper.pdf)
    (if available) or ARMv8’s CRC instructions, with incremental support
    (\>1GB/s on a low power Atom/ARM CPU, \>15GB/s on a modern Intel CPU)
-   ability to combine two CRC32 hashes into one (useful for amalgamating
    *pcrc32s* into a *crc32* for yEnc), as well as quickly compute the CRC32 of a sequence of null bytes
-   eventually may support incremental processing (algorithms internally support
    it, they’re just not exposed to the Javascript interface)
-   [context awareness](https://nodejs.org/api/addons.html#addons_context_aware_addons) (NodeJS 10.7.0 or later), enabling use within [worker threads](https://nodejs.org/api/worker_threads.html)
-   supports NodeJS 0.10.x to 12.x.x and beyond

Installing
==========

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
npm install yencode
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Or you can download the package and run

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
npm install -g node-gyp # if you don't have it already
node-gyp rebuild
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Note, Windows builds are always compiled with SSE2 support. If you can’t have
this, delete all instances of `"msvs_settings": {"VCCLCompilerTool":
{"EnableEnhancedInstructionSet": "2"}},` in *binding.gyp* before compiling.

Redistributable Builds
----------------------

By default, non-Windows builds are built with `-march=native` flag, which means
that the compiler will optimise the build for the CPU of the build machine. If
you’re looking to run built binaries elsewhere, this may be undesirable. To make
builds redistributable, replace `"enable_native_tuning%": 1,` from
*binding.gyp* with `"enable_native_tuning%": 0,` and (re)compile.

Windows builds are redistributable by default as MSVC doesn’t support native CPU targeting.

Older Compilers
---------------

Unfortunately node-gyp doesn’t provide much in the way of compiler version
detection. As such, the build script assumes the compiler isn’t too old and
supports AVX. If you are trying to build using an older compiler (such as Visual
Studio 2008), you may need to edit *binding.gyp* to remove AVX related options,
such as `-mavx`, `-mpopcnt` and `"EnableEnhancedInstructionSet": "3"`

## GCC 9 on ARMv7

I’ve noticed that GCC 9.2.1 (which may include GCC 9.x.x), with ARMv7 targets, seems to generate code which crashes. I‘m not yet sure on the reason, but have not seen the issue with GCC 8.3.0 or Clang, or GCC 9 with ARMv8 targets.

API
===

Note, on node v0.10, functions returning a Buffer actually return a *SlowBuffer*
object, similar to how node’s crypto functions work.

Buffer encode(Buffer data, int line_size=128, int column_offset=0)
------------------------------------------------------------------

Performs raw yEnc encoding on *data* returning the result.  
*line_size* controls how often to insert newlines (note, as per yEnc
specifications, it's possible to have lines longer than this length)  
*column_offset* is the column of the first character

int encodeTo(Buffer data, Buffer output, int line_size=128, int column_offset=0)
--------------------------------------------------------------------------------

Same as above, but instead of returning a Buffer, writes it to the supplied
*output* Buffer. Returns the length of the encoded data.  
Note that the *output* Buffer must be at least large enough to hold the largest
possible output size (use the *maxSize* function to determine this), otherwise
an error will be thrown. Whilst this amount of space
is usually not required, for performance reasons this is not checked during
encoding, so the space is needed to prevent possible overflow conditions.

int maxSize(int length, int line_size=128, float escape_ratio=1)
----------------------------------------------------------------

Returns the maximum possible size for a raw yEnc encoded message of *length*
bytes. Note that this does include some provision for dealing with alignment
issues specific to *yencode*‘s implementation; in other words, the returned
value is actually an over-estimate for the maximum size.

You can manually specify expected yEnc character escaping ratio with the *escape_ratio* parameter if you wish to calculate an “expected size” rather than the maximum. The ratio must be between 0 (no characters ever escaped) and 1 (all characters escaped, i.e. calculates maximum possible size, the default behavior).
For random data, and a line size of 128, the expected escape ratio for yEnc is roughly 0.0158. For 1KB of random data, the probability that the escape ratio exceeds 5% would be about 2.188\*10^-12^ (or 1 in 4.571\*10^11^). For 128KB of random data, exceeding a 1.85% ratio has a likelihood of 1.174\*10^-14^ (or 1 in 8.517\*10^13^).

For usage with *encodeTo*, the *escape_ratio* must be 1.

## int minSize(int length, int line_size=128)

Returns the minimum possible size for a raw yEnc encoded message of *length* bytes. Unlike `maxSize`, this does not include alignment provisions for *yencode*‘s implementation of yEnc.

This is equivalent to `maxSize(length, line_size, 0) - 2` (`maxSize` adds a 2 to provision for an early end-of-line due to a line offset being used).

Buffer decode(Buffer data, bool stripDots=false)
------------------------------------------------

Performs raw yEnc decoding on *data* returning the result. If *stripDots* is true,
will perform NNTP's "dot unstuffing" during decode. If *data* was sourced from an
NNTP abstraction layer which already performs unstuffing, *stripDots* should be
false, otherwise, if you're processing data from the socket yourself and haven't
othewise performed unstuffing, *stripDots* should be set to true.

int decodeTo(Buffer data, Buffer output, bool stripDots=false)
--------------------------------------------------------------

Same as above, but instead of returning a Buffer, writes it to the supplied
*output* Buffer. Returns the length of the decoded data.  
Note that the *output* Buffer must be at least large enough to hold the largest
possible output size (i.e. length of the input), otherwise an error is thrown.

Object decodeChunk\(Buffer data \[, string state=null\]\[, Buffer output\]\)
-----------------------------------------------------------------------------

Perform raw yEnc decoding on a chunk of data sourced from NNTP. This function is
designed to incrementally process a stream from the network, and will perform NNTP
"dot unstuffing" as well as stop when the end of the data is reached.

*data* is the data to be decoded  
*state* is the current state of the incremental decode. Set to *null* if this is starting the decode of a new article, otherwise this should be set to the value of *state* given from the previous invocation of *decodeChunk*  
If *output* is supplied, the output will be written here \(see *decodeTo* for notes
on required size\), otherwise a new buffer will be created where the output will be
written to.

Returns an object with the following keys:

- *int read*: number of bytes read from the *data*. Will be equal to the length of
  the input unless the end was reached (*ended* set to *true*).
- *int written*: number of bytes written to the output
- *Buffer output*: the output data. If the *output* parameter was supplied to this
  function, this will just be a reference to it.
- *bool ended*: whether the end of the yEnc data was reached. The *state* value will indicate the type of end which was reached
- *string state*: the state after decoding. This indicates the last few (up to 4) characters encountered, if they affect the decoding of subsequent characters. For example, a state of `"="` suggests that the first byte of the next call to *decodeChunk* needs to be unescaped. Feed this into the next invocation of *decodeChunk*
  Note that this value is after NNTP “dot unstuffing”, where applicable (`\r\n.=` sequences are replaced with `\r\n=`)
  If the end was reached (*ended* set to true), this will indicate the type of end which was reached, which can be either `\r\n=y`  (yEnc control line encountered) or `\r\n.\r\n` (end of article marker encountered)

Buffer(4) crc32(Buffer data, Buffer(4) initial=false)
-----------------------------------------------------

Calculate CRC32 hash of data, returning the hash as a 4 byte Buffer.  
You can perform incremental CRC32 calculation by specifying a 4 byte Buffer in
the second argument.

**Example**

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
y.crc32(new Buffer('the fox jumped'))
// <Buffer f8 7b 6f 30>
y.crc32(new Buffer(' into the fence'), new Buffer([0xf8, 0x7b, 0x6f, 0x30]))
// <Buffer 70 4f 00 7e>
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Buffer(4) crc32_combine(Buffer(4) crc1, Buffer(4) crc2, int len2)
-----------------------------------------------------------------

Combines two CRC32s, returning the resulting CRC32 as a 4 byte Buffer. To put it
another way, it calculates `crc32(a+b)` given `crc32(a)`, `crc32(b)` and
`b.length`.  
*crc1* is the first CRC, *crc2* is the CRC to append onto the end, where *len2*
represents then length of the data being appended.

**Example**

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
y.crc32_combine(
  y.crc32(new Buffer('the fox jumped')),
  y.crc32(new Buffer(' into the fence')),
  ' into the fence'.length
)
// <Buffer 70 4f 00 7e>
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Buffer(4) crc32_zeroes(int len, Buffer(4) initial=false)
--------------------------------------------------------

Calculates the CRC32 of a sequence of *len* null bytes, returning the resulting
CRC32 as a 4 byte Buffer.
You can supply a starting CRC32 value by passing it in the second parameter.

**Example**

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
y.crc32_zeroes(2)
// <Buffer 41 d9 12 ff>
y.crc32(new Buffer([0, 0]))
// <Buffer 41 d9 12 ff>
y.crc32_zeroes(2, y.crc32(new Buffer([1, 2])))
// <Buffer 9a 7c 6c 17>
y.crc32(new Buffer([1, 2, 0, 0]))
// <Buffer 9a 7c 6c 17>
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Buffer post(string filename, data, int line_size=128)
-----------------------------------------------------

Returns a single yEnc encoded post, suitable for posting to newsgroups.  
Note that *data* can be a Buffer or string or anything that `Buffer.from` or `new Buffer` accepts
(this differs from the above functions).

**Example**

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
y.post('bytes.bin', [0, 1, 2, 3, 4]).toString()
// '=ybegin line=128 size=5 name=bytes.bin\r\n*+,-.\r\n=yend size=5 crc32=515ad3cc'
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

YEncoder multi_post(string filename, int size, int parts, int line_size=128)
----------------------------------------------------------------------------

Returns a *YEncoder* instance for generating multi-part yEnc posts. This
implementation will only generate multi-part posts sequentially.  
You need to supply the *size* of the file, and the number of *parts* that it
will be broken into (typically this will be `Math.ceil(file_size/article_size)`)

The *YEncoder* instance has the following method and read-only properties:

-   **Buffer encode(data)** : Encode the next part (*data*) and returns the
    result.

-   **int size** : The file's size

-   **int parts** : Number of parts to post

-   **int line_size** : Size of each line

-   **int part** : Current part

-   **int pos** : Current position in file

-   **int crc** : CRC32 of data already fed through *encode()*

**Example**

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
enc = y.multi_post('bytes.bin', 5, 1)
enc.encode([0, 1, 2, 3, 4]).toString()
// '=ybegin line=128 size=5 name=bytes.bin\r\n*+,-.\r\n=yend size=5 crc32=515ad3cc'
enc.crc
<Buffer 51 5a d3 cc>
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Example 2**

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
enc = y.multi_post('bytes.bin', 5, 2)
enc.encode([0, 1, 2, 3]).toString()
// '=ybegin part=1 total=2 line=128 size=5 name=bytes.bin\r\n=ypart begin=1 end=4\r\n*+,-\r\n=yend size=4 part=1 pcrc32=8bb98613'
enc.encode([4]).toString()
// '=ybegin part=2 total=2 line=128 size=5 name=bytes.bin\r\n=ypart begin=5 end=5\r\n=n\r\n=yend size=1 part=2 pcrc32=d56f2b94 crc32=515ad3cc'
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

## {Object|DecoderError} from_post\(Buffer data, bool stripDots=false\)

Decode post specified in *data*. Set *stripDots* to true if NNTP “dot unstuffing” has not yet been performed.

Returns an object detailing the info parsed from the post, where the keys are:

- *int yencStart*: location of the `=ybegin` sequence (is usually `0` for most posts)
- *int dataStart*: location of where the yEnc raw data begins
- *int dataEnd*: location of where the yEnc raw data ends
- *int yencEnd*: location of the end of the `=yend` line (after the trailing newline)
- *Buffer data*: decoded data
- *Buffer(4) crc32*: 4 byte CRC32 of decoded data
- *Object\<Object\<string\>\> props*: two-level structure listing the properties given in the yEnc metadata. First level represents the line type (e.g. `=ybegin` line is keyed as `begin`), and the second level maps keys to values within that line. For example, the line `=ybegin line=128 name=my-file.dat` would be decoded as `{begin: {line: "128", name: "my-file.dat"}}`
- *Array\<DecoderWarning\> warnings*: a list of non-fatal issues encountered when decoding the post. Each *DecoderWarning* is an object with two properties:
  - *string code*: type of issue
  - *string message*: description of issue

If the post failed to decode, a *DecoderError* is returned, which is an *Error* object where the *code* property indicates the type of error. There are 3 possible error codes which could be returned:

- *no_start_found*: the `=ybegin` sequence could not be found
- *no_end_found*: the `=yend` sequence could not be found
- *missing_required_properties*: required properties could not be found

string encoding='utf8'
----------------------

The default character set used for encoding filenames.

Example
=======

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
var y = require('yencode');
var data = new Buffer(768000);
var post = Buffer.concat([
    // yEnc header
    new Buffer('=ybegin line=128 size=768000 name=rubbish.bin\r\n'),
    // encode the data
    y.encode(data),
    // yEnc footer
    new Buffer('\r\n=yend size=768000 crc32=' + y.crc32(data).toString('hex'))
]);
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Algorithm
=========

A brief description of how the SIMD yEnc encoding algorithm works [can be found
here](https://github.com/animetosho/node-yencode/issues/4#issuecomment-330025192).
I may eventually write up something more detailed, regarding optimizations and
such used.

License
=======

This module is Public Domain or
[CC0](https://creativecommons.org/publicdomain/zero/1.0/legalcode) (or
equivalent) if PD isn’t recognised.

[crcutil](https://code.google.com/p/crcutil/), used for CRC32 calculation, is
licensed under the [Apache License
2.0](http://www.apache.org/licenses/LICENSE-2.0)

[zlib-ng](https://github.com/Dead2/zlib-ng), from where the CRC32 calculation
using folding approach was stolen, is under a [zlib
license](https://github.com/Dead2/zlib-ng/blob/develop/LICENSE.md)
