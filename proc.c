#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>

#include "tarfs.h"

#define TARFS_ALIGN_UP(x)               (((x) + TARFS_BLOCK_SIZE - 1) & ~(TARFS_BLOCK_SIZE - 1))

/*
 * Convert tar type to POSIX.
 */
static inline mode_t tar_type_to_posix(int typeflag)
{
  switch(typeflag) {
    case TAR_REGTYPE:
    case TAR_AREGTYPE:
      return S_IFREG;
    case TAR_DIRTYPE:
      return S_IFDIR;
    case TAR_SYMTYPE:
    case TAR_LNKTYPE:
      return S_IFLNK;
    case TAR_CHRTYPE:
      return S_IFCHR;
    case TAR_BLKTYPE:
      return S_IFBLK;
    case TAR_FIFOTYPE:
      return S_IFIFO;
    default:
      return 0;
  }
}

/*
 * Get or create a tar entry.
 */
static struct tar_entry *tar_get_or_create_entry(struct super_block *sb, struct tar_entry *parent, const char *name,
                                                 char *linkname, struct tar_header *hdr, off_t offset)
{
  struct tarfs_sb_info *sbi = tarfs_sb(sb);
  struct tar_entry *entry = NULL;
  struct list_head *pos;
  size_t link_name_len;
  
  /* check if entry already exist */
  if (parent) {
    list_for_each(pos, &parent->children) {
      entry = list_entry(pos, struct tar_entry, list);
      if (strcmp(entry->name, name) == 0)
        return entry;
    }
  }

  /* create new entry */
  entry = (struct tar_entry *) kmalloc(sizeof(struct tar_entry), GFP_KERNEL);
  if (!entry)
    goto err;

  /* set new entry name */
  entry->linkname = NULL;
  entry->name = kstrdup(name, GFP_KERNEL);
  if (!entry->name)
    goto err;

  /* set link name (hard link : add root '/') */
  if (hdr && hdr->typeflag == TAR_SYMTYPE) {
    entry->linkname = kstrdup(linkname, GFP_KERNEL);
    if (!entry->linkname)
      goto err;
  } else if (hdr && hdr->typeflag == TAR_LNKTYPE) {
    link_name_len = strlen(linkname);

    /* reallocate link name */
    entry->linkname = (char *) kmalloc(link_name_len + 2, GFP_KERNEL);
    if (!entry->linkname)
      goto err;

    /* concat '/' and link name */
    entry->linkname[0] = '/';
    memcpy(entry->linkname + 1, linkname, link_name_len);
    entry->linkname[link_name_len + 1] = 0;
  }

  /* parse tar header */
  if (hdr) {
    entry->data_off = offset + TARFS_BLOCK_SIZE;
    entry->mtime.tv_nsec = 0;
    entry->atime.tv_nsec = 0;
    entry->ctime.tv_nsec = 0;
    
    /* get file size */
    if (kstrtoul(hdr->size, 8, &entry->data_len) != 0)
      goto err;
    
    /* get file mode */
    if (kstrtouint(hdr->mode, 8, &entry->mode) != 0)
      goto err;
    entry->mode |= tar_type_to_posix(hdr->typeflag);
    
    /* get uid */
    if (kstrtouint(hdr->uid, 8, &entry->uid) != 0)
      goto err;
    
    /* get gid */
    if (kstrtouint(hdr->gid, 8, &entry->gid) != 0)
      goto err;
    
    /* get last modification time */
    if (kstrtoull(hdr->mtime, 8, &entry->mtime.tv_sec) != 0)
      goto err;
    
    /* get last access time */
    if (kstrtoull(hdr->atime, 8, &entry->atime.tv_sec) != 0)
      entry->atime = entry->mtime;
    
    /* get creation time */
    if (kstrtoull(hdr->ctime, 8, &entry->ctime.tv_sec) != 0)
      entry->ctime = entry->mtime;
  } else {
    entry->data_off = 0;
    entry->data_len = 0;
    entry->mode = S_IFDIR | 0755;
    entry->uid = 0;
    entry->gid = 0;
    entry->atime.tv_sec = entry->mtime.tv_sec = entry->ctime.tv_sec = 0;
    entry->atime.tv_nsec = entry->mtime.tv_nsec = entry->ctime.tv_nsec = 0;
  }

  /* set inode number */
  entry->ino = sbi->s_ninodes++;

  /* init lists */
  INIT_LIST_HEAD(&entry->children);
  INIT_LIST_HEAD(&entry->list);

  /* add to parent */
  if (parent) {
    entry->parent = parent;
    list_add(&entry->list, &parent->children);
  } else {
    entry->parent = NULL;
  }

  return entry;
err:
  if (entry) {
    if (entry->name)
      kfree(entry->name);
    if (entry->linkname)
      kfree(entry->linkname);
    kfree(entry);
  }
  return NULL;
}

