#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/resource.h>
#include <stdint.h>

// API to read per thread/task energy
int proc_pid_rusage(int pid, int flavor, rusage_info_t *buffer);

void start_routine(void *arg) {
	pid_t pid = getpid();
	uint64_t last = 0;	
	while(1) {
		rusage_info_current usage;
		proc_pid_rusage(pid, RUSAGE_INFO_CURRENT, &usage); 
		syslog(LOG_WARNING, "inject.c: ri_energy_nj = %llu\n", usage.ri_energy_nj);	
		syslog(LOG_WARNING, "inject.c: power = %fw\n", (usage.ri_energy_nj - last) / 1e9);	
		last = usage.ri_energy_nj;
		sleep(1);
	}
}

// ask linker to put our function to __mod_init_func section
__attribute__((constructor))
static void __main(int argc, const char **argv) {
	syslog(LOG_WARNING, "inject.c: started...\n");	
	pthread_t p;
	pthread_create(&p, NULL, start_routine, NULL);	
}
