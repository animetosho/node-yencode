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

console.log('    Test                     Output rate         Read rate   ');

// warmup
mAvg.forEach(function(buf) {
	var p=process.hrtime();
	for(var j=0;j<_.rounds;j+=2) y.decodeTo(buf, _.bufTarget);
	for(var j=0;j<_.rounds;j+=2) y.decodeNntpTo(buf, _.bufTarget);
	for(var j=0;j<_.rounds;j+=2) y.decodeIncr(buf, 0, _.bufTarget);
	for(var j=0;j<_.rounds;j+=2) y.decodeNntpIncr(buf, 0, _.bufTarget);
	var t=process.hrtime(p);
});

_.run('Worst (all escaping)', y.decodeTo.bind(null, mWorst, _.bufTarget), lenWorst);
_.run('Best (min escaping)',  y.decodeTo.bind(null, mBest, _.bufTarget), lenBest);
_.run('Pass (no escaping)',  y.decodeTo.bind(null, mBest2, _.bufTarget));
_.run('Raw worst', y.decodeNntpTo.bind(null, mWorst, _.bufTarget), lenWorst);
_.run('Raw best',  y.decodeNntpTo.bind(null, mBest, _.bufTarget), lenBest);
_.run('Raw pass',  y.decodeNntpTo.bind(null, mBest2, _.bufTarget));
_.run('Incr worst', y.decodeIncr.bind(null, mWorst, 0, _.bufTarget), lenWorst);
_.run('Incr best',  y.decodeIncr.bind(null, mBest, 0, _.bufTarget), lenBest);
_.run('Incr pass',  y.decodeIncr.bind(null, mBest2, 0, _.bufTarget));
_.run('Incr-raw worst', y.decodeNntpIncr.bind(null, mWorst, 0, _.bufTarget), lenWorst);
_.run('Incr-raw best',  y.decodeNntpIncr.bind(null, mBest, 0, _.bufTarget), lenBest);
_.run('Incr-raw pass',  y.decodeNntpIncr.bind(null, mBest2, 0, _.bufTarget));

mAvg.forEach(function(buf, i) {
	_.run('Random ('+i+')',   y.decodeTo.bind(null, buf, _.bufTarget), lenAvg[i]);
});
mAvg.forEach(function(buf, i) {
	_.run('Raw random ('+i+')',   y.decodeNntpTo.bind(null, buf, _.bufTarget), lenAvg[i]);
});
mAvg.forEach(function(buf, i) {
	_.run('Incr random ('+i+')',  y.decodeIncr.bind(null, buf, 0, _.bufTarget), lenAvg[i]);
});
mAvg.forEach(function(buf, i) {
	_.run('Incr-raw random ('+i+')',  y.decodeNntpIncr.bind(null, buf, 0, _.bufTarget), lenAvg[i]);
});

mAvg2x.forEach(function(buf, i) {
	_.run('Random 2xEsc ('+i+')',   y.decodeTo.bind(null, buf, _.bufTarget), lenAvg2x[i]);
});
mAvg2x.forEach(function(buf, i) {
	_.run('Raw random 2xEsc ('+i+')',   y.decodeNntpTo.bind(null, buf, _.bufTarget), lenAvg2x[i]);
});
mAvg2x.forEach(function(buf, i) {
	_.run('Incr random 2xEsc ('+i+')',  y.decodeIncr.bind(null, buf, 0, _.bufTarget), lenAvg2x[i]);
});
mAvg2x.forEach(function(buf, i) {
	_.run('Incr-raw random 2xEsc ('+i+')',  y.decodeNntpIncr.bind(null, buf, 0, _.bufTarget), lenAvg2x[i]);
});
