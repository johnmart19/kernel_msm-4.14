/*
 * Copyright (c) 2018 The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include "debug.h"
#include "hif.h"
#include "htc.h"
#include "ce.h"
#include "snoc.h"
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#define  WCN3990_CE_ATTR_FLAGS 0
#define ATH10K_SNOC_RX_POST_RETRY_MS 50

static char *const ce_name[] = {
	"WLAN_CE_0",
	"WLAN_CE_1",
	"WLAN_CE_2",
	"WLAN_CE_3",
	"WLAN_CE_4",
	"WLAN_CE_5",
	"WLAN_CE_6",
	"WLAN_CE_7",
	"WLAN_CE_8",
	"WLAN_CE_9",
	"WLAN_CE_10",
	"WLAN_CE_11",
};

static const struct ath10k_snoc_drv_priv drv_priv = {
	.hw_rev = ATH10K_HW_WCN3990,
	.dma_mask = DMA_BIT_MASK(37),
};

static struct ce_attr host_ce_config_wlan[] = {
	/* CE0: host->target HTC control streams */
	{
		.flags = WCN3990_CE_ATTR_FLAGS,
		.src_nentries = 16,
		.src_sz_max = 2048,
		.dest_nentries = 0,
		.send_cb = NULL,
	},

	/* CE1: target->host HTT + HTC control */
	{
		.flags = WCN3990_CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 2048,
		.dest_nentries = 512,
		.recv_cb = NULL,
	},

	/* CE2: target->host WMI */
	{
		.flags = WCN3990_CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 2048,
		.dest_nentries = 64,
		.recv_cb = NULL,
	},

	/* CE3: host->target WMI */
	{
		.flags = WCN3990_CE_ATTR_FLAGS,
		.src_nentries = 32,
		.src_sz_max = 2048,
		.dest_nentries = 0,
		.send_cb = NULL,
	},

	/* CE4: host->target HTT */
	{
		.flags = WCN3990_CE_ATTR_FLAGS | CE_ATTR_DIS_INTR,
		.src_nentries = 256,
		.src_sz_max = 256,
		.dest_nentries = 0,
		.send_cb = NULL,
	},

	/* CE5: target->host HTT (ipa_uc->target ) */
	{
		.flags = WCN3990_CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 512,
		.dest_nentries = 512,
		.recv_cb = NULL,
	},

	/* CE6: target autonomous hif_memcpy */
	{
		.flags = WCN3990_CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 0,
		.dest_nentries = 0,
	},

	/* CE7: ce_diag, the Diagnostic Window */
	{
		.flags = WCN3990_CE_ATTR_FLAGS,
		.src_nentries = 2,
		.src_sz_max = 2048,
		.dest_nentries = 2,
	},

	/* CE8: Target to uMC */
	{
		.flags = WCN3990_CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 2048,
		.dest_nentries = 128,
	},

	/* CE9 target->host HTT */
	{
		.flags = WCN3990_CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 2048,
		.dest_nentries = 512,
		.recv_cb = NULL,
	},

	/* CE10: target->host HTT */
	{
		.flags = WCN3990_CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 2048,
		.dest_nentries = 512,
		.recv_cb = NULL,
	},

	/* CE11: target -> host PKTLOG */
	{
		.flags = WCN3990_CE_ATTR_FLAGS,
		.src_nentries = 0,
		.src_sz_max = 2048,
		.dest_nentries = 512,
		.recv_cb = NULL,
	},
};

void ath10k_snoc_write32(struct ath10k *ar, u32 offset, u32 value)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);

	iowrite32(value, ar_snoc->mem + offset);
}

u32 ath10k_snoc_read32(struct ath10k *ar, u32 offset)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	u32 val;

	val = ioread32(ar_snoc->mem + offset);

	return val;
}

