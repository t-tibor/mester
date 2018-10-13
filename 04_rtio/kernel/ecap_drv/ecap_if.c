#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/uaccess.h>

#include <linux/miscdevice.h>
#include <asm/ioctl.h>
#include <linux/sysfs.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/circ_buf.h>
#include <linux/clk.h>
#include <linux/interrupt.h>

#include <linux/pm_runtime.h>

#include "../event_mux/event_mux.h"
#include "../icap_channel/icap_channel.h"


// 4.4 and 4.12 kernels behave differently:
// @ 4.4 kernel the eCAP clock is disabled after the driver swap, so it has to be enabled,
// by using the appropriate function of the pwmss parent module.
#define OLD_KERNEL

#ifdef OLD_KERNEL
#include <linux/mutex.h>

	// copied from /drivers/pwm/pwm-tipwmss.h
	#define PWMSS_ECAPCLK_EN	BIT(0)
	#define PWMSS_ECAPCLK_STOP_REQ	BIT(1)
	#define PWMSS_EPWMCLK_EN	BIT(8)
	#define PWMSS_EPWMCLK_STOP_REQ	BIT(9)

	#define PWMSS_ECAPCLK_EN_ACK	BIT(0)
	#define PWMSS_EPWMCLK_EN_ACK	BIT(8)

	#define PWMSS_CLKCONFIG		0x8	/* Clock gating reg */
	#define PWMSS_CLKSTATUS		0xc	/* Clock gating status reg */

	struct pwmss_info {
		void __iomem	*mmio_base;
		struct mutex	pwmss_lock;
		u16		pwmss_clkconfig;
	};
	u16 pwmss_enable_ecap_clk(struct device *dev)
	{
		struct pwmss_info *info = dev_get_drvdata(dev);
		u16 val;

		mutex_lock(&info->pwmss_lock);
		val = readw(info->mmio_base + PWMSS_CLKCONFIG);
		val &= ~((u32)0x3);
		val |= PWMSS_ECAPCLK_EN;
		writew(val , info->mmio_base + PWMSS_CLKCONFIG);
		mutex_unlock(&info->pwmss_lock);

		return readw(info->mmio_base + PWMSS_CLKSTATUS);
	}

	u16 pwmss_disable_ecap_clk(struct device *dev)
	{
		struct pwmss_info *info = dev_get_drvdata(dev);
		u16 val;

		mutex_lock(&info->pwmss_lock);
		val = readw(info->mmio_base + PWMSS_CLKCONFIG);
		val &= ~((u32)0x3);
		val |= PWMSS_ECAPCLK_STOP_REQ;
		writew(val , info->mmio_base + PWMSS_CLKCONFIG);
		mutex_unlock(&info->pwmss_lock);

		return readw(info->mmio_base + PWMSS_CLKSTATUS);
	}
#endif


// DMTimer register offsets
#define ECAP_TSCTR					0x00
#define ECAP_CTRPHS			 		0x04
#define ECAP_CAP1					0x08
#define ECAP_CAP2					0x0C
#define ECAP_CAP3					0x10
#define ECAP_CAP4					0x14
#define ECAP_ECCTL1					0x28
#define ECAP_ECCTL2					0x2A
#define ECAP_ECEINT					0x2C
#define ECAP_ECFLG					0x2E
	#define ECAP_ECFLG_CEVT1			(1<<1)
	#define ECAP_ECFLG_CEVT2			(1<<2)
	#define ECAP_ECFLG_CEVT3			(1<<3)
	#define ECAP_ECFLG_CEVT4			(1<<4)
	#define ECAP_ECFLG_CNTOVF			(1<<5)
#define ECAP_ECCLR					0x30
#define ECAP_ECFRC					0x32
#define ECAP_REVID					0x5C

/*
TODO:
	check ecap clock source

*/

// type declarations

struct ecap_priv
{
	char *name;
	uint32_t idx;
	int irq;
	struct clk *fclk;

	void __iomem *io_base;				// kernel virtual address
	unsigned long regspace_phys_base;	// physical base address
	uint32_t regspace_size;				// memory region size

