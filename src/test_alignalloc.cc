// used by GYP to test if posix_memalign needs declarations
#include "common.h"
void f() {
	void* p;
	ALIGN_ALLOC(p, 16, 16);
}
