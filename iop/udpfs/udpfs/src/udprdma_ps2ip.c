/*
 * UDPRDMA transport over PS2IP/lwIP sockets.
 *
 * The original dashboard transport talks directly to the custom ministack
 * SMAP driver.  That is fast, but it prevents a TCP service such as ps2ftpd
 * from owning the adapter at the same time.  This dashboard-only transport
 * preserves the exact UDPRDMA UDP payload and reliability state machine while
 * using the same PS2IP stack as FTP.  Neutrino's in-game FHI remains on the
 * original zero-copy transport.
 */

#include <ps2ip.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "mprintf.h"
#include "udprdma.h"


#define UDPRDMA_RX_ACK_WINDOW 6
#define UDPRDMA_PACKET_SIZE (sizeof(udprdma_hdr_t) + sizeof(udprdma_hdr_data_t) + \
                             UDPRDMA_MAX_APP_HDR + UDPRDMA_MAX_PAYLOAD)

/* Internal receive events.  Completion/window state is also kept in the
 * socket because a response can arrive while udprdma_send() waits for the
 * ACK piggybacked on that response. */
#define RX_EVENT_INFO   (1 << 0)
#define RX_EVENT_ACK    (1 << 1)
#define RX_EVENT_NACK   (1 << 2)
#define RX_EVENT_FIN    (1 << 3)
#define RX_EVENT_WINDOW (1 << 4)
#define RX_EVENT_DISC   (1 << 5)

typedef enum {
    STATE_INIT,
    STATE_DISCOVERING,
    STATE_CONNECTED,
    STATE_DISCONNECTED
} udprdma_state_t;

struct udprdma_socket {
    int in_use;
    int fd;

    uint16_t port;
    uint16_t service_id;
    udprdma_state_t state;
    struct sockaddr_in peer_addr;
    uint32_t peer_ip;             /* Host-order, for the public status API. */

    uint16_t tx_seq_nr;
    uint16_t tx_seq_nr_acked;

    void *rx_buffer;
    uint32_t rx_buffer_size;
    uint32_t rx_received;
    uint16_t rx_seq_nr_expected;
    uint8_t rx_window_count;
    uint8_t rx_complete;
    uint8_t rx_window_pending;
    uint8_t rx_nack_pending;

    void *rx_hdr_buffer;
    uint32_t rx_hdr_size;
    uint32_t rx_hdr_received;

    uint8_t tx_packet[UDPRDMA_PACKET_SIZE] __attribute__((aligned(4)));
    uint8_t rx_packet[UDPRDMA_PACKET_SIZE] __attribute__((aligned(4)));
};

#ifndef UDPRDMA_MAX_SOCKETS
#define UDPRDMA_MAX_SOCKETS 2
#endif
static struct udprdma_socket sockets[UDPRDMA_MAX_SOCKETS];


static int _same_peer(const struct udprdma_socket *s, const struct sockaddr_in *from)
{
    return from->sin_family == AF_INET &&
           from->sin_port == s->peer_addr.sin_port &&
           from->sin_addr.s_addr == s->peer_addr.sin_addr.s_addr;
}

static int _wait_readable(int fd, uint32_t timeout_ms)
{
    fd_set read_set;
    struct timeval timeout;

    FD_ZERO(&read_set);
    FD_SET(fd, &read_set);
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    return lwip_select(fd + 1, &read_set, NULL, NULL, &timeout);
}

static int _send_packet(struct udprdma_socket *s, const void *packet,
                        uint32_t packet_size, struct sockaddr_in *destination)
{
    int result = lwip_sendto(s->fd, (void *)packet, packet_size, 0,
                             (struct sockaddr *)destination,
                             sizeof(*destination));
    return result == (int)packet_size ? 0 : -1;
}

static int _send_discovery(struct udprdma_socket *s)
{
    udprdma_hdr_t base;
    udprdma_hdr_disc_t disc;
    struct sockaddr_in destination;

    base.raw = 0;
    base.packet_type = UDPRDMA_PT_DISCOVERY;
    base.seq_nr = 0;
    disc.service_id = s->service_id;
    disc.reserved = 0;

    memcpy(s->tx_packet, &base, sizeof(base));
    memcpy(s->tx_packet + sizeof(base), &disc, sizeof(disc));

    memset(&destination, 0, sizeof(destination));
    destination.sin_len = sizeof(destination);
    destination.sin_family = AF_INET;
    destination.sin_port = htons(s->port);
    destination.sin_addr.s_addr = IPADDR_BROADCAST;

    return _send_packet(s, s->tx_packet, sizeof(base) + sizeof(disc),
                        &destination);
}

