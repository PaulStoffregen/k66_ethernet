#include "IPAddress.h"

// set this to an unused IP number for your network
IPAddress myaddress(192, 168, 194, 67);

#define MACADDR1 0x04E9E5
#define MACADDR2 0x000001

// This test program prints a *lot* of info to the Arduino Serial Monitor
// Ping response time is approx 1.3 ms with 180 MHz clock, due to all the
// time spent printing.  To get a realistic idea of ping time, you would
// need to delete or comment out all the Serial print stuff.

typedef struct {
	uint16_t length;
	uint16_t flags;
	void *buffer;
	uint32_t moreflags;
	uint16_t checksum;
	uint16_t header;
	uint32_t dmadone;
	uint32_t timestamp;
	uint32_t unused1;
	uint32_t unused2;
} enetbufferdesc_t;

#define RXSIZE 12
#define TXSIZE 10
static enetbufferdesc_t rx_ring[RXSIZE] __attribute__ ((aligned(16)));
static enetbufferdesc_t tx_ring[TXSIZE] __attribute__ ((aligned(16)));
uint32_t rxbufs[RXSIZE*128] __attribute__ ((aligned(16)));
uint32_t txbufs[TXSIZE*128] __attribute__ ((aligned(16)));

// initialize the ethernet hardware
void setup()
{
	while (!Serial) ; // wait
	print("Ethernet Testing");
	MPU_RGDAAC0 |= 0x007C0000;
	SIM_SCGC2 |= SIM_SCGC2_ENET;
	CORE_PIN3_CONFIG =  PORT_PCR_MUX(4); // RXD1
	CORE_PIN4_CONFIG =  PORT_PCR_MUX(4); // RXD0
	CORE_PIN24_CONFIG = PORT_PCR_MUX(2); // REFCLK
	CORE_PIN25_CONFIG = PORT_PCR_MUX(4); // RXER
	CORE_PIN26_CONFIG = PORT_PCR_MUX(4); // RXDV
	CORE_PIN27_CONFIG = PORT_PCR_MUX(4); // TXEN
	CORE_PIN28_CONFIG = PORT_PCR_MUX(4); // TXD0
	CORE_PIN39_CONFIG = PORT_PCR_MUX(4); // TXD1
	CORE_PIN16_CONFIG = PORT_PCR_MUX(4); // MDIO
	CORE_PIN17_CONFIG = PORT_PCR_MUX(4); // MDC
	SIM_SOPT2 |= SIM_SOPT2_RMIISRC | SIM_SOPT2_TIMESRC(3);
	// ENET_EIR	1356	Interrupt Event Register
	// ENET_EIMR	1359	Interrupt Mask Register
	// ENET_RDAR	1362	Receive Descriptor Active Register
	// ENET_TDAR	1363	Transmit Descriptor Active Register
	// ENET_ECR	1363	Ethernet Control Register
	// ENET_RCR	1369	Receive Control Register
	// ENET_TCR	1372	Transmit Control Register
	// ENET_PALR/UR	1374	Physical Address
	// ENET_RDSR	1378	Receive Descriptor Ring Start
	// ENET_TDSR	1379	Transmit Buffer Descriptor Ring
	// ENET_MRBR	1380	Maximum Receive Buffer Size
	//		1457	receive buffer descriptor
	//		1461	transmit buffer descriptor

	print("enetbufferdesc_t size = ", sizeof(enetbufferdesc_t));
	print("rx_ring size = ", sizeof(rx_ring));
	memset(rx_ring, 0, sizeof(rx_ring));
	memset(tx_ring, 0, sizeof(tx_ring));

	for (int i=0; i < RXSIZE; i++) {
		rx_ring[i].flags = 0x8000; // empty flag
		rx_ring[i].buffer = rxbufs + i * 128;
	}
	rx_ring[RXSIZE-1].flags = 0xA000; // empty & wrap flags
	for (int i=0; i < TXSIZE; i++) {
		tx_ring[i].buffer = txbufs + i * 128;
	}
	tx_ring[TXSIZE-1].flags = 0x2000; // wrap flag

	ENET_EIMR = 0;
	ENET_RCR = ENET_RCR_NLC | ENET_RCR_MAX_FL(1522) | ENET_RCR_CFEN |
		ENET_RCR_CRCFWD | ENET_RCR_PADEN | ENET_RCR_RMII_MODE |
		/* ENET_RCR_FCE | */ ENET_RCR_PROM | ENET_RCR_MII_MODE;
	ENET_TCR = ENET_TCR_ADDINS | /* ENET_TCR_RFC_PAUSE | ENET_TCR_TFC_PAUSE | */
		ENET_TCR_FDEN;
	ENET_PALR = (MACADDR1 << 8) | ((MACADDR2 >> 16) & 255);
	ENET_PAUR = ((MACADDR2 << 8) & 0xFFFF0000) | 0x8808;
	ENET_OPD = 0x10014;
	ENET_IAUR = 0;
	ENET_IALR = 0;
	ENET_GAUR = 0;
	ENET_GALR = 0;
	ENET_RDSR = (uint32_t)rx_ring;
	ENET_TDSR = (uint32_t)tx_ring;
	ENET_MRBR = 512;
	ENET_TACC = ENET_TACC_SHIFT16;
	//ENET_TACC = ENET_TACC_SHIFT16 | ENET_TACC_IPCHK | ENET_TACC_PROCHK;
	ENET_RACC = ENET_RACC_SHIFT16;

	ENET_ECR = 0xF0000000 | ENET_ECR_DBSWP | ENET_ECR_EN1588 | ENET_ECR_ETHEREN;
	ENET_RDAR = ENET_RDAR_RDAR;
	ENET_TDAR = ENET_TDAR_TDAR;
}

