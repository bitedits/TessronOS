/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	bashext.c
 *	What tsh takes of the way bash is written (design 5.8.8), besides
 *	what the POSIX shell it is made from has: brace expansion
 *	({a,b} and {1..9}), the backslash escapes of PS1 (\u \h \w ...),
 *	history expansion with ! and ^old^new, the history and shopt
 *	builtins, source. The arrays and [[ ]] are in basharr.c.
 *
 *	None of it is there when the shell is started as sh (or with
 *	POSIXLY_CORRECT set): it is then the POSIX shell.
 */

#include <sys/types.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "shell.h"
#include "parser.h"
#include "nodes.h"
#include "expand.h"
#include "eval.h"
#include "var.h"
#include "options.h"
#include "output.h"
#include "memalloc.h"
#include "mystring.h"
#include "error.h"
#include "builtins.h"
#include "myhistedit.h"
#include "bashext.h"

int	tshext = 1;		/* the ways of bash are taken */
int	sh_nullglob, sh_dotglob, sh_nocaseglob, sh_nocasematch, sh_extglob;
int	sh_histappend = 1, sh_checkwinsize = 1, sh_expand_aliases = 1, sh_globstar;

/* ---------------------------------------------------------------- brace expansion */

#define BR_MAX		4096	/* words one word may become */

/*
 * The end of what starts at p, a part of a word not to look into: a
 * ${ }, a $(( )), a $( ) (taken whole as its place in the text) or a
 * letter escaped; p itself when it is none of them
 */
static const char *
br_skip(const char *p)
{
	int depth;

	switch ((unsigned char)*p) {
	case (unsigned char)CTLESC:
		return (p[1] != '\0') ? p + 2 : p + 1;
	case (unsigned char)CTLVAR:
		depth = 0;
		for (; *p; p++) {
			if (*p == CTLESC && p[1] != '\0') {
				p++;
				continue;
			}
			if (*p == CTLVAR)
				depth++;
			else if (*p == CTLENDVAR && --depth == 0)
				return p + 1;
		}
		return p;
	case (unsigned char)CTLARI:
		depth = 0;
		for (; *p; p++) {
			if (*p == CTLESC && p[1] != '\0') {
				p++;
				continue;
			}
			if (*p == CTLARI)
				depth++;
			else if (*p == CTLENDARI && --depth == 0)
				return p + 1;
		}
		return p;
	default:
		return p;
	}
}

/* A whole number of a sequence, optionally signed; NULL when it is not one */
static const char *
br_num(const char *p, const char *end, long *v, int *width)
{
	const char *s = p;
	int neg = 0;
	long n = 0;

	if (p < end && (*p == '-' || *p == '+')) {
		neg = (*p == '-');
		p++;
	}
	if (p >= end || !isdigit((unsigned char)*p))
		return NULL;
	*width = (p[0] == '0' && p + 1 < end && isdigit((unsigned char)p[1]))
	    ? (int)(end - s) : 0;
	for (; p < end && isdigit((unsigned char)*p); p++)
		n = n * 10 + (*p - '0');
	*v = neg ? -n : n;
	return p;
}

static struct strlist **br_add(struct strlist **lp, const char *a, size_t na,
    const char *b, size_t nb, const char *c, int *count);

/*
 * {x..y} and {x..y..step}, of numbers or of single letters: the words,
 * pre + each + post, added at *lp. 0 when the text in the braces is
 * not a sequence.
 */
