var y = require('../build/Release/yencode');
var _ = require('./_speedbase');

var bufSize = _.bufTarget.length;

var mWorst = new Buffer(bufSize);
var mAvg = _.bufAvg.map(function() {
	return new Buffer(bufSize);
});
var mAvg2x = _.bufAvg2x.map(function() {
	return new Buffer(bufSize);
});
var mBest = new Buffer(bufSize);
var mBest2 = new Buffer(_.size);
mBest2.fill(32);

var lenWorst = y.encodeTo(_.bufWorst, mWorst);
var lenBest = y.encodeTo(_.bufBest, mBest);
var lenAvg = Array(_.bufAvg.length);
var lenAvg2x = Array(_.bufAvg2x.length);
_.bufAvg.forEach(function(buf, i) {
	lenAvg[i] = y.encodeTo(buf, mAvg[i]);
});
_.bufAvg2x.forEach(function(buf, i) {
	lenAvg2x[i] = y.encodeTo(buf, mAvg2x[i]);
});



_.parseArgs('Syntax: node test/speeddec [-a|--average-only] [{-s|--sleep}=msecs(0)] [{-m|--methods}=clean,raw,rawincr]');

console.log('    Test                     Output rate         Read rate   ');

// warmup
if(!_.sleep) {
	mAvg.forEach(function(buf) {
		var p=process.hrtime();
		for(var j=0;j<_.rounds;j+=2) y.decodeTo(buf, _.bufTarget);
		for(var j=0;j<_.rounds;j+=2) y.decodeTo(buf, _.bufTarget, true);
		for(var j=0;j<_.rounds;j+=2) y.decodeIncr(buf, 0, _.bufTarget);
		var t=process.hrtime(p);
	});
}

setTimeout(function() {
	if(!_.avgOnly) {
		if(_.decMethods.clean) {
			_.run('Clean worst (all escaping)', y.decodeTo.bind(null, mWorst, _.bufTarget), lenWorst);
			_.run('Clean best (min escaping)',  y.decodeTo.bind(null, mBest, _.bufTarget), lenBest);
			_.run('Clean pass (no escaping)',  y.decodeTo.bind(null, mBest2, _.bufTarget));
		}
		if(_.decMethods.raw) {
			_.run('Raw worst', y.decodeTo.bind(null, mWorst, _.bufTarget, true), lenWorst);
			_.run('Raw best',  y.decodeTo.bind(null, mBest, _.bufTarget, true), lenBest);
			_.run('Raw pass',  y.decodeTo.bind(null, mBest2, _.bufTarget, true));
		}
		if(_.decMethods.rawincr) {
			_.run('Raw-incr worst', y.decodeIncr.bind(null, mWorst, 0, _.bufTarget), lenWorst);
			_.run('Raw-incr best',  y.decodeIncr.bind(null, mBest, 0, _.bufTarget), lenBest);
			_.run('Raw-incr pass',  y.decodeIncr.bind(null, mBest2, 0, _.bufTarget));
		}
	}
	
	if(_.decMethods.clean)
		mAvg.forEach(function(buf, i) {
			_.run('Clean random ('+i+')',   y.decodeTo.bind(null, buf, _.bufTarget), lenAvg[i]);
		});
	if(_.decMethods.raw)
		mAvg.forEach(function(buf, i) {
			_.run('Raw random ('+i+')',   y.decodeTo.bind(null, buf, _.bufTarget, true), lenAvg[i]);
		});
	if(_.decMethods.rawincr) {
		mAvg.forEach(function(buf, i) {
			_.run('Raw-incr random ('+i+')',  y.decodeIncr.bind(null, buf, 0, _.bufTarget), lenAvg[i]);
		});
	}
	
	if(!_.avgOnly) {
		if(_.decMethods.clean)
			mAvg2x.forEach(function(buf, i) {
				_.run('Clean random 2xEsc ('+i+')',   y.decodeTo.bind(null, buf, _.bufTarget), lenAvg2x[i]);
			});
		if(_.decMethods.raw)
			mAvg2x.forEach(function(buf, i) {
				_.run('Raw random 2xEsc ('+i+')',   y.decodeTo.bind(null, buf, _.bufTarget, true), lenAvg2x[i]);
			});
		if(_.decMethods.rawincr)
			mAvg2x.forEach(function(buf, i) {
				_.run('Raw-incr random 2xEsc ('+i+')',  y.decodeIncr.bind(null, buf, 0, _.bufTarget), lenAvg2x[i]);
			});
	}
}, _.sleep);
