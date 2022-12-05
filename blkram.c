#include "asm/page.h"
#include "linux/blk_types.h"
#include "linux/sysfb.h"
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/idr.h>

unsigned long capacity_mb = 40;
module_param(capacity_mb, ulong, 0644);
MODULE_PARM_DESC(capacity_mb, "capacity of the block device in MB");
EXPORT_SYMBOL_GPL(capacity_mb);

unsigned long max_segments = 32;
module_param(max_segments, ulong, 0644);
MODULE_PARM_DESC(max_segments, "maximum segments");
EXPORT_SYMBOL_GPL(max_segments);

unsigned long max_segment_size = 65536;
module_param(max_segment_size, ulong, 0644);
MODULE_PARM_DESC(max_segment_size, "maximum segment size");
EXPORT_SYMBOL_GPL(max_segment_size);

unsigned long lbs = PAGE_SIZE;
module_param(lbs, ulong, 0644);
MODULE_PARM_DESC(lbs, "Logical block size");
EXPORT_SYMBOL_GPL(lbs);

unsigned long pbs = PAGE_SIZE;
module_param(pbs, ulong, 0644);
MODULE_PARM_DESC(pbs, "Physical block size");
EXPORT_SYMBOL_GPL(pbs);


struct blk_ram_dev_t {
	sector_t capacity;
	u8 *data;
	struct blk_mq_tag_set tag_set;
	struct gendisk *disk;
};

static int major;
static DEFINE_IDA(blk_ram_indexes);
static struct blk_ram_dev_t *blk_ram_dev = NULL;

static blk_status_t blk_ram_queue_rq(struct blk_mq_hw_ctx *hctx,
				     const struct blk_mq_queue_data *bd)
{
	struct request *rq = bd->rq;
	blk_status_t err = BLK_STS_OK;
	struct bio_vec bv;
	struct req_iterator iter;
	loff_t pos = blk_rq_pos(rq) << SECTOR_SHIFT;
	struct blk_ram_dev_t *blkram = hctx->queue->queuedata;
	loff_t data_len = (blkram->capacity << SECTOR_SHIFT);

	blk_mq_start_request(rq);

	rq_for_each_segment(bv, rq, iter) {
		unsigned int len = bv.bv_len;
		void *buf = page_address(bv.bv_page) + bv.bv_offset;

		if (pos + len > data_len) {
			err = BLK_STS_IOERR;
			break;
		}

		switch (req_op(rq)) {
		case REQ_OP_READ:
			memcpy(buf, blkram->data + pos, len);
			break;
		case REQ_OP_WRITE:
			memcpy(blkram->data + pos, buf, len);
			break;
		default:
			err = BLK_STS_IOERR;
			goto end_request;
		}
		pos += len;
	}

end_request:
	blk_mq_end_request(rq, err);
	return BLK_STS_OK;
}

static const struct blk_mq_ops blk_ram_mq_ops = {
	.queue_rq = blk_ram_queue_rq,
};

static const struct block_device_operations blk_ram_rq_ops = {
	.owner = THIS_MODULE,
};

static int __init blk_ram_init(void)
{
	int ret = 0;
	int minor;
	struct gendisk *disk;
	loff_t data_size_bytes = capacity_mb << 20;

	ret = register_blkdev(0, "blkram");
	if (ret < 0)
		return ret;

	major = ret;
	blk_ram_dev = kzalloc(sizeof(struct blk_ram_dev_t), GFP_KERNEL);

	if (blk_ram_dev == NULL) {
		pr_err("memory allocation failed for blk_ram_dev\n");
		ret = -ENOMEM;
		goto unregister_blkdev;
	}

	blk_ram_dev->capacity = data_size_bytes >> SECTOR_SHIFT;
	blk_ram_dev->data = kvmalloc(data_size_bytes, GFP_KERNEL);
	if (blk_ram_dev->data == NULL) {
		pr_err("memory allocation failed for the RAM disk\n");
		ret = -ENOMEM;
		goto data_err;
	}

	memset(&blk_ram_dev->tag_set, 0, sizeof(blk_ram_dev->tag_set));
	blk_ram_dev->tag_set.ops = &blk_ram_mq_ops;
	blk_ram_dev->tag_set.queue_depth = 128;
	blk_ram_dev->tag_set.numa_node = NUMA_NO_NODE;
	blk_ram_dev->tag_set.flags = BLK_MQ_F_SHOULD_MERGE;
	blk_ram_dev->tag_set.cmd_size = 0;
	blk_ram_dev->tag_set.driver_data = blk_ram_dev;
	blk_ram_dev->tag_set.nr_hw_queues = 1;

	ret = blk_mq_alloc_tag_set(&blk_ram_dev->tag_set);
	if (ret)
		goto data_err;

	disk = blk_ram_dev->disk =
		blk_mq_alloc_disk(&blk_ram_dev->tag_set, blk_ram_dev);

	blk_queue_logical_block_size(disk->queue, lbs);
	blk_queue_physical_block_size(disk->queue, pbs);
	blk_queue_max_segments(disk->queue, max_segments);
	blk_queue_max_segment_size(disk->queue, max_segment_size);

	if (IS_ERR(disk)) {
		ret = PTR_ERR(disk);
		pr_err("Error allocating a disk\n");
		goto tagset_err;
	}

	// This is not necessary as we don't support partitions, and creating
	// more RAM backed devices with the existing module
	minor = ret = ida_alloc(&blk_ram_indexes, GFP_KERNEL);
	if (ret < 0)
		goto cleanup_disk;

	disk->major = major;
	disk->first_minor = minor;
	disk->minors = 1;
	snprintf(disk->disk_name, DISK_NAME_LEN, "blkram");
	disk->fops = &blk_ram_rq_ops;
	disk->flags = GENHD_FL_NO_PART;
	set_capacity(disk, blk_ram_dev->capacity);

	ret = add_disk(disk);
	if (ret < 0)
		goto cleanup_disk;

	pr_info("module loaded\n");
	return 0;

cleanup_disk:
	put_disk(blk_ram_dev->disk);
tagset_err:
	kfree(blk_ram_dev->data);
data_err:
	kfree(blk_ram_dev);
unregister_blkdev:
	unregister_blkdev(major, "blkram");

	return ret;
}

static void __exit blk_ram_exit(void)
{
	if (blk_ram_dev->disk) {
		del_gendisk(blk_ram_dev->disk);
		put_disk(blk_ram_dev->disk);
	}
	unregister_blkdev(major, "blkram");
	kfree(blk_ram_dev);

	pr_info("module unloaded\n");
}

module_init(blk_ram_init);
module_exit(blk_ram_exit);

MODULE_AUTHOR("Pankaj");
MODULE_LICENSE("GPL");

