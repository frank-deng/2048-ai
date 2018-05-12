#include <sys/sysinfo.h>

int nprocs(){
	return get_nprocs();
}
