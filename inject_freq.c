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

#define SAMPLE_INTERVAL_SECOND 1
#define THREADLIST_UPDATE 6

// see https://github.com/apple-oss-distributions/xnu/blob/5c2921b07a2480ab43ec66f5b9e41cb872bc554f/bsd/sys/proc_info.h#L898

struct proc_threadcounts_data {
    uint64_t ptcd_instructions;
    uint64_t ptcd_cycles;
    uint64_t ptcd_user_time_mach;
    uint64_t ptcd_system_time_mach;
    uint64_t ptcd_energy_nj;
};

struct proc_threadcounts {
    uint16_t ptc_len;
    uint16_t ptc_reserved0;
    uint32_t ptc_reserved1;
    struct proc_threadcounts_data ptc_counts[];
};

#define PROC_PIDTHREADCOUNTS 34
#define PROC_PIDTHREADCOUNTS_SIZE (sizeof(struct proc_threadcounts))

int proc_pidinfo(int pid, int flavor, uint64_t arg, void *buffer, int buffersize);

static thread_act_array_t threads;
static mach_msg_type_number_t thread_count = 0;
static task_t this_task;
static thread_t this_thread;
static uint64_t tids[200];

static mach_timebase_info_data_t _clock_timebase;


// for sampling 
static struct proc_threadcounts *start = NULL, *last = NULL;
static int countsize = 0;

static inline double mach_time_to_seconds(uint64_t mach_time) {
   double nanos = (mach_time * _clock_timebase.numer) / _clock_timebase.denom;
   return nanos / 1e9;
}

static inline void update_threadlist() {
	mach_msg_type_number_t tmp = thread_count;
	task_threads(this_task, &threads, &thread_count);
	if(thread_count != tmp) {
		start = realloc(start, countsize * thread_count);
		last = realloc(last, countsize * thread_count);
	}

	for (int i = 0; i < thread_count; ++i) {
		pthread_t pt = pthread_from_mach_thread_np(threads[i]);
		pthread_threadid_np(pt, &tids[i]);
	}
}

void start_routine(void *arg) {
	pthread_set_qos_class_self_np(QOS_CLASS_BACKGROUND, 0);	
	double time_elaspe_last = 0, time_elaspe_last_ecore = 0;
	uint64_t cycle_per_second = 0, cycle_per_second_ecore = 0;
	pid_t pid = getpid();

	while(1) {
		uint64_t start_time = mach_absolute_time();
		update_threadlist();

		int si = 0;
		for(int i = 0; i < thread_count; i++) {
			struct proc_threadcounts *start_si = (struct proc_threadcounts*)((uint8_t*)start + si * countsize);

			int ret = proc_pidinfo(pid, PROC_PIDTHREADCOUNTS, tids[i], start_si, countsize);

			if(ret == 88) {
				start_si->ptc_reserved1 = (uint32_t)tids[i];
				si++;
			}
		}

		// Here we update the threadlist every 5s
		for(int j = 0; j < THREADLIST_UPDATE; j++) {
			usleep((SAMPLE_INTERVAL_SECOND + 0.3f - mach_time_to_seconds(mach_absolute_time() - start_time)) * 1e6);
			for(int i = 0; i < si; i++) {
				start_time = mach_absolute_time();
				struct proc_threadcounts *start_si = (struct proc_threadcounts*)((uint8_t*)start + i * countsize);
				struct proc_threadcounts *last_si = (struct proc_threadcounts*)((uint8_t*)last + i * countsize);
				
				int ret = proc_pidinfo(pid, PROC_PIDTHREADCOUNTS, start_si->ptc_reserved1, last_si, countsize);
				last_si->ptc_reserved1 = start_si->ptc_reserved1;
				// incase the thread exists.
				if(ret != 88) continue;

				struct proc_threadcounts_data *p1 = &(start_si->ptc_counts[0]);
				struct proc_threadcounts_data *p2 = &(last_si->ptc_counts[0]);

				double time_elaspe = mach_time_to_seconds((p2->ptcd_user_time_mach + p2->ptcd_system_time_mach) \
					- (p1->ptcd_user_time_mach + p1->ptcd_system_time_mach));

				// We use the freuqnecy of the thread that has highest execution time
				if(time_elaspe > time_elaspe_last) {
					cycle_per_second = (p2->ptcd_cycles - p1->ptcd_cycles) / time_elaspe;
					time_elaspe_last = time_elaspe;
				}

				// ECore
				p1 = &(start_si->ptc_counts[1]);
				p2 = &(last_si->ptc_counts[1]);

				time_elaspe = mach_time_to_seconds((p2->ptcd_user_time_mach + p2->ptcd_system_time_mach) \
					- (p1->ptcd_user_time_mach + p1->ptcd_system_time_mach));

				// We use the freuqnecy of the thread that has highest execution time
				if(time_elaspe > time_elaspe_last_ecore) {
					cycle_per_second_ecore = (p2->ptcd_cycles - p1->ptcd_cycles) / time_elaspe;
					time_elaspe_last_ecore = time_elaspe;
				}
			}

			syslog(LOG_WARNING, "inject.c: Frequency: %fMhz(P) %fMhz(E)\n", cycle_per_second / 1e6, cycle_per_second_ecore / 1e6);
			struct proc_threadcounts *tmp = start;
			start = last;
			last = tmp;
			time_elaspe_last = 0;
			time_elaspe_last_ecore = 0;

			cycle_per_second = 0;
			cycle_per_second_ecore = 0;
		}
	}
}

// ask linker to put our function to __mod_init_func section
__attribute__((constructor))
static void __main(int argc, const char **argv) {
	syslog(LOG_WARNING, "inject.c: started...\n");

	// initialization
	mach_timebase_info(&_clock_timebase);
	this_task = mach_task_self();
	this_thread = mach_thread_self();
	countsize = sizeof(struct proc_threadcounts) + 2 * sizeof(struct proc_threadcounts_data);

	pthread_t p;
	pthread_create(&p, NULL, (void * _Nullable (* _Nonnull)(void * _Nullable))start_routine, NULL);	
}
