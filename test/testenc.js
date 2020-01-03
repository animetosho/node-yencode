// a basic script to test that raw yEnc works as expected

var assert = require('assert');
var y = (function() {
	try {
		return require('../build/Debug/yencode.node');
	} catch(x) {}
	return require('../build/Release/yencode.node');
})();
var maxSize = require('./_maxsize');

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
	
	// if the last character is tab/space, escape it
	if(ret[ret.length-1] == '\t'.charCodeAt(0) || ret[ret.length-1] == ' '.charCodeAt(0)) {
		var c = ret[ret.length-1];
		ret.pop();
		ret.push('='.charCodeAt(0));
		ret.push((c+64) & 0xFF);
	}
	
	return new Buffer(ret);
};
var doTest = function(msg, test, expected) {
	test[0] = new Buffer(test[0]);
	if(!test[1]) test[1] = 128; // line size
	if(!test[2]) test[2] = 0; // column offset
	
	if(!expected && expected !== '') expected = refYEnc.apply(null, test).toString('hex');
	else expected = expected.replace(/ /g, '');
	var actual = y.encode.apply(null, test).toString('hex');
	if(actual != expected) {
		console.log('Actual:', actual);
		console.log('Expect:', expected);
		console.log('Source:', test[0].toString('hex'));
		assert.equal(actual, expected, msg);
	}
	
	var buf = new Buffer(maxSize(test[0].length, test[1]));
	var len = y.encodeTo.apply(null, [test[0], buf].concat(test.slice(1)));
	assert.equal(buf.slice(0, len).toString('hex'), expected, msg);
};

var range = function(start, end) {
	var ret = Array(end-start);
	for(var i=start; i<end; i++)
		ret[i-start] = i;
	return ret;
}
var multiRange = function(list) {
	var ret = [];
	for(var i=0; i<list.length; i+=2) {
		ret = ret.concat(range(list[i], list[i+1]));
	}
	return ret;
}

var lineSizes = // 48 sizes to test
	range(1, 18).concat(range(23, 26)).concat(range(30, 35)).concat(range(46, 51)).concat(range(62, 67)).concat(range(126, 131)).concat([136, 145, 159]).concat(range(254, 259));
var runLineSizes = function(fn) {
	lineSizes.forEach(function(ls) {
		lineSizes.forEach(function(offs) {
			if(offs >= ls) return;
			fn(ls, offs);
		});
	});
};

// simple tests
runLineSizes(function(ls, offs) {
	var infoStr = ' [ls='+ls+', offs='+offs+']';
	var b = new Buffer(256);
	b.fill(0);
	doTest('Long no escaping' + infoStr, [b, ls]);
	b.fill(227);
	doTest('Long all escaping' + infoStr, [b, ls]);
	b.fill(4);
	doTest('Long all dots' + infoStr, [b, ls]);
	b.fill(223);
	doTest('Long all tabs' + infoStr, [b, ls]);
});

// case tests
var padLens = range(0, 35).concat(range(46, 51)).concat(range(62, 67));
var padding = new Buffer(128);
padding.fill(97); // 'a'
[ ['Empty test', [[]], ''],
  ['Simple test', [[0,1,2,3,224,4]]],
  ['Partial tab line', [[223, 223, 223]]],
  ['Dot first should escape', [[4,3,224,2,1,0]]],
  ['Short line', [[0,1,2,3,4], 2]],
  ['Short line (2)', [[0,1,224,3,4], 2]],
  ['Short line + offset', [[0,1,2,3,4], 2, 1]],
  ['Short line (2) + offset', [[0,1,224,3,4], 2, 1]],
  ['Tab & lf around line break', [[223,224], 128, 127], '3d 49 0d 0a 3d 4a'],
].forEach(function(test) {
	doTest.apply(null, test);
	if(test.length == 2) {
		// if no defined result, try padding the data to make it long enough to invoke SIMD behaviour
		padLens.forEach(function(prepad) {
			padLens.forEach(function(postpad) {
				var newBuf = Buffer.concat([padding.slice(0, prepad), new Buffer(test[1][0]), padding.slice(0, postpad)]);
				var newTest = test.slice();
				newTest[1] = [newBuf].concat(newTest[1].slice(1));
				doTest.apply(null, newTest);
			});
		});
	}
});

// random tests
for(var i=0; i<32; i++) {
	var rand = require('crypto').pseudoRandomBytes(4*1024);
	runLineSizes(function(ls, offs) {
		doTest('Random [ls='+ls+', offs='+offs+']', [rand, ls, offs]);
	});
}

// targeted random tests
var charset = 'a\xd6\xdf\xe0\xe3\xf6\x04\x13';
var randStr = function(n) {
	var ret = new Buffer(n);
	for(var i=0; i<n; i++)
		ret[i] = charset[(Math.random() * charset.length) | 0].charCodeAt(0);
	return ret;
};
for(var i=0; i<32; i++) {
	var rand = randStr(2048);
	runLineSizes(function(ls, offs) {
		doTest('Random2 [ls='+ls+', offs='+offs+']', [rand, ls, offs]);
	});
}


console.log('All tests passed');
