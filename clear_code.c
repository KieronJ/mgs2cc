#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MIN(x, y)       (((x) < (y)) ? (x) : (y))
#define MAX(x, y)       (((x) > (y)) ? (x) : (y))
#define HOWMANY(x,y)    (((x) + ((y) - 1)) / (y))

#define CODE_BASE       26  /* A-Z */
#define CODE_SIZE       28  /* Substance PC */
#define CODE_BITS       110 /* Substance PC */
#define REGION_BITS     2
#define PLATFORM_BITS   2
#define CRC_BITS        7
#define KEY_BITS        5
#define ALL_BITS        (CODE_BITS + REGION_BITS + PLATFORM_BITS + CRC_BITS + KEY_BITS)
#define WORK_SIZE       64

typedef struct {
    char data[WORK_SIZE];
    int  bits;
} BITSTREAM;

static unsigned int rand_work;
static char sbox[WORK_SIZE * 8];

int crc_buffer(char *buf, int len)
{
    unsigned int hash;
    int i;

    hash = 0;
    for (; len > 0; len -= 8)
    {
        /* TODO: sort out char signedness crap */
        hash ^= (int)(signed char)*buf++;

        for (i = MIN(len, 8); i > 0; i--)
        {
            if (hash & 0x1)
            {
                hash ^= 0xE8;
            }

            hash >>= 1;
        }
    }

    return hash;
}

unsigned int get_rand(void)
{
    rand_work = rand_work * 0x7D2B89DD + 0xCF9;
    return rand_work;
}

void set_bit(BITSTREAM *bs, int index, char val)
{
    bs->data[index / 8] = (val << (index & 7)) | (bs->data[index / 8] & ~(1 << (index & 7)));
}

int get_bit(BITSTREAM *bs, int index)
{
    return ((1 << (index & 7)) & bs->data[index / 8]) >> (index & 7);
}

void create_sbox(char *s, int size)
{
    int i, j;
    unsigned int index;
    int swap1;
    int swap2;

    for (i = 0; i < size; i++)
    {
        s[i] = i;
    }

    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < size; j++)
        {
            index = get_rand() % size;
            swap1 = s[j];
            swap2 = s[index];
            s[index] = swap1;
            s[j] = swap2;
        }
    }
}

void create_pbox(char *something, unsigned int total, int len, unsigned int seed)
{
    int i, j;
    int v4;
    int v7;

    rand_work = seed;

    for (i = 0; i < len; i++)
    {
        something[i] = get_rand() % (total - i);

        v4 = 0;
        do
        {
            v7 = v4;
            v4 = 0;

            for (j = 0; j < i; j++)
            {
                if (something[j] <= v7 + something[i])
                {
                    v4++;
                }
            }
        }
        while (v7 != v4);

        something[i] += v7;
    }
}

void dump_bitstream(BITSTREAM *bs, const char *label)
{
    int i;

    printf("%s: size: %d\n", label, bs->bits);
    for (i = 0; i < HOWMANY(bs->bits, 8); i++)
    {
        printf("%d: %02X\n", i, bs->data[i]);
    }
}

void trim_bitstream(BITSTREAM *bs)
{
    int size;
    int rest;

    size = bs->bits;
    bs->data[size / 8] &= (1 << (size & 0x7)) - 1;

    rest = size / 8 + 1;
    if (rest < WORK_SIZE)
    {
        memset(&bs->data[rest], 0, WORK_SIZE - rest);
    }
}

void emplace_bits(BITSTREAM *bs, unsigned int val, int len, unsigned int seed)
{
    char pbox[512];
    BITSTREAM work;
    int i;
    int v7;
    char bit;

    create_pbox(pbox, len + bs->bits, len, seed);

    memset(work.data, 0, sizeof(work.data));
    work.bits = len + bs->bits;

    for (i = 0; i < len; i++)
    {
        set_bit(&work, pbox[i], 1);
    }

    v7 = 0;
    for (i = 0; i < work.bits; i++)
    {
        if (get_bit(&work, i) == 0)
        {
            bit = get_bit(bs, v7);
            set_bit(&work, i, bit);
            v7++;
        }
    }

    for (i = 0; i < len; i++)
    {
        set_bit(&work, pbox[i], val & 0x1);
        val >>= 1;
    }

    *bs = work;
}