	struct miscdevice misc;
	struct platform_device *pdev;

	char * icap_channel_name;
	struct icap_channel *icap;
};


// <------------------- CLOCK SOURCE SETTINGS ------------------->
#define CLK_DISABLE		0x00
#define CLK_ENABLE		0x01

static int ecap_set_clk_state(struct ecap_priv *ecap, u32 arg)
{
	int ret = 0;

	if(unlikely(arg > 1) || unlikely(!ecap) || IS_ERR(ecap->fclk))
		return -EINVAL;

	switch(arg)
	{
		case CLK_DISABLE:
			clk_disable(ecap->fclk);
			break;
		case CLK_ENABLE:
			ret = clk_enable(ecap->fclk);
			break;
		default:
			ret = -EINVAL;
			break;
	}

	return ret;
}


// <------------------------  Timer character device operations ------------------------>

static ssize_t ecap_cdev_open(struct inode *inode, struct file *pfile)
{
	try_module_get(THIS_MODULE);
	return 0;
}

static int ecap_cdev_close (struct inode *inode, struct file *pfile)
{
	module_put(THIS_MODULE);
	return 0;
}


static const struct vm_operations_struct ecap_physical_vm_ops = {
#ifdef CONFIG_HAVE_IOREMAP_PROT
	.access = generic_access_phys,
#endif
};

static int ecap_cdev_mmap(struct file *filep, struct vm_area_struct *vma)
{
	int ret;
	struct miscdevice *misc_dev = (struct miscdevice*) (filep->private_data);
	struct ecap_priv *ecap= container_of(misc_dev, struct ecap_priv, misc);

	// the offset is not used
	unsigned long physical = ecap->regspace_phys_base;
	unsigned long vsize = vma->vm_end - vma->vm_start;
	unsigned long psize = ecap->regspace_size;

	if (vsize > psize)
	{
		dev_warn(&ecap->pdev->dev,"[%s:%d] Requested memoryregion is too big. (0x%lx) Size of the remapped memory region: 0x%lx\n",__FUNCTION__,__LINE__,vsize,psize);
	}

	vma->vm_ops = &ecap_physical_vm_ops;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	ret = remap_pfn_range(vma, vma->vm_start, physical >> PAGE_SHIFT, psize, vma->vm_page_prot);
	if(ret)
	{
		dev_warn(&ecap->pdev->dev,"[%s:%d] Cannot remap memory region. ret:%d, phys:0x%lx\n",__FUNCTION__,__LINE__,ret,physical);
		return -EAGAIN;
	}
	return 0;
}

#define TIMER_IOCTL_MAGIC	'-'

#define IOCTL_SET_CLOCK_STATE		_IO(TIMER_IOCTL_MAGIC,1)
// ECAP_SET_CLOCK_SOURCE 	is not implemented
#define IOCTL_SET_ICAP_SOURCE 		_IO(TIMER_IOCTL_MAGIC,3)
#define IOCTL_GET_CLK_FREQ			_IO(TIMER_IOCTL_MAGIC,4)

#define ECAP_IOCTL_MAX				4



static long ecap_cdev_ioctl(struct file *pfile, unsigned int cmd, unsigned long arg)
{
	struct miscdevice *misc_dev = (struct miscdevice*) (pfile->private_data);
	struct ecap_priv *ecap = container_of(misc_dev, struct ecap_priv, misc);
	int ret = 0;

	if (_IOC_TYPE(cmd) != TIMER_IOCTL_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) > ECAP_IOCTL_MAX) return -ENOTTY;

	switch(cmd)
	{
		case IOCTL_SET_CLOCK_STATE:
			ret = ecap_set_clk_state(ecap,arg);
			break;
			
		case IOCTL_SET_ICAP_SOURCE:
			ret = event_mux_set_ecap_event(ecap->idx,arg);
			break;
		case IOCTL_GET_CLK_FREQ:
			ret = clk_get_rate(ecap->fclk);
			break;
		default:
			dev_err(&ecap->pdev->dev,"Invalid IOCTL command : %d.\n",cmd);
			ret = -ENOTTY;
			break;
	}
	return ret;
}


