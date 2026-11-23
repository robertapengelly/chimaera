/******************************************************************************
 * @file            msdos.c
 *****************************************************************************/
#include    <kaishi/vfs/msdos.h>
#include    <kaishi/vfs/node.h>
#include    <kaishi/disk.h>
#include    <kaishi/misc.h>
#include    <kaishi/spin.h>
#include    <kaishi/stdio.h>
#include    <kaishi/stdlib.h>

#include    <ctype.h>
#include    <stdint.h>
#include    <string.h>

__attribute__ ((packed)) struct fat_cache_entry {

    struct fs_node *node;
    
    uint32_t sector;
    uint8_t *data;

};

static spin_lock_t tmp_read_lock = { 0 };
static spin_lock_t tmp_open_lock = { 0 };
static spin_lock_t tmp_close_lock = { 0 };

static uint32_t sectors_per_cluster = 0;
static uint32_t reserved_sectors = 0;
static uint32_t number_of_fats = 0;
static uint32_t root_entries = 0;
static uint32_t sectors_per_fat = 0;
static uint32_t root_cluster = 0;
static uint32_t total_sectors = 0;
static uint32_t root_start = 0;
static uint32_t data_area = 0;
static uint32_t cluster_count = 0;
static uint32_t bytes_per_sector = 0;
static uint32_t hidden_sectors = 0;

static uint32_t get_fat_entry (struct fs_node *node, struct fat_cache_entry *fat_cache, uint32_t cluster) {

    uint32_t offset, sector, result;
    uint8_t *scratch;
    
    if (!node->read || !node->data) {
        return 0x0FFFFFF7;
    }
    
    if (cluster_count <= MAX_CLUST_12) {
        offset = cluster + (cluster / 2);
    } else if (cluster_count >= MIN_CLUST_16 && cluster_count <= MAX_CLUST_16) {
        offset = cluster * 2;
    } else if (cluster_count >= MIN_CLUST_32 && cluster_count <= MAX_CLUST_32) {
        offset = cluster * 4;
    } else {
        return 0x0FFFFFF7;
    }
    
    sector = ((offset / 512) + reserved_sectors) + hidden_sectors;
    
    if (!fat_cache->data) {
        fat_cache->data = kmalloc (disk_get_sector_size (node->data));
    }
    
    if (fat_cache->node != node || fat_cache->sector != sector) {
    
        if (node->read (node, fat_cache->data, (uint64_t) sector + 1, (size_t) 1)) {
            return 0x0FFFFFF7;
        }
        
        fat_cache->node = node;
        fat_cache->sector = sector;
    
    }
    
    scratch = fat_cache->data;
    offset %= 512;
    
    if (cluster_count <= MAX_CLUST_12) {
    
        if (offset == 511) {
        
            result = (uint32_t) scratch[offset];
            sector++;
            
            if (fat_cache->node != node || fat_cache->sector != sector) {
            
                if (node->read (node, fat_cache->data, (uint64_t) sector + 1, (size_t) 1)) {
                    return 0x0FFFFFF7;
                }
                
                fat_cache->node = node;
                fat_cache->sector = sector;
            
            }
            
            scratch = fat_cache->data;
            result |= ((uint32_t) scratch[0]) << 8;
        
        } else {
            result = (uint32_t) scratch[offset] | ((uint32_t) scratch[offset + 1]) << 8;
        }
        
        if (cluster & 1) {
            result = result >> 4;
        } else {
            result = result & 0x0FFF;
        }
    
    } else if (cluster_count >= MIN_CLUST_16 && cluster_count <= MAX_CLUST_16) {
        result = (uint32_t) scratch[offset] | ((uint32_t) scratch[offset + 1]) << 8;
    } else if (cluster_count >= MIN_CLUST_32 && cluster_count <= MAX_CLUST_32) {
        result = ((uint32_t) scratch[offset] | ((uint32_t) scratch[offset + 1]) << 8 | ((uint32_t) scratch[offset + 2]) << 16 | ((uint32_t) scratch[offset + 3]) << 24) & 0x0FFFFFFF;
    } else {
        result = 0x0FFFFFF7;
    }
    
    return result;

}

#define     MAX(a,b)                    ((a) > (b) ? (a) : (b))
#define     MIN(a,b)                    ((a) < (b) ? (a) : (b))

#define     LFN_MAX                     255

__attribute__ ((packed)) struct fat_dirent {

    uint8_t fn[12];
    uint16_t lfn[LFN_MAX + 1];

};

