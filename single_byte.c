//
// Created by dan on 5/17/20.
//
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
int produce_byte(struct circ_buf *ring, char ch)
{
    int head = READ_ONCE(ring->head);

    if (CIRC_SPACE(head, READ_ONCE(ring->tail), ring_len)) {
        ring->buf[head] = ch;
        WRITE_ONCE(ring->head, (head + 1) & (ring_len - 1));
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
int consume_byte(struct circ_buf *ring, char *ch)
{
    int tail = READ_ONCE(ring->tail);

    if (CIRC_CNT(READ_ONCE(ring->head), tail, ring_len)) {
        *ch = ring->buf[ring->tail];
        WRITE_ONCE(ring->tail, (tail + 1) & (ring_len - 1));
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    char ch;
    struct circ_buf ring = {0};

    ring.buf = malloc(ring_len);

    printf("Writing 3 bytes abc\n");
    produce_byte(&ring, 'a');
    produce_byte(&ring, 'b');
    produce_byte(&ring, 'c');
    printf("Reading 3 bytes (expecting abc) ");
    consume_byte(&ring, &ch); printf("%c", ch);
    consume_byte(&ring, &ch); printf("%c", ch);
    consume_byte(&ring, &ch); printf("%c\n", ch);

    /* Try to consume 1 byte too many */
    printf("Tried to over-read and got return %d\n", consume_byte(&ring,&ch));

    /* Fill the buffer */
    for(int i=0;i<ring_len-1;i++)
        produce_byte(&ring, 'a');
    printf("Tried to over-write and got return %d\n", produce_byte(&ring, 'a'));
}