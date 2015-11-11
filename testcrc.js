// a basic script to test that raw yEnc works as expected

var assert = require('assert');
var y = require('./build/Release/yencode.node');
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


console.log('All tests passed');
