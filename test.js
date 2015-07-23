var assert = require('assert');
var y = require('./build/Release/obj.target/yencode.node');

// slow reference yEnc implementation
var refYEnc = function(src, line_size, col) {
	var ret = [];
	for (var i = 0; i < src.length; i++) {
		var c = (src[i] + 42) & 0xFF;
		switch(String.fromCharCode(c)) {
			case '.':
				if(col > 0) break;
			case '\t': case ' ':
				if(col > 0 && col < line_size-1) break;
			case '\0': case '\r': case '\n': case '=':
				ret.push('='.charCodeAt(0));
				c += 64;
				col++;
		}
		ret.push(c);
		col++;
		if(col >= line_size) {
			ret.push('\r'.charCodeAt(0));
			ret.push('\n'.charCodeAt(0));
			col = 0;
		}
	}
	
	// if there's a trailing newline, trim it
	if(ret[ret.length-2] == '\r'.charCodeAt(0) && ret[ret.length-1] == '\n'.charCodeAt(0)) {
		ret.pop();
		ret.pop();
	}
	
	return new Buffer(ret);
};
var doTest = function(msg, test, expected) {
	test[0] = new Buffer(test[0]);
	if(!test[1]) test[1] = 128; // line size
	if(!test[2]) test[2] = 0; // column offset
	
	if(!expected) expected = refYEnc.apply(null, test).toString('hex');
	else expected = expected.replace(/ /g, '');
	assert.equal(y.encode.apply(null, test).toString('hex'), expected, msg);
};


doTest('Empty test', [[]], '');
doTest('Simple test', [[0,1,2,3,224,4]]);
doTest('Dot first should escape', [[4,3,224,2,1,0]]);
doTest('Short line', [[0,1,2,3,4], 2]);
doTest('Short line (2)', [[0,1,224,3,4], 2]);
doTest('Short line + offset', [[0,1,2,3,4], 2, 1]);
doTest('Short line (2) + offset', [[0,1,224,3,4], 2, 1]);
doTest('Tab & lf around line break', [[223,224], 128, 127], '3d 49 0d 0a 3d 4a');

// longer tests
var b = new Buffer(256);
b.fill(0);
doTest('Long no escaping', [b]);
b.fill(227);
doTest('Long all escaping', [b]);
b.fill(4);
doTest('Long all dots', [b]);
b.fill(223);
doTest('Long all tabs', [b]);


// random tests
for(var i=0; i<16; i++) {
	var rand = require('crypto').pseudoRandomBytes(4096);
	doTest('Random', [rand]);
	doTest('Random + short line', [rand, 3]);
	doTest('Random + offset', [rand, 128, 1]);
	doTest('Random + offset (end)', [rand, 128, 127]);
}

console.log('All tests passed');