static int _send_inform(struct udprdma_socket *s)
{
    udprdma_hdr_t base;
    udprdma_hdr_disc_t disc;

    if (s->peer_addr.sin_family != AF_INET)
        return -1;

    base.raw = 0;
    base.packet_type = UDPRDMA_PT_INFORM;
    base.seq_nr = 1;
    disc.service_id = s->service_id;
    disc.reserved = 0;

    memcpy(s->tx_packet, &base, sizeof(base));
    memcpy(s->tx_packet + sizeof(base), &disc, sizeof(disc));
    return _send_packet(s, s->tx_packet, sizeof(base) + sizeof(disc),
                        &s->peer_addr);
}

static int _send_ack(struct udprdma_socket *s, int is_ack)
{
    udprdma_hdr_t base;
    udprdma_hdr_data_t data;

    base.raw = 0;
    base.packet_type = UDPRDMA_PT_DATA;
    base.seq_nr = s->tx_seq_nr;

    data.raw = 0;
    data.seq_nr_ack = is_ack ? ((s->rx_seq_nr_expected - 1) & 0x0fff) :
                               s->rx_seq_nr_expected;
    data.flags = is_ack ? UDPRDMA_DF_ACK : 0;

    memcpy(s->tx_packet, &base, sizeof(base));
    memcpy(s->tx_packet + sizeof(base), &data, sizeof(data));
    return _send_packet(s, s->tx_packet, sizeof(base) + sizeof(data),
                        &s->peer_addr);
}

static int _send_data(struct udprdma_socket *s,
                      const void *app_hdr, uint32_t app_hdr_size,
                      const void *data_ptr, uint32_t data_size)
{
    udprdma_hdr_t base;
    udprdma_hdr_data_t data;
    uint32_t padded_data_size = (data_size + 3) & ~3;
    uint32_t packet_size;

    if (app_hdr_size > UDPRDMA_MAX_APP_HDR ||
        (app_hdr_size & 3) != 0 ||
        data_size > UDPRDMA_MAX_PAYLOAD)
        return -1;

    base.raw = 0;
    base.packet_type = UDPRDMA_PT_DATA;
    base.seq_nr = s->tx_seq_nr;

    data.raw = 0;
    data.seq_nr_ack = (s->rx_seq_nr_expected - 1) & 0x0fff;
    data.flags = UDPRDMA_DF_ACK | UDPRDMA_DF_FIN;
    data.hdr_word_count = app_hdr_size / 4;
    data.data_byte_count = padded_data_size;

    memcpy(s->tx_packet, &base, sizeof(base));
    memcpy(s->tx_packet + sizeof(base), &data, sizeof(data));
    packet_size = sizeof(base) + sizeof(data);

    if (app_hdr_size != 0) {
        memcpy(s->tx_packet + packet_size, app_hdr, app_hdr_size);
        packet_size += app_hdr_size;
    }
    if (data_size != 0) {
        memcpy(s->tx_packet + packet_size, data_ptr, data_size);
        if (padded_data_size != data_size)
            memset(s->tx_packet + packet_size + data_size, 0,
                   padded_data_size - data_size);
        packet_size += padded_data_size;
    }

    if (_send_packet(s, s->tx_packet, packet_size, &s->peer_addr) < 0)
        return -1;

    s->tx_seq_nr = (s->tx_seq_nr + 1) & 0x0fff;
    return 0;
}

