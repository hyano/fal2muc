/*
 * fal2muc: decompiler from Falcom Sound Data to MUCOM88 MML
 *
 * Copyright (c) 2019 Hirokuni Yano
 *
 * Released under the MIT license.
 * see https://opensource.org/licenses/MIT
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

/* use macro instead of expanding envelope command. */
#define USE_SSG_ENV_MACRO

/* combine long length tones. */
#define COMBINE_LONG_TONE

/* combine long length rests. doesn't work due to MUCOM88 bug. */
#undef COMBINE_LONG_REST

/* global option(s) */
bool g_opt_verbose = false;
bool g_opt_ignore_warning = false;

#define BUFF_SIZE (0x10000)
uint8_t g_data[BUFF_SIZE];
uint8_t g_loop_flag[BUFF_SIZE];
uint8_t g_loop_nest[BUFF_SIZE];
uint32_t g_ssg_tempo_prev = UINT32_MAX;
uint32_t g_ssg_tempo_count = 0;

typedef enum
{
    DRIVER_TYPE_UNKNOWN,
    DRIVER_TYPE_OPN,
    DRIVER_TYPE_OPNA,
    DRIVER_TYPE_OPNA_VA,
    DRIVER_TYPE_OPNA_MONO,
    DRIVER_TYPE_X1_OPM,
    DRIVER_TYPE_X1_PSG,
} DRIVER_TYPE;

typedef enum
{
    SOUND_TYPE_NONE		= 0x0000,
    SOUND_TYPE_FM		= 0x0001,
    SOUND_TYPE_SSG		= 0x0002,
    SOUND_TYPE_STEREO	= 0x0004,
    SOUND_TYPE_OPM		= 0x0008,
    SOUND_TYPE_DUMMY	= 0x8000,
} SOUND_TYPE;

#ifdef USE_SSG_ENV_MACRO
const char g_ssg_inst[] = 
"# *0{E$ff,$ff,$ff,$ff,$00,$ff}\n"
"# *1{E$ff,$ff,$ff,$c8,$00,$0a}\n"
"# *2{E$ff,$ff,$ff,$c8,$01,$0a}\n"
"# *3{E$ff,$ff,$ff,$be,$00,$0a}\n"
"# *4{E$ff,$ff,$ff,$be,$01,$0a}\n"
"# *5{E$ff,$ff,$ff,$be,$04,$0a}\n"
"# *6{E$ff,$ff,$ff,$be,$0a,$0a}\n"
"# *7{E$ff,$ff,$ff,$01,$ff,$ff}\n"
"# *8{E$ff,$ff,$ff,$ff,$01,$0a}\n"
"# *9{E$64,$64,$ff,$ff,$01,$0a}\n"
"# *10{E$28,$02,$ff,$f0,$00,$0a}\n"
"# *11{E$ff,$ff,$ff,$c8,$01,$28}\n"
"";
#else /* USE_SSG_ENV_MACRO */
const uint8_t g_ssg_env[12][6] =
{
    {0xff, 0xff, 0xff, 0xff, 0x00, 0xff},
    {0xff, 0xff, 0xff, 0xc8, 0x00, 0x0a},
    {0xff, 0xff, 0xff, 0xc8, 0x01, 0x0a},
    {0xff, 0xff, 0xff, 0xbe, 0x00, 0x0a},
    {0xff, 0xff, 0xff, 0xbe, 0x01, 0x0a},
    {0xff, 0xff, 0xff, 0xbe, 0x04, 0x0a},
    {0xff, 0xff, 0xff, 0xbe, 0x0a, 0x0a},
    {0xff, 0xff, 0xff, 0x01, 0xff, 0xff},
    {0xff, 0xff, 0xff, 0xff, 0x01, 0x0a},
    {0x64, 0x64, 0xff, 0xff, 0x01, 0x0a},
    {0x28, 0x02, 0xff, 0xf0, 0x00, 0x0a},
    {0xff, 0xff, 0xff, 0xc8, 0x01, 0x28},
};
#endif /* USE_SSG_ENV_MACRO */

int DBG(const char *format, ...)
{
    va_list va;
    int ret = 0;

    va_start(va, format);
    if (g_opt_verbose)
    {
        ret = vprintf(format, va);
    }
    va_end(va);

    return ret;
}

