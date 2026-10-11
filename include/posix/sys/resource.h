/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	sys/resource.h
 *	What a process used, the limits and priorities a program may ask of
 *	(design 5.8.3): the use is counted only as times, the limits are
 *	fixed, the priorities not used. In place of newlib's, whose struct
 *	rusage has the times only.
 */

#ifndef _SYS_RESOURCE_H_
#define _SYS_RESOURCE_H_

#include <sys/types.h>
#include <sys/time.h>

#define RUSAGE_SELF	0
#define RUSAGE_CHILDREN	-1

struct rusage {
	struct timeval	ru_utime;
	struct timeval	ru_stime;
	long		ru_maxrss;
	long		ru_ixrss;
	long		ru_idrss;
	long		ru_isrss;
	long		ru_minflt;
	long		ru_majflt;
	long		ru_nswap;
	long		ru_inblock;
	long		ru_oublock;
	long		ru_msgsnd;
	long		ru_msgrcv;
	long		ru_nsignals;
	long		ru_nvcsw;
	long		ru_nivcsw;
};

typedef unsigned long long	rlim_t;

struct rlimit {
	rlim_t	rlim_cur;
	rlim_t	rlim_max;
};

#define RLIM_INFINITY	(~(rlim_t)0)
#define RLIM_SAVED_MAX	RLIM_INFINITY
#define RLIM_SAVED_CUR	RLIM_INFINITY

#define RLIMIT_CPU	0
#define RLIMIT_FSIZE	1
#define RLIMIT_DATA	2
#define RLIMIT_STACK	3
#define RLIMIT_CORE	4
#define RLIMIT_RSS	5
#define RLIMIT_MEMLOCK	6
#define RLIMIT_NPROC	7
#define RLIMIT_NOFILE	8
#define RLIMIT_SBSIZE	9
#define RLIMIT_AS	10
#define RLIM_NLIMITS	11

#define PRIO_PROCESS	0
#define PRIO_PGRP	1
#define PRIO_USER	2
#define PRIO_MIN	-20
#define PRIO_MAX	20

#ifdef __cplusplus
extern "C" {
#endif

int	getrusage( int, struct rusage * );
int	getrlimit( int, struct rlimit * );
int	setrlimit( int, const struct rlimit * );
int	getpriority( int, id_t );
int	setpriority( int, id_t, int );

#ifdef __cplusplus
}
#endif

#endif /* _SYS_RESOURCE_H_ */