static const uint8_t cvt[] = {

    0x80,   0x9A,   0x45,   0x41,   0x8E,   0x41,   0x8F,
    0x80,   0x45,   0x45,   0x45,   0x49,   0x49,   0x49,
    0x8E,   0x8F,   0x90,   0x92,   0x92,   0x4F,   0x99,
    0x4F,   0x55,   0x55,   0x59,   0x99,   0x9A,   0x9B,
    0x9C,   0x9D,   0x9E,   0x9F,   0x41,   0x49,   0x4F,
    0x55,   0xA5,   0xA5,   0xA6,   0xA7,   0xA8,   0xA9,
    0xAA,   0xAB,   0xAC,   0xAD,   0xAE,   0xAF,   0xB0,
    0xB1,   0xB2,   0xB3,   0xB4,   0xB5,   0xB6,   0xB7,
    0xB8,   0xB9,   0xBA,   0xBB,   0xBC,   0xBD,   0xBE,
    0xBF,   0xC0,   0xC1,   0xC2,   0xC3,   0xC4,   0xC5,
    0xC6,   0xC7,   0xC8,   0xC9,   0xCA,   0xCB,   0xCC,
    0xCD,   0xCE,   0xCF,   0xD0,   0xD1,   0xD2,   0xD3,
    0xD4,   0xD5,   0xD6,   0xD7,   0xD8,   0xD9,   0xDA,
    0xDB,   0xDC,   0xDD,   0xDE,   0xDF,   0xE0,   0xE1,
    0xE2,   0xE3,   0xE4,   0xE5,   0xE6,   0xE7,   0xE8,
    0xE9,   0xEA,   0xEB,   0xEC,   0xED,   0xEE,   0xEF,
    0xF0,   0xF1,   0xF2,   0xF3,   0xF4,   0xF5,   0xF6,
    0xF7,   0xF8,   0xF9,   0xFA,   0xFB,   0xFC,   0xFD,
    0xFE,   0xFF

};

static const uint16_t uc437[] = {                     /*  CP437(U.S.) to Unicode conversion table */

    0x00C7,     0x00FC,     0x00E9,     0x00E2,     0x00E4,
    0x00E0,     0x00E5,     0x00E7,     0x00EA,     0x00EB,
    0x00E8,     0x00EF,     0x00EE,     0x00EC,     0x00C4,
    0x00C5,     0x00C9,     0x00E6,     0x00C6,     0x00F4,
    0x00F6,     0x00F2,     0x00FB,     0x00F9,     0x00FF,
    0x00D6,     0x00DC,     0x00A2,     0x00A3,     0x00A5,
    0x20A7,     0x0192,     0x00E1,     0x00ED,     0x00F3,
    0x00FA,     0x00F1,     0x00D1,     0x00AA,     0x00BA,
    0x00BF,     0x2310,     0x00AC,     0x00BD,     0x00BC,
    0x00A1,     0x00AB,     0x00BB,     0x2591,     0x2592,
    0x2593,     0x2502,     0x2524,     0x2561,     0x2562,
    0x2556,     0x2555,     0x2563,     0x2551,     0x2557,
    0x255D,     0x255C,     0x255B,     0x2510,     0x2514,
    0x2534,     0x252C,     0x251C,     0x2500,     0x253C,
    0x255E,     0x255F,     0x255A,     0x2554,     0x2569,
    0x2566,     0x2560,     0x2550,     0x256C,     0x2567,
    0x2568,     0x2564,     0x2565,     0x2559,     0x2558,
    0x2552,     0x2553,     0x256B,     0x256A,     0x2518,
    0x250C,     0x2588,     0x2584,     0x258C,     0x2590,
    0x2580,     0x03B1,     0x00DF,     0x0393,     0x03C0,
    0x03A3,     0x03C3,     0x00B5,     0x03C4,     0x03A6,
    0x0398,     0x03A9,     0x03B4,     0x221E,     0x03C6,
    0x03B5,     0x2229,     0x2261,     0x00B1,     0x2265,
    0x2264,     0x2320,     0x2321,     0x00F7,     0x2248,
    0x00B0,     0x2219,     0x00B7,     0x221A,     0x207F,
    0x00B2,     0x25A0,     0x00A0

};

static uint16_t uni2oem (uint32_t uni, short cp) {

    const uint16_t *p = uc437;
    uint16_t c = 0;
    
    if (uni < 0x80) {
        c = (uint16_t) uni;
    } else {
    
        (void) cp;
        
        if (uni < 0x10000) {
        
            for (c = 0; c < 0x80 && uni != p[c]; c++) {
                ;
            }
            
            c = (c + 0x80) & 0xff;
        
        }
    
    }
    
    return c;

}

static uint16_t oem2uni (uint16_t oem, short cp) {

    const uint16_t *p = uc437;
    uint16_t c = 0;
    
    if (oem < 0x80) {
        c = oem;
    } else {
    
        (void) cp;
        
        if (oem < 0x100) {
            c = p[oem - 0x80];
        }
    
    }
    
    return c;

}

static uint32_t tchar2uni (const char **str) {

    const char *p = *str;
    uint32_t uc;
    
    uint16_t wc = (uint8_t) *p++;
    
    if (wc != 0) {
    
        if ((wc = oem2uni (wc, 437)) == 0) {
            return 0xffffffff;
        }
    
    }
    
    uc = wc;
    
    *str = p;
    return uc;

}

static int chk_chr (const char *str, int chr) {

    while (*str && *str != chr) {
        str++;
    }
    
    return *str;

}

