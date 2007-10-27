/*
 * curses.c - curses windowing module for zsh
 *
 * This file is part of zsh, the Z shell.
 *
 * Copyright (c) 2007  Clint Adams
 * All rights reserved.
 *
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and to distribute modified versions of this software for any
 * purpose, provided that the above copyright notice and the following
 * two paragraphs appear in all copies of this software.
 *
 * In no event shall Clint Adams or the Zsh Development Group be liable
 * to any party for direct, indirect, special, incidental, or consequential
 * damages arising out of the use of this software and its documentation,
 * even if Clint Adams and the Zsh Development Group have been advised of
 * the possibility of such damage.
 *
 * Clint Adams and the Zsh Development Group specifically disclaim any
 * warranties, including, but not limited to, the implied warranties of
 * merchantability and fitness for a particular purpose.  The software
 * provided hereunder is on an "as is" basis, and Clint Adams and the
 * Zsh Development Group have no obligation to provide maintenance,
 * support, updates, enhancements, or modifications.
 *
 */

#define _XOPEN_SOURCE_EXTENDED 1

#include "curses.mdh"
#include "curses.pro"

#ifdef HAVE_NCURSES_H
# include <ncurses.h>
#else
# ifdef HAVE_CURSES_H
#  include <curses.h>
# endif
#endif

#ifndef MULTIBYTE_SUPPORT
# undef HAVE_SETCCHAR
# undef HAVE_WADDWSTR
# undef HAVE_WGET_WCH
#endif

#ifdef HAVE_SETCCHAR
# include <wchar.h>
#endif

#include <stdio.h>

enum zc_win_flags {
    /* Window is permanent (probably "stdscr") */
    ZCWF_PERMANENT = 0x0001,
    /* Scrolling enabled */
    ZCWF_SCROLL = 0x0002
};

typedef struct zc_win {
    WINDOW *win;
    char *name;
    int flags;
} *ZCWin;

struct zcurses_namenumberpair {
    char *name;
    int number;
};

struct colorpairnode {
    struct hashnode node;
    short colorpair;
};
typedef struct colorpairnode *Colorpairnode;

typedef int (*zccmd_t)(const char *nam, char **args);
struct zcurses_subcommand {
    const char *name;
    zccmd_t cmd;
    int minargs;
    int maxargs;
};

static struct ttyinfo saved_tty_state;
static struct ttyinfo curses_tty_state;
static LinkList zcurses_windows;
static HashTable zcurses_colorpairs = NULL;

#define ZCURSES_EINVALID 1
#define ZCURSES_EDEFINED 2
#define ZCURSES_EUNDEFINED 3

#define ZCURSES_UNUSED 1
#define ZCURSES_USED 2

#define ZCURSES_ATTRON 1
#define ZCURSES_ATTROFF 2

static int zc_errno, zc_color_phase=0;
static short next_cp=0;

static const struct zcurses_namenumberpair zcurses_attributes[] = {
    {"blink", A_BLINK},
    {"bold", A_BOLD},
    {"dim", A_DIM},
    {"reverse", A_REVERSE},
    {"standout", A_STANDOUT},
    {"underline", A_UNDERLINE},
    {NULL, 0}
};

static const struct zcurses_namenumberpair zcurses_colors[] = {
    {"black", COLOR_BLACK},
    {"red", COLOR_RED},
    {"green", COLOR_GREEN},
    {"yellow", COLOR_YELLOW},
    {"blue", COLOR_BLUE},
    {"magenta", COLOR_MAGENTA},
    {"cyan", COLOR_CYAN},
    {"white", COLOR_WHITE},
    {NULL, 0}
};

/* Autogenerated keypad string/number mapping*/
#include "curses_keys.h"

static char **
zcurses_pairs_to_array(const struct zcurses_namenumberpair *nnps)
{
    char **arr, **arrptr;
    int count;
    const struct zcurses_namenumberpair *nnptr;

    for (nnptr = nnps; nnptr->name; nnptr++)
	;
    count = nnptr - nnps;

    arrptr = arr = (char **)zhalloc((count+1) * sizeof(char *));

    for (nnptr = nnps; nnptr->name; nnptr++)
	*arrptr++ = dupstring(nnptr->name);
    *arrptr = NULL;

    return arr;
}

