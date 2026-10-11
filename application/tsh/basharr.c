/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	basharr.c
 *	The arrays and [[ ]] of bash in tsh (design 5.8.8)
 *
 *	An array is kept here by its name: an indexed one by its numbers,
 *	an associative one (declare -A) by its keys, each element a string.
 *	The variable of the same name holds its element 0 (or "0"), which
 *	is what $name gives, as in bash. What is written:
 *
 *	name=(a b c)  name+=(d)  name=([3]=x [5]=y)  name[i]=v  name[i]+=v
 *	${name[i]}  ${name[@]}  ${name[*]}  ${#name[@]}  ${#name[i]}
 *	${!name[@]}  unset name[i]  unset name  declare -a/-A/-p/-r/-x
 *
 *	The subscript of an indexed array is an arithmetic expression; of
 *	an associative one, a string. Arrays are not made local by local
 *	and are not exported.
 *
 *	[[ expression ]]: the words are not split and not globbed; the
 *	right of == and != is a pattern (quoted parts of it taken as they
 *	are), of =~ an extended regular expression whose matches go into
 *	BASH_REMATCH; -eq and the others compare numbers, < and > strings;
 *	the tests of test(1) for the rest; && || ! ( ) join them.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>
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
#include "arithmetic.h"
#include "syntax.h"
#include "bashext.h"
#include "tspawn.h"

struct tsharr {
	struct tsharr *next;
	char	*name;
	int	assoc;
	int	n, cap;
	long	*idx;			/* indexed: the numbers, ascending */
	char	**key;			/* associative: the keys, as they came */
	char	**val;
};

static struct tsharr *arrays;
static int in_sync;		/* the variable of element 0 being set: not an element again */

/* ---------------------------------------------------------------- the table */

static struct tsharr *
arr_find(const char *name, size_t len)
{
	struct tsharr *a;

	for (a = arrays; a != NULL; a = a->next)
		if (strlen(a->name) == len && strncmp(a->name, name, len) == 0)
			return a;
	return NULL;
}

static struct tsharr *
arr_make(const char *name, size_t len, int assoc)
{
	struct tsharr *a = arr_find(name, len);

	if (a != NULL)
		return a;
	a = ckmalloc(sizeof(*a));
	memset(a, 0, sizeof(*a));
	a->name = ckmalloc(len + 1);
	memcpy(a->name, name, len);
	a->name[len] = '\0';
	a->assoc = assoc;
	a->next = arrays;
	arrays = a;
	return a;
}

static void
arr_clear(struct tsharr *a)
{
	int i;

	for (i = 0; i < a->n; i++) {
		ckfree(a->val[i]);
		if (a->key != NULL)
			ckfree(a->key[i]);
	}
	a->n = 0;
}

static void
arr_free(struct tsharr *a)
{
	struct tsharr **pp;

	for (pp = &arrays; *pp != NULL; pp = &(*pp)->next) {
		if (*pp == a) {
			*pp = a->next;
			break;
		}
	}
	arr_clear(a);
	if (a->idx)
		ckfree(a->idx);
	if (a->key)
		ckfree(a->key);
	if (a->val)
		ckfree(a->val);
	ckfree(a->name);
	ckfree(a);
}

static void
arr_room(struct tsharr *a)
{
	if (a->n < a->cap)
		return;
	a->cap = (a->cap == 0) ? 8 : a->cap * 2;
	a->val = ckrealloc(a->val, sizeof(char *) * (size_t)a->cap);
	if (a->assoc)
		a->key = ckrealloc(a->key, sizeof(char *) * (size_t)a->cap);
	else
		a->idx = ckrealloc(a->idx, sizeof(long) * (size_t)a->cap);
}

/* Where an element is, or would go: its place, and whether it is there */
static int
arr_at(struct tsharr *a, long i, const char *k, int *found)
{
	int lo = 0, hi = a->n, m;

	*found = 0;
	if (a->assoc) {
		for (m = 0; m < a->n; m++)
			if (strcmp(a->key[m], k) == 0) {
				*found = 1;
				return m;
			}
		return a->n;
	}
	while (lo < hi) {
		m = (lo + hi) / 2;
		if (a->idx[m] == i) {
			*found = 1;
			return m;
		}
		if (a->idx[m] < i)
			lo = m + 1;
		else
			hi = m;
	}
	return lo;
}

/* The variable of the name holds element 0 */
static void
arr_sync(struct tsharr *a)
{
	int found, at = arr_at(a, 0, "0", &found);
	char *t;

	if (found) {
		t = ckmalloc(strlen(a->name) + strlen(a->val[at]) + 2);
		strcpy(t, a->name);
		strcat(t, "=");
		strcat(t, a->val[at]);
		in_sync = 1;
		setvareq(t, 0);
		in_sync = 0;
	}
}

static void
arr_set(struct tsharr *a, long i, const char *k, const char *v, int append)
{
	int found, at = arr_at(a, i, k, &found), j;
	char *nv;

	if (found) {
		if (append) {
			nv = ckmalloc(strlen(a->val[at]) + strlen(v) + 1);
			strcpy(nv, a->val[at]);
			strcat(nv, v);
			ckfree(a->val[at]);
			a->val[at] = nv;
		} else {
			ckfree(a->val[at]);
			a->val[at] = savestr(v);
		}
	} else {
		arr_room(a);
		for (j = a->n; j > at; j--) {
			a->val[j] = a->val[j - 1];
			if (a->assoc)
				a->key[j] = a->key[j - 1];
			else
				a->idx[j] = a->idx[j - 1];
		}
		a->val[at] = savestr(v);
		if (a->assoc)
			a->key[at] = savestr(k);
		else
			a->idx[at] = i;
		a->n++;
	}
	if ((!a->assoc && i == 0) || (a->assoc && strcmp(k, "0") == 0))
		arr_sync(a);
}

static void
arr_del(struct tsharr *a, long i, const char *k)
{
	int found, at = arr_at(a, i, k, &found), j;

	if (!found)
		return;
	ckfree(a->val[at]);
	if (a->assoc)
		ckfree(a->key[at]);
	for (j = at; j < a->n - 1; j++) {
		a->val[j] = a->val[j + 1];
		if (a->assoc)
			a->key[j] = a->key[j + 1];
		else
			a->idx[j] = a->idx[j + 1];
	}
	a->n--;
}

/* ---------------------------------------------------------------- subscripts */

/*
 * A subscript as it was written in ${name[...]}: $name, ${name} and
 * $(( )) in it put in, quotes taken off. The words of an assignment
 * come here expanded already, and pass through unchanged.
 */
static char *
sub_text(const char *s, size_t n)
{
	char *o = ckmalloc(n * 4 + 64), *q = o, name[256];
	const char *e = s + n, *v;
	size_t k;

	while (s < e) {
		if (*s == '\'' || *s == '"') {
			s++;
			continue;
		}
		if (*s == '$' && s + 1 < e && (s[1] == '{' || is_name(s[1]))) {
			const char *b = s + 1;

			if (*b == '{')
				b++;
			for (k = 0; b + k < e && is_in_name(b[k]) && k < sizeof(name) - 1; k++)
				name[k] = b[k];
			name[k] = '\0';
			s = b + k;
			if (s < e && *s == '}')
				s++;
			v = lookupvar(name);
			if (v != NULL) {
				size_t vl = strlen(v);

				o = ckrealloc(o, (size_t)(q - o) + vl + (size_t)(e - s) + 64);
				q = o + strlen(o);	/* o was NUL-ended as it grew */
				memcpy(q, v, vl);
				q += vl;
				*q = '\0';
			}
			continue;
		}
		if (*s == '$' && s + 2 < e && s[1] == '(' && s[2] == '(') {
			s += 3;			/* $(( x )): the x, evaluated below as arithmetic */
			continue;
		}
		*q++ = *s++;
		*q = '\0';
	}
	*q = '\0';
	/* the )) of a $(( )) */
	k = strlen(o);
	while (k >= 2 && o[k - 1] == ')' && o[k - 2] == ')') {
		o[k - 2] = '\0';
		k -= 2;
	}
	return o;
}

static long
sub_index(struct tsharr *a, const char *s, size_t n)
{
	char *t = sub_text(s, n);
	long i;

	i = (long)arith(t, 0);
	ckfree(t);
	if (i < 0 && a != NULL && a->n > 0)
		i += a->idx[a->n - 1] + 1;	/* from the end, as bash counts */
	return i;
}

/* The parts of name[sub]: the name's length; the subscript and its length */
static int
split_sub(const char *p, size_t *nlen, const char **sub, size_t *slen)
{
	const char *b = strchr(p, '['), *e;
	int depth = 0;

	if (b == NULL)
		return 0;
	for (e = b; *e; e++) {
		if (*e == '[')
			depth++;
		else if (*e == ']' && --depth == 0)
			break;
	}
	if (*e != ']')
		return 0;
	*nlen = (size_t)(b - p);
	*sub = b + 1;
	*slen = (size_t)(e - b - 1);
	return 1;
}

/* ---------------------------------------------------------------- assignments */

/* Whether the name of "name...=" has a subscript */
int
array_subscripted(const char *text)
{
	const char *eq = strchr(text, '='), *b = strchr(text, '[');

	return tshext && b != NULL && (eq == NULL || b < eq);
}

/*
 * The hook of setvareq: name[sub]=value and name[sub]+=value set an
 * element; name+=value appends to a variable; name=value of an array
 * sets its element 0 as well. TRUE when the setting was done here.
 */
int
array_setelem(const char *text, int flags)
{
	const char *eq = strchr(text, '='), *sub;
	size_t nlen, slen;
	int append;
	struct tsharr *a;
	long i = 0;
	char *k = NULL;

	if (!tshext || eq == NULL || in_sync)
		return 0;
	append = (eq > text && eq[-1] == '+');
	if (array_subscripted(text)) {
		if (!split_sub(text, &nlen, &sub, &slen))
			return 0;
		a = arr_find(text, nlen);
		if (a == NULL)
			a = arr_make(text, nlen, 0);
		if (a->assoc)
			k = sub_text(sub, slen);
		else
			i = sub_index(a, sub, slen);
		arr_set(a, i, (k != NULL) ? k : "0", eq + 1, append);
		if (k != NULL)
			ckfree(k);
		return 1;
	}
	nlen = (size_t)(eq - text) - (append ? 1 : 0);
	a = arr_find(text, nlen);
	if (a != NULL) {
		/* a scalar setting of an array: its element 0 */
		arr_set(a, 0, "0", eq + 1, append);
		return 1;
	}
	if (append) {
		/* name+=value: the value after what there is */
		char *name = ckmalloc(nlen + 1), *old, *t;

		memcpy(name, text, nlen);
		name[nlen] = '\0';
		old = lookupvar(name);
		t = ckmalloc(nlen + (old ? strlen(old) : 0) + strlen(eq + 1) + 2);
		strcpy(t, name);
		strcat(t, "=");
		if (old)
			strcat(t, old);
		strcat(t, eq + 1);
		ckfree(name);
		setvareq(t, flags & ~(VSTACK | VTEXTFIXED));
		return 1;
	}
	return 0;
}

/*
 * name=(words) and name+=(words): the words expanded as the arguments
 * of a command are, [k]=v among them setting element k
 */
int
array_assign(const char *nameeq, union node *elems, int assoc)
{
	struct arglist list;
	struct strlist *sp;
	union node *ap;
	struct tsharr *a;
	size_t nlen = strcspn(nameeq, "+=");
	int append = (nameeq[nlen] == '+');
	long next = 0;
	const char *eq, *sub;
	size_t slen, dummy;

	list.lastp = &list.list;
	for (ap = elems; ap != NULL; ap = ap->narg.next)
		expandarg(ap, &list, EXP_FULL | EXP_TILDE);
	*list.lastp = NULL;

	a = arr_find(nameeq, nlen);
	if (a == NULL) {
		a = arr_make(nameeq, nlen, assoc);
		/* a scalar that was there becomes element 0 */
		if (!assoc) {
			char *name = ckmalloc(nlen + 1), *old;

			memcpy(name, nameeq, nlen);
			name[nlen] = '\0';
			old = lookupvar(name);
			if (old != NULL && append)
				arr_set(a, 0, "0", old, 0);
			ckfree(name);
		}
	}
	if (!append)
		arr_clear(a);
	else if (!a->assoc && a->n > 0)
		next = a->idx[a->n - 1] + 1;
	for (sp = list.list; sp != NULL; sp = sp->next) {
		if (sp->text[0] == '[' && (eq = strstr(sp->text, "]=")) != NULL &&
		    split_sub(sp->text, &dummy, &sub, &slen)) {
			if (a->assoc) {
				char *k = sub_text(sub, slen);

				arr_set(a, 0, k, eq + 2, 0);
				ckfree(k);
			} else {
				next = sub_index(a, sub, slen);
				arr_set(a, next++, NULL, eq + 2, 0);
			}
		} else if (a->assoc) {
			outfmt(out2, "%s: %s: an associative array needs [key]=value\n",
			    a->name, sp->text);
		} else
			arr_set(a, next++, NULL, sp->text, 0);
	}
	if (!a->assoc && a->n == 0) {
		/* name=(): the variable empty too */
		char *t = ckmalloc(nlen + 2);

		memcpy(t, nameeq, nlen);
		t[nlen] = '=';
		t[nlen + 1] = '\0';
		in_sync = 1;
		setvareq(t, 0);
		in_sync = 0;
	}
	return 0;
}

/* unset name[sub], unset name: TRUE when it was one of these */
int
array_unset(const char *s)
{
	size_t nlen, slen;
	const char *sub;
	struct tsharr *a;

	if (!tshext)
		return 0;
	if (split_sub(s, &nlen, &sub, &slen)) {
		a = arr_find(s, nlen);
		if (a != NULL) {
			if (slen == 1 && (*sub == '@' || *sub == '*'))
				arr_free(a);
			else if (a->assoc) {
				char *k = sub_text(sub, slen);

				arr_del(a, 0, k);
				ckfree(k);
			} else
				arr_del(a, sub_index(a, sub, slen), NULL);
		}
		return 1;
	}
	a = arr_find(s, strlen(s));
	if (a != NULL)
		arr_free(a);
	return 0;			/* the variable goes too */
}

/* ---------------------------------------------------------------- values */

/*
 * The value of name[sub] (the text of ${...} up to its =), or of name
 * when it is an array; *isarr says whether it was an array's
 */
const char *
array_value(const char *var, int *isarr)
{
	size_t nlen, slen;
	const char *sub;
	struct tsharr *a;
	int found, at;
	char *k;

	*isarr = 0;
	if (!tshext || !split_sub(var, &nlen, &sub, &slen) ||
	    (strchr(var, '=') != NULL && strchr(var, '=') < sub))
		return NULL;
	*isarr = 1;
	a = arr_find(var, nlen);
	if (a == NULL) {
		/* name[0] of a scalar is the scalar */
		char *name = ckmalloc(nlen + 1);
		const char *v;

		memcpy(name, var, nlen);
		name[nlen] = '\0';
		v = lookupvar(name);
		ckfree(name);
		return (v != NULL && sub_index(NULL, sub, slen) == 0) ? v : NULL;
	}
	if (a->assoc) {
		k = sub_text(sub, slen);
		at = arr_at(a, 0, k, &found);
		ckfree(k);
	} else
		at = arr_at(a, sub_index(a, sub, slen), NULL, &found);
	return found ? a->val[at] : NULL;
}

/*
 * The words of name[@] or name[*] (keys: of !name[@]): their number;
 * *words made for the caller to free; -1 when it is not such a form
 */
int
array_words(const char *var, int keys, char ***words)
{
	size_t nlen, slen;
	const char *sub;
	struct tsharr *a;
	char **w, num[32];
	int i;

	if (!tshext || !split_sub(var, &nlen, &sub, &slen) || slen != 1 ||
	    (*sub != '@' && *sub != '*'))
		return -1;
	a = arr_find(var, nlen);
	w = ckmalloc(sizeof(char *) * (size_t)((a != NULL ? a->n : 1) + 1));
	if (a == NULL) {
		/* a scalar: itself, one element */
		char *name = ckmalloc(nlen + 1);
		const char *v;
		int n = 0;

		memcpy(name, var, nlen);
		name[nlen] = '\0';
		v = lookupvar(name);
		ckfree(name);
		if (v != NULL)
			w[n++] = savestr(keys ? "0" : v);
		w[n] = NULL;
		*words = w;
		return n;
	}
	for (i = 0; i < a->n; i++) {
		if (!keys)
			w[i] = savestr(a->val[i]);
		else if (a->assoc)
			w[i] = savestr(a->key[i]);
		else {
			fmtstr(num, sizeof(num), "%ld", a->idx[i]);
			w[i] = savestr(num);
		}
	}
	w[a->n] = NULL;
	*words = w;
	return a->n;
}

/* ---------------------------------------------------------------- a subshell's arrays */

void
serarrays(struct shbuf *b)
{
	struct tsharr *a;
	char num[32];
	int i;

	for (a = arrays; a != NULL; a = a->next) {
		sb_int(b, 'R');
		sb_str(b, a->name);
		sb_int(b, a->assoc);
		sb_int(b, a->n);
		for (i = 0; i < a->n; i++) {
			if (a->assoc)
				sb_str(b, a->key[i]);
			else {
				fmtstr(num, sizeof(num), "%ld", a->idx[i]);
				sb_str(b, num);
			}
			sb_str(b, a->val[i]);
		}
	}
}

void
restorearray(struct shrd *r)
{
	char *name = sr_str(r), *k, *v;
	int assoc = sr_int(r), n = sr_int(r), i;
	struct tsharr *a = arr_make(name, strlen(name), assoc);

	for (i = 0; i < n; i++) {
		k = sr_str(r);
		v = sr_str(r);
		arr_set(a, assoc ? 0 : strtol(k, NULL, 10), k, v, 0);
		ckfree(k);
		ckfree(v);
	}
	ckfree(name);
}

/* ---------------------------------------------------------------- declare */

static void
print_array(struct tsharr *a)
{
	int i;

	out1fmt("declare -%c %s=(", a->assoc ? 'A' : 'a', a->name);
	for (i = 0; i < a->n; i++) {
		if (i > 0)
			out1c(' ');
		if (a->assoc)
			out1fmt("[%s]=", a->key[i]);
		else
			out1fmt("[%ld]=", a->idx[i]);
		outshstr(a->val[i], out1);
	}
	out1str(")\n");
}

/*
 * declare/typeset [-aAprxg] [name[=value] ...]: arrays made (-a
 * indexed, -A associative), variables read only (-r) or exported
 * (-x), what is there shown (-p)
 */
int
declarecmd(int argc, char **argv)
{
	int c, aflg = 0, Aflg = 0, pflg = 0, rflg = 0, xflg = 0;
	char *name, *eq;
	struct tsharr *a;

	(void)argc;
	while ((c = nextopt("aAprxgfiF")) != '\0') {
		switch (c) {
		case 'a': aflg = 1; break;
		case 'A': Aflg = 1; break;
		case 'p': pflg = 1; break;
		case 'r': rflg = 1; break;
		case 'x': xflg = 1; break;
		default: break;		/* -g -i -f -F: taken, nothing more done */
		}
	}
	(void)argv;
	if (*argptr == NULL || pflg) {
		if (*argptr == NULL) {
			for (a = arrays; a != NULL; a = a->next)
				print_array(a);
			if (!aflg && !Aflg)
				showvars("declare --", 0, 0, NULL);
			return 0;
		}
		for (; (name = *argptr) != NULL; argptr++) {
			a = arr_find(name, strlen(name));
			if (a != NULL)
				print_array(a);
			else if (lookupvar(name) != NULL) {
				out1fmt("declare -- %s=", name);
				outshstr(lookupvar(name), out1);
				out1c('\n');
			}
		}
		return 0;
	}
	for (; (name = *argptr) != NULL; argptr++) {
		eq = strchr(name, '=');
		if (aflg || Aflg) {
			size_t n = (eq != NULL) ? (size_t)(eq - name) : strlen(name);

			(void)arr_make(name, n, Aflg);
		}
		if (eq != NULL)
			setvareq(savestr(name), (rflg ? VREADONLY : 0) | (xflg ? VEXPORT : 0));
		else if (rflg || xflg)
			setvar(name, NULL, (rflg ? VREADONLY : 0) | (xflg ? VEXPORT : 0) | VNOSET);
	}
	return 0;
}

/* A declaration's array: the assignments of name=(...) come here before the words */
int
array_declared_assoc(const char *nameeq)
{
	struct tsharr *a = arr_find(nameeq, strcspn(nameeq, "+="));

	return a != NULL && a->assoc;
}

/* ---------------------------------------------------------------- [[ ]] */

#define DB_OP	'\001'		/* the parser's mark of && || ! ( ) < > inside [[ ]] */

struct db {
	union node **w;		/* the words, raw */
	char	**v;		/* expanded: NULL for an operator */
	int	n, at;
};

static int db_or(struct db *);

static int
db_isop(struct db *d, const char *op)
{
	return d->at < d->n && d->v[d->at] == NULL &&
	    strcmp(d->w[d->at]->narg.text + 1, op) == 0;
}

static const char *
db_word(struct db *d)
{
	if (d->at >= d->n)
		error("[[: an operand is missing");
	if (d->v[d->at] == NULL) {
		const char *t = d->w[d->at]->narg.text + 1;

		if (strcmp(t, "<") == 0 || strcmp(t, ">") == 0)
			return t;		/* the comparisons: words where an operand stands */
		error("[[: %s where an operand should be", t);
	}
	return d->v[d->at];
}

static int
db_unary(const char *op, const char *a)
{
	struct stat st;

	if (op[0] != '-' || op[1] == '\0' || op[2] != '\0')
		return -1;
	switch (op[1]) {
	case 'n': return *a != '\0';
	case 'z': return *a == '\0';
	case 'e':
	case 'a': return stat(a, &st) == 0;
	case 'f': return stat(a, &st) == 0 && S_ISREG(st.st_mode);
	case 'd': return stat(a, &st) == 0 && S_ISDIR(st.st_mode);
	case 'c': return stat(a, &st) == 0 && S_ISCHR(st.st_mode);
	case 'p': return stat(a, &st) == 0 && S_ISFIFO(st.st_mode);
	case 's': return stat(a, &st) == 0 && st.st_size > 0;
	case 'r': return access(a, R_OK) == 0;
	case 'w': return access(a, W_OK) == 0;
	case 'x': return access(a, X_OK) == 0;
	case 'h':
	case 'L': return 0;			/* the objects have no symbolic links */
	case 't': return isatty(atoi(a));
	case 'v': return lookupvar(a) != NULL;
	case 'o': {
		int i;

		for (i = 0; optlist[i].name != NULL; i++)
			if (strcmp(optlist[i].name, a) == 0)
				return optlist[i].val != 0;
		return 0;
	}
	default:
		return -1;
	}
}

static int
db_regex(const char *s, const char *re)
{
	regex_t r;
	regmatch_t m[10];
	int e, i, ok;
	char name[32], *v;

	if ((e = regcomp(&r, re, REG_EXTENDED)) != 0)
		return 2;
	ok = regexec(&r, s, 10, m, 0) == 0;
	(void)unsetvar("BASH_REMATCH", 0);
	if (ok) {
		for (i = 0; i < 10 && m[i].rm_so >= 0; i++) {
			size_t n = (size_t)(m[i].rm_eo - m[i].rm_so);

			v = ckmalloc(n + 32);
			fmtstr(name, sizeof(name), "BASH_REMATCH[%d]=", i);
			strcpy(v, name);
			memcpy(v + strlen(name), s + m[i].rm_so, n);
			v[strlen(name) + n] = '\0';
			(void)array_setelem(v, 0);
			ckfree(v);
		}
	}
	regfree(&r);
	return ok;
}

static int
db_primary(struct db *d)
{
	const char *a, *op, *b;
	int r;

	if (db_isop(d, "!")) {
		d->at++;
		return !db_primary(d);
	}
	if (db_isop(d, "(")) {
		d->at++;
		r = db_or(d);
		if (!db_isop(d, ")"))
			error("[[: ) is missing");
		d->at++;
		return r;
	}
	/* a unary test, unless a binary operator after it makes it a left operand */
	if (d->at + 1 < d->n && d->v[d->at] != NULL && d->v[d->at][0] == '-') {
		int binary = 0;

		if (d->at + 2 < d->n) {
			const char *nx = (d->v[d->at + 1] != NULL) ? d->v[d->at + 1] :
			    d->w[d->at + 1]->narg.text + 1;

			binary = (strcmp(nx, "==") == 0 || strcmp(nx, "=") == 0 ||
			    strcmp(nx, "!=") == 0 || strcmp(nx, "=~") == 0 ||
			    strcmp(nx, "<") == 0 || strcmp(nx, ">") == 0 ||
			    (nx[0] == '-' && strlen(nx) == 3));
		}
		if (!binary) {
			r = db_unary(d->v[d->at], d->v[d->at + 1] != NULL ? d->v[d->at + 1] : "");
			if (r >= 0) {
				d->at += 2;
				return r;
			}
		}
	}
	a = db_word(d);
	d->at++;
	if (d->at >= d->n || db_isop(d, "&&") || db_isop(d, "||") || db_isop(d, ")"))
		return *a != '\0';
	op = db_word(d);
	d->at++;
	if (strcmp(op, "==") == 0 || strcmp(op, "=") == 0 || strcmp(op, "!=") == 0) {
		/* the right a pattern: matched raw, its quoted parts as they are */
		if (d->at >= d->n || d->v[d->at] == NULL)
			error("[[: an operand is missing after %s", op);
		if (sh_nocasematch) {
			char *x = savestr(a), *y = savestr(d->v[d->at]);
			size_t i;

			for (i = 0; x[i]; i++)
				x[i] = (char)tolower((unsigned char)x[i]);
			for (i = 0; y[i]; i++)
				y[i] = (char)tolower((unsigned char)y[i]);
			r = (strcmp(x, y) == 0) || casematch(d->w[d->at], x);
			ckfree(x);
			ckfree(y);
		} else
			r = casematch(d->w[d->at], __UNCONST(a));
		d->at++;
		return (op[0] == '!') ? !r : r;
	}
	if (strcmp(op, "=~") == 0) {
		b = db_word(d);
		d->at++;
		r = db_regex(a, b);
		if (r == 2)
			error("[[: %s: not a regular expression", b);
		return r;
	}
	b = db_word(d);
	d->at++;
	if (strcmp(op, "<") == 0)
		return strcmp(a, b) < 0;
	if (strcmp(op, ">") == 0)
		return strcmp(a, b) > 0;
	if (op[0] == '-') {
		intmax_t x = arith(a, 0), y = arith(b, 0);

		if (strcmp(op, "-eq") == 0) return x == y;
		if (strcmp(op, "-ne") == 0) return x != y;
		if (strcmp(op, "-lt") == 0) return x < y;
		if (strcmp(op, "-le") == 0) return x <= y;
		if (strcmp(op, "-gt") == 0) return x > y;
		if (strcmp(op, "-ge") == 0) return x >= y;
		if (strcmp(op, "-nt") == 0 || strcmp(op, "-ot") == 0 || strcmp(op, "-ef") == 0) {
			struct stat s1, s2;

			if (stat(a, &s1) != 0 || stat(b, &s2) != 0)
				return 0;
			if (op[1] == 'n')
				return s1.st_mtime > s2.st_mtime;
			if (op[1] == 'o')
				return s1.st_mtime < s2.st_mtime;
			return s1.st_dev == s2.st_dev && s1.st_ino == s2.st_ino;
		}
	}
	error("[[: %s: an unknown operator", op);
	/* NOTREACHED */
	return 0;
}

static int
db_and(struct db *d)
{
	int r = db_primary(d);

	while (db_isop(d, "&&")) {
		d->at++;
		r = db_primary(d) && r;
	}
	return r;
}

static int
db_or(struct db *d)
{
	int r = db_and(d);

	while (db_isop(d, "||")) {
		d->at++;
		r = db_and(d) || r;
	}
	return r;
}

/* Whether a simple command is a [[ ]] the parser made (bashext's parsedbracket) */
int
is_dbracket(union node *cmd)
{
	return tshext && cmd->ncmd.args != NULL &&
	    strcmp(cmd->ncmd.args->narg.text, "[[") == 0;
}

/* [[ ]] evaluated: 0 true, 1 false, 2 an error */
int
eval_dbracket(union node *cmd)
{
	struct db d;
	union node *ap;
	struct arglist list;
	int n = 0, r;

	for (ap = cmd->ncmd.args->narg.next; ap != NULL; ap = ap->narg.next)
		n++;
	d.w = ckmalloc(sizeof(union node *) * (size_t)(n + 1));
	d.v = ckmalloc(sizeof(char *) * (size_t)(n + 1));
	d.n = 0;
	d.at = 0;
	for (ap = cmd->ncmd.args->narg.next; ap != NULL; ap = ap->narg.next) {
		if (ap->narg.text[0] == DB_OP) {
			d.w[d.n] = ap;
			d.v[d.n++] = NULL;
			continue;
		}
		if (strcmp(ap->narg.text, "]]") == 0 && ap->narg.next == NULL)
			break;
		list.lastp = &list.list;
		expandarg(ap, &list, EXP_TILDE);
		*list.lastp = NULL;
		d.w[d.n] = ap;
		d.v[d.n++] = (list.list != NULL) ? list.list->text : savestr("");
	}
	if (d.n == 0) {
		r = 2;
	} else {
		r = db_or(&d) ? 0 : 1;
		if (d.at < d.n)
			error("[[: something is left after the expression");
	}
	ckfree(d.w);
	ckfree(d.v);
	return r;
}
