// Zephyr RTOS kernel and logging headers
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
// Zephyr networking stack headers for socket operations
#include <zephyr/net/socket.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_ip.h>
// Standard C library headers for string manipulation and I/O
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

// Include LED control header for visual feedback
#include "led.h"
#include "control.h"
#include "icm20948.h"
#include "resource_monitor.h"

// Declare this module for logging purposes
LOG_MODULE_DECLARE(k2_app);

// Network configuration constants
#define UDP_PORT 12345          // Port number for UDP server to listen on
#define RECV_BUFFER_SIZE 64     // Buffer size for incoming UDP messages

// Packet structure definition
typedef struct {
    uint32_t sequence;  // Sequence number
    uint64_t payload;   // Payload data
    uint32_t crc32;     // CRC32 checksum
} __attribute__((packed)) udp_packet_t;

// Static IP configuration - customize these for your network
#define STATIC_IP_ADDR "192.168.1.100"   // Device's static IP address
#define STATIC_NETMASK "255.255.255.0"   // Subnet mask
#define STATIC_GATEWAY "192.168.1.1"     // Default gateway address
#define TOPSIDE_IP "192.168.1.255"
#define SENSOR_PORT 5002

// Network management callback structure for handling interface events
static struct net_mgmt_event_callback mgmt_cb;
bool network_ready = false;  // Flag to track network interface status

// UDP socket and thread management variables
int udp_sock = -1;                                    // UDP socket file descriptor
K_THREAD_STACK_DEFINE(udp_thread_stack, 2048);      // Stack space for UDP thread
struct k_thread udp_thread_data;                     // Thread control block

// Pre-computed CRC32 lookup table for faster calculation
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
    0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
    0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
    0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
    0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
    0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
    0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
    0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
    0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
    0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
    0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
    0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
    0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
    0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
    0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
    0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
    0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
    0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
    0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
    0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
    0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693,
    0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

/**
 * Network management event handler - called when network interface events occur
 * @param cb: Callback structure (unused)
 * @param mgmt_event: Type of network management event
 * @param iface: Network interface that generated the event
 */
static void net_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                   uint64_t mgmt_event, struct net_if *iface)
{
    // Check if network interface came up
    if (mgmt_event == NET_EVENT_IF_UP) {
        LOG_INF("Network interface is up - network is ready");
        network_ready = true;  // Set flag to indicate network is available
    } 
    // Check if network interface went down
    else if (mgmt_event == NET_EVENT_IF_DOWN) {
        LOG_WRN("Network interface is down");
        network_ready = false;  // Clear network ready flag
    }
}

/**
 * Parse IPv4 address string into binary format
 * @param str: String representation of IP address (e.g., "192.168.1.100")
 * @param addr: Output structure to store parsed address
 * @return: 0 on success, negative error code on failure
 */
static inline int parse_ipv4_addr(const char *str, struct in_addr *addr) // ⚡ Added: inline
{
    unsigned int a, b, c, d;
    
    // Validate and parse the IPv4 address string
    if (sscanf(str, "%u.%u.%u.%u", &a, &b, &c, &d) != 4 || 
        a > 255 || b > 255 || c > 255 || d > 255) {
        return -EINVAL;
    }
    
    addr->s_addr = htonl((a << 24) | (b << 16) | (c << 8) | d);
    return 0;
}

/**
 * Configure static IP settings for the network interface
 * @param iface: Network interface to configure
 * @return: 0 on success, negative error code on failure
 */