static int create_name (struct fat_dirent *dp, const char *path) {

    uint32_t di = 0, uc, i, ni, si;
    
    const char *p = path;
    uint8_t b, cf;
    
    uint16_t wc;
    
    for (;;) {
    
        uc = tchar2uni (&p);
        
        if (uc == 0xffffffff) {
            return -1;
        }
        
        if (uc >= 0x10000) {
            dp->lfn[di++] = (uint16_t) (uc >> 16);
        }
        
        wc = (uint16_t) uc;
        
        if (wc < ' ' || wc == '/' || wc == '\\') {
            break;
        }
        
        if (wc < 0x80 && chk_chr ("\"*:<>\?|\x7F", wc)) {
            return -1;
        }
        
        if (di >= LFN_MAX) {
            return -1;
        }
        
        dp->lfn[di++] = wc;
    
    }
    
    while (*p == '/' || *p == '\\') {
        p++;
    }
    
    cf = (wc < ' ') ? 4 : 0;
    
    while (di) {
    
        wc = dp->lfn[di - 1];
        
        if (wc != ' ' && wc != '.') {
            break;
        }
        
        di--;
    
    }
    
    dp->lfn[di] = 0;
    
    if (di == 0) {
        return -1;
    }
    
    for (si = 0; dp->lfn[si] == ' '; si++) {
        ;
    }
    
    while (dp->lfn[si] == '.') {
    
        cf |= 3;
        si++;
    
    }
    
    while (di > 0 && dp->lfn[di - 1] != '.') {
        di--;
    }
    
    memset (dp->fn, ' ', 11);
    i = b = 0; ni = 8;
    
    for (;;) {
    
        wc = dp->lfn[si++];
        
        if (wc == 0) {
            break;
        }
        
        if (wc == ' ' || (wc == '.' && si != di)) {
        
            cf |= 3;
            continue;
        
        }
        
        if (i >= ni || si == di) {
        
            if (ni == 11) {
            
                cf |= 3;
                break;
            
            }
            
            if (si != di) {
                cf |= 3;
            }
            
            if (si > di) {
                break;
            }
            
            si = di;
            
            i = 8;
            ni = 11;
            
            b <<= 2;
            continue;
        
        }
        
        if (wc >= 0x80) {
        
            cf |= 2;
            
            if ((wc = uni2oem (wc, 437)) & 0x80) {
                wc = cvt[wc & 0x7f];
            }
        
        }
        
        if (wc >= 0x100) {
        
            if (i >= ni - 1) {
            
                cf |= 3;
                
                i = ni;
                continue;
            
            }
            
            dp->fn[i++] = (uint8_t) (wc >> 8);
        
        } else {
        
            if (wc == 0 || chk_chr ("+,;=[]", wc)) {
                wc = '_'; cf |= 3;
            } else {
            
                if (isupper ((int) wc)) {
                    b |= 2;
                }
                
                if (islower ((int) wc)) {
                    b |= 1; wc -= 0x20;
                }
            
            }
        
        }
        
        dp->fn[i++] = (uint8_t) wc;
    
    }
    
    if (dp->fn[0] == 0xe5) {
        dp->fn[0] = 0x05;
    }
    
    if (ni == 8) {
        b <<= 2;
    }
    
    if ((b & 0x0c) == 0x0c || (b & 0x03) == 0x03) {
        cf |= 2;
    }
    
    if (!(cf & 2)) {
    
        if (b & 0x01) {
            cf |= 0x10;
        }
        
        if (b & 0x04) {
            cf |= 0x08;
        }
    
    }
    
    dp->fn[11] = cf;
    return 0;

}

static const uint8_t lfn_ofs[] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };

