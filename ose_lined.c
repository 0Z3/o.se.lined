/*
  Copyright (c) 2019-21 John MacCallum Permission is hereby granted,
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
/* raise */
#include <signal.h>

#include "ose_conf.h"
#include "ose.h"
#include "ose_context.h"
#include "ose_util.h"
#include "ose_stackops.h"
#include "ose_assert.h"
#include "ose_vm.h"
#include "sys/ose_term.h"
#include "ose_print.h"

#define OSE_LINED_BUFSIZE 4096

#define BUFSIZE_OFFSET (OSE_BUNDLE_HEADER_LEN + 12)
#define BUFLEN_OFFSET (BUFSIZE_OFFSET + 16)
#define CURPOS_OFFSET (BUFLEN_OFFSET + 16)
/* placeholder until we actually keep track of cols */
#define COLS_OFFSET CURPOS_OFFSET /* (CURPOS_OFFSET + 16) */
/* skips over the size of the blob */
#define BUF_OFFSET (COLS_OFFSET + 20)

#ifdef OSE_DEBUG
const int32_t bufsize_offset = BUFSIZE_OFFSET;
const int32_t buflen_offset = BUFLEN_OFFSET;
const int32_t curpos_offset = CURPOS_OFFSET;
const int32_t cols_offset = COLS_OFFSET;
const int32_t buf_offset = BUF_OFFSET;
#endif

#define CTRL(c) (c & 0x1f)
#define BS 8
#define LF 10
#define RET 13
#define SPC 32
#define DEL 127

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

static void ose_lined_read(ose_bundle osevm)
{
    ose_bundle vm_s = OSEVM_STACK(osevm);
    ose_bundle vm_i = OSEVM_INPUT(osevm);
    ose_drop(vm_s);             /* file descriptor */
    int32_t c = ose_termRead();
    ose_pushInt32(vm_s, c);
    ose_pushString(vm_i, "/!/lined/char");
}

static void ose_lined_char(ose_bundle osevm)
{
    ose_bundle vm_le = ose_enter(osevm, "/le");
    ose_bundle vm_s = OSEVM_STACK(osevm);
    /* ose_bundle vm_e = OSEVM_ENV(osevm); */
    ose_bundle vm_i = OSEVM_INPUT(osevm);
    ose_assert(ose_bundleHasAtLeastNElems(vm_s, 1) == OSETT_TRUE);
    ose_assert(ose_peekType(vm_s) == OSETT_MESSAGE);
    ose_assert(ose_peekMessageArgType(vm_s) == OSETT_INT32);
    int32_t c = ose_popInt32(vm_s);
    char *b = ose_getBundlePtr(vm_le);
    int32_t buflen = ose_readInt32(vm_le, BUFLEN_OFFSET);
    int32_t curpos = ose_readInt32(vm_le, CURPOS_OFFSET);
    switch(c)
    {
    case CTRL('a'):
        ose_writeInt32(vm_le, CURPOS_OFFSET, 0);
        pushline(osevm, b + BUF_OFFSET, buflen, buflen, 0);
        break;
    case CTRL('b'):
        deccurpos(vm_le);
        pushline(osevm, b + BUF_OFFSET, buflen, buflen, curpos - 1);
        break;
    case CTRL('c'):
        raise(SIGINT);
        break;
    case CTRL('e'):
        ose_writeInt32(vm_le, CURPOS_OFFSET, buflen);
        pushline(osevm, b + BUF_OFFSET, buflen, buflen, buflen);
        break;
    case CTRL('f'):
        inccurpos(vm_le);
        pushline(osevm, b + BUF_OFFSET, buflen, buflen, curpos + 1);
        break;
    case LF:
    case RET:
        ose_pushString(vm_s, b + BUF_OFFSET);
        clear(vm_le);
        ose_pushString(vm_i, "/!/lined/line");
        break;
    case BS:
    case DEL:
        delchar(vm_le);
        pushline(osevm, b + BUF_OFFSET,
                 buflen, buflen - 1, curpos - 1);
        break;
    default:
        addchar(vm_le, c);
        pushline(osevm, b + BUF_OFFSET,
                 buflen, buflen + 1, curpos + 1);
        break;
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
    int32_t oldlen = ose_popInt32(vm_s);
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
    ose_bundle vm_le = ose_enter(osevm, "/le");
    addchar(vm_le, '/');
    addchar(vm_le, ' ');
}

void ose_main(ose_bundle osevm)
{
    ose_pushContextMessage(osevm, 8192, "/le");
    ose_bundle vm_le = ose_enter(osevm, "/le");
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
    /* add history support */

    ose_bundle vm_s = OSEVM_STACK(osevm);
    ose_pushBundle(vm_s);
    ose_pushMessage(vm_s, "/lined/read", strlen("/lined/read"), 1,
                    OSETT_ALIGNEDPTR, ose_lined_read);
    ose_push(vm_s);
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
    ose_pushMessage(vm_s, "/lined/parse", strlen("/lined/parse"), 1,
                    OSETT_BLOB,
                    OSE_BUNDLE_HEADER_LEN,
                    OSE_BUNDLE_HEADER);
    ose_push(vm_s);
    {
        ose_pushMessage(vm_s, "/lined/line", strlen("/lined/line"), 0);
        ose_pushString(vm_s, "/!/lined/parse");
        ose_pushString(vm_s, "/>/_e");
        ose_pushString(vm_s, "/!/swap");
        ose_pushString(vm_s, "/!/exec");
        ose_pushString(vm_s, "/</_e");
        /* ose_pushString(vm_s, "/!/bundle/all"); */
        ose_pushString(vm_s, "/!/lined/format");
        /* ose_pushString(vm_s, "/!/push"); */
        /* ose_pushString(vm_s, "/!/unpack/drop"); */
        ose_pushInt32(vm_s, 0);
        ose_pushInt32(vm_s, 0);
        ose_pushInt32(vm_s, 0);
        ose_pushInt32(vm_s, 9);
        ose_bundleFromTop(vm_s);
        ose_push(vm_s);
        ose_push(vm_s);
    }
}
