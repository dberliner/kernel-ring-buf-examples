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

/*
 * Taken from https://stackoverflow.com/questions/7775991/how-to-get-hexdump-of-a-structure-data
 * Use print_hex_dump in the kernel
 */
void hex_dump (const char * desc, const void * addr, const int len);

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
    printf("%s oldhead=%d start=%d, len=%d, wrap=%d ", __func__, ring->head, section.start, section.len, section.wrap);
    int newhead =
            (section.start + section.len + section.wrap) & (ring_len - 1);
    printf("newhead=%d\n", newhead);
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

    ring.buf = malloc(ring_len);

    pkt = malloc(100);
    pkt->header.len = 100;
    for (int i=0; i<pkt->header.len; i++) {
        pkt->payload[i] = i;
    }

    /* Write a single byte to the ring */
    section = get_producer_section(&ring, ring_len, 1);
    if (section.start != -1) {
        ring.buf[section.start] = *((char *) pkt);
        ring_produce(&ring, ring_len, section);
    } else {
        printf("Section single byte is invalid where it shouldn't be");
    }

    /* See if we have enough to read the header to evaluate the whole read */
    section = get_consumer_section(&ring, ring_len, sizeof(int));
    if(section.start == -1)
        printf("Coud not read header, I don't know how many bytes I need to read (start=%d, len=%d, wrap=%d)\n",
                section.start, section.len, section.wrap);
    else {
        printf("FAILED, Header returned without having been written");
        goto cleanup;
    }

    /* Copy half the packet in */
    section = get_producer_section(&ring, ring_len, 49);
    if (section.start != -1) {
        memcpy(ring.buf + section.start, ((char*) pkt)+1, 49);
        ring_produce(&ring, ring_len, section);
    } else {
        printf("Section is invalid where it shouldn't be");
    }

    /* See if we have enough to read the header to evaluate the whole read */
    section = get_consumer_section(&ring, ring_len, sizeof(int));
    if(section.start != -1) {
        int read_len;
        memcpy(&read_len, ring.buf + section.start, sizeof(int));
        section = get_consumer_section(&ring, ring_len, read_len);
        if(section.start == -1) {
            printf("Read header, cannot read entire packet of %d bytes. Not consuming header\n", read_len);
        } else {
            printf("FAILED, entire packet can be read before it was written ring_len=%d, read_len=%d, start=%d, len=%d, wrap=%d\n",
                    ring_len, read_len, section.start, section.len, section.wrap);
            goto cleanup;
        }
    } else {
        printf("Section half packet is invalid where it shouldn't be");
    }

    /* Write the rest */
    section = get_producer_section(&ring, ring_len, 50);
    if (section.start != -1) {
        memcpy(ring.buf + 50, ((char*) pkt)+50, 50);
        ring_produce(&ring, ring_len, section);
    } else {
        printf("Section is valid where it shouldn't be");
    }

    /* See if we have enough to read the header to evaluate the whole read */
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
                printf("FAILED, Could not read while packet when it should exist in buffer");
                goto cleanup;
            }
        } else {
            printf("FAILED, Could not read header when it should exist in buffer start=%d, len=%d, wrap=%d",
                    section.start, section.len, section.wrap);
            goto cleanup;
        }
    } else {
        printf("Section is invalid where it shouldn't be");
    }

cleanup:
    free(ring.buf);
    free(pkt);

    return 0;
}

void hex_dump (const char * desc, const void * addr, const int len) {
    int i;
    unsigned char buff[17];
    const unsigned char * pc = (const unsigned char *)addr;

    // Output description if given.

    if (desc != NULL)
        printf ("%s:\n", desc);

    // Length checks.
    if (len == 0) {
        printf("  ZERO LENGTH\n");
        return;
    }
    else if (len < 0) {
        printf("  NEGATIVE LENGTH: %d\n", len);
        return;
    }

    // Process every byte in the data.
    for (i = 0; i < len; i++) {
        // Multiple of 16 means new line (with line offset).
        if ((i % 16) == 0) {
            // Don't print ASCII buffer for the "zeroth" line.
            if (i != 0)
                printf ("  %s\n", buff);

            // Output the offset.
            printf ("  %04x ", i);
        }

        // Now the hex code for the specific character.
        printf (" %02x", pc[i]);

        // And buffer a printable ASCII character for later.
        if ((pc[i] < 0x20) || (pc[i] > 0x7e)) // isprint() may be better.
            buff[i % 16] = '.';
        else
            buff[i % 16] = pc[i];
        buff[(i % 16) + 1] = '\0';
    }

    // Pad out last line if not exactly 16 characters.
    while ((i % 16) != 0) {
        printf ("   ");
        i++;
    }

    // And print the final ASCII buffer.
    printf ("  %s\n", buff);
}
