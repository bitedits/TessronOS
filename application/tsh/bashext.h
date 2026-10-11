/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	bashext.h
 *	The ways of bash tsh takes (bashext.c, basharr.c; design 5.8.8)
 */

#ifndef TSH_BASHEXT_H
#define TSH_BASHEXT_H

struct strlist;
union node;
struct arglist;

/* Taken, unless the shell was started as sh or with POSIXLY_CORRECT */
extern int tshext;

/* shopt */
extern int sh_nullglob, sh_dotglob, sh_nocaseglob, sh_nocasematch, sh_extglob;
extern int sh_histappend, sh_checkwinsize, sh_expand_aliases, sh_globstar;

/* {a,b} and {1..9}: the words a word becomes, added at lp */
int	has_braces(const char *);
struct strlist **brace_words(char *, struct strlist **, int *);

/* PS1's backslash escapes, before the prompt is expanded */
char	*ps1_escapes(const char *);

/* A line typed, its ! and ^ made into what they name; NULL when nothing was */
char	*hist_expand(const char *);

/* basharr.c: [[ ]] and the arrays */
int	is_dbracket(union node *);
int	eval_dbracket(union node *);
int	array_assign(const char *, union node *, int);
int	array_subscripted(const char *);
int	array_setelem(const char *, int);
const char *array_value(const char *, int *);
int	array_words(const char *, int, char ***);
int	array_unset(const char *);
int	array_declared_assoc(const char *);
struct shbuf;
struct shrd;
void	serarrays(struct shbuf *);
void	restorearray(struct shrd *);

#endif /* TSH_BASHEXT_H */
