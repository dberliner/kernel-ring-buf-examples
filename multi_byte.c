//
// Created by dan on 5/17/20.
//
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "circ_buf.h"

static int ring_len = 1024;

/*
 * produce_byte - Writes a byte to the buffer
 * @ring The ring to write to
 * @ch The char to write
 *
 * Returns: 1 on success, 0 on failure
 */
int produce_bytes(struct circ_buf *ring, const char *ch, int len)
{
    int head = READ_ONCE(ring->head);
    int tail = READ_ONCE(ring->tail);

    if (CIRC_SPACE(head, READ_ONCE(ring->tail), ring_len) >= len) {
        int remainder = len % CIRC_SPACE_TO_END(head, tail, ring_len);
        int seq_len = len - remainder;

        /* Write the block making sure to wrap around the end of the buffer */
        memcpy(ring->buf+head, ch, seq_len);
        memcpy(ring->buf, ch+seq_len, remainder);
        WRITE_ONCE(ring->head, (head + len) & (ring_len - 1));
        return 1;
    }
    return 0;
}

/*
 * consume_byte - Takes a byte off of the buffer and writes it to *ch
 * @ring The ring to take from
 * @ch A pointer to the character to write to
 *
 * Returns: 1 on success, 0 on failure
 */
int consume_bytes(struct circ_buf *ring, char *ch, int len)
{
    int head = READ_ONCE(ring->head);
    int tail = READ_ONCE(ring->tail);

    if (CIRC_CNT(head, tail, ring_len) >= len) {
        int remainder = len % CIRC_SPACE_TO_END(head, tail, ring_len);
        int seq_len = len - remainder;

        /* Write the block making sure to wrap around the end of the buffer */
        memcpy(ch, ring->buf+tail, seq_len);
        memcpy(ch + seq_len, ring->buf, remainder);

        WRITE_ONCE(ring->tail, (tail + len) & (ring_len - 1));
        return 1;
    }
    return 0;
}
int main(int argc, char **argv) {
    char ch[4];
    struct circ_buf ring = {0};
    const char *long_buf = "1234567890";

    ring.buf = malloc(ring_len);

    printf("Writing 3 bytes abc\n");
    produce_bytes(&ring, "abc", 3);
    printf("Tried to read 4 and got return %d\n", consume_bytes(&ring, ch,4));
    printf("Reading 3 bytes (expecting abc) ");
    consume_bytes(&ring, ch, 3);
    ch[3] = '\0';
    printf("%s\n", ch);

    /* Try to consume 1 byte too many */
    printf("Tried to over-read and got return %d\n", consume_bytes(&ring, ch,5));

    /* Fill the buffer */
    for(int i=0; i<102; i++)
        produce_bytes(&ring, long_buf, 10);

    /* Try to write to the 1024th byte, free 1 byte then the same should be writable */
    printf("Tried to over-write and got return %d\n", produce_bytes(&ring, "1234", 4));
    consume_bytes(&ring, ch, 1);
    printf("Tried to fill up and got return %d\n", produce_bytes(&ring, "1234", 4));
}