static int
br_seq(const char *pre, size_t npre, const char *in, const char *end,
    const char *post, struct strlist ***lpp, int *count)
{
	const char *d1, *d2;
	long a, b, step = 1, i;
	int wa = 0, wb = 0, ws, letters = 0;
	char buf[32];

	d1 = strstr(in, "..");
	if (d1 == NULL || d1 >= end)
		return 0;
	if (end - in == 4 && d1 == in + 1 && in[0] != CTLESC && in[3] != CTLESC &&
	    isalpha((unsigned char)in[0]) && isalpha((unsigned char)in[3])) {
		letters = 1;
		a = (unsigned char)in[0];
		b = (unsigned char)in[3];
	} else {
		if (br_num(in, d1, &a, &wa) != d1)
			return 0;
		d2 = strstr(d1 + 2, "..");
		if (d2 != NULL && d2 < end) {
			if (br_num(d1 + 2, d2, &b, &wb) != d2 ||
			    br_num(d2 + 2, end, &step, &ws) != end)
				return 0;
		} else if (br_num(d1 + 2, end, &b, &wb) != end)
			return 0;
		if (step < 0)
			step = -step;
		if (step == 0)
			step = 1;
	}
	if (wb > wa)
		wa = wb;
	for (i = a; (a <= b) ? i <= b : i >= b; i += (a <= b) ? step : -step) {
		if (letters) {
			buf[0] = (char)i;
			buf[1] = '\0';
		} else
			fmtstr(buf, sizeof(buf), "%0*ld", wa, i);
		*lpp = br_add(*lpp, pre, npre, buf, strlen(buf), post, count);
		if (*count >= BR_MAX)
			break;
	}
	return 1;
}

/* A word made of three parts, expanded again for any braces left in it */
static struct strlist **
br_add(struct strlist **lp, const char *a, size_t na, const char *b, size_t nb,
    const char *c, int *count)
{
	size_t nc = strlen(c);
	char *w = stalloc((int)(na + nb + nc + 1));

	memcpy(w, a, na);
	memcpy(w + na, b, nb);
	memcpy(w + na + nb, c, nc + 1);
	return brace_words(w, lp, count);
}

/*
 * The words a word becomes by its braces: the first group of an
 * unquoted { with a , or a .. at its level in it, each of its parts
 * with what goes before and after it, each of those expanded again.
 * A word without such braces is itself. The words are added at lp;
 * where the next goes is answered.
 */
struct strlist **
brace_words(char *text, struct strlist **lp, int *count)
{
	const char *p, *q, *open = NULL, *close = NULL, *s;
	int quoted = 0, depth, commas;
	struct strlist *sp;

	for (p = text; *p; ) {
		if (*p == CTLQUOTEMARK) {
			quoted = !quoted;
			p++;
			continue;
		}
		if ((s = br_skip(p)) != p) {
			p = s;
			continue;
		}
		if (*p == CTLBACKQ || *p == (CTLBACKQ | CTLQUOTE)) {
			p++;
			continue;
		}
		if (*p != '{' || quoted) {
			p++;
			continue;
		}
		/* its } at the same level, and whether it has a , or .. at that level */
		depth = 0;
		commas = 0;
		for (q = p + 1; *q; ) {
			if (*q == CTLQUOTEMARK) {
				quoted = !quoted;
				q++;
				continue;
			}
			if ((s = br_skip(q)) != q) {
				q = s;
				continue;
			}
			if (*q == CTLBACKQ || *q == (CTLBACKQ | CTLQUOTE)) {
				depth = -1000;		/* a $( ) inside: left as it is */
				q++;
				continue;
			}
			if (!quoted) {
				if (*q == '{')
					depth++;
				else if (*q == '}') {
					if (depth == 0)
						break;
					depth--;
				} else if (*q == ',' && depth == 0)
					commas++;
			}
			q++;
		}
		quoted = 0;
		if (*q != '}' || depth < 0) {
			p++;
			continue;
		}
		open = p;
		close = q;
		if (commas > 0) {
			const char *part = open + 1;

			depth = 0;
			for (q = open + 1; q <= close; ) {
				if (q < close && *q == CTLQUOTEMARK) {
					quoted = !quoted;
					q++;
					continue;
				}
				if (q < close && (s = br_skip(q)) != q) {
					q = s;
					continue;
				}
				if (q == close || (!quoted && depth == 0 && *q == ',')) {
					lp = br_add(lp, text, (size_t)(open - text), part,
					    (size_t)(q - part), close + 1, count);
					if (*count >= BR_MAX)
						return lp;
					part = q + 1;
				} else if (!quoted && *q == '{')
					depth++;
				else if (!quoted && *q == '}')
					depth--;
				q++;
			}
			return lp;
		}
		if (br_seq(text, (size_t)(open - text), open + 1, close, close + 1, &lp, count))
			return lp;
		p++;
	}
	sp = stalloc(sizeof(*sp));
	sp->text = text;
	sp->next = NULL;
	*lp = sp;
	(*count)++;
	return &sp->next;
}

