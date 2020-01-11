
var assert = require('assert');
var y = require('../index.js');

var assertObjectHas = function(obj, required) {
	for(var k in required) {
		if(required[k] === undefined || required[k] === null)
			assert(obj[k] === required[k]);
		else if(typeof required[k] === 'object') // recurse to sub-objects
			assertObjectHas(obj[k], required[k]);
		else
			assert.equal(obj[k], required[k], k);
	}
};

var post, postData;

// test basic post
postData = [
	'=ybegin part=5 line=128 size=500000 name=myBinary.dat',
	'=ypart begin=499991 end=500000',
	'..... data',
	'=yend size=10 part=5 pcrc32=97f4bd52',
	''
].join('\r\n');
post = y.from_post(new Buffer(postData));
assertObjectHas(post, {
	yencStart: 0,
	dataStart: postData.indexOf('...'),
	dataEnd: postData.indexOf('\r\n=yend'),
	yencEnd: postData.length,
	props: {
		begin: {
			part: '5',
			line: '128',
			size: '500000',
			name: 'myBinary.dat'
		},
		part: {
			begin: '499991',
			end: '500000'
		},
		end: {
			size: '10',
			part: '5',
			pcrc32: '97f4bd52'
		}
	},
	warnings: undefined
});
assert.equal(post.data.length, post.dataEnd - post.dataStart); // there's no escaped characters in this post, so length should equal input length
// won't bother testing actual encoding or CRC32 (leave this to other tests)


// test post with extra data
postData = [
	'ignored data',
	'=ybegin part=5a some_prop=hello line=0 size=0 name=name with space and = chars',
	'.... data',
	'=yend size=2 pcrc32=invalid pcrc32=invalid invalid_prop',
].join('\r\n');
post = y.from_post(new Buffer(postData));
assertObjectHas(post, {
	yencStart: postData.indexOf('=ybegin'),
	dataStart: postData.indexOf('...'),
	dataEnd: postData.indexOf('\r\n=yend'),
	yencEnd: postData.length,
	props: {
		begin: {
			part: '5a',
			line: '0',
			some_prop: 'hello',
			name: 'name with space and = chars'
		},
		end: {
			pcrc32: 'invalid'
		}
	}
});
assert.equal(post.data.length, post.dataEnd - post.dataStart);
// check that expected warnings are there
var seenWarnings = {};
post.warnings.forEach(function(warn) {
	seenWarnings[warn.code] = warn.message;
});
assert(seenWarnings['ignored_line_data']);
assert(seenWarnings['duplicate_property']);
assert(seenWarnings['missing_yend_newline']);
assert(seenWarnings['invalid_prop_part']);
assert(seenWarnings['zero_prop_line']);
assert(seenWarnings['invalid_prop_pcrc32']);
assert(seenWarnings['size_mismatch']);

// empty post
postData = [
	'=ybegin size=0 line=1 name=',
	'=yend size=0',
	''
].join('\r\n');
post = y.from_post(new Buffer(postData));
assertObjectHas(post, {
	yencStart: 0,
	dataStart: undefined,
	dataEnd: undefined,
	yencEnd: postData.length,
	data: undefined,
	warnings: undefined,
	props: {
		begin: {
			size: '0',
			line: '1',
			name: ''
		},
		end: { size: '0' }
	}
});


// test parse errors
post = y.from_post(new Buffer('invalid post'));
assert.equal(post.code, 'no_start_found');
post = y.from_post(new Buffer('=ybegin abc=def'));
assert.equal(post.code, 'no_end_found');


console.log('All tests passed');
