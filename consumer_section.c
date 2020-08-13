//
// Created by dan on 5/17/20.
//

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "circ_buf.h"

static int ring_len = 1024;

struct ring_section {
    int start;
    int len;
    int wrap;
};


/**
 * xaprc00x_ring_section - Gives the caller parameters to consume data from a
 * ring buffer.
 *
 * Not all consumers want to perform a memcpy to another location so this
 * function provides the caller with parameters describing the portions of the
 * buffer they can safely read. This structure may then be passed to xaprc00x_ring_consume
 * to actually move the ring forward upon completion.
 *
 * @ring A pointer to the circ_buf object to manipulate
 * @ring_len The length of the circ_buf buffer
 * @len The length to consume from the ring buffer
 *
 * Returns: {-1,-,-} on failure, the copy parameters if the entire length can
 * be served. On success the first member (start) is the offset on the buffer
 * to begin reading, the second (len) is the length to read from start. The
 * final member (wrap) is the number of bytes to read from the beginning of
 * the buffer.
 *
 * Notes:
 * This only returns information on where to read from the buffer, it does not
 * reserve or restrict other consumers from using this data.
 */
struct ring_section get_consumer_section(
        struct circ_buf *ring, int ring_len, int len)
{
    int head = READ_ONCE(ring->head);
    int tail = READ_ONCE(ring->tail);
    struct ring_section ret = {-1, 0, 0};

    /* If there is not enough data to return */
    if (CIRC_CNT(head, tail, ring_len) >= len) {
        int cnt_to_end = CIRC_CNT_TO_END(head, tail, ring_len);

        ret.len = len <= cnt_to_end ? len : cnt_to_end;
        ret.start = tail;
        ret.wrap = len - ret.len;
    }

    return ret;
}

/**
 * xaprc00x_ring_consume - Consumes a secion of data on the ring
 *
 * When a consumer no longer needs a section of data on the circular buffer
 * this function should be called to free the data in the structure. Once
 * called the memory described in this section is no longer safe to read.
 *
 * @ring A pointer to the circ_buf object to manipulate
 * @ring_len The length of the circ_buf buffer
 * @section The section of memory to consume
 */
void ring_consume(
        struct circ_buf *ring,
        int ring_len,
        struct ring_section section)
{
    int newtail =
            (section.start + section.len + section.wrap) & (ring_len - 1);
    WRITE_ONCE(ring->tail, newtail);
}

void ring_produce(
        struct circ_buf *ring,
        int ring_len,
        struct ring_section section)
{
    int newhead =
            (section.start + section.len + section.wrap) & (ring_len - 1);
    WRITE_ONCE(ring->head, newhead);
}

/**
 * get_producer_section - Return a consumer section describing the buffer area ready for write
 *
 * @ring A pointer to the circ_buf object to manipulate
 * @ring_len The length of the circ_buf buffer
 * @len The length to place from buf onto the ring
 *
 * Returns: A ring_section describing the area ready for write, {-1,-,-} if insufficient space exists
 */
struct ring_section get_producer_section(struct circ_buf *ring, int ring_len, int len)
{
    int head = READ_ONCE(ring->head);
    int tail = READ_ONCE(ring->tail);
    struct ring_section ret = {-1, 0, 0};

    /* Make sure there is enough space to perform the operation */
    if (CIRC_SPACE(head, tail, ring_len) >= len) {
        int space_to_end = CIRC_SPACE_TO_END(head, tail, ring_len);

        ret.len = len < space_to_end ? len : space_to_end;
        ret.start = head;
        ret.wrap = len - ret.len;
    }

    return ret;
}

struct packet {
    struct {int len;} header;
    char payload[0];
};

int main(int argc, char **argv) {
    struct circ_buf ring = {0};
    struct packet *pkt;
    struct ring_section section;
    typedef const char * err_msg_t;
    err_msg_t *error_msg = NULL;

    ring.buf = malloc(ring_len);

    pkt = malloc(100);
    pkt->header.len = 100;
    for (int i=0; i<pkt->header.len; i++) {
        pkt->payload[i] = i;
    }

    /* Write a single byte to the ring */
    printf("TEST 1: Write single byte to ring\n");
    section = get_producer_section(&ring, ring_len, 1);
    if (section.start != -1) {
        ring.buf[section.start] = *((char *) pkt);
        ring_produce(&ring, ring_len, section);
    } else {
        *error_msg = "Section single byte is invalid where it shouldn't be.";
        goto error;
    }
    printf("PASS\n");

    /* See if we have enough to read the header to evaluate the whole read */
    printf("TEST 2: Attempt to read packet with incomplete header\n");
    section = get_consumer_section(&ring, ring_len, sizeof(int));

    if(section.start != -1) {
        *error_msg = "Header returned without having been written.";
        goto error;
    }
    printf("PASS\n");

    /* Copy half the packet in */
    printf("TEST 3: Copy complete header and half the packet\n");
    section = get_producer_section(&ring, ring_len, 49);
    if (section.start != -1) {
        memcpy(ring.buf + section.start, ((char*) pkt)+1, 49);
        ring_produce(&ring, ring_len, section);
    } else {
        *error_msg = "Section is invalid where it shouldn't be.";
        goto error;
    }
    printf("PASS\n");

    /* See if we have enough to read the header to evaluate the whole read */
    printf("TEST 4: Attempt to read whole packet with full header but incomplete packet\n");
    section = get_consumer_section(&ring, ring_len, sizeof(int));
    if(section.start != -1) {
        int read_len;
        memcpy(&read_len, ring.buf + section.start, sizeof(int));
        section = get_consumer_section(&ring, ring_len, read_len);

        if (section.start != -1) {
            *error_msg = "Entire packet can be read before it was written.";
            goto error;
        }
    } else {
        printf("Section half packet is invalid where it shouldn't be");
    }
    printf("PASS\n");

    /* Write the rest */
    printf("TEST 5: Write entire packet.\n");
    section = get_producer_section(&ring, ring_len, 50);
    if (section.start != -1) {
        memcpy(ring.buf + 50, ((char*) pkt)+50, 50);
        ring_produce(&ring, ring_len, section);
    } else {
        *error_msg = "Section is valid where it shouldn't be.";
        goto error;
    }
    printf("PASS\n");

    /* See if we have enough to read the header to evaluate the whole read */
    printf("TEST 6: Try to read entire packet with sufficient data\n");
    section = get_consumer_section(&ring, ring_len, sizeof(int));
    if(section.start != -1) {
        int read_len;
        memcpy(&read_len, ring.buf, sizeof(int));
        section = get_consumer_section(&ring, ring_len, read_len);
        if(section.start != -1) {
            section = get_consumer_section(&ring, ring_len, read_len);
            if (section.len != -1) {
                ring_consume(&ring, ring_len, section);
            } else {
                *error_msg = "Could not read whole packet when it should exist in buffer.";
                goto error;
            }
        } else {
            *error_msg = "Could not read header when it should exist in buffer.";
            goto error;
        }
    } else {
        *error_msg = "Invalid section.";
        goto error;
    }
    printf("PASS\n");

error:
    if (error_msg) {
        printf("FAIL: %s\n", *error_msg);
        printf("Exiting.\n");
    }

    free(ring.buf);
    free(pkt);

    return 0;
}