/* Whether a word is worth looking into: an unquoted { and a } after it */
int
has_braces(const char *text)
{
	const char *p = strchr(text, '{');

	return tshext && p != NULL && strchr(p, '}') != NULL;
}

/* ---------------------------------------------------------------- PS1 */

static void
ps_put(char **o, char *end, const char *s, int escape)
{
	for (; *s && *o < end - 2; s++) {
		if (escape && (*s == '$' || *s == '`' || *s == '\\'))
			*(*o)++ = '\\';
		*(*o)++ = *s;
	}
}

/*
 * The backslash escapes of a prompt as bash has them, before the prompt
 * is expanded: \u the user, \h and \H the machine's name, \w the place
 * with ~ for the home, \W its last part, \$ # for the administrator and
 * $ for others, \d \t \T \@ \A the date and time, \n, \s, \v, \\, and
 * \[ \] (around what takes no room) taken away. What they put in is
 * protected from the expansion that follows.
 */
char *
ps1_escapes(const char *ps)
{
	static char buf[1024];
	char *o = buf, *end = buf + sizeof(buf), tmp[256];
	const char *home, *pwd, *b;
	struct passwd *pw;
	time_t now;
	struct tm *tm;

	if (!tshext || strchr(ps, '\\') == NULL)
		return __UNCONST(ps);
	for (; *ps && o < end - 2; ps++) {
		if (*ps != '\\' || ps[1] == '\0') {
			*o++ = *ps;
			continue;
		}
		switch (*++ps) {
		case 'u':
			pw = getpwuid(getuid());
			ps_put(&o, end, (pw != NULL) ? pw->pw_name : "?", 1);
			break;
		case 'h':
		case 'H':
			if (gethostname(tmp, sizeof(tmp)) != 0)
				strcpy(tmp, "tessronos");
			tmp[sizeof(tmp) - 1] = '\0';
			if (*ps == 'h' && (b = strchr(tmp, '.')) != NULL)
				tmp[b - tmp] = '\0';
			ps_put(&o, end, tmp, 1);
			break;
		case 'w':
		case 'W':
			pwd = lookupvar("PWD");
			if (pwd == NULL)
				pwd = "?";
			home = lookupvar("HOME");
			if (*ps == 'W') {
				if (home != NULL && strcmp(pwd, home) == 0)
					ps_put(&o, end, "~", 1);
				else if ((b = strrchr(pwd, '/')) != NULL && b[1] != '\0')
					ps_put(&o, end, b + 1, 1);
				else
					ps_put(&o, end, pwd, 1);
			} else if (home != NULL && *home != '\0' &&
			    strncmp(pwd, home, strlen(home)) == 0 &&
			    (pwd[strlen(home)] == '/' || pwd[strlen(home)] == '\0')) {
				ps_put(&o, end, "~", 1);
				ps_put(&o, end, pwd + strlen(home), 1);
			} else
				ps_put(&o, end, pwd, 1);
			break;
		case '$':
			*o++ = (geteuid() == 0) ? '#' : '$';
			break;
		case 'd':
		case 't':
		case 'T':
		case '@':
		case 'A':
			now = time(NULL);
			tm = localtime(&now);
			strftime(tmp, sizeof(tmp),
			    (*ps == 'd') ? "%a %b %e" : (*ps == 't') ? "%H:%M:%S" :
			    (*ps == 'T') ? "%I:%M:%S" : (*ps == '@') ? "%I:%M %p" : "%H:%M",
			    tm);
			ps_put(&o, end, tmp, 1);
			break;
		case 'n':
			*o++ = '\n';
			break;
		case 'r':
			*o++ = '\r';
			break;
		case 'a':
			*o++ = '\007';
			break;
		case 'e':
			*o++ = '\033';
			break;
		case 's':
			ps_put(&o, end, "tsh", 1);
			break;
		case 'v':
		case 'V':
			ps_put(&o, end, "1.0", 1);
			break;
		case '[':
		case ']':
			break;
		case '\\':
			*o++ = '\\';
			*o++ = '\\';
			break;
		default:
			*o++ = '\\';
			*o++ = *ps;
			break;
		}
	}
	*o = '\0';
	return buf;
}