int extract_bits(BITSTREAM *bs, int len, unsigned int seed)
{
    char pbox[512];
    BITSTREAM work;
    int i;
    int val;
    int v7;
    int bit;

    create_pbox(pbox, bs->bits, len, seed);

    memset(work.data, 0, sizeof(work.data));
    work.bits = bs->bits;

    val = 0;
    for (i = 0; i < len; i++)
    {
        val |= get_bit(bs, pbox[i]) << i;
        set_bit(&work, pbox[i], 1);
    }

    v7 = 0;
    for (i = 0; i < work.bits; i++)
    {
        if (get_bit(&work, i) == 0)
        {
            bit = get_bit(bs, i);
            set_bit(&work, v7, bit);
            v7++;
        }
    }

    memcpy(bs->data, work.data, HOWMANY(bs->bits, 8));

    bs->bits -= len;
    trim_bitstream(bs);

    return val;
}

void emplace_bits_direct(BITSTREAM *bs, unsigned int val, int len)
{
    int size;
    int pos;
    int num;
    int mask;

    size = bs->bits;
    bs->bits += len;

    while (len > 0)
    {
        pos = size & 7;
        num = MIN(len, 8 - pos);
        mask = (1 << num) - 1;

        bs->data[size / 8] &= ~(mask << pos);
        bs->data[size / 8] |= (mask << pos) & (val << pos);

        len -= num;
        val >>= num;
        size += num;
    }
}

int extract_bits_direct(BITSTREAM *bs, int len)
{
    int val;
    int i;

    val = 0;
    for (i = 0; i < len; i++)
    {
        val |= get_bit(bs, bs->bits) << i;
        bs->bits--;
    }

    return val;
}

void scramble_bitstream(BITSTREAM *bs, unsigned int seed)
{
    int i;

    rand_work = seed;

    for (i = 0; i < bs->bits; i += 8)
    {
        bs->data[i / 8] ^= get_rand();
    }
}

void apply_sbox(BITSTREAM *bs, int seed, int encrypt)
{
    BITSTREAM work;
    int i;

    rand_work = seed;

    memset(work.data, 0, sizeof(work.data));
    work.bits = 0;

    create_sbox(sbox, bs->bits);

    for (i = 0; i < bs->bits; i++)
    {
        if (encrypt)
        {
            work.data[sbox[i] >> 3] |= ((bs->data[i / 8] >> (i & 7)) & 1) << (sbox[i] & 7);
        }
        else
        {
            work.data[i / 8] |= ((bs->data[sbox[i] >> 3] >> (sbox[i] & 7)) & 1) << (i & 7);
        }
    }

    memcpy(bs->data, work.data, WORK_SIZE);
}

void append_secrets(BITSTREAM *bs, int key)
{
    unsigned int seed;
    int hash;

    key &= 0x1F;
    seed = (1 << key) - 1;

    trim_bitstream(bs);

    hash = crc_buffer(bs->data, bs->bits);
    emplace_bits(bs, hash & 0x7F, CRC_BITS, seed);

    scramble_bitstream(bs, seed);
    apply_sbox(bs, seed, 1);

    emplace_bits(bs, key, KEY_BITS, 0xBAD0DEED);
    trim_bitstream(bs);
}

int pad_bitstream(BITSTREAM *bs, int out_size, int base, int key)
{
    float in_base;
    float out_base;
    int size;

    in_base = logf(2.0f) * log2f(2.0f);
    out_base = logf(2.0f) * log2f(base);
    size = (in_base / out_base) * bs->bits + 1.0f;
    size = MAX(size, out_size);

    emplace_bits_direct(bs, key * 0x7D2B89DD + 0xCF9, (int)((out_base / in_base) * size) - bs->bits);
    return size;
}

void base_encode(
    char *out,
    int out_size,
    int out_base,
    const char *in,
    int in_size,
    int in_base)
{
    char work[WORK_SIZE];
    int i, j;
    int v6;
    int rem;
    int old;

    memcpy(work, in, in_size);

    v6 = in_size - 1;

    for (i = 0; v6 >= 0; i++)
    {
        rem = 0;

        for (j = v6; j >= 0; j--)
        {
            old = work[j];
            work[j] = (old + in_base * rem) / out_base;
            rem = (old + in_base * rem) % out_base;

            if (j == v6 && work[j] == 0)
            {
                v6--;
            }
        }

        out[i] = rem;
    }

    if (i < out_size)
    {
        memset(&out[i], 0, out_size - i);
    }
}