static struct file_operations ecap_cdev_fops = 
{
	.open = ecap_cdev_open,
	.release = ecap_cdev_close,
	.mmap = ecap_cdev_mmap,
	.unlocked_ioctl = ecap_cdev_ioctl
};



static irqreturn_t ecap_irq_handler(int irq, void *data)
{
	struct ecap_priv *ecap= (struct ecap_priv*)data;
	// clear interrupt line and save the input capture flag
	u16 irq_status;


	irq_status = readw(ecap->io_base + ECAP_ECFLG);

	// the CAP1-CAP4 registers are used as fifo,
	// all data are sent to the same channel
	
	if(irq_status & ECAP_ECFLG_CEVT1)
		icap_add_ts(ecap->icap,
					readl(ecap->io_base + ECAP_CAP1),
					irq_status & ECAP_ECFLG_CNTOVF);

	if(irq_status & ECAP_ECFLG_CEVT2)
		icap_add_ts(ecap->icap,
					readl(ecap->io_base + ECAP_CAP2),
					irq_status & ECAP_ECFLG_CNTOVF);
	

	if(irq_status & ECAP_ECFLG_CEVT3)
		icap_add_ts(ecap->icap,
					readl(ecap->io_base + ECAP_CAP3),
					irq_status & ECAP_ECFLG_CNTOVF);
	

	if(irq_status & ECAP_ECFLG_CEVT4)
		icap_add_ts(ecap->icap,
					readl(ecap->io_base + ECAP_CAP4),
					irq_status & ECAP_ECFLG_CNTOVF);

	if(irq_status & ECAP_ECFLG_CNTOVF)
		icap_signal_timer_ovf(ecap->icap);
	
	
	// clear interrupt flag
	writew(irq_status,ecap->io_base + ECAP_ECCLR);


	return IRQ_HANDLED;
}


