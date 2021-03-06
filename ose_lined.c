/*
  Copyright (c) 2019-22 John MacCallum Permission is hereby granted,
  free of charge, to any person obtaining a copy of this software
  and associated documentation files (the "Software"), to deal in
  the Software without restriction, including without limitation the
  rights to use, copy, modify, merge, publish, distribute,
  sublicense, and/or sell copies of the Software, and to permit
  persons to whom the Software is furnished to do so, subject to the
  following conditions:

  The above copyright notice and this permission notice shall be
  included in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
  HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
  WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
  DEALINGS IN THE SOFTWARE.
*/

/* memmove */
#include <string.h>

#include "ose_conf.h"
#include "ose.h"
#include "ose_context.h"
#include "ose_util.h"
#include "ose_stackops.h"
#include "ose_assert.h"
#include "ose_vm.h"
#include "ose_print.h"

#define OSE_LINED_BUFSIZE 4096

#define BUFSIZE_OFFSET (OSE_BUNDLE_HEADER_LEN + 12)
#define BUFLEN_OFFSET (BUFSIZE_OFFSET + 16)
#define CURPOS_OFFSET (BUFLEN_OFFSET + 16)
/* placeholder until we actually keep track of cols */
#define COLS_OFFSET CURPOS_OFFSET /* (CURPOS_OFFSET + 16) */
/* skips over the size of the blob */
#define BUF_OFFSET (COLS_OFFSET + 20)

#define BUFSIZE ose_readInt32(ose_getBundlePtr(vm_le), \
                              BUFSIZE_OFFSET)
#define BUFLEN ose_readInt32(ose_getBundlePtr(vm_le), BUFLEN_OFFSET)
#define CURPOS ose_readInt32(ose_getBundlePtr(vm_le), CURPOS_OFFSET)
#define BUFP ose_getBundlePtr(vm_le) + BUF_OFFSET

#define PROMPTSTRING_OFFSET (OSE_BUNDLE_HEADER_LEN + 12)
#define PROMPTSTRING ose_getBundlePtr(vm_lo) + PROMPTSTRING_OFFSET
#define WORDBREAKCHARS_OFFSET PROMPTSTRING_OFFSET + \
    (ose_pstrlen(PROMPTSTRING) + 12)
#define WORDBREAKCHARS ose_getBundlePtr(vm_lo) +    \
    WORDBREAKCHARS_OFFSET

#ifdef OSE_DEBUG
const int32_t bufsize_offset = BUFSIZE_OFFSET;
const int32_t buflen_offset = BUFLEN_OFFSET;
const int32_t curpos_offset = CURPOS_OFFSET;
const int32_t cols_offset = COLS_OFFSET;
const int32_t buf_offset = BUF_OFFSET;
const int32_t promptstring_offset = PROMPTSTRING_OFFSET;
#endif

#ifndef CTRL
#define CTRL(c) (c & 0x1f)
#endif

#define BS 8
#define LF 10
#define RET 13
#define ESC 27
#define SPC 32
#define DEL 127

#define OSE_LINED_PROMPTSTRING "/ "
#define OSE_LINED_WORDBREAKCHARS "/"

#define OSE_LINED_MAX_NUM_CHARS 4

static void ose_lined_prompt(ose_bundle osevm);

static int addchar(ose_bundle vm_le, int32_t c)
{
    int32_t bufsize = ose_readInt32(vm_le, BUFSIZE_OFFSET);
    int32_t buflen = ose_readInt32(vm_le, BUFLEN_OFFSET);
    int32_t curpos = ose_readInt32(vm_le, CURPOS_OFFSET);
    if(buflen < bufsize)
    {
        if(buflen == curpos)
        {
            ose_writeByte(vm_le, BUF_OFFSET + buflen, c);
            ++buflen;
            ++curpos;
        }
        else
        {
            char *p = ose_getBundlePtr(vm_le) + BUF_OFFSET;
            memmove(p + curpos + 1, p + curpos, buflen - curpos);
            ose_writeByte(vm_le, BUF_OFFSET + curpos, c);
            ++buflen;
            ++curpos;
        }
    }
    ose_writeInt32(vm_le, BUFLEN_OFFSET, buflen);
    ose_writeInt32(vm_le, CURPOS_OFFSET, curpos);
    return 1;
}

