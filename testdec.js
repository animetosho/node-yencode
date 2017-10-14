// a basic script to test that raw yEnc works as expected

var assert = require('assert');
var y = require('./build/Release/yencode.node');

var ord = function(c) {
	return c.charCodeAt(0);
}, chr = String.fromCharCode;
// slow reference yEnc implementation
var refYDec = function(src, findEnd) {
	var ret = [];
	if(findEnd && chr(src[0]) == '=' && chr(src[1]) == 'y') return new Buffer(0);
	for (var i = 0; i < src.length; i++) {
		switch(chr(src[i])) {
			case '\r':
				if(findEnd && chr(src[i+1]) == '\n' && chr(src[i+2]) == '=' && chr(src[i+3]) == 'y')
					return new Buffer(ret);
			case '\n': continue;
			case '=':
				i++;
				if(i < src.length)
					ret.push((src[i] - 42 - 64) & 0xFF);
				if(chr(src[i]) == '\r') i--;
				continue;
		}
		ret.push((src[i] - 42) & 0xFF);
	}
	return new Buffer(ret);
};
var refYDecRaw = function(src, findEnd) {
	// undo NNTP layer
	var data = [];
	var i = 0;
	if(src[0] == ord('.')) {
		i++;
		if(findEnd && src[1] == ord('\r') && src[2] == ord('\n'))
			return new Buffer(0);
	}
	for(; i<src.length; i++) {
		if(src[i] == ord('\r') && src[i+1] == ord('\n') && src[i+2] == ord('.')) {
			data.push(src[i], src[i+1]);
			if(findEnd && src[i+3] == ord('\r') && src[i+4] == ord('\n')) {
				// it's a little vague how this is exactly handled, but we'll push a \r\n to the yenc decoder (above line)
				// doing so means that =\r\n.\r\n will generate a single escaped character
				break;
			}
			i += 2;
			continue;
		}
		data.push(src[i]);
	}
	
	return refYDec(data, findEnd);
};
var testFuncs = [
	{l: 'nntp', r: refYDecRaw, a: y.decodeNntp},
	{l: 'plain', r: refYDec, a: y.decode},
	{l: 'nntp-end', r: function(s) {
		return refYDecRaw(s, true);
	}, a: function(s) {
		if(!s.length) return Buffer(0);
		return y.decodeNntpIncr(s).output;
	}},
	{l: 'plain-end', r: function(s) {
		return refYDec(s, true);
	}, a: function(s) {
		if(!s.length) return Buffer(0);
		return y.decodeIncr(s).output;
	}}
];
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
			testFuncs.forEach(function(f) {
				var post = new Buffer(j);
				post.fill(1);
				
				var testData = Buffer.concat([pre, data, post]);
				var x;
				if(expected === undefined) x = f.r(testData).toString('hex');
				else x = new Buffer(expected).toString('hex').replace(/ /g, '');
				var actual = f.a(testData).toString('hex');
				if(actual != x) {
					console.log(actual, x);
					console.log(data.toString('hex'));
					assert.equal(actual, x, msg + ' [' + i + '/' + j + ' ' + f.l + ']');
				}
			});
			if(expected !== undefined) return; // if given expected string, only do one test
			
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
doTest('Stripped dot', [13, 10, 46]);
doTest('Stripped dot (2)', [98, 13, 10, 46, 97]);
doTest('Just dot', [46]);
doTest('Consecutive stripped dot', [13, 10, 46, 13, 10, 46]);
doTest('Bad escape stripped dot', [61, 13, 10, 46]);

doTest('NNTP end sequence', '\r\n.\r\n');
doTest('NNTP end sequence (2)', '.\r\n');
doTest('Yenc end sequence', '\r\n=y');
doTest('Yenc end sequence (2)', '=y');
doTest('Mixed end sequence', '\r\n=y\r\n.\r\n');
doTest('Mixed end sequence (2)', '\r\n.\r\n=y');
doTest('Not end sequence', '\r\n=abc');
doTest('Dot stuffed end sequence', '\r\n.=y');
doTest('Dot stuffed bad escape sequence', '\r\n.=.');
doTest('Broken end sequence', '\r\n.\ra\n');
doTest('NNTP end sequence, badly dot stuffed', '\r\n..\r\n');
doTest('Bad escape, NNTP end sequence', '=\r\n.\r\n');
doTest('Bad escape, Yenc end sequence', '=\r\n=y');


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
var charset = '=\r\n.ay';
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