int encrypt(
    char *out,
    int out_size,
    const void *in,
    int in_bits,
    int key)
{
    BITSTREAM bs;
    int size;
    int i;

    memcpy(bs.data, in, HOWMANY(in_bits, 8));
    bs.bits = in_bits;

    emplace_bits_direct(&bs, 2, PLATFORM_BITS); /* platform? */
    emplace_bits_direct(&bs, 2, REGION_BITS);   /* region? */

    append_secrets(&bs, key);

    size = pad_bitstream(&bs, out_size, CODE_BASE, key);
    base_encode(out, size, CODE_BASE, bs.data, HOWMANY(bs.bits, 8), 256);

    for (i = 0; i < size; i++)
    {
        out[i] += 'A';
    }

    return size;
}

int decrypt(
    void *out,
    int out_size,
    const void *in,
    int in_bits,
    int *platform,
    int *region)
{
    BITSTREAM bs;
    int i;
    int key;
    int seed;
    int hash;
    int calc;

    memcpy(bs.data, in, HOWMANY(in_bits, 8));
    bs.bits = in_bits;

    for (i = 0; i < HOWMANY(in_bits, 8); i++)
    {
        bs.data[i] -= 'A';
    }

    base_encode(bs.data, WORK_SIZE, 256, bs.data, HOWMANY(in_bits, 8), CODE_BASE);
    bs.bits = ALL_BITS;
    trim_bitstream(&bs);

    key = extract_bits(&bs, KEY_BITS, 0xBAD0DEED);
    seed = (1 << key) - 1;

    apply_sbox(&bs, seed, 0);
    scramble_bitstream(&bs, seed);

    hash = extract_bits(&bs, CRC_BITS, seed);
    calc = crc_buffer(bs.data, bs.bits) & 0x7f;

    if (hash != calc)
    {
        printf("ERROR: crc mismatch!\n");
        printf("expected: %02X, got: %02X\n", hash, calc);
        return 0;
    }

    *region = extract_bits_direct(&bs, REGION_BITS);
    *platform = extract_bits_direct(&bs, PLATFORM_BITS);

    bs.bits = CODE_BITS;
    trim_bitstream(&bs);

    out_size = MIN(out_size, HOWMANY(bs.bits, 8));
    memcpy(out, bs.data, out_size);
    return out_size;
}

void print_info(int platform, int version, unsigned int code[4])
{
    int radar;
    int time;
    int shots;
    int damage;

    int alerts;
    int kills;
    int escapes;
    int tanker_clears;
    int sealouse;
    int special;

    int continues;
    int rations;
    int saves;
    int mechs;
    int plant_clears;

    int level;
    int mode;
    int dogtags;

    radar = code[0] & 0x3;
    time = ((code[0] >> 2) & 0x7FFF) * 15;
    shots = (code[0] >> 17) & 0x3FF;
    damage = code[0] >> 27;

    alerts = code[1] & 0xFF;
    kills = (code[1] >> 8) & 0xFF;
    escapes = (code[1] >> 16) & 0xFF;
    tanker_clears = (code[1] >> 24) & 0x3F;
    sealouse = (code[1] >> 30) & 0x1;
    special = code[1] >> 31;

    continues = code[2] & 0x3F;
    rations = (code[2] >> 6) & 0x1F;
    saves = (code[2] >> 11) & 0x7F;
    mechs = (code[2] >> 18) & 0xFF;
    plant_clears = (code[2] >> 26) & 0x3F;

    level = (code[3] >> 2) & 0x7;
    mode = (code[3] >> 5) & 0x3;
    dogtags = (code[3] >> 7) & 0x7F;

    switch (platform)
    {
    case 2:  printf("PLATFORM:               PC\n"); break;
    default: printf("PLATFORM:               UNKNOWN (%d)\n", platform);
    }

    switch (version)
    {
    case 2:  printf("VERSION:                EUROPE\n"); break;
    default: printf("VERSION:                UNKNOWN (%d)\n", version);
    }

    switch (mode)
    {
    case 0:  printf("MODE:                   TANKER-PLANT\n"); break;
    case 1:  printf("MODE:                   TANKER\n"); break;
    case 2:  printf("MODE:                   PLANT\n"); break;
    default: printf("MODE:                   INVALID (%d)\n", mode);
    }

    switch (level)
    {
    case 0:  printf("GAME LEVEL:             VERY EASY\n"); break;
    case 1:  printf("GAME LEVEL:             EASY\n"); break;
    case 2:  printf("GAME LEVEL:             NORMAL\n"); break;
    case 3:  printf("GAME LEVEL:             HARD\n"); break;
    case 4:  printf("GAME LEVEL:             EXTREME\n"); break;
    case 5:  printf("GAME LEVEL:             EUROPEAN EXTREME\n"); break;
    default: printf("GAME LEVEL:             UNKNOWN (%d)\n", level);
    }

    printf("\n");
    printf("CLEAR TIME:             %02d:%02d:%02d\n", time / 3600, (time / 60) % 60, time % 60);
    printf("TOTAL DAMAGE:           %d\n", damage);
    printf("SHOTS FIRED:            %d\n", shots);
    printf("ALERT MODE:             %d\n", alerts);
    printf("CLEARING ESCAPE:        %d\n", escapes);
    printf("ENEMIES KILLED:         %d\n", kills);
    printf("MECH DESTRUCTION:       %d\n", mechs);
    printf("CONTINUES:              %d\n", continues);
    printf("RATIONS USED:           %d\n", rations);
    printf("SAVES:                  %d\n", saves);

    printf("\n");
    printf("RADAR:                  %s\n", radar ? "USED" : "NOT USED");
    printf("USE OF SPECIAL ITEM:    %s\n", special ? "YES" : "NO");
    printf("DOG TAG COLLECTION:     %d%%\n", dogtags);
    printf("TANKER CHAPTER CLEARED: %d\n", tanker_clears);
    printf("PLANT CHAPTER CLEARED:  %d\n", plant_clears);
    printf("CLEAR WITH SEALOUCE:    %s\n", sealouse ? "YES" : "NO");
}

