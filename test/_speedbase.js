// test config
var sz = 768000;
var rounds = 80;
var trials = 8;

var maxSize = require('./_maxsize');
var decimal = (''+1.1).substr(1, 1);
var fmtSpeed = function(size, time) {
	var rate = ('' + (Math.round(100*(size/1048576)/time)/100)).split(decimal);
	
	return ('        ' + rate[0]).substr(-8) + decimal + ((rate[1]|0) + '00').substr(0, 2) + ' MiB/s';
};
module.exports = {
	size: sz,
	rounds: rounds,
	
	bufWorst: new Buffer(sz),
	bufBest: new Buffer(sz),
	bufAvg: [],
	bufAvg2x: [],
	bufTarget: new Buffer(maxSize(sz)),
	
	run: function(name, fn, sz2) {
		var times = Array(trials);
		for(var trial=0; trial<trials; trial++) {
			var p=process.hrtime();
			for(var i=0;i<rounds;i++) fn();
			var t=process.hrtime(p);
			
			times[trial] = t[0] + t[1]/1000000000;
		}
		
		// pick fastest time to try to avoid issues with clockspeed throttling
		var time = Math.min.apply(null, times);
		console.log(
			(name+'                         ').substr(0, 25) + ':'
			+ fmtSpeed(sz*rounds, time)
			+ (sz2 ? (' ' + fmtSpeed(sz2*rounds, time)) : '')
		);
	}
	
};

module.exports.bufWorst.fill(224);
module.exports.bufBest.fill(0);

// use cipher as a fast, consistent RNG
var cipher = require('crypto').createCipher;
[['aes-128-cbc', 'my_incredibly_strong_password'],
 ['rc4', 'my_incredibly_strong_password'],
 ['aes-128-cbc', '9h8a08b08qpklnac']
].forEach(function(cargs) {
	var rand = cipher.apply(null, cargs);
	var data = Buffer.concat([rand.update(module.exports.bufBest), rand.final()]).slice(0, sz);
	module.exports.bufAvg.push(new Buffer(data));
	
	// all yEnc special characters exist in range 0-61 (post shift) or 214-19 (pre-shift)
	// to generate biased data, we'll pack the range down (64-191 will get packed to 192-63)
	for(var i=0; i<data.length; i++) {
		if(data[i] >= 64 && data[i] < 192)
			data[i] = (data[i] + 128) & 0xff;
	}
	module.exports.bufAvg2x.push(data);
});

