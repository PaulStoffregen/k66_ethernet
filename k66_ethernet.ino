#include "IPAddress.h"

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

#define MACADDR1 0x04E9E5
#define MACADDR2 0x000001

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
		ENET_RCR_FCE | ENET_RCR_PROM | ENET_RCR_MII_MODE;
	ENET_TCR = ENET_TCR_ADDINS | ENET_TCR_RFC_PAUSE | ENET_TCR_TFC_PAUSE |
		ENET_TCR_FDEN;
	ENET_PALR = (MACADDR1 << 24) | MACADDR2;
	ENET_PAUR = ((0x04E9E5 << 8) & 0xFFFF0000) | 0x8808;
	ENET_OPD = 0x10014;
	ENET_IAUR = 0;
	ENET_IALR = 0;
	ENET_GAUR = 0;
	ENET_GALR = 0;
	ENET_RDSR = (uint32_t)rx_ring;
	ENET_TDSR = (uint32_t)tx_ring;
	ENET_MRBR = 512;
	ENET_TACC = ENET_TACC_SHIFT16;
	ENET_RACC = ENET_RACC_SHIFT16;

	ENET_ECR = 0xF0000000 | ENET_ECR_DBSWP | ENET_ECR_EN1588 | ENET_ECR_ETHEREN;
	ENET_RDAR = ENET_RDAR_RDAR;
	ENET_TDAR = ENET_TDAR_TDAR;
}

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

void incoming(void *packet, unsigned int len)
{
	const uint8_t *p8;
	const uint16_t *p16;
	const uint32_t *p32;
	IPAddress src, dst;
	uint16_t type;
	unsigned int i;

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
		for (i=0; i < len-2; i++) {
			Serial.printf(" %02X", p8[i]);
			if ((i & 15) == 15) Serial.println();
		}
		Serial.println();
		if (p8[23] == 1) {
			Serial.println("  Protocol is ICMP:");
			if (p8[34] == 8) {
				print("  echo request:");
				uint16_t id = __builtin_bswap16(p16[19]);
				uint16_t seqnum = __builtin_bswap16(p16[20]);
				printhex("   id = ", id);
				printhex("   sequence number = ", seqnum);
			}
		}
	} else if (type == 0x0608) {
		Serial.println("ARP Packet:");
		for (i=0; i < len-2; i++) {
			Serial.printf(" %02X", p8[i]);
			if ((i & 15) == 15) Serial.println();
		}
		Serial.println();
		if (p32[4] == 0x00080100 && p32[5] == 0x01000406) {
			// request is for IPv4 address of ethernet mac
			IPAddress from((p16[15] << 16) | p16[14]);
			IPAddress to(p32[10]);
			Serial.print("  Request for ");
			Serial.print(to);
			Serial.print(" from ");
			Serial.print(from);
			Serial.print(" (");
			printmac(p8 + 22);
			Serial.println(")");
		}
	}
}


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




