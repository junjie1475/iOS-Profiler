#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/resource.h>
#include <stdint.h>
#include <time.h>
#include <sys/qos.h>
#include <mach/mach_time.h>

#define SAMPLE_RATE_NS 500000000L / 2
// API to read per thread/task energy
int proc_pid_rusage(int pid, int flavor, rusage_info_t *buffer);
mach_timebase_info_data_t _clock_timebase;

static inline double mach_time_to_seconds(uint64_t mach_time) {
   double nanos = (mach_time * _clock_timebase.numer) / _clock_timebase.denom;
   return nanos / 1e9;
}

void start_routine(void *arg) {
	pthread_set_qos_class_self_np(QOS_CLASS_BACKGROUND, 0);	
	pid_t pid = getpid();
	uint64_t last = 0;	
	uint64_t last_mach = 0;	
	struct timespec tim, tim2;
   tim.tv_sec = 0;
   tim.tv_nsec = SAMPLE_RATE_NS;

	while(1) {
		rusage_info_current usage;
		proc_pid_rusage(pid, RUSAGE_INFO_CURRENT, &usage); 
		uint64_t delta = mach_absolute_time() - last_mach;

		syslog(LOG_WARNING, "inject.c: power = %fw\n", ((usage.ri_energy_nj - last) / 1e9) * (1 / mach_time_to_seconds(delta)));	
		last = usage.ri_energy_nj;
		last_mach = delta + last_mach;
		nanosleep(&tim , &tim2);
	}
}

// ask linker to put our function to __mod_init_func section
__attribute__((constructor))
static void __main(int argc, const char **argv) {
	syslog(LOG_WARNING, "inject.c: started...\n");	
	mach_timebase_info(&_clock_timebase);
	pthread_t p;
	pthread_create(&p, NULL, start_routine, NULL);	
}