/*
 * Build a tar entry long name (long names are stored in data blocks)
 * Header and offset will be updated to point to real tar header.
 */
static char *tar_build_long_name(struct super_block *sb, struct tar_header *hdr, off_t *offset)
{
  size_t full_name_len, pos, count;
  struct buffer_head *bh;
  char *full_name;

  /* get full name length */
  if (kstrtoul(hdr->size, 8, &full_name_len) != 0)
    return NULL;

  /* allocate full name */
  full_name = (char *) kmalloc(full_name_len + 1, GFP_KERNEL);
  if (!full_name)
    return NULL;

  /* compute offset */
  for (pos = 0, *offset += sb->s_blocksize; pos < full_name_len;) {
    /* get next data block */
    bh = sb_bread(sb, *offset / sb->s_blocksize);
    if (!bh) {
      kfree(full_name);
      return NULL;
    }

    /* copy full name */
    count = full_name_len - pos < sb->s_blocksize ? full_name_len - pos : sb->s_blocksize;
    memcpy(full_name + pos, bh->b_data, count);

    /* release block buffer */
    brelse(bh);

    /* update position and offset */
    pos += count;
    *offset += sb->s_blocksize;
  }

  /* end full name */
  full_name[full_name_len] = 0;

  /* remove last '/' */
  if (full_name[full_name_len - 1] == '/')
    full_name[full_name_len - 1] = 0;

  /* get next block buffer (= real tar header) */
  bh = sb_bread(sb, *offset / sb->s_blocksize);
  if (!bh) {
    kfree(full_name);
    return NULL;
  }

  /* update tar header */
  memcpy(hdr, bh->b_data, sizeof(struct tar_header));
  brelse(bh);

  return full_name;
}

/*
 * Build full name of a TAR entry.
 */
static char *tar_build_full_name(struct super_block *sb, struct tar_header *hdr, off_t *offset)
{
  size_t prefix_len, name_len, full_name_len;
  char *full_name;

  /* build long name */
  if (hdr->typeflag == TAR_LONGNAME)
    return tar_build_long_name(sb, hdr, offset);

  /* compute name length */
  prefix_len = strnlen(hdr->prefix, sizeof(hdr->prefix));
  name_len = strnlen(hdr->name, sizeof(hdr->name));
  full_name_len = prefix_len + name_len;

  /* allocate full name */
  full_name = (char *) kmalloc(full_name_len + 1, GFP_KERNEL);
  if (!full_name)
    return NULL;

  /* concat prefix and name */
  memcpy(full_name, hdr->prefix, prefix_len);
  memcpy(full_name + prefix_len, hdr->name, name_len);
  full_name[full_name_len] = 0;

  /* remove last '/' */
  if (full_name[full_name_len - 1] == '/')
    full_name[full_name_len - 1] = 0;

  return full_name;
}

/*
 * Build link name.
 */
