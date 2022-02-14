#include <linux/unistd.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/hrtimer.h>
#include <linux/jiffies.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/spi/spi.h>
#include <linux/regmap.h>
#include <linux/skbuff.h>
#include <linux/of_gpio.h>
#include <linux/ieee802154.h>
#include <linux/debugfs.h>

#include <net/mac802154.h>
#include <net/cfg802154.h>

#include "at86rf215.h"

/* TODO: This structure will be deleted later: "rstn" will be used directly. */
struct at86rf215_platform_data {
	int rstn;
};

struct at86rf215_local;

struct at86rf215_chip_data {
	u16	t_power_to_off;
	u16	t_sleep_to_off;
	u16	t_dsleep_to_off;
	u16	t_reset_to_off;
	u16	t_off_to_prep;
	u16	t_off_to_rx;
	u16	t_prep_to_tx;
	u16	t_tx_start_delay;
	u16	t_prep_to_rx;
	u16	t_prep_to_off;
	u16	t_rx_to_off;
	u16	t_rx_to_prep;
	u16	t_txfe_to_prep;
	u16	t_txprep_to_prep;
	u16	t_tx_to_off;
	u16	t_vreg_settl;
	u16	t_xosc_settl;
	u16	t_pll_ch_switch;
	u16	t_rxfe;
	int	rssi_base_val;

	int	(*set_channel)(struct at86rf215_local *, u8, u8);
	int	(*set_txpower)(struct at86rf215_local *, s32);
};

#define AT86RF215_MAX_BUF               (2047 + 3)
/* We use the recommended 5 minutes timeout to recalibrate */
#define AT86RF215_CAL_LOOP_TIMEOUT      (5 * 60 * HZ)

struct at86rf215_state_change {
	struct at86rf215_local *lp;
	int			irq;

	struct hrtimer		timer;
	struct spi_message	msg;
	struct spi_transfer	trx;
	u8			buf[AT86RF215_MAX_BUF];

	void			(*complete)(void *context);
	u8			from_state;
	u8			to_state;

	bool			free;
};

struct at86rf215_local {
	struct spi_device *		spi;

	struct ieee802154_hw *		hw;
	struct at86rf215_chip_data *	data;
	struct regmap *			regmap;

	struct completion		state_complete;
	struct at86rf215_state_change	state;

	unsigned long			cal_timeout;
	bool				is_tx;
	bool				is_tx_from_off;
	u8				tx_retry;
	struct sk_buff *		tx_skb;
	struct at86rf215_state_change	tx;
};

static void
at86rf215_async_state_change(struct at86rf215_local *lp, struct
                             at86rf215_state_change *ctx, const u8 state,
                             void (*complete)(void *context));
static void at86rf215_async_state_change_start(void *context);
static void at86rf215_write(void *context);

static inline int at86rf215_read_subreg(struct at86rf215_local *lp,
					unsigned int addr, unsigned int mask,
					unsigned int shift, unsigned int *data)
{
	int rc;

	rc = regmap_read(lp->regmap, addr, data);
	if (!rc)
		*data = (*data & mask) >> shift;

	return rc;
}

static inline int at86rf215_write_subreg(struct at86rf215_local *lp,
					 unsigned int addr, unsigned int mask,
					 unsigned int shift, unsigned int data)
{
	return regmap_update_bits(lp->regmap, addr, mask, data << shift);
}

static bool at86rf215_reg_writeable(struct device *dev, unsigned int reg)
{
	/* For BBC0_FBTXS registers */
	if ((reg & 0x2800) == 0x2800)
		return true;
	if ((reg & 0X100) == 0X100)
		return true;

	switch (reg) {
	case RG_RF09_AUXS:
	case RG_BBC0_FBTXS:
	case RG_BBC0_OFDMPHRTX:
	case RG_RF09_CMD:
	case RG_RF09_STATE:
	case RG_BBC0_AMEDT:
	case RG_BBC0_AMCS:
	case RG_BBC0_AFC0:
	case RG_RF_CFG:
	case RG_BBC0_IRQM:
	case RG_RF09_IRQM:
	case RG_BBC0_TXFLL:
	case RG_BBC0_TXFLH:
	case RG_BBC0_PC:
	case RG_RF09_CCF0L:
	case RG_RF09_CCF0H:
	case RG_RF09_CS:
	case RG_RF09_CNL:
	case RG_RF09_CNM:
	case RG_RF09_RXBWC:
	case RG_RF09_RXDFE:
	case RG_RF09_EDD:
	case RG_RF09_TXCUTC:
	case RG_RF09_TXDFE:
	case RG_RF09_PAC:
	case RG_IQIFC1:
	case RG_RF09_EDC:
	case RG_BBC0_AFFTM:
	case RG_BBC0_PS:
	case RG_RF09_RSSI:
	case RG_RF09_RNDV:
		return true;
	default:
		return false;
	}
}

static bool at86rf215_reg_readable(struct device *dev, unsigned int reg)
{
	bool rc;

	/* all writeable are also readable */
	rc = at86rf215_reg_writeable(dev, reg);
	if (rc)
		return rc;

	/* readonly regs */
	switch (reg) {
	case RG_RF_PN:
	case RG_RF_VN:
	case RG_RF09_CMD:
	case RG_RF09_IRQS:
		return true;
	default:
		return false;
	}
}

/*TODO : To check why volatile ? */
static bool at86rf215_reg_volatile(struct device *dev, unsigned int reg)
{
	/* Can be changed during runtime */
	switch (reg) {
	case RG_RF09_CMD:
	case RG_RF09_STATE:
	case RG_RF09_IRQS:
	case RG_BBC0_IRQS:
		return true;
	default:
		return false;
	}
}