static int _process_packet(struct udprdma_socket *s, const uint8_t *packet,
                           uint32_t packet_size,
                           const struct sockaddr_in *from)
{
    udprdma_hdr_t base;

    if (packet_size < sizeof(base))
        return 0;
    memcpy(&base, packet, sizeof(base));

    if (base.packet_type == UDPRDMA_PT_DISCOVERY ||
        base.packet_type == UDPRDMA_PT_INFORM) {
        udprdma_hdr_disc_t disc;

        if (packet_size < sizeof(base) + sizeof(disc))
            return 0;
        memcpy(&disc, packet + sizeof(base), sizeof(disc));
        if (disc.service_id != s->service_id)
            return 0;

        if (base.packet_type == UDPRDMA_PT_DISCOVERY) {
            s->peer_addr = *from;
            s->peer_ip = ntohl(from->sin_addr.s_addr);
            return RX_EVENT_DISC;
        }

        if (s->state != STATE_CONNECTED) {
            s->peer_addr = *from;
            s->peer_ip = ntohl(from->sin_addr.s_addr);
            return RX_EVENT_INFO;
        }
        return 0;
    }

    if (base.packet_type == UDPRDMA_PT_DATA) {
        udprdma_hdr_data_t data;
        uint32_t hdr_size;
        uint32_t payload_size;
        uint32_t data_offset;
        int events = 0;

        if (s->state != STATE_CONNECTED || !_same_peer(s, from) ||
            packet_size < sizeof(base) + sizeof(data))
            return 0;

        memcpy(&data, packet + sizeof(base), sizeof(data));
        hdr_size = data.hdr_word_count * 4;
        payload_size = hdr_size + data.data_byte_count;
        data_offset = sizeof(base) + sizeof(data) + hdr_size;
        if (hdr_size > UDPRDMA_MAX_APP_HDR ||
            sizeof(base) + sizeof(data) + payload_size > packet_size)
            return 0;

        if (data.flags & UDPRDMA_DF_ACK) {
            s->tx_seq_nr_acked = data.seq_nr_ack;
            events |= RX_EVENT_ACK;
        }

        if (payload_size == 0) {
            if (!(data.flags & UDPRDMA_DF_ACK))
                events |= RX_EVENT_NACK;
            return events;
        }

        if (s->rx_buffer == NULL)
            return events;

        if (base.seq_nr != s->rx_seq_nr_expected) {
            /* A duplicate means our last ACK was lost: repeat it.  Any other
             * out-of-order packet asks the server to resume at expected. */
            if (base.seq_nr == ((s->rx_seq_nr_expected - 1) & 0x0fff)) {
                s->rx_window_pending = 1;
                events |= RX_EVENT_WINDOW;
            } else {
                s->rx_nack_pending = 1;
                events |= RX_EVENT_NACK;
            }
            return events;
        }

        if (hdr_size != 0 && s->rx_hdr_received == 0) {
            if (s->rx_hdr_buffer != NULL) {
                uint32_t copy_size = hdr_size;
                if (copy_size > s->rx_hdr_size)
                    copy_size = s->rx_hdr_size;
                memcpy(s->rx_hdr_buffer,
                       packet + sizeof(base) + sizeof(data), copy_size);
            }
            s->rx_hdr_received = hdr_size;
        }

        if (data.data_byte_count != 0 && s->rx_received < s->rx_buffer_size) {
            uint32_t remaining = s->rx_buffer_size - s->rx_received;
            uint32_t copy_size = data.data_byte_count;
            if (copy_size > remaining)
                copy_size = remaining;
            memcpy((uint8_t *)s->rx_buffer + s->rx_received,
                   packet + data_offset, copy_size);
            s->rx_received += copy_size;
        }

        s->rx_seq_nr_expected = (base.seq_nr + 1) & 0x0fff;
        if ((data.flags & UDPRDMA_DF_FIN) ||
            s->rx_received >= s->rx_buffer_size) {
            s->rx_complete = 1;
            events |= RX_EVENT_FIN;
        } else if (++s->rx_window_count >= UDPRDMA_RX_ACK_WINDOW) {
            s->rx_window_count = 0;
            s->rx_window_pending = 1;
            events |= RX_EVENT_WINDOW;
        }
        return events;
    }

    return 0;
}

static int _receive_event(struct udprdma_socket *s, uint32_t timeout_ms)
{
    struct sockaddr_in from;
    socklen_t from_size = sizeof(from);
    int packet_size;
    int ready = _wait_readable(s->fd, timeout_ms);

    if (ready <= 0)
        return ready;

    memset(&from, 0, sizeof(from));
    packet_size = lwip_recvfrom(s->fd, s->rx_packet, sizeof(s->rx_packet), 0,
                                (struct sockaddr *)&from, &from_size);
    if (packet_size <= 0)
        return -1;

    return _process_packet(s, s->rx_packet, packet_size, &from);
}

