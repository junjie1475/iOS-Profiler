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
#include <mach/mach.h>
static thread_act_array_t threads;
static mach_msg_type_number_t thread_count = 0;
static task_t this_task;

static uint64_t tid;

void start_routine(void **args) {
	struct sched_param param_c;
   param_c.sched_priority = 6;
   pthread_setschedparam(pthread_self(), SCHED_OTHER, &param_c);

	struct sched_param param;
	param.sched_priority = 47;
	uint64_t current_tid;
	pthread_threadid_np(pthread_self(), &current_tid);

	while(1){
		usleep(500000 / 8 / 2);
		mach_msg_type_number_t thread_count_prev = thread_count;
		task_threads(this_task, &threads, &thread_count);
//		if(thread_count == thread_count_prev) continue;

		// set them to P Core;
		for (int i = 0; i < thread_count; ++i) {
			pthread_t pt = pthread_from_mach_thread_np(threads[i]);
			pthread_threadid_np(pt, &tid);
			if(tid != current_tid) {
				pthread_setschedparam(pt, SCHED_OTHER, &param);
			}
		}
	}
}
// ask linker to put our function to __mod_init_func section
__attribute__((constructor))
static void __main(int argc, const char **argv) {
	syslog(LOG_WARNING, "inject.c: started...\n");

	// initialization
	// mach_timebase_info(&_clock_timebase);
	this_task = mach_task_self();
	// this_thread = mach_thread_self();

	pthread_t p;
	pthread_create(&p, NULL, (void * _Nullable (* _Nonnull)(void * _Nullable))start_routine, NULL);	
}
