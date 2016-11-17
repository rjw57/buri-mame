// license:BSD-3-Clause
// copyright-holders:Rich Wareham
/*
 * Buri driver.
 */

#include "emu.h"

#include "bus/rs232/rs232.h"
#include "cpu/g65816/g65816.h"
#include "machine/6522via.h"
#include "machine/ins8250.h"
#include "machine/mos6551.h"
#include "machine/pckeybrd.h"
#include "sound/3812intf.h"
#include "video/tms9928a.h"

const char* VIA6522_TAG = "via6522";
const char* MOS6551_TAG = "mos6551";
const char* YM3812_TAG = "ym3812";
const char* TMS9929_TAG = "tms9929";
const char* KEYBOARD_TAG = "pc_keyboard";

const char* UART1_TAG = "uart1";

const int VIA6522_START = 0xDEF0;
const int MOS6551_START = 0xDFFC;
const int TMS9929_START = 0xDE00;
const int YM3812_START = 0xDE02;

// Interesting wrinkles of Buri hardware
// =====================================
//
// SPI
// ---
//
// Buri exposes a SPI interface to peripherals via the 6522 VIA:
//
//           |     |
//       PA0 |-->--| CLK
// VIA   PA1 |-->--| MOSI   SPI peripheral
//       PA7 |--<--| MISO
//           |     |
//
// Lines PA2, PA3 and PA4 are connected to a 74138 3-to-8 decoder to provide the
// chip select lines for the peripherals. Consequently there can be up to 8 SPI
// peripherals attached.
//
// The CA1 line is used as a peripheral interrupt. A rising edge on the CA1 line
// causes an interrupt on the processor.
//
// Well known peripherals:
//
//     1 - Keyboard

enum spi_mode_t { SPI_MODE0, SPI_MODE1, SPI_MODE2, SPI_MODE3 };
enum spi_data_direction_t { SPI_MSB_FIRST, SPI_LSB_FIRST };

class spi_bus
{
public:
	spi_bus(spi_mode_t mode, spi_data_direction_t data_dir):
		m_mode(mode), m_data_dir(data_dir),
		m_selected(false),
		m_clk(0), m_mosi(0), m_miso(0),
		m_recv_byte(0), m_send_byte(0),
		m_recv_count(0), m_send_count(0)
	{ }

	// Called when a byte has been exchanged. Return new byte to send.
	virtual uint8_t exchange(uint8_t recv) = 0;

	void clk_w(int state);
	void select_w(int state);
	void mosi_w(int state) { if(m_selected) { m_mosi = (state != 0); } }
	int miso_r() { return m_miso; }

private:
	spi_mode_t m_mode;
	spi_data_direction_t m_data_dir;
	bool m_selected;
	int m_clk;
	int m_mosi;
	int m_miso;

	uint8_t m_recv_byte, m_send_byte;
	int m_recv_count, m_send_count;

	void clk_edge_(int is_idle_to_active);

	inline int cpol() {
		return ((m_mode == SPI_MODE2) || (m_mode == SPI_MODE3)) ? 1 : 0;
	}

	inline int cpha() {
		return ((m_mode == SPI_MODE1) || (m_mode == SPI_MODE3)) ? 1 : 0;
	}
};

void spi_bus::select_w(int state) {
	if(state != m_selected) {
		if(state) {
			// newly selected, clear recv/send counts
			m_recv_count = m_send_count = 0;
		}
	}
	m_selected = (state != 0);
}

void spi_bus::clk_w(int state)
{
	// NOP if no change or not selected
	if(!m_selected or (state == m_clk)) { return; }
	m_clk = state;

	if(state) {
		// low to high
		clk_edge_(cpol() ? 0 : 1);
	} else {
		// high to low
		clk_edge_(cpol() ? 1 : 0);
	}
}

void spi_bus::clk_edge_(int is_idle_to_active)
{
	int read_mosi = cpha() ? (!is_idle_to_active) : is_idle_to_active;
	if(read_mosi) {
		// Should read MOSI on this edge
		if(m_data_dir == SPI_MSB_FIRST) {
			m_recv_byte <<= 1;
			m_recv_byte |= m_mosi ? 1 : 0;
		} else {
			m_recv_byte >>= 1;
			m_recv_byte |= m_mosi ? 0x80 : 0x00;
		}
		++m_recv_count;
	} else {
		// Should set MISO on this edge
		if(m_data_dir == SPI_MSB_FIRST) {
			m_miso = (m_send_byte & 0x80) ? 1 : 0;
			m_send_byte <<= 1;
		} else {
			m_miso = m_send_byte & 0x1;
			m_send_byte >>= 1;
		}
		++m_send_count;
	}

	if((m_recv_count == 8) && (m_send_count == 8)) {
		// sent and received an entire byte
		m_send_byte = exchange(m_recv_byte);

		// prepare MISO
		if(m_data_dir == SPI_MSB_FIRST) {
			m_miso = (m_send_byte & 0x80) ? 1 : 0;
		} else {
			m_miso = m_send_byte & 0x1;
		}
	}
}