static int configure_static_ip(struct net_if *iface)
{
    struct in_addr addr;     // IP address structure
    struct in_addr netmask;  // Netmask structure
    struct in_addr gateway;  // Gateway address structure
    int ret;

    // Configure IP address
    ret = parse_ipv4_addr(STATIC_IP_ADDR, &addr);
    if (ret < 0) {
        LOG_ERR("Invalid IP address format: %s", STATIC_IP_ADDR);
        return -EINVAL;
    }
    // Add the IP address to the interface with manual configuration
    net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0);

    // Configure netmask (subnet mask)
    ret = parse_ipv4_addr(STATIC_NETMASK, &netmask);
    if (ret < 0) {
        LOG_ERR("Invalid netmask format: %s", STATIC_NETMASK);
        return -EINVAL;
    }
    // Set the netmask for the configured IP address
    net_if_ipv4_set_netmask_by_addr(iface, &addr, &netmask);

    // Configure gateway (default route)
    ret = parse_ipv4_addr(STATIC_GATEWAY, &gateway);
    if (ret < 0) {
        LOG_ERR("Invalid gateway format: %s", STATIC_GATEWAY);
        return -EINVAL;
    }
    // Set the default gateway for the interface
    net_if_ipv4_set_gw(iface, &gateway);

    // Log the complete static IP configuration for verification
    LOG_INF("Static IP configuration:");
    LOG_INF("  IP: %s", STATIC_IP_ADDR);
    LOG_INF("  Netmask: %s", STATIC_NETMASK);
    LOG_INF("  Gateway: %s", STATIC_GATEWAY);

    return 0;  // Configuration successful
}

/**
 * Initialize the network subsystem with static IP configuration
 * This function sets up the network interface and applies static IP settings
 */
void network_init(void)
{
    struct net_if *iface;  // Network interface handle
    int ret;

    LOG_INF("Initializing network with static IP...");

    // Get the default network interface
    iface = net_if_get_default();
    if (!iface) {
        LOG_ERR("No network interface found");
        return;  // Cannot proceed without a network interface
    }

    // Initialize network management event callback to monitor interface status
    net_mgmt_init_event_callback(&mgmt_cb, net_mgmt_event_handler,
                                 NET_EVENT_IF_UP | NET_EVENT_IF_DOWN);
    // Register the callback with the network management subsystem
    net_mgmt_add_event_callback(&mgmt_cb);

    // Apply static IP configuration to the interface
    ret = configure_static_ip(iface);
    if (ret < 0) {
        LOG_ERR("Failed to configure static IP: %d", ret);
        return;  // Configuration failed
    }

    // Bring the network interface up (activate it)
    net_if_up(iface);
    
    // Allow some time for the interface to become operational
    k_sleep(K_MSEC(200));
    network_ready = true;  // Mark network as ready for use
    
    LOG_INF("Static IP configuration complete");
}

/**
 * Calculate CRC32 checksum using simple polynomial
 * @param data: Pointer to data to calculate CRC for
 * @param length: Length of data in bytes
 * @return: Calculated CRC32 value
 */
static inline uint32_t calculate_crc32(const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFF;
    
    // CRC32 polynomial (IEEE 802.3)
    for (size_t i = 0; i < length; i++) {
        uint8_t index = (crc ^ bytes[i]) & 0xFF;
        crc = (crc >> 8) ^ crc32_table[index];
    }
    
    return ~crc;  // Final inversion
}

/**
 * Convert 64-bit value from network byte order to host byte order
 * @param value: 64-bit value in network byte order
 * @return: 64-bit value in host byte order
 */
static inline uint64_t net_to_host_64(uint64_t value)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap64(value); // ⚡ Hardware instruction vs manual swapping
#else
    return value;
#endif
}

/**
 * UDP server thread function - handles incoming UDP messages
 * This thread runs continuously, listening for UDP packets and responding
 * @param arg1, arg2, arg3: Thread arguments (unused)
 */
