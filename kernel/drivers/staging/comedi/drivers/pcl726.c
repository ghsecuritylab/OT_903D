


#include "../comedidev.h"

#include <linux/ioport.h>

#undef ACL6126_IRQ		/* no interrupt support (yet) */

#define PCL726_SIZE 16
#define PCL727_SIZE 32
#define PCL728_SIZE 8

#define PCL726_DAC0_HI 0
#define PCL726_DAC0_LO 1

#define PCL726_DO_HI 12
#define PCL726_DO_LO 13
#define PCL726_DI_HI 14
#define PCL726_DI_LO 15

#define PCL727_DO_HI 24
#define PCL727_DO_LO 25
#define PCL727_DI_HI  0
#define PCL727_DI_LO  1

static const struct comedi_lrange range_4_20mA = { 1, {RANGE_mA(4, 20)} };
static const struct comedi_lrange range_0_20mA = { 1, {RANGE_mA(0, 20)} };

static const struct comedi_lrange *const rangelist_726[] = {
	&range_unipolar5, &range_unipolar10,
	&range_bipolar5, &range_bipolar10,
	&range_4_20mA, &range_unknown
};

static const struct comedi_lrange *const rangelist_727[] = {
	&range_unipolar5, &range_unipolar10,
	&range_bipolar5,
	&range_4_20mA
};

static const struct comedi_lrange *const rangelist_728[] = {
	&range_unipolar5, &range_unipolar10,
	&range_bipolar5, &range_bipolar10,
	&range_4_20mA, &range_0_20mA
};

static int pcl726_attach(struct comedi_device *dev,
			 struct comedi_devconfig *it);
static int pcl726_detach(struct comedi_device *dev);

struct pcl726_board {

	const char *name;	/*  driver name */
	int n_aochan;		/*  num of D/A chans */
	int num_of_ranges;	/*  num of ranges */
	unsigned int IRQbits;	/*  allowed interrupts */
	unsigned int io_range;	/*  len of IO space */
	char have_dio;		/*  1=card have DI/DO ports */
	int di_hi;		/*  ports for DI/DO operations */
	int di_lo;
	int do_hi;
	int do_lo;
	const struct comedi_lrange *const *range_type_list;
	/*  list of supported ranges */
};

static const struct pcl726_board boardtypes[] = {
	{"pcl726", 6, 6, 0x0000, PCL726_SIZE, 1,
	 PCL726_DI_HI, PCL726_DI_LO, PCL726_DO_HI, PCL726_DO_LO,
	 &rangelist_726[0],},
	{"pcl727", 12, 4, 0x0000, PCL727_SIZE, 1,
	 PCL727_DI_HI, PCL727_DI_LO, PCL727_DO_HI, PCL727_DO_LO,
	 &rangelist_727[0],},
	{"pcl728", 2, 6, 0x0000, PCL728_SIZE, 0,
	 0, 0, 0, 0,
	 &rangelist_728[0],},
	{"acl6126", 6, 5, 0x96e8, PCL726_SIZE, 1,
	 PCL726_DI_HI, PCL726_DI_LO, PCL726_DO_HI, PCL726_DO_LO,
	 &rangelist_726[0],},
	{"acl6128", 2, 6, 0x0000, PCL728_SIZE, 0,
	 0, 0, 0, 0,
	 &rangelist_728[0],},
};

#define n_boardtypes (sizeof(boardtypes)/sizeof(struct pcl726_board))
#define this_board ((const struct pcl726_board *)dev->board_ptr)

static struct comedi_driver driver_pcl726 = {
	.driver_name = "pcl726",
	.module = THIS_MODULE,
	.attach = pcl726_attach,
	.detach = pcl726_detach,
	.board_name = &boardtypes[0].name,
	.num_names = n_boardtypes,
	.offset = sizeof(struct pcl726_board),
};

COMEDI_INITCLEANUP(driver_pcl726);

struct pcl726_private {

	int bipolar[12];
	const struct comedi_lrange *rangelist[12];
	unsigned int ao_readback[12];
};

#define devpriv ((struct pcl726_private *)dev->private)

static int pcl726_ao_insn(struct comedi_device *dev, struct comedi_subdevice *s,
			  struct comedi_insn *insn, unsigned int *data)
{
	int hi, lo;
	int n;
	int chan = CR_CHAN(insn->chanspec);

	for (n = 0; n < insn->n; n++) {
		lo = data[n] & 0xff;
		hi = (data[n] >> 8) & 0xf;
		if (devpriv->bipolar[chan])
			hi ^= 0x8;
		/*
		 * the programming info did not say which order
		 * to write bytes.  switch the order of the next
		 * two lines if you get glitches.
		 */
		outb(hi, dev->iobase + PCL726_DAC0_HI + 2 * chan);
		outb(lo, dev->iobase + PCL726_DAC0_LO + 2 * chan);
		devpriv->ao_readback[chan] = data[n];
	}

	return n;
}

