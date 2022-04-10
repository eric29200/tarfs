#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/vfs.h>
#include <linux/buffer_head.h>
#include <linux/writeback.h>

#include "tarfs.h"

/* TarFS inode cache */
static struct kmem_cache *tarfs_inode_cache;

/*
 * Get TarFS file system status.
 */
static int tarfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
  struct super_block *sb = dentry->d_sb;
  struct tarfs_sb_info *sbi = tarfs_sb(sb);
  
  buf->f_type = sb->s_magic;
  buf->f_bsize = sb->s_blocksize;
  buf->f_blocks = 0;
  buf->f_bfree = 0;
  buf->f_bavail = 0;
  buf->f_files = sbi->s_ninodes - 1;
  buf->f_ffree = 0;
  buf->f_namelen = 0;
  buf->f_fsid = u64_to_fsid(huge_encode_dev(sb->s_bdev->bd_dev));
  
  return 0;
}

/*
 * Release a TarFS super block.
 */
static void tarfs_put_super(struct super_block *sb)
{
  struct tarfs_sb_info *sbi = tarfs_sb(sb);
  
  /* free tar entries */
  tar_free(sbi->s_root_entry);
  if (sbi->s_tar_entries)
    kfree(sbi->s_tar_entries);
  
  sb->s_fs_info = NULL;
  kfree(sbi);
}

/*
 * Allocate a new TarFS inode.
 */
static struct inode *tarfs_alloc_inode(struct super_block *sb)
{
  struct tarfs_inode_info *tarfs_inode;
  
  tarfs_inode = kmem_cache_alloc(tarfs_inode_cache, GFP_KERNEL);
  if (!tarfs_inode)
    return NULL;
  
  return &tarfs_inode->vfs_inode;
}

/*
 * Free a TarFS inode.
 */
static void tarfs_free_inode(struct inode *inode)
{
  kmem_cache_free(tarfs_inode_cache, tarfs_i(inode));
}

/*
 * Init a new inode from cache.
 */
static void init_once(void *foo)
{
  struct tarfs_inode_info *tarfs_inode = (struct tarfs_inode_info *) foo;
  inode_init_once(&tarfs_inode->vfs_inode);
}

/*
 * Create TarFS inode cache.
 */
static int __init init_inodecache(void)
{
  tarfs_inode_cache = kmem_cache_create("tarfs_inode_cache", sizeof(struct tarfs_inode_info), 0,
                                        SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD | SLAB_ACCOUNT,
                                        init_once);
  if (!tarfs_inode_cache)
    return -ENOMEM;
  
  return 0;
}

/*
 * Destroy TarFS inode cache.
 */
static void destroy_inodecache(void)
{
  rcu_barrier();
  kmem_cache_destroy(tarfs_inode_cache);
}

/*
 * TarFS super operations.
 */
static struct super_operations tarfs_sops = {
  .alloc_inode          = tarfs_alloc_inode,
  .free_inode           = tarfs_free_inode,
  .put_super            = tarfs_put_super,
  .statfs               = tarfs_statfs,
};

/*
 * Fill in a TarFS super block.
 */
static int tarfs_fill_super(struct super_block *sb, void *data, int silent)
{
  struct tarfs_sb_info *sbi;
  struct inode *root_inode;
  int err, i;
  
  /* allocate TarFS super block */
  sb->s_fs_info = sbi = (struct tarfs_sb_info *) kmalloc(sizeof(struct tarfs_sb_info), GFP_KERNEL);
  if (!sbi)
    return -ENOMEM;
  
  /* set super block */
  sb_set_blocksize(sb, TARFS_BLOCK_SIZE);
  sbi->s_ninodes = 0;
  sbi->s_root_entry = NULL;
  sbi->s_tar_entries = NULL;
  
  /* parse tar archive */
  err = tar_create(sb);
  if (err)
    goto err_bad_sb;
  
  /* create tar entries index */
  sbi->s_tar_entries = (struct tar_entry **) kmalloc(sizeof(struct tar_entry *) * sbi->s_ninodes, GFP_KERNEL);
  if (!sbi->s_tar_entries) {
    err = -ENOMEM;
    goto err_index;
  }
  
  /* index tar entries */
  for (i = 0; i < sbi->s_ninodes; i++)
    sbi->s_tar_entries[i] = NULL;
  tar_index(sb, sbi->s_root_entry);
  
  /* set super operations */
  sb->s_op = &tarfs_sops;
  
  /* get root inode */
  root_inode = tarfs_iget(sb, TARFS_ROOT_INO);
  if (IS_ERR(root_inode)) {
    err = PTR_ERR(root_inode);
    goto err_no_root;
  }
  
  /* make root inode */
  sb->s_root = d_make_root(root_inode);
  if (!sb->s_root) {
    err = -ENOMEM;
    goto err_no_root;
  }
  
  return 0;
err_no_root:
  printk("TARFS : can't get root inode\n");
  goto err_release_tar;
err_index:
  printk("TARFS : can't create tar index\n");
err_release_tar:
  tar_free(sbi->s_root_entry);
  goto err;
err_bad_sb:
  printk("TARFS : can't read super block\n");
err:
  kfree(sbi);
  sb->s_fs_info = NULL;
  return err;
}

/*
 * Mount a TarFS file system.
 */
static struct dentry *tarfs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
{
  return mount_bdev(fs_type, flags, dev_name, data, tarfs_fill_super);
}

/*
 * TarFS file system type.
 */
static struct file_system_type tarfs_type = {
  .owner          = THIS_MODULE,
  .name           = "tarfs",
  .mount          = tarfs_mount,
  .kill_sb        = kill_block_super,
  .fs_flags       = FS_REQUIRES_DEV,
};

/*
 * Init TarFS module.
 */
static int __init init_tarfs(void)
{
  int err;
  
  /* init inode cache */
  err = init_inodecache();
  if (err)
    return err;
  
  /* register TarFS */
  err = register_filesystem(&tarfs_type);
  if (err) {
    destroy_inodecache();
    return err;
  }
  
  return 0;
}

/*
 * Exit TarFS module.
 */
static void __exit exit_tarfs(void)
{
  unregister_filesystem(&tarfs_type);
  destroy_inodecache();
}

module_init(init_tarfs);
module_exit(exit_tarfs);
MODULE_LICENSE("GPL");