static bool at86rf215_reg_precious(struct device *dev, unsigned int reg)
{
	/* Don't clear irq line on read */
	switch (reg) {
	case RG_RF09_IRQS:
	case RG_BBC0_IRQS:
	case RG_BBC0_IRQM:
	case RG_RF09_IRQM:
		return true;
	default:
		return false;
	}
}

#define AT86RF215_NUMREGS 0xFFFFFFFF
static const struct regmap_config at86rf215_regmap_spi_config = {
	.reg_bits		= 16,
	.val_bits		= 8,
	.write_flag_mask	= CMD_WRITE,
	.read_flag_mask		= CMD_READ,
	.cache_type		= REGCACHE_RBTREE, /*Red Black tree algorithm*/
	.max_register		= AT86RF215_NUMREGS,
	.writeable_reg		= at86rf215_reg_writeable,
	.readable_reg		= at86rf215_reg_readable,
	.volatile_reg		= at86rf215_reg_volatile,
	.precious_reg		= at86rf215_reg_precious,
};

static void at86rf215_async_error_recover_complete(void *context)
{
	struct at86rf215_state_change *ctx = context;
	struct at86rf215_local *lp = ctx->lp;

	if (ctx->free)
		kfree(ctx);

	ieee802154_wake_queue(lp->hw);
}

static inline void
at86rf215_async_error(struct at86rf215_local *lp, struct
		      at86rf215_state_change *ctx, int rc)
{
	/* TODO: What state should we reach whenever an error happens ? */
	at86rf215_async_error_recover_complete(ctx);
}

static void
at86rf215_async_write_reg(struct at86rf215_local *lp, u16 reg, u8 val, struct
			  at86rf215_state_change *ctx, void (*complete)(
				  void *context))
{
	int rc;

	ctx->buf[0] = ((reg & CMD_REG_MSB) >> 8) | CMD_WRITE;
	ctx->buf[1] = reg & CMD_REG_LSB;
	ctx->buf[2] = val;

	ctx->msg.complete = complete;
	rc = spi_async(lp->spi, &ctx->msg);
	if (rc) {
		printk(KERN_DEBUG "spi_async failed in write_reg.");
		at86rf215_async_error(lp, ctx, rc);
	}
}

static void
at86rf215_async_read_reg(struct at86rf215_local *lp, u16 reg, struct
			 at86rf215_state_change *ctx, void (*complete)(
				 void *context))
{
	int rc;
	u8 *tx_buf = ctx->buf;

	tx_buf[0] = ((reg & CMD_REG_MSB) >> 8);
	tx_buf[1] = reg & CMD_REG_LSB;
	ctx->msg.complete = complete;
	rc = spi_async(lp->spi, &ctx->msg);
	if (rc) {
		printk(KERN_DEBUG "spi_async failed in read_reg.");
		at86rf215_async_error(lp, ctx, rc);
	}
}

static void at86rf215_async_state_assert(void *context)
{
	struct at86rf215_state_change *ctx = context;
	const u8 *buf = ctx->buf;
	const u8 trx_state = buf[2];

	if (trx_state != ctx->to_state) {
		printk(
			KERN_ALERT
			"[TIMEOUT]: We couldn't move from state %x to state %x .",
			ctx->from_state, ctx->to_state);
	} else {
		printk(KERN_DEBUG "We reached state: %x", trx_state);
		ctx->from_state = ctx->to_state;
		if (ctx->complete)
			ctx->complete(context);
	}
}

static enum hrtimer_restart at86rf215_async_state_timer(struct hrtimer *timer)
{
	struct at86rf215_state_change *ctx =
		container_of(timer, struct at86rf215_state_change, timer);
	struct at86rf215_local *lp = ctx->lp;

	at86rf215_async_read_reg(lp, RG_RF09_STATE, ctx,
				 at86rf215_async_state_assert);

	return HRTIMER_NORESTART;
}

static void at86rf215_irq_status(void *context)
{
	struct at86rf215_state_change *ctx = context;
	struct at86rf215_local *lp = ctx->lp;
	const u8 *buf = ctx->buf;
	u8 val = buf[2];

	enable_irq(lp->spi->irq);
	if (val & IRQS_4_TXFE) {
		printk(KERN_DEBUG "[INTERRUPTION]: Transmission complete!");
		/*disable_irq(lp->spi->irq);*/
	} else if (val & IRQS_0_RXFS) {
		printk(
			KERN_DEBUG
			"[INTERRUPTION]: A valid frame header is decoded.");
		disable_irq(lp->spi->irq);
	} else if (val) {
		printk(
			KERN_DEBUG
			"[INTERRUPTION]: Another interruption has occured.");
	} else {
		kfree(ctx);
	}
}

