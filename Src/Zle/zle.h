/*
 * zle.h - header file for line editor
 *
 * This file is part of zsh, the Z shell.
 *
 * Copyright (c) 1992-1997 Paul Falstad
 * All rights reserved.
 *
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and to distribute modified versions of this software for any
 * purpose, provided that the above copyright notice and the following
 * two paragraphs appear in all copies of this software.
 *
 * In no event shall Paul Falstad or the Zsh Development Group be liable
 * to any party for direct, indirect, special, incidental, or consequential
 * damages arising out of the use of this software and its documentation,
 * even if Paul Falstad and the Zsh Development Group have been advised of
 * the possibility of such damage.
 *
 * Paul Falstad and the Zsh Development Group specifically disclaim any
 * warranties, including, but not limited to, the implied warranties of
 * merchantability and fitness for a particular purpose.  The software
 * provided hereunder is on an "as is" basis, and Paul Falstad and the
 * Zsh Development Group have no obligation to provide maintenance,
 * support, updates, enhancements, or modifications.
 *
 */

#ifdef ZLE_UNICODE_SUPPORT
typedef wchar_t ZLE_CHAR_T;
typedef wchar_t *ZLE_STRING_T;
typedef wint_t   ZLE_INT_T;
#define ZLE_CHAR_SIZE	sizeof(wchar_t)

/*
 * MB_CUR_MAX is the maximum number of bytes that a single wide
 * character will convert into.  We use it to keep strings
 * sufficiently long.  It should always be defined, but if it isn't
 * just assume we are using Unicode which requires 6 characters.
 * (Note that it's not necessarily defined to a constant.)
 */
#ifndef MB_CUR_MAX
#define MB_CUR_MAX 6
#endif

#define ZLENL	L'\n'
#define ZLENUL	L'\0'
#define ZLETAB	L'\t'

#define DIGIT_1		L'1'
#define DIGIT_9		L'9'
#define LETTER_a	L'a'
#define LETTER_z	L'z'
#define LETTER_A	L'A'
#define LETTER_Z	L'Z'
#define LETTER_y	L'y'
#define LETTER_n	L'n'

#define ZLENULSTR	L""
#define ZLEEOF	WEOF
#define ZS_memcpy wmemcpy
#define ZS_memmove wmemmove
#define ZC_icntrl iswcntrl

#define LASTFULLCHAR	lastchar_wide

#else  /* Not ZLE_UNICODE_SUPPORT: old single-byte code */

typedef int ZLE_CHAR_T;
typedef unsigned char *ZLE_STRING_T;
typedef int ZLE_INT_T;
#define ZLE_CHAR_SIZE	sizeof(unsigned char)

#define ZLENL	'\n'
#define ZLENUL	'\0'
#define ZLETAB	'\t'

#define DIGIT_1		'1'
#define DIGIT_9		'9'
#define LETTER_a	'a'
#define LETTER_z	'z'
#define LETTER_A	'A'
#define LETTER_Z	'Z'
#define LETTER_y	'y'
#define LETTER_n	'n'

#define ZLENULSTR	""
#define ZLEEOF	EOF
#define ZS_memcpy memcpy
#define ZS_memmove memmove
#define ZC_icntrl icntrl

#define LASTFULLCHAR	lastchar

#endif


typedef struct widget *Widget;
typedef struct thingy *Thingy;

/* widgets (ZLE functions) */

typedef int (*ZleIntFunc) _((char **));

struct widget {
    int flags;		/* flags (see below) */
    Thingy first;	/* `first' thingy that names this widget */
    union {
	ZleIntFunc fn;	/* pointer to internally implemented widget */
	char *fnnam;	/* name of the shell function for user-defined widget */
	struct {
	    ZleIntFunc fn; /* internal widget function to call */
	    char *wid;     /* name of widget to call */
	    char *func;    /* name of shell function to call */
	} comp;
    } u;
};

#define WIDGET_INT	(1<<0)	/* widget is internally implemented */
#define WIDGET_NCOMP    (1<<1)	/* new style completion widget */
#define ZLE_MENUCMP	(1<<2)	/* DON'T invalidate completion list */
#define ZLE_YANK	(1<<3)
#define ZLE_LINEMOVE	(1<<4)	/* command is a line-oriented movement */
#define ZLE_LASTCOL     (1<<5)	/* command maintains lastcol correctly */
#define ZLE_KILL	(1<<6)
#define ZLE_KEEPSUFFIX	(1<<7)	/* DON'T remove added suffix */
#define ZLE_NOTCOMMAND  (1<<8)	/* widget should not alter lastcmd */
#define ZLE_ISCOMP      (1<<9)	/* usable for new style completion */