static int _ack_covers(uint16_t acknowledged, uint16_t sent)
{
    return ((acknowledged - sent) & 0x0fff) < 0x0800;
}

static int _wait_for_tx_ack(struct udprdma_socket *s, uint16_t sent_seq_nr)
{
    for (;;) {
        int events = _receive_event(s, UDPRDMA_RETX_TIMEOUT_US / 1000);
        if (events <= 0)
            return -1;

        if (s->rx_nack_pending) {
            _send_ack(s, 0);
            s->rx_nack_pending = 0;
        }
        if (s->rx_window_pending) {
            _send_ack(s, 1);
            s->rx_window_pending = 0;
        }

        if ((events & RX_EVENT_ACK) &&
            _ack_covers(s->tx_seq_nr_acked, sent_seq_nr))
            return 0;
        if (events & RX_EVENT_NACK)
            return -1;
    }
}

static int _send_reliably(struct udprdma_socket *s,
                          const void *app_hdr, uint32_t app_hdr_size,
                          const void *data, uint32_t data_size)
{
    int retries;

    for (retries = 0; retries < UDPRDMA_MAX_RETRIES; retries++) {
        uint16_t sent_seq_nr = s->tx_seq_nr;

        if (_send_data(s, app_hdr, app_hdr_size, data, data_size) == 0 &&
            _wait_for_tx_ack(s, sent_seq_nr) == 0)
            return UDPRDMA_OK;

        s->tx_seq_nr = sent_seq_nr;
        M_PRINTF("send: retry seq=%d attempt=%d\n", sent_seq_nr, retries + 1);
    }

    s->state = STATE_DISCONNECTED;
    return UDPRDMA_ERR_NACK;
}


udprdma_socket_t *udprdma_create(uint16_t port, uint16_t service_id)
{
    struct udprdma_socket *s = NULL;
    struct sockaddr_in local;
    int enabled = 1;
    int i;

    for (i = 0; i < UDPRDMA_MAX_SOCKETS; i++) {
        if (!sockets[i].in_use) {
            s = &sockets[i];
            break;
        }
    }
    if (s == NULL)
        return NULL;

    memset(s, 0, sizeof(*s));
    s->fd = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s->fd < 0)
        return NULL;

    s->in_use = 1;
    s->port = port ? port : UDPFS_PORT;
    s->service_id = service_id;
    s->state = STATE_INIT;

    lwip_setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
    if (lwip_setsockopt(s->fd, SOL_SOCKET, SO_BROADCAST,
                        &enabled, sizeof(enabled)) < 0) {
        lwip_close(s->fd);
        memset(s, 0, sizeof(*s));
        return NULL;
    }

    memset(&local, 0, sizeof(local));
    local.sin_len = sizeof(local);
    local.sin_family = AF_INET;
    local.sin_port = htons(s->port);
    local.sin_addr.s_addr = IPADDR_ANY;
    if (lwip_bind(s->fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
        lwip_close(s->fd);
        memset(s, 0, sizeof(*s));
        return NULL;
    }

    M_DEBUG("udprdma_ps2ip: socket on port 0x%04X service 0x%04X\n",
            s->port, s->service_id);
    return s;
}

void udprdma_destroy(udprdma_socket_t *socket)
{
    if (socket == NULL || !socket->in_use)
        return;
    lwip_close(socket->fd);
    memset(socket, 0, sizeof(*socket));
}

int udprdma_discover(udprdma_socket_t *socket, uint32_t timeout_ms)
{
    uint32_t attempt_ms;
    int retries;

    if (socket == NULL)
        return UDPRDMA_ERR_INVAL;
    if (timeout_ms == 0)
        timeout_ms = UDPRDMA_DISC_TIMEOUT_US / 1000;
    attempt_ms = timeout_ms / UDPRDMA_MAX_RETRIES;
    if (attempt_ms == 0)
        attempt_ms = 1;

    socket->state = STATE_DISCOVERING;
    for (retries = 0; retries < UDPRDMA_MAX_RETRIES; retries++) {
        if (_send_discovery(socket) == 0) {
            int events = _receive_event(socket, attempt_ms);
            if (events & RX_EVENT_INFO) {
                socket->state = STATE_CONNECTED;
                M_DEBUG("udprdma_ps2ip: connected to %d.%d.%d.%d:%d\n",
                        (socket->peer_ip >> 24) & 0xff,
                        (socket->peer_ip >> 16) & 0xff,
                        (socket->peer_ip >> 8) & 0xff,
                        socket->peer_ip & 0xff,
                        ntohs(socket->peer_addr.sin_port));
                return UDPRDMA_OK;
            }
        }
    }

    socket->state = STATE_DISCONNECTED;
    return UDPRDMA_ERR_TIMEOUT;
}