// watch for data to arrive
void loop()
{
	static int rxnum=0;
	volatile enetbufferdesc_t *buf;

	buf = rx_ring + rxnum;

	if ((buf->flags & 0x8000) == 0) {
		incoming(buf->buffer, buf->length);
		if (rxnum < RXSIZE-1) {
			buf->flags = 0x8000;
			rxnum++;
		} else {
			buf->flags = 0xA000;
			rxnum = 0;
		}
	}
}

// when we get data, try to parse it
void incoming(void *packet, unsigned int len)
{
	const uint8_t *p8;
	const uint16_t *p16;
	const uint32_t *p32;
	IPAddress src, dst;
	uint16_t type;

	Serial.println();
	print("data, len=", len);
	p8 = (const uint8_t *)packet + 2;
	p16 = (const uint16_t *)p8;
	p32 = (const uint32_t *)packet;
	type = p16[6];
	if (type == 0x0008) {
		src = p32[7];
		dst = p32[8];
		Serial.print("IPv4 Packet, src=");
		Serial.print(src);
		Serial.print(", dst=");
		Serial.print(dst);
		Serial.println();
		printpacket(p8, len - 2);
		if (p8[23] == 1 && dst == myaddress) {
			Serial.println("  Protocol is ICMP:");
			if (p8[34] == 8) {
				print("  echo request:");
				uint16_t id = __builtin_bswap16(p16[19]);
				uint16_t seqnum = __builtin_bswap16(p16[20]);
				printhex("   id = ", id);
				print("   sequence number = ", seqnum);
				ping_reply((uint32_t *)packet, len);
			}
		}
	} else if (type == 0x0608) {
		Serial.println("ARP Packet:");
		printpacket(p8, len - 2);
		if (p32[4] == 0x00080100 && p32[5] == 0x01000406) {
			// request is for IPv4 address of ethernet mac
			IPAddress from((p16[15] << 16) | p16[14]);
			IPAddress to(p32[10]);
			Serial.print("  Who is ");
			Serial.print(to);
			Serial.print(" from ");
			Serial.print(from);
			Serial.print(" (");
			printmac(p8 + 22);
			Serial.println(")");
			if (to == myaddress) {
				arp_reply(p8+22, from);
			}
		}
	}
}