/* ---------------------------------------------------------------- history expansion */

/* An event of the history: the latest whose line starts with s, or the nth, or the nth back */
static const char *
hist_event(const char *spec, size_t n, int contains)
{
	HistEvent he;
	char key[256];
	int num, retval;

	if (hist == NULL)
		return NULL;
	if (n >= sizeof(key))
		n = sizeof(key) - 1;
	memcpy(key, spec, n);
	key[n] = '\0';
	if (n > 0 && (isdigit((unsigned char)key[0]) || (key[0] == '-' && n > 1))) {
		num = atoi(key);
		if (num < 0) {
			retval = history(hist, &he, H_FIRST);	/* the latest */
			while (retval != -1 && ++num < 0)
				retval = history(hist, &he, H_NEXT);
		} else
			retval = history(hist, &he, H_NEXT_EVENT, num);
		return (retval != -1) ? he.str : NULL;
	}
	for (retval = history(hist, &he, H_FIRST); retval != -1;
	    retval = history(hist, &he, H_NEXT)) {
		if (contains ? strstr(he.str, key) != NULL : strncmp(he.str, key, n) == 0)
			return he.str;
	}
	return NULL;
}

/* The words of a line, for !$ !^ !* and :n */
static int
hist_words(const char *line, const char **w, size_t *len, int max)
{
	int n = 0;
	const char *p = line, *s;
	char q;

	while (*p && n < max) {
		while (*p == ' ' || *p == '\t' || *p == '\n')
			p++;
		if (*p == '\0')
			break;
		s = p;
		while (*p && *p != ' ' && *p != '\t' && *p != '\n') {
			if (*p == '\'' || *p == '"') {
				q = *p++;
				while (*p && *p != q)
					p++;
				if (*p)
					p++;
			} else
				p++;
		}
		w[n] = s;
		len[n] = (size_t)(p - s);
		n++;
	}
	return n;
}

static int
hist_put(char **o, size_t *cap, size_t *n, const char *s, size_t k)
{
	if (*n + k + 2 > *cap) {
		*cap = (*n + k + 2) * 2;
		*o = ckrealloc(*o, *cap);
	}
	memcpy(*o + *n, s, k);
	*n += k;
	return 0;
}

/*
 * A line typed with !! (the last line), !n, !-n, !word (the latest line
 * starting with it), !?word? (with it in it), !$ !^ !* (words of the
 * last line), the designators :n :$ :^ :*, and ^old^new at the start.
 * NULL when nothing was to expand; the new line otherwise (malloc'ed),
 * or an error said and an empty line.
 */