static void delchar(ose_bundle vm_le)
{
    int32_t buflen = ose_readInt32(vm_le, BUFLEN_OFFSET);
    int32_t curpos = ose_readInt32(vm_le, CURPOS_OFFSET);
    if(curpos > 0)
    {
        if(curpos == buflen)
        {
            --buflen;
            --curpos;
            ose_writeByte(vm_le, BUF_OFFSET + curpos, 0);
        }
        else
        {
            char *b = ose_getBundlePtr(vm_le) + BUF_OFFSET;
            memmove(b + curpos - 1, b + curpos, buflen - curpos);
            b[buflen - 1] = 0;
            --buflen;
            --curpos;
        }
    }
    ose_writeInt32(vm_le, BUFLEN_OFFSET, buflen);
    ose_writeInt32(vm_le, CURPOS_OFFSET, curpos);
}

static void clear(ose_bundle vm_le)
{
    char *b = ose_getBundlePtr(vm_le);
    memset(b + BUF_OFFSET, 0, OSE_LINED_BUFSIZE);
    ose_writeInt32(vm_le, BUFLEN_OFFSET, 0);
    ose_writeInt32(vm_le, CURPOS_OFFSET, 0);
}

static void inccurpos(ose_bundle vm_le)
{
    int32_t bufsize = ose_readInt32(vm_le, BUFSIZE_OFFSET);
    int32_t buflen = ose_readInt32(vm_le, BUFLEN_OFFSET);
    int32_t curpos = ose_readInt32(vm_le, CURPOS_OFFSET);
    if(curpos < buflen && curpos < bufsize)
    {
        ++curpos;
    }
    ose_writeInt32(vm_le, CURPOS_OFFSET, curpos);
}

static void deccurpos(ose_bundle vm_le)
{
    int32_t curpos = ose_readInt32(vm_le, CURPOS_OFFSET);
    if(curpos > 0)
    {
        --curpos;
    }
    ose_writeInt32(vm_le, CURPOS_OFFSET, curpos);
}

static void pushline(ose_bundle osevm,
                     const char * const line,
                     int32_t oldlen,
                     int32_t newlen,
                     int32_t curpos)
{
    ose_bundle vm_s = OSEVM_STACK(osevm);
    ose_pushString(vm_s, line);
    ose_pushInt32(vm_s, oldlen);
    ose_pushInt32(vm_s, newlen);
    ose_pushInt32(vm_s, curpos);
}

static void setposvars(ose_bundle vm_le,
                       int32_t bufsize,
                       int32_t buflen,
                       int32_t curpos)
{
    ose_writeInt32(vm_le, BUFSIZE_OFFSET, bufsize);
    ose_writeInt32(vm_le, BUFLEN_OFFSET, buflen);
    ose_writeInt32(vm_le, CURPOS_OFFSET, curpos);
}

static const char *gethistitem(ose_bundle vm_lh)
{
    const int32_t histnum =
        ose_readInt32(vm_lh, ose_getLastBundleElemOffset(vm_lh) + 16);
    int32_t o = OSE_BUNDLE_HEADER_LEN;
    const int32_t s = ose_readInt32(vm_lh, o);
    int32_t i;
    o += 4 + OSE_BUNDLE_HEADER_LEN;
    if(histnum < 0)
    {
        return NULL;
    }

    for(i = 0;
        i < histnum && (o - (OSE_BUNDLE_HEADER_LEN + 4)) < s;
        ++i, o += ose_readInt32(vm_lh, o) + 4)
    {
        ;
    }
    {
        const char *p = ose_getBundlePtr(vm_lh) + o + 12;
        return p;
    }
}

