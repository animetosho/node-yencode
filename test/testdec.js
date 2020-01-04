// a basic script to test that raw yEnc works as expected

var assert = require('assert');
var y = (function() {
	try {
		return require('../build/Debug/yencode.node');
	} catch(x) {}
	return require('../build/Release/yencode.node');
})();

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
	// TODO: do leading/trailing spaces/tabs need to be trimmed?
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
	{l: 'nntp', r: refYDecRaw, a: function(s) {
		return y.decode(s, true);
	}},
	{l: 'plain', r: refYDec, a: y.decode},
	{l: 'nntp-end', r: function(s) {
		return refYDecRaw(s, true);
	}, a: function(s) {
		if(!s.length) return Buffer(0);
		return y.decodeIncr(s).output;
	}}
];
var doTest = function(msg, data, expected) {
	data = new Buffer(data);
	
	var prepad = 48, postpad = 48;
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
					console.log('Actual:', actual);
					console.log('Expect:', x);
					console.log('Source:', data.toString('hex'));
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
doTest('NNTP end sequence, badly dot stuffed (2)', '\r\n.a=y');
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

// test for past bug in ARMv8 NEON decoder where nextMask wasn't properly compensated for
doTest('Extra null issue', new Buffer('2e900a4fb6054c9126171cdc196dc41237bb1b76da9191aa5e85c1d2a2a5c638fe39054a210e8c799473cd510541fd118f3904b242a9938558c879238aae1d3bdab32e287cedb820b494f54ffae6dd0b13f73a4a9499df486a7845c612182bcef72a6e50a8e98351c35765d26c605115dc8c5c56a5e3f20ae6da8dcd78536e6d1601eb1fc3ddc774', 'hex'));
// end detection bug
doTest('End detect', new Buffer('612e6161610d610d612e793d3d0d0d2e612e2e0a0d0d61792e3d3d61612e0d0a2e0d2e0a0d79612e0a3d2e2e793d2e610a0d0a0a2e793d790d612e61612e0a3d792e2e3d2e7961793d792e0a61790a0d0a2e0d0a3d0a0d0d0d0a610a0a6161792e3d2e0a2e0d0d0d613d610a0a0a793d613d3d0a3d790d3d0a0a2e2e7979796179613d0d2e792e793d3d61792e612e2e2e793d616161790d0d2e0d0d793d0d790a0a3d0d617979790d2e0d792e612e610a0a0a0a0a79790d0a610d612e0d0a0d3d0a61792e2e0a790d0d792e790d0a2e79612e3d0a79790a0d0d3d0a0a0d3d0a7961610a2e613d792e0a612e613d610a2e0a0a79613d2e2e0d3d3d2e793d792e792e0d0d610d2e2e0d2e79610d2e790d790d3d2e3d790a0a0d0a0a0a612e2e79612e0d2e3d793d2e0a2e3d790a2e3d792e2e610d3d2e0d3d3d0a3d2e0d613d2e0d61610a3d0a2e0a0a3d3d612e3d790d6161613d3d612e3d0d0a2e0d0d0d616179790a2e3d610d612e0d2e3d0d0a3d610d0d61610a7961613d2e790d613d610a3d612e0a2e0d79790d0a610a2e2e0a2e612e2e0d792e61610a2e0d610d3d0a793d613d0d3d0a3d0d0a613d2e0a3d610a3d0d793d0d7979792e3d613d0a2e61610d793d2e0a0a2e612e0d2e2e792e0d2e613d0d790a0d2e610d2e0a2e61793d0d0d0a0a0d0d2e2e0d2e793d3d79612e0a610a610a0d610d0d2e2e790', 'hex'));

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
for(var i=0; i<128; i++) {
	var rand = randStr(2048);
	doTest('Random2', rand);
}



console.log('All tests passed');