static uint32_t wtoupper (uint32_t uni) {

    static const uint16_t cvt1[] = {                  /* Compressed up conversion table for U+0000 - U+0FFF */
    
        /* Basic Latin */
        0x0061,     0x031A,
        
        /* Latin-1 Supplement */
        0x00E0,     0x0317,     0x00F8,     0x0307,
        0x00FF,     0x0001,     0x0178,
        
        /* Latin Extended-A */
        0x0100,     0x0130,     0x0132,     0x0106,
        0x0139,     0x0110,     0x014A,     0x012E,
        0x0179,     0x0106,
        
        /* Latin Extended-B */
        0x0180,     0x004D,     0x0243,     0x0181,
        0x0182,     0x0182,     0x0184,     0x0184,
        0x0186,     0x0187,     0x0187,     0x0189,
        0x018A,     0x018B,     0x018B,     0x018D,
        0x018E,     0x018F,     0x0190,     0x0191,
        0x0191,     0x0193,     0x0194,     0x01F6,
        0x0196,     0x0197,     0x0198,     0x0198,
        0x023D,     0x019B,     0x019C,     0x019D,
        0x0220,     0x019F,     0x01A0,     0x01A0,
        0x01A2,     0x01A2,     0x01A4,     0x01A4,
        0x01A6,     0x01A7,     0x01A7,     0x01A9,
        0x01AA,     0x01AB,     0x01AC,     0x01AC,
        0x01AE,     0x01AF,     0x01AF,     0x01B1,
        0x01B2,     0x01B3,     0x01B3,     0x01B5,
        0x01B5,     0x01B7,     0x01B8,     0x01B8,
        0x01BA,     0x01BB,     0x01BC,     0x01BC,
        0x01BE,     0x01F7,     0x01C0,     0x01C1,
        0x01C2,     0x01C3,     0x01C4,     0x01C5,
        0x01C4,     0x01C7,     0x01C8,     0x01C7,
        0x01CA,     0x01CB,     0x01CA,     0x01CD,
        0x0110,     0x01DD,     0x0001,     0x018E,
        0x01DE,     0x0112,     0x01F3,     0x0003,
        0x01F1,     0x01F4,     0x01F4,     0x01F8,
        0x0128,     0x0222,     0x0112,     0x023A,
        0x0009,     0x2C65,     0x023B,     0x023B,
        0x023D,     0x2C66,     0x023F,     0x0240,
        0x0241,     0x0241,     0x0246,     0x010A,
        
        /* IPA Extensions */
        0x0253,     0x0040,     0x0181,     0x0186,
        0x0255,     0x0189,     0x018A,     0x0258,
        0x018F,     0x025A,     0x0190,     0x025C,
        0x025D,     0x025E,     0x025F,     0x0193,
        0x0261,     0x0262,     0x0194,     0x0264,
        0x0265,     0x0266,     0x0267,     0x0197,
        0x0196,     0x026A,     0x2C62,     0x026C,
        0x026D,     0x026E,     0x019C,     0x0270,
        0x0271,     0x019D,     0x0273,     0x0274,
        0x019F,     0x0276,     0x0277,     0x0278,
        0x0279,     0x027A,     0x027B,     0x027C,
        0x2C64,     0x027E,     0x027F,     0x01A6,
        0x0281,     0x0282,     0x01A9,     0x0284,
        0x0285,     0x0286,     0x0287,     0x01AE,
        0x0244,     0x01B1,     0x01B2,     0x0245,
        0x028D,     0x028E,     0x028F,     0x0290,
        0x0291,     0x01B7,
        
        /* Greek, Coptic */
        0x037B,     0x0003,     0x03FD,     0x03FE,
        0x03FF,     0x03AC,     0x0004,     0x0386,
        0x0388,     0x0389,     0x038A,     0x03B1,
        0x0311,     0x03C2,     0x0002,     0x03A3,
        0x03A3,     0x03C4,     0x0308,     0x03CC,
        0x0003,     0x038C,     0x038E,     0x038F,
        0x03D8,     0x0118,     0x03F2,     0x000A,
        0x03F9,     0x03F3,     0x03F4,     0x03F5,
        0x03F6,     0x03F7,     0x03F7,     0x03F9,
        0x03FA,     0x03FA,
        
        /* Cyrillic */
        0x0430,     0x0320,     0x0450,     0x0710,
        0x0460,     0x0122,     0x048A,     0x0136,
        0x04C1,     0x010E,     0x04CF,     0x0001,
        0x04C0,     0x04D0,     0x0144,
        
        /* Armenian */
        0x0561,     0x0426,
        
        /* EOT */
        0x0000
    
    };
    
    static const uint16_t cvt2[] = {                  /* Compressed up conversion table for U+1000 - U+FFFF */
    
        /* Phonetic Extensions */
        0x1D7D,     0x0001,     0x2C63,
        
        /* Latin Extended Additional */
        0x1E00,     0x0196,
        0x1EA0,     0x015A,
        
        /* Greek Extended */
        0x1F00,     0x0608,     0x1F10,     0x0606,
        0x1F20,     0x0608,     0x1F30,     0x0608,
        0x1F40,     0x0606,     0x1F51,     0x0007,
        0x1F59,     0x1F52,     0x1F5B,     0x1F54,
        0x1F5D,     0x1F56,     0x1F5F,     0x1F60,
        0x0608,     0x1F70,     0x000E,     0x1FBA,
        0x1FBB,     0x1FC8,     0x1FC9,     0x1FCA,
        0x1FCB,     0x1FDA,     0x1FDB,     0x1FF8,
        0x1FF9,     0x1FEA,     0x1FEB,     0x1FFA,
        0x1FFB,     0x1F80,     0x0608,     0x1F90,
        0x0608,     0x1FA0,     0x0608,     0x1FB0,
        0x0004,     0x1FB8,     0x1FB9,     0x1FB2,
        0x1FBC,     0x1FCC,     0x0001,     0x1FC3,
        0x1FD0,     0x0602,     0x1FE0,     0x0602,
        0x1FE5,     0x0001,     0x1FEC,     0x1FF3,
        0x0001,     0x1FFC,
        
        /* Letterlike Symbols */
        0x214E,     0x0001,     0x2132,
        
        /* Number forms */
        0x2170,     0x0210,     0x2184,     0x0001,
        0x2183,
        
        /* Enclosed Alphanumerics */
        0x24D0,     0x051A,     0x2C30,     0x042F,
        
        /* Latin Extended-C */
        0x2C60,     0x0102,     0x2C67,     0x0106,
        0x2C75,     0x0102,
        
        /* Coptic */
        0x2C80,     0x0164,
        
        /* Georgian Supplement */
        0x2D00,     0x0826,
        
        /* Full-width */
        0xFF41,     0x031A,
        
        /* EOT */
        0x0000
    
    };
    
    const uint16_t *p;
    uint16_t uc, bc, nc, cmd;
    
    if (uni < 0x10000) {
    
        uc = (uint16_t) uni;
        p = uc < 0x1000 ? cvt1 : cvt2;
        
        for (;;) {
        
            bc = *p++;
            
            if (bc == 0 || uc < bc) {
                break;
            }
            
            nc = *p++; cmd = nc >> 8; nc &= 0xFF;
            
            if (uc < bc + nc) {
            
                switch (cmd) {
                
                    case 0:
                    
                        uc = p[uc - bc];
                        break;
                    
                    case 1:
                    
                        uc -= (uc - bc) & 1;
                        break;
                    
                    case 2:
                    
                        uc -= 16;
                        break;
                    
                    case 3:
                    
                        uc -= 32;
                        break;
                    
                    case 4:
                    
                        uc -= 48;
                        break;
                    
                    case 5:
                    
                        uc -= 26;
                        break;
                    
                    case 6:
                    
                        uc += 8;
                        break;
                    
                    case 7:
                    
                        uc -= 80;
                        break;
                    
                    case 8:
                    
                        uc -= 0x1C60;
                        break;
                
                }
                
                break;
            
            }
            
            if (cmd == 0) {
                p += nc;
            }
        
        }
        
        uni = uc;
    
    }
    
    return uni;

}