static const char *
zcurses_strerror(int err)
{
    static const char *errs[] = {
	"unknown error",
	"window name invalid",
	"window already defined",
	"window undefined",
	NULL };

    return errs[(err < 1 || err > 3) ? 0 : err];
}

static LinkNode
zcurses_getwindowbyname(const char *name)
{
    LinkNode node;
    ZCWin w;

    for (node = firstnode(zcurses_windows); node; incnode(node))
	if (w = (ZCWin)getdata(node), !strcmp(w->name, name))
	    return node;

    return NULL;
}

static LinkNode
zcurses_validate_window(char *win, int criteria)
{
    LinkNode target;

    if (win==NULL || strlen(win) < 1) {
	zc_errno = ZCURSES_EINVALID;
	return NULL;
    }

    target = zcurses_getwindowbyname(win);

    if (target && (criteria & ZCURSES_UNUSED)) {
	zc_errno = ZCURSES_EDEFINED;
	return NULL;
    }

    if (!target && (criteria & ZCURSES_USED)) {
	zc_errno = ZCURSES_EUNDEFINED;
	return NULL;
    }

    zc_errno = 0;
    return target;
}

static int
zcurses_free_window(ZCWin w)
{
    if (!(w->flags & ZCWF_PERMANENT) && delwin(w->win)!=OK)
	return 1;

    if (w->name)
	zsfree(w->name);

    zfree(w, sizeof(struct zc_win));

    return 0;
}

static int
zcurses_attribute(WINDOW *w, char *attr, int op)
{
    struct zcurses_namenumberpair *zca;

    if (!attr)
	return 1;

    for(zca=(struct zcurses_namenumberpair *)zcurses_attributes;zca->name;zca++)
	if (!strcmp(attr, zca->name)) {
	    switch(op) {
		case ZCURSES_ATTRON:
		    wattron(w, zca->number);
		    break;
		case ZCURSES_ATTROFF:
		    wattroff(w, zca->number);
		    break;
	    }

	    return 0;
	}

    return 1;
}

static short
zcurses_color(const char *color)
{
    struct zcurses_namenumberpair *zc;

    for(zc=(struct zcurses_namenumberpair *)zcurses_colors;zc->name;zc++)
	if (!strcmp(color, zc->name)) {
	    return (short)zc->number;
	}

    return (short)-1;
}

static int
zcurses_colorset(const char *nam, WINDOW *w, char *colorpair)
{
    char *bg, *cp;
    short f, b;
    Colorpairnode cpn;

    if (zc_color_phase==1 ||
	!(cpn = (Colorpairnode) gethashnode(zcurses_colorpairs, colorpair))) {
	zc_color_phase = 2;
	cp = ztrdup(colorpair);

	bg = strchr(cp, '/');
	if (bg==NULL) {
	    zsfree(cp);
	    return 1;
	}

	*bg = '\0';        
	f = zcurses_color(cp);
	b = zcurses_color(bg+1);

	if (f==-1 || b==-1) {
	    if (f == -1)
		zwarnnam(nam, "foreground color `%s' not known", cp);
	    if (b == -1)
		zwarnnam(nam, "background color `%s' not known", bg+1);
	    *bg = '/';
	    zsfree(cp);
	    return 1;
	}
	*bg = '/';

	++next_cp;
	if (next_cp >= COLOR_PAIRS || init_pair(next_cp, f, b) == ERR)  {
	    zsfree(cp);
	    return 1;
	}

	cpn = (Colorpairnode)zalloc(sizeof(struct colorpairnode));
	
	if (!cpn) {
	    zsfree(cp);
	    return 1;
	}

	cpn->colorpair = next_cp;
	addhashnode(zcurses_colorpairs, cp, (void *)cpn);
    }

    return (wcolor_set(w, cpn->colorpair, NULL) == ERR);
}

static void
freecolorpairnode(HashNode hn)
{
    zsfree(hn->nam);
    zfree(hn, sizeof(struct colorpairnode));
}


/*************
 * Subcommands
 *************/