/*
int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    int in[4];

    char out[32];
    int out2[8];
    int e_size, d_size;
    int i;
    int platform;
    int region;

    memset(in, 0, sizeof(in));
    memset(out, 0, sizeof(out));

    // Test
    in[0] = 0x1098013C;
    in[1] = 0x01000001;
    in[2] = 0x00041007;
    in[3] = 0x00000028;

    // NOTE: MGS2 uses a pseudorandom key which is transmitted in the clear code
    srand(time(NULL));

    e_size = encrypt(out, CODE_SIZE, in, CODE_BITS, rand() & 0x7FFFFFFF);
    // printf("encrypt returned %d bits\n", e_size * 8);
//
    // for (i = 0; i < e_size; i += 4)
    // {
    //     printf("%c%c%c%c ", out[i + 0], out[i + 1], out[i + 2], out[i + 3]);
    // }
//
    // printf("\n");

    d_size = decrypt(out2, 16, out, 224, &platform, &region);
    // printf("decrypt returned %d bits\n", d_size * 8);
    // printf("platform: %d, region: %d\n", platform, region);
//
    // for (i = 0; i < HOWMANY(d_size, 4); i++)
    // {
    //     printf("%08x ", out2[i]);
    // }
//
    // printf("\n");

    print_info(platform, region, out2[0], out2[1], out2[2], out2[3]);
    return 0;
}
*/



int main(int argc, char **argv)
{
    int len;
    int i;
    unsigned int pos;
    int c;
    char input[32];
    unsigned int code[4];
    int platform;
    int version;

    if (argc != 2)
    {
        printf("usage: dumpcc code\n");
        return 0;
    }

    memset(input, 0, sizeof(input));
    len = strlen(argv[1]);
    pos = 0;

    for (i = 0; i < len; i++)
    {
        c = argv[1][i];

        if (c == ' ')
        {
            continue;
        }

        if (!isalpha(c))
        {
            printf("ERROR: unrecognised character %c\n", c);
            return 1;
        }

        if (pos >= 28)
        {
            printf("ERROR: input size %u exceeds expected 28\n", pos);
            return 1;
        }

        input[pos++] = toupper(c);
    }

    if (pos != 28)
    {
        printf("ERROR: input size %u, expected 28\n", pos);
        return 1;
    }

    decrypt(code, sizeof(code), input, CODE_SIZE * 8, &platform, &version);
    print_info(platform, version, code);
    return 0;
}