int WARN(const char *format, ...)
{
    va_list va;
    int ret = 0;

    va_start(va, format);
    if (g_opt_verbose || !g_opt_ignore_warning)
    {
        ret = vprintf(format, va);
    }
    va_end(va);

    if (!g_opt_ignore_warning)
    {
        fprintf(stderr, "exit with warning. try -w option to apply workaround.\n");
        exit(1);
    }

    return ret;
}

uint32_t get_word(const uint8_t *p)
{
    return (uint32_t)p[0] + ((uint32_t)p[1] << 8);
}

void dump_inst(FILE *fp, uint32_t num, uint8_t *data, uint32_t offset)
{
    const unsigned char *d = data;
    uint32_t o = offset;

    fprintf(fp, "  @%%%03u\n", num);
    fprintf(fp, "  $%03X,$%03X,$%03X,$%03X\n", d[o], d[o+1], d[o+2], d[o+3]); o += 4; // DT/ML
    fprintf(fp, "  $%03X,$%03X,$%03X,$%03X\n", d[o], d[o+1], d[o+2], d[o+3]); o += 4; // TL
    fprintf(fp, "  $%03X,$%03X,$%03X,$%03X\n", d[o], d[o+1], d[o+2], d[o+3]); o += 4; // KS/AR
    fprintf(fp, "  $%03X,$%03X,$%03X,$%03X\n", d[o], d[o+1], d[o+2], d[o+3]); o += 4; // DR
    fprintf(fp, "  $%03X,$%03X,$%03X,$%03X\n", d[o], d[o+1], d[o+2], d[o+3]); o += 4; // SR
    fprintf(fp, "  $%03X,$%03X,$%03X,$%03X\n", d[o], d[o+1], d[o+2], d[o+3]); o += 4; // SL/RR
    fprintf(fp, "  $%03X\n"   , d[o]); // FB/AL
    fprintf(fp, "\n");
}

void convert_inst(FILE *fp, unsigned char *data, uint32_t offset)
{
    uint32_t i;
    uint32_t n = (get_word(data) - offset) / 0x0020;

    for (i = 0; i < n; i++)
    {
        dump_inst(fp, i, data, offset + i * 0x20);
    }
}

void detect_clock(const uint32_t len_count[256], uint32_t *clock, uint32_t *deflen)
{
    const struct {
        uint32_t clock;
        uint32_t count;
    } count_table[] = {
        { 192, len_count[192] + len_count[96] + len_count[48] + len_count[24] + len_count[12] + len_count[6] + len_count[3]},
        { 144, len_count[144] + len_count[72] + len_count[36] + len_count[18] + len_count[ 9]},
        { 128, len_count[128] + len_count[64] + len_count[32] + len_count[16] + len_count[ 8] + len_count[4] + len_count[2]},
        { 112, len_count[112] + len_count[56] + len_count[28] + len_count[14] + len_count[7]},
        { 0, 0},
    };
    uint32_t c;
    uint32_t l;

    {
        DBG("----------------\n");
        for (uint32_t i = 0; count_table[i].clock != 0; i++)
        {
            DBG("%3d: %4d\n", count_table[i].clock, count_table[i].count);
        }
        DBG("--------\n");
        for (uint32_t i = 0; i < 20; i++)
        {
            DBG("%3d:", i*10);
            for (uint32_t j = 0; j < 10; j++)
            {
                DBG(" %4d", len_count[i * 10 + j]);
            }
            DBG("\n");
        }
        DBG("----------------\n");
    }

    c = 0;
    for (uint32_t i = 1; count_table[i].clock != 0; i++)
    {
        if (count_table[i].count > count_table[c].count)
        {
            c = i;
        }
    }

    l = 1;
    for (uint32_t i = 1; i < 7; i++)
    {
        if (len_count[count_table[c].clock / (1 << i)] > len_count[count_table[c].clock / l])
        {
            l = 1 << i;
        }
    }

    *clock = count_table[c].clock;
    *deflen = l;
}