static int
zccmd_init(const char *nam, char **args)
{
    LinkNode stdscr_win = zcurses_getwindowbyname("stdscr");

    if (!stdscr_win) {
	ZCWin w = (ZCWin)zshcalloc(sizeof(struct zc_win));
	if (!w)
	    return 1;

	gettyinfo(&saved_tty_state);
	w->name = ztrdup("stdscr");
	w->win = initscr();
	if (w->win == NULL) {
	    zsfree(w->name);
	    zfree(w, sizeof(struct zc_win));
	    return 1;
	}
	w->flags = ZCWF_PERMANENT;
	zinsertlinknode(zcurses_windows, lastnode(zcurses_windows), (void *)w);
	if (start_color() != ERR) {
	    if(!zc_color_phase)
		zc_color_phase = 1;
	    zcurses_colorpairs = newhashtable(8, "zc_colorpairs", NULL);

	    zcurses_colorpairs->hash        = hasher;
	    zcurses_colorpairs->emptytable  = emptyhashtable;
	    zcurses_colorpairs->filltable   = NULL;
	    zcurses_colorpairs->cmpnodes    = strcmp;
	    zcurses_colorpairs->addnode     = addhashnode;
	    zcurses_colorpairs->getnode     = gethashnode2;
	    zcurses_colorpairs->getnode2    = gethashnode2;
	    zcurses_colorpairs->removenode  = removehashnode;
	    zcurses_colorpairs->disablenode = NULL;
	    zcurses_colorpairs->enablenode  = NULL;
	    zcurses_colorpairs->freenode    = freecolorpairnode;
	    zcurses_colorpairs->printnode   = NULL;

	}
	/*
	 * We use cbreak mode because we don't want line buffering
	 * on input since we'd just need to loop over characters.
	 * We use noecho since the manual says that's the right
	 * thing to do with cbreak.
	 *
	 * Turn these on immediately to catch typeahead.
	 */
	cbreak();
	noecho();
	gettyinfo(&curses_tty_state);
    } else {
	settyinfo(&curses_tty_state);
    }
    return 0;
}


static int
zccmd_addwin(const char *nam, char **args)
{
    int nlines, ncols, begin_y, begin_x;
    ZCWin w;

    if (zcurses_validate_window(args[0], ZCURSES_UNUSED) == NULL &&
	zc_errno) {
	zerrnam(nam, "%s: %s", zcurses_strerror(zc_errno), args[0], 0);
	return 1;
    }

    nlines = atoi(args[1]);
    ncols = atoi(args[2]);
    begin_y = atoi(args[3]);
    begin_x = atoi(args[4]);

    w = (ZCWin)zshcalloc(sizeof(struct zc_win));
    if (!w)
	return 1;

    w->name = ztrdup(args[0]);
    w->win = newwin(nlines, ncols, begin_y, begin_x);

    if (w->win == NULL) {
	zsfree(w->name);
	free(w);
	return 1;
    }

    zinsertlinknode(zcurses_windows, lastnode(zcurses_windows), (void *)w);

    return 0;
}

static int
zccmd_delwin(const char *nam, char **args)
{
    LinkNode node;
    ZCWin w;

    node = zcurses_validate_window(args[0], ZCURSES_USED);
    if (node == NULL) {
	zwarnnam(nam, "%s: %s", zcurses_strerror(zc_errno), args[0]);
	return 1;
    }

    w = (ZCWin)getdata(node);

    if (w == NULL) {
	zwarnnam(nam, "record for window `%s' is corrupt", args[0]);
	return 1;
    }
    if (w->flags & ZCWF_PERMANENT) {
	zwarnnam(nam, "window `%s' can't be deleted", args[0]);
	return 1;
    }
    if (delwin(w->win)!=OK)
	return 1;

    if (w->name)
	zsfree(w->name);

    zfree((ZCWin)remnode(zcurses_windows, node), sizeof(struct zc_win));

    return 0;
}


static int
zccmd_refresh(const char *nam, char **args)
{
    if (args[0]) {
	LinkNode node;
	ZCWin w;

	node = zcurses_validate_window(args[0], ZCURSES_USED);
	if (node == NULL) {
	    zwarnnam(nam, "%s: %s", zcurses_strerror(zc_errno), args[0],
		     0);
	    return 1;
	}

	w = (ZCWin)getdata(node);

	return (wrefresh(w->win)!=OK) ? 1 : 0;
    }
    else
    {
	return (wrefresh(curscr) != OK) ? 1 : 0;
    }
}


