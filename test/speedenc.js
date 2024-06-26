var y = require('../build/Release/yencode');
var _ = require('./_speedbase');


_.parseArgs('Syntax: node test/speedenc [-a|--average-only] [{-s|--sleep}=msecs(0)]');


console.log('    Test                       Read rate       Output rate        Read + CRC      ');

var lenWorst = y.encodeTo(_.bufWorst, _.bufTarget);
var lenBest = y.encodeTo(_.bufBest, _.bufTarget);
var lenAvg = Array(_.bufAvg.length);
var lenAvg2x = Array(_.bufAvg2x.length);

// warmup
var initRounds = _.sleep ? 1 : _.rounds;
_.bufAvg.forEach(function(buf, i) {
	var p=process.hrtime();
	for(var j=0;j<initRounds;j+=1) lenAvg[i] = y.encodeTo(buf, _.bufTarget);
	var t=process.hrtime(p);
});
_.bufAvg2x.forEach(function(buf, i) {
	var p=process.hrtime();
	for(var j=0;j<initRounds;j+=1) lenAvg2x[i] = y.encodeTo(buf, _.bufTarget);
	var t=process.hrtime(p);
});

var encCrc = function(src) {
	y.encodeTo(src, _.bufTarget);
	y.crc32(src);
};

setTimeout(function() {
	if(!_.avgOnly) {
		_.run('Worst (all escaping)', y.encodeTo.bind(null, _.bufWorst, _.bufTarget), lenWorst, encCrc.bind(null, _.bufWorst));
		_.run('Best (no escaping)', y.encodeTo.bind(null, _.bufBest, _.bufTarget), lenBest, encCrc.bind(null, _.bufBest));
	}
	
	_.bufAvg.forEach(function(buf, i) {
		_.run('Random ('+i+')', y.encodeTo.bind(null, buf, _.bufTarget), lenAvg[i], encCrc.bind(null, buf));
	});
	if(!_.avgOnly) {
		_.bufAvg2x.forEach(function(buf, i) {
			_.run('Random 2xEsc ('+i+')', y.encodeTo.bind(null, buf, _.bufTarget), lenAvg2x[i], encCrc.bind(null, buf));
		});
	}

}, _.sleep);
