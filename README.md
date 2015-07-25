This module provides a fast (non-JS) compiled implementation of [yEnc encoding](http://www.yenc.org/yenc-draft.1.3.txt) and CRC32 hash calculation for node.js. It's mainly optimised for x86 processors, and will use SIMD operations if available.

At time of writing, there are a number of yEnc encoding and CRC32 modules for node.js, however all seem to be implemented in Javascript, which is significantly slower than compiled code.

Supports:
---------

* fast raw yEnc encoding with incremental support and the ability to specify line length. Will use SSE2 if available
* fast compiled CRC32 implementation via [crcutil](https://code.google.com/p/crcutil/) with incremental support
* (may eventually support) yEnc decoding
* (may support) full yEnc generation and parsing

Only tested with node 0.10 and 0.12 on Linux/gcc with an x86 CPU. Tests and fixes for other platforms welcome.

Building
========

	npm install -g node-gyp # if you don't have it already
	node-gyp build

API
===

	var y = require('yencode');
	
	// encode data using raw yEnc
	//   line_size controls how often to insert newlines (note, as per yEnc specifications, it's possible to have lines longer than this length)
	//   column_offset is the column of the first character
	// returns the encoded data
	Buffer y.encode(Buffer data, int line_size=128, int column_offset=0)
	
	// calculate CRC32 of data
	//   you can perform incremental CRC32 calculation by specifying a 4 byte Buffer for init
	// returns a 4 byte Buffer representing the CRC32
	Buffer(4) y.crc32(Buffer data, Buffer(4) initial=[0,0,0,0])

Example
=======

	var y = require('yencode');
	var data = new Buffer(768000);
	var post = Buffer.concat([
		new Buffer('=ybegin line=128 size=768000 name=rubbish.bin\r\n'),
		y.encode(data),
		new Buffer('\r\n=yend size=768000 crc32=' + y.crc32(data).toString('hex'))
	]);