class spi_keyboard : public spi_bus
{
public:
	spi_keyboard() :
		spi_bus(SPI_MODE0, SPI_MSB_FIRST)
	{ }

	virtual uint8_t exchange(uint8_t recv) {
		uint8_t send = 0x23;
		printf("keybd recv: 0x%02x, will send 0x%02x\n",
			recv, send);
		return send;
	}
};

class buri_state : public driver_device
{
public:
	buri_state(const machine_config &mconfig,
	           device_type type, const char *tag) :
	           driver_device(mconfig, type, tag),
	           m_maincpu(*this, "maincpu"),
	           m_mos6551(*this, MOS6551_TAG),
	           m_tms2998a(*this, TMS9929_TAG),
	           m_keyboard(*this, KEYBOARD_TAG),
	           m_via6522(*this, VIA6522_TAG)
	{
		m_irqs.val = 0;
	}

	DECLARE_WRITE_LINE_MEMBER(mos6551_irq_w);
	DECLARE_WRITE_LINE_MEMBER(tms9929a_irq_w);
	DECLARE_WRITE_LINE_MEMBER(via6522_irq_w);
	DECLARE_WRITE_LINE_MEMBER(keyboard_data_ready);

	DECLARE_WRITE8_MEMBER(via_pa_w);

	required_device<cpu_device> m_maincpu;
	required_device<mos6551_device> m_mos6551;
	required_device<tms9929a_device> m_tms2998a;
	required_device<pc_keyboard_device> m_keyboard;
	required_device<via6522_device> m_via6522;

	union {
		uint32_t val;
		struct {
			int mos6551 : 1;
			int tms9929a : 1;
			int via6522 : 1;
		} flags;
	} m_irqs;

private:
	void irqs_updated_() {
		m_maincpu->set_input_line(
			G65816_LINE_IRQ,
			m_irqs.val ? ASSERT_LINE : CLEAR_LINE);
	}

	spi_keyboard m_spi_keyboard;
};

static ADDRESS_MAP_START(buri_mem, AS_PROGRAM, 8, buri_state)
	AM_RANGE(0x0000, 0xC000) AM_RAM
	AM_RANGE(0xE000, 0xFFFF) AM_ROM AM_REGION("maincpu", 0)
	AM_RANGE(0x010000, 0x07FFFF) AM_RAM
	AM_RANGE(0xE0C000, 0xE0FFFF) AM_RAM

	// VDP
	AM_RANGE(TMS9929_START, TMS9929_START)
		AM_DEVREADWRITE(TMS9929_TAG, tms9929a_device,
				vram_read, vram_write)
	AM_RANGE(TMS9929_START + 1, TMS9929_START + 1)
		AM_DEVREADWRITE(TMS9929_TAG, tms9929a_device,
				register_read, register_write)

	// ACIA
	AM_RANGE(MOS6551_START, MOS6551_START + 3)
		AM_DEVREADWRITE(MOS6551_TAG, mos6551_device, read, write)

	// SOUND
	AM_RANGE(YM3812_START, YM3812_START + 1)
		AM_DEVREADWRITE(YM3812_TAG, ym3812_device, read, write)

	// VIA
	AM_RANGE(VIA6522_START, VIA6522_START + 15)
		AM_DEVREADWRITE(VIA6522_TAG, via6522_device, read, write)
ADDRESS_MAP_END

static INPUT_PORTS_START(buri)
	PORT_INCLUDE(pc_keyboard)
INPUT_PORTS_END

static DEVICE_INPUT_DEFAULTS_START(terminal)
	DEVICE_INPUT_DEFAULTS( "RS232_TXBAUD", 0xff, RS232_BAUD_19200 )
	DEVICE_INPUT_DEFAULTS( "RS232_RXBAUD", 0xff, RS232_BAUD_19200 )
	DEVICE_INPUT_DEFAULTS( "RS232_STARTBITS", 0xff, RS232_STARTBITS_1 )
	DEVICE_INPUT_DEFAULTS( "RS232_DATABITS", 0xff, RS232_DATABITS_8 )
	DEVICE_INPUT_DEFAULTS( "RS232_PARITY", 0xff, RS232_PARITY_NONE )
	DEVICE_INPUT_DEFAULTS( "RS232_STOPBITS", 0xff, RS232_STOPBITS_1 )