static int ecap_probe(struct platform_device *pdev)
{
	const char *ecap_name;
	int ecap_idx;
	struct resource *mem, *irq;
	struct ecap_priv *ecap;
	int ret;
	char *irq_name;

	// read up ecap index
	of_property_read_string_index(pdev->dev.of_node, "interrupt-names", 0, &ecap_name);
	sscanf(ecap_name, "ecap%d", &ecap_idx);
	if(ecap_idx < 0 || ecap_idx > 2) 
	{
		pr_err("Invalid ecap index found in the device tree: %d\n",ecap_idx);
		return -EINVAL;
	}

	// alloc private descriptor struct
	ecap = devm_kzalloc(&pdev->dev,sizeof(struct ecap_priv),GFP_KERNEL);
	if(!ecap)
	{
		dev_err(&pdev->dev,"%s:%d: Cannot allocate memory.\n",__func__,__LINE__);
		return -ENOMEM;
	}
	ecap->pdev = pdev;
	ecap->idx = ecap_idx;
	ecap->name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "ecap%d",ecap_idx);

	// ecap is valid, so lets get its resources
	irq = platform_get_resource(pdev,IORESOURCE_IRQ,0);
	if(unlikely( NULL  ==  irq))
	{
		dev_err(&pdev->dev,"%s:%d: Cannot get interrupt.\n",__func__,__LINE__);
		return -ENODEV;
	}
	mem = platform_get_resource(pdev,IORESOURCE_MEM,0);
	if(unlikely(NULL == mem))
	{
		dev_err(&pdev->dev,"%s:%d: Cannot get ecap memory.\n",__func__,__LINE__);
		return -ENODEV;
	}

	// remapping registers
	ecap->io_base =  devm_ioremap_resource(&pdev->dev, mem);
 	if (IS_ERR(ecap->io_base))
 	{
 		dev_err(&pdev->dev,"%s,%d: Cannot remap the ecap memory range.\n",__func__,__LINE__);
 		return PTR_ERR(ecap->io_base);
 	}
 	ecap->regspace_size = resource_size(mem);
 	ecap->regspace_phys_base = mem->start;

 	// get ecap clock
 	ecap->fclk = devm_clk_get(&pdev->dev,"fck");
 	if(IS_ERR(ecap->fclk))
 	{
 		dev_err(&pdev->dev,"%s:%d Cannot get clock for the ecap.\n",__FUNCTION__,__LINE__);
 		return -ENODEV;
 	}

 	// setup misc character device
 	ecap->misc.fops = &ecap_cdev_fops;
	ecap->misc.minor = MISC_DYNAMIC_MINOR;
	ecap->misc.name =ecap->name;
	ecap->misc.mode = S_IRUGO | S_IWUGO;
	if(misc_register(&ecap->misc))
	{
		dev_err(&pdev->dev,"Couldn't initialize miscdevice /dev/%s.\n",ecap->misc.name);
		return -ENODEV;
	}

	ecap->icap_channel_name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s%d_icap","ecap",ecap_idx);
	ecap->icap = icap_create_channel(ecap->icap_channel_name,13);
	if(!ecap->icap)
	{
		dev_err(&pdev->dev,"Cannot initialize icap channel: /dev/%s.\n",ecap->misc.name);
		misc_deregister(&ecap->misc);
		return -ENOMEM;
	}

	 // remapping irq
 	ecap->irq = irq->start;
 	irq_name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s%d_irq",
					  dev_name(&pdev->dev),ecap_idx);
 	ret = devm_request_irq(&pdev->dev,ecap->irq,ecap_irq_handler,0,irq_name,ecap);
 	if(unlikely(ret))
 	{
 		dev_err(&pdev->dev, "%s:%d Interrupt allocation (irq:%d) failed [%d].\n", __func__,__LINE__,ecap->irq,ret);
 		icap_delete_channel(ecap->icap);
 		misc_deregister(&ecap->misc);
		return -ENXIO;
 	}


	dev_info(&pdev->dev,"%s initialization succeeded.\n",ecap->name);
	dev_info(&pdev->dev,"Base address: 0x%lx, length: %u, irq num: %d.\n",ecap->regspace_phys_base,ecap->regspace_size,ecap->irq);

 	platform_set_drvdata(pdev,ecap);


 	// start the ecap interface clock
 	pm_runtime_enable(&pdev->dev);
 	if(pm_runtime_get_sync(&pdev->dev) < 0)
 	{
 		dev_err(&pdev->dev,"[%s:%d] Cannot resume the ecap.\n",__FUNCTION__,__LINE__);
 	}

 	#ifdef OLD_KERNEL
 		ret = pwmss_enable_ecap_clk(pdev->dev.parent);
 		if(!(ret & PWMSS_ECAPCLK_EN_ACK))
 			dev_err(&pdev->dev,"Cannot enable eCAP clock.\n");
 	#endif


 	return 0;

}

static int ecap_remove(struct platform_device *pdev)
{
	struct ecap_priv *ecap;
	ecap = platform_get_drvdata(pdev);

	#ifdef OLD_KERNEL
		pwmss_disable_ecap_clk(pdev->dev.parent);
	#endif

	// stop the ecap interface clock
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	devm_free_irq(&pdev->dev, ecap->irq, ecap);
	
	icap_delete_channel(ecap->icap);
	misc_deregister(&ecap->misc);

	// all other resourcees are freed by the manager layer
	return 0;
}				





static struct of_device_id ecap_of_match[] = {
	{ .compatible	= "ti,am3352-ecap" },
	{ .compatible	= "ti,am33xx-ecap" },
	{}
};

MODULE_DEVICE_TABLE(of, ecap_of_match);

static struct platform_driver ecap_platform_driver = {
	.probe = ecap_probe,
	.remove = ecap_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "RTIO_ECAP",
		.of_match_table = ecap_of_match,
	},
};


module_platform_driver(ecap_platform_driver);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tusori Tibor");
MODULE_DESCRIPTION("User space driver support for BeagleBone ecaps.");