void parse_music(
    const uint8_t *data, uint32_t offset, uint8_t *loop_flag, uint8_t *loop_nest,
    uint32_t *end, uint32_t *clock, uint32_t *deflen)
{
    const uint8_t *d = data;
    uint32_t o = offset;
    uint32_t c;
    uint32_t w;
    uint32_t len;
    bool quit = false;
    uint32_t len_count[256];

    memset(len_count, 0, sizeof(len_count));

    while (!quit)
    {
        c = d[o++];
        if (c >= 0xf0)
        {
            switch (c)
            {
            case 0xfb:
            case 0xfc:
                break;
            case 0xf0:
            case 0xf1:
            case 0xf2:
            case 0xf3:
            case 0xf4:
            case 0xf5:
            case 0xfe:
                o++;
                break;
            case 0xf8:
            case 0xfa:
            case 0xfd:
                o += 2;
                break;
            case 0xf7:
                o += 5;
                break;
            case 0xf9:
                o += 6;
                break;
            case 0xf6:
                o++;
                o++;
                w = get_word(&d[o]);
                o += 2;
                loop_nest[o - w]++;
                break;
            case 0xff:
                w = get_word(&d[o]);
                o += 2;
                if (w != 0)
                {
                    loop_flag[o - w] = 1;
                }
                quit = true;
                break;
            }

        }
        else if (c >= 0x80)
        {
            len = c & 0x7f;
#ifdef COMBINE_LONG_REST
            if ((len == 0x6f)							// length
                && (d[o] < 0xf0 && d[o] >= 0x80)		// next command
                )
            {
                len += d[o] & 0x7f;
                o++;
            }
#endif /* COMBINE_LONG_REST */
            len_count[len]++;
        }
        else
        {
            len = c;
#ifdef COMBINE_LONG_TONE
            if ((len == 0x6f)							// length
                && (d[o] & 0x80)						// &
                && (d[o + 1] < 0x80)					// next command
                && ((d[o + 2] & 0x7f) == (d[o] & 0x7f))	// next note
                )
            {
                len += d[o + 1];
                o += 2;
            }
#endif /* COMBINE_LONG_TONE */
            len_count[len]++;
            o++;
        }
    }

    *end = o;

    detect_clock(len_count, clock, deflen);
}

int print_length(FILE *fp, uint32_t clock, uint32_t deflen, uint32_t len)
{
    int ret = 0;

    if (clock % len == 0)
    {
        if (clock / len == deflen)
        {
            /* nothing */
        }
        else
        {
            ret += fprintf(fp, "%u", clock / len);
        }
    }
    else if ((len % 3 == 0) && (clock % (len / 3 * 2) == 0))
    {
        if (clock / (len / 3 * 2) == deflen)
        {
            ret += fprintf(fp, ".");
        }
        else
        {
            ret += fprintf(fp, "%u.", clock / (len / 3 * 2));
        }
    }
    else
    {
        ret += fprintf(fp, "%%%u", len);
    }
    return ret;
}

