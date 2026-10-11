/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	hello.c
 *	A program loaded from the file system and run as a process
 *	(design 9.14). It proves the whole path: the loader maps the
 *	image, the main task runs it at protection level 3 in its own
 *	space, system calls reach the kernel, and the exit code comes back
 *	to whoever started it.
 *
 *	The start-up argument the creator passed is mapped read only at
 *	ARG_PAGE; its first four bytes are the exit code to return.
 *
 *	Built with HELLO_CMD it is the command hello, linked from bin (design
 *	16.5.19): its argument is then the words typed after its name, a
 *	line of UTF-8, which it prints; the first word, when it is a number,
 *	is the exit code, and 0 otherwise.
 */

typedef unsigned char		UB;
typedef unsigned int		UW;
typedef signed int		INT;
typedef unsigned long		UD;

#define ARG_PAGE		((const volatile UW *)0x300000UL)

extern void ts_ext_prc( INT exitcd );
extern INT  ts_get_pid( void );
extern INT  ts_get_mono( UD *p_ns );
extern INT  tk_get_tid( void );
extern INT  tk_dly_tsk( UW ms );
extern INT  tm_putstring( const UB *s );
extern long ts_get_arg( void *buf, long size );

/* One message of the process queue (kernel include/ts/proc.h) */
#define TS_MSG_MAX	256
typedef struct {
	UW	type;
	INT	from;
	long	size;
	UB	body[TS_MSG_MAX];
} T_TSMSG;

extern INT  ts_rcv_msg( T_TSMSG *buf, INT tmout );
extern INT  ts_snd_msg( INT pid, const T_TSMSG *msg, INT tmout );

/* The data and bss sections prove that the loader mapped them writable */
static const char	greeting[] = "hello from a TessronOS process\n";
static char		scratch[64];
static UW		counter;

static void put( const char *s )
{
	tm_putstring((const UB *)s);
}

static void put_dec( INT v )
{
	char	b[12];
	INT	i = 11;

	b[i--] = '\0';
	if ( v == 0 ) b[i--] = '0';
	while ( v > 0 && i >= 0 ) { b[i--] = (char)('0' + (v % 10)); v /= 10; }
	put(&b[i + 1]);
}

int main( void )
{
	UD	t0 = 0, t1 = 0;
	INT	i;
	UW	code;

	put(greeting);

	put("  pid=");
	put_dec(ts_get_pid());
	put(" tid=");
	put_dec(tk_get_tid());
	put("\n");

	/* writable data, and a system call that blocks */
	for ( i = 0; i < (INT)sizeof(scratch); i++ ) scratch[i] = (char)('a' + (i % 26));
	scratch[sizeof(scratch) - 1] = '\0';
	counter = 0;

	ts_get_mono(&t0);
	for ( i = 0; i < 3; i++ ) {
		tk_dly_tsk(10);
		counter++;
	}
	ts_get_mono(&t1);

	put("  slept ");
	put_dec((INT)((t1 - t0) / 1000000));
	put(" ms over ");
	put_dec((INT)counter);
	put(" waits, scratch=");
	put(scratch);
	put("\n");

#ifdef HELLO_CMD
	{
		static char	words[256];
		long		n = ts_get_arg(words, (long)sizeof(words) - 1);
		const char	*w = words;

		code = 0;
		if ( n > 0 ) {
			words[( n < (long)sizeof(words) ) ? n : (long)sizeof(words) - 1] = '\0';
			put("  arguments: ");
			put(words);
			put("\n");
			while ( *w >= '0' && *w <= '9' ) code = code * 10 + (UW)( *w++ - '0' );
			if ( w == words || ( *w != '\0' && *w != ' ' ) ) code = 0;
		}
		put("  exit code ");
		put_dec((INT)code);
		put("\n");
		return (int)code;
	}
#endif
	code = ARG_PAGE[0];		/* the exit code the creator asked for */

	/*
	 * Second word of the argument: when it is 1, wait for a message and
	 * take the exit code from its first byte instead.
	 */
	if ( ARG_PAGE[1] == 1 ) {
		static T_TSMSG	msg;

		put("  waiting for a message\n");
		if ( ts_rcv_msg(&msg, 60000) == 0 ) {
			put("  got type=");
			put_dec((INT)msg.type);
			put(" from=");
			put_dec(msg.from);
			put(" size=");
			put_dec((INT)msg.size);
			put("\n");
			code = msg.body[0];
		} else {
			put("  no message came\n");
			code = 0xff;
		}
	}

	return (int)code;
}
