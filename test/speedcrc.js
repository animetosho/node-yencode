var y = require('../build/Release/yencode');
var _ = require('./_speedbase');


// warmup
_.bufAvg.forEach(function(buf, i) {
	var p=process.hrtime();
	for(var i=0;i<_.rounds;i+=2) y.crc32(buf);
	var t=process.hrtime(p);
});

_.bufAvg.forEach(function(buf, i) {
	_.run('Random ('+i+')', y.crc32.bind(null, buf));
});