/* thingies */

struct thingy {
    HashNode next;	/* next node in the hash chain */
    char *nam;		/* name of the thingy */
    int flags;		/* TH_* flags (see below) */
    int rc;		/* reference count */
    Widget widget;	/* widget named by this thingy */
    Thingy samew;	/* `next' thingy (circularly) naming the same widget */
};

/* DISABLED is (1<<0) */
#define TH_IMMORTAL	(1<<1)    /* can't refer to a different widget */

/* command modifier prefixes */

struct modifier {
    int flags;		/* MOD_* flags (see below) */
    int mult;		/* repeat count */
    int tmult;		/* repeat count actually being edited */
    int vibuf;		/* vi cut buffer */
};

#define MOD_MULT  (1<<0)   /* a repeat count has been selected */
#define MOD_TMULT (1<<1)   /* a repeat count is being entered */
#define MOD_VIBUF (1<<2)   /* a vi cut buffer has been selected */
#define MOD_VIAPP (1<<3)   /* appending to the vi cut buffer */
#define MOD_NEG   (1<<4)   /* last command was negate argument */

/* current modifier status */

#define zmult (zmod.mult)

/* undo system */

struct change {
    struct change *prev, *next;	/* adjacent changes */
    int flags;			/* see below */
    int hist;			/* history line being changed */
    int off;			/* offset of the text changes */
    ZLE_STRING_T del;		/* characters to delete */
    int dell;			/* no. of characters in del */
    ZLE_STRING_T ins;		/* characters to insert */
    int insl;			/* no. of characters in ins */
    int old_cs, new_cs;		/* old and new cursor positions */
};

#define CH_NEXT (1<<0)   /* next structure is also part of this change */
#define CH_PREV (1<<1)   /* previous structure is also part of this change */

/* known thingies */

#define Th(X) (&thingies[X])

/* opaque keymap type */

typedef struct keymap *Keymap;

typedef void (*KeyScanFunc) _((char *, Thingy, char *, void *));

#define invicmdmode() (!strcmp(curkeymapname, "vicmd"))

/* Standard type of suffix removal. */

#define removesuffix() iremovesuffix(256, 0)

/*
 * Cut/kill buffer type.  The buffer itself is purely binary data, not
 * NUL-terminated.  len is a length count (N.B. number of characters,
 * not size in bytes).  flags uses the CUTBUFFER_* constants defined
 * below.
 */

struct cutbuffer {
    ZLE_STRING_T buf;
    size_t len;
    char flags;
};

typedef struct cutbuffer *Cutbuffer;

#define CUTBUFFER_LINE 1   /* for vi: buffer contains whole lines of data */

#define KRINGCTDEF 8   /* default number of buffers in the kill ring */

/* Types of completion. */

#define COMP_COMPLETE        0
#define COMP_LIST_COMPLETE   1
#define COMP_SPELL           2
#define COMP_EXPAND          3
#define COMP_EXPAND_COMPLETE 4
#define COMP_LIST_EXPAND     5
#define COMP_ISEXPAND(X) ((X) >= COMP_EXPAND)

/* Information about one brace run. */

typedef struct brinfo *Brinfo;

struct brinfo {
    Brinfo next;		/* next in list */
    Brinfo prev;		/* previous (only for closing braces) */
    char *str;			/* the string to insert */
    int pos;			/* original position */
    int qpos;			/* original position, with quoting */
    int curpos;			/* position for current match */
};

/* Convenience macros for the hooks */

#define LISTMATCHESHOOK    (zlehooks + 0)
#define COMPLETEHOOK       (zlehooks + 1)
#define BEFORECOMPLETEHOOK (zlehooks + 2)
#define AFTERCOMPLETEHOOK  (zlehooks + 3)
#define ACCEPTCOMPHOOK     (zlehooks + 4)
#define REVERSEMENUHOOK    (zlehooks + 5)
#define INVALIDATELISTHOOK (zlehooks + 6)

/* complete hook data struct */

typedef struct compldat *Compldat;

struct compldat {
    char *s;
    int lst;
    int incmd;
};

/* List completion matches. */

#define listmatches() runhookdef(LISTMATCHESHOOK, NULL)

/* Invalidate the completion list. */

#define invalidatelist() runhookdef(INVALIDATELISTHOOK, NULL)

/* Bit flags to setline */
enum {
    ZSL_COPY = 1,		/* Copy the argument, don't modify it */
    ZSL_TOEND = 2,		/* Go to the end of the new line */
};