static void inchistnum(ose_bundle vm_lh)
{
    const int32_t o = ose_getLastBundleElemOffset(vm_lh) + 12;
    ose_assert(o > OSE_BUNDLE_HEADER_LEN);
    ose_assert(o < ose_readSize(vm_lh));
    {
        const int32_t histcount = ose_readInt32(vm_lh, o);
        const int32_t histnum = ose_readInt32(vm_lh, o + 4);
        ose_assert(histcount >= 0);
        ose_assert(histnum < histcount);
        if(histnum + 1 < histcount)
        {
            ose_writeInt32(vm_lh, o + 4,
                           ose_readInt32(vm_lh, o + 4) + 1);
        }
    }
}

static void dechistnum(ose_bundle vm_lh)
{
    const int32_t o = ose_getLastBundleElemOffset(vm_lh) + 12;
    ose_assert(o > OSE_BUNDLE_HEADER_LEN);
    ose_assert(o < ose_readSize(vm_lh));
    {
        const int32_t histcount = ose_readInt32(vm_lh, o);
        const int32_t histnum = ose_readInt32(vm_lh, o + 4);
        ose_assert(histcount >= 0);
        ose_assert(histnum < histcount);
        if(histnum - 1 >= -1)
        {
            ose_writeInt32(vm_lh, o + 4,
                           ose_readInt32(vm_lh, o + 4) - 1);
        }
    }
}

static void resethistnum(ose_bundle vm_lh)
{
    const int32_t o = ose_getLastBundleElemOffset(vm_lh) + 12;
    ose_assert(o > OSE_BUNDLE_HEADER_LEN);
    ose_assert(o < ose_readSize(vm_lh));
    ose_writeInt32(vm_lh, o + 4, -1);
}

static int chariswbc(char c, int32_t nwbcs,
                     const char * const wbcs)
{
    int32_t i;
    for(i = 0; i < nwbcs; i++)
    {
        if(c == wbcs[i])
        {
            return 1;
        }
    }
    return 0;
}