// compose an answer to ARP requests
void arp_reply(const uint8_t *mac, IPAddress &ip)
{
	uint32_t packet[11]; // 42 bytes needed + 2 pad
	uint8_t *p = (uint8_t *)packet + 2;

	packet[0] = 0;       // first 2 bytes are padding
	memcpy(p, mac, 6);
	memset(p + 6, 0, 6); // hardware automatically adds our mac addr
	//p[6] = (MACADDR1 >> 16) & 255;
	//p[7] = (MACADDR1 >> 8) & 255;
	//p[8] = (MACADDR1) & 255;
	//p[9] = (MACADDR2 >> 16) & 255; // this is how to do it the hard way
	//p[10] = (MACADDR2 >> 8) & 255;
	//p[11] = (MACADDR2) & 255;
	p[12] = 8;
	p[13] = 6;  // arp protocol
	packet[4] = 0x00080100; // IPv4 on ethernet
	packet[5] = 0x02000406; // reply, ip 4 byte, macaddr 6 bytes
	packet[6] = (__builtin_bswap32(MACADDR1) >> 8) | ((MACADDR2 << 8) & 0xFF000000);
	packet[7] = __builtin_bswap16(MACADDR2 & 0xFFFF) | ((uint32_t)myaddress << 16);
	packet[8] = (((uint32_t)myaddress & 0xFFFF0000) >> 16) | (mac[0] << 16) | (mac[1] << 24);
	packet[9] = (mac[5] << 24) | (mac[4] << 16) | (mac[3] << 8) | mac[2];
	packet[10] = (uint32_t)ip;
	Serial.println("ARP Reply:");
	printpacket(p, 42);
	outgoing(packet, 44);
}

// compose an reply to pings
void ping_reply(const uint32_t *recv, unsigned int len)
{
	uint32_t packet[32];
	uint8_t *p8 = (uint8_t *)packet + 2;

	if (len > sizeof(packet)) return;
	memcpy(packet, recv, len);
	memcpy(p8, p8 + 6, 6); // send to the mac address we received
	// hardware automatically adds our mac addr
	packet[8] = packet[7]; // send to the IP number we received
	packet[7] = (uint32_t)myaddress;
	p8[34] = 0;            // type = echo reply
	// TODO: checksums in IP and ICMP headers - is the hardware
	// really inserting correct checksums automatically?
	printpacket((uint8_t *)packet + 2, len - 2);
	outgoing(packet, len);
}

// transmit a packet
void outgoing(void *packet, unsigned int len)
{
	static int txnum=0;
	volatile enetbufferdesc_t *buf;
	uint16_t flags;

	buf = tx_ring + txnum;
	flags = buf->flags;
	if ((flags & 0x8000) == 0) {
		print("tx, num=", txnum);
		buf->length = len;
		memcpy(buf->buffer, packet, len);
		buf->flags = flags | 0x8C00;
		ENET_TDAR = ENET_TDAR_TDAR;
		if (txnum < TXSIZE-1) {
			txnum++;
		} else {
			txnum = 0;
		}
	}
}

// misc print functions, for lots of info in the serial monitor.
// this stuff probably slows things down and would need to go
// for any hope of keeping up with full ethernet data rate!

void print(const char *s)
{
	Serial.println(s);
}

void print(const char *s, int num)
{
	Serial.print(s);
	Serial.println(num);
}

void printhex(const char *s, int num)
{
	Serial.print(s);
	Serial.println(num, HEX);
}

void printmac(const uint8_t *data)
{
	Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X",
		data[0], data[1], data[2], data[3], data[4], data[5]);
}

void printpacket(const uint8_t *data, unsigned int len)
{
#if 1
	unsigned int i;

	for (i=0; i < len; i++) {
		Serial.printf(" %02X", *data++);
		if ((i & 15) == 15) Serial.println();
	}
	Serial.println();
#endif
}