static char *tar_build_link_name(struct super_block *sb, struct tar_header *hdr, off_t *offset)
{
  size_t link_name_len;
  char *link_name;

  /* long link name */
  if (hdr->typeflag == TAR_LONGLINK)
    return tar_build_long_name(sb, hdr, offset);

  /* get link name length */
  link_name_len = strnlen(hdr->linkname, sizeof(hdr->linkname));
  if (!link_name_len)
    return NULL;

  /* allocate link name */
  link_name = (char *) kmalloc(link_name_len + 1, GFP_KERNEL);
  if (!link_name)
    return NULL;

  /* set link name */
  memcpy(link_name, hdr->linkname, link_name_len);
  link_name[link_name_len] = 0;

  return link_name;
}

/*
 * Parse a TAR entry.
 */
static struct tar_entry *tar_parse_entry(struct super_block *sb, off_t offset)
{
  char *full_name, *link_name, *start, *end;
  struct tar_entry *entry = NULL, *parent;
  struct tar_header hdr;
  struct buffer_head *bh;

  /* read block buffer */
  bh = sb_bread(sb, offset / sb->s_blocksize);
  if (!bh)
    return NULL;

  /* get tar header */
  memcpy(&hdr, bh->b_data, sizeof(struct tar_header));
  brelse(bh);
  
  /* check magic string */
  if (memcmp(hdr.magic, TARFS_MAGIC_STR, sizeof(hdr.magic)))
    return NULL;

  /* build link name */
  link_name = NULL;
  if (hdr.typeflag == TAR_LNKTYPE || hdr.typeflag == TAR_SYMTYPE || hdr.typeflag == TAR_LONGLINK) {
    link_name = tar_build_link_name(sb, &hdr, &offset);
    if (!link_name)
      return NULL;
  }

  /* build full name */
  full_name = tar_build_full_name(sb, &hdr, &offset);
  if (!full_name) {
    if (link_name)
      kfree(link_name);

    return NULL;
  }

  /* parse full name */
  for (start = full_name, parent = tarfs_sb(sb)->s_root_entry;;) {
    /* skip '/' */
    for (; *start == '/' && *start; start++);

    /* find next folder in path */
    end = strchr(start, '/');
    if (end)
      *end = 0;

    /* create a new entry */
    entry = tar_get_or_create_entry(sb, parent, start, end ? NULL : link_name, end ? NULL : &hdr, offset);
    if (!end || !entry)
      break;

    /* go to next folder */
    start = end + 1;
    parent = entry;
  }

  /* free full name */
  kfree(full_name);
  if (link_name)
    kfree(link_name);

  return entry;
}

/*
 * Create and parse a tar archive.
 */
int tar_create(struct super_block *sb)
{
  struct tarfs_sb_info *sbi = tarfs_sb(sb);
  struct tar_entry *entry;
  off_t offset;
  
  /* set start inode */
  sbi->s_ninodes = TARFS_ROOT_INO;

  /* create root entry */
  sbi->s_root_entry = tar_get_or_create_entry(sb, NULL, "/", NULL, NULL, 0);
  if (!sbi->s_root_entry)
    return -ENOSPC;

  /* parse each entry */
  for (offset = 0;;) {
    /* parse entry */
    entry = tar_parse_entry(sb, offset);
    if (!entry)
      break;

    /* update offset */
    offset = TARFS_ALIGN_UP(entry->data_off + entry->data_len);
  }

  return 0;
}

/*
 * Free a tar entry and its children.
 */
void tar_free(struct tar_entry *entry)
{
  struct list_head *pos, *n;
  
  /* free name */
  if (entry->name)
    kfree(entry->name);
  
  /* free children */
  list_for_each_safe(pos, n, &entry->children)
    tar_free(list_entry(pos, struct tar_entry, list));
}

/*
 * Index a tar entry by its inode number.
 */
void tar_index(struct super_block *sb, struct tar_entry *entry)
{
  struct list_head *pos;
  
  if (!entry)
    return;
  
  /* index entry */
  tarfs_sb(sb)->s_tar_entries[entry->ino] = entry;
  
  /* index children */
  list_for_each(pos, &entry->children)
    tar_index(sb, list_entry(pos, struct tar_entry, list));
}