void convert_music(FILE *fp, uint32_t ch, SOUND_TYPE sound_type, const char *chname,
                   const uint8_t *data, uint8_t *loop_flag, uint8_t *loop_nest)
{
    static const char *notestr[16] = {
        "c", "c+", "d", "d+", "e", "f", "f+", "g", "g+", "a", "a+", "b",
        "?", "?", "?", "?"
    };
    static const uint8_t x1_illegal_note[] = {
        0x4e, 0x0b, 0x0e, 0x0b, 0x36, 0x0b, 0x08, 0x09,
        0x41, 0x09, 0x21, 0x09, 0x08, 0x08, 0x57, 0x08,
        0x4b, 0x08, 0x47, 0x06, 0x47, 0x06, 0x4d, 0x06,
        0x20, 0x1e, 0x1d, 0x1a, 0x18, 0x17, 0x14, 0x12,
    };
    const uint8_t *d = data;
    uint32_t o = get_word(&data[ch * 2]);
    uint32_t end;
    uint32_t c;
    uint32_t prev_oct, oct, note, len;
    uint32_t ssg_mixer;
    uint32_t ssg_noise;
    uint32_t nest;
    uint32_t clock, deflen;
    uint32_t timerb_on_ssg = UINT32_MAX;
    bool init = false;
    bool quit = false;
    int ll;

#define DUMMY(x) if (sound_type & SOUND_TYPE_DUMMY) { o += x; break; }

    parse_music(data, o, loop_flag, loop_nest, &end, &clock, &deflen);

    ll = 0;
    prev_oct = 0xff;
    ssg_mixer = 0x02;
    ssg_noise = 0xff;

    while (!quit)
    {
        if (ll <= 0)
        {
            fprintf(fp, "\n");
            ll = 70;
            ll -= fprintf(fp, "%s ", chname);
            if (!init)
            {
                ll -= fprintf(fp, "C%ul%u", clock, deflen);
                init = true;
            }
        }

        if (loop_flag[o] || loop_nest[o])
        {
            ssg_mixer = 0xff;
            ssg_noise = 0xff;
        }
        if (loop_flag[o])
        {
            ll -= fprintf(fp, " L ");
        }
        for (nest = 0; nest < loop_nest[o]; nest++)
        {
            ll -= fprintf(fp, "[");
        }
        if (loop_nest[o])
        {
            DBG("{%04x}", o);
        }

        c = d[o++];
        if (c >= 0xf0)
        {
            switch (c)
            {
            case 0xf0:
                if (sound_type & SOUND_TYPE_FM)
                {
                    ll -= fprintf(fp, "@%u", (uint32_t)d[o++]);
                }
                else if (sound_type & SOUND_TYPE_SSG)
                {
#ifdef USE_SSG_ENV_MACRO
                    ll -= fprintf(fp, "*%u", (uint32_t)d[o++]);
#else /* USE_SSG_ENV_MACRO */
                    c = d[o++];
                    ll -= fprintf(fp, "E%d,%d,%d,%d,%d,%d",
                                  g_ssg_env[c][0], g_ssg_env[c][1], g_ssg_env[c][2],
                                  g_ssg_env[c][3], g_ssg_env[c][4], g_ssg_env[c][5]);
#endif /* USE_SSG_ENV_MACRO */
                }
                else
                {
                    o++;
                }
                break;
            case 0xf1: DUMMY(1);
                ll -= fprintf(fp, "v%d", d[o++]);
                break;
            case 0xf2: DUMMY(1);
                ll -= fprintf(fp, "q%d", d[o++]);
                break;
            case 0xf3: DUMMY(1);
                ll -= fprintf(fp, "D%d", (char)d[o++]);
                break;
            case 0xf4: DUMMY(1);
                if (sound_type & SOUND_TYPE_FM)
                {
                    /* not supported in MUCOM88 */
                    ll -= fprintf(fp, "??@v%d", d[o++]);
                }
                else if (sound_type & SOUND_TYPE_SSG)
                {
                    c = d[o++];
                    if ((c>>6) != ssg_mixer)
                    {
                        ssg_mixer = c >> 6;
                        ll -= fprintf(fp, "P%u", ssg_mixer ^ 3);
                    }
                    if ((c&0x1f) != ssg_noise)
                    {
                        ssg_noise = c & 0x1f;
                        ll -= fprintf(fp, "w%u", ssg_noise);
                    }
                }
                else
                {
                    o++;
                }
                break;
            case 0xf5:
                ll -= fprintf(fp, "t%u", (uint32_t)d[o]);
                if (sound_type & SOUND_TYPE_SSG)
                {
                    if (g_ssg_tempo_prev == UINT32_MAX)
                    {
                        g_ssg_tempo_prev = (uint32_t)d[o];
                    }
                    else if (g_ssg_tempo_prev != (uint32_t)d[o])
                    {
                        g_ssg_tempo_prev = (uint32_t)d[o];
                        g_ssg_tempo_count++;
                    }
                    DBG("{%04x}", o - 2);
                }
                o++;
                break;
            case 0xf6:
                DBG("{%04x:%04x}", o - 1, o + 4 - get_word(&d[o + 2]));
                ll -= fprintf(fp, "]%d", d[o++]);
                ssg_mixer = 0xff;
                ssg_noise = 0xff;
                o++;
                o += 2;
                break;
            case 0xf7: DUMMY(5);
                ll -= fprintf(fp, "M%u,%u,%d,%u",
                              (uint32_t)d[o], (uint32_t)d[o + 1],
                              (int16_t)get_word(&d[o + 2]), (uint32_t)d[o + 4]);
                o += 5;
                break;
            case 0xf8: DUMMY(2);
                if (d[o] == 0x10)
                {
                    ll -= fprintf(fp, "MF%d", (d[o + 1] == 0) ? 0 : 1);
                }
                else
                {
                    /* not supported in MUCOM88 */
                    ll -= fprintf(fp, "??work");
                }
                o += 2;
                break;
            case 0xf9: DUMMY(6);
                ll -= fprintf(fp, "E%u,%u,%u,%u,%u,%u",
                              (uint32_t)d[o + 0], (uint32_t)d[o + 1], (uint32_t)d[o + 2],
                              (uint32_t)d[o + 3], (uint32_t)d[o + 4], (uint32_t)d[o + 5]);
                if (sound_type & SOUND_TYPE_FM)
                {
                    DBG("{%04x}", o - 1);
                }
                o += 6;
                break;
            case 0xfa: DUMMY(2);
                ll -= fprintf(fp, "y%u,%u", (uint32_t)d[o], (uint32_t)d[o + 1]);
                if (sound_type & SOUND_TYPE_SSG)
                {
                    DBG("{%04x}", o - 1);
                }
                o += 2;
                break;
            case 0xfb: DUMMY(0);
                /* not compatible with MUCOM88 */
                ll -= fprintf(fp, "(");
                break;
            case 0xfc: DUMMY(0);
                /* not compatible with MUCOM88 */
                ll -= fprintf(fp, ")");
                break;
            case 0xfd:
                if (o + 2 + (int)get_word(&d[o]) >= end)
                {
                    /* workaround */
                    /*  [PC-8801] Eiyu Densetsu II / MUS002 */
                    /*  [PC-8801] DINOSAUR / 049 */
                    WARN("\nDetect wrong '/' command @ %04x\n", o - 1);
                }
                else
                {
                    ll -= fprintf(fp, "/");
                    DBG("{%04x:%04x}", o - 1, o + 2 + get_word(&d[o]));
                }
                o += 2;
                break;
            case 0xfe: DUMMY(1);
                if (sound_type & SOUND_TYPE_STEREO)
                {
                    ll -= fprintf(fp, "p%u", (uint32_t)(d[o] >> 6));
                }
                o++;
                break;
            case 0xff:
                quit = true;
                break;
            }

        }
        else if (c >= 0x80)
        {
            len = c & 0x7f;
#ifdef COMBINE_LONG_REST
            if ((len == 0x6f)							// length
                && (d[o] < 0xf0 && d[o] >= 0x80)		// next command
                && (!loop_flag[o] && !loop_nest[o])
                )
            {
                len += d[o] & 0x7f;
                o++;
            }
#endif /* COMBINE_LONG_REST */
            ll -= fprintf(fp, "r");
            ll -= print_length(fp, clock, deflen, len);
        }
        else if (sound_type & SOUND_TYPE_DUMMY)
        {
            len = c;
#ifdef COMBINE_LONG_REST
            if ((len == 0x6f)							// length
                && (d[o] & 0x80)						// &
                && (d[o + 1] < 0x80)					// next command
                && ((d[o + 2] & 0x7f) == (d[o] & 0x7f))	// next note
                && (!loop_flag[o + 1] && !loop_nest[o + 1])
                )
            {
                len += d[o+1];
                o += 2;
            }
#endif /* COMBINE_LONG_TONE */
            ll -= fprintf(fp, "|r"); /* '|' is workaround for MUCOM88 bug */
            ll -= print_length(fp, clock, deflen, len);
            o++;
        }
        else
        {
            len = c;
#ifdef COMBINE_LONG_TONE
            if ((len == 0x6f)							// length
                && (d[o] & 0x80)						// &
                && (d[o + 1] < 0x80)					// next command
                && ((d[o + 2] & 0x7f) == (d[o] & 0x7f))	// next note
                && (!loop_flag[o + 1] && !loop_nest[o + 1])
                )
            {
                len += d[o+1];
                o += 2;
            }
#endif /* COMBINE_LONG_TONE */
            if (!(sound_type & SOUND_TYPE_OPM))
            {
                oct = ((d[o] >> 4) & 0x07) + 1;
                note = d[o] & 0x0f;
            }
            else
            {
                c = d[o] & 0x7f;
#if 0
                if (d[o] == 0xfc)
                {
                    /* workaround for [X1] SORCERIAN / SS086 */
                    c = 0xdc;
                }
#endif
                if (c >= 0x60)
                {
                    WARN("\nDetect too high tone %02x @ %04x\n", c, o - 1);
                    c = x1_illegal_note[(c & 0x7f) - 0x60];
                }
                oct = (c + 15) / 12;
                note = (c + 15) % 12;
            }
            if (oct != prev_oct)
            {
                if (oct == prev_oct + 1)
                {
                    ll -= fprintf(fp, ">");
                }
                else if (oct == prev_oct - 1)
                {
                    ll -= fprintf(fp, "<");
                }
                else
                {
                    ll -= fprintf(fp, "o%u", oct);
                }
                prev_oct = oct;
            }
            ll -= fprintf(fp, "%s", notestr[note]);
            ll -= print_length(fp, clock, deflen, len);
            if (d[o] & 0x80)
            {
                ll -= fprintf(fp, "&");
            }
            o++;
        }
    }

    fprintf(fp, "\n");

    if (timerb_on_ssg != UINT32_MAX)
    {
        DBG("set Timer-B on ch.A\n");
        fprintf(fp, "A C192t%u\n", timerb_on_ssg);
    }

}

