// test config (defaults)
var sz = 768000;
var rounds = 80;
var trials = 8;
var asyncWait = 1000;

var maxSize = require('./_maxsize');
var decimal = (''+1.1).substr(1, 1);
var fmtSpeed = function(size, time) {
	var rate = ('' + (Math.round(100*(size/1048576)/time)/100)).split(decimal);
	
	return ('        ' + rate[0]).substr(-8) + decimal + ((rate[1]|0) + '00').substr(0, 2) + ' MiB/s';
};
var initBuffers = function() {
	module.exports.bufWorst = new Buffer(sz);
	module.exports.bufBest = new Buffer(sz);
	module.exports.bufAvg = [];
	module.exports.bufAvg2x = [];
	module.exports.bufTarget = new Buffer(maxSize(sz));

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
};
module.exports = {
	size: sz,
	rounds: rounds,
	sleep: 0,
	avgOnly: false,
	decMethods: {'clean':true, 'raw':true, 'incr':true, 'rawincr':true},
	
	bufWorst: null,
	bufBest: null,
	bufAvg: null,
	bufAvg2x: null,
	bufTarget: null,
	
	bench: function(fn) {
		var times = Array(trials);
		for(var trial=0; trial<trials; trial++) {
			var p=process.hrtime();
			for(var i=0;i<rounds;i++) fn();
			var t=process.hrtime(p);
			
			times[trial] = t[0] + t[1]/1000000000;
		}
		// pick fastest time to try to avoid issues with clockspeed throttling
		return Math.min.apply(null, times);
	},
	_benchAsync: function(fn, cb, trials, results) {
		var p=process.hrtime();
		for(var i=0;i<rounds;i++) fn();
		results.push(process.hrtime(p));
		
		if(--trials)
			setTimeout(module.exports._benchAsync.bind(null, fn, cb, trials, results), asyncWait);
		else
			cb(Math.min.apply(null, results));
	},
	benchAsync: function(fn, cb) {
		setTimeout(function() {
			module.exports._benchAsync(fn, cb, trials, []);
		}, asyncWait);
	},
	run: function(name, fn, sz2) {
		var time = module.exports.bench(fn);
		console.log(
			(name+'                         ').substr(0, 25) + ':'
			+ fmtSpeed(sz*rounds, time)
			+ (sz2 ? (' ' + fmtSpeed(sz2*rounds, time)) : '')
		);
	},
	
	parseArgs: function(helpText) {
		process.argv.forEach(function(arg) {
			arg = arg.toLowerCase();
			if(arg == '-h' || arg == '--help' || arg == '-?') {
				console.log(helpText + ' [{-z|--size}=bytes('+sz+')] [{-r|--rounds}=num('+rounds+')] [{-t|--trials}=num('+trials+')]');
				process.exit(0);
			}
			if(arg == '-a' || arg == '--average-only') {
				module.exports.avgOnly = true;
			}
			
			var m = arg.match(/^(-s=?|--sleep=)(\d+)$/);
			if(m)
				module.exports.sleep = m[2] |0;
			
			m = arg.match(/^(-m=?|--methods=)([a-z,]+)$/);
			if(m) {
				var methods = module.exports.decMethods;
				for(var k in methods)
					methods[k] = false;
				var setAMethod = false;
				m[2].split(',').forEach(function(meth) {
					if(meth in methods) {
						setAMethod = true;
						methods[meth] = true;
					}
				});
				if(!setAMethod) {
					console.log('No valid method specified');
					process.exit(1);
				}
			}
			
			m = arg.match(/^(-t=?|--trials=)(\d+)$/);
			if(m)
				trials = m[2] |0;
			
			m = arg.match(/^(-r=?|--rounds=)(\d+)$/);
			if(m) {
				rounds = m[2] |0;
				module.exports.rounds = rounds;
			}
			
			m = arg.match(/^(-z=?|--size=)(\d+)$/);
			if(m) {
				sz = m[2] |0;
				module.exports.size = sz;
				initBuffers();
			}
			
		});
	}
	
};

initBuffers();