static void ose_lined_char(ose_bundle osevm)
{
    ose_bundle vm_le = ose_enter(osevm, "/le");
    ose_assert(ose_getBundlePtr(vm_le));
    ose_bundle vm_lo = ose_enter(osevm, "/lo");
    ose_assert(ose_getBundlePtr(vm_lo));
    ose_bundle vm_lh = ose_enter(osevm, "/lh");
    ose_assert(ose_getBundlePtr(vm_lh));
    ose_bundle vm_s = OSEVM_STACK(osevm);
    ose_bundle vm_c = OSEVM_CONTROL(osevm);
    ose_bundle vm_i = OSEVM_INPUT(osevm);
    ose_assert(ose_bundleHasAtLeastNElems(vm_s, 1));
    ose_assert(ose_peekType(vm_s) == OSETT_MESSAGE);
    ose_assert(ose_peekMessageArgType(vm_s) == OSETT_INT32);
    /* int32_t c = ose_popInt32(vm_s); */
    char *b = ose_getBundlePtr(vm_le);
    char *bufp = b + BUF_OFFSET;
    int32_t bufsize = ose_readInt32(vm_le, BUFSIZE_OFFSET);
    int32_t buflen = ose_readInt32(vm_le, BUFLEN_OFFSET);
    int32_t curpos = ose_readInt32(vm_le, CURPOS_OFFSET);
    int32_t promptlen = strlen(PROMPTSTRING);


    int32_t numchars = 0;
    if(ose_bundleHasAtLeastNElems(vm_s, 2)
       && ose_peekType(vm_s) == OSETT_MESSAGE
       && ose_peekMessageArgType(vm_s) == OSETT_INT32)
    {
        numchars = ose_popInt32(vm_s);
    }
    if(numchars == 0)
    {
        return;
    }
    ose_assert(ose_bundleHasAtLeastNElems(vm_s, numchars));

    int32_t i = 0;
    while(i < numchars)
    {
        if(ose_peekType(vm_s) != OSETT_MESSAGE
           || ose_peekMessageArgType(vm_s) != OSETT_INT32)
        {
            while(i < numchars)
            {
                ose_drop(vm_s);
            }
            pushline(osevm, bufp, buflen, buflen, curpos);
            return;
        }
        int32_t c = ose_popInt32(vm_s);
        ++i;
        switch(c)
        {
        case CTRL('a'):
        {
            /* jump to beginning of line (end of prompt) */
            int32_t o = promptlen;
            ose_writeInt32(vm_le, CURPOS_OFFSET, o);
            pushline(osevm, b + BUF_OFFSET, buflen, buflen, o);
        }
        break;
        case CTRL('b'):
            /* move back one char */
            if(ose_readInt32(vm_le, CURPOS_OFFSET) >
               promptlen)
            {
                deccurpos(vm_le);
                pushline(osevm, b + BUF_OFFSET, buflen, buflen,
                         curpos - 1);
            }
            else
            {
                pushline(osevm, b + BUF_OFFSET, buflen, buflen,
                         curpos);
            }
            break;
        case CTRL('c'):
            ose_pushString(vm_c, "/!/lined/binding/C^c");
            ose_swap(vm_c);
            pushline(osevm, bufp, buflen, buflen, curpos);
            break;
        case CTRL('d'):
            /* delete char under cursor */
            if(curpos < buflen)
            {
                inccurpos(vm_le);
                delchar(vm_le);
                pushline(osevm, bufp, buflen, buflen - 1, curpos);
            }
            else
            {
                pushline(osevm, bufp, buflen, buflen, curpos);
            }
            break;
        case CTRL('e'):
            /* jump to end of line */
            ose_writeInt32(vm_le, CURPOS_OFFSET, buflen);
            pushline(osevm, b + BUF_OFFSET, buflen, buflen, buflen);
            break;
        case CTRL('f'):
            /* move forward one char */
            inccurpos(vm_le);
            pushline(osevm, b + BUF_OFFSET, buflen, buflen,
                     curpos + 1);
            break;
        case CTRL('k'):
        {
            /* kill forward to end of line */
            memset(bufp + curpos, 0, buflen - curpos);
            ose_writeInt32(vm_le, BUFLEN_OFFSET, curpos);
            pushline(osevm, bufp, buflen, curpos, curpos);
            buflen = curpos;
            resethistnum(vm_lh);
        }
        break;
        case CTRL('n'):
        {
            /* get next history item */
            dechistnum(vm_lh);
            const char * const p = gethistitem(vm_lh);
            if(!p)
            {
                /* pushline(osevm, bufp, buflen, buflen, curpos); */
                memset(bufp + promptlen, 0, buflen - promptlen);
                ose_writeInt32(vm_le, BUFLEN_OFFSET, promptlen);
                curpos = promptlen;
                ose_writeInt32(vm_le, CURPOS_OFFSET, promptlen);
                pushline(osevm, bufp, buflen, curpos, curpos);
                buflen = curpos;
                break;
            }
            int32_t len = strlen(p);
            int32_t plen = ose_pnbytes(len);
            if(len < buflen)
            {
                memset(bufp + promptlen, 0, buflen - promptlen);
            }
            memcpy(bufp + promptlen, p, plen);
            len += promptlen;
            pushline(osevm, bufp, len, len, len);
            setposvars(vm_le, bufsize, len, len);
        }
        break;
        case CTRL('p'):
        {
            /* get previous history item */
            inchistnum(vm_lh);
            const char * const p = gethistitem(vm_lh);
            if(!p)
            {
                pushline(osevm, bufp, buflen, buflen, curpos);
                break;
            }
            int32_t len = strlen(p);
            int32_t plen = ose_pnbytes(len);
            if(len < buflen)
            {
                memset(bufp + promptlen, 0, buflen - promptlen);
            }
            memcpy(bufp + promptlen, p, plen);
            len += promptlen;
            pushline(osevm, bufp, len, len, len);
            setposvars(vm_le, bufsize, len, len);
        }
        break;
        case LF:
        case RET:
            if(curpos == promptlen)
            {
                pushline(osevm, bufp, buflen, buflen, curpos);
                break;
            }
            ose_pushString(vm_s, b + BUF_OFFSET + promptlen);
            clear(vm_le);
            ose_pushString(vm_c, "/!/lined/binding/RET");
            ose_swap(vm_c);
            resethistnum(vm_lh);
            break;
        case BS:
        case DEL:
            if(ose_readInt32(vm_le, CURPOS_OFFSET) >
               promptlen)
            {
                delchar(vm_le);
                pushline(osevm, b + BUF_OFFSET,
                         buflen, buflen - 1, curpos - 1);
            }
            else
            {
                pushline(osevm, b + BUF_OFFSET,
                         buflen, buflen, curpos);
            }
            resethistnum(vm_lh);
            break;
        case ESC:
            if(i < numchars
               && ose_peekType(vm_s) == OSETT_MESSAGE
               && ose_peekMessageArgType(vm_s) == OSETT_INT32)
            {
                const char ec = (char)ose_popInt32(vm_s);
                const char * const wbcs = WORDBREAKCHARS;
                const int32_t nwbcs = strlen(wbcs);
                ++i;
                switch(ec)
                {
                case 'b':
                {
                    /* jump back to prev word break char */
                    if(chariswbc(bufp[curpos - 1], nwbcs, wbcs))
                    {
                        deccurpos(vm_le);
                        --curpos;
                    }
                    
                    while(!chariswbc(bufp[curpos - 1], nwbcs, wbcs)
                          && curpos > promptlen)
                    {
                        deccurpos(vm_le);
                        --curpos;
                    }
                    pushline(osevm, bufp, buflen, buflen,
                             curpos);
                }
                break;
                case 'd':
                {
                    /* delete from curpos to next word break char */
                    int32_t i, j;
                    for(i = curpos, j = 0; i < buflen; ++i, ++j)
                    {
                        if(chariswbc(bufp[i], nwbcs, wbcs))
                        {
                            break;
                        }
                    }
                    int32_t n = buflen - curpos - j;
                    memmove(bufp + curpos, bufp + curpos + j, n);
                    memset(bufp + buflen - j, 0, j);
                    ose_writeInt32(vm_le, BUFLEN_OFFSET, buflen - j);
                    pushline(osevm, bufp, buflen, buflen - j,
                             curpos);
                    buflen -= j;
                }
                break;
                case 'f':
                {
                    /* jump forward to next word break char */
                    if(chariswbc(bufp[curpos], nwbcs, wbcs))
                    {
                        inccurpos(vm_le);
                        ++curpos;
                    }  
                    while(!chariswbc(bufp[curpos], nwbcs, wbcs)
                          && curpos <= buflen)
                    {
                        inccurpos(vm_le);
                        ++curpos;
                    }
                    pushline(osevm, bufp, buflen, buflen,
                             curpos);
                }
                break;
                case BS:
                case DEL:
                {
                    /* delete back to prev word break char */
                    int32_t i = 0;
                    if(chariswbc(bufp[curpos - 1], nwbcs, wbcs))
                    {
                        delchar(vm_le);
                        --curpos;
                        ++i;
                    }
                    while(!chariswbc(bufp[curpos - 1], nwbcs, wbcs)
                          && curpos > promptlen)
                    {
                        delchar(vm_le);
                        --curpos;
                        ++i;
                    }
                    pushline(osevm, bufp, buflen, buflen - i,
                             curpos);
                    break;
                }
                default:
                    pushline(osevm, bufp, buflen, buflen, curpos);
                    break;
                }
                for( ; i < numchars; ++i)
                {
                    /* eat up the rest */
                    ose_popInt32(vm_s);
                }
            }
            else
            {
                /* we don't implement ESC at the moment */
                pushline(osevm, b + BUF_OFFSET, buflen, buflen,
                         curpos);
            }
            break;
        default:
            addchar(vm_le, c);
            pushline(osevm, b + BUF_OFFSET,
                     buflen, buflen + 1, curpos + 1);
            break;
        }
    }    
}

