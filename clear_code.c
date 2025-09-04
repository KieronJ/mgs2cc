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
#define WORK_SIZE       64

typedef struct {
    char data[WORK_SIZE];
    int  bits;
} BITSTREAM;

static unsigned int rand_work;
static char sbox[WORK_SIZE * 8];

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

/* TODO: Work out what this is */
void create_something(char *something, unsigned int total, int len, unsigned int seed)
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

void set_bit(BITSTREAM *bs, int index, char val)
{
    bs->data[index / 8] = (val << (index & 7)) | (bs->data[index / 8] & ~(1 << (index & 7)));
}

int get_bit(BITSTREAM *bs, int index)
{
    return ((1 << (index & 7)) & bs->data[index / 8]) >> (index & 7);
}

void emplace_bits(BITSTREAM *bs, unsigned int val, int len, unsigned int seed)
{
    char something[512];
    BITSTREAM work;
    int i;
    int v7;
    char bit;

    create_something(something, len + bs->bits, len, seed);

    memset(work.data, 0, sizeof(work.data));
    work.bits = len + bs->bits;

    for (i = 0; i < len; i++)
    {
        set_bit(&work, something[i], 1);
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
        set_bit(&work, something[i], val & 0x1);
        val >>= 1;
    }

    *bs = work;
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
    emplace_bits(bs, hash & 0x7F, 7, seed);
    scramble_bitstream(bs, seed);
    apply_sbox(bs, seed, 1);
    emplace_bits(bs, key, 5, 0xBAD0DEED);

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
    int base,
    const char *in,
    int in_size,
    int a6)
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
            work[j] = (old + a6 * rem) / base;
            rem = (old + a6 * rem) % base;

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

int generate_clear_code(
    char *out,
    int out_size,
    int base,
    const void *in,
    int in_bits,
    int key)
{
    BITSTREAM bs;
    int size;
    int i;

    memcpy(bs.data, in, HOWMANY(in_bits, 8));
    bs.bits = in_bits;

    emplace_bits_direct(&bs, 2, 2); /* platform? */
    emplace_bits_direct(&bs, 2, 2); /* region? */

    append_secrets(&bs, key);

    size = pad_bitstream(&bs, out_size, base, key);
    base_encode(out, size, base, bs.data, HOWMANY(bs.bits, 8), 256);

    for (i = 0; i < size; i++)
    {
        out[i] += 'A';
    }

    return size;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    char in[16];
    char out[32];
    int size;
    int i;

    memset(in, 0, sizeof(in));
    memset(out, 0, sizeof(out));

    /* TODO: This test data is supposed to be player stats */
    in[ 0] = 0x3C;
    in[ 1] = 0x01;
    in[ 2] = 0x98;
    in[ 3] = 0x10;
    in[ 4] = 0x01;
    in[ 5] = 0x00;
    in[ 6] = 0x00;
    in[ 7] = 0x01;
    in[ 8] = 0x07;
    in[ 9] = 0x10;
    in[10] = 0x04;
    in[11] = 0x00;
    in[12] = 0x28;
    in[13] = 0x00;
    in[14] = 0x00;
    in[15] = 0x00;

    /* NOTE: MGS2 uses a pseudorandom key which is transmitted in the clear code */
    srand(time(NULL));

    size = generate_clear_code(out, CODE_SIZE, CODE_BASE, in, CODE_BITS, rand() & 0x7FFFFFFF);

    for (i = 0; i < size; i += 4)
    {
        printf("%c%c%c%c ", out[i + 0], out[i + 1], out[i + 2], out[i + 3]);
    }

    printf("\n");
    return 0;
}