static int pcl726_ao_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	int chan = CR_CHAN(insn->chanspec);
	int n;

	for (n = 0; n < insn->n; n++)
		data[n] = devpriv->ao_readback[chan];
	return n;
}

static int pcl726_di_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	if (insn->n != 2)
		return -EINVAL;

	data[1] = inb(dev->iobase + this_board->di_lo) |
	    (inb(dev->iobase + this_board->di_hi) << 8);

	return 2;
}

static int pcl726_do_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	if (insn->n != 2)
		return -EINVAL;

	if (data[0]) {
		s->state &= ~data[0];
		s->state |= data[0] & data[1];
	}
	if (data[1] & 0x00ff)
		outb(s->state & 0xff, dev->iobase + this_board->do_lo);
	if (data[1] & 0xff00)
		outb((s->state >> 8), dev->iobase + this_board->do_hi);

	data[1] = s->state;

	return 2;
}

static int pcl726_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	unsigned long iobase;
	unsigned int iorange;
	int ret, i;
#ifdef ACL6126_IRQ
	unsigned int irq;
#endif

	iobase = it->options[0];
	iorange = this_board->io_range;
	printk(KERN_WARNING "comedi%d: pcl726: board=%s, 0x%03lx ", dev->minor,
	       this_board->name, iobase);
	if (!request_region(iobase, iorange, "pcl726")) {
		printk(KERN_WARNING "I/O port conflict\n");
		return -EIO;
	}

	dev->iobase = iobase;

	dev->board_name = this_board->name;

	ret = alloc_private(dev, sizeof(struct pcl726_private));
	if (ret < 0)
		return -ENOMEM;

	for (i = 0; i < 12; i++) {
		devpriv->bipolar[i] = 0;
		devpriv->rangelist[i] = &range_unknown;
	}

#ifdef ACL6126_IRQ
	irq = 0;
	if (boardtypes[board].IRQbits != 0) {	/* board support IRQ */
		irq = it->options[1];
		devpriv->first_chan = 2;
		if (irq) {	/* we want to use IRQ */
			if (((1 << irq) & boardtypes[board].IRQbits) == 0) {
				printk(KERN_WARNING
					", IRQ %d is out of allowed range,"
					" DISABLING IT", irq);
				irq = 0;	/* Bad IRQ */
			} else {
				if (request_irq(irq, interrupt_pcl818, 0,
						"pcl726", dev)) {
					printk(KERN_WARNING
						", unable to allocate IRQ %d,"
						" DISABLING IT", irq);
					irq = 0;	/* Can't use IRQ */
				} else {
					printk(", irq=%d", irq);
				}
			}
		}
	}

	dev->irq = irq;
#endif

	printk("\n");

	ret = alloc_subdevices(dev, 3);
	if (ret < 0)
		return ret;

	s = dev->subdevices + 0;
	/* ao */
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE | SDF_GROUND;
	s->n_chan = this_board->n_aochan;
	s->maxdata = 0xfff;
	s->len_chanlist = 1;
	s->insn_write = pcl726_ao_insn;
	s->insn_read = pcl726_ao_insn_read;
	s->range_table_list = devpriv->rangelist;
	for (i = 0; i < this_board->n_aochan; i++) {
		int j;

		j = it->options[2 + 1];
		if ((j < 0) || (j >= this_board->num_of_ranges)) {
			printk
			    ("Invalid range for channel %d! Must be 0<=%d<%d\n",
			     i, j, this_board->num_of_ranges - 1);
			j = 0;
		}
		devpriv->rangelist[i] = this_board->range_type_list[j];
		if (devpriv->rangelist[i]->range[0].min ==
		    -devpriv->rangelist[i]->range[0].max)
			devpriv->bipolar[i] = 1;	/* bipolar range */
	}

	s = dev->subdevices + 1;
	/* di */
	if (!this_board->have_dio) {
		s->type = COMEDI_SUBD_UNUSED;
	} else {
		s->type = COMEDI_SUBD_DI;
		s->subdev_flags = SDF_READABLE | SDF_GROUND;
		s->n_chan = 16;
		s->maxdata = 1;
		s->len_chanlist = 1;
		s->insn_bits = pcl726_di_insn_bits;
		s->range_table = &range_digital;
	}

	s = dev->subdevices + 2;
	/* do */
	if (!this_board->have_dio) {
		s->type = COMEDI_SUBD_UNUSED;
	} else {
		s->type = COMEDI_SUBD_DO;
		s->subdev_flags = SDF_WRITABLE | SDF_GROUND;
		s->n_chan = 16;
		s->maxdata = 1;
		s->len_chanlist = 1;
		s->insn_bits = pcl726_do_insn_bits;
		s->range_table = &range_digital;
	}

	return 0;
}

static int pcl726_detach(struct comedi_device *dev)
{
/* printk("comedi%d: pcl726: remove\n",dev->minor); */

#ifdef ACL6126_IRQ
	if (dev->irq)
		free_irq(dev->irq, dev);
#endif

	if (dev->iobase)
		release_region(dev->iobase, this_board->io_range);

	return 0;
}