static void at86rf215_setup_spi_messages(struct at86rf215_local *	lp,
					 struct at86rf215_state_change *state)
{
	state->lp = lp;
	state->irq = lp->spi->irq;
	spi_message_init(&state->msg);  /* Initialize spi_message */
	state->msg.context = state;
	state->trx.len = 3;             /* 2 bytes(address) + 1 byte(value read/written) */
	state->trx.tx_buf = state->buf;
	state->trx.rx_buf = state->buf;
	spi_message_add_tail(&state->trx, &state->msg); /* Add the message */
	hrtimer_init(&state->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	state->timer.function = at86rf215_async_state_timer;
}

/* Request the IRQ and associate an interrupt handler with it */
static irqreturn_t at86rf215_isr(int irq, void *data)
{
	struct at86rf215_local *lp = data;
	struct at86rf215_state_change *ctx;
	int rc;

	/* Disables the interrupt associated with "irq" without waiting for any
	 * currently executing instances of the interrupt handler to return*/
	disable_irq_nosync(irq);

	ctx = kzalloc(sizeof(*ctx), GFP_ATOMIC);
	if (!ctx) {
		enable_irq(irq);
		return IRQ_NONE;
	}

	at86rf215_setup_spi_messages(lp, ctx);

	/* Tell on error handling to free ctx*/
	ctx->free = true;

	/* Determine which IRQ has occurred : read the IRQ Status*/
	ctx->buf[0] = (RG_BBC0_IRQS & CMD_REG_MSB) >> 8;
	ctx->buf[1] = RG_BBC0_IRQS & CMD_REG_LSB;
	ctx->msg.complete = at86rf215_irq_status;

	rc = spi_async(lp->spi, &ctx->msg);
	if (rc) {
		printk(KERN_DEBUG "Failed to request IRQ.");
		at86rf215_async_error(lp, ctx, rc);
		enable_irq(irq);
		return IRQ_NONE;
	}

	return IRQ_HANDLED;
}

static void at86rf215_async_state_delay(void *context)
{
	struct at86rf215_state_change *ctx = context;
	struct at86rf215_local *lp = ctx->lp;
	struct at86rf215_chip_data *c = lp->data;
	ktime_t tim = 0;

	switch (ctx->from_state) {
	case STATE_RF_TRXOFF:
		switch (ctx->to_state) {
		case STATE_RF_TXPREP:
			printk(
				KERN_DEBUG
				"[DELAY]: Setting delay for TRXOFF ==> TXPREP");
			tim = c->t_off_to_prep * NSEC_PER_USEC;
			goto change;
		case STATE_RF_RX:
			printk(
				KERN_DEBUG
				"[DELAY]: setting delay for TRXOFF ==> RX");
			tim = c->t_off_to_rx * NSEC_PER_USEC;
			goto change;
		default:
			break;
		}
	case STATE_RF_TXPREP:
		switch (ctx->to_state) {
		case STATE_RF_TX:
			printk(
				KERN_DEBUG
				"[DELAY]: setting delay for TXPREP ==> TX");
			tim = c->t_prep_to_tx;
			goto change;
		case STATE_RF_TRXOFF:
			printk(
				KERN_DEBUG
				"[DELAY]: setting delay for TXPREP ==> TRXOFF");
			tim = c->t_prep_to_off;
			goto change;
		default:
			break;
		}
	case STATE_RF_TX:
		switch (ctx->to_state) {
		case STATE_RF_TXPREP:
			printk(
				KERN_DEBUG
				"[DELAY]: setting delay for TX ==> TXPREP");
			tim = c->t_txfe_to_prep;
			goto change;
		default:
			break;
		}

	default:
		break;
	}

change:
	hrtimer_start(&ctx->timer, tim, HRTIMER_MODE_REL);
}

static void at86rf215_async_state_change_start(void *context)
{
	struct at86rf215_state_change *ctx = context;
	struct at86rf215_local *lp = ctx->lp;
	u8 *buffer = ctx->buf;

	const u8 trx_state = buffer[2];

	//printk(KERN_DEBUG "Actual state = %x", trx_state);

	if (trx_state == STATE_RF_TRANSITION) {
		printk(KERN_DEBUG "We're in a transition state.");
		at86rf215_async_read_reg(lp, RG_RF09_STATE, ctx,
					 at86rf215_async_state_change_start);
		return;
	}

	/* Check if we already are in the state which we wanna change to. */
	if (trx_state == ctx->to_state) {
		if (ctx->complete) {
			printk(KERN_DEBUG "trx_state == ctx->to_stat");
			ctx->from_state = trx_state;
			ctx->complete(context);
		}
		return;
	}

	if (!(ctx->from_state))
		ctx->from_state = trx_state;

	/* Set current state to the context of state change */
	printk(KERN_DEBUG "We're moving from state %x to state: %x",
	       ctx->from_state, ctx->to_state);
	ctx->from_state = trx_state;

	/* Going into the next step for a state change which do a timing
	 * relevant delay. */
	at86rf215_async_write_reg(lp, RG_RF09_CMD, ctx->to_state, ctx,
				  at86rf215_async_state_delay);
}

static void
at86rf215_async_state_change(struct at86rf215_local *lp,
			     struct at86rf215_state_change *ctx,
			     const u8 state, void (*complete)(void *context))
{
	/* Initialization for the state change context */
	ctx->to_state = state;
	printk(KERN_DEBUG "to->state = %x", ctx->to_state);
	if (complete)
		ctx->complete = complete;
	at86rf215_async_read_reg(lp, RG_RF09_STATE, ctx,
				 at86rf215_async_state_change_start);
}

static void at86rf215_write_frame_complete(void *context)
{
	struct at86rf215_state_change *ctx = context;
	struct at86rf215_local *lp = ctx->lp;

	ctx->complete = NULL;
	ctx->to_state = STATE_RF_TX;
	at86rf215_async_read_reg(lp, RG_RF09_STATE, ctx,
				 at86rf215_async_state_change_start);
}

static void at86rf215_write(void *context)
{
	struct at86rf215_state_change *ctx = context;
	struct at86rf215_local *lp = ctx->lp;
	struct sk_buff *skb = lp->tx_skb;
	u8 *buf = ctx->buf;
	int rc;

	/*lp->tx_skb = skb;*/
	buf[0] = ((RG_BBC0_FBTXS & CMD_REG_MSB) >> 8) | CMD_WRITE;
	buf[1] = RG_BBC0_FBTXS & CMD_REG_LSB;
	memcpy(buf + 2, skb->data, skb->len);
	ctx->trx.len = skb->len + 2;
	ctx->msg.complete = at86rf215_write_frame_complete;
	rc = spi_async(lp->spi, &ctx->msg);
	if (rc) {
		printk(
			KERN_ALERT "Impossible to write in BBC0_FBTXS registers");
		ctx->trx.len = 2;
		at86rf215_async_error(lp, ctx, rc);
	}
}

static int at86rf215_xmit(struct ieee802154_hw *hw, struct sk_buff *skb)
{
	struct at86rf215_local *lp = hw->priv;
	struct at86rf215_state_change *ctx = &lp->tx;
	void *y;
	int i;
	u8 x[100];

	/*buf = skb->data;*/
	lp->tx_skb = skb;
	lp->tx_retry = 0;

	printk(KERN_DEBUG "[xmit]: Starting ..");
	/*Debugging*/
	print_hex_dump(KERN_ALERT, "mem: ", DUMP_PREFIX_ADDRESS, 16, 1,
		       skb->data, skb->len, 1);
	at86rf215_async_state_change(lp, ctx, RF_TXPREP_STATUS,
				     at86rf215_write);

	return 0;
}

static int at86rf215_ed(struct ieee802154_hw *hw, u8 *level)
{
	printk(KERN_DEBUG "[CCA]: Starting ..");
	WARN_ON(!level);
	*level = 0xbe;
	return 0;
}

static int at86rf215_start(struct ieee802154_hw *hw)
{
	struct at86rf215_local *lp = hw->priv;

	printk(KERN_DEBUG "[start]: called. ");
	enable_irq(lp->spi->irq);
	return 0;
}

static void at86rf215_stop(struct ieee802154_hw *hw)
{
	struct at86rf215_local *lp = hw->priv;
	int rc;

	/* stop the continuous transmission.*/
	rc = at86rf215_write_subreg(lp, SR_BBC0_PC_CTX, 0x0);
	if (rc)
		printk(
			KERN_ALERT "Impossible to stop continuous transmission.");

	disable_irq(lp->spi->irq);
}

/* TODO:
 * 1. Before setting the channel, check if the state is on TRXOFF
 * 2. See with Madani if we should add equations here to determine the "Channel
 * Center Frequency" and "The channel Spacing" or not. */
/* The register CNM ( Both Channel Mode and Channel Number High Bit Settings
 * must be written "last". */
/* The register SR_RF09_CNM_CNH is only needed for frequency range (2400-2483.5)MHz,
 * MR-OFDM Option 4 and MR-FSK mode #1 for: TotalNumChan=416. So in our case, it's
 * gonna be set at 0. */
static int at86rf215_set_channel(struct at86rf215_local *lp, u8 page,
				 u8 channel)
{
	int rc;

	/*unsigned int cf_low=0, cf_high=0, cs=0;*/

	printk(KERN_DEBUG "[channel]: Setting ..");

	/*Channel Center Frequency*/
/*      cf_low = channel_freq & 0x00ff;
 *      cf_high = ( channel_freq & 0xff00 ) >> 8;
 *      rc = (regmap_write( lp->regmap, RG_RF09_CCF0L, cf_low) &&
 *      regmap_write( lp->regmap, RG_RF09_CCF0H, cf_high));
 *      if (rc)
 *              return rc;
 */
	/*Channel Spacing*/
/*      rc = regmap_write(lp->regmap, RG_RF09_CS, cs);
 *      if (rc)
 *              return rc;
 */
	if (channel > 10) {
		printk(KERN_DEBUG "Please insert a channel from 0 to 10");
		return rc;
	}

	/*Channel Number*/
/*	rc = (regmap_write(lp->regmap, RG_RF09_CNL, channel) ||
 *	at86rf215_write_subreg(lp, SR_RF09_CNM_CNH, 0x0));
 *      if (rc){
 *              printk(KERN_DEBUG "RG_RF09_CNL/SR_RF09_CNM_CNH can't be set");
 *              return rc;
 *      }
 */
	/*Channel Mode 0 : IEEE compliant channel scheme*/
/*      rc = at86rf215_write_subreg(lp, SR_RF09_CNM_CM, 0x0);
 *      if (rc){
 *              printk(KERN_DEBUG "SR_RF09_CNM_CM can't be set");
 *              return rc;
 *      }
 */
/*      rc = at86rf215_write_subreg(lp, SR_RF09_CNM_CNH, 0x0);
 *      if (rc){
 *              printk(KERN_DEBUG "RG_RF09_CNM can't be set");
 *              return rc;
 *      }
 *
 *      rc = regmap_write(lp->regmap, RG_RF09_CNM, 0x00);
 *      if (rc){
 *              printk(KERN_ALERT "RG_RF09_CNM cant be written.");
 *      }
 */
	return 0;
}

#define AT86RF215_MAX_ED_LEVELS 0xF /* 16 registers */
/* TODO: These values are so random, they should be edited. */
static const s32 at86rf215_ed_levels[AT86RF215_MAX_ED_LEVELS + 1] = {
	-9800, -9600, -9400, -9200, -9000, -8800, -8600, -8400, -8200, -8000,
	-7800, -7600, -7400, -7200, -7000, -6800
};

/* This function set the channel and the page + set the timeout to recalibrate
 * means form the current moment, set 5 minutes before the recalibration.
 * Jiffies : Kernel Internal value
 * TODO: WHY : + 10 ? */
static int at86rf215_channel(struct ieee802154_hw *hw, u8 page, u8 channel)
{
	struct at86rf215_local *lp = hw->priv;
	int rc;

	printk(KERN_DEBUG "_channel is being called");
	rc = lp->data->set_channel(lp, page, channel);
	/* Wait for PLL */
	usleep_range(lp->data->t_pll_ch_switch,
		     lp->data->t_pll_ch_switch + 10);

	lp->cal_timeout = jiffies + AT86RF215_CAL_LOOP_TIMEOUT;

	return rc;
}

#define AT86RF215_MAX_TX_POWERS 0x1F /* 32 registres */
/* PLease check datasheet page 50, register concerned: TXPWR */
static const s32 at86rf215_powers[AT86RF215_MAX_TX_POWERS + 1] =
{ 0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100, 1200, 1300, 1400,
 1500, 1600, 1700, 1800, 1900, 2000, 2100, 2200, 2300, 2400, 2500, 2600, 2700,
 2800, 2900, 3000, 3100 };

/* Change  the corresponding register with the suitable power value */
static int at86rf2xx_set_txpower(struct at86rf215_local *lp, s32 mbm)
{
	u32 i;

	for (i = 0; i < lp->hw->phy->supported.tx_powers_size; i++)
		if (lp->hw->phy->supported.tx_powers[i] == mbm)
			return at86rf215_write_subreg(lp, SR_RF09_PAC_TXPWR, i);

	return -EINVAL;
}

/* TODO: Combine "at86rf215_set_txpower" and "at86rf2xx_set_txpower" . */
/* Set the transmitter power */
static int at86rf215_set_txpower(struct ieee802154_hw *hw, s32 mbm)
{
	struct at86rf215_local *lp = hw->priv;

	return lp->data->set_txpower(lp, mbm);
}

/* This function search for the CCA level set ( in the function parameters )
* if it is supported. Then affected to the registre RG_BBC0_AMEDT.
* CCA reports a busy medium upon detecting energy above the ED threshold. */
static int at86rf215_set_cca_ed_level(struct ieee802154_hw *hw, s32 mbm)
{
	struct at86rf215_local *lp = hw->priv;
	u32 i;

	for (i = 0; i < hw->phy->supported.cca_ed_levels_size; i++)
		if (hw->phy->supported.cca_ed_levels[i] == mbm)
			return regmap_write(lp->regmap, RG_BBC0_AMEDT, i);

	return -EINVAL;
}

/* Promiscuous mode : The Automatic Acknowledgement shoud be disabled (resp.
 * enabled) and the promiscuous mode shoud be enabled (resp. disabled). */
static int at86rf215_set_promiscuous_mode(struct ieee802154_hw *hw,
					  const bool		on)
{
	struct at86rf215_local *lp = hw->priv;
	int rc;

	if (on) {
		rc = at86rf215_write_subreg(lp, SR_BBC0_AMCS_AACK, 1);
		if (rc < 0)
			return rc;

		rc = at86rf215_write_subreg(lp, SR_BBC0_AFC0_PM, 1);
		if (rc < 0)
			return rc;
	} else {
		rc = at86rf215_write_subreg(lp, SR_BBC0_AMCS_AACK, 0);
		if (rc < 0)
			return rc;

		rc = at86rf215_write_subreg(lp, SR_BBC0_AFC0_PM, 0);
		if (rc < 0)
			return rc;
	}

	return 0;
}

/* These functions represent callbacks that the 802.15.4 module call later */
static const struct ieee802154_ops at86rf215_ops = {
	/* It is mendatory to implement these functions. */
	.owner			= THIS_MODULE,
	.xmit_async		= at86rf215_xmit,
	.ed			= at86rf215_ed,
	.set_channel		= at86rf215_channel,
	.start			= at86rf215_start,
	.stop			= at86rf215_stop,
	/* The following functions are optional. */
	.set_txpower		= at86rf215_set_txpower,
	.set_cca_ed_level	= at86rf215_set_cca_ed_level,
	.set_promiscuous_mode	= at86rf215_set_promiscuous_mode,
};

/* Datasheet : page 189 (Transition time) */
static struct at86rf215_chip_data at86rf215_data = {
	.t_power_to_off		= 500,  /*us*/
	.t_sleep_to_off		= 1,    /*us*/
	.t_dsleep_to_off	= 500,  /*us*/
	.t_reset_to_off		= 1,    /*us*/
	.t_off_to_prep		= 200,  /*us*/
	.t_off_to_rx		= 90,   /*us*/
	.t_prep_to_tx		= 200,  /*ns*/
	.t_tx_start_delay	= 4,    /*us*/
	.t_prep_to_rx		= 200,  /*ns*/
	.t_prep_to_off		= 200,  /*ns*/
	.t_rx_to_off		= 200,  /*ns*/
	.t_rx_to_prep		= 200,  /*ns*/
	.t_txfe_to_prep		= 200,  /*ns*/ /*TX (TXFE) ==> TXPREP */
	.t_txprep_to_prep	= 33,   /*us*/ /*TX (CMD=TXPREP) ==> TXPREP */
	.t_tx_to_off		= 200,  /*ns*/ /*TX (CMD=TRXOFF) ==> TRXOFF */
	.t_vreg_settl		= 35,   /*us*/ /*Settling time of AVDD/DVDD */
	.t_xosc_settl		= 150,  /*us*/ /*Settling time of crystal OCS*/
	.t_pll_ch_switch	= 100,  /*us*/ /*Freq channel switch time PLL*/
	.t_rxfe			= 100,  /*us*/ /*RX(RXFE) depends on PHY mode*/
	.rssi_base_val		= -117,
	.set_channel		= at86rf215_set_channel,
	.set_txpower		= at86rf2xx_set_txpower,
};

static int at86rf215_hw_init(struct at86rf215_local *lp)
{
	int rc, irq_type, irq_pol = IRQ_ACTIVE_HIGH;
	unsigned int val;

	/* TODO: This should be replaced with an async function. */
	/* Check if we're in state TRXOFF */
	rc = regmap_read(lp->regmap, RG_RF09_STATE, &val);
	if ((rc) || (val != RF_TRXOFF_STATUS)) {
		printk(KERN_DEBUG "Hardware Initialisation: FAILED!");
		return rc;
	}

	/* Configuring the IRQ pin polarity. */
	irq_type = irq_get_trigger_type(lp->spi->irq);
	if (irq_type == IRQ_TYPE_EDGE_FALLING || irq_type == IRQ_TYPE_LEVEL_LOW)
		irq_pol = IRQ_ACTIVE_LOW;
	rc = at86rf215_write_subreg(lp, SR_RF_CFG_IRQP, irq_pol);
	if (rc) {
		printk(KERN_DEBUG "IRQ configuration: FAILED!");
		return rc;
	}

	return 0;
}

/* This configuration is custom for our application, just for test.*/
static int at86rf215_config(struct at86rf215_local *lp)
{
	int rc;

	rc = regmap_write(lp->regmap, RG_RF09_IRQM, 0x1F);
	if (rc) {
		printk(KERN_DEBUG "RG_RF09_IRQM: Impossible to write in.");
		return rc;
	}
	rc = regmap_write(lp->regmap, RG_RF09_CS, 0x30);
	if (rc) {
		printk(KERN_DEBUG "RG_RF09_CS: Impossible to write in.");
		return rc;
	}
	rc = regmap_write(lp->regmap, RG_RF09_CCF0L, 0x20);
	if (rc) {
		printk(KERN_DEBUG "RG_RF09_CCF0L: Impossible to write in.");
		return rc;
	}
	rc = regmap_write(lp->regmap, RG_RF09_CCF0H, 0x8D);
	if (rc) {
		printk(KERN_DEBUG "RG_RF09_CCF0H: Impossible to write in.");
		return rc;
	}
	rc = regmap_write(lp->regmap, RG_RF09_CNL, 0x03);
	if (rc) {
		printk(KERN_DEBUG "RG_RF09_CNL: Impossible to write in.");
		return rc;
	}
	rc = regmap_write(lp->regmap, RG_RF09_RXBWC, 0x09);
	if (rc) {
		printk(KERN_DEBUG "RG_RF09_RXBWC: Impossible to write in.");
		return rc;
	}
	rc = regmap_write(lp->regmap, RG_RF09_RXDFE, 0x83);
	if (rc) {
		printk(KERN_DEBUG "RG_RF09_RXDFE: Impossible to write in.");
		return rc;
	}
	rc = regmap_write(lp->regmap, RG_RF09_EDD, 0x7A);
	if (rc) {
		printk(KERN_DEBUG "RG_RF09_EDD: Impossible to write in.");
		return rc;
	}
	rc = regmap_write(lp->regmap, RG_RF09_TXCUTC, 0x0B);
	if (rc) {
		printk(KERN_DEBUG "RG_RF09_TXCUTC: Impossible to write in.");
		return rc;
	}
	rc = regmap_write(lp->regmap, RG_RF09_TXDFE, 0x83);
	if (rc) {
		printk(KERN_DEBUG "RG_RF09_TXDFE: Impossible to write in.");
		return rc;
	}
	rc = regmap_write(lp->regmap, RG_RF09_PAC, 0x7C);
	if (rc) {
		printk(KERN_DEBUG "RG_RF09_PAC: Impossible to write in.");
		return rc;
	}
	rc = regmap_write(lp->regmap, RG_BBC0_IRQM, 0x1f);
	if (rc)
		printk(KERN_ALERT "RG_BBC0_IRQM: Impossible to write in.");
	rc = regmap_write(lp->regmap, RG_BBC0_PC, 0x56);
	if (rc) {
		printk(KERN_ALERT "RG_BBC0_PC: Impossible to write in.");
		return rc;
	}
	rc = regmap_write(lp->regmap, RG_BBC0_OFDMPHRTX, 0x03);
	if (rc) {
		printk(KERN_ALERT "RG_BBC0_OFDMPHRTX: Impossible to write in.");
		return rc;
	}
	rc = regmap_write(lp->regmap, RG_RF09_CNM, 0x00);
	if (rc) {
		printk(KERN_ALERT "RG_RF09_CNM: Impossible to write in.");
		return rc;
	}
	return 0;
}

/* Check if device tree definition for the spi device is correct. */
static int at86rf215_get_pdata(struct spi_device *spi, int *rstn)
{
	struct at86rf215_platform_data *pdata = spi->dev.platform_data;

	/* dev.of_node :associated device tree node */
	if (!IS_ENABLED(CONFIG_OF) || !spi->dev.of_node) {
		if (!pdata)
			return -ENOENT;

		*rstn = pdata->rstn;
		return 0;
	}

	/* Get RESET GPIO Number */
	*rstn = of_get_named_gpio(spi->dev.of_node, "reset-gpio", 0);

	return 0;
}

static int at86rf215_detect_device(struct at86rf215_local *lp)
{
	unsigned int part, version;
	int rc;

	rc = regmap_read(lp->regmap, RG_RF_VN, &version);
	if (rc)
		return rc;
	printk(KERN_DEBUG "[Detecting]: AT86RF215 version: %x", version);

	rc = regmap_read(lp->regmap, RG_RF_PN, &part);
	if (rc)
		return rc;
	/* printk (KERN_DEBUG "[Detecting]: AT86RF215 PN: %x", part); */

	/* TODO: Check this stupid test. */
/*        if ( (version != 1) || (version != 3)) {
 *              dev_err(&lp->spi->dev, "VN %x doesn't exist. ", version);
 *              return -EINVAL;
 *      }
 *      if ((part != 52) | (part != 53) || (part != 54)) {
 *              dev_err(&lp->spi->dev, "PN %x doesn't exist.", part);
 *              return -EINVAL;
 *      }
 */

	/* Please check mac802154.h */
	lp->hw->flags = IEEE802154_HW_TX_OMIT_CKSUM |   /*Tx will add FCS aut*/
			IEEE802154_HW_RX_OMIT_CKSUM |   /*Rx will add FCS aut*/
			IEEE802154_HW_PROMISCUOUS;      /*Support promiscuous mode*/

	/* Please check cfg802154.h */
	lp->hw->phy->flags = WPAN_PHY_FLAG_TXPOWER |      /*TXPWR setting*/
			     WPAN_PHY_FLAG_CCA_ED_LEVEL | /*cca ED threshold */
			     WPAN_PHY_FLAG_CCA_MODE;      /*cca mode setting*/

	/* NL802154_CCA_ENERGY :Energy above threshold */
	/* (CCA-ED) is supported only, please refer to Datasheet (p148) */
	lp->hw->phy->supported.cca_modes = BIT(NL802154_CCA_ENERGY);
	lp->hw->phy->supported.cca_opts =
		BIT(NL802154_CCA_OPT_ENERGY_CARRIER_OR);
	lp->hw->phy->cca.mode = NL802154_CCA_ENERGY;

	lp->data = &at86rf215_data;

	/* 868 MHz BPSK 802.15.4-2003 */
	lp->hw->phy->supported.channels[0] |= 1;
	/* 915 MHz BPSK 802.15.4-2003 */
	lp->hw->phy->supported.channels[0] |= 0x7fe;
	/* 2.4 GHz O-QPSK 802.15.4-2003 */
	lp->hw->phy->supported.channels[0] |= 0x7FFF800;
	/* 868 MHz ASK 802.15.4-2006 */
	lp->hw->phy->supported.channels[1] |= 1;
	/* 915 MHz ASK 802.15.4-2006 */
	lp->hw->phy->supported.channels[1] |= 0x7fe;
	/* 868 MHz O-QPSK 802.15.4-2006 */
	lp->hw->phy->supported.channels[2] |= 1;
	/* 915 MHz O-QPSK 802.15.4-2006 */
	lp->hw->phy->supported.channels[2] |= 0x7fe;

	/* Select page and channel by default. */
	lp->hw->phy->current_channel = 3;
	lp->hw->phy->current_page = 2;

	/* Symbol_duration: la duree d'un symbole PSDU module et code */
	lp->hw->phy->symbol_duration = 4; /*(ttx_start_delay)*/
	lp->hw->phy->supported.tx_powers = at86rf215_powers;
	lp->hw->phy->supported.tx_powers_size = ARRAY_SIZE(at86rf215_powers);
	lp->hw->phy->supported.cca_ed_levels = at86rf215_ed_levels;
	lp->hw->phy->supported.cca_ed_levels_size = ARRAY_SIZE(
		at86rf215_ed_levels);

	/* Define the ED threshold + the transmitter power */
	lp->hw->phy->cca_ed_level = lp->hw->phy->supported.cca_ed_levels[7];
	lp->hw->phy->transmit_power = lp->hw->phy->supported.tx_powers[28];

	return rc;
}

static int at86rf215_probe(struct spi_device *spi)
{
	struct ieee802154_hw *hw;
	struct at86rf215_local *lp;
	int rc, rstn, irq_type;
	unsigned int status;

	pr_info("[Probing]: AT86RF215 probe function is called ..\n");

	if (!spi->irq) {
		dev_err(&spi->dev, "no IRQ specified\n");
		return -EINVAL;
	}

	pr_info("[Probing]: Checking if device tree is well configured ..\n");
	rc = at86rf215_get_pdata(spi, &rstn);
	if (rc < 0) {
		dev_err(&spi->dev,
			"failed to parse platform_data : %d .\n Please check your DT. ",
			rc);
		return rc;
	}

	/* Check if GPIO pins configured in the DT can be used. */
	if (gpio_is_valid(rstn)) { /* This function turns 0 if gpio is valid */
		/* Request a single GPIO with initial setup IF NOT */
		pr_info("[Probing]: requesting a RESET PIN for GPIO ..");
		rc = devm_gpio_request_one(&spi->dev, rstn, GPIOF_OUT_INIT_HIGH,
					   "rstn");
		if (rc)
			return rc;
	}

	if (gpio_is_valid(rstn)) {
		pr_info("[Probing]: The board is being reset.");
		udelay(1);
		gpio_set_value_cansleep(rstn, 0);
		udelay(1);
		gpio_set_value_cansleep(rstn, 1);
		usleep_range(120, 240);
	}

	/* It must be called once for each hardware device to allocate memory.
	 * It returns the address of the pointer lp->hw .*/
	hw = ieee802154_alloc_hw(sizeof(*lp), &at86rf215_ops);
	if (!hw)
		return -ENOMEM;

	lp = hw->priv;
	lp->hw = hw;
	lp->spi = spi;
	hw->parent = &spi->dev;

	/* TODO: Necessary ? */
	ieee802154_random_extended_addr(&hw->phy->perm_extended_addr);

	/* This function define SPI Protocol specifications. */
	lp->regmap = devm_regmap_init_spi(spi, &at86rf215_regmap_spi_config);
	if (IS_ERR(lp->regmap)) {
		rc = PTR_ERR(lp->regmap);
		dev_err(&spi->dev,
			"[Probing]: Failed to allocate register map: %d\n", rc);
		goto free_dev;
	}

	/* TODO: The following may be edited */
	at86rf215_setup_spi_messages(lp, &lp->state);
	at86rf215_setup_spi_messages(lp, &lp->tx);

	rc = at86rf215_detect_device(lp);
	if (rc) {
		printk(KERN_DEBUG "[Probing]: Device detecting failed.");
		goto free_dev;
	}

	/* This function initialize a dynamically allocated completion pointer
	 * for completion structure that is to be initialized */
	init_completion(&lp->state_complete);

	spi_set_drvdata(spi, lp); /* spi->dev->driver_data = lp */

	rc = at86rf215_hw_init(lp);
	if (rc)
		goto free_dev;

	irq_type = irq_get_trigger_type(spi->irq);
	if (!irq_type) {
		printk(KERN_DEBUG "Assigning an IRQ type to the IRQ line");
		irq_type = IRQF_TRIGGER_HIGH;
	}

	/* Request the IRQ and associate an interrupt handler with it.
	 * lp: is passed as an argument to at86rf215_isr */
	rc = devm_request_irq(&spi->dev, spi->irq, at86rf215_isr,
			      IRQF_SHARED | irq_type, dev_name(&spi->dev), lp);

	if (rc) {
		printk(KERN_ALERT "devm_request_irq FAILED.");
		goto free_dev;
	}

	/* disable_irq by default and wait for starting hardware */
	disable_irq(spi->irq);

	rc = ieee802154_register_hw(lp->hw);
	if (rc) {
		printk(KERN_ALERT "Unable to register the device");
		return rc;
	}
	printk(KERN_DEBUG "Device REGISTRED !");


	rc = at86rf215_config(lp);
	if (rc) {
		printk(KERN_ALERT "at86rf215_config FAILED.");
		goto free_dev;
	}

	rc = regmap_read(lp->regmap, RG_RF09_STATE, &status);
	if (rc) {
		printk(KERN_DEBUG "Error while reading RG_RF09_CMD");
		return rc;
	}
	if (status != RF_TRXOFF_STATUS)
		printk(KERN_DEBUG "The radio is OFF or bad wiring!");

	/* Set the frame length. Exple : 100 bytes */
	rc = regmap_write(lp->regmap, RG_BBC0_TXFLL, 0x64) ||
	     at86rf215_write_subreg(lp, SR_BBC0_TXFLH, 0x0);
	if (rc) {
		printk(KERN_ALERT "Error while writing frame length");
		return rc;
	}

	/* Continuous Transmition. */
	rc = at86rf215_write_subreg(lp, SR_BBC0_PC_CTX, 0x1);
	if (rc) {
		printk(KERN_ALERT "Error while writing RG_BBC0_PC_CTX");
		return rc;
	}

/*        rc = regmap_write(lp->regmap, RG_RF09_AUXS, 0x6);
 *      if (rc){
 *              printk(KERN_ALERT "Error while writing");
 *      }
 *
 *      rc = regmap_write(lp->regmap, RG_RF09_RSSI, 0x90);
 *      if (rc){
 *              printk(KERN_ALERT "Error while writing");
 *      }
 *
 *      rc = regmap_write(lp->regmap, RG_RF09_RNDV, 0x41);
 *      if (rc){
 *              printk(KERN_ALERT "Error while writing");
 *      }
 */
	return 0;

free_dev:
	printk(KERN_ALERT "free_dev!");
	ieee802154_free_hw(lp->hw);

	return rc;
}

static int at86rf215_remove(struct spi_device *spi)
{
	struct at86rf215_local *lp = spi_get_drvdata(spi);

	regmap_write(lp->regmap, RG_RF09_IRQM, 0x0000);
	ieee802154_unregister_hw(lp->hw);
	ieee802154_free_hw(lp->hw);
	dev_dbg(&spi->dev, "[AT85RF215] The driver is unregistered.");

	return 0;
}

static const struct of_device_id at86rf215_of_match[] = {
	{ .compatible = "atmel,at86rf215", },
	{ },
};
MODULE_DEVICE_TABLE(of, at86rf215_of_match);

/* List of SPI devices supported by this drive */
static const struct spi_device_id at86rf215_device_id[] = {
	{ .name = "at86rf215", },
	{ },
};
MODULE_DEVICE_TABLE(spi, at86rf215_device_id);

/* of_match_ptr : Find and read an array of u8 from a property.
 * of_match_table : The open firmware table. */
static struct spi_driver at86rf215_driver = {
	.id_table		= at86rf215_device_id,
	.driver			= {
		.of_match_table = of_match_ptr(at86rf215_of_match),
		.name		= "at86rf215",
	},
	.probe			= at86rf215_probe,
	.remove			= at86rf215_remove,
};

module_spi_driver(at86rf215_driver);

MODULE_DESCRIPTION("AT86RF215 Transceiver Driver");
MODULE_LICENSE("GPL v2");