DEVICE_INPUT_DEFAULTS_END

static MACHINE_CONFIG_START(buri, buri_state)
	MCFG_CPU_ADD("maincpu", G65816, XTAL_2MHz)
	MCFG_CPU_PROGRAM_MAP(buri_mem)

	MCFG_DEVICE_ADD(MOS6551_TAG, MOS6551, 0)
	MCFG_MOS6551_XTAL(XTAL_1_8432MHz)
	MCFG_MOS6551_TXD_HANDLER(DEVWRITELINE(UART1_TAG, rs232_port_device, write_txd))
	MCFG_MOS6551_IRQ_HANDLER(WRITELINE(buri_state, mos6551_irq_w))

	MCFG_RS232_PORT_ADD(UART1_TAG, default_rs232_devices, "pty")
	MCFG_RS232_RXD_HANDLER(
		DEVWRITELINE(MOS6551_TAG, mos6551_device, write_rxd))
	MCFG_RS232_DCD_HANDLER(
		DEVWRITELINE(MOS6551_TAG, mos6551_device, write_dcd))
	MCFG_RS232_DSR_HANDLER(
		DEVWRITELINE(MOS6551_TAG, mos6551_device, write_dsr))
	MCFG_RS232_CTS_HANDLER(
		DEVWRITELINE(MOS6551_TAG, mos6551_device, write_cts))

	MCFG_DEVICE_CARD_DEVICE_INPUT_DEFAULTS("pty", terminal)
	MCFG_DEVICE_CARD_DEVICE_INPUT_DEFAULTS("terminal", terminal)

	MCFG_DEVICE_ADD(TMS9929_TAG, TMS9929A, XTAL_10_738635MHz)
	MCFG_TMS9928A_VRAM_SIZE(0x4000)
	MCFG_TMS9928A_OUT_INT_LINE_CB(WRITELINE(buri_state, tms9929a_irq_w))
	MCFG_TMS9928A_SCREEN_ADD_PAL( "screen" )
	MCFG_SCREEN_UPDATE_DEVICE( TMS9929_TAG, tms9929a_device, screen_update )

	MCFG_SPEAKER_STANDARD_MONO("mono")
	MCFG_SOUND_ADD(YM3812_TAG, YM3812, XTAL_3_579545MHz)
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "mono", 1.00)

	MCFG_PC_KEYB_ADD(KEYBOARD_TAG, WRITELINE(buri_state, keyboard_data_ready))

	MCFG_DEVICE_ADD(VIA6522_TAG, VIA6522, XTAL_2MHz)
	MCFG_VIA6522_WRITEPA_HANDLER(WRITE8(buri_state, via_pa_w))
	MCFG_VIA6522_IRQ_HANDLER(WRITELINE(buri_state, via6522_irq_w))
MACHINE_CONFIG_END

ROM_START(buri)
	ROM_REGION(0x2000, "maincpu", 0)
	ROM_LOAD("burios.bin", 0x0000, 0x2000, CRC(e527d758))
ROM_END

WRITE_LINE_MEMBER(buri_state::mos6551_irq_w)
{
	m_irqs.flags.mos6551 = (state != 0);
	irqs_updated_();
}

WRITE_LINE_MEMBER(buri_state::tms9929a_irq_w)
{
	m_irqs.flags.tms9929a = (state != 0);
	irqs_updated_();
}

WRITE_LINE_MEMBER(buri_state::via6522_irq_w)
{
	m_irqs.flags.via6522 = (state != 0);
	irqs_updated_();
}

WRITE_LINE_MEMBER(buri_state::keyboard_data_ready)
{
	// Called when there is data to be read from the keyboard.
	if(state) {
		uint8_t v = m_keyboard->read(machine().dummy_space(), 0);
		m_via6522->write_pb(machine().dummy_space(), 0, v);

		// Pulse CA1
		m_via6522->write_cb1(1);
		m_via6522->write_cb1(0);
	}
}

WRITE8_MEMBER(buri_state::via_pa_w)
{
	int clk = data & 0x1;
	int device = (data & 0x1C) >> 2;
	int mosi = (data & 0x2) ? 1 : 0;

	m_spi_keyboard.select_w(device == 1);
	m_spi_keyboard.clk_w(clk);
	m_spi_keyboard.mosi_w(mosi);
}

/*    YEAR  NAME    PARENT  COMPAT   MACHINE    INPUT    CLASS         INIT    COMPANY                FULLNAME               FLAGS */
COMP(2016,  buri,   0,      0,       buri,      buri,    driver_device,0,      "Rich Wareham", "Búri homebrew computer", MACHINE_TYPE_COMPUTER)