static uint16_t ld_word (const uint8_t *ptr) {
    return ((uint16_t) ptr[0]) | (((uint16_t) ptr[1]) << 8);
}

static int cmp_lfn (const uint16_t *lfnbuf, uint8_t *dir) {

    uint16_t wc, uc;
    uint32_t i, s;
    
    if (ld_word (dir + 26) != 0) {
        return 0;
    }
    
    i = ((*dir & 0x3f) - 1) * 13;
    
    for (wc = 1, s = 0; s < 13; s++) {
    
        uc = ld_word (dir + lfn_ofs[s]);
        
        if (wc != 0) {
        
            if (i >= LFN_MAX || wtoupper (uc) != wtoupper (lfnbuf[i++])) {
                return 0;
            }
            
            wc = uc;
        
        } else {
        
            if (uc != 0xffff) {
                return 0;
            }
        
        }
    
    }
    
    if ((*dir & 0x40) && wc && lfnbuf[i]) {
        return 0;
    }
    
    return 1;

}

static uint8_t sum_sfn (uint8_t *dir) {

    uint8_t sum = 0;
    uint32_t n = 11;
    
    do {
        sum = (sum >> 1) + (sum << 7) + *dir++;
    } while (--n);
    
    return sum;

}

__attribute__ ((packed)) struct msdos_node {

    uint64_t reserved_sectors;
    uint32_t hidden_sectors;
    
    uint32_t start_cluster, data_area, file_offset;
    size_t sectors_per_cluster, cluster_count, sector_size, file_size;
    
    struct fat_cache_entry fat_cache;
    void *scratch;

};

static int msdos_read (void *data, void *buf, size_t count) {

    struct fs_node *parent = 0, *node = 0;
    struct msdos_node *this = 0;
    
    uint32_t offset = 0, sector = 0, current_cluster = 0;
    size_t clusters = 0, sectors, remaining, copy;
    
    int ret = 0, first = 1;
    spin_lock (&tmp_read_lock);
    
    if (!(node = (struct fs_node *) data)) {
    
        spin_unlock (&tmp_read_lock);
        return -1;
    
    }
    
    if (!(parent = (struct fs_node *) node->parent)) {
    
        spin_unlock (&tmp_read_lock);
        return -1;
    
    }
    
    if (!parent->read) {
    
        spin_unlock (&tmp_read_lock);
        return -1;
    
    }
    
    if (!parent->data) {
    
        spin_unlock (&tmp_read_lock);
        return -1;
    
    }
    
    if (!(this = (struct msdos_node *) node->data)) {
    
        spin_unlock (&tmp_read_lock);
        return -1;
    
    }
    
    current_cluster = this->start_cluster;
    
    reserved_sectors = this->reserved_sectors;
    hidden_sectors   = this->hidden_sectors;
    cluster_count    = this->cluster_count;
    
    clusters = this->file_offset / this->sector_size;
    
    while (clusters--) {
    
        if ((current_cluster = get_fat_entry (parent, &this->fat_cache, current_cluster)) >= 0x0FFFFFF7) {
        
            spin_unlock (&tmp_read_lock);
            return -1;
        
        }
    
    }
    
    sectors = 1;
    
    if (this->sector_size > disk_get_sector_size (parent->data)) {
        sectors = this->sector_size / disk_get_sector_size (parent->data);
    }
    
    remaining = count;
    
    if (remaining > (this->file_size - this->file_offset)) {
        remaining = (this->file_size - this->file_offset);
    }
    
    copy = 0;
    
    for (;;) {
    
        if (this->file_offset >= this->file_size || !remaining) {
        
            spin_unlock (&tmp_read_lock);
            return ret;
        
        }
        
        if (first || (this->file_offset % this->sector_size) == 0) {
        
            if (!first) {
            
                if ((current_cluster = get_fat_entry (parent, &this->fat_cache, current_cluster)) >= 0x0FFFFFF7) {
                
                    spin_unlock (&tmp_read_lock);
                    return -1;
                
                }
            
            }
            
            first = 0;
            
            sector = (uint64_t) this->data_area + ((current_cluster - 2) * this->sectors_per_cluster);
            sector += this->hidden_sectors;
            
            if (parent->read (parent, this->scratch, (uint64_t) sector + 1, (size_t) sectors)) {
            
                spin_unlock (&tmp_read_lock);
                return -1;
            
            }
        
        }
        
        offset = (this->file_offset % this->sector_size);
        
        if (this->sector_size - offset < remaining) {
        
            offset = this->file_offset % this->sector_size;
            copy = MIN (this->sector_size - offset, remaining);
            
            memcpy ((uint8_t *) buf + ret, (uint8_t *) this->scratch + offset, copy);
            
            this->file_offset += copy;
            ret += copy;
            
            remaining -= copy;
            continue;
        
        }
        
        memcpy ((uint8_t *) buf + ret, (uint8_t *) this->scratch + offset, MIN (this->sector_size, remaining));
        
        this->file_offset += MIN (this->sector_size, remaining);
        ret += MIN (this->sector_size, remaining);
        
        remaining -= MIN (this->sector_size, remaining);
    
    }
    
    spin_unlock (&tmp_read_lock);
    return -1;

}

