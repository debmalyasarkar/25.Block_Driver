#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>

#define FIRST_MINOR 0
#define MINOR_COUNT 1

#define SECTOR_SIZE     512
#define NUM_OF_SECTOR   1024

static int major_num = 0;
module_param(major_num, int, 0);

/* Private Structure */
static struct ramdisk_t
{
  /* Request Queue */
  struct request_queue *queue;
  /* Kernel Representation of Disk Device */
  /* Main Purpose is helping to keep track of Disk Partitions */
  struct gendisk *gendisk;
  /* Size of the Device */
  unsigned int size;
  /* Lock for Request Queue - Used while handling a Request */
  spinlock_t lock;
  /* Array to Store Disk Data */
  uint8_t *data;
}ramdisk; 

/* Low level write to ramdisk from userspace */
void ramdevice_write(sector_t sector_off, u8 *buffer, unsigned int sectors)
{
  memcpy(ramdisk.data + sector_off * SECTOR_SIZE, buffer, sectors * SECTOR_SIZE);
}

/* Low level read from ramdisk to userspace */
void ramdevice_read(sector_t sector_off, u8 *buffer, unsigned int sectors)
{
  memcpy(buffer, ramdisk.data + sector_off * SECTOR_SIZE, sectors * SECTOR_SIZE);
}

static int do_actual_request(struct request *req)
{
  sector_t start_sector, sector_offset; 
  uint32_t sector_cnt, sectors;
  int dir, ret = 0;
  uint8_t *buffer;
  struct bio_vec bv;
  struct req_iterator iter;
 
  pr_info("Processing Request : \r\n");
  
  /* Get direction of data transfer */
  dir          = rq_data_dir(req);
  /* Get the first sector of the request */
  start_sector = blk_rq_pos(req);
  /* Get the total number of sectors for this request */
  sector_cnt   = blk_rq_sectors(req);

  pr_info("Start Sector = %4llu, No of Sectors = %4u\r\n", start_sector, sector_cnt);
  /* We receive the starting sector and the no of sectors associated with the request.
     The number of sectors are in a multiple of 8 */
  
  sector_offset = 0;

  /* Get each bio_vect directly from the request */
  /* Alternatively we can use nested loops as follows:
     Outer Loop - Get each bio from the request
                  __rq_for_each_bio(bio, req)
     Inner Loop - Get each bio_vect from each bio 
                  bio_for_each_segment(bv, bio, ii)
  */
  rq_for_each_segment(bv, req, iter)
  {
    /* Find corresponding base address for Read/Wrrite */
    buffer = page_address(bv.bv_page) + bv.bv_offset;
    if(bv.bv_len % SECTOR_SIZE != 0)
    {
      pr_err("Size of bio (%d) is not a multiple of SECTOR_SIZE (%d)\r\n",bv.bv_len,SECTOR_SIZE);
      pr_err("Should never occur as this may lead to data truncation\r\n");
      ret = -EIO;
    }
    /* Length in terms of number of sectors */
    sectors = bv.bv_len / SECTOR_SIZE;
    pr_info("Start Sector = %4llu, Sector Offset = %4llu; Buffer = %p; Length = %u sectors\r\n",
               start_sector, sector_offset, buffer, sectors);
    if(dir == WRITE)
      ramdevice_write(start_sector + sector_offset, buffer, sectors);
    else
      ramdevice_read(start_sector + sector_offset, buffer, sectors);
    sector_offset += sectors;
  }

  if(sector_offset != sector_cnt)
  {
    pr_err("Bio info doesn't match with the request info\r\n");
    ret = -EIO;
  }
  return ret;
}

static void ramdisk_request_callback(struct request_queue *q)
{
  int ret;
  struct request *req = NULL;

  /* Take requests one by one from the request queue and process it */
  /* blk_fetch_request returns the req at the top of the queue */
  while(NULL != (req = blk_fetch_request(q)))
  {
    /* If not a normal filesystem request then end request and exit */
    /* Other type of req can be packet-mode or device specific diagnostics */
    if(req->cmd_type != REQ_TYPE_FS)
    {
      __blk_end_request(req, 0, 0);
      continue;
    }
    /* Process the request */
    ret = do_actual_request(req);
    __blk_end_request_all(req, ret);
  }
};

