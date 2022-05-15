// a basic script to test that raw yEnc works as expected

var assert = require('assert');
var y = (function() {
	try {
		return require('../build/Debug/yencode.node');
	} catch(x) {}
	return require('../build/Release/yencode.node');
})();
var crc32 = require('buffer-crc32'); // reference implementation

var ycrc32 = function(s) {
	return y.crc32(Buffer(s));
};
var doTest = function(msg, f, test, expected) {
	if(!Array.isArray(test)) test = [test];
	test[0] = Buffer(test[0]);
	if(!expected && test.length == 1 && f == 'crc32') expected = crc32(test[0]).toString('hex');
	else if(Buffer.isBuffer(expected)) expected = expected.toString('hex');
	assert.equal(y[f].apply(null, test).toString('hex'), expected, msg);
};


doTest('Empty test', 'crc32', '');
doTest('Single char', 'crc32', 'z');
doTest('Simple string', 'crc32', 'aabbcc');
doTest('Join', 'crc32', ['cc', ycrc32('aabb')], crc32('aabbcc'));
doTest('Combine', 'crc32_combine', [ycrc32('aabb'), crc32('cc'), 2], crc32('aabbcc'));
doTest('Join 2', 'crc32', ['789012', ycrc32('123456')], crc32('123456789012'));
doTest('Combine 2', 'crc32_combine', [ycrc32('123456'), crc32('789012'), 6], crc32('123456789012'));

doTest('Join Empty', 'crc32', ['', ycrc32('z')], crc32('z'));
doTest('Join Empty 2', 'crc32', ['z', ycrc32('')], crc32('z'));
doTest('Join Empty 3', 'crc32', ['', ycrc32('')], crc32(''));
doTest('Combine Empty', 'crc32_combine', [ycrc32(''), ycrc32('z'), 1], crc32('z'));
doTest('Combine Empty 2', 'crc32_combine', [ycrc32('z'), ycrc32(''), 0], crc32('z'));
doTest('Combine Empty 3', 'crc32_combine', [ycrc32(''), ycrc32(''), 0], crc32(''));

assert.equal(y.crc32_zeroes(0).toString('hex'), '00000000', 'Zeroes (0)');
assert.equal(y.crc32_zeroes(1).toString('hex'), 'd202ef8d', 'Zeroes (1)');
assert.equal(y.crc32_zeroes(4).toString('hex'), '2144df1c', 'Zeroes (4)');

assert.equal(y.crc32_zeroes(0, ycrc32('')).toString('hex'), '00000000', 'Zeroes-Join (0)');
assert.equal(y.crc32_zeroes(1, ycrc32('')).toString('hex'), 'd202ef8d', 'Zeroes-Join (1)');
assert.equal(y.crc32_zeroes(0, ycrc32('z')).toString('hex'), crc32('z').toString('hex'), 'Zeroes Empty Join');
assert.equal(y.crc32_zeroes(4, ycrc32('z')).toString('hex'), crc32('z\u0000\u0000\u0000\u0000').toString('hex'), 'Zeroes (4) Join');


doTest('Random', 'crc32', 'fj[-oqijnw34-59n26 4345j8yn89032q78t9ab9gabh023quhoiBO Z GEB780a sdasdq2345673-98hq2-9348h-na9we8zdfgh-n9  8qwhn-098');
doTest('Random Continue', 'crc32', ['KZSHZ5EDOVAmDdakZZOrGSUGGKSpCJoWH7M0MHy6ohnSzvHY4DjpxXmyfWYJQoJ7tKdNhGcuRVUzrgXM', ycrc32('BdenbmoBgiB10ZkeUBjrsZV3dg2Da2fhHqU9TMdi69AHhLRck3Nk60YuFBXh6lvtefBpjdTxbeEmsaEm')], crc32('BdenbmoBgiB10ZkeUBjrsZV3dg2Da2fhHqU9TMdi69AHhLRck3Nk60YuFBXh6lvtefBpjdTxbeEmsaEmKZSHZ5EDOVAmDdakZZOrGSUGGKSpCJoWH7M0MHy6ohnSzvHY4DjpxXmyfWYJQoJ7tKdNhGcuRVUzrgXM'));


// random tests
for(var i=1; i<128; i++) {
	var rand = require('crypto').pseudoRandomBytes(i);
	doTest('Random Short Buffer', 'crc32', rand);
}
for(var i=0; i<32; i++) {
	var rand = require('crypto').pseudoRandomBytes(100000);
	doTest('Random Buffer', 'crc32', rand);
	
	var split = Math.random()*rand.length;
	doTest('Random Continue Buffer', 'crc32', [rand.slice(split), ycrc32(rand.slice(0, split))], crc32(rand));
}


console.log('All tests passed');