static int __ath10k_snoc_rx_post_buf(struct ath10k_snoc_pipe *pipe)
{
	struct ath10k_ce_pipe *ce_pipe = pipe->ce_hdl;
	struct ath10k *ar = pipe->hif_ce_state;
	struct ath10k_ce *ce = ath10k_ce_priv(ar);
	struct sk_buff *skb;
	dma_addr_t paddr;
	int ret;

	skb = dev_alloc_skb(pipe->buf_sz);
	if (!skb)
		return -ENOMEM;

	WARN_ONCE((unsigned long)skb->data & 3, "unaligned skb");

	paddr = dma_map_single(ar->dev, skb->data,
			       skb->len + skb_tailroom(skb),
			       DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(ar->dev, paddr))) {
		ath10k_warn(ar, "failed to dma map snoc rx buf\n");
		dev_kfree_skb_any(skb);
		return -EIO;
	}

	ATH10K_SKB_RXCB(skb)->paddr = paddr;

	spin_lock_bh(&ce->ce_lock);
	ret = ce_pipe->ops->ce_rx_post_buf(ce_pipe, skb, paddr);
	spin_unlock_bh(&ce->ce_lock);
	if (ret) {
		dma_unmap_single(ar->dev, paddr, skb->len + skb_tailroom(skb),
				 DMA_FROM_DEVICE);
		dev_kfree_skb_any(skb);
		return ret;
	}

	return 0;
}

static void ath10k_snoc_rx_post_pipe(struct ath10k_snoc_pipe *pipe)
{
	struct ath10k *ar = pipe->hif_ce_state;
	struct ath10k_ce *ce = ath10k_ce_priv(ar);
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	struct ath10k_ce_pipe *ce_pipe = pipe->ce_hdl;
	int ret, num;

	if (pipe->buf_sz == 0)
		return;

	if (!ce_pipe->dest_ring)
		return;

	spin_lock_bh(&ce->ce_lock);
	num = __ath10k_ce_rx_num_free_bufs(ce_pipe);
	spin_unlock_bh(&ce->ce_lock);
	while (num--) {
		ret = __ath10k_snoc_rx_post_buf(pipe);
		if (ret) {
			if (ret == -ENOSPC)
				break;
			ath10k_warn(ar, "failed to post rx buf: %d\n", ret);
			mod_timer(&ar_snoc->rx_post_retry, jiffies +
				  ATH10K_SNOC_RX_POST_RETRY_MS);
			break;
		}
	}
}

static void ath10k_snoc_rx_post(struct ath10k *ar)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	int i;

	for (i = 0; i < CE_COUNT; i++)
		ath10k_snoc_rx_post_pipe(&ar_snoc->pipe_info[i]);
}

static inline void ath10k_snoc_irq_disable(struct ath10k *ar)
{
	ath10k_ce_disable_interrupts(ar);
}

static inline void ath10k_snoc_irq_enable(struct ath10k *ar)
{
	ath10k_ce_enable_interrupts(ar);
}

static void ath10k_snoc_rx_pipe_cleanup(struct ath10k_snoc_pipe *snoc_pipe)
{
	struct ath10k_ce_pipe *ce_pipe;
	struct ath10k_ce_ring *ce_ring;
	struct sk_buff *skb;
	struct ath10k *ar;
	int i;

	ar = snoc_pipe->hif_ce_state;
	ce_pipe = snoc_pipe->ce_hdl;
	ce_ring = ce_pipe->dest_ring;

	if (!ce_ring)
		return;

	if (!snoc_pipe->buf_sz)
		return;

	for (i = 0; i < ce_ring->nentries; i++) {
		skb = ce_ring->per_transfer_context[i];
		if (!skb)
			continue;

		ce_ring->per_transfer_context[i] = NULL;

		dma_unmap_single(ar->dev, ATH10K_SKB_RXCB(skb)->paddr,
				 skb->len + skb_tailroom(skb),
				 DMA_FROM_DEVICE);
		dev_kfree_skb_any(skb);
	}
}

static void ath10k_snoc_tx_pipe_cleanup(struct ath10k_snoc_pipe *snoc_pipe)
{
	struct ath10k_ce_pipe *ce_pipe;
	struct ath10k_ce_ring *ce_ring;
	struct ath10k_snoc *ar_snoc;
	struct sk_buff *skb;
	struct ath10k *ar;
	int i;

	ar = snoc_pipe->hif_ce_state;
	ar_snoc = ath10k_snoc_priv(ar);
	ce_pipe = snoc_pipe->ce_hdl;
	ce_ring = ce_pipe->src_ring;

	if (!ce_ring)
		return;

	if (!snoc_pipe->buf_sz)
		return;

	for (i = 0; i < ce_ring->nentries; i++) {
		skb = ce_ring->per_transfer_context[i];
		if (!skb)
			continue;

		ce_ring->per_transfer_context[i] = NULL;

		ath10k_htc_tx_completion_handler(ar, skb);
	}
}