void udp_server_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1); ARG_UNUSED(arg2); ARG_UNUSED(arg3);

    struct sockaddr_in bind_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    
    // 🚀 PERFORMANCE: Direct struct receive (eliminates memcpy overhead)
    udp_packet_t packet; // ⚡ Direct to struct instead of buffer + casting
    
    int ret;

    while (!network_ready) {
        k_sleep(K_MSEC(100));
    }

    udp_sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_sock < 0) {
        LOG_ERR("Failed to create UDP socket: %d", udp_sock); // ⚡ Essential: socket creation failure
        return;
    }

    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port = htons(UDP_PORT);

    ret = zsock_bind(udp_sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr));
    if (ret < 0) {
        LOG_ERR("Failed to bind UDP socket: %d", ret); // ⚡ Essential: bind failure
        zsock_close(udp_sock);
        return;
    }

    LOG_INF("UDP server ready on port %d", UDP_PORT); // ⚡ Essential: startup confirmation

    while (1) {
        //PERFORMANCE: Direct struct receive (no buffer copying)
        ret = zsock_recvfrom(udp_sock, &packet, sizeof(packet), 0, // ⚡ Direct to struct
                             (struct sockaddr *)&client_addr, &client_addr_len);

        if (ret == sizeof(udp_packet_t)) {
            //TESTING: Print all packet values
            uint32_t recv_sequence = ntohl(packet.sequence);
            uint64_t recv_payload = net_to_host_64(packet.payload);
            uint32_t recv_crc = ntohl(packet.crc32);
            
            //LOG_INF("=== PACKET RECEIVED ===");
            //LOG_INF("Sequence: %u", recv_sequence);
            //LOG_INF("Payload:  0x%016llX", recv_payload);
            //LOG_INF("CRC32:    0x%08X", recv_crc);
            
            // Calculate CRC32 for validation
            uint32_t calculated_crc = calculate_crc32(&packet, 
                sizeof(packet.sequence) + sizeof(packet.payload));
            
            //LOG_INF("Calc CRC: 0x%08X", calculated_crc);
            
            //TESTING: Clear match/mismatch indication
            if (calculated_crc == recv_crc) {
                //LOG_INF("CRC MATCH - Packet is valid!");
                //gpio_pin_toggle_dt(&led); // Visual feedback

                // Forward command to control system
                rov_send_command(recv_sequence, recv_payload);

                // Update resource monitor stats
                resource_monitor_inc_udp_rx();
            } else {
                LOG_ERR("CRC MISMATCH - Packet corrupted!");
                LOG_ERR("Expected: 0x%08X, Got: 0x%08X", calculated_crc, recv_crc);
                resource_monitor_inc_udp_errors();
            }
            //LOG_INF("========================");
            
        } else if (ret < 0) {
            LOG_ERR("UDP recv error: %d", ret);
            resource_monitor_inc_udp_errors();
            k_sleep(K_MSEC(100));
        } else {
            //TESTING: Print wrong packet sizes for debugging
            LOG_WRN("Wrong packet size: got %d bytes, expected %d bytes", 
                    ret, sizeof(udp_packet_t));
        }
    }
}

void sensor_sender_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1); ARG_UNUSED(arg2); ARG_UNUSED(arg3);
    
    int sock;
    struct sockaddr_in dest_addr;
    char buffer[128];
    int16_t acc[3], gyro[3];

    while (!network_ready) {
        k_sleep(K_MSEC(100));
    }

    sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        LOG_ERR("Failed to create sensor UDP socket");
        return;
    }

    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(SENSOR_PORT);

    int on = 1;
    zsock_setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
    zsock_inet_pton(AF_INET, TOPSIDE_IP, &dest_addr.sin_addr);


    LOG_INF("Sensor UDP sender started (Target: 192.168.1.2:%d)", SENSOR_PORT);

    while (1) {
        icm20948_get_latest(acc, gyro);

        // JSON format matching Topside/routes.py expectation for "9dof"
        // It expects a dict. Routes.py: get_section("9dof").
        // DataHandler reads file. So we need a Receiver on PC side to write to file.
        // Our receiver will expect JSON.
        int len = snprintf(buffer, sizeof(buffer), 
            "{\"9dof\":{\"accel\":[%d,%d,%d],\"gyro\":[%d,%d,%d]}}",
            acc[0], acc[1], acc[2], gyro[0], gyro[1], gyro[2]);

        if (len > 0) {
            zsock_sendto(sock, buffer, len, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        }

        k_msleep(50); // 20 Hz
    }
}

K_THREAD_DEFINE(sensor_tid, 2048, sensor_sender_thread, NULL, NULL, NULL, 7, 0, 0);

/**
 * Start the UDP server by creating and launching the server thread
 * This function creates a new thread that will handle all UDP server operations
 */
void udp_server_start(void)
{
    k_tid_t thread_id;
    
    // Create and start the UDP server thread
    thread_id = k_thread_create(&udp_thread_data,
                               udp_thread_stack,
                               K_THREAD_STACK_SIZEOF(udp_thread_stack),
                               udp_server_thread,
                               NULL, NULL, NULL,
                               K_PRIO_COOP(7),
                               0,
                               K_NO_WAIT);
    
    if (thread_id != NULL) {
        LOG_INF("UDP server thread created successfully");
    } else {
        LOG_ERR("Failed to create UDP server thread");
    }
}