"use strict";

var y = require('./build/Release/yencode.node');

var toBuffer = Buffer.alloc ? Buffer.from : Buffer;

var nl = toBuffer([13, 10]);
var RE_BADCHAR = /\r\n\0/g;
var RE_YPROP = /([a-z_][a-z_0-9]*)=/;
var RE_NUMBER = /^\d+$/;
var decodePrev = ['\r\n', '=', '\r', '', '\r\n.', '\r\n.\r', '\r\n='];

var DecoderError = function(code, message) {
	var err = new Error(message);
	err.code = code;
	return err;
}
var DecoderWarning = function(code, message) {
	if(!this) return new DecoderWarning(code, message);
	this.code = code;
	this.message = message;
}
DecoderWarning.prototype = { toString: function() { return this.message; } };

var bufferFind, bufferFindRev;
if(Buffer.prototype.indexOf)
	bufferFind = function(buf, search, start) {
		return buf.indexOf(search, start, module.exports.encoding);
	};
else
	bufferFind = function(buf, search, start) {
		if(!Buffer.isBuffer(search))
			search = toBuffer(search, module.exports.encoding);
		if(search.length == 0) return -1;
		start = start|0;
		if(search.length > buf.length-start) return -1;
		
		for(var i = start; i < buf.length - search.length + 1; i++) {
			var match = true;
			for(var j = 0; j < search.length; j++) {
				if(buf[i+j] != search[j]) {
					match = false;
					break;
				}
			}
			if(match) return i;
		}
		return -1;
	};
if(Buffer.prototype.lastIndexOf)
	bufferFindRev = function(buf, search) {
		return buf.lastIndexOf(search, -1, module.exports.encoding);
	};
else
	bufferFindRev = function(buf, search) {
		if(!Buffer.isBuffer(search))
			search = toBuffer(search, module.exports.encoding);
		if(search.length == 0) return -1;
		if(search.length > buf.length) return -1;
		
		for(var i = buf.length-search.length; i >= 0; i--) {
			var match = true;
			for(var j = 0; j < search.length; j++) {
				if(buf[i+j] != search[j]) {
					match = false;
					break;
				}
			}
			if(match) return i;
		}
		return -1;
	};

var decoderParseLines = function(lines, ydata) {
	var warnings = [];
	for(var i=0; i<lines.length; i++) {
		var yprops = {};
		
		var line = lines[i].substr(2); // cut off '=y'
		// parse tag
		var p = line.indexOf(' ');
		var tag = (p<0 ? line : line.substr(0, p));
		line = line.substr(tag.length+1).trim();
		
		// parse props
		var m = line.match(RE_YPROP);
		while(m) {
			if(m.index != 0) {
				warnings.push(DecoderWarning('ignored_line_data', 'Unknown additional data ignored: "' + line.substr(0, m.index) + '"'));
			}
			var prop = m[1], val;
			var valPos = m.index + m[0].length;
			if(tag == 'begin' && prop == 'name') {
				// special treatment of filename - the value is the rest of the line (can include spaces)
				val = line.substr(valPos);
				line = '';
			} else {
				p = line.indexOf(' ', valPos);
				val = (p<0 ? line.substr(valPos) : line.substr(valPos, p-valPos));
				line = line.substr(valPos + val.length +1);
			}
			if(prop in yprops) {
				warnings.push(DecoderWarning('duplicate_property', 'Duplicate property encountered: `' + prop + '`'));
			}
			yprops[prop] = val;
			m = line.match(RE_YPROP);
		}
		if(line != '') {
			warnings.push(DecoderWarning('ignored_line_data', 'Unknown additional end-of-line data ignored: "' + line + '"'));
		}
		
		if(tag in ydata) {
			warnings.push(DecoderWarning('duplicate_line', 'Duplicate line encountered: `' + tag + '`'));
		}
		ydata[tag] = yprops;
	}
	return warnings;
};

var propIsNotValidNumber = function(prop) {
	return prop && !RE_NUMBER.test(prop);
};

