This module provides a fast (non-JS) compiled implementation of yEnc encoding for node.js.  It is currently incomplete and still under development.

Supports:

* raw yEnc encoding with SSE2 support
* (may support) yEnc decoding
* (will support) fast compiled CRC32 implementation via crcutil library

Only tested on node 0.10 and 0.12 on Linux/gcc.

Building
========

	node-gyp build

API
===

	require('yencode').encode(Buffer data, int line_size=128, int column_offset=0)

