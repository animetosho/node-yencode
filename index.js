"use strict";

var y = require('./build/Release/yencode.node');

var toBuffer = Buffer.alloc ? Buffer.from : Buffer;

var nl = toBuffer([13, 10]);
var RE_BADCHAR = /\r\n\0/g;

module.exports = {
	encoding: 'utf8',
	
	encode: y.encode,
	encodeTo: y.encodeTo,
	decode: y.decode,
	decodeTo: y.decodeTo,
	decodeNntp: y.decodeNntp,
	decodeNntpTo: y.decodeNntpTo,
	
	minSize: function(length, line_size) {
		if(!length) return 0;
		return length // no characters escaped
			+ 2 * Math.floor(length / (line_size||128)) // newlines
		);
	},
	maxSize: function(length, line_size, esc_ratio) {
		if(!length) return 0;
		if(!esc_ratio || esc_ratio !== 0)
			esc_ratio = 2;
		else
			esc_ratio++;
		if(esc_ratio < 1 || esc_ratio > 2)
			throw new Error('yEnc escape ratio must be between 0 and 1');
		return
			  Math.ceil(length*esc_ratio) // all characters escaped
			+ 2 * Math.floor((length*esc_ratio) / (line_size||128)) // newlines, considering the possibility of all chars escaped
			+ 2 // allocation for offset and that a newline may occur early
			+ 32 // extra space just in case things go awry... just kidding, it's just extra padding to make SIMD logic easier
		;
	},
	// TODO: check ordering of CRC32
	crc32: y.crc32,
	crc32_combine: y.crc32_combine,
	crc32_zeroes: y.crc32_zeroes,
	
	post: function(filename, data, line_size) {
		if(!line_size) line_size = 128;
		
		if(!Buffer.isBuffer(data)) data = toBuffer(data);
		
		filename = toBuffer(filename.replace(RE_BADCHAR, '').substr(0, 256), exports.encoding);
		return Buffer.concat([
			toBuffer('=ybegin line='+line_size+' size='+data.length+' name='),
			filename, nl,
			y.encode(data, line_size),
			toBuffer('\r\n=yend size='+data.length+' crc32=' + y.crc32(data).toString('hex'))
		]);
	},
	multi_post: function(filename, size, parts, line_size) {
		return new YEncoder(filename, size, parts, line_size);
	}
};

function YEncoder(filename, size, parts, line_size) {
	if(!line_size) line_size = 128;
	
	this.size = size;
	this.parts = parts;
	this.line_size = line_size;
	
	this.part = 0;
	this.pos = 0;
	this.crc = toBuffer([0,0,0,0]);
	
	filename = toBuffer(filename.replace(RE_BADCHAR, '').substr(0, 256), exports.encoding);
	if(parts > 1) {
		this.yInfo = Buffer.concat([
			toBuffer(' total='+parts+' line='+line_size+' size='+size+' name='),
			filename, nl
		]);
	} else {
		this.yInfo = Buffer.concat([
			toBuffer('=ybegin line='+line_size+' size='+size+' name='),
			filename, nl
		]);
		this.encode = this._encodeSingle;
	}
}
var singleEncodeError = function() {
	throw new Error('Exceeded number of specified yEnc parts');
};
YEncoder.prototype = {
	encode: function(data) {
		if(!Buffer.isBuffer(data)) data = toBuffer(data);
		
		this.part++;
		if(this.part > this.parts)
			throw new Error('Exceeded number of specified yEnc parts');
		var end = this.pos + data.length;
		if(end > this.size)
			throw new Error('Exceeded total file size');
		
		var yInfo = this.yInfo;
		var crc = y.crc32(data), fullCrc = '';
		this.crc = y.crc32_combine(this.crc, crc, data.length);
		if(this.part == this.parts) {
			// final part treated slightly differently
			if(end != this.size)
				throw new Error('File size doesn\'t match total data length');
			fullCrc = ' crc32='+this.crc.toString('hex');
			this.yInfo = null;
		}
		
		var ret = Buffer.concat([
			toBuffer('=ybegin part='+this.part),
			yInfo,
			toBuffer('=ypart begin='+( this.pos+1 )+' end='+end+'\r\n'),
			y.encode(data, this.line_size),
			toBuffer('\r\n=yend size='+data.length+' part='+this.part+' pcrc32='+crc.toString('hex')+fullCrc)
		]);
		
		this.pos = end;
		return ret;
	},
	
	_encodeSingle: function(data) {
		if(!Buffer.isBuffer(data)) data = toBuffer(data);
		
		if(this.size != data.length)
			throw new Error('File size doesn\'t match total data length');
		this.encode = singleEncodeError;
		
		this.part = 1;
		this.pos = data.length;
		this.crc = y.crc32(data);
		
		var yInfo = this.yInfo;
		this.yInfo = null;
		return Buffer.concat([
			yInfo,
			y.encode(data, this.line_size),
			toBuffer('\r\n=yend size='+data.length+' crc32=' + this.crc.toString('hex'))
		]);
	}
};