static void ath10k_snoc_buffer_cleanup(struct ath10k *ar)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	struct ath10k_snoc_pipe *pipe_info;
	int pipe_num;

	del_timer_sync(&ar_snoc->rx_post_retry);
	for (pipe_num = 0; pipe_num < CE_COUNT; pipe_num++) {
		pipe_info = &ar_snoc->pipe_info[pipe_num];
		ath10k_snoc_rx_pipe_cleanup(pipe_info);
		ath10k_snoc_tx_pipe_cleanup(pipe_info);
	}
}

static void ath10k_snoc_hif_stop(struct ath10k *ar)
{
	ath10k_snoc_irq_disable(ar);
	ath10k_snoc_buffer_cleanup(ar);
	ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot hif stop\n");
}

static int ath10k_snoc_hif_start(struct ath10k *ar)
{
	ath10k_snoc_irq_enable(ar);
	ath10k_snoc_rx_post(ar);

	ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot hif start\n");

	return 0;
}

static const struct ath10k_hif_ops ath10k_snoc_hif_ops = {
	.read32		= ath10k_snoc_read32,
	.write32	= ath10k_snoc_write32,
	.start		= ath10k_snoc_hif_start,
	.stop		= ath10k_snoc_hif_stop,
};

static const struct ath10k_bus_ops ath10k_snoc_bus_ops = {
	.read32		= ath10k_snoc_read32,
	.write32	= ath10k_snoc_write32,
};

static irqreturn_t ath10k_snoc_per_engine_handler(int irq, void *arg)
{
	return IRQ_HANDLED;
}

static int ath10k_snoc_request_irq(struct ath10k *ar)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	int irqflags = IRQF_TRIGGER_RISING;
	int ret, id;

	for (id = 0; id < CE_COUNT_MAX; id++) {
		ret = request_irq(ar_snoc->ce_irqs[id].irq_line,
				  ath10k_snoc_per_engine_handler,
				  irqflags, ce_name[id], ar);
		if (ret) {
			ath10k_err(ar,
				   "failed to register IRQ handler for CE %d: %d",
				   id, ret);
			goto err_irq;
		}
	}

	return 0;

err_irq:
	for (id -= 1; id >= 0; id--)
		free_irq(ar_snoc->ce_irqs[id].irq_line, ar);

	return ret;
}

static void ath10k_snoc_free_irq(struct ath10k *ar)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	int id;

	for (id = 0; id < CE_COUNT_MAX; id++)
		free_irq(ar_snoc->ce_irqs[id].irq_line, ar);
}

static int ath10k_snoc_resource_init(struct ath10k *ar)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	struct platform_device *pdev;
	struct resource *res;
	int i, ret = 0;

	pdev = ar_snoc->dev;
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "membase");
	if (!res) {
		ath10k_err(ar, "Memory base not found in DT\n");
		return -EINVAL;
	}

	ar_snoc->mem_pa = res->start;
	ar_snoc->mem = devm_ioremap(&pdev->dev, ar_snoc->mem_pa,
				    resource_size(res));
	if (!ar_snoc->mem) {
		ath10k_err(ar, "Memory base ioremap failed with physical address %pa\n",
			   &ar_snoc->mem_pa);
		return -EINVAL;
	}

	for (i = 0; i < CE_COUNT; i++) {
		res = platform_get_resource(ar_snoc->dev, IORESOURCE_IRQ, i);
		if (!res) {
			ath10k_err(ar, "failed to get IRQ%d\n", i);
			ret = -ENODEV;
			goto out;
		}
		ar_snoc->ce_irqs[i].irq_line = res->start;
	}

out:
	return ret;
}

static int ath10k_snoc_setup_resource(struct ath10k *ar)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	struct ath10k_ce *ce = ath10k_ce_priv(ar);
	struct ath10k_snoc_pipe *pipe;
	int i, ret;

	spin_lock_init(&ce->ce_lock);
	for (i = 0; i < CE_COUNT; i++) {
		pipe = &ar_snoc->pipe_info[i];
		pipe->ce_hdl = &ce->ce_states[i];
		pipe->pipe_num = i;
		pipe->hif_ce_state = ar;

		ret = ath10k_ce_alloc_pipe(ar, i, &host_ce_config_wlan[i]);
		if (ret) {
			ath10k_err(ar, "failed to allocate copy engine pipe %d: %d\n",
				   i, ret);
			return ret;
		}

		pipe->buf_sz = host_ce_config_wlan[i].src_sz_max;
	}

	return 0;
}

