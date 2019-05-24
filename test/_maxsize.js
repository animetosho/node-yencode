"use strict";
module.exports = function(length, line_size) {
	if(!length) return 0;
	return Math.ceil(length*2) // all characters escaped
		+ 2 * Math.floor((length*2) / (line_size||128)) // newlines, considering the possibility of all chars escaped
		+ 2 // allocation for offset and that a newline may occur early
		+ 64 // extra space just in case things go awry... just kidding, it's just extra padding to make SIMD logic easier
	;
};