char *
hist_expand(const char *line)
{
	const char *p, *ev, *w[128], *s;
	size_t wl[128], cap = strlen(line) + 64, n = 0, k;
	char *o;
	int nw, changed = 0, sq = 0, i, a, b;

	if (!tshext || hist == NULL || strpbrk(line, "!^") == NULL)
		return NULL;
	o = ckmalloc(cap);
	p = line;
	if (*p == '^') {
		/* ^old^new^: the last line with the first old made new */
		const char *old = p + 1, *oe, *nw2, *ne;

		oe = strchr(old, '^');
		ev = hist_event("-1", 2, 0);
		if (oe == NULL || ev == NULL)
			goto bad;
		nw2 = oe + 1;
		ne = strpbrk(nw2, "^\n");
		if (ne == NULL)
			ne = nw2 + strlen(nw2);
		s = strstr(ev, old);
		if (s == NULL || oe == old) {
			ckfree(o);
			out2str("tsh: ^: no such text in the last line\n");
			return savestr("\n");
		}
		hist_put(&o, &cap, &n, ev, (size_t)(s - ev));
		hist_put(&o, &cap, &n, nw2, (size_t)(ne - nw2));
		s += oe - old;
		k = strlen(s);
		while (k > 0 && s[k - 1] == '\n')
			k--;
		hist_put(&o, &cap, &n, s, k);
		hist_put(&o, &cap, &n, "\n", 1);
		o[n] = '\0';
		return o;
	}
	for (; *p; ) {
		if (*p == '\'' )
			sq = !sq;
		if (*p == '\\' && p[1] == '!') {
			hist_put(&o, &cap, &n, "!", 1);
			p += 2;
			changed = 1;
			continue;
		}
		if (*p != '!' || sq || p[1] == '\0' || p[1] == ' ' || p[1] == '\t' ||
		    p[1] == '\n' || p[1] == '=' || p[1] == '(') {
			hist_put(&o, &cap, &n, p, 1);
			p++;
			continue;
		}
		/* which line */
		s = p + 1;
		if (*s == '!') {
			ev = hist_event("-1", 2, 0);
			s++;
		} else if (*s == '$' || *s == '^' || *s == '*' || *s == ':') {
			ev = hist_event("-1", 2, 0);
		} else if (*s == '?') {
			const char *e = strchr(s + 1, '?');
			size_t m = (e != NULL) ? (size_t)(e - s - 1) : strlen(s + 1);

			ev = hist_event(s + 1, m, 1);
			s = (e != NULL) ? e + 1 : s + 1 + m;
		} else {
			const char *e = s;

			if (*e == '-')
				e++;
			if (isdigit((unsigned char)*e)) {
				while (isdigit((unsigned char)*e))
					e++;
			} else {
				while (*e && strchr(" \t\n:;&|<>()\"'", *e) == NULL)
					e++;
			}
			ev = hist_event(s, (size_t)(e - s), 0);
			s = e;
		}
		if (ev == NULL)
			goto bad;
		/* which words of it */
		nw = hist_words(ev, w, wl, 128);
		a = 0;
		b = nw - 1;
		if (*s == ':')
			s++;
		if (*s == '$') {
			a = b = nw - 1;
			s++;
		} else if (*s == '^') {
			a = b = 1;
			s++;
		} else if (*s == '*') {
			a = 1;
			s++;
		} else if (isdigit((unsigned char)*s) && s[-1] == ':') {
			a = b = atoi(s);
			while (isdigit((unsigned char)*s))
				s++;
		}
		if (a < 0 || a > b || b >= nw) {
			if (!(a == 1 && b == 0)) {	/* !* of a line of one word: nothing */
				if (b >= nw)
					goto bad;
			}
		}
		for (i = a; i <= b && i < nw; i++) {
			if (i > a)
				hist_put(&o, &cap, &n, " ", 1);
			hist_put(&o, &cap, &n, w[i], wl[i]);
		}
		changed = 1;
		p = s;
	}
	o[n] = '\0';
	if (!changed) {
		ckfree(o);
		return NULL;
	}
	return o;
 bad:
	ckfree(o);
	out2str("tsh: !: no such event\n");
	flushout(out2);
	return savestr("\n");
}

/* ---------------------------------------------------------------- shopt */