static void ath10k_snoc_release_resource(struct ath10k *ar)
{
	int i;

	for (i = 0; i < CE_COUNT; i++)
		ath10k_ce_free_pipe(ar, i);
}

static const struct of_device_id ath10k_snoc_dt_match[] = {
	{ .compatible = "qcom,wcn3990-wifi",
	 .data = &drv_priv,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, ath10k_snoc_dt_match);

static int ath10k_snoc_probe(struct platform_device *pdev)
{
	const struct ath10k_snoc_drv_priv *drv_data;
	const struct of_device_id *of_id;
	struct ath10k_snoc *ar_snoc;
	struct device *dev;
	struct ath10k *ar;
	int ret;

	of_id = of_match_device(ath10k_snoc_dt_match, &pdev->dev);
	if (!of_id) {
		dev_err(&pdev->dev, "failed to find matching device tree id\n");
		return -EINVAL;
	}

	drv_data = of_id->data;
	dev = &pdev->dev;

	ret = dma_set_mask_and_coherent(dev, drv_data->dma_mask);
	if (ret) {
		dev_err(dev, "failed to set dma mask: %d", ret);
		return ret;
	}

	ar = ath10k_core_create(sizeof(*ar_snoc), dev, ATH10K_BUS_SNOC,
				drv_data->hw_rev, &ath10k_snoc_hif_ops);
	if (!ar) {
		dev_err(dev, "failed to allocate core\n");
		return -ENOMEM;
	}

	ar_snoc = ath10k_snoc_priv(ar);
	ar_snoc->dev = pdev;
	platform_set_drvdata(pdev, ar);
	ar_snoc->ar = ar;
	ar_snoc->ce.bus_ops = &ath10k_snoc_bus_ops;
	ar->ce_priv = &ar_snoc->ce;

	ath10k_snoc_resource_init(ar);
	if (ret) {
		ath10k_warn(ar, "failed to initialize resource: %d\n", ret);
		goto err_core_destroy;
	}

	ath10k_snoc_setup_resource(ar);
	if (ret) {
		ath10k_warn(ar, "failed to setup resource: %d\n", ret);
		goto err_core_destroy;
	}
	ret = ath10k_snoc_request_irq(ar);
	if (ret) {
		ath10k_warn(ar, "failed to request irqs: %d\n", ret);
		goto err_release_resource;
	}
	ret = ath10k_core_register(ar, drv_data->hw_rev);
	if (ret) {
		ath10k_err(ar, "failed to register driver core: %d\n", ret);
		goto err_free_irq;
	}
	ath10k_dbg(ar, ATH10K_DBG_SNOC, "snoc probe\n");
	ath10k_warn(ar, "Warning: SNOC support is still work-in-progress, it will not work properly!");

	return 0;

err_free_irq:
	ath10k_snoc_free_irq(ar);

err_release_resource:
	ath10k_snoc_release_resource(ar);

err_core_destroy:
	ath10k_core_destroy(ar);

	return ret;
}

static int ath10k_snoc_remove(struct platform_device *pdev)
{
	struct ath10k *ar = platform_get_drvdata(pdev);

	ath10k_dbg(ar, ATH10K_DBG_SNOC, "snoc remove\n");
	ath10k_core_unregister(ar);
	ath10k_snoc_free_irq(ar);
	ath10k_snoc_release_resource(ar);
	ath10k_core_destroy(ar);

	return 0;
}

static struct platform_driver ath10k_snoc_driver = {
		.probe  = ath10k_snoc_probe,
		.remove = ath10k_snoc_remove,
		.driver = {
			.name   = "ath10k_snoc",
			.owner = THIS_MODULE,
			.of_match_table = ath10k_snoc_dt_match,
		},
};

static int __init ath10k_snoc_init(void)
{
	int ret;

	ret = platform_driver_register(&ath10k_snoc_driver);
	if (ret)
		pr_err("failed to register ath10k snoc driver: %d\n",
		       ret);

	return ret;
}
module_init(ath10k_snoc_init);

static void __exit ath10k_snoc_exit(void)
{
	platform_driver_unregister(&ath10k_snoc_driver);
}
module_exit(ath10k_snoc_exit);

MODULE_AUTHOR("Qualcomm");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Driver support for Atheros WCN3990 SNOC devices");