static int
zccmd_move(const char *nam, char **args)
{
    int y, x;
    LinkNode node;
    ZCWin w;

    node = zcurses_validate_window(args[0], ZCURSES_USED);
    if (node == NULL) {
	zwarnnam(nam, "%s: %s", zcurses_strerror(zc_errno), args[0]);
	return 1;
    }

    y = atoi(args[1]);
    x = atoi(args[2]);

    w = (ZCWin)getdata(node);

    if (wmove(w->win, y, x)!=OK)
	return 1;

    return 0;
}


static int
zccmd_clear(const char *nam, char **args)
{
    LinkNode node;
    ZCWin w;

    node = zcurses_validate_window(args[0], ZCURSES_USED);
    if (node == NULL) {
	zwarnnam(nam, "%s: %s", zcurses_strerror(zc_errno), args[0]);
	return 1;
    }

    w = (ZCWin)getdata(node);

    if (!args[1]) {
	return werase(w->win) != OK;
    } else if (!strcmp(args[1], "redraw")) {
	return wclear(w->win) != OK;
    } else if (!strcmp(args[1], "eol")) {
	return wclrtoeol(w->win) != OK;
    } else if (!strcmp(args[1], "bot")) {
	return wclrtobot(w->win) != OK;
    } else {
	zwarnnam(nam, "`clear' expects `redraw', `eol' or `bot'");
	return 1;
    }
}


static int
zccmd_char(const char *nam, char **args)
{
    LinkNode node;
    ZCWin w;
#ifdef HAVE_SETCCHAR
    wchar_t c;
    cchar_t cc;
#endif

    node = zcurses_validate_window(args[0], ZCURSES_USED);
    if (node == NULL) {
	zwarnnam(nam, "%s: %s", zcurses_strerror(zc_errno), args[0]);
	return 1;
    }

    w = (ZCWin)getdata(node);

#ifdef HAVE_SETCCHAR
    if (mbrtowc(&c, args[1], MB_CUR_MAX, NULL) < 1)
	return 1;

    if (setcchar(&cc, &c, A_NORMAL, 0, NULL)==ERR)
	return 1;

    if (wadd_wch(w->win, &cc)!=OK)
	return 1;
#else
    if (waddch(w->win, (chtype)args[1][0])!=OK)
	return 1;
#endif

    return 0;
}


static int
zccmd_string(const char *nam, char **args)
{
    LinkNode node;
    ZCWin w;

#ifdef HAVE_WADDWSTR
    int clen;
    wint_t wc;
    wchar_t *wstr, *wptr;
    char *str = args[1];
#endif

    node = zcurses_validate_window(args[0], ZCURSES_USED);
    if (node == NULL) {
	zwarnnam(nam, "%s: %s", zcurses_strerror(zc_errno), args[0]);
	return 1;
    }

    w = (ZCWin)getdata(node);

#ifdef HAVE_WADDWSTR
    mb_metacharinit();
    wptr = wstr = zhalloc((strlen(str)+1) * sizeof(wchar_t));

    while (*str && (clen = mb_metacharlenconv(str, &wc))) {
	str += clen;
	if (wc == WEOF) /* TODO: replace with space? nicen? */
	    continue;
	*wptr++ = wc;
    }
    *wptr++ = L'\0';
    if (waddwstr(w->win, wstr)!=OK) {
	return 1;
    }
#else
    if (waddstr(w->win, args[1])!=OK)
	return 1;
#endif
    return 0;
}


static int
zccmd_border(const char *nam, char **args)
{
    LinkNode node;
    ZCWin w;

    node = zcurses_validate_window(args[0], ZCURSES_USED);
    if (node == NULL) {
	zwarnnam(nam, "%s: %s", zcurses_strerror(zc_errno), args[0]);
	return 1;
    }

    w = (ZCWin)getdata(node);

    if (wborder(w->win, 0, 0, 0, 0, 0, 0, 0, 0)!=OK)
	return 1;

    return 0;
}