static const struct {
	const char *name;
	int *val;
} shopts[] = {
	{ "checkwinsize",	&sh_checkwinsize },
	{ "dotglob",		&sh_dotglob },
	{ "expand_aliases",	&sh_expand_aliases },
	{ "extglob",		&sh_extglob },
	{ "globstar",		&sh_globstar },
	{ "histappend",		&sh_histappend },
	{ "nocaseglob",		&sh_nocaseglob },
	{ "nocasematch",	&sh_nocasematch },
	{ "nullglob",		&sh_nullglob },
};

/*
 * shopt [-psuq] [name ...]: the options of bash this shell has (the
 * others it does not know); with -o, those of set -o
 */
int
shoptcmd(int argc, char **argv)
{
	int set = 0, unset = 0, print = 0, quiet = 0, oflag = 0, c, i, j, ret = 0;

	while ((c = nextopt("psuqo")) != '\0') {
		switch (c) {
		case 's': set = 1; break;
		case 'u': unset = 1; break;
		case 'p': print = 1; break;
		case 'q': quiet = 1; break;
		case 'o': oflag = 1; break;
		}
	}
	(void)argc;
	if (oflag) {
		/* the options of set -o by the way shopt shows them */
		for (i = 0; optlist[i].name != NULL; i++) {
			if (*argptr != NULL) {
				for (j = 0; argptr[j] != NULL; j++)
					if (strcmp(argptr[j], optlist[i].name) == 0)
						break;
				if (argptr[j] == NULL)
					continue;
			}
			if (set || unset) {
				optlist[i].val = set ? 1 : 0;
				continue;
			}
			if (!quiet)
				out1fmt("%-16s%s\n", optlist[i].name, optlist[i].val ? "on" : "off");
			if (!optlist[i].val)
				ret = 1;
		}
		if (set || unset)
			optschanged();
		return ret;
	}
	if (*argptr == NULL) {
		for (i = 0; i < (int)(sizeof(shopts) / sizeof(shopts[0])); i++) {
			if ((set && !*shopts[i].val) || (unset && *shopts[i].val))
				continue;
			if (print)
				out1fmt("shopt %s %s\n", *shopts[i].val ? "-s" : "-u", shopts[i].name);
			else
				out1fmt("%-16s%s\n", shopts[i].name, *shopts[i].val ? "on" : "off");
		}
		return 0;
	}
	for (j = 0; argptr[j] != NULL; j++) {
		for (i = 0; i < (int)(sizeof(shopts) / sizeof(shopts[0])); i++)
			if (strcmp(argptr[j], shopts[i].name) == 0)
				break;
		if (i == (int)(sizeof(shopts) / sizeof(shopts[0]))) {
			outfmt(out2, "shopt: %s: invalid shell option name\n", argptr[j]);
			ret = 1;
			continue;
		}
		if (set)
			*shopts[i].val = 1;
		else if (unset)
			*shopts[i].val = 0;
		else {
			if (!quiet) {
				if (print)
					out1fmt("shopt %s %s\n", *shopts[i].val ? "-s" : "-u",
					    shopts[i].name);
				else
					out1fmt("%-16s%s\n", shopts[i].name,
					    *shopts[i].val ? "on" : "off");
			}
			if (!*shopts[i].val)
				ret = 1;
		}
	}
	return ret;
}

/* ---------------------------------------------------------------- history */

/*
 * history [n], history -c: the lines typed, as fc -l lists them, or the
 * last n of them; -c forgets them
 */
int
historycmd(int argc, char **argv)
{
	char *av[5], num[16];
	int n = 0;

	(void)argc;
	if (argv[1] != NULL && strcmp(argv[1], "-c") == 0) {
		HistEvent he;

		if (hist != NULL)
			history(hist, &he, H_CLEAR);
		return 0;
	}
	av[n++] = __UNCONST("fc");
	av[n++] = __UNCONST("-l");
	if (argv[1] != NULL && isdigit((unsigned char)argv[1][0])) {
		fmtstr(num, sizeof(num), "-%d", atoi(argv[1]));
		av[n++] = num;
	} else {
		av[n++] = __UNCONST("1");
	}
	av[n] = NULL;
	return histcmd(n, av);
}