DRIVER_TYPE detect_driver_type(unsigned char *data)
{
    DRIVER_TYPE ret = DRIVER_TYPE_OPN;
    uint32_t ch9;

    if ((get_word(data) / 16) % 2 == 0)
    {
        ch9 = get_word(&data[0x0012]);
        if (ch9 == 0)
        {
            if (get_word(&data[0x001a]) == 0)
            {
                ret = DRIVER_TYPE_OPNA_VA;
            }
            else
            {
                ret = DRIVER_TYPE_X1_OPM;
            }
        }
        else
        {
            if (data[ch9] == 0xff)
            {
                ret = DRIVER_TYPE_OPNA;
            }
            else
            {
                fprintf(stderr, "Unknown driver type: ch9:%04x [%02x %02x %02x %02x]\n",
                        ch9, data[ch9 + 0], data[ch9 + 3], data[ch9 + 2], data[ch9 + 3]);
                ret = DRIVER_TYPE_UNKNOWN;
            }
        }
    }

    return ret;
}

void help(void)
{
    fprintf(stderr, "Usage: fal2muc [option(s)] file\n");
    fprintf(stderr, "  -h\t\tprint this help message and exit\n");
    fprintf(stderr, "  -v\t\tverbose (debug info)\n");
    fprintf(stderr, "  -w\t\tapply workaround and ignore warnings\n");
    fprintf(stderr, "  -o FILE\toutput file (default: stdout)\n");
    fprintf(stderr, "  -m VERSION\tMUCOM88 version\n");
    fprintf(stderr, "  -t TITLE\ttitle for tag\n");
    fprintf(stderr, "  -a AUTHOR\tauthor for tag\n");
    fprintf(stderr, "  -c COMPOSER\tcomposer for tag\n");
    fprintf(stderr, "  -d DATE\tdate for tag\n");
    fprintf(stderr, "  -C COMMENT\tcomment for tag\n");
    fprintf(stderr, "  -F FORMAT\tfile format (default: auto detect)\n");
    fprintf(stderr, "\t\t          Data          / Playback\n");
    fprintf(stderr, "\t\t  opn   = OPN           / OPN\n");
    fprintf(stderr, "\t\t  opna  = OPNA          / OPNA\n");
    fprintf(stderr, "\t\t  va    = OPNA(PC-88VA) / OPNA\n");
    fprintf(stderr, "\t\t  mono  = OPNA          / OPN\n");
    fprintf(stderr, "\t\t  x1opm = OPM+PSG(X1)   / OPNA\n");
    fprintf(stderr, "\t\t  x1psg = PSG(X1)       / OPN\n");
    exit(1);
}