static int
zccmd_endwin(const char *nam, char **args)
{
    LinkNode stdscr_win = zcurses_getwindowbyname("stdscr");

    if (stdscr_win) {
	endwin();
	/* Restore TTY as it was before zcurses -i */
	settyinfo(&saved_tty_state);
	/*
	 * TODO: should I need the following?  Without it
	 * the screen stays messed up.  Presumably we are
	 * doing stuff with shttyinfo when we shouldn't really be.
	 */
	gettyinfo(&shttyinfo);
    }
    return 0;
}


static int
zccmd_attr(const char *nam, char **args)
{
    LinkNode node;
    ZCWin w;
    char **attrs;
    int ret = 0;

    if (!args[0])
	return 1;

    node = zcurses_validate_window(args[0], ZCURSES_USED);
    if (node == NULL) {
	zwarnnam(nam, "%s: %s", zcurses_strerror(zc_errno), args[0]);
	return 1;
    }

    w = (ZCWin)getdata(node);

    for(attrs = args+1; *attrs; attrs++) {
	if (strchr(*attrs, '/')) {
	    if (zcurses_colorset(nam, w->win, *attrs))
		ret = 1;
	} else {
	    char *ptr;
	    int onoff;

	    switch(*attrs[0]) {
	    case '-':
		onoff = ZCURSES_ATTROFF;
		ptr = (*attrs) + 1;
		break;
	    case '+':
		onoff = ZCURSES_ATTRON;
		ptr = (*attrs) + 1;
		break;
	    default:
		onoff = ZCURSES_ATTRON;
		ptr = *attrs;
		break;
	    }
	    if (zcurses_attribute(w->win, ptr, onoff)) {
		zwarnnam(nam, "attribute `%s' not known", ptr);
		ret = 1;
	    }
	}
    }
    return ret;
}


static int
zccmd_scroll(const char *nam, char **args)
{
    LinkNode node;
    ZCWin w;
    int ret = 0;

    node = zcurses_validate_window(args[0], ZCURSES_USED);
    if (node == NULL) {
	zwarnnam(nam, "%s: %s", zcurses_strerror(zc_errno), args[0]);
	return 1;
    }

    w = (ZCWin)getdata(node);

    if (!strcmp(args[1], "on")) {
	if (scrollok(w->win, TRUE) == ERR)
	    return 1;
	w->flags |= ZCWF_SCROLL;
    } else if (!strcmp(args[1], "off")) {
	if (scrollok(w->win, FALSE) == ERR)
	    return 1;
	w->flags &= ~ZCWF_SCROLL;
    } else {
	char *endptr;
	zlong sl = zstrtol(args[1], &endptr, 10);
	if (*endptr) {
	    zwarnnam(nam, "scroll requires `on', `off' or integer: %s",
		     args[1]);
	    return 1;
	}
	if (!(w->flags & ZCWF_SCROLL))
	    scrollok(w->win, TRUE);
	if (wscrl(w->win, (int)sl) == ERR)
	    ret = 1;
	if (!(w->flags & ZCWF_SCROLL))
	    scrollok(w->win, FALSE);
    }

    return ret;
}