static void msdos_close (void *data) {

    struct fs_node *node = 0;
    struct msdos_node *this = 0;
    
    /* Lock the system so that other cores don't interupt us. */
    spin_lock (&tmp_close_lock);
    
    /* Check if data was not NULL. */
    if (!(node = (struct fs_node *) data)) {
    
        spin_unlock (&tmp_close_lock);
        return;
    
    }
    
    /* Have we got child data?? */
    if ((this = (struct msdos_node *) node->data)) {
    
        /* Yep, so have we got cached FAT data?? */
        if (this->fat_cache.data) {
        
            /* Yep, so free it. */
            kfree (this->fat_cache.data);
        
        }
        
        /* Do we have a scratch buffer?? Shuld be yes. */
        if (this->scratch) {
        
            /* Free it. */
            kfree (this->scratch);
        
        }
        
        /* Free the child data. */
        kfree (this);
    
    }
    
    /* Free the passed node. */
    kfree (node);
    
    /* Finally unlock so other calls work. */
    spin_unlock (&tmp_close_lock);

}

static struct fs_node *msdos_open (void *data, const char *path, size_t depth) {

    struct fs_node *root_node = 0, *parent = 0, *node = 0;
    
    struct msdos_specs *this = 0;
    uint64_t offset = 0;
    
    struct fat_dirent *dp;
    struct msdos_dirent *de;
    
    size_t path_depth, sector_size;
    size_t no_ents, i;
    
    uint32_t current_cluster, end_cluster;
    uint8_t a, ord, sum, c;
    
    struct msdos_node *msdos_node;
    void *scratch;
    
    struct fat_cache_entry fat_cache = { 0 };
    spin_lock (&tmp_open_lock);
    
    if (!(root_node = (struct fs_node *) data)) {
    
        spin_unlock (&tmp_open_lock);
        return 0;
    
    }
    
retry:
    
    if (fat_cache.data) {
        kfree (fat_cache.data);
    }
    
    fat_cache.data = 0;
    fat_cache.node = 0;
    fat_cache.sector = 0xFFFFFFFF;
    
    node = root_node;
    path_depth = depth;
    
    if (!(parent = (struct fs_node *) node->parent)) {
    
        spin_unlock (&tmp_open_lock);
        return 0;
    
    }
    
    if (!parent->read) {
    
        spin_unlock (&tmp_open_lock);
        return 0;
    
    }
    
    if (!parent->data) {
    
        spin_unlock (&tmp_open_lock);
        return 0;
    
    }
    
    if (!(this = (struct msdos_specs *) node->data)) {
    
        spin_unlock (&tmp_open_lock);
        return 0;
    
    }
    
    reserved_sectors = this->reserved_sectors;
    hidden_sectors   = this->hidden_sectors;
    cluster_count    = this->cluster_count;
    
    sector_size      = MAX (disk_get_sector_size (parent->data), this->bytes_per_sector);
    
