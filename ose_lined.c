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

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_WHITE   "\x1b[37m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#define ANSI_COLOR_BRIGHT_RED     "\x1b[91m"
#define ANSI_COLOR_BRIGHT_GREEN   "\x1b[92m"
#define ANSI_COLOR_BRIGHT_YELLOW  "\x1b[93m"
#define ANSI_COLOR_BRIGHT_BLUE    "\x1b[94m"
#define ANSI_COLOR_BRIGHT_MAGENTA "\x1b[95m"
#define ANSI_COLOR_BRIGHT_CYAN    "\x1b[96m"
#define ANSI_COLOR_BRIGHT_WHITE   "\x1b[97m"
#define ANSI_COLOR_BRIGHT_RESET   "\x1b[0m"

#define OSE_LINED_BUFSIZE 4096

#define BUFSIZE_OFFSET (OSE_BUNDLE_HEADER_LEN + 12)
#define BUFLEN_OFFSET (BUFSIZE_OFFSET + 16)
#define CURPOS_OFFSET (BUFLEN_OFFSET + 16)
/* placeholder until we actually keep track of cols */
#define COLS_OFFSET CURPOS_OFFSET /* (CURPOS_OFFSET + 16) */
/* skips over the size of the blob */
#define BUF_OFFSET (COLS_OFFSET + 20)
#define PROMPTSTRING_OFFSET (BUF_OFFSET + OSE_LINED_BUFSIZE + 12)

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

static void ose_lined_char(ose_bundle osevm)
{
    ose_bundle vm_le = ose_enter(osevm, "/le");
    ose_bundle vm_s = OSEVM_STACK(osevm);
    ose_bundle vm_c = OSEVM_CONTROL(osevm);
    /* ose_bundle vm_e = OSEVM_ENV(osevm); */
    ose_bundle vm_i = OSEVM_INPUT(osevm);
    ose_assert(ose_bundleHasAtLeastNElems(vm_s, 1) == OSETT_TRUE);
    ose_assert(ose_peekType(vm_s) == OSETT_MESSAGE);
    ose_assert(ose_peekMessageArgType(vm_s) == OSETT_INT32);
    /* int32_t c = ose_popInt32(vm_s); */
    const char * const b = ose_getBundlePtr(vm_le);
    const char * const bufp = b + BUF_OFFSET;
    int32_t buflen = ose_readInt32(vm_le, BUFLEN_OFFSET);
    int32_t curpos = ose_readInt32(vm_le, CURPOS_OFFSET);
    int32_t promptlen = strlen(b + PROMPTSTRING_OFFSET);


    int32_t numchars = 0;
    if(ose_bundleHasAtLeastNElems(vm_s, 2) == OSETT_TRUE
       && ose_peekType(vm_s) == OSETT_MESSAGE
       && ose_peekMessageArgType(vm_s) == OSETT_INT32)
    {
        numchars = ose_popInt32(vm_s);
    }
    if(numchars == 0)
    {
        return;
    }
    ose_assert(ose_bundleHasAtLeastNElems(vm_s, numchars) == OSETT_TRUE);

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
            int32_t o = strlen(b + PROMPTSTRING_OFFSET);
            ose_writeInt32(vm_le, CURPOS_OFFSET, o);
            pushline(osevm, b + BUF_OFFSET, buflen, buflen, o);
        }
        break;
        case CTRL('b'):
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
            /* raise(SIGINT); */
            ose_pushString(vm_c, "/!/lined/binding/C^c");
            ose_swap(vm_c);
            pushline(osevm, bufp, buflen, buflen, curpos);
            break;
        case CTRL('d'):
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
            break;
        case ESC:
            if(i < numchars
               && ose_peekType(vm_s) == OSETT_MESSAGE
               && ose_peekMessageArgType(vm_s) == OSETT_INT32)
            {
                char ec = (char)ose_popInt32(vm_s);
                ++i;
                switch(ec)
                {
                case 'b':
                {
                    if(bufp[curpos - 1] == '/')
                    {
                        deccurpos(vm_le);
                        --curpos;
                    }
                    while(bufp[curpos - 1] != '/'
                          && curpos > promptlen)
                    {
                        deccurpos(vm_le);
                        --curpos;
                    }
                    pushline(osevm, bufp, buflen, buflen,
                             curpos);
                
                }
                break;
                case 'f':
                {
                    while(bufp[curpos] != '/'
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
                    int32_t i = 0;
                    if(bufp[curpos - 1] == '/')
                    {
                        delchar(vm_le);
                        --curpos;
                        ++i;
                    }
                    while(bufp[curpos - 1] != '/'
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
    const char * const b = ose_getBundlePtr(vm_le);
    const char * const promptstring = b + PROMPTSTRING_OFFSET;
    {
        int i = 0;
        for(; i < strlen(promptstring); i++)
        {
            addchar(vm_le, promptstring[i]);
        }
    }
    ose_pushString(vm_s, b + BUF_OFFSET);
    ose_push(vm_s);
    ose_concatenateStrings(vm_s);
}

static void ose_lined_init(ose_bundle osevm)
{
    ose_bundle vm_s = OSEVM_STACK(osevm);
    ose_pushString(vm_s, "");
    ose_lined_prompt(osevm);
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
    /* prompt string */
    ose_pushMessage(vm_le, "/ps", 3, 1,
                    OSETT_STRING, OSE_LINED_PROMPTSTRING);
    /* add history support */

    ose_bundle vm_s = OSEVM_STACK(osevm);
    ose_pushBundle(vm_s);
    /* ose_pushMessage(vm_s, "/lined/read", strlen("/lined/read"), 1, */
    /*                 OSETT_ALIGNEDPTR, ose_lined_read); */
    /* ose_push(vm_s); */
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
    ose_pushMessage(vm_s, "/lined/parse", strlen("/lined/parse"), 1,
                    OSETT_BLOB,
                    OSE_BUNDLE_HEADER_LEN,
                    OSE_BUNDLE_HEADER);
    ose_push(vm_s);
    {
        ose_pushMessage(vm_s, "/lined/line",
                        strlen("/lined/line"), 0);
        /* the input string is sitting on top of the stack.  we test
           it against the prompt, and re-prompt if it's the same */
        /* make a copy of the input string for the test */
        ose_pushString(vm_s, "/!/dup");
        {
            /* else clause of the if statement.  this means the
               input string contains more than just the prompt */
            
            /* store command that was entered */
            ose_pushString(vm_s, "/!/dup");
            ose_pushString(vm_s, "/>/le");
            ose_pushString(vm_s, "/!/swap");
            ose_pushString(vm_s, "/s//cmd");
            ose_pushString(vm_s, "/!/assign");
            ose_pushString(vm_s, "/</le");
            /* remove prompt */
            ose_pushInt32(vm_s, 2);
            ose_pushString(vm_s, "/!/decat/string/fromstart");
            ose_pushString(vm_s, "/!/pop");
            ose_pushString(vm_s, "/!/nip");
            /* parse */
            ose_pushString(vm_s, "/!/lined/parse");
            ose_pushString(vm_s, "/>/_e");
            ose_pushString(vm_s, "/!/swap");
            ose_pushString(vm_s, "/!/exec");
            ose_pushString(vm_s, "/</_e");
            ose_pushString(vm_s, "/!/bundle/all");
            ose_pushString(vm_s, "/!/lined/format");
            {
                /* put the command at the beginning of the 
                   output string, and add a newline at the 
                   end of the formatted text */
                ose_pushString(vm_s, "/>/le");
                ose_pushString(vm_s, "/s//cmd");
                ose_pushString(vm_s, "/!/lookup");
                ose_pushString(vm_s, "/!/nip");

                ose_pushString(vm_s, "/s/"ANSI_COLOR_BRIGHT_GREEN);
                ose_pushString(vm_s, "/!/swap");
                ose_pushString(vm_s, "/!/push");
                ose_pushString(vm_s, "/!/concat/strings");
                
                ose_pushString(vm_s, "/s/"ANSI_COLOR_RESET"\n");
                ose_pushString(vm_s, "/!/push");
                ose_pushString(vm_s, "/!/concat/strings");
                ose_pushString(vm_s, "/!/swap");
                ose_pushString(vm_s, "/!/push");
                ose_pushString(vm_s, "/!/concat/strings");
                ose_pushString(vm_s, "/s/\n");
                ose_pushString(vm_s, "/!/push");
                ose_pushString(vm_s, "/!/concat/strings");
            }
            ose_pushString(vm_s, "/!/push");
            ose_pushString(vm_s, "/!/unpack/drop");
            ose_pushString(vm_s, "/!/lined/prompt");
            ose_pushInt32(vm_s, 0);
            /* ose_pushInt32(vm_s, 2); */
            /* ose_pushInt32(vm_s, 2); */
            /* use the length of the prompt as curpos and bufpos */
            ose_pushString(vm_s, "/>/le");
            ose_pushString(vm_s, "/ps");
            ose_pushString(vm_s, "/!/lookup");
            ose_pushString(vm_s, "/!/nip");
            ose_pushString(vm_s, "/!/length/item");
            ose_pushString(vm_s, "/!/nip");
            ose_pushString(vm_s, "/!/dup");
            ose_pushInt32(vm_s, 45);
            ose_bundleFromTop(vm_s);
        }
        {
            /* then clause of the if statement. this means the input
               string is the same as the prompt, so we just reset
               the prompt */
            ose_pushString(vm_s, "/!/drop");
            ose_pushString(vm_s, "/s/");
            ose_pushString(vm_s, "/!/lined/prompt");
            ose_pushInt32(vm_s, 0);
            /* ose_pushInt32(vm_s, 2); */
            /* ose_pushInt32(vm_s, 2); */
            /* use the length of the prompt as curpos and bufpos */
            ose_pushString(vm_s, "/>/le");
            ose_pushString(vm_s, "/ps");
            ose_pushString(vm_s, "/!/lookup");
            ose_pushString(vm_s, "/!/nip");
            ose_pushString(vm_s, "/!/length/item");
            ose_pushString(vm_s, "/!/nip");
            ose_pushString(vm_s, "/!/dup");
            ose_pushInt32(vm_s, 11);
            ose_bundleFromTop(vm_s);
        }
        /* get the copy of the input string for the test */
        ose_pushString(vm_s, "/!/rot");
        /* get a copy of the prompt */
        ose_pushString(vm_s, "/>/le");
        ose_pushString(vm_s, "/s//ps");
        ose_pushString(vm_s, "/!/lookup");
        ose_pushString(vm_s, "/!/nip");
        ose_pushString(vm_s, "/!/pop/all/drop");
        /* test */
        ose_pushString(vm_s, "/!/eql");
        /* now run the if statement */
        ose_pushString(vm_s, "/!/if");
        /* if uses exec which leaves a copy of env on the stack */
        ose_pushString(vm_s, "/</_e");
        ose_pushInt32(vm_s, 12);
        ose_bundleFromTop(vm_s);
        ose_push(vm_s);
    }
    ose_push(vm_s);

    ose_pushMessage(vm_s, "/lined/binding/C^c",
                    strlen("/lined/binding/C^c"), 0);
    ose_pushBundle(vm_s);
    ose_push(vm_s);
    ose_push(vm_s);
}