static int
zccmd_input(const char *nam, char **args)
{
    LinkNode node;
    ZCWin w;
    char *var;
    int keypadnum = -1;
#ifdef HAVE_WGET_WCH
    int ret;
    wint_t wi;
    VARARR(char, instr, 2*MB_CUR_MAX+1);
#else
    int ci;
    char instr[3];
#endif

    node = zcurses_validate_window(args[0], ZCURSES_USED);
    if (node == NULL) {
	zwarnnam(nam, "%s: %s", zcurses_strerror(zc_errno), args[0]);
	return 1;
    }

    w = (ZCWin)getdata(node);

    if (args[1] && args[2]) {
	keypad(w->win, TRUE);
    } else {
	keypad(w->win, FALSE);
    }

#ifdef HAVE_WGET_WCH
    switch (wget_wch(w->win, &wi)) {
    case OK:
	ret = wctomb(instr, (wchar_t)wi);
	if (ret == 0) {
	    instr[0] = Meta;
	    instr[1] = '\0' ^ 32;
	    instr[2] = '\0';
	} else {
	    (void)metafy(instr, ret, META_NOALLOC);
	}
	break;

    case KEY_CODE_YES:
	*instr = '\0';
	keypadnum = (int)wi;
	break;

    case ERR:
    default:
	return 1;
    }
#else
    ci = wgetch(w->win);
    if (ci == ERR)
	return 1;
    if (ci >= 256) {
	keypadnum = ci;
	*instr = '\0';
    } else {
	if (imeta(ci)) {
	    instr[0] = Meta;
	    instr[1] = (char)ci ^ 32;
	    instr[2] = '\0';
	} else {
	    instr[0] = (char)ci;
	    instr[1] = '\0';
	}
    }
#endif
    if (args[1])
	var = args[1];
    else
	var = "REPLY";
    if (!setsparam(var, ztrdup(instr)))
	return 1;
    if (args[1] && args[2]) {
	if (keypadnum > 0) {
	    const struct zcurses_namenumberpair *nnptr;
	    char fbuf[DIGBUFSIZE+1];

	    for (nnptr = keypad_names; nnptr->name; nnptr++) {
		if (keypadnum == nnptr->number) {
		    if (!setsparam(args[2], ztrdup(nnptr->name)))
			return 1;
		    return 0;
		}
	    }
	    if (keypadnum > KEY_F0) {
		/* assume it's a function key */
		sprintf(fbuf, "F%d", keypadnum - KEY_F0);
	    } else {
		/* print raw number */
		sprintf(fbuf, "%d", keypadnum);
	    }
	    if (!setsparam(args[2], ztrdup(fbuf)))
		return 1;
	} else {
	    if (!setsparam(args[2], ztrdup("")))
		return 1;
	}
    }
    return 0;
}


static int
zccmd_position(const char *nam, char **args)
{
    LinkNode node;
    ZCWin w;
    int i, intarr[6];
    char **array, dbuf[DIGBUFSIZE];

    node = zcurses_validate_window(args[0], ZCURSES_USED);
    if (node == NULL) {
	zwarnnam(nam, "%s: %s", zcurses_strerror(zc_errno), args[0]);
	return 1;
    }

    w = (ZCWin)getdata(node);

    /* Look no pointers:  these are macros. */
    if (getyx(w->win, intarr[0], intarr[1]) == ERR ||
	getbegyx(w->win, intarr[2], intarr[3]) == ERR ||
	getmaxyx(w->win, intarr[4], intarr[5]) == ERR)
	return 1;

    array = (char **)zalloc(7*sizeof(char *));
    for (i = 0; i < 6; i++) {
	sprintf(dbuf, "%d", intarr[i]);
	array[i] = ztrdup(dbuf);
    }
    array[6] = NULL;

    setaparam(args[1], array);
    return 0;
}


/*********************
  Main builtin handler
 *********************/

/**/
static int
bin_zcurses(char *nam, char **args, Options ops, UNUSED(int func))
{
    char **saargs;
    struct zcurses_subcommand *zcsc;
    int num_args;

    struct zcurses_subcommand scs[] = {
	{"init", zccmd_init, 0, 0},
	{"addwin", zccmd_addwin, 5, 5},
	{"delwin", zccmd_delwin, 1, 1},
	{"refresh", zccmd_refresh, 0, 1},
	{"move", zccmd_move, 3, 3},
	{"clear", zccmd_clear, 1, 2},
	{"position", zccmd_position, 2, 2},
	{"char", zccmd_char, 2, 2},
	{"string", zccmd_string, 2, 2},
	{"border", zccmd_border, 1, 1},
	{"end", zccmd_endwin, 0, 0},
	{"attr", zccmd_attr, 2, -1},
	{"scroll", zccmd_scroll, 2, 2},
	{"input", zccmd_input, 1, 3},
	{NULL, (zccmd_t)0, 0, 0}
    };

    for(zcsc = scs; zcsc->name; zcsc++) {
	if(!strcmp(args[0], zcsc->name))
	    break;
    }

    if (zcsc->name == NULL) {
	zwarnnam(nam, "unknown subcommand: %s", args[0]);
	return 1;
    }

    saargs = args;
    while (*saargs++);
    num_args = saargs - (args + 2);

    if (num_args < zcsc->minargs) {
	zwarnnam(nam, "too few arguments for subcommand: %s", args[0]);
	return 1;
    } else if (zcsc->maxargs >= 0 && num_args > zcsc->maxargs) {
	zwarnnam(nam, "too may arguments for subcommand: %s", args[0]);
	return 1;
    }

    if (zcsc->cmd != zccmd_init && zcsc->cmd != zccmd_endwin &&
	!zcurses_getwindowbyname("stdscr")) {
	zwarnnam(nam, "command `%s' can't be used before `zcurses init'",
		 zcsc->name);
	return 1;
    }

    return zcsc->cmd(nam, args+1);
}


