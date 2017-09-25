// a basic script to test that raw yEnc works as expected

var assert = require('assert');
var y = require('./build/Release/yencode.node');

var ord = function(c) {
	return c.charCodeAt(0);
}, chr = String.fromCharCode;
// slow reference yEnc implementation
var refYDec = function(src) {
	var ret = [];
	for (var i = 0; i < src.length; i++) {
		switch(chr(src[i])) {
			case '\r': case '\n': continue;
			case '=':
				i++;
				if(i < src.length)
					ret.push((src[i] - 42 - 64) & 0xFF);
				continue;
		}
		ret.push((src[i] - 42) & 0xFF);
	}
	return new Buffer(ret);
};
var refYDecRaw = function(src) {
	// undo NNTP layer
	var data = [];
	var i = 0;
	if(src[0] == ord('.')) i++;
	for(; i<src.length; i++) {
		if(src[i] == ord('\r') && src[i+1] == ord('\n') && src[i+2] == ord('.')) {
			data.push(src[i], src[i+1]);
			i += 2;
			continue;
		}
		data.push(src[i]);
	}
	
	return refYDec(data);
};
var doTest = function(msg, data, expected) {
	data = new Buffer(data);
	
	var prepad = 32, postpad = 32;
	if(data.length > 1024) {
		prepad = 1;
		postpad = 1;
	}
	
	for(var i=0; i<prepad; i++) {
		var pre = new Buffer(i);
		pre.fill(1);
		for(var j=0; j<postpad; j++) {
			var post = new Buffer(j);
			post.fill(1);
			
			var testData = Buffer.concat([pre, data, post]);
			var x;
			if(expected === undefined) x = refYDecRaw(testData).toString('hex');
			else x = new Buffer(expected).toString('hex').replace(/ /g, '');
			var actual = y.decodeNntp(testData).toString('hex');
			if(actual != x) {
				console.log(actual, x);
				assert.equal(actual, x, msg + ' [' + i + '/' + j + ']');
			}
			
			if(expected !== undefined) return; // if given expected string, only do one test
			
			
			// TODO: test non-raw
			// TODO: test various states
		}
	}
};


doTest('Empty test', [], '');
doTest('Simple test', [0,1,2,10,3,61,64]);
doTest('Just newline', [10], '');
doTest('Just equals', [61], '');
doTest('Equal+newline', [61, 13], [163]);
doTest('Equal+equal', [61, 61], [211]);
doTest('Equal+equal+newline', [61, 61, 13], [211]);
doTest('Newline, equal', [10, 61], '');
doTest('Stripped dot', [13, 10, 46], '');
doTest('Stripped dot (2)', [98, 13, 10, 46, 97]);
doTest('Consecutive stripped dot', [13, 10, 46, 13, 10, 46]);
doTest('Bad escape stripped dot', [61, 13, 10, 46]);

// longer tests
var b = new Buffer(256);
b.fill(0);
doTest('Long no escaping', b);
b.fill('='.charCodeAt(0));
doTest('Long all equals', b);
for(var i=1; i<b.length; i+=2)
	b[i] = 64;
doTest('Long all escaped nulls', b);
b.fill('='.charCodeAt(0));
for(var i=0; i<b.length; i+=2)
	b[i] = 64;
doTest('Long all invalid escaped nulls', b);
b.fill(10);
doTest('Long all newlines', b);
b.fill(223);
doTest('Long all tabs', b);


// random tests
for(var i=0; i<32; i++) {
	var rand = require('crypto').pseudoRandomBytes(128*1024);
	doTest('Random', rand);
}

// targeted random tests
var charset = '=\r\n.a';
var randStr = function(n) {
	var ret = new Buffer(n);
	for(var i=0; i<n; i++)
		ret[i] = ord(charset[(Math.random() * charset.length) | 0]);
	return ret;
};
for(var i=0; i<32; i++) {
	var rand = randStr(16384);
	doTest('Random2', rand);
}



console.log('All tests passed');