static void ose_lined_format(ose_bundle osevm)
{
    ose_bundle vm_s = OSEVM_STACK(osevm);
    char buf[8192];
    memset(buf, 0, 8192);
    int32_t n = ose_pprintBundle(vm_s, buf, 8192);
    buf[n++] = '\n';
    buf[n++] = '\r';
    ose_pushString(vm_s, buf);
}

static void ose_lined_print(ose_bundle osevm)
{
    ose_bundle vm_s = OSEVM_STACK(osevm);
    /* arg check */

    int32_t curpos = ose_popInt32(vm_s);
    int32_t newlen = ose_popInt32(vm_s);
    /* int32_t oldlen =  */ose_popInt32(vm_s);
    if(curpos < newlen)
    {
        char buf[(newlen - curpos) + 1];
        int i;
        for(i = 0; i < newlen - curpos; i++)
        {
            buf[i] = '\b';
        }
        buf[newlen - curpos] = 0;
        ose_pushString(vm_s, buf);
        ose_push(vm_s);
        ose_concatenateStrings(vm_s);
    }
}

static void ose_lined_prompt(ose_bundle osevm)
{
    ose_bundle vm_s = OSEVM_STACK(osevm);
    ose_bundle vm_le = ose_enter(osevm, "/le");
    ose_assert(ose_getBundlePtr(vm_le));
    ose_bundle vm_lo = ose_enter(osevm, "/lo");
    ose_assert(ose_getBundlePtr(vm_lo));
    const char * const bufp = BUFP;
    const char * const promptstring = PROMPTSTRING;
    const int32_t promptlen = strlen(promptstring);
    {
        int i = 0;
        for(; i < promptlen; i++)
        {
            addchar(vm_le, promptstring[i]);
        }
    }
    ose_pushString(vm_s, bufp);
    ose_pushInt32(vm_s, 0);
    ose_pushInt32(vm_s, promptlen);
    ose_pushInt32(vm_s, promptlen);
}