void udprdma_inform(udprdma_socket_t *socket)
{
    if (socket != NULL)
        _send_inform(socket);
}

int udprdma_send(udprdma_socket_t *socket, const void *data, uint32_t size)
{
    if (socket == NULL || data == NULL)
        return UDPRDMA_ERR_INVAL;
    if (size == 0)
        return UDPRDMA_OK;
    if (socket->state != STATE_CONNECTED)
        return UDPRDMA_ERR_NOTCONN;
    if (size > UDPRDMA_MAX_PAYLOAD)
        size = UDPRDMA_MAX_PAYLOAD;

    return _send_reliably(socket, NULL, 0, data, size);
}

int udprdma_send_ll(udprdma_socket_t *socket,
                    const void *app_hdr, uint32_t app_hdr_size,
                    const void *data, uint32_t data_size)
{
    if (socket == NULL || app_hdr == NULL || data == NULL ||
        app_hdr_size > UDPRDMA_MAX_APP_HDR || (app_hdr_size & 3) != 0 ||
        data_size > UDPRDMA_MAX_PAYLOAD)
        return UDPRDMA_ERR_INVAL;
    if (app_hdr_size + data_size == 0)
        return UDPRDMA_OK;
    if (socket->state != STATE_CONNECTED)
        return UDPRDMA_ERR_NOTCONN;

    return _send_reliably(socket, app_hdr, app_hdr_size, data, data_size);
}

int udprdma_recv(udprdma_socket_t *socket, void *buffer, uint32_t size,
                 uint32_t timeout_ms)
{
    if (socket == NULL || buffer == NULL)
        return UDPRDMA_ERR_INVAL;
    if (size == 0)
        return 0;
    if (socket->state != STATE_CONNECTED)
        return UDPRDMA_ERR_NOTCONN;
    if (timeout_ms == 0)
        timeout_ms = 5000;

    if (socket->rx_buffer == NULL) {
        socket->rx_buffer = buffer;
        socket->rx_buffer_size = size;
        socket->rx_received = 0;
    }

    for (;;) {
        if (socket->rx_complete) {
            int result = socket->rx_received;
            _send_ack(socket, 1);
            socket->rx_buffer = NULL;
            socket->rx_hdr_buffer = NULL;
            socket->rx_complete = 0;
            socket->rx_window_pending = 0;
            socket->rx_nack_pending = 0;
            return result;
        }
        if (socket->rx_nack_pending) {
            _send_ack(socket, 0);
            socket->rx_nack_pending = 0;
        }
        if (socket->rx_window_pending) {
            _send_ack(socket, 1);
            socket->rx_window_pending = 0;
        }

        if (_receive_event(socket, timeout_ms) <= 0) {
            socket->rx_buffer = NULL;
            socket->rx_hdr_buffer = NULL;
            socket->state = STATE_DISCONNECTED;
            return UDPRDMA_ERR_TIMEOUT;
        }
    }
}

int udprdma_is_connected(udprdma_socket_t *socket)
{
    return socket != NULL && socket->state == STATE_CONNECTED;
}

uint32_t udprdma_get_peer_ip(udprdma_socket_t *socket)
{
    return socket != NULL ? socket->peer_ip : 0;
}

void udprdma_set_rx_buffer(udprdma_socket_t *socket, void *buffer,
                           uint32_t size)
{
    if (socket == NULL)
        return;
    socket->rx_buffer = buffer;
    socket->rx_buffer_size = size;
    socket->rx_received = 0;
    socket->rx_window_count = 0;
    socket->rx_complete = 0;
    socket->rx_window_pending = 0;
    socket->rx_nack_pending = 0;
}

void udprdma_set_rx_app_header(udprdma_socket_t *socket, void *hdr_buf,
                               uint32_t hdr_size)
{
    if (socket == NULL)
        return;
    socket->rx_hdr_buffer = hdr_buf;
    socket->rx_hdr_size = hdr_size;
    socket->rx_hdr_received = 0;
}