static int ramdisk_open(struct block_device *bd, fmode_t mode)
{
  pr_info("%s  : Open\r\n",__func__);
  return 0;
}

static void ramdisk_close(struct gendisk *gd, fmode_t mode)
{
  pr_info("%s : Close\r\n",__func__);
}

static struct block_device_operations ramdisk_fops = {
  .owner   = THIS_MODULE,
  .open    = ramdisk_open,
  .release = ramdisk_close,
};

static int __init ramdisk_init(void)
{
  pr_info("%s  : Init\r\n",__func__);

/* Step 1 : Set Up Device */
  /* Allocate Memory for Disk Device */
  ramdisk.data = vmalloc(SECTOR_SIZE * NUM_OF_SECTOR);
  if(NULL == ramdisk.data)
  {
    pr_err("Allocation of Memory Failed for RAMDisk\r\n");
    return -ENOMEM;
  }
  ramdisk.size = NUM_OF_SECTOR;
  memset(ramdisk.data, 0, SECTOR_SIZE * NUM_OF_SECTOR);
 
  spin_lock_init(&ramdisk.lock);
/* Step 2 : Register Driver With Block Layer */

  /* blk_init_queue  - Prepare a request queue for use with a block device
  *  @rfn:  The function to be called to process requests that have been
  *         placed on the queue.[Callback]
  *  @lock: Request queue spin lock which is held by driver to access the 
  *         request queue while processing a request
  */
  ramdisk.queue = blk_init_queue(ramdisk_request_callback, &ramdisk.lock);
  if(NULL == ramdisk.queue)
  {
    pr_err("Creation of Request Queue Failed\r\n");
    vfree(ramdisk.data);
    return -ENOMEM;
  }

/* Step 3 : Register Driver with VFS and a Valid Major Number */
  /* If Major Number is 0, Allocates Dynamically */
  major_num = register_blkdev(major_num, "sbd0");
  if(0 >= major_num)
  {
    pr_err("Acquiring Major Number Failed\r\n");
    vfree(ramdisk.data);
    return -ENOMEM;
  } 
  /* Allocate gendisk structure */
  /* Argument is Number of Minor Numbers which Indicate Number of Partitions 
     Supported by this Disk Device */
  ramdisk.gendisk = alloc_disk(MINOR_COUNT);
  if(NULL == ramdisk.gendisk)
  {
    pr_err("Allocation of gendisk Failed\r\n");
    unregister_blkdev(major_num, "sbd0");
    vfree(ramdisk.data);
    return -ENOMEM;
  }
  /* Populate the gendisk structure */
  ramdisk.gendisk->major        = major_num;
  ramdisk.gendisk->first_minor  = FIRST_MINOR;
  ramdisk.gendisk->fops         = &ramdisk_fops;
  ramdisk.gendisk->private_data = &ramdisk;
  strcpy(ramdisk.gendisk->disk_name, "sbd0");
  set_capacity(ramdisk.gendisk, ramdisk.size);
  ramdisk.gendisk->queue        = ramdisk.queue;
  /* Registers partitioning information of device gendisk with kernel */
  add_disk(ramdisk.gendisk);
  return 0;
}

static void __exit ramdisk_exit(void)
{
  pr_info("%s  : Exit\r\n",__func__);
  /* Unregister partitioning information of gendisk with kernel */
  del_gendisk(ramdisk.gendisk);
  /* Decrement the refcount, and if 0, call kobject_cleanup() */
  /* Ref Count was incremented when we invoked alloc_disk() */ 
  put_disk(ramdisk.gendisk);
  /* Unregister from VFS */
  unregister_blkdev(major_num, "sbd0");
  /* Cancel all pending requests and shutdown the request queue */
  blk_cleanup_queue(ramdisk.queue);
  /* Free memory allocated for the disk device */
  vfree(ramdisk.data);
}

module_init(ramdisk_init);
module_exit(ramdisk_exit);

MODULE_AUTHOR("debmalyasarkar1@gmail.com");
MODULE_DESCRIPTION("RAMDisk Block Driver");
MODULE_LICENSE("GPL");