static void ose_lined_init(ose_bundle osevm)
{
    ose_bundle vm_s = OSEVM_STACK(osevm);
}

static void ose_lined_addToHist(ose_bundle osevm)
{
    ose_bundle vm_s = OSEVM_STACK(osevm);
    ose_bundle vm_lh = ose_enter(osevm, "/lh");
    ose_assert(ose_getBundlePtr(vm_lh));
    if(ose_bundleHasAtLeastNElems(vm_s, 1)
       && ose_peekType(vm_s) == OSETT_MESSAGE
       && ose_peekMessageArgType(vm_s) == OSETT_STRING)
    {
        const char * const str = ose_peekString(vm_s);
        const int32_t len = ose_pstrlen(str);
        const int32_t msgsize = len + 12;
        int32_t freespace = ose_spaceAvailable(vm_lh);
        if(freespace - msgsize <= 20)
        {
            ose_swap(vm_lh);
            while(freespace - msgsize <= 20 + msgsize + 4)
            {
                ose_pop(vm_lh);
                ose_drop(vm_lh);
                freespace = ose_spaceAvailable(vm_lh);
            }
            ose_swap(vm_lh);
        }
        ose_push(vm_lh);
        ose_pushString(vm_lh, str);
        ose_swap(vm_lh);
        ose_unpackDrop(vm_lh);
        ose_bundleAll(vm_lh);
        ose_pop(vm_lh);
        {
            const int32_t o = ose_getLastBundleElemOffset(vm_lh);
            ose_writeInt32(vm_lh, o + 12,
                           ose_readInt32(vm_lh, o + 12) + 1);
        }
    }
}