int main(int argc, char *argv[])
{
    int c;
    FILE *fp;
    uint8_t *data = &g_data[0x0000];
    uint32_t ch;
    const char *chname[] = {"A", "B", "C", "D", "E", "F", "H", "I", "J"};
    typedef enum
    {
        CH_ASSIGN_FM0 = 0,
        CH_ASSIGN_SSG = 3,
        CH_ASSIGN_FM3 = 6,
    } CH_ASSIGN;
    const char *mucom88ver = NULL;
    const char *title = NULL;
    const char *author = NULL;
    const char *composer = NULL;
    const char *date = NULL;
    const char *comment = NULL;
    const char *outfile = NULL;
    DRIVER_TYPE driver_type = DRIVER_TYPE_UNKNOWN;
    struct {
        SOUND_TYPE type;
        CH_ASSIGN assign;
    } ch_info[3];
    uint32_t inst_offset;
    const struct {
        const char *name;
        DRIVER_TYPE type;
    } driver_type_table[] = {
        {"opn",		DRIVER_TYPE_OPN			},
        {"opna",	DRIVER_TYPE_OPNA		},
        {"va",		DRIVER_TYPE_OPNA_VA		},
        {"mono",	DRIVER_TYPE_OPNA_MONO	},
        {"x1opm",	DRIVER_TYPE_X1_OPM		},
        {"x1psg",	DRIVER_TYPE_X1_PSG		},
        {NULL,		DRIVER_TYPE_UNKNOWN		},
    };

    /* command line options */
    while ((c = getopt(argc, argv, "vwo:m:t:a:c:d:C:F:")) != -1)
    {
        switch (c)
        {
        case 'v':
            /* debug option */
            g_opt_verbose = true;
            break;
        case 'w':
            /* apply workaround and ignore warnings */
            g_opt_ignore_warning = true;
            break;
        case 'o':
            outfile = optarg;
            break;
        case 'm':
            /* 1.7 is required for using "r%n" */
            mucom88ver = optarg;
            break;
        case 't':
            title = optarg;
            break;
        case 'a':
            author = optarg;
            break;
        case 'c':
            composer = optarg;
            break;
        case 'd':
            date = optarg;
            break;
        case 'C':
            comment = optarg;
            break;
        case 'F':
            for (int i = 0; driver_type_table[i].name != NULL; i++)
            {
                if (strcmp(optarg, driver_type_table[i].name) == 0)
                {
                    driver_type = driver_type_table[i].type;
                    break;
                }
            }
            break;
        default:
            help();
            break;
        }
    }

    if (optind != argc - 1)
    {
        help();
    }

    /* read data to buffer */
    fp = fopen(argv[optind], "rb");
    if (fp == NULL)
    {
        fprintf(stderr, "Can't open '%s'\n", argv[optind]);
        exit(1);
    }
    fread(g_data, sizeof(uint8_t), sizeof(g_data), fp);
    fclose(fp);

    if (outfile != NULL)
    {
        fp = fopen(outfile, "w");
        if (fp == NULL)
        {
            fprintf(stderr, "Can't open '%s'\n", outfile);
            exit(1);
        }
    }
    else
    {
        fp = stdout;
    }

    /* driver type */
    if (driver_type == DRIVER_TYPE_UNKNOWN)
    {
        driver_type = detect_driver_type(g_data);
    }

    switch (driver_type)
    {
    case DRIVER_TYPE_OPN:
        inst_offset = 0x0010;
        ch_info[0].type = SOUND_TYPE_FM;
        ch_info[1].type = SOUND_TYPE_SSG;
        ch_info[2].type = SOUND_TYPE_NONE;
        ch_info[0].assign = CH_ASSIGN_FM0;
        ch_info[1].assign = CH_ASSIGN_SSG;
        ch_info[2].assign = CH_ASSIGN_FM3;
        break;
    case DRIVER_TYPE_OPNA:
        inst_offset = 0x0020;
        ch_info[0].type = SOUND_TYPE_FM | SOUND_TYPE_STEREO;
        ch_info[1].type = SOUND_TYPE_FM | SOUND_TYPE_STEREO;
        ch_info[2].type = SOUND_TYPE_SSG;
        ch_info[0].assign = CH_ASSIGN_FM3;
        ch_info[1].assign = CH_ASSIGN_FM0;
        ch_info[2].assign = CH_ASSIGN_SSG;
        break;
    case DRIVER_TYPE_OPNA_VA:
        inst_offset = 0x0020;
        ch_info[0].type = SOUND_TYPE_FM | SOUND_TYPE_STEREO;
        ch_info[1].type = SOUND_TYPE_SSG;
        ch_info[2].type = SOUND_TYPE_FM | SOUND_TYPE_STEREO;
        ch_info[0].assign = CH_ASSIGN_FM0;
        ch_info[1].assign = CH_ASSIGN_SSG;
        ch_info[2].assign = CH_ASSIGN_FM3;
        break;
    case DRIVER_TYPE_OPNA_MONO:
        inst_offset = 0x0020;
        ch_info[0].type = SOUND_TYPE_NONE;
        ch_info[1].type = SOUND_TYPE_FM;
        ch_info[2].type = SOUND_TYPE_SSG;
        ch_info[0].assign = CH_ASSIGN_FM3;
        ch_info[1].assign = CH_ASSIGN_FM0;
        ch_info[2].assign = CH_ASSIGN_SSG;
        break;
    case DRIVER_TYPE_X1_OPM:
        inst_offset = 0x0020;
        ch_info[0].type = SOUND_TYPE_FM | SOUND_TYPE_STEREO | SOUND_TYPE_OPM;
        ch_info[1].type = SOUND_TYPE_SSG;
        ch_info[2].type = SOUND_TYPE_FM | SOUND_TYPE_STEREO | SOUND_TYPE_OPM;
        ch_info[0].assign = CH_ASSIGN_FM0;
        ch_info[1].assign = CH_ASSIGN_SSG;
        ch_info[2].assign = CH_ASSIGN_FM3;
        break;
    case DRIVER_TYPE_X1_PSG:
        data = &g_data[get_word(&g_data[0x001a])];
        inst_offset = 0x0010;
        ch_info[0].type = SOUND_TYPE_NONE;
        ch_info[1].type = SOUND_TYPE_SSG;
        ch_info[2].type = SOUND_TYPE_NONE;
        ch_info[0].assign = CH_ASSIGN_FM0;
        ch_info[1].assign = CH_ASSIGN_SSG;
        ch_info[2].assign = CH_ASSIGN_FM3;
        break;
    default:
        fprintf(stderr, "Unknown driver type\n");
        exit(1);
        break;
    }

    /* insert tag */
    if (mucom88ver != NULL)
    {
        fprintf(fp, "#mucom88 %s\n", mucom88ver);
    }
    if (title != NULL)
    {
        fprintf(fp, "#title %s\n", title);
    }
    if (author != NULL)
    {
        fprintf(fp, "#author %s\n", author);
    }
    if (composer != NULL)
    {
        fprintf(fp, "#composer %s\n", composer);
    }
    if (date != NULL)
    {
        fprintf(fp, "#date %s\n", date);
    }
    if (comment != NULL)
    {
        fprintf(fp, "#comment %s\n", comment);
    }
    fprintf(fp, "\n");

    /* convert */
    memset(g_loop_flag, 0, sizeof(g_loop_flag));
    memset(g_loop_nest, 0, sizeof(g_loop_nest));

    convert_inst(fp, data, inst_offset);

#ifdef USE_SSG_ENV_MACRO
    fprintf(fp, "%s", g_ssg_inst);
#endif /* USE_SSG_ENV_MACRO */

    for (ch = 0; ch < 9; ch++)
    {
        if (ch_info[ch / 3].type != SOUND_TYPE_NONE)
        {
            convert_music(
                fp,
                ch, ch_info[ch / 3].type,
                chname[ch_info[ch / 3].assign + (ch % 3)],
                data, g_loop_flag, g_loop_nest);
        }
    }

    /* Control tempo in X1 PSG data */
    if (g_ssg_tempo_count > 1 && driver_type == DRIVER_TYPE_X1_PSG)
    {
        DBG("Use FM channel for changing tempo\n");
        memset(g_loop_flag, 0, sizeof(g_loop_flag));
        memset(g_loop_nest, 0, sizeof(g_loop_nest));

        for (ch = 0; ch < 3; ch++)
        {
            convert_music(
                fp,
                3 + ch, SOUND_TYPE_DUMMY,
                chname[CH_ASSIGN_FM0 + ch],
                data, g_loop_flag, g_loop_nest);
        }
    }

    if (outfile)
    {
        fclose(fp);
    }

    return 0;
}
