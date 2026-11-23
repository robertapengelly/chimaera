/******************************************************************************
 * @file            msdos.c
 *****************************************************************************/
#include    <corazon/storage/disk.h>
#include    <corazon/utils/misc.h>
#include    <corazon/vfs/msdos.h>
#include    <corazon/vfs/node.h>
#include    <corazon/spin.h>

#include    <sys/stat.h>
#include    <sys/types.h>

#include    <errno.h>
#include    <ctype.h>
#include    <dirent.h>
#include    <stdint.h>
#include    <stdio.h>
#include    <string.h>

__attribute__ ((packed)) struct fat_cache_entry {

    struct fs_node *node;
    
    uint32_t sector;
    uint8_t *data;

};

extern int cmos_get_unix_time (uint64_t *seconds);

static spin_lock_t tmp_seek_lock = { 0 };
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
    
    struct disk *disk;
    uint8_t *scratch;
    
    if (!node->read || !node->data) {
        return 0x0FFFFFF7;
    }
    
    disk = (struct disk *) node->data;
    
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
    
        if (!(fat_cache->data = kmalloc (disk->sector_size))) {
            return 0x0FFFFFF7;
        }
    
    }
    
    if (fat_cache->node != node || fat_cache->sector != sector) {
    
        if (node->read (node, fat_cache->data, (uint64_t) sector, (size_t) 1)) {
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
            
                if (node->read (node, fat_cache->data, (uint64_t) sector, (size_t) 1)) {
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

    const uint8_t *p = (const uint8_t *) *str;
    
    uint32_t uc, limit;
    uint8_t c;
    
    size_t remaining, i;
    
    if (!*p) {
        return 0;
    }
    
    uc = *p++;
    
    if (uc < 0x80) {
        *str = (const char *) p;
        return uc;
    }
    
    if (uc >= 0xC2 && uc <= 0xDF) {
        uc &= 0x1F;
        remaining = 1;
        limit = 0x80;
    } else if (uc >= 0xE0 && uc <= 0xEF) {
        uc &= 0x0F;
        remaining = 2;
        limit = 0x800;
    } else if (uc >= 0xF0 && uc <= 0xF4) {
        uc &= 0x07;
        remaining = 3;
        limit = 0x10000;
    } else {
        return 0xFFFFFFFF;
    }
    
    for (i = 0; i < remaining; i++) {
    
        c = *p++;
        
        if ((c & 0xC0) != 0x80) {
            return 0xFFFFFFFF;
        }
        
        uc = (uc << 6) | (c & 0x3F);
    
    }
    
    if (uc < limit || uc > 0x10FFFF || (uc >= 0xD800 && uc <= 0xDFFF)) {
        return 0xFFFFFFFF;
    }
    
    *str = (const char *) p;
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
    
    if (strlen (path) > 255) {
        return -ENAMETOOLONG;
    }
    
    for (;;) {
    
        uc = tchar2uni (&p);
        
        if (uc == 0xffffffff) {
            return -EINVAL;
        }
        
        if (uc >= 0x10000) {
        
            if (di + 2 > LFN_MAX) {
                return -ENAMETOOLONG;
            }
            
            uc -= 0x10000;
            
            dp->lfn[di++] = (uint16_t) (0xD800 | (uc >> 10));
            uc = 0xDC00 | (uc & 0x3FF);
        
        }
        
        wc = (uint16_t) uc;
        
        if (wc < ' ' || wc == '/' || wc == '\\') {
            break;
        }
        
        if (wc < 0x80 && chk_chr ("\"*:<>\?|\x7F", wc)) {
            return -EINVAL;
        }
        
        if (di >= LFN_MAX) {
            return -ENAMETOOLONG;
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
        return -EINVAL;
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
    uint32_t root_start, root_entries;
    
    size_t sectors_per_cluster, cluster_count, sector_size, file_size;
    
    uint16_t modified_date, modified_time;
    uint16_t accessed_date;
    
    uint8_t attributes;
    
    struct fat_cache_entry fat_cache;
    void *scratch;
    
    struct msdos_specs specs;
    
    uint64_t dir_sector;
    uint32_t dir_index;
    
    int open_flags;
    
    struct msdos_node *open_next;
    struct fs_node *open_parent;

};

static struct msdos_node *msdos_open_nodes = 0;

struct msdos_remove_entries {

    uint64_t sector[20];
    uint32_t index[20];
    
    size_t count;

};

static void msdos_track_open (struct fs_node *parent, struct msdos_node *file) {

    spin_lock (&tmp_close_lock);
    
    file->open_parent = parent;
    file->open_next = msdos_open_nodes;
    
    msdos_open_nodes = file;
    spin_unlock (&tmp_close_lock);

}

static int64_t msdos_timestamp (uint16_t date, uint16_t clock) {

    static const uint8_t month_days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    uint32_t year, month, day, hours, minutes, seconds, days = 0, y, m;
    
    if (!date) {
        return 0;
    }
    
    year = 1980 + (date >> 9);
    month = (date >> 5) & 15;
    day = date & 31;
    hours = (clock >> 11) & 31;
    minutes = (clock >> 5) & 63;
    seconds = (clock & 31) * 2;
    
    if (!month || month > 12 || !day || hours > 23 || minutes > 59 || seconds > 59) {
        return 0;
    }
    
    for (y = 1970; y < year; y++) {
        days += 365 + (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    }
    
    for (m = 1; m < month; m++) {
    
        days += month_days[m - 1];
        
        if (m == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) {
            days++;
        }
    
    }
    
    if (day > (uint32_t) month_days[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) {
        return 0;
    }
    
    days += day - 1;
    return (int64_t) days * 86400 + (int64_t) hours * 3600 + (int64_t) minutes * 60 + seconds;

}

static uint32_t msdos_get_write_fat_entry (struct fs_node *parent, struct fat_cache_entry *cache, uint32_t cluster, const struct msdos_specs *specs) {

    uint32_t offset, sector, result;
    uint8_t *scratch;
    
    if (specs->cluster_count <= MAX_CLUST_12) {
        offset = cluster + cluster / 2;
    } else if (specs->cluster_count <= MAX_CLUST_16) {
        offset = cluster * 2;
    } else {
        offset = cluster * 4;
    }
    
    sector = specs->hidden_sectors + specs->reserved_sectors + offset / 512;
    offset %= 512;
    
    if (!cache->data) {
    
        if (!(cache->data = kmalloc (512))) {
            return 0xFFFFFFFF;
        }
    
    }
    
    scratch = cache->data;
    
    if (cache->node != parent || cache->sector != sector) {
    
        cache->sector = 0xFFFFFFFF;
        
        if (parent->read (parent, scratch, (uint64_t) sector, (size_t) 1)) {
            return 0xFFFFFFFF;
        }
        
        cache->node = parent;
        cache->sector = sector;
    
    }
    
    if (specs->cluster_count <= MAX_CLUST_12) {
    
        result = scratch[offset];
        
        if (offset == 511) {
        
            cache->sector = 0xFFFFFFFF;
            
            if (parent->read (parent, scratch, (uint64_t) (sector + 1), (size_t) 1)) {
                return 0xFFFFFFFF;
            }
            
            cache->sector = sector + 1;
            result |= (uint32_t) scratch[0] << 8;
        
        } else {
            result |= (uint32_t) scratch[offset + 1] << 8;
        }
        
        return (cluster & 1) ? (result >> 4) & 0xFFF : result & 0xFFF;
    
    }
    
    if (specs->cluster_count <= MAX_CLUST_16) {
        return (uint32_t) array_to_int (scratch + offset, 2);
    }
    
    return (uint32_t) array_to_int (scratch + offset, 4) & 0x0FFFFFFF;

}

static int msdos_cluster_valid (const struct msdos_specs *specs, uint32_t cluster) {

    uint32_t limit;
    
    if (specs->cluster_count <= MAX_CLUST_12) {
        limit = 0xFF0;
    } else if (specs->cluster_count <= MAX_CLUST_16) {
        limit = 0xFFF0;
    } else {
        limit = 0x0FFFFFF0;
    }
    
    return cluster >= 2 && cluster < limit && cluster - 2 < specs->cluster_count;

}

static int msdos_end_of_chain (const struct msdos_specs *specs, uint32_t cluster) {

    if (specs->cluster_count <= MAX_CLUST_12) {
        return cluster >= 0xFF8 && cluster <= 0xFFF;
    } else if (specs->cluster_count <= MAX_CLUST_16) {
        return cluster >= 0xFFF8 && cluster <= 0xFFFF;
    }
    
    return cluster >= 0x0FFFFFF8 && cluster <= 0x0FFFFFFF;

}

static uint64_t msdos_cluster_sector (const struct msdos_specs *specs, uint32_t cluster) {
    return (uint64_t) specs->hidden_sectors + specs->data_area + (uint64_t) (cluster - 2) * specs->sectors_per_cluster;
}

static int msdos_write_supported (struct fs_node *parent, const struct msdos_specs *specs) {

    struct disk *disk;
    uint64_t fat_bytes, end;
    
    if (!parent || !specs || !parent->read || !parent->write || !(disk = (struct disk *) parent->data) || disk->sector_size != 512 || specs->bytes_per_sector != 512 || (specs->fat_flags & 0x80)) {
        return 0;
    }
    
    fat_bytes = (uint64_t) specs->cluster_count + 2;
    
    if (specs->cluster_count <= MAX_CLUST_12) {
        fat_bytes = (fat_bytes * 3 + 1) / 2;
    } else if (specs->cluster_count >= MIN_CLUST_16 && specs->cluster_count <= MAX_CLUST_16) {
        fat_bytes *= 2;
    } else if (specs->cluster_count >= MIN_CLUST_32 && specs->cluster_count <= MAX_CLUST_32) {
        fat_bytes *= 4;
    } else {
        return 0;
    }
    
    end = (uint64_t) specs->hidden_sectors + specs->data_area + (uint64_t) specs->cluster_count * specs->sectors_per_cluster;
    return specs->reserved_sectors && specs->sectors_per_cluster && specs->number_of_fats && fat_bytes <= (uint64_t) specs->sectors_per_fat * 512 && (!disk->total_sectors || end <= disk->total_sectors);

}

static void msdos_get_datetime (uint16_t *date, uint16_t *clock) {

    static const uint8_t month_days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    uint64_t seconds;
    
    uint32_t days, year, month, length;
    int leap;
    
    *date = 0x21;
    *clock = 0;
    
    if (!cmos_get_unix_time (&seconds) || seconds < 315532800ULL || seconds >= 4354819200ULL) {
        return;
    }
    
    days = (uint32_t) (seconds / 86400);
    seconds %= 86400;
    
    for (year = 1970; ; year++) {
    
        leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
        length = 365 + leap;
        
        if (days < length) {
            break;
        }
        
        days -= length;
    
    }
    
    for (month = 1; month <= 12; month++) {
    
        length = month_days[month - 1] + (month == 2 && leap);
        
        if (days < length) {
            break;
        }
        
        days -= length;
    
    }
    
    *date = (uint16_t) (((year - 1980) << 9) | (month << 5) | (days + 1));
    *clock = (uint16_t) (((seconds / 3600) << 11) | (((seconds / 60) % 60) << 5) | ((seconds % 60) / 2));

}

static int msdos_invalidate_fsinfo (struct fs_node *parent, const struct msdos_specs *specs) {

    uint8_t scratch[512];
    uint32_t sector, i;
    
    if (specs->cluster_count < MIN_CLUST_32 || !specs->info_sector || specs->info_sector >= specs->reserved_sectors) {
        return 0;
    }
    
    for (i = 0; i < 2; i++) {
    
        sector = specs->info_sector;
        
        if (i) {
        
            if (!specs->backup_boot || specs->backup_boot >= specs->reserved_sectors - sector) {
                break;
            }
            
            sector += specs->backup_boot;
        
        }
        
        sector += specs->hidden_sectors;
        
        if (parent->read (parent, scratch, (uint64_t) sector, (size_t) 1)) {
            return -EIO;
        }
        
        if (array_to_int (scratch, 4) == 0x41615252UL && array_to_int (scratch + 484, 4) == 0x61417272UL && array_to_int (scratch + 508, 4) == 0xAA550000UL) {
        
            if (array_to_int (scratch + 488, 4) != 0xFFFFFFFFUL || array_to_int (scratch + 492, 4) != 0xFFFFFFFFUL) {
            
                memset (scratch + 488, 0xFF, 8);
                
                if (parent->write (parent, scratch, (uint64_t) sector, (size_t) 1)) {
                    return -EIO;
                }
            
            }
        
        }
    
    }
    
    return 0;

}

static int msdos_set_fat_entry (struct fs_node *parent, const struct msdos_specs *specs, uint32_t cluster, uint32_t value) {

    uint8_t scratch[1024];
    uint32_t offset, entries, sector, i, width, count, old, encoded;
    
    if (!msdos_cluster_valid (specs, cluster)) {
        return -EIO;
    }
    
    if (specs->cluster_count <= MAX_CLUST_12) {
    
        offset = cluster + cluster / 2;
        width = 2;
    
    } else if (specs->cluster_count <= MAX_CLUST_16) {
    
        offset = cluster * 2;
        width = 2;
    
    } else {
    
        offset = cluster * 4;
        width = 4;
    
    }
    
    entries = offset / 512;
    offset %= 512;
    count = offset + width > 512 ? 2 : 1;
    
    if (entries >= specs->sectors_per_fat || count > specs->sectors_per_fat - entries) {
        return -EIO;
    }
    
    for (i = 0; i < specs->number_of_fats; i++) {
    
        sector = specs->hidden_sectors + specs->reserved_sectors + i * specs->sectors_per_fat + entries;
        
        if (parent->read (parent, scratch, (uint64_t) sector, (size_t) count)) {
            return -EIO;
        }
        
        old = (uint32_t) array_to_int (scratch + offset, (int) width);
        encoded = value;
        
        if (specs->cluster_count <= MAX_CLUST_12) {
            encoded = cluster & 1 ? (old & 0x000F) | ((value & 0xFFF) << 4) : (old & 0xF000) | (value & 0xFFF);
        } else if (specs->cluster_count >= MIN_CLUST_32) {
            encoded = (old & 0xF0000000UL) | (value & 0x0FFFFFFFUL);
        }
        
        int_to_array (scratch + offset, encoded, (int) width);
        
        if (parent->write (parent, scratch, (uint64_t) sector, (size_t) count)) {
            return -EIO;
        }
    
    }
    
    return 0;

}

static int msdos_allocate_cluster (struct fs_node *parent, const struct msdos_specs *specs, struct fat_cache_entry *cache, uint32_t *result) {

    uint8_t scratch[512];
    
    uint32_t cluster, value, i;
    uint64_t sector;
    
    cache->sector = 0xFFFFFFFF;
    
    for (cluster = 2; msdos_cluster_valid (specs, cluster); cluster++) {
    
        value = msdos_get_write_fat_entry (parent, cache, cluster, specs);
        
        if (value == 0xFFFFFFFF) {
            return -EIO;
        }
        
        if (!value) {
            break;
        }
    
    }
    
    if (!msdos_cluster_valid (specs, cluster)) {
        return -ENOSPC;
    }
    
    if (msdos_invalidate_fsinfo (parent, specs)) {
        return -EIO;
    }
    
    memset (scratch, 0, sizeof (scratch));
    sector = msdos_cluster_sector (specs, cluster);
    
    for (i = 0; i < specs->sectors_per_cluster; i++) {
    
        if (parent->write (parent, scratch, (uint64_t) (sector + i), (size_t) 1)) {
            return -EIO;
        }
    
    }
    
    if (msdos_set_fat_entry (parent, specs, cluster, 0x0FFFFFFF)) {
        return -EIO;
    }
    
    cache->sector = 0xFFFFFFFF;
    *result = cluster;
    
    return 0;

}

static int msdos_free_chain (struct fs_node *parent, const struct msdos_specs *specs, struct fat_cache_entry *cache, uint32_t cluster) {

    uint32_t next, i;
    
    if (!cluster) {
        return 0;
    }
    
    if (msdos_invalidate_fsinfo (parent, specs)) {
        return -EIO;
    }
    
    for (i = 0; i < specs->cluster_count && msdos_cluster_valid (specs, cluster); i++) {
    
        cache->sector = 0xFFFFFFFF;
        next = msdos_get_write_fat_entry (parent, cache, cluster, specs);
        
        if (!msdos_end_of_chain (specs, next) && !msdos_cluster_valid (specs, next)) {
            return -EIO;
        }
        
        if (msdos_set_fat_entry (parent, specs, cluster, 0)) {
            return -EIO;
        }
        
        if (msdos_end_of_chain (specs, next)) {
        
            cache->sector = 0xFFFFFFFF;
            return 0;
        
        }
        
        cluster = next;
    
    }
    
    return -EIO;

}

static int msdos_update_entry (struct fs_node *parent, struct msdos_node *this, uint32_t cluster, uint32_t size) {

    uint8_t scratch[512];
    
    struct msdos_dirent *de;
    uint16_t date, clock;
    
    if (parent->read (parent, scratch, (uint64_t) this->dir_sector, (size_t) 1)) {
        return -EIO;
    }
    
    de = &((struct msdos_dirent *) scratch)[this->dir_index];
    
    if (!de->name[0] || de->name[0] == 0xE5 || (de->attr & (ATTR_DIR | ATTR_VOLUME_ID | ATTR_READONLY))) {
        return -EIO;
    }
    
    msdos_get_datetime (&date, &clock);
    
    int_to_array (de->startlo, cluster, 2);
    int_to_array (de->starthi, cluster >> 16, 2);
    int_to_array (de->size, size, 4);
    int_to_array (de->date, date, 2);
    int_to_array (de->xtime, clock, 2);
    
    de->attr |= ATTR_ARCHIVE;
    
    if (parent->write (parent, scratch, (uint64_t) this->dir_sector, (size_t) 1)) {
        return -EIO;
    }
    
    this->start_cluster = cluster;
    this->file_size = size;
    this->modified_date = date;
    this->modified_time = clock;
    this->attributes = de->attr;
    
    return 0;

}

static int msdos_refresh_entry (struct fs_node *parent, struct msdos_node *this) {

    uint8_t scratch[512];
    struct msdos_dirent *de;
    
    if (this->specs.bytes_per_sector != 512 || ((struct disk *) parent->data)->sector_size != 512) {
        return 0;
    }
    
    if (parent->read (parent, scratch, (uint64_t) this->dir_sector, (size_t) 1)) {
        return -EIO;
    }
    
    de = &((struct msdos_dirent *) scratch)[this->dir_index];
    
    if (!de->name[0] || de->name[0] == 0xE5 || (de->attr & (ATTR_DIR | ATTR_VOLUME_ID))) {
        return -EIO;
    }
    
    this->file_size = (uint32_t) array_to_int (de->size, 4);
    this->start_cluster = ((uint32_t) array_to_int (de->starthi, 2) << 16 | (uint32_t) array_to_int (de->startlo, 2)) & 0x0FFFFFFF;
    this->modified_date = (uint16_t) array_to_int (de->date, 2);
    this->modified_time = (uint16_t) array_to_int (de->xtime, 2);
    this->accessed_date = (uint16_t) array_to_int (de->adate, 2);
    this->attributes = de->attr;
    this->fat_cache.sector = 0xFFFFFFFF;
    
    return 0;

}

static int msdos_zero_range (struct fs_node *parent, const struct msdos_specs *specs, uint32_t cluster, size_t offset, size_t end) {

    uint8_t scratch[512];
    uint64_t sector;
    
    size_t start, count;
    
    while (offset < end) {
        
        sector = msdos_cluster_sector (specs, cluster) + offset / 512;
        start = offset % 512;
        count = MIN (512 - start, end - offset);
        
        if (start || count != 512) {
            
            if (parent->read (parent, scratch, sector, (size_t) 1)) {
                return -EIO;
            }
        
        }
        
        memset (scratch + start, 0, count);
        
        if (parent->write (parent, scratch, sector, (size_t) 1)) {
            return -EIO;
        }
        
        offset += count;
    
    }
    
    return 0;

}

static int msdos_truncate (void *data, off_t length) {

    struct fs_node *node = (struct fs_node *) data;
    struct fs_node *parent;
    
    struct msdos_node *this, *open;
    const struct msdos_specs *specs;
    
    uint32_t cluster, next, last = 0, keep = 0, tail = 0;
    uint32_t first_new = 0, last_new = 0, size, old_size;
    
    size_t required, used, i = 0, offset, end;
    int ret;
    
    if (!node || !(this = (struct msdos_node *) node->data) || !(parent = node->parent)) {
        return -EIO;
    }
    
    if (length < 0) {
        return -EINVAL;
    }
    
    if ((this->open_flags & VFS_O_ACCMODE) == VFS_O_RDONLY) {
        return -EBADF;
    }
    
    specs = &this->specs;
    size = (uint32_t) length;
    
    spin_lock (&tmp_open_lock);
    spin_lock (&tmp_read_lock);
    spin_lock (&tmp_seek_lock);
    
    if (!msdos_write_supported (parent, specs)) {
        
        ret = -EROFS;
        goto done;
    
    }
    
    if ((ret = msdos_refresh_entry (parent, this))) {
        goto done;
    }
    
    if (this->attributes & ATTR_READONLY) {
        
        ret = -EACCES;
        goto done;
    
    }
    
    old_size = (uint32_t) this->file_size;
    
    if (size == old_size) {
        goto done;
    }
    
    if (!this->sector_size) {
        
        ret = -EIO;
        goto done;
    
    }
    
    required = size / this->sector_size + (size % this->sector_size != 0);
    used = old_size / this->sector_size + (old_size % this->sector_size != 0);
    
    if (required > specs->cluster_count) {
        
        ret = -ENOSPC;
        goto done;
    
    }
    
    cluster = this->start_cluster;
    
    while (cluster) {
        
        if (i >= specs->cluster_count || !msdos_cluster_valid (specs, cluster)) {
            
            ret = -EIO;
            goto done;
        
        }
        
        if (i < required) {
            keep = cluster;
        } else if (i == required) {
            tail = cluster;
        }
        
        last = cluster;
        i++;
        
        next = msdos_get_write_fat_entry (parent, &this->fat_cache, cluster, specs);
        
        if (msdos_end_of_chain (specs, next)) {
            break;
        }
        
        if (!msdos_cluster_valid (specs, next)) {
            
            ret = -EIO;
            goto done;
        
        }
        
        cluster = next;
    
    }
    
    if (i < used) {
        
        ret = -EIO;
        goto done;
    
    }
    
    if (size < old_size) {
        
        if ((ret = msdos_update_entry (parent, this, required ? this->start_cluster : 0, size))) {
            goto done;
        }
        
        if (tail) {
            
            if (keep) {
                ret = msdos_set_fat_entry (parent, specs, keep, 0x0FFFFFFF);
            }
            
            if (!ret) {
                ret = msdos_free_chain (parent, specs, &this->fat_cache, tail);
            }
        
        }
        
        goto sync;
    
    }
    
    cluster = this->start_cluster;
    
    for (used = 0; used < i && used < required; used++) {
        
        if (used >= old_size / this->sector_size) {
            
            offset = used == old_size / this->sector_size ? old_size % this->sector_size : 0;
            end = MIN (this->sector_size, size - used * this->sector_size);
            
            if ((ret = msdos_zero_range (parent, specs, cluster, offset, end))) {
                goto done;
            }
        
        }
        
        cluster = msdos_get_write_fat_entry (parent, &this->fat_cache, cluster, specs);
    
    }
    
    while (i < required) {
        
        if ((ret = msdos_allocate_cluster (parent, specs, &this->fat_cache, &next))) {
            goto failed;
        }
        
        if (last_new) {
            
            if ((ret = msdos_set_fat_entry (parent, specs, last_new, next))) {
                
                if (!msdos_set_fat_entry (parent, specs, last_new, 0x0FFFFFFF)) {
                    msdos_free_chain (parent, specs, &this->fat_cache, next);
                }
                
                goto failed;
            
            }
            
        } else {
            first_new = next;
        }
        
        last_new = next;
        i++;
    
    }
    
    if (last && first_new) {
        
        if ((ret = msdos_set_fat_entry (parent, specs, last, first_new))) {
            goto detach;
        }
    
    }
    
    if ((ret = msdos_update_entry (parent, this, last ? this->start_cluster : first_new, size))) {
        goto detach;
    }
    
sync:
    
    spin_lock (&tmp_close_lock);
    
    for (open = msdos_open_nodes; open; open = open->open_next) {
        
        if (open->open_parent == parent && open->dir_sector == this->dir_sector && open->dir_index == this->dir_index) {
            
            open->start_cluster = this->start_cluster;
            open->file_size = this->file_size;
            open->modified_date = this->modified_date;
            open->modified_time = this->modified_time;
            open->attributes = this->attributes;
            open->fat_cache.sector = 0xFFFFFFFF;
        
        }
    
    }
    
    spin_unlock (&tmp_close_lock);
    goto done;
    
detach:
    
    if (last && first_new && msdos_set_fat_entry (parent, specs, last, 0x0FFFFFFF)) {
        goto done;
    }
    
failed:
    
    if (first_new) {
        msdos_free_chain (parent, specs, &this->fat_cache, first_new);
    }
    
done:
    
    this->fat_cache.sector = 0xFFFFFFFF;
    
    spin_unlock (&tmp_seek_lock);
    spin_unlock (&tmp_read_lock);
    spin_unlock (&tmp_open_lock);
    
    return ret;

}

static int msdos_write (void *data, void *buf, size_t count) {

    struct fs_node *node = (struct fs_node *) data;
    struct fs_node *parent;
    
    struct msdos_node *this;
    const struct msdos_specs *specs;
    
    uint8_t scratch[512];
    
    uint64_t sector;
    uint32_t cluster, next, skip, offset, size;
    
    size_t copy, written = 0;
    int ret = -5;
    
    if (!node || !(this = (struct msdos_node *) node->data) || !(parent = node->parent)) {
        return -EIO;
    }
    
    if ((this->open_flags & VFS_O_ACCMODE) == VFS_O_RDONLY) {
        return -EBADF;
    }
    
    if (!count) {
        return 0;
    }
    
    specs = &this->specs;
    
    spin_lock (&tmp_open_lock);
    spin_lock (&tmp_read_lock);
    spin_lock (&tmp_seek_lock);
    
    if (!msdos_write_supported (parent, specs)) {
    
        ret = -EROFS;
        goto done;
    
    }
    
    if ((ret = msdos_refresh_entry (parent, this))) {
        goto done;
    }
    
    if (this->attributes & ATTR_READONLY) {
    
        ret = -EACCES;
        goto done;
    
    }
    
    if (this->open_flags & VFS_O_APPEND) {
        this->file_offset = (uint32_t) this->file_size;
    }
    
    if (this->file_offset > this->file_size || count > 0xFFFFFFFFUL - this->file_offset) {
    
        ret = -EFBIG;
        goto done;
    
    }
    
    if (count > 0x7FFFFFFF) {
        count = 0x7FFFFFFF;
    }
    
    cluster = this->start_cluster;
    
    if (!cluster) {
    
        if (this->file_size) {
            goto done;
        }
        
        ret = msdos_allocate_cluster (parent, specs, &this->fat_cache, &cluster);
        
        if (ret) {
            goto done;
        }
        
        if ((ret = msdos_update_entry (parent, this, cluster, 0))) {
        
            msdos_set_fat_entry (parent, specs, cluster, 0);
            goto done;
        
        }
    
    }
    
    skip = this->file_offset / this->sector_size;
    
    for (;;) {
    
        if (!msdos_cluster_valid (specs, cluster)) {
        
            ret = -EIO;
            break;
        
        }
        
        if (!skip) {
        
            offset = this->file_offset % this->sector_size;
            sector = msdos_cluster_sector (specs, cluster) + offset / 512;
            
            offset %= 512;
            copy = MIN (512 - offset, count - written);
            
            if (parent->read (parent, scratch, (uint64_t) sector, (size_t) 1)) {
            
                ret = -5;
                break;
            
            }
            
            memcpy (scratch + offset, (uint8_t *) buf + written, copy);
            
            if (parent->write (parent, scratch, (uint64_t) sector, (size_t) 1)) {
            
                ret = -EIO;
                break;
            
            }
            
            size = MAX ((uint32_t) this->file_size, this->file_offset + (uint32_t) copy);
            
            if ((ret = msdos_update_entry (parent, this, this->start_cluster, size))) {
                break;
            }
            
            written += copy;
            this->file_offset += (uint32_t) copy;
            
            if (written == count) {
                break;
            }
            
            if (this->file_offset % this->sector_size) {
                continue;
            }
        
        } else {
            skip--;
        }
        
        next = msdos_get_write_fat_entry (parent, &this->fat_cache, cluster, specs);
        
        if (msdos_end_of_chain (specs, next)) {
        
            if (skip || this->file_offset < this->file_size) {
            
                ret = -EIO;
                break;
            
            }
            
            if ((ret = msdos_allocate_cluster (parent, specs, &this->fat_cache, &next))) {
                break;
            }
            
            if ((ret = msdos_set_fat_entry (parent, specs, cluster, next))) {
            
                break;
            
            }
            
            this->fat_cache.sector = 0xFFFFFFFF;
        
        }
        
        cluster = next;
    
    }
    
done:
    
    spin_unlock (&tmp_seek_lock);
    spin_unlock (&tmp_read_lock);
    spin_unlock (&tmp_open_lock);
    
    return written ? (int) written : ret;

}

static int msdos_encode_timestamp (int64_t timestamp, uint16_t *date, uint16_t *clock) {

    static const uint8_t month_days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    uint64_t seconds;
    
    uint32_t days, year, month, length;
    int leap;
    
    if (timestamp < 315532800LL || timestamp >= 4354819200LL) {
        return -EOVERFLOW;
    }
    
    seconds = (uint64_t) timestamp;
    days = (uint32_t) (seconds / 86400);
    seconds %= 86400;
    
    for (year = 1970; ; year++) {
    
        leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
        length = 365 + leap;
        
        if (days < length) {
            break;
        }
        
        days -= length;
    
    }
    
    for (month = 1; month <= 12; month++) {
    
        length = month_days[month - 1] + (month == 2 && leap);
        
        if (days < length) {
            break;
        }
        
        days -= length;
    
    }
    
    *date = (uint16_t) (((year - 1980) << 9) | (month << 5) | (days + 1));
    *clock = (uint16_t) (((seconds / 3600) << 11) | (((seconds / 60) % 60) << 5) | ((seconds % 60) / 2));
    
    return 0;

}

static int msdos_utime (void *data, int64_t atime, int64_t mtime) {

    struct fs_node *node = (struct fs_node *) data, *parent;
    struct msdos_node *this;
    
    struct msdos_dirent *de;
    uint8_t scratch[512];
    
    uint16_t adate, aclock, date, clock;
    int ret = -5;
    
    if (!node || !(this = (struct msdos_node *) node->data) || !(parent = node->parent)) {
        return -EIO;
    }
    
    if (!msdos_write_supported (parent, &this->specs)) {
        return -EROFS;
    }
    
    if (msdos_encode_timestamp ((int64_t) atime, &adate, &aclock) || msdos_encode_timestamp ((int64_t) mtime, &date, &clock)) {
        return -EOVERFLOW;
    }
    
    spin_lock (&tmp_open_lock);
    spin_lock (&tmp_read_lock);
    
    if (parent->read (parent, scratch, (uint64_t) this->dir_sector, (size_t) 1)) {
        goto done;
    }
    
    de = &((struct msdos_dirent *) scratch)[this->dir_index];
    
    if (!de->name[0] || de->name[0] == 0xE5 || (de->attr & ATTR_VOLUME_ID)) {
        goto done;
    }
    
    if (de->attr & ATTR_READONLY) {
    
        ret = -EACCES;
        goto done;
    
    }
    
    int_to_array (de->adate, adate, 2);
    int_to_array (de->date, date, 2);
    int_to_array (de->xtime, clock, 2);
    
    if (parent->write (parent, scratch, (uint64_t) this->dir_sector, (size_t) 1)) {
        goto done;
    }
    
    this->accessed_date = adate;
    this->modified_date = date;
    this->modified_time = clock;
    
    ret = 0;

done:
    
    spin_unlock (&tmp_read_lock);
    spin_unlock (&tmp_open_lock);
    
    return ret;

}

static int msdos_statfs (void *data, struct vfs_statfs *st) {
    
    struct fs_node *node = (struct fs_node *) data;
    struct fat_cache_entry cache = { 0 };
    
    struct msdos_node *this;
    const struct msdos_specs *specs;
    
    uint32_t cluster, entry, free_clusters = 0;
    int ret = 0;
    
    if (!node || !(this = (struct msdos_node *) node->data) || !node->parent || !node->parent->read) {
        return -EIO;
    }
    
    specs = &this->specs;
    
    spin_lock (&tmp_read_lock);
    
    for (cluster = 2; cluster < specs->cluster_count + 2; cluster++) {
    
        entry = msdos_get_write_fat_entry (node->parent, &cache, cluster, specs);
        
        if (entry == 0xFFFFFFFF) {
        
            ret = -EIO;
            break;
        
        }
        
        if (!entry) {
            free_clusters++;
        }
    
    }
    
    if (cache.data) {
        kfree (cache.data);
    }
    
    spin_unlock (&tmp_read_lock);
    
    if (ret) {
        return ret;
    }
    
    st->type = 0x4D44;
    st->bsize = specs->bytes_per_sector;
    st->frsize = (uint64_t) specs->bytes_per_sector * specs->sectors_per_cluster;
    st->blocks = specs->cluster_count;
    st->bfree = free_clusters;
    st->bavail = free_clusters;
    st->namelen = LFN_MAX;
    
    return 0;

}

static int msdos_stat (void *data, struct stat *st) {

    struct fs_node *node = (struct fs_node *) data;
    struct fat_cache_entry cache = { 0 };
    
    struct msdos_node *this;
    const struct msdos_specs *specs;
    
    uint32_t cluster, next;
    uint64_t blocks = 0;
    
    size_t i;
    int complete = 0;
    
    if (!node || !(this = (struct msdos_node *) node->data)) {
        return -EIO;
    }
    
    specs = &this->specs;
    
    st->st_size = node->flags == INO_DIR ? 0 : (int64_t) this->file_size;
    st->st_blksize = this->sector_size ? (int64_t) this->sector_size : 512;
    
    if (node->flags == INO_DIR && !this->start_cluster && specs->root_entries) {
        blocks = ((uint64_t) specs->root_entries * sizeof (struct msdos_dirent) + 511) / 512;
    } else if (this->start_cluster) {
    
        if (!node->parent || !node->parent->read) {
            return -EIO;
        }
        
        cluster = this->start_cluster;
        
        for (i = 0; i < specs->cluster_count && msdos_cluster_valid (specs, cluster); i++) {
        
            blocks += ((uint64_t) specs->bytes_per_sector * specs->sectors_per_cluster + 511) / 512;
            next = msdos_get_write_fat_entry (node->parent, &cache, cluster, specs);
            
            if (msdos_end_of_chain (specs, next)) {
            
                complete = 1;
                break;
            
            }
            
            cluster = next;
        
        }
        
        if (cache.data) {
            kfree (cache.data);
        }
        
        if (!complete) {
            return -EIO;
        }
    
    }
    
    st->st_blocks = (int64_t) blocks;
    
    st->st_mtime = msdos_timestamp (this->modified_date, this->modified_time);
    st->st_atime = msdos_timestamp (this->accessed_date, 0);
    st->st_ctime = st->st_mtime;
    
    if (this->attributes & ATTR_READONLY) {
        st->st_mode &= (mode_t) ~0222;
    }
    
    return 0;

}

static off_t msdos_seek (void *data, off_t offset, int whence) {

    struct fs_node *node = 0;
    struct msdos_node *this = 0;
    
    int64_t next;
    spin_lock (&tmp_seek_lock);
    
    if (!(node = (struct fs_node *) data)) {
    
        spin_unlock (&tmp_seek_lock);
        return -EINVAL;
    
    }
    
    if (!(this = (struct msdos_node *) node->data)) {
    
        spin_unlock (&tmp_seek_lock);
        return -EINVAL;
    
    }
    
    if (whence == SEEK_SET) {
        next = offset;
    } else if (whence == SEEK_CUR) {
        next = (int64_t) this->file_offset + offset;
    } else if (whence == SEEK_END) {
        next = (int64_t) this->file_size + offset;
    } else {
    
        spin_unlock (&tmp_seek_lock);
        return -EINVAL;
    
    }
    
    if (next < 0 || ((uint64_t) next > this->file_size && (whence != SEEK_CUR || offset))) {
    
        spin_unlock (&tmp_seek_lock);
        return -EINVAL;
    
    }
    
    if (next > 0x7FFFFFFFLL) {
    
        spin_unlock (&tmp_seek_lock);
        return -EOVERFLOW;
    
    }
    
    this->file_offset = (uint32_t) next;
    
    if (this->fat_cache.data) {
    
        kfree (this->fat_cache.data);
        this->fat_cache.data = 0;
    
    }
    
    this->fat_cache.sector = 0xFFFFFFFF;
    
    spin_unlock (&tmp_seek_lock);
    return this->file_offset;

}

static int msdos_read (void *data, void *buf, size_t count) {

    struct fs_node *parent = 0, *node = 0;
    
    struct msdos_node *this = 0;
    struct disk *disk = 0;
    
    uint32_t offset = 0, sector = 0, current_cluster = 0;
    size_t clusters = 0, sectors, remaining, copy;
    
    int ret = 0, first = 1;
    spin_lock (&tmp_read_lock);
    
    if (!(node = (struct fs_node *) data)) {
    
        spin_unlock (&tmp_read_lock);
        return -EINVAL;
    
    }
    
    if (!(parent = (struct fs_node *) node->parent)) {
    
        spin_unlock (&tmp_read_lock);
        return -EIO;
    
    }
    
    if (!parent->read) {
    
        spin_unlock (&tmp_read_lock);
        return -EIO;
    
    }
    
    if (!(disk = (struct disk *) parent->data)) {
    
        spin_unlock (&tmp_read_lock);
        return -EIO;
    
    }
    
    if (!(this = (struct msdos_node *) node->data)) {
    
        spin_unlock (&tmp_read_lock);
        return -EIO;
    
    }
    
    if ((this->open_flags & VFS_O_ACCMODE) == VFS_O_WRONLY) {
    
        spin_unlock (&tmp_read_lock);
        return -EBADF;
    
    }
    
    if (this->file_offset >= this->file_size || !count) {
    
        spin_unlock (&tmp_read_lock);
        return 0;
    
    }
    
    current_cluster = this->start_cluster;
    
    reserved_sectors = this->reserved_sectors;
    hidden_sectors   = this->hidden_sectors;
    cluster_count    = this->cluster_count;
    
    clusters = this->file_offset / this->sector_size;
    
    while (clusters--) {
    
        if ((current_cluster = get_fat_entry (parent, &this->fat_cache, current_cluster)) >= 0x0FFFFFF7) {
        
            spin_unlock (&tmp_read_lock);
            return -EIO;
        
        }
    
    }
    
    sectors = 1;
    
    if (this->sector_size > disk->sector_size) {
        sectors = this->sector_size / disk->sector_size;
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
                    return -EIO;
                
                }
            
            }
            
            first = 0;
            
            sector = (uint64_t) this->data_area + ((current_cluster - 2) * this->sectors_per_cluster);
            sector += this->hidden_sectors;
            
            if (parent->read (parent, this->scratch, (uint64_t) sector, (size_t) sectors)) {
            
                spin_unlock (&tmp_read_lock);
                return -EIO;
            
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
    return -EIO;

}

static void msdos_append_utf8 (char *name, size_t *length, uint32_t code) {

    uint8_t bytes[4];
    size_t count, i;
    
    if (code < 0x80) {
    
        bytes[0] = (uint8_t) code;
        count = 1;
    
    } else if (code < 0x800) {
    
        bytes[0] = (uint8_t) (0xC0 | (code >> 6));
        bytes[1] = (uint8_t) (0x80 | (code & 0x3F));
        
        count = 2;
    
    } else if (code < 0x10000) {
    
        bytes[0] = (uint8_t) (0xE0 | (code >> 12));
        bytes[1] = (uint8_t) (0x80 | ((code >> 6) & 0x3F));
        bytes[2] = (uint8_t) (0x80 | (code & 0x3F));
        
        count = 3;
    
    } else {
    
        bytes[0] = (uint8_t) (0xF0 | (code >> 18));
        bytes[1] = (uint8_t) (0x80 | ((code >> 12) & 0x3F));
        bytes[2] = (uint8_t) (0x80 | ((code >> 6) & 0x3F));
        bytes[3] = (uint8_t) (0x80 | (code & 0x3F));
        
        count = 4;
    
    }
    
    if (*length + count > 255) {
        return;
    }
    
    for (i = 0; i < count; i++) {
    
        name[*length] = (char) bytes[i];
        (*length)++;
    
    }
    
    name[*length] = '\0';

}

static int msdos_readdir (void *data, uint32_t index, struct dirent *entry) {

    struct fs_node *node = (struct fs_node *) data;
    
    struct msdos_node *this;
    struct fs_node *parent;
    struct disk *disk;
    
    struct fat_cache_entry cache = { 0 };
    struct msdos_dirent *fat_entry;
    
    uint16_t long_name[LFN_MAX + 1];
    
    uint32_t raw, sector, previous_sector = 0xFFFFFFFF;
    uint32_t cluster, cluster_index = 0, requested_cluster, end_cluster;
    uint32_t entries_per_sector, sector_index, i, code;
    
    uint8_t order = 0, checksum = 0, valid = 0;
    uint8_t *scratch = 0;
    
    size_t length, j;
    int result = 0;
    
    if (!node || !(this = (struct msdos_node *) node->data) || !(node->flags & INO_DIR)) {
        return -ENOTDIR;
    }
    
    if (index < 2) {
    
        strcpy (entry->d_name, index ? ".." : ".");
        
        entry->d_ino = index;
        entry->d_off = index + 1;
        entry->d_type = DT_DIR;
        
        return 1;
    
    }
    
    parent = node->parent;
    
    if (!parent || !parent->read || !(disk = (struct disk *) parent->data)) {
        return -EIO;
    }
    
    entries_per_sector = disk->sector_size / sizeof (struct msdos_dirent);
    
    if (!entries_per_sector || !this->sectors_per_cluster) {
        return -EIO;
    }
    
    spin_lock (&tmp_open_lock);
    
    if (!(scratch = kmalloc (disk->sector_size))) {
    
        result = -ENOMEM;
        goto done;
    
    }
    
    reserved_sectors = (uint32_t) this->reserved_sectors;
    hidden_sectors = this->hidden_sectors;
    cluster_count = this->cluster_count;
    cache.sector = 0xFFFFFFFF;
    cluster = this->start_cluster;
    
    if (this->cluster_count <= MAX_CLUST_12) {
        end_cluster = 0xFF7;
    } else if (this->cluster_count <= MAX_CLUST_16) {
        end_cluster = 0xFFF7;
    } else {
        end_cluster = 0x0FFFFFF7;
    }
    
    for (raw = index - 2; ; raw++) {
    
        sector_index = raw / entries_per_sector;
        
        if (this->root_entries && this->start_cluster == 0) {
        
            if (raw >= this->root_entries) {
                break;
            }
            
            sector = this->root_start + sector_index + this->hidden_sectors;
        
        } else {
        
            requested_cluster = sector_index / (uint32_t) this->sectors_per_cluster;
            
            while (cluster_index < requested_cluster) {
            
                if (cluster < 2 || cluster >= end_cluster) {
                    goto done;
                }
                
                cluster = get_fat_entry (parent, &cache, cluster);
                cluster_index++;
            
            }
            
            if (cluster < 2 || cluster >= end_cluster) {
                break;
            }
            
            sector = this->data_area + ((cluster - 2) * (uint32_t) this->sectors_per_cluster);
            sector += (sector_index % (uint32_t) this->sectors_per_cluster) + this->hidden_sectors;
        
        }
        
        if (sector != previous_sector) {
        
            if (parent->read (parent, scratch, (uint64_t) sector, (size_t) 1)) {
            
                result = -EIO;
                goto done;
            
            }
            
            previous_sector = sector;
        
        }
        
        fat_entry = &((struct msdos_dirent *) scratch)[raw % entries_per_sector];
        
        if (fat_entry->name[0] == 0) {
            break;
        }
        
        if (fat_entry->name[0] == 0xE5) {
        
            valid = 0;
            continue;
        
        }
        
        if (fat_entry->attr == ATTR_LONG_NAME) {
        
            uint8_t ordinal = fat_entry->name[0] & 0x1F;
            
            if (fat_entry->name[0] & 0x40) {
            
                valid = ordinal > 0 && ordinal <= 20;
                order = ordinal;
                checksum = fat_entry->csum;
                
                memset (long_name, 0xFF, sizeof (long_name));
            
            }
            
            if (!valid || ordinal != order || checksum != fat_entry->csum || ld_word ((uint8_t *) fat_entry + 26) != 0) {
            
                valid = 0;
                continue;
            
            }
            
            for (i = 0; i < 13 && (ordinal - 1) * 13 + i < LFN_MAX; i++) {
                long_name[(ordinal - 1) * 13 + i] = ld_word ((uint8_t *) fat_entry + lfn_ofs[i]);
            }
            
            order--;
            continue;
        
        }
        
        if (fat_entry->attr & ATTR_VOLUME_ID) {
        
            valid = 0;
            continue;
        
        }
        
        entry->d_name[0] = '\0';
        length = 0;
        
        if (valid && !order && checksum == sum_sfn (fat_entry->name)) {
        
            for (i = 0; i < LFN_MAX; i++) {
            
                code = long_name[i];
                
                if (!code || code == 0xFFFF) {
                    break;
                }
                
                if (code >= 0xD800 && code <= 0xDBFF && i + 1 < LFN_MAX && long_name[i + 1] >= 0xDC00 && long_name[i + 1] <= 0xDFFF) {
                    code = 0x10000 + ((code - 0xD800) << 10) + (long_name[++i] - 0xDC00);
                } else if (code >= 0xD800 && code <= 0xDFFF) {
                    code = '?';
                }
                
                msdos_append_utf8 (entry->d_name, &length, code);
            
            }
        
        }
        
        if (!length) {
        
            for (j = 0; j < 8 && fat_entry->name[j] != ' '; j++) {
            
                code = fat_entry->name[j];
                
                if (j == 0 && code == 0x05) {
                    code = 0xE5;
                }
                
                code = oem2uni ((uint16_t) code, 437);
                
                if (code >= 'A' && code <= 'Z' && (fat_entry->type & 0x08)) {
                    code += 'a' - 'A';
                }
                
                msdos_append_utf8 (entry->d_name, &length, code);
            
            }
            
            if (fat_entry->name[8] != ' ') {
            
                msdos_append_utf8 (entry->d_name, &length, '.');
                
                for (j = 8; j < 11 && fat_entry->name[j] != ' '; j++) {
                
                    code = oem2uni (fat_entry->name[j], 437);
                    
                    if (code >= 'A' && code <= 'Z' && (fat_entry->type & 0x10)) {
                        code += 'a' - 'A';
                    }
                    
                    msdos_append_utf8 (entry->d_name, &length, code);
                
                }
            
            }
        
        }
        
        if (!strcmp (entry->d_name, ".") || !strcmp (entry->d_name, "..")) {
        
            valid = 0;
            continue;
        
        }
        
        entry->d_ino = raw + 2;
        entry->d_off = raw + 3;
        
        entry->d_type = (fat_entry->attr & ATTR_DIR) ? DT_DIR : DT_REG;
        
        result = 1;
        break;
    
    }

done:

    if (cache.data) {
        kfree (cache.data);
    }
    
    if (scratch) {
        kfree (scratch);
    }
    
    spin_unlock (&tmp_open_lock);
    return result;

}

static int msdos_close (void *data) {

    struct fs_node *node = 0;
    struct msdos_node *this = 0, **entry;
    
    /* Lock the system so that other cores don't interupt us. */
    spin_lock (&tmp_close_lock);
    
    /* Check if data was not NULL. */
    if (!(node = (struct fs_node *) data)) {
    
        spin_unlock (&tmp_close_lock);
        return 0;
    
    }
    
    /* Have we got child data?? */
    if ((this = (struct msdos_node *) node->data)) {
    
        for (entry = &msdos_open_nodes; *entry; entry = &(*entry)->open_next) {
        
            if (*entry == this) {
            
                *entry = this->open_next;
                break;
            
            }
        
        }
        
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
    return 0;

}

static struct fs_node *msdos_open_internal (void *data, const char *path, size_t depth, int *error, struct msdos_remove_entries *entries) {

    struct fs_node *root_node = 0, *parent = 0, *node = 0;
    int ret;
    
    struct msdos_specs *this = 0;
    uint64_t offset = 0;
    
    struct fat_dirent *dp = 0;
    struct msdos_dirent *de;
    
    size_t path_depth, sector_size;
    size_t no_ents, i;
    
    uint32_t current_cluster, end_cluster;
    uint8_t a, ord, sum, c;
    
    struct msdos_node *msdos_node = 0;
    void *scratch = 0;
    
    struct fat_cache_entry fat_cache = { 0 };
    struct disk *disk;
    
    spin_lock (&tmp_open_lock);
    *error = -ENOENT;
    
    if (!(root_node = (struct fs_node *) data)) {
    
        *error = -EIO;
        
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
    
    if (entries) {
        entries->count = 0;
    }
    
    node = root_node;
    path_depth = depth;
    
    if (!(parent = (struct fs_node *) node->parent)) {
    
        *error = -EIO;
        
        spin_unlock (&tmp_open_lock);
        return 0;
    
    }
    
    if (!parent->read) {
    
        *error = -EIO;
        
        spin_unlock (&tmp_open_lock);
        return 0;
    
    }
    
    if (!(disk = (struct disk *) parent->data)) {
    
        *error = -EIO;
        
        spin_unlock (&tmp_open_lock);
        return 0;
    
    }
    
    if (!(this = (struct msdos_specs *) node->data)) {
    
        *error = -EIO;
        
        spin_unlock (&tmp_open_lock);
        return 0;
    
    }
    
    reserved_sectors = this->reserved_sectors;
    hidden_sectors   = this->hidden_sectors;
    cluster_count    = this->cluster_count;
    
    sector_size      = MAX (disk->sector_size, this->bytes_per_sector);
    
    if (depth == 0) {
    
        if (!(msdos_node = kmalloc (sizeof (*msdos_node)))) {
        
            *error = -ENOMEM;
            
            spin_unlock (&tmp_open_lock);
            return 0;
        
        }
        
        memset (msdos_node, 0, sizeof (*msdos_node));
        
        msdos_node->fat_cache.sector = 0xFFFFFFFF;
        msdos_node->sectors_per_cluster = this->sectors_per_cluster;
        msdos_node->reserved_sectors = this->reserved_sectors;
        msdos_node->hidden_sectors = this->hidden_sectors;
        msdos_node->start_cluster = this->root_cluster;
        msdos_node->root_start = this->root_start;
        msdos_node->root_entries = this->root_entries;
        msdos_node->data_area = this->data_area;
        msdos_node->cluster_count = this->cluster_count;
        msdos_node->specs = *this;
        
        if (!(node = kmalloc (sizeof (*node)))) {
        
            kfree (msdos_node);
            *error = -ENOMEM;
            
            spin_unlock (&tmp_open_lock);
            return 0;
        
        }
        
        memset (node, 0, sizeof (*node));
        
        node->parent = parent;
        node->data = msdos_node;
        node->flags = INO_DIR;
        node->kclose = msdos_close;
        node->kstat = msdos_stat;
        node->kstatfs = msdos_statfs;
        node->readdir = msdos_readdir;
        
        spin_unlock (&tmp_open_lock);
        return node;
    
    }
    
    if (depth > 0) {
    
        if (!(scratch = kmalloc (sector_size))) {
        
            *error = -ENOMEM;
            
            spin_unlock (&tmp_open_lock);
            return 0;
        
        }
        
        if (this->cluster_count >= MIN_CLUST_32 && this->cluster_count <= MAX_CLUST_32) {
        
            current_cluster = this->root_cluster;
            /* current_entry = 0; */
            
            offset = (uint64_t) this->data_area + ((current_cluster - 2) * this->sectors_per_cluster);
        
        } else {
        
            current_cluster = 0;
            /* current_entry = 0; */
            
            offset = (uint64_t) this->root_start;
        
        }
        
        root_entries = this->root_entries;
        offset += this->hidden_sectors;
        
        for (;;) {
        
            sectors_per_cluster = this->sectors_per_cluster;
            
            while (sector_size >= (this->bytes_per_sector * this->cluster_count)) {
                sectors_per_cluster--;
            }
            
            if (!(dp = kmalloc (sizeof (*dp)))) {
            
                *error = -ENOMEM;
                goto failed;
            
            }
            
            ord = sum = 0xff;
            
            if ((ret = create_name (dp, path)) < 0) {
            
                *error = ret;
                goto failed;
            
            }
            
            ret = parent->read (parent, scratch, (uint64_t) offset, (size_t) 1);
            
            if (ret == 0x06) {
            
                if (root_node->check_and_mount) {
                
                    if ((ret = root_node->check_and_mount (root_node))) {
                    
                        *error = ret < 0 ? ret : -EIO;
                        goto failed;
                    
                    }
                    
                    kfree (scratch);
                    kfree (dp);
                    
                    scratch = 0;
                    dp = 0;
                    
                    goto retry;
                
                }
                
                *error = -EIO;
                goto failed;
            
            }
            
            if (ret) {
            
                *error = -EIO;
                goto failed;
            
            }
            
            no_ents = 512 / sizeof (*de);
            de = scratch;
            
            for (i = 0; i < no_ents; i++) {
            
                a = de[i].attr & 0x3f;
                
                if ((c = de[i].name[0]) == 0) {
                    goto failed;
                }
                
                if (c == 0xE5 || ((a & 0x08) && a != 15)) {
                
                    if (entries) {
                        entries->count = 0;
                    }
                    
                    ord = 0xff;
                
                } else {
                
                    if (a == 15) {
                    
                        if (entries && entries->count <= 20) {
                        
                            if (entries->count < 20) {
                            
                                entries->sector[entries->count] = offset;
                                entries->index[entries->count] = (uint32_t) i;
                            
                            }
                            
                            entries->count++;
                        
                        }
                        
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
                        
                        if (entries) {
                            entries->count = 0;
                        }
                        
                        ord = 0xff;
                    
                    }
                
                }
            
            }
            
            kfree (dp);
            dp = 0;
            
            if (i == no_ents) {
                
                if (current_cluster == 0) {
                
                    root_entries -= i;
                    
                    if (!root_entries) {
                        goto failed;
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
                    goto failed;
                }
                
                current_cluster = get_fat_entry (parent, &fat_cache, current_cluster);
                
                if (current_cluster >= 0x0FFFFFF7) {
                    goto failed;
                }
                
                offset = (uint64_t) this->data_area + ((current_cluster - 2) * this->sectors_per_cluster);
                offset += this->hidden_sectors;
                
                continue;
            
            }
            
            current_cluster = ((uint32_t) de[i].startlo[0] | ((uint32_t) de[i].startlo[1]) << 8 | ((uint32_t) de[i].starthi[0]) << 16 | ((uint32_t) de[i].starthi[1]) << 24) & 0x0FFFFFFF;
            a = de[i].attr & 0x3F;
            
            if (path_depth-- > 1) {
            
                if (a != ATTR_DIR) {
                
                    *error = -ENOTDIR;
                    goto failed;
                
                }
                
                path += strlen (path) + 1;
                
                if (entries) {
                    entries->count = 0;
                }
                
                if (*path) {
                
                    offset = (uint64_t) this->data_area + ((current_cluster - 2) * this->sectors_per_cluster);
                    offset += this->hidden_sectors;
                    
                    continue;
                
                }
            
            }
            /*
            if (a != ATTR_ARCHIVE) {
            
                kfree (scratch);
                
                spin_unlock (&tmp_open_lock);
                return 0;
            
            }
            */
            if (!(msdos_node = kmalloc (sizeof (*msdos_node)))) {
            
                *error = -ENOMEM;
                goto failed;
            
            }
            
            memset (msdos_node, 0, sizeof (*msdos_node));
            msdos_node->fat_cache.sector    = 0xFFFFFFFF;
            
            msdos_node->sectors_per_cluster = this->sectors_per_cluster;
            msdos_node->reserved_sectors    = this->reserved_sectors;
            msdos_node->hidden_sectors      = this->hidden_sectors;
            msdos_node->start_cluster       = current_cluster;
            msdos_node->data_area           = this->data_area;
            
            msdos_node->cluster_count       = this->cluster_count;
            msdos_node->file_size           = ((size_t) de[i].size[0] | ((size_t) de[i].size[1]) << 8 | ((size_t) de[i].size[2]) << 16 | ((size_t) de[i].size[3]) << 24);
            
            msdos_node->sector_size         = this->bytes_per_sector * this->sectors_per_cluster;
            
            msdos_node->modified_date       = (uint16_t) de[i].date[0] | (uint16_t) de[i].date[1] << 8;
            msdos_node->modified_time       = (uint16_t) de[i].xtime[0] | (uint16_t) de[i].xtime[1] << 8;
            msdos_node->accessed_date       = (uint16_t) de[i].adate[0] | (uint16_t) de[i].adate[1] << 8;
            
            msdos_node->attributes          = de[i].attr;
            msdos_node->specs               = *this;
            
            msdos_node->dir_sector          = offset;
            msdos_node->dir_index           = (uint32_t) i;
            
            if (!(msdos_node->scratch = kmalloc (MAX (msdos_node->sector_size, (size_t) disk->sector_size)))) {
            
                *error = -ENOMEM;
                goto failed;
            
            }
            
            memset (msdos_node->scratch, 0, MAX (msdos_node->sector_size, disk->sector_size));
            
            if (!(node = kmalloc (sizeof (*node)))) {
            
                *error = -ENOMEM;
                goto failed;
            
            }
            
            memset (node, 0, sizeof (*node));
            
            node->parent  = parent;
            node->data    = msdos_node;
            
            node->kclose  = msdos_close;
            node->kread   = msdos_read;
            node->kseek   = msdos_seek;
            node->kstat   = msdos_stat;
            node->kstatfs = msdos_statfs;
            node->kutime  = msdos_utime;
            
            node->flags   = (a & ATTR_DIR) ? INO_DIR : INO_REG;
            node->readdir = (a & ATTR_DIR) ? msdos_readdir : 0;
            
            msdos_track_open (parent, msdos_node);
            kfree (scratch);
            
            if (fat_cache.data) {
                kfree (fat_cache.data);
            }
            
            spin_unlock (&tmp_open_lock);
            return node;
        
        }
    
    }
    
failed:
    
    if (dp) {
        kfree (dp);
    }
    
    if (scratch) {
        kfree (scratch);
    }
    
    if (fat_cache.data) {
        kfree (fat_cache.data);
    }
    
    if (msdos_node) {
    
        if (msdos_node->scratch) {
            kfree (msdos_node->scratch);
        }
        
        kfree (msdos_node);
    
    }
    
    spin_unlock (&tmp_open_lock);
    return 0;

}

static struct fs_node *msdos_open (void *data, const char *path, size_t depth) {

    int error;
    return msdos_open_internal (data, path, depth, &error, 0);

}

static int msdos_initialize_directory (struct fs_node *parent, const struct msdos_specs *specs, uint32_t cluster, uint32_t parent_cluster, uint16_t date, uint16_t clock) {

    uint8_t scratch[512];
    
    struct msdos_dirent *de = (struct msdos_dirent *) scratch;
    uint32_t i, start;
    
    memset (scratch, 0, sizeof (scratch));
    
    for (i = 0; i < 2; i++) {
    
        memset (de[i].name, ' ', 11);
        de[i].name[0] = '.';
        
        if (i) {
            de[i].name[1] = '.';
        }
        
        start = i ? parent_cluster : cluster;
        de[i].attr = ATTR_DIR;
        
        int_to_array (de[i].cdate, date, 2);
        int_to_array (de[i].xctime, clock, 2);
        int_to_array (de[i].adate, date, 2);
        int_to_array (de[i].date, date, 2);
        int_to_array (de[i].xtime, clock, 2);
        int_to_array (de[i].startlo, start & 0xFFFF, 2);
        int_to_array (de[i].starthi, start >> 16, 2);
    
    }
    
    if (parent->write (parent, scratch, (uint64_t) msdos_cluster_sector (specs, cluster), (size_t) 1)) {
        return -EIO;
    }
    
    return 0;

}

static int msdos_short_name_exists (struct fs_node *parent, const struct msdos_specs *specs, struct msdos_node *directory, const uint8_t *name) {

    struct fat_cache_entry cache = { 0 };
    struct msdos_dirent *de;
    
    uint8_t scratch[512];
    uint64_t sector;
    
    uint32_t cluster, next, count, scanned = 0, n, i;
    int result = 0;
    
    cluster = directory->start_cluster;
    
    if (!cluster && !directory->root_entries) {
        return -EIO;
    }
    
    for (;;) {
    
        count = cluster ? specs->sectors_per_cluster * 16 : directory->root_entries;
        sector = cluster ? msdos_cluster_sector (specs, cluster) : (uint64_t) specs->hidden_sectors + directory->root_start;
        
        for (n = 0; n < count; n += 16, sector++) {
        
            if (parent->read (parent, scratch, sector, (size_t) 1)) {
            
                result = -EIO;
                goto done;
            
            }
            
            de = (struct msdos_dirent *) scratch;
            
            for (i = 0; i < 16 && n + i < count; i++) {
            
                if (!de[i].name[0]) {
                    goto done;
                }
                
                if (de[i].name[0] != 0xE5 && de[i].attr != ATTR_LONG_NAME && !memcmp (de[i].name, name, 11)) {
                
                    result = 1;
                    goto done;
                
                }
            
            }
        
        }
        
        if (!cluster) {
            break;
        }
        
        if (++scanned >= specs->cluster_count) {
        
            result = -EIO;
            break;
        
        }
        
        next = msdos_get_write_fat_entry (parent, &cache, cluster, specs);
        
        if (msdos_end_of_chain (specs, next)) {
            break;
        }
        
        if (!msdos_cluster_valid (specs, next)) {
        
            result = -EIO;
            break;
        
        }
        
        cluster = next;
    
    }
    
done:
    
    if (cache.data) {
        kfree (cache.data);
    }
    
    return result;

}

static void msdos_short_alias (const struct fat_dirent *dp, uint32_t number, uint8_t *name) {

    uint8_t digits[6];
    size_t length = 0, prefix = 0, i;
    
    memset (name, ' ', 11);
    memcpy (name + 8, dp->fn + 8, 3);
    
    do {
    
        digits[length++] = (uint8_t) ('0' + number % 10);
        number /= 10;
    
    } while (number && length < sizeof (digits));
    
    for (i = 0; i < 8 - length - 1 && dp->fn[i] != ' '; i++) {
        name[prefix++] = dp->fn[i];
    }
    
    if (!prefix) {
        name[prefix++] = '_';
    }
    
    name[prefix++] = '~';
    
    while (length) {
        name[prefix++] = digits[--length];
    }

}

static int msdos_create_entry (struct fs_node *parent, const struct msdos_specs *specs, struct msdos_node *directory, const struct fat_dirent *dp, struct msdos_node *file, uint8_t attr, const struct msdos_dirent *source, struct msdos_remove_entries *entries) {

    struct fat_cache_entry cache = { 0 };
    struct msdos_dirent *de;
    
    uint8_t scratch[512], name[11], entry[32];
    uint64_t slots_sector[21], sector;
    
    uint8_t slots_index[21];
    
    uint32_t cluster, next, first_extended = 0, extend_parent = 0;
    uint32_t start_cluster = 0, parent_cluster, scanned = 0;
    uint32_t count, n, i, j, used = 0, written = 0, ordinal, alias = 0;
    
    size_t length = 0, needed, position;
    
    uint16_t date, clock, code;
    uint8_t checksum;
    
    int ret = -EIO;
    
    if (!msdos_write_supported (parent, specs)) {
        return -EROFS;
    }
    
    while (dp->lfn[length]) {
        length++;
    }
    
    needed = (dp->fn[11] & 3) ? (length + 12) / 13 + 1 : 1;
    memcpy (name, dp->fn, 11);
    
    if (dp->fn[11] & 1) {
    
        alias = 1;
        msdos_short_alias (dp, alias, name);
    
    }
    
    for (;;) {
    
        ret = msdos_short_name_exists (parent, specs, directory, name);
        
        if (ret < 0) {
            goto done;
        }
        
        if (!ret) {
            break;
        }
        
        if (!(dp->fn[11] & 3)) {
        
            ret = -EEXIST;
            goto done;
        
        }
        
        if (++alias > 999999) {
        
            ret = -ENOSPC;
            goto done;
        
        }
        
        msdos_short_alias (dp, alias, name);
    
    }
    
    cluster = directory->start_cluster;
    
    if (cluster && !msdos_cluster_valid (specs, cluster)) {
    
        ret = -EIO;
        goto done;
    
    }
    
    if (!cluster && !directory->root_entries) {
    
        ret = -EIO;
        goto done;
    
    }
    
    for (;;) {
    
        count = cluster ? specs->sectors_per_cluster * 16 : directory->root_entries;
        sector = cluster ? msdos_cluster_sector (specs, cluster) : (uint64_t) specs->hidden_sectors + directory->root_start;
        
        for (n = 0; n < count; n += 16, sector++) {
        
            if (parent->read (parent, scratch, sector, (size_t) 1)) {
            
                ret = -EIO;
                goto done;
            
            }
            
            de = (struct msdos_dirent *) scratch;
            
            for (i = 0; i < 16 && n + i < count; i++) {
            
                if (!de[i].name[0] || de[i].name[0] == 0xE5) {
                
                    slots_sector[used] = sector;
                    slots_index[used] = (uint8_t) i;
                    
                    if (++used == needed) {
                        goto found;
                    
                    }
                
                } else {
                    used = 0;
                }
            
            }
        
        }
        
        if (!cluster) {
        
            ret = -ENOSPC;
            goto done;
        
        }
        
        if (++scanned >= specs->cluster_count) {
        
            ret = -EIO;
            goto done;
        
        }
        
        next = msdos_get_write_fat_entry (parent, &cache, cluster, specs);
        
        if (msdos_end_of_chain (specs, next)) {
        
            ret = msdos_allocate_cluster (parent, specs, &cache, &next);
            
            if (ret) {
                goto done;
            }
            
            ret = msdos_set_fat_entry (parent, specs, cluster, next);
            
            if (ret) {
            
                msdos_free_chain (parent, specs, &cache, next);
                goto done;
            
            }
            
            if (!first_extended) {
                first_extended = next;
                extend_parent = cluster;
            }
        
        } else if (!msdos_cluster_valid (specs, next)) {
        
            ret = -EIO;
            goto done;
        
        }
        
        cluster = next;
    
    }
    
found:
    
    msdos_get_datetime (&date, &clock);
    
    if (!source && attr == ATTR_DIR) {
    
        ret = msdos_allocate_cluster (parent, specs, &cache, &start_cluster);
        
        if (ret) {
            goto done;
        }
        
        parent_cluster = directory->start_cluster;
        
        if (parent_cluster == specs->root_cluster) {
            parent_cluster = 0;
        }
        
        ret = msdos_initialize_directory (parent, specs, start_cluster, parent_cluster, date, clock);
        
        if (ret) {
            goto done;
        
        }
    
    }
    
    checksum = sum_sfn (name);
    
    for (i = 0; i < needed; i++) {
    
        memset (entry, 0, sizeof (entry));
        
        if (i + 1 < needed) {
        
            ordinal = (uint32_t) (needed - i - 1);
            
            entry[0] = (uint8_t) (ordinal | (i ? 0 : 0x40));
            entry[11] = ATTR_LONG_NAME;
            entry[13] = checksum;
            
            for (j = 0; j < 13; j++) {
            
                position = (size_t) (ordinal - 1) * 13 + j;
                code = position < length ? dp->lfn[position] : (position == length ? 0 : 0xFFFF);
                
                entry[lfn_ofs[j]] = (uint8_t) code;
                entry[lfn_ofs[j] + 1] = (uint8_t) (code >> 8);
            
            }
        
        } else {
        
            de = (struct msdos_dirent *) entry;
            memcpy (de->name, name, 11);
            
            de->attr = attr;
            de->type = dp->fn[11] & 0x18;
            
            int_to_array (de->cdate, date, 2);
            int_to_array (de->xctime, clock, 2);
            int_to_array (de->adate, date, 2);
            int_to_array (de->date, date, 2);
            int_to_array (de->xtime, clock, 2);
            int_to_array (de->startlo, start_cluster & 0xFFFF, 2);
            int_to_array (de->starthi, start_cluster >> 16, 2);
            
            if (source) {
            
                memcpy (de, source, sizeof (*de));
                memcpy (de->name, name, 11);
                
                de->type = (de->type & 0xE7) | (dp->fn[11] & 0x18);
            
            }
        
        }
        
        sector = slots_sector[i];
        
        if (parent->read (parent, scratch, sector, (size_t) 1)) {
        
            ret = -EIO;
            goto done;
        
        }
        
        memcpy (scratch + (size_t) slots_index[i] * 32, entry, 32);
        
        if (parent->write (parent, scratch, sector, (size_t) 1)) {
        
            ret = -EIO;
            goto done;
        
        }
        
        written++;
    
    }
    
    file->start_cluster = start_cluster;
    file->dir_sector    = slots_sector[needed - 1];
    file->dir_index     = slots_index[needed - 1];
    file->modified_date = date;
    file->modified_time = clock;
    file->accessed_date = date;
    file->attributes    = attr;
    
    if (entries) {
    
        entries->count = needed - 1;
        
        for (i = 0; i < entries->count; i++) {
        
            entries->sector[i] = slots_sector[i];
            entries->index[i] = slots_index[i];
        
        }
    
    }
    
    ret = 0;
    
done:
    
    if (ret) {
    
        for (i = 0; i < written; i++) {
        
            if (!parent->read (parent, scratch, slots_sector[i], (size_t) 1)) {
            
                scratch[(size_t) slots_index[i] * 32] = 0xE5;
                parent->write (parent, scratch, slots_sector[i], (size_t) 1);
            
            }
        
        }
        
        if (start_cluster) {
            msdos_free_chain (parent, specs, &cache, start_cluster);
        }
        
        if (first_extended && !msdos_set_fat_entry (parent, specs, extend_parent, 0x0FFFFFFF)) {
            msdos_free_chain (parent, specs, &cache, first_extended);
        }
    
    }
    
    if (cache.data) {
        kfree (cache.data);
    }
    
    return ret;

}

static struct fs_node *msdos_create (void *data, const char *path, size_t depth, uint8_t attr, int *error) {

    struct fs_node *root_node = (struct fs_node *) data;
    struct fs_node *directory, *parent, *node = 0;
    
    struct msdos_node *file = 0;
    struct msdos_specs *specs;
    
    struct fat_dirent *dp;
    const char *name = path;
    
    size_t i;
    
    if (!depth) {
    
        *error = -EISDIR;
        return 0;
    
    }
    
    for (i = 1; i < depth; i++) {
        name += strlen (name) + 1;
    }
    
    if (!(dp = kmalloc (sizeof (*dp)))) {
    
        *error = -ENOMEM;
        return 0;
    
    }
    
    *error = create_name (dp, name);
    
    if (*error < 0) {
    
        kfree (dp);
        return 0;
    
    }
    
    if (!(directory = msdos_open_internal (data, path, depth - 1, error, 0))) {
    
        kfree (dp);
        return 0;
    
    }
    
    if (directory->flags != INO_DIR) {
    
        *error = -ENOTDIR;
        goto failed;
    
    }
    
    parent = directory->parent;
    specs = (struct msdos_specs *) root_node->data;
    
    if (!msdos_write_supported (parent, specs)) {
    
        *error = -EROFS;
        goto failed;
    
    }
    
    *error = -ENOMEM;
    
    if (!(file = kmalloc (sizeof (*file)))) {
        goto failed;
    }
    
    memset (file, 0, sizeof (*file));
    
    file->fat_cache.sector    = 0xFFFFFFFF;
    file->sectors_per_cluster = specs->sectors_per_cluster;
    file->reserved_sectors    = specs->reserved_sectors;
    file->hidden_sectors      = specs->hidden_sectors;
    file->data_area           = specs->data_area;
    file->cluster_count       = specs->cluster_count;
    file->sector_size         = specs->bytes_per_sector * specs->sectors_per_cluster;
    file->specs               = *specs;
    
    if (!(file->scratch = kmalloc (file->sector_size))) {
        goto failed;
    }
    
    if (!(node = kmalloc (sizeof (*node)))) {
        goto failed;
    }
    
    memset (node, 0, sizeof (*node));
    
    spin_lock (&tmp_open_lock);
    spin_lock (&tmp_read_lock);
    
    *error = msdos_create_entry (parent, specs, directory->data, dp, file, attr, 0, 0);
    
    spin_unlock (&tmp_read_lock);
    spin_unlock (&tmp_open_lock);
    
    if (*error) {
        goto failed;
    }
    
    node->parent  = parent;
    node->data    = file;
    
    node->kclose  = msdos_close;
    node->kread   = msdos_read;
    node->kseek   = msdos_seek;
    node->kstat   = msdos_stat;
    node->kstatfs = msdos_statfs;
    node->kutime  = msdos_utime;
    
    node->flags   = attr == ATTR_DIR ? INO_DIR : INO_REG;
    node->readdir = attr == ATTR_DIR ? msdos_readdir : 0;
    
    msdos_track_open (parent, file);
    msdos_close (directory);
    
    kfree (dp);
    return node;
    
failed:
    
    if (node) {
        kfree (node);
    }
    
    if (file) {
    
        if (file->scratch) {
            kfree (file->scratch);
        }
        
        kfree (file);
    
    }
    
    msdos_close (directory);
    kfree (dp);
    
    return 0;

}

static int msdos_directory_empty (struct fs_node *parent, struct msdos_node *file) {

    const struct msdos_specs *specs = &file->specs;
    uint8_t scratch[512];
    
    struct msdos_dirent *de = (struct msdos_dirent *) scratch;
    
    uint32_t cluster = file->start_cluster, scanned, i, j;
    uint64_t sector;
    
    for (scanned = 0; scanned < specs->cluster_count && msdos_cluster_valid (specs, cluster); scanned++) {
    
        sector = msdos_cluster_sector (specs, cluster);
        
        for (i = 0; i < specs->sectors_per_cluster; i++) {
        
            if (parent->read (parent, scratch, sector + i, (size_t) 1)) {
                return -EIO;
            }
            
            for (j = 0; j < 16; j++) {
            
                if (!de[j].name[0]) {
                    return 0;
                }
                
                if (de[j].name[0] == 0xE5) {
                    continue;
                }
                
                if (de[j].attr == ATTR_DIR && (!memcmp (de[j].name, ".          ", 11) || !memcmp (de[j].name, "..         ", 11))) {
                    continue;
                }
                
                return -ENOTEMPTY;
            
            }
        
        }
        
        cluster = msdos_get_write_fat_entry (parent, &file->fat_cache, cluster, specs);
        
        if (msdos_end_of_chain (specs, cluster)) {
            return 0;
        }
    
    }
    
    return -EIO;

}

static int msdos_remove (void *data, const char *path, size_t depth, int directory) {

    struct fs_node *node, *parent;
    struct msdos_node *file, *entry;
    struct msdos_remove_entries entries = { 0 };
    
    uint8_t scratch[512], checksum, ordinal;
    struct msdos_dirent *de = (struct msdos_dirent *) scratch;
    
    uint32_t cluster, scanned;
    size_t i;
    int ret;
    
    if (!(node = msdos_open_internal (data, path, depth, &ret, &entries))) {
        return ret;
    }
    
    file = node->data;
    parent = node->parent;
    
    if (node->flags == INO_DIR && directory != 1) {
    
        ret = -EISDIR;
        goto done;
    
    }
    
    if (node->flags != INO_DIR && directory) {
    
        ret = -ENOTDIR;
        goto done;
    
    }
    
    spin_lock (&tmp_open_lock);
    spin_lock (&tmp_read_lock);
    
    if (!msdos_write_supported (parent, &file->specs)) {
    
        ret = -EROFS;
        goto unlock;
    
    }
    
    if (file->attributes & ATTR_READONLY) {
    
        ret = -EACCES;
        goto unlock;
    
    }
    
    spin_lock (&tmp_close_lock);
    
    for (entry = msdos_open_nodes; entry; entry = entry->open_next) {
    
        if (entry != file && entry->open_parent == parent && entry->dir_sector == file->dir_sector && entry->dir_index == file->dir_index) {
            break;
        }
    
    }
    
    spin_unlock (&tmp_close_lock);
    
    if (entry) {
    
        ret = -EBUSY;
        goto unlock;
    
    }
    
    if (directory == 1 && (ret = msdos_directory_empty (parent, file))) {
        goto unlock;
    }
    
    ret = -EIO;
    
    if (entries.count > 20 || file->dir_index >= 16 || parent->read (parent, scratch, file->dir_sector, (size_t) 1)) {
        goto unlock;
    }
    
    if (!de[file->dir_index].name[0] || de[file->dir_index].name[0] == 0xE5) {
        goto unlock;
    }
    
    checksum = sum_sfn (de[file->dir_index].name);
    
    if (!file->start_cluster && file->file_size) {
        goto unlock;
    }
    
    /* Validate the entire chain before removing the directory entry. */
    cluster = file->start_cluster;
    
    if (cluster) {
    
        for (scanned = 0; scanned < file->specs.cluster_count; scanned++) {
        
            if (!msdos_cluster_valid (&file->specs, cluster)) {
                goto unlock;
            }
            
            cluster = msdos_get_write_fat_entry (parent, &file->fat_cache, cluster, &file->specs);
            
            if (msdos_end_of_chain (&file->specs, cluster)) {
                break;
            }
        
        }
        
        if (scanned == file->specs.cluster_count) {
            goto unlock;
        }
    
    }
    
    for (i = 0; i < entries.count; i++) {
    
        if (parent->read (parent, scratch, entries.sector[i], (size_t) 1)) {
            goto unlock;
        }
        
        ordinal = (uint8_t) (entries.count - i);
        
        if (!i) {
            ordinal |= 0x40;
        }
        
        if (de[entries.index[i]].attr != ATTR_LONG_NAME || de[entries.index[i]].name[0] != ordinal || de[entries.index[i]].csum != checksum) {
            goto unlock;
        }
    
    }
    
    for (i = 0; i < entries.count; i++) {
    
        if (parent->read (parent, scratch, entries.sector[i], (size_t) 1)) {
            goto unlock;
        }
        
        de[entries.index[i]].name[0] = 0xE5;
        
        if (parent->write (parent, scratch, entries.sector[i], (size_t) 1)) {
            goto unlock;
        }
    
    }
    
    if (parent->read (parent, scratch, file->dir_sector, (size_t) 1)) {
        goto unlock;
    }
    
    de[file->dir_index].name[0] = 0xE5;
    
    if (parent->write (parent, scratch, file->dir_sector, (size_t) 1)) {
        goto unlock;
    }
    
    ret = msdos_free_chain (parent, &file->specs, &file->fat_cache, file->start_cluster);

unlock:

    spin_unlock (&tmp_read_lock);
    spin_unlock (&tmp_open_lock);

done:

    msdos_close (node);
    return ret;

}

static int msdos_rename_read (struct fs_node *parent, uint64_t sector, uint32_t index, struct msdos_dirent *entry) {

    uint8_t scratch[512];
    
    if (index >= 16 || parent->read (parent, scratch, sector, (size_t) 1)) {
        return -EIO;
    }
    
    memcpy (entry, scratch + index * 32, sizeof (*entry));
    return 0;

}

static int msdos_rename_write (struct fs_node *parent, uint64_t sector, uint32_t index, const struct msdos_dirent *entry) {

    uint8_t scratch[512];
    
    if (index >= 16 || parent->read (parent, scratch, sector, (size_t) 1)) {
        return -EIO;
    }
    
    memcpy (scratch + index * 32, entry, sizeof (*entry));
    
    if (parent->write (parent, scratch, sector, (size_t) 1)) {
        return -EIO;
    }
    
    return 0;

}

static int msdos_rename (void *data, const char *old_path, size_t old_depth, const char *new_path, size_t new_depth, int directory) {

    struct fs_node *node, *target = 0, *dir = 0, *ancestor, *parent;
    struct msdos_node *file, *dest = 0, *new_dir, *open, created;
    
    struct msdos_remove_entries old_entries = { 0 }, new_entries = { 0 };
    struct msdos_dirent old_entry, target_entry, entry, dotdot, saved[20];
    struct fat_dirent *dp = 0;
    
    const char *name = new_path;
    
    uint64_t dot_sector = 0, new_sector;
    uint32_t new_index, cluster, scanned;
    
    size_t i, removed = 0;
    uint8_t checksum, ordinal;
    
    int ret, dot_changed = 0, target_written = 0, source_written = 0;
    
    if (!old_depth || !new_depth) {
        return -EBUSY;
    }
    
    if (!(node = msdos_open_internal (data, old_path, old_depth, &ret, &old_entries))) {
        return ret;
    }
    
    file = node->data;
    parent = node->parent;
    
    if (directory && node->flags != INO_DIR) {
    
        ret = -ENOTDIR;
        goto done;
    
    }
    
    target = msdos_open_internal (data, new_path, new_depth, &ret, 0);
    
    if (target) {
    
        dest = target->data;
        
        if (file->dir_sector == dest->dir_sector && file->dir_index == dest->dir_index) {
        
            ret = 0;
            goto done;
        
        }
        
        if (node->flags != target->flags) {
        
            ret = node->flags == INO_DIR ? -ENOTDIR : -EISDIR;
            goto done;
        
        }
    
    } else if (ret != -ENOENT) {
        goto done;
    }
    
    if (!(dir = msdos_open_internal (data, new_path, new_depth - 1, &ret, 0))) {
        goto done;
    }
    
    if (dir->flags != INO_DIR) {
    
        ret = -ENOTDIR;
        goto done;
    
    }
    
    new_dir = dir->data;
    
    if (node->flags == INO_DIR) {
    
        for (i = 1; i < new_depth; i++) {
        
            if (!(ancestor = msdos_open_internal (data, new_path, i, &ret, 0))) {
                goto done;
            }
            
            cluster = ((struct msdos_node *) ancestor->data)->start_cluster;
            msdos_close (ancestor);
            
            if (cluster == file->start_cluster) {
            
                ret = -EINVAL;
                goto done;
            
            }
        
        }
    
    }
    
    if (!target) {
    
        for (i = 1; i < new_depth; i++) {
            name += strlen (name) + 1;
        }
        
        if (!(dp = kmalloc (sizeof (*dp)))) {
        
            ret = -ENOMEM;
            goto done;
        
        }
        
        if ((ret = create_name (dp, name)) < 0) {
            goto done;
        }
    
    }
    
    spin_lock (&tmp_open_lock);
    spin_lock (&tmp_read_lock);
    
    if (!msdos_write_supported (parent, &file->specs)) {
    
        ret = -EROFS;
        goto unlock;
    
    }
    
    if (dest) {
    
        if (dest->attributes & ATTR_READONLY) {
        
            ret = -EACCES;
            goto unlock;
        
        }
        
        spin_lock (&tmp_close_lock);
        
        for (open = msdos_open_nodes; open; open = open->open_next) {
        
            if (open != dest && open->open_parent == parent && open->dir_sector == dest->dir_sector && open->dir_index == dest->dir_index) {
                break;
            }
        
        }
        
        spin_unlock (&tmp_close_lock);
        
        if (open) {
        
            ret = -EBUSY;
            goto unlock;
        
        }
        
        if (target->flags == INO_DIR && (ret = msdos_directory_empty (parent, dest))) {
            goto unlock;
        }
        
        ret = -EIO;
        cluster = dest->start_cluster;
        
        if ((!cluster && dest->file_size) || (cluster && cluster == file->start_cluster)) {
            goto unlock;
        }
        
        if (cluster) {
        
            for (scanned = 0; scanned < dest->specs.cluster_count; scanned++) {
            
                if (!msdos_cluster_valid (&dest->specs, cluster)) {
                    goto unlock;
                }
                
                cluster = msdos_get_write_fat_entry (parent, &dest->fat_cache, cluster, &dest->specs);
                
                if (msdos_end_of_chain (&dest->specs, cluster)) {
                    break;
                }
            
            }
            
            if (scanned == dest->specs.cluster_count) {
                goto unlock;
            }
        
        }
        
        if (msdos_rename_read (parent, dest->dir_sector, dest->dir_index, &target_entry)) {
            goto unlock;
        }
        
        if (!target_entry.name[0] || target_entry.name[0] == 0xE5) {
            goto unlock;
        }
    
    }
    
    ret = -EIO;
    
    if (old_entries.count > 20 || msdos_rename_read (parent, file->dir_sector, file->dir_index, &old_entry)) {
        goto unlock;
    }
    
    if (!old_entry.name[0] || old_entry.name[0] == 0xE5) {
        goto unlock;
    }
    
    checksum = sum_sfn (old_entry.name);
    
    for (i = 0; i < old_entries.count; i++) {
    
        if (msdos_rename_read (parent, old_entries.sector[i], old_entries.index[i], &saved[i])) {
            goto unlock;
        }
        
        ordinal = (uint8_t) (old_entries.count - i);
        
        if (!i) {
            ordinal |= 0x40;
        }
        
        if (saved[i].attr != ATTR_LONG_NAME || saved[i].name[0] != ordinal || saved[i].csum != checksum) {
            goto unlock;
        }
    
    }
    
    if (node->flags == INO_DIR) {
    
        if (!msdos_cluster_valid (&file->specs, file->start_cluster)) {
            goto unlock;
        }
        
        dot_sector = msdos_cluster_sector (&file->specs, file->start_cluster);
        
        if (msdos_rename_read (parent, dot_sector, 1, &dotdot) || dotdot.attr != ATTR_DIR || memcmp (dotdot.name, "..         ", 11)) {
            goto unlock;
        }
    
    }
    
    if (target) {
    
        entry = old_entry;
        
        memcpy (entry.name, target_entry.name, 11);
        entry.type = (entry.type & 0xE7) | (target_entry.type & 0x18);
        
        new_sector = dest->dir_sector;
        new_index = dest->dir_index;
        
        target_written = 1;
        
        if (msdos_rename_write (parent, new_sector, new_index, &entry)) {
            goto rollback;
        }
    
    } else {
    
        memset (&created, 0, sizeof (created));
        ret = msdos_create_entry (parent, &file->specs, new_dir, dp, &created, old_entry.attr, &old_entry, &new_entries);
        
        if (ret) {
            goto unlock;
        }
        
        new_sector = created.dir_sector;
        new_index = created.dir_index;
        
        target_written = 1;
    
    }
    
    ret = -EIO;
    
    if (node->flags == INO_DIR) {
    
        cluster = new_dir->start_cluster;
        
        if (cluster == file->specs.root_cluster) {
            cluster = 0;
        }
        
        entry = dotdot;
        
        int_to_array (entry.startlo, cluster & 0xFFFF, 2);
        int_to_array (entry.starthi, cluster >> 16, 2);
        
        dot_changed = 1;
        
        if (msdos_rename_write (parent, dot_sector, 1, &entry)) {
            goto rollback;
        }
    
    }
    
    for (i = 0; i < old_entries.count; i++) {
    
        entry = saved[i];
        
        entry.name[0] = 0xE5;
        removed++;
        
        if (msdos_rename_write (parent, old_entries.sector[i], old_entries.index[i], &entry)) {
            goto rollback;
        }
    
    }
    
    entry = old_entry;
    
    entry.name[0] = 0xE5;
    source_written = 1;
    
    if (msdos_rename_write (parent, file->dir_sector, file->dir_index, &entry)) {
        goto rollback;
    }
    
    spin_lock (&tmp_close_lock);
    
    for (open = msdos_open_nodes; open; open = open->open_next) {
    
        if (open->open_parent == parent && open->dir_sector == file->dir_sector && open->dir_index == file->dir_index && open != file) {
        
            open->dir_sector = new_sector;
            open->dir_index = new_index;
            open->fat_cache.sector = 0xFFFFFFFF;
        
        }
    
    }
    
    spin_unlock (&tmp_close_lock);
    ret = 0;
    
    if (dest) {
        ret = msdos_free_chain (parent, &dest->specs, &dest->fat_cache, dest->start_cluster);
    }
    
    goto unlock;

rollback:

    if (source_written) {
        msdos_rename_write (parent, file->dir_sector, file->dir_index, &old_entry);
    }
    
    for (i = 0; i < removed; i++) {
        msdos_rename_write (parent, old_entries.sector[i], old_entries.index[i], &saved[i]);
    }
    
    if (dot_changed) {
        msdos_rename_write (parent, dot_sector, 1, &dotdot);
    }
    
    if (target_written) {
    
        if (target) {
            msdos_rename_write (parent, new_sector, new_index, &target_entry);
        } else {
        
            memset (&entry, 0, sizeof (entry));
            entry.name[0] = 0xE5;
            
            for (i = 0; i < new_entries.count; i++) {
                msdos_rename_write (parent, new_entries.sector[i], new_entries.index[i], &entry);
            }
            
            msdos_rename_write (parent, new_sector, new_index, &entry);
        
        }
    
    }

unlock:

    spin_unlock (&tmp_read_lock);
    spin_unlock (&tmp_open_lock);

done:

    if (dp) {
        kfree (dp);
    }
    
    if (dir) {
        msdos_close (dir);
    }
    
    if (target) {
        msdos_close (target);
    }
    
    msdos_close (node);
    return ret;

}

static int msdos_mkdir (void *data, const char *path, size_t depth, mode_t mode) {

    struct fs_node *node;
    int error;
    
    (void) mode;
    
    if ((node = msdos_open_internal (data, path, depth, &error, 0))) {
    
        msdos_close (node);
        return -EEXIST;
    
    }
    
    if (error != -ENOENT) {
        return error;
    }
    
    if (!(node = msdos_create (data, path, depth, ATTR_DIR, &error))) {
        return error;
    }
    
    msdos_close (node);
    return 0;

}

static struct fs_node *msdos_open_flags (void *data, const char *path, size_t depth, int flags, int *error) {

    struct fs_node *node;
    struct msdos_node *file;
    struct fs_node *parent;
    
    int access = flags & VFS_O_ACCMODE;
    uint32_t cluster;
    
    *error = -ENOENT;
    
    if (access == VFS_O_ACCMODE || ((flags & VFS_O_TRUNC) && access == VFS_O_RDONLY)) {
    
        *error = -EINVAL;
        return 0;
    
    }
    
    if (!(node = msdos_open_internal (data, path, depth, error, 0))) {
    
        if (*error != -ENOENT || !(flags & VFS_O_CREAT) || !(node = msdos_create (data, path, depth, ATTR_ARCHIVE, error))) {
            return 0;
        }
    
    } else if ((flags & VFS_O_CREAT) && (flags & VFS_O_EXCL)) {
    
        msdos_close (node);
        
        *error = -EEXIST;
        return 0;
    
    }
    
    if (access == VFS_O_RDONLY) {
        return node;
    }
    
    file = (struct msdos_node *) node->data;
    parent = node->parent;
    
    if (node->flags == INO_DIR) {
    
        *error = -EISDIR;
        goto failed;
    
    }
    
    if (file->attributes & ATTR_READONLY) {
    
        *error = -EACCES;
        goto failed;
    
    }
    
    if (!msdos_write_supported (parent, &file->specs)) {
    
        *error = -EROFS;
        goto failed;
    
    }
    
    if (flags & VFS_O_TRUNC) {
    
        spin_lock (&tmp_open_lock);
        spin_lock (&tmp_read_lock);
        
        *error = msdos_refresh_entry (parent, file);
        
        if (!*error) {
        
            cluster = file->start_cluster;
            *error = msdos_update_entry (parent, file, 0, 0);
            
            if (!*error) {
                *error = msdos_free_chain (parent, &file->specs, &file->fat_cache, cluster);
            }
        
        }
        
        spin_unlock (&tmp_read_lock);
        spin_unlock (&tmp_open_lock);
        
        if (*error) {
            goto failed;
        }
    
    }
    
    file->open_flags = flags;
    
    node->ktruncate = msdos_truncate;
    node->kwrite = msdos_write;
    
    if (access == VFS_O_WRONLY) {
        node->kread = 0;
    }
    
    if (flags & VFS_O_APPEND) {
        file->file_offset = (uint32_t) file->file_size;
    }
    
    return node;
    
failed:
    
    msdos_close (node);
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
    
    if (!(this = kmalloc (sizeof (*this)))) {
    
        spin_unlock (&tmp_read_lock);
        return 0;
    
    }
    
    this->sectors_per_cluster = sectors_per_cluster;
    this->bytes_per_sector    = bytes_per_sector;
    this->hidden_sectors      = hidden_sectors;
    
    this->cluster_count       = (total_sectors - data_area) / sectors_per_cluster;
    this->root_cluster        = root_cluster;
    
    this->reserved_sectors    = reserved_sectors;
    this->root_entries        = root_entries;
    
    this->root_start          = root_start;
    this->data_area           = data_area;
    
    this->sectors_per_fat     = sectors_per_fat;
    this->number_of_fats      = number_of_fats;
    
    this->info_sector         = root_entries ? 0 : (uint32_t) array_to_int (bs->fat32.info_sector, 2);
    this->backup_boot         = root_entries ? 0 : (uint32_t) array_to_int (bs->fat32.backup_boot, 2);
    this->fat_flags           = root_entries ? 0 : (uint16_t) array_to_int (bs->fat32.flags, 2);
    
    if (!(node = kmalloc (sizeof (*node)))) {
    
        spin_unlock (&tmp_read_lock);
        return 0;
    
    }
    
    memset (node, 0, sizeof (*node));
    
    node->parent      = root_node;
    node->data        = this;
    node->kmkdir      = msdos_mkdir;
    node->kopen       = msdos_open;
    node->kopen_flags = msdos_open_flags;
    node->kremove     = msdos_remove;
    node->krename     = msdos_rename;
    
    spin_unlock (&tmp_read_lock);
    return node;

}