void ose_main(ose_bundle osevm)
{
    /* main lined bundle */
    ose_pushContextMessage(osevm, 8192, "/le");
    ose_bundle vm_le = ose_enter(osevm, "/le");
    /* options */
    ose_pushContextMessage(osevm, 512, "/lo");
    ose_bundle vm_lo = ose_enter(osevm, "/lo");
    /* history */
    ose_pushContextMessage(osevm, 8192, "/lh");
    ose_bundle vm_lh = ose_enter(osevm, "/lh");
    /* kill ring */
    ose_pushContextMessage(osevm, 8192, "/lk");
    ose_bundle vm_lk = ose_enter(osevm, "/lk");
    /* buf size */
    ose_pushMessage(vm_le, "/bs", 3, 1,
                    OSETT_INT32, OSE_LINED_BUFSIZE);
    /* buf len -- current position */
    ose_pushMessage(vm_le, "/bl", 3, 1,
                    OSETT_INT32, 0);
    /* cursor pos */
    ose_pushMessage(vm_le, "/cp", 3, 1,
                    OSETT_INT32, 0);
    /* buf */
    ose_pushMessage(vm_le, "/bf", 3, 1,
                    OSETT_BLOB, OSE_LINED_BUFSIZE, NULL);
    /* prompt string */
    ose_pushMessage(vm_lo, "/ps", 3, 1,
                    OSETT_STRING, OSE_LINED_PROMPTSTRING);
    /* word break chars */
    ose_pushMessage(vm_lo, "/wb", 3, 1,
                    OSETT_STRING, OSE_LINED_WORDBREAKCHARS);
    /* history */
    /* ose_pushMessage(vm_lh, "/en", 3, 1, */
    /*                 OSETT_INT32, -1); */
    /* ose_pushMessage(vm_lh, "/lh", 3, 0); */
    ose_pushBundle(vm_lh);
    ose_pushMessage(vm_lh, "/en", 3, 2,
                    OSETT_INT32, 0, OSETT_INT32, -1);
    /* kill ring */
    ose_pushMessage(vm_lk, "/lk", 3, 0);

    ose_bundle vm_s = OSEVM_STACK(osevm);
    ose_pushBundle(vm_s);
    ose_pushMessage(vm_s, "/lined/char", strlen("/lined/char"), 1,
                    OSETT_ALIGNEDPTR, ose_lined_char);
    ose_push(vm_s);
    ose_pushMessage(vm_s, "/lined/format", strlen("/lined/format"), 1,
                    OSETT_ALIGNEDPTR, ose_lined_format);
    ose_push(vm_s);
    ose_pushMessage(vm_s, "/lined/print", strlen("/lined/print"), 1,
                    OSETT_ALIGNEDPTR, ose_lined_print);
    ose_push(vm_s);
    ose_pushMessage(vm_s, "/lined/prompt", strlen("/lined/prompt"), 1,
                    OSETT_ALIGNEDPTR, ose_lined_prompt);
    ose_push(vm_s);
    ose_pushMessage(vm_s, "/lined/init", strlen("/lined/init"), 1,
                    OSETT_ALIGNEDPTR, ose_lined_init);
    ose_push(vm_s);
    ose_pushMessage(vm_s,
                    "/lined/addtohist", strlen("/lined/addtohist"),
                    1, OSETT_ALIGNEDPTR, ose_lined_addToHist);
    ose_push(vm_s);

    /* empty bindings for C^c and RET */
    ose_pushMessage(vm_s, "/lined/binding/C^c",
                    strlen("/lined/binding/C^c"), 0);
    ose_pushBundle(vm_s);
    ose_push(vm_s);
    ose_push(vm_s);
    ose_pushMessage(vm_s, "/lined/binding/RET",
                    strlen("/lined/binding/RET"), 0);
    ose_pushBundle(vm_s);
    ose_push(vm_s);
    ose_push(vm_s);
    ose_pushMessage(vm_s, "/lined/NL", strlen("/lined/NL"), 1,
                    OSETT_STRING, "\n");
    ose_push(vm_s);
}