    if (depth > 0) {
    
        scratch = kmalloc (sector_size);
        
        if (this->cluster_count >= MIN_CLUST_32 && this->cluster_count <= MAX_CLUST_32) {
        
            current_cluster = this->root_cluster;
            /* current_entry = 0; */
            
            offset = (uint64_t) this->data_area + ((current_cluster - 2) * this->sectors_per_cluster);
        
        } else {
        
            current_cluster = 0;
            /* current_entry = 0; */
            
            offset = (uint64_t) this->root_start;
        
        }
        
        sectors_per_cluster = this->sectors_per_cluster;
        root_entries = this->root_entries;
        
        offset += this->hidden_sectors;
        
        while (sector_size >= (this->bytes_per_sector * this->cluster_count)) {
            sectors_per_cluster--;
        }
        
        for (;;) {
        
            dp = kmalloc (sizeof (*dp));
            ord = sum = 0xff;
            
            if (create_name (dp, path) < 0) {
            
                kfree (scratch);
                kfree (dp);
                
                spin_unlock (&tmp_open_lock);
                return 0;
            
            }
            
            if (parent->read (parent, scratch, (uint64_t) offset + 1, (size_t) 1) == 0x06) {
            
                kfree (scratch);
                kfree (dp);
                
                if (root_node->check_and_mount) {
                
                    if (root_node->check_and_mount (root_node)) {
                    
                        kfree (scratch);
                        
                        spin_unlock (&tmp_open_lock);
                        return 0;
                    
                    }
                    
                    goto retry;
                
                }
                
                spin_unlock (&tmp_open_lock);
                return 0;
            
            }
            
            no_ents = 512 / sizeof (*de);
            de = scratch;
            
            for (i = 0; i < no_ents; i++) {
            
                a = de[i].attr & 0x3f;
                
                if ((c = de[i].name[0]) == 0) {
                
                    kfree (scratch);
                    kfree (dp);
                    
                    spin_unlock (&tmp_open_lock);
                    return 0;
                
                }
                
                if (c == 0xE5 || ((a & 0x08) && a != 15)) {
                    ord = 0xff;
                } else {
                
                    if (a == 15) {
                    
                        if (!(dp->fn[11] & 0x40)) {
                        
                            if (c & 0x40) {
                            
                                sum = de[i].csum;
                                
                                c &= (uint8_t) ~0x40;
                                ord = c;
                            
                            }
                            
                            ord = (c == ord && sum == de[i].csum && cmp_lfn (dp->lfn, (uint8_t *) &de[i])) ? ord - 1 : 0xff;
                        
                        }
                    
                    } else {
                    
                        if (ord == 0 && sum == sum_sfn ((uint8_t *) &de[i])) {
                            break;
                        }
                        
                        if (!(dp->fn[11] & 1) && !memcmp (&de[i], dp->fn, 11)) {
                            break;
                        }
                        
                        ord = 0xff;
                    
                    }
                
                }
            
            }
            
            kfree (dp);
            
            if (i == no_ents) {
                
                if (current_cluster == 0) {
                
                    root_entries -= i;
                    
                    if (!root_entries) {
                        
                        kfree (scratch);
                        
                        spin_unlock (&tmp_open_lock);
                        return 0;
                    
                    }
                    
                    offset++;
                    continue;
                
                }
                
                if ((int32_t) --sectors_per_cluster > 0) {
                
                    offset++;
                    continue;
                
                }
                
                if (cluster_count >= MIN_CLUST_32 && current_cluster <= MAX_CLUST_32) {
                    end_cluster = 0x0FFFFFF7;
                } else if (cluster_count >= MIN_CLUST_16 && cluster_count <= MAX_CLUST_16) {
                    end_cluster = 0xFFF7;
                } else if (cluster_count <= MAX_CLUST_12) {
                    end_cluster = 0x0FF7;
                }
                
                if (current_cluster >= end_cluster) {
                
                    kfree (scratch);
                    
                    spin_unlock (&tmp_open_lock);
                    return 0;
                
                }
                
                current_cluster = get_fat_entry (parent, &fat_cache, current_cluster);
                
                if (current_cluster >= end_cluster) {
                
                    kfree (scratch);
                    
                    spin_unlock (&tmp_open_lock);
                    return 0;
                
                }
                
                offset = (uint64_t) this->data_area + ((current_cluster - 2) * this->sectors_per_cluster);
                offset += this->hidden_sectors;
                
                sectors_per_cluster = this->sectors_per_cluster;
                continue;
            
            }
            
            current_cluster = ((uint32_t) de[i].startlo[0] | ((uint32_t) de[i].startlo[1]) << 8 | ((uint32_t) de[i].starthi[0]) << 16 | ((uint32_t) de[i].starthi[1]) << 24) & 0x0FFFFFFF;
            a = de[i].attr & 0x3F;
            
            if (path_depth-- > 1) {
            
                if (!(a & ATTR_DIR)) {
                
                    kfree (scratch);
                    
                    spin_unlock (&tmp_open_lock);
                    return 0;
                
                }
                
                path += strlen (path) + 1;
                
                if (*path) {
                
                    offset = (uint64_t) this->data_area + ((current_cluster - 2) * this->sectors_per_cluster);
                    offset += this->hidden_sectors;
                    
                    sectors_per_cluster = this->sectors_per_cluster;
                    continue;
                
                }
            
            }
            
            if (a & (ATTR_DIR | ATTR_VOLUME_ID)) {
            
                kfree (scratch);
                
                spin_unlock (&tmp_open_lock);
                return 0;
            
            }
            
            msdos_node = kmalloc (sizeof (*msdos_node));
            msdos_node->fat_cache.sector    = 0xFFFFFFFF;
            
            msdos_node->sectors_per_cluster = this->sectors_per_cluster;
            msdos_node->reserved_sectors    = this->reserved_sectors;
            msdos_node->hidden_sectors      = this->hidden_sectors;
            msdos_node->start_cluster       = current_cluster;
            msdos_node->data_area           = this->data_area;
            
            msdos_node->cluster_count       = this->cluster_count;
            msdos_node->file_size           = ((size_t) de[i].size[0] | ((size_t) de[i].size[1]) << 8 | ((size_t) de[i].size[2]) << 16 | ((size_t) de[i].size[3]) << 24);
            
            msdos_node->sector_size         = this->bytes_per_sector * this->sectors_per_cluster;
            msdos_node->scratch             = kmalloc (MAX (msdos_node->sector_size, (size_t) disk_get_sector_size (parent->data)));
            
            node = kmalloc (sizeof (*node));
            
            node->parent = parent;
            node->data   = msdos_node;
            
            node->kclose = msdos_close;
            node->kread  = msdos_read;
            
            kfree (scratch);
            
            if (fat_cache.data) {
                kfree (fat_cache.data);
            }
            
            spin_unlock (&tmp_open_lock);
            return node;
        
        }
    
    }
    