static struct builtin bintab[] = {
    BUILTIN("zcurses", 0, bin_zcurses, 1, 6, 0, "", NULL),
};


/*******************
 * Special variables
 *******************/

static char **
zcurses_colorsarrgetfn(UNUSED(Param pm))
{
    return zcurses_pairs_to_array(zcurses_colors);
}

static const struct gsu_array zcurses_colorsarr_gsu =
{ zcurses_colorsarrgetfn, arrsetfn, stdunsetfn };


static char **
zcurses_attrgetfn(UNUSED(Param pm))
{
    return zcurses_pairs_to_array(zcurses_attributes);
}

static const struct gsu_array zcurses_attrs_gsu =
{ zcurses_attrgetfn, arrsetfn, stdunsetfn };


static char **
zcurses_windowsgetfn(UNUSED(Param pm))
{
    LinkNode node;
    char **arr, **arrptr;
    int count = countlinknodes(zcurses_windows);

    arrptr = arr = (char **)zhalloc((count+1) * sizeof(char *));

    for (node = firstnode(zcurses_windows); node; incnode(node))
	*arrptr++ = dupstring(((ZCWin)getdata(node))->name);
    *arrptr = NULL;

    return arr;
}

static const struct gsu_array zcurses_windows_gsu =
{ zcurses_windowsgetfn, arrsetfn, stdunsetfn };


static zlong
zcurses_colorsintgetfn(UNUSED(Param pm))
{
    return COLORS;
}

static const struct gsu_integer zcurses_colorsint_gsu =
{ zcurses_colorsintgetfn, nullintsetfn, stdunsetfn };


static zlong
zcurses_colorpairsintgetfn(UNUSED(Param pm))
{
    return COLOR_PAIRS;
}

static const struct gsu_integer zcurses_colorpairsint_gsu =
{ zcurses_colorpairsintgetfn, nullintsetfn, stdunsetfn };


static struct paramdef partab[] = {
    SPECIALPMDEF("zcurses_colors", PM_ARRAY|PM_READONLY,
		 &zcurses_colorsarr_gsu, NULL, NULL),
    SPECIALPMDEF("zcurses_attrs", PM_ARRAY|PM_READONLY,
		 &zcurses_attrs_gsu, NULL, NULL),
    SPECIALPMDEF("zcurses_windows", PM_ARRAY|PM_READONLY,
		 &zcurses_windows_gsu, NULL, NULL),
    SPECIALPMDEF("ZCURSES_COLORS", PM_INTEGER|PM_READONLY,
		 &zcurses_colorsint_gsu, NULL, NULL),
    SPECIALPMDEF("ZCURSES_COLOR_PAIRS", PM_INTEGER|PM_READONLY,
		 &zcurses_colorpairsint_gsu, NULL, NULL)
};

/***************************
 * Standard module interface
 ***************************/


/*
 * boot_ is executed when the module is loaded.
 */

static struct features module_features = {
    bintab, sizeof(bintab)/sizeof(*bintab),
    NULL, 0,
    NULL, 0,
    partab, sizeof(partab)/sizeof(*partab),
    0
};

/**/
int
setup_(UNUSED(Module m))
{
    return 0;
}

/**/
int
features_(Module m, char ***features)
{
    *features = featuresarray(m, &module_features);
    return 0;
}

/**/
int
enables_(Module m, int **enables)
{
    return handlefeatures(m, &module_features, enables);
}

/**/
int
boot_(Module m)
{
    zcurses_windows = znewlinklist();

    return 0;
}

/**/
int
cleanup_(Module m)
{
    freelinklist(zcurses_windows, (FreeFunc) zcurses_free_window);
    if (zcurses_colorpairs)
	deletehashtable(zcurses_colorpairs);
    return setfeatureenables(m, &module_features, NULL);
}

/**/
int
finish_(UNUSED(Module m))
{
    return 0;
}
