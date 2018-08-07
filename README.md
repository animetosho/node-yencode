This module provides a very fast (non-JS) compiled implementation of
[yEnc](http://www.yenc.org/yenc-draft.1.3.txt) and CRC32 hash calculation for
node.js. The implementations are optimised for speed, and can optionally use
x86/ARM SIMD optimised routines if available.

This module should be *significantly* faster than pure Javascript versions.

Supports:
---------

-   fast raw yEnc encoding and the ability to specify line length. A single
    thread can achieve \>500MB/s on a low power Atom CPU, or \>3GB/s on a Core-i
    series CPU.

-   fast yEnc decoding, with and without NNTP layer dot unstuffing. (algorithm
    internally also supports stopping on end markers, but not exposed via node)
    Can achieve \>2GB/s on one thread on a modern Intel CPU.

-   SIMD optimised encoding and decoding routines, which can use ARM NEON or the
    following x86 CPU features when available (with dynamic dispatch): SSE2,
    SSSE3, AVX, AVX512BW (128-bit), VBMI2

-   full yEnc encoding for single and multi-part posts, according to the
    [version 1.3 specifications](http://www.yenc.org/yenc-draft.1.3.txt)

-   fast compiled CRC32 implementation via
    [crcutil](https://code.google.com/p/crcutil/) or [PCLMULQDQ
    instruction](http://www.intel.com/content/dam/www/public/us/en/documents/white-papers/fast-crc-computation-generic-polynomials-pclmulqdq-paper.pdf)
    (if available) or ARMv8’s CRC instructions, with incremental support
    (\>1GB/s on a low power Atom/ARM CPU, \>15GB/s on a modern Intel CPU)

-   ability to combine two CRC32 hashes into one (useful for amalgamating
    *pcrc32s* into a *crc32* for yEnc)

-   eventually may support incremental processing (algorithms internally support
    it, they’re just not exposed to the Javascript interface)

Should work on nodejs 0.10.x and later.

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

Some versions of GCC/Clang don't like the `-march=native` switch. If you're such
having build issues, see the following section.

Redistributable Builds
----------------------

By default, non-Windows builds are built with `-march=native` flag, which means
that the compiler will optimise the build for the CPU of the build machine. If
you’re looking to run built binaries elsewhere, this may be undesirable. To make
builds redistributable, try removing all instances of `"-march=native",` from
*binding.gyp* and recompiling.

Windows builds are redistributable by default.

Older Compilers
---------------

Unfortunately node-gyp doesn’t provide much in the way of compiler version
detection. As such, the build script assumes the compiler isn’t too old and
supports AVX. If you are trying to build using an older compiler (such as Visual
Studio 2008), you may need to edit *binding.gyp* to remove AVX related options,
such as `-mavx`, `-mpopcnt` and `"EnableEnhancedInstructionSet": "3"`

API
===

Note that for the *encode*, *crc32* and *crc32_combine* functions, the *data*
parameter must be a Buffer and not a string. Also, on node v0.10, these
functions actually return a *SlowBuffer* object, similar to how node’s crypto
functions work.

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
the function returns 0 and does not encode anything. Whilst this amount of space
is usually not required, for performance reasons this is not checked during
encoding, so the space is needed to prevent possible overflow conditions.

int maxSize(int length, int line_size=128)
------------------------------------------

Returns the maximum possible size for a raw yEnc encoded message of *length*
bytes. Note that this does include some provision for dealing with alignment
issues specific to *yencode*‘s implementation; in other words, the returned
value is actually an over-estimate for the maximum size.

Buffer decode(Buffer data)
--------------------------

Performs raw yEnc decoding on *data* returning the result. Note, this assumes
that NNTP's "dot unstuffing" has already been performed, use `decodeNntp` if
this is not the case.

int decodeTo(Buffer data, Buffer output)
----------------------------------------

Same as above, but instead of returning a Buffer, writes it to the supplied
*output* Buffer. Returns the length of the decoded data.  
Note that the *output* Buffer must be at least large enough to hold the largest
possible output size (i.e. length of the input), otherwise the function returns
0 and does not decode anything.

Buffer decodeNntp(Buffer data)
------------------------------

Performs raw yEnc decoding on *data* returning the result. This differs from
`decode` in that *data* is assumed to be from a socket and NNTP's "dot
unstuffing" has not yet been performed.

int decodeNntpTo(Buffer data, Buffer output)
--------------------------------------------

See `decodeNntp` and `decodeTo` for descriptions.

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

Buffer(4) crc32_zeroes(int len)
-------------------------------

Calculates the CRC32 of a sequence of *len* null bytes, returning the resulting
CRC32 as a 4 byte Buffer.

**Example**

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
y.crc32_zeroes(2)
// <Buffer 41 d9 12 ff>
y.crc32(new Buffer([0, 0]))
// <Buffer 41 d9 12 ff>
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Buffer post(string filename, data, int line_size=128)
-----------------------------------------------------

Returns a single yEnc encoded post, suitable for posting to newsgroups.  
Note that *data* can be a Buffer or string or anything that `new Buffer` accepts
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