    spin_unlock (&tmp_open_lock);
    return 0;

}

void *msdos_probe (void *data, void *scratch) {

    struct fs_node *node = 0, *root_node = 0;
    
    struct msdos_boot_sector *bs = 0;
    struct msdos_specs *this = 0;
    
    spin_lock (&tmp_read_lock);
    
    if (!(root_node = (struct fs_node *) data)) {
    
        spin_unlock (&tmp_read_lock);
        return 0;
    
    }
    
    bs = (struct msdos_boot_sector *) scratch;
    
    if ((bs->boot_jump[0] & 0xff) != 0xEB || (bs->boot_jump[1] & 0xff) < 0x21 || (bs->boot_jump[2] & 0xff) != 0x90) {
    
        spin_unlock (&tmp_read_lock);
        return 0;
    
    }
    
    bytes_per_sector    = (uint32_t) array_to_int (bs->bytes_per_sector, 2);
    reserved_sectors    = (uint32_t) array_to_int (bs->reserved_sectors, 2);
    sectors_per_fat     = (uint32_t) array_to_int (bs->sectors_per_fat16, 2);
    total_sectors       = (uint32_t) array_to_int (bs->total_sectors16, 2);
    root_entries        = (uint32_t) array_to_int (bs->root_entries, 2);
    
    sectors_per_cluster = (uint32_t) bs->sectors_per_cluster;
    number_of_fats      = (uint32_t) bs->no_fats;
    
    hidden_sectors      = (uint32_t) array_to_int (bs->hidden_sectors, 4);
    
    if (bytes_per_sector < 512 || (bytes_per_sector % 512) != 0) {
    
        spin_unlock (&tmp_read_lock);
        return 0;
    
    }
    
    if (!sectors_per_cluster || !reserved_sectors || !number_of_fats) {
    
        spin_unlock (&tmp_read_lock);
        return 0;
    
    }
    
    root_cluster = 0;
    
    if (!root_entries) {
    
        if (bs->boot_jump[1] < 0x58) {
        
            spin_unlock (&tmp_read_lock);
            return 0;
        
        }
        
        root_cluster = (uint32_t) array_to_int (bs->fat32.root_cluster, 4);
        
        if (!root_cluster) {
        
            spin_unlock (&tmp_read_lock);
            return 0;
        
        }
    
    }
    
    if (!total_sectors) {
    
        if (bs->boot_jump[1] < 0x22) {
        
            spin_unlock (&tmp_read_lock);
            return 0;
        
        }
        
        total_sectors = (uint32_t) array_to_int (bs->total_sectors32, 4);
        
        if (!total_sectors) {
        
            spin_unlock (&tmp_read_lock);
            return 0;
        
        }
    
    }
    
    if (!sectors_per_fat) {
    
        if (bs->boot_jump[1] < 0x58) {
        
            spin_unlock (&tmp_read_lock);
            return 0;
        
        }
        
        sectors_per_fat = (uint32_t) array_to_int (bs->fat32.sectors_per_fat32, 4);
        
        if (!sectors_per_fat) {
        
            spin_unlock (&tmp_read_lock);
            return 0;
        
        }
    
    }
    
    root_start = 0;
    
    if (root_entries) {
    
        root_start = reserved_sectors + (sectors_per_fat * number_of_fats);
        data_area = root_start + (((root_entries * 32) + (bytes_per_sector - 1)) / bytes_per_sector);
    
    } else {
        data_area = reserved_sectors + (sectors_per_fat * number_of_fats);
    }
    
    this = kmalloc (sizeof (*this));
    
    this->sectors_per_cluster = sectors_per_cluster;
    this->bytes_per_sector    = bytes_per_sector;
    this->hidden_sectors      = hidden_sectors;
    
    this->cluster_count       = (total_sectors - data_area) / sectors_per_cluster;
    this->root_cluster        = root_cluster;
    
    this->reserved_sectors    = reserved_sectors;
    this->root_entries        = root_entries;
    
    this->root_start          = root_start;
    this->data_area           = data_area;
    
    node = kmalloc (sizeof (*node));
    
    node->parent  = root_node;
    node->data    = this;
    
    node->kopen   = msdos_open;
    
    spin_unlock (&tmp_read_lock);
    return node;

}