module.exports = {
	encoding: 'utf8',
	
	encode: y.encode,
	encodeTo: y.encodeTo,
	decode: y.decode,
	decodeTo: y.decodeTo,
	
	decodeChunk: function(data, output, prev) {
		// both output and prev are optional
		if(typeof output !== 'undefined' && !Buffer.isBuffer(output)) {
			prev = output;
			output = null;
		}
		if(prev === null || typeof prev === 'undefined')
			prev = '\r\n';
		
		if(Buffer.isBuffer(prev)) prev = prev.toString();
		prev = prev.substr(-4); // only care about the last 4 chars of previous state
		if(prev == '\r\n.=') prev = '\r\n='; // aliased after dot stripped
		if(data.length == 0) return {
			read: 0,
			written: 0,
			output: toBuffer([]),
			ended: 0,
			state: prev
		};
		var state = decodePrev.indexOf(prev);
		if(state < 0) {
			for(var l=-3; l<0; i++) {
				state = decodePrev.indexOf(prev.substr(l));
				if(state >= 0) break;
			}
			if(state < 0) state = decodePrev.indexOf('');
		}
		var ret = y.decodeIncr(data, state, output);
		
		return {
			read: ret.read,
			written: ret.written,
			output: ret.output || output,
			ended: !!ret.ended,
			state: [decodePrev[ret.state], '\r\n=y', '\r\n.\r\n'][ret.ended]
		};
	},
	
	minSize: function(length, line_size) {
		if(!length) return 0;
		return length // no characters escaped
			+ 2 * Math.floor(length / (line_size||128)); // newlines
	},
	maxSize: function(length, line_size, esc_ratio) {
		if(!length) return 0;
		if(!esc_ratio && esc_ratio !== 0)
			esc_ratio = 2;
		else
			esc_ratio++;
		if(esc_ratio < 1 || esc_ratio > 2)
			throw new Error('yEnc escape ratio must be between 0 and 1');
		return Math.ceil(length*esc_ratio) // all characters escaped
			+ 2 * Math.floor((length*esc_ratio) / (line_size||128)) // newlines, considering the possibility of all chars escaped
			+ 2 // allocation for offset and that a newline may occur early
			+ 64 // extra space just in case things go awry... just kidding, it's just extra padding to make SIMD logic easier
		;
	},
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
	},
	from_post: function(data, isRaw) {
		if(!Buffer.isBuffer(data))
			throw new TypeError('Expected string or Buffer');
		
		var ret = {};
		
		// find '=ybegin' to know where the yEnc data starts
		var yencStart;
		if(data.slice(0, 8).toString('hex') == '3d79626567696e20' /*=ybegin */) {
			// common case: starts right at the beginning
			yencStart = 0;
		} else {
			// otherwise, we have to search for the beginning marker
			yencStart = bufferFind(data, '\r\n=ybegin ');
			if(yencStart < 0)
				return DecoderError('no_start_found', 'yEnc start marker not found');
			yencStart += 2;
		}
		ret.yencStart = yencStart;
		
		// find all start lines
		var lines = [];
		var sp = yencStart;
		var p = bufferFind(data, '\r\n', yencStart+8);
		while(p > 0) {
			var line = data.slice(sp, p).toString(this.encoding).trim();
			lines.push(line);
			sp = p+2;
			if(line.substr(0, 6) == '=yend ') { // no data in post
				ret.yencEnd = sp;
				break;
			}
			
			if(data[sp] != 0x3d /*=*/ || data[sp+1] != 0x79 /*y*/) {
				ret.dataStart = sp;
				break;
			}
			p = bufferFind(data, '\r\n', sp);
		}
		if(!ret.dataStart && !ret.yencEnd) // reached end of data but '=yend' not found
			return DecoderError('no_end_found', 'yEnd end marker not found');
		
		var ydata = {};
		var warnings = decoderParseLines(lines, ydata);
		
		if(!ret.yencEnd) {
			var yencEnd = bufferFindRev(data.slice(ret.dataStart), '\r\n=yend ');
			if(yencEnd < 0)
				return DecoderError('no_end_found', 'yEnd end marker not found');
			
			yencEnd += ret.dataStart;
			ret.dataEnd = yencEnd;
			p = bufferFind(data, '\r\n', yencEnd+8);
			if(p < 0) {
				warnings.push(DecoderWarning('missing_yend_newline', 'No line terminator found for =yend line'));
				p = data.length;
				ret.yencEnd = p;
			} else
				ret.yencEnd = p+2;
			var endLine = data.slice(yencEnd+2, p).toString(this.encoding).trim();
			
			warnings = warnings.concat(decoderParseLines([endLine], ydata));
		}
		
		ret.props = ydata;
		// check properties
		if(!ydata.begin.line || !ydata.begin.size || !('name' in ydata.begin)) // required properties, according to yEnc 1.2 spec
			return DecoderError('missing_required_properties', 'Could not find line/size/name properties on ybegin line');
		if(!ydata.end.size)
			return DecoderError('missing_required_properties', 'Could not find size properties on yend line');
		
		// check numerical fields
		var NUMERICAL_FIELDS = {
			begin: ['line', 'total', 'part', 'size'],
			part: ['begin', 'end'],
			end: ['size', 'part']
		};
		for(var tag in NUMERICAL_FIELDS) {
			if(!ydata[tag]) continue;
			NUMERICAL_FIELDS[tag].forEach(function(key) {
				if(!ydata[tag][key]) return;
				if(!RE_NUMBER.test(ydata[tag][key]))
					warnings.push(DecoderWarning('invalid_prop_'+key, '`'+key+'` is not a number'));
				else if(ydata[tag][key] === '0' && key != 'size') // most fields have to be at least one
					warnings.push(DecoderWarning('zero_prop_'+key, '`'+key+'` cannot be 0'));
			});
		}
		if(ydata.begin.part && ydata.end.part && ydata.begin.part != ydata.end.part)
			warnings.push(DecoderWarning('part_number_mismatch', 'Part number specified in begin and end do not match'));
		else if(ydata.begin.total && ((ydata.begin.part && +ydata.begin.total < +ydata.begin.part) || (ydata.end.part && +ydata.begin.total < +ydata.end.part)))
			warnings.push(DecoderWarning('part_number_exceeds_total', 'Specified part number exceeds specified total'));
		
		var expectedSize = +ydata.end.size;
		if(ydata.part && ydata.part.begin && ydata.part.end) {
			var partBegin = +ydata.part.begin;
			var partEnd = +ydata.part.end;
			if(partBegin > partEnd)
				warnings.push(DecoderWarning('invalid_part_range', 'begin offset cannot exceed end offset'));
			else if(expectedSize != partEnd-partBegin +1)
				warnings.push(DecoderWarning('size_mismatch_part_range', 'Specified size does not match part range'));
			else if(partEnd > +ydata.begin.size)
				warnings.push(DecoderWarning('part_range_exceeds_size', 'Specified part range exceeds total file size'));
		} else if(+ydata.begin.total > 1 || +ydata.begin.part > 1 || +ydata.end.part > 1)
			warnings.push(DecoderWarning('missing_part_range', 'Part range not specified for multi-part post'));
		
		['pcrc32','crc32'].forEach(function(prop) {
			if(ydata.end[prop] && !/^[a-fA-F0-9]{8}$/.test(ydata.end[prop]))
				warnings.push(DecoderWarning('invalid_prop_'+prop, '`'+prop+'` is not a valid CRC32 value'));
		});
		if(!ydata.begin.part && ydata.begin.size != ydata.end.size)
			warnings.push(DecoderWarning('size_mismatch', 'Size specified in begin and end do not match'));
		else if(+ydata.begin.size < +ydata.end.size)
			warnings.push(DecoderWarning('size_mismatch', 'Size specified for part exceeds size specified for whole file'));
		
		if(ret.dataStart) {
			ret.data = y.decode(data.slice(ret.dataStart, ret.dataEnd), isRaw);
			ret.crc32 = y.crc32(ret.data);
			var hexCrc = ret.crc32.toString('hex');
			
			if(expectedSize != ret.data.length)
				warnings.push(DecoderWarning('data_size_mismatch', 'Decoded data length doesn\'t match size specified in yend'));
			if(ydata.end.pcrc32 && hexCrc != ydata.end.pcrc32.toLowerCase())
				warnings.push(DecoderWarning('pcrc32_mismatch', 'Specified pcrc32 is invalid'));
			if(ydata.end.crc32 && !ydata.part && hexCrc != ydata.end.crc32.toLowerCase())
				// if single part, check CRC32 as well
				warnings.push(DecoderWarning('crc32_mismatch', 'Specified crc32 is invalid'));
		} else {
			// empty article
			if(expectedSize != 0)
				warnings.push(DecoderWarning('data_size_mismatch', 'Decoded data length doesn\'t match size specified in yend'));
			if(ydata.end.pcrc32 && ydata.end.pcrc32 != '00000000')
				warnings.push(DecoderWarning('pcrc32_mismatch', 'Specified pcrc32 is invalid'));
			if(ydata.end.crc32 && !ydata.part && ydata.end.crc32 != '00000000')
				warnings.push(DecoderWarning('crc32_mismatch', 'Specified crc32 is invalid'));
		}
		
		if(warnings.length)
			ret.warnings = warnings;
		
		return ret;
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
