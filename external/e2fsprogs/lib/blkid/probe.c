
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/types.h>
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_SYS_MKDEV_H
#include <sys/mkdev.h>
#endif
#include <sys/utsname.h>
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include "blkidP.h"
#include "uuid/uuid.h"
#include "probe.h"

static int figure_label_len(const unsigned char *label, int len)
{
	const unsigned char *end = label + len - 1;

	while ((*end == ' ' || *end == 0) && end >= label)
		--end;
	if (end >= label) {
		label = label;
		return end - label + 1;
	}
	return 0;
}

static unsigned char *get_buffer(struct blkid_probe *pr, 
			  blkid_loff_t off, size_t len)
{
	ssize_t		ret_read;
	unsigned char	*newbuf;

	if (off + len <= SB_BUFFER_SIZE) {
		if (!pr->sbbuf) {
			pr->sbbuf = malloc(SB_BUFFER_SIZE);
			if (!pr->sbbuf)
				return NULL;
			if (lseek(pr->fd, 0, SEEK_SET) < 0)
				return NULL;
			ret_read = read(pr->fd, pr->sbbuf, SB_BUFFER_SIZE);
			if (ret_read < 0)
				ret_read = 0;
			pr->sb_valid = ret_read;
		}
		if (off+len > pr->sb_valid)
			return NULL;
		return pr->sbbuf + off;
	} else {
		if (len > pr->buf_max) {
			newbuf = realloc(pr->buf, len);
			if (newbuf == NULL)
				return NULL;
			pr->buf = newbuf;
			pr->buf_max = len;
		}
		if (blkid_llseek(pr->fd, off, SEEK_SET) < 0)
			return NULL;
		ret_read = read(pr->fd, pr->buf, len);
		if (ret_read != (ssize_t) len)
			return NULL;
		return pr->buf;
	}
}


static int check_mdraid(int fd, unsigned char *ret_uuid)
{
	struct mdp_superblock_s *md;
	blkid_loff_t		offset;
	char			buf[4096];
	
	if (fd < 0)
		return -BLKID_ERR_PARAM;

	offset = (blkid_get_dev_size(fd) & ~((blkid_loff_t)65535)) - 65536;

	if (blkid_llseek(fd, offset, 0) < 0 ||
	    read(fd, buf, 4096) != 4096)
		return -BLKID_ERR_IO;

	/* Check for magic number */
	if (memcmp("\251+N\374", buf, 4) && memcmp("\374N+\251", buf, 4))
		return -BLKID_ERR_PARAM;

	if (!ret_uuid)
		return 0;
	*ret_uuid = 0;

	/* The MD UUID is not contiguous in the superblock, make it so */
	md = (struct mdp_superblock_s *)buf;
	if (md->set_uuid0 || md->set_uuid1 || md->set_uuid2 || md->set_uuid3) {
		memcpy(ret_uuid, &md->set_uuid0, 4);
		memcpy(ret_uuid + 4, &md->set_uuid1, 12);
	}
	return 0;
}

static void set_uuid(blkid_dev dev, uuid_t uuid, const char *tag)
{
	char	str[37];

	if (!uuid_is_null(uuid)) {
		uuid_unparse(uuid, str);
		blkid_set_tag(dev, tag ? tag : "UUID", str, sizeof(str));
	}
}

static void get_ext2_info(blkid_dev dev, struct blkid_magic *id,
			  unsigned char *buf)
{
	struct ext2_super_block *es = (struct ext2_super_block *) buf;
	const char *label = 0;

	DBG(DEBUG_PROBE, printf("ext2_sb.compat = %08X:%08X:%08X\n", 
		   blkid_le32(es->s_feature_compat),
		   blkid_le32(es->s_feature_incompat),
		   blkid_le32(es->s_feature_ro_compat)));

	if (strlen(es->s_volume_name))
		label = es->s_volume_name;
	blkid_set_tag(dev, "LABEL", label, sizeof(es->s_volume_name));

	set_uuid(dev, es->s_uuid, 0);

	if ((es->s_feature_compat & EXT3_FEATURE_COMPAT_HAS_JOURNAL) &&
	    !uuid_is_null(es->s_journal_uuid))
		set_uuid(dev, es->s_journal_uuid, "EXT_JOURNAL");

	if (strcmp(id->bim_type, "ext2") &&
	    ((blkid_le32(es->s_feature_incompat) &
	      EXT2_FEATURE_INCOMPAT_UNSUPPORTED) == 0))
		blkid_set_tag(dev, "SEC_TYPE", "ext2", sizeof("ext2"));
}

static int fs_proc_check(const char *fs_name)
{
	FILE	*f;
	char	buf[80], *cp, *t;

	f = fopen("/proc/filesystems", "r");
	if (!f)
		return (0);
	while (!feof(f)) {
		if (!fgets(buf, sizeof(buf), f))
			break;
		cp = buf;
		if (!isspace(*cp)) {
			while (*cp && !isspace(*cp))
				cp++;
		}
		while (*cp && isspace(*cp))
			cp++;
		if ((t = strchr(cp, '\n')) != NULL)
			*t = 0;
		if ((t = strchr(cp, '\t')) != NULL)
			*t = 0;
		if ((t = strchr(cp, ' ')) != NULL)
			*t = 0;
		if (!strcmp(fs_name, cp)) {
			fclose(f);
			return (1);
		}
	}
	fclose(f);
	return (0);
}

static int check_for_modules(const char *fs_name)
{
	struct utsname	uts;
	FILE		*f;
	char		buf[1024], *cp, *t;
	int		i;

	if (uname(&uts))
		return (0);
	snprintf(buf, sizeof(buf), "/lib/modules/%s/modules.dep", uts.release);

	f = fopen(buf, "r");
	if (!f)
		return (0);
	while (!feof(f)) {
		if (!fgets(buf, sizeof(buf), f))
			break;
		if ((cp = strchr(buf, ':')) != NULL)
			*cp = 0;
		else
			continue;
		if ((cp = strrchr(buf, '/')) != NULL)
			cp++;
		i = strlen(cp);
		if (i > 3) {
			t = cp + i - 3;
			if (!strcmp(t, ".ko"))
				*t = 0;
		}
		if (!strcmp(cp, fs_name))
			return (1);
	}
	fclose(f);
	return (0);
}

static int system_supports_ext4(void)
{
	static time_t	last_check = 0;
	static int	ret = -1;
	time_t		now = time(0);

	if (ret != -1 || (last_check - now) < 5)
		return ret;
	last_check = now;
	ret = (fs_proc_check("ext4") || check_for_modules("ext4"));
	return ret;
}

static int system_supports_ext4dev(void)
{
	static time_t	last_check = 0;
	static int	ret = -1;
	time_t		now = time(0);

	if (ret != -1 || (last_check - now) < 5)
		return ret;
	last_check = now;
	ret = (fs_proc_check("ext4dev") || check_for_modules("ext4dev"));
	return ret;
}

static int probe_ext4dev(struct blkid_probe *probe,
			 struct blkid_magic *id,
			 unsigned char *buf)
{
	struct ext2_super_block *es;
	es = (struct ext2_super_block *)buf;

	/* Distinguish from jbd */
	if (blkid_le32(es->s_feature_incompat) &
	    EXT3_FEATURE_INCOMPAT_JOURNAL_DEV)
		return -BLKID_ERR_PARAM;

	/* ext4dev requires a journal */
	if (!(blkid_le32(es->s_feature_compat) &
	      EXT3_FEATURE_COMPAT_HAS_JOURNAL))
		return -BLKID_ERR_PARAM;

	/*
	 * If the filesystem is marked as OK for use by in-development
	 * filesystem code, but ext4dev is not supported, and ext4 is,
	 * then don't call ourselves ext4dev, since we should be
	 * detected as ext4 in that case.
	 *
	 * If the filesystem is marked as in use by production
	 * filesystem, then it can only be used by ext4 and NOT by
	 * ext4dev, so always disclaim we are ext4dev in that case.
	 */
	if (blkid_le32(es->s_flags) & EXT2_FLAGS_TEST_FILESYS) {
		if (!system_supports_ext4dev() && system_supports_ext4())
			return -BLKID_ERR_PARAM;
	} else
		return -BLKID_ERR_PARAM;

    	get_ext2_info(probe->dev, id, buf);
	return 0;
}

static int probe_ext4(struct blkid_probe *probe, struct blkid_magic *id,
		      unsigned char *buf)
{
	struct ext2_super_block *es;
	es = (struct ext2_super_block *)buf;

	/* Distinguish from jbd */
	if (blkid_le32(es->s_feature_incompat) & 
	    EXT3_FEATURE_INCOMPAT_JOURNAL_DEV)
		return -BLKID_ERR_PARAM;

	/* ext4 requires journal */
	if (!(blkid_le32(es->s_feature_compat) &
	      EXT3_FEATURE_COMPAT_HAS_JOURNAL))
		return -BLKID_ERR_PARAM;

	/* Ext4 has at least one feature which ext3 doesn't understand */
	if (!(blkid_le32(es->s_feature_ro_compat) &
	      EXT3_FEATURE_RO_COMPAT_UNSUPPORTED) &&
	    !(blkid_le32(es->s_feature_incompat) &
	      EXT3_FEATURE_INCOMPAT_UNSUPPORTED))
		return -BLKID_ERR_PARAM;

	/*
	 * If the filesystem is a OK for use by in-development
	 * filesystem code, and ext4dev is supported or ext4 is not
	 * supported, then don't call ourselves ext4, so we can redo
	 * the detection and mark the filesystem as ext4dev.
	 *
	 * If the filesystem is marked as in use by production
	 * filesystem, then it can only be used by ext4 and NOT by
	 * ext4dev.
	 */
	if (blkid_le32(es->s_flags) & EXT2_FLAGS_TEST_FILESYS) {
		if (system_supports_ext4dev() || !system_supports_ext4())
			return -BLKID_ERR_PARAM;
	}
    	get_ext2_info(probe->dev, id, buf);
	return 0;
}

static int probe_ext3(struct blkid_probe *probe, struct blkid_magic *id,
		      unsigned char *buf)
{
	struct ext2_super_block *es;
	es = (struct ext2_super_block *)buf;

	/* Distinguish from ext4dev */
	if (blkid_le32(es->s_flags) & EXT2_FLAGS_TEST_FILESYS)
		return -BLKID_ERR_PARAM;

	/* ext3 requires journal */
	if (!(blkid_le32(es->s_feature_compat) &
	      EXT3_FEATURE_COMPAT_HAS_JOURNAL))
		return -BLKID_ERR_PARAM;

	/* Any features which ext3 doesn't understand */
	if ((blkid_le32(es->s_feature_ro_compat) &
	     EXT3_FEATURE_RO_COMPAT_UNSUPPORTED) ||
	    (blkid_le32(es->s_feature_incompat) &
	     EXT3_FEATURE_INCOMPAT_UNSUPPORTED))
		return -BLKID_ERR_PARAM;

    	get_ext2_info(probe->dev, id, buf);
	return 0;
}

static int probe_ext2(struct blkid_probe *probe, struct blkid_magic *id,
		      unsigned char *buf)
{
	struct ext2_super_block *es;

	es = (struct ext2_super_block *)buf;

	/* Distinguish between ext3 and ext2 */
	if ((blkid_le32(es->s_feature_compat) &
	      EXT3_FEATURE_COMPAT_HAS_JOURNAL))
		return -BLKID_ERR_PARAM;

	/* Any features which ext2 doesn't understand */
	if ((blkid_le32(es->s_feature_ro_compat) &
	     EXT2_FEATURE_RO_COMPAT_UNSUPPORTED) ||
	    (blkid_le32(es->s_feature_incompat) &
	     EXT2_FEATURE_INCOMPAT_UNSUPPORTED))
		return -BLKID_ERR_PARAM;

	get_ext2_info(probe->dev, id, buf);
	return 0;
}

static int probe_jbd(struct blkid_probe *probe, struct blkid_magic *id,
		     unsigned char *buf)
{
	struct ext2_super_block *es = (struct ext2_super_block *) buf;

	if (!(blkid_le32(es->s_feature_incompat) &
	      EXT3_FEATURE_INCOMPAT_JOURNAL_DEV))
		return -BLKID_ERR_PARAM;

	get_ext2_info(probe->dev, id, buf);

	return 0;
}

#define FAT_ATTR_VOLUME_ID		0x08
#define FAT_ATTR_DIR			0x10
#define FAT_ATTR_LONG_NAME		0x0f
#define FAT_ATTR_MASK			0x3f
#define FAT_ENTRY_FREE			0xe5

static const char *no_name = "NO NAME    ";

static unsigned char *search_fat_label(struct vfat_dir_entry *dir, int count)
{
	int i;

	for (i = 0; i < count; i++) {
		if (dir[i].name[0] == 0x00)
			break;
		
		if ((dir[i].name[0] == FAT_ENTRY_FREE) ||
		    (dir[i].cluster_high != 0 || dir[i].cluster_low != 0) ||
		    ((dir[i].attr & FAT_ATTR_MASK) == FAT_ATTR_LONG_NAME))
			continue;

		if ((dir[i].attr & (FAT_ATTR_VOLUME_ID | FAT_ATTR_DIR)) == 
		    FAT_ATTR_VOLUME_ID) {
			return dir[i].name;
		}
	}
	return 0;
}

static int probe_fat(struct blkid_probe *probe,
		      struct blkid_magic *id __BLKID_ATTR((unused)), 
		      unsigned char *buf)
{
	struct vfat_super_block *vs = (struct vfat_super_block *) buf;
	struct msdos_super_block *ms = (struct msdos_super_block *) buf;
	struct vfat_dir_entry *dir;
	char serno[10];
	const unsigned char *label = 0, *vol_label = 0, *tmp;
	unsigned char	*vol_serno;
	int label_len = 0, maxloop = 100;
	__u16 sector_size, dir_entries, reserved;
	__u32 sect_count, fat_size, dir_size, cluster_count, fat_length;
	__u32 buf_size, start_data_sect, next, root_start, root_dir_entries;

	/* sector size check */
	tmp = (unsigned char *)&ms->ms_sector_size;
	sector_size = tmp[0] + (tmp[1] << 8);
	if (sector_size != 0x200 && sector_size != 0x400 &&
	    sector_size != 0x800 && sector_size != 0x1000)
		return 1;

	tmp = (unsigned char *)&ms->ms_dir_entries;
	dir_entries = tmp[0] + (tmp[1] << 8);
	reserved =  blkid_le16(ms->ms_reserved);
	tmp = (unsigned char *)&ms->ms_sectors;
	sect_count = tmp[0] + (tmp[1] << 8);
	if (sect_count == 0)
		sect_count = blkid_le32(ms->ms_total_sect);

	fat_length = blkid_le16(ms->ms_fat_length);
	if (fat_length == 0)
		fat_length = blkid_le32(vs->vs_fat32_length);

	fat_size = fat_length * ms->ms_fats;
	dir_size = ((dir_entries * sizeof(struct vfat_dir_entry)) +
			(sector_size-1)) / sector_size;

	cluster_count = sect_count - (reserved + fat_size + dir_size);
	if (ms->ms_cluster_size == 0)
		return 1;
	cluster_count /= ms->ms_cluster_size;

	if (cluster_count > FAT32_MAX)
		return 1;

	if (ms->ms_fat_length) {
		/* the label may be an attribute in the root directory */
		root_start = (reserved + fat_size) * sector_size;
		root_dir_entries = vs->vs_dir_entries[0] + 
			(vs->vs_dir_entries[1] << 8);

		buf_size = root_dir_entries * sizeof(struct vfat_dir_entry);
		dir = (struct vfat_dir_entry *) get_buffer(probe, root_start, 
							   buf_size);
		if (dir)
			vol_label = search_fat_label(dir, root_dir_entries);

		if (!vol_label || !memcmp(vol_label, no_name, 11))
			vol_label = ms->ms_label;
		vol_serno = ms->ms_serno;

		blkid_set_tag(probe->dev, "SEC_TYPE", "msdos", 
			      sizeof("msdos"));
	} else {
		/* Search the FAT32 root dir for the label attribute */
		buf_size = vs->vs_cluster_size * sector_size;
		start_data_sect = reserved + fat_size;

		next = blkid_le32(vs->vs_root_cluster);
		while (next && --maxloop) {
			__u32 next_sect_off;
			__u64 next_off, fat_entry_off;
			int count;

			next_sect_off = (next - 2) * vs->vs_cluster_size;
			next_off = (start_data_sect + next_sect_off) * 
				sector_size;

			dir = (struct vfat_dir_entry *) 
				get_buffer(probe, next_off, buf_size);
			if (dir == NULL)
				break;

			count = buf_size / sizeof(struct vfat_dir_entry);

			vol_label = search_fat_label(dir, count);
			if (vol_label)
				break;

			/* get FAT entry */
			fat_entry_off = (reserved * sector_size) + 
				(next * sizeof(__u32));
			buf = get_buffer(probe, fat_entry_off, buf_size);
			if (buf == NULL)
				break;

			/* set next cluster */
			next = blkid_le32(*((__u32 *) buf) & 0x0fffffff);
		}

		if (!vol_label || !memcmp(vol_label, no_name, 11))
			vol_label = vs->vs_label;
		vol_serno = vs->vs_serno;
	}

	if (vol_label && memcmp(vol_label, no_name, 11)) {
		if ((label_len = figure_label_len(vol_label, 11)))
			label = vol_label;
	}

	/* We can't just print them as %04X, because they are unaligned */
	sprintf(serno, "%02X%02X-%02X%02X", vol_serno[3], vol_serno[2],
		vol_serno[1], vol_serno[0]);

	blkid_set_tag(probe->dev, "LABEL", (const char *) label, label_len);
	blkid_set_tag(probe->dev, "UUID", serno, sizeof(serno)-1);

	return 0;
}

static int probe_fat_nomagic(struct blkid_probe *probe,
			     struct blkid_magic *id __BLKID_ATTR((unused)), 
			     unsigned char *buf)
{
	struct vfat_super_block *vs;

	vs = (struct vfat_super_block *)buf;

	/* heads check */
	if (vs->vs_heads == 0)
		return 1;

	/* cluster size check*/	
	if (vs->vs_cluster_size == 0 ||
	    (vs->vs_cluster_size & (vs->vs_cluster_size-1)))
		return 1;

	/* media check */
	if (vs->vs_media < 0xf8 && vs->vs_media != 0xf0)
		return 1;

	/* fat counts(Linux kernel expects at least 1 FAT table) */
	if (!vs->vs_fats)
		return 1;

	return probe_fat(probe, id, buf);
}

static int probe_ntfs(struct blkid_probe *probe,
		      struct blkid_magic *id __BLKID_ATTR((unused)), 
		      unsigned char *buf)
{
	struct ntfs_super_block *ns;
	struct master_file_table_record *mft;
	struct file_attribute *attr;
	char		uuid_str[17], label_str[129], *cp;
	int		bytes_per_sector, sectors_per_cluster;
	int		mft_record_size, attr_off, attr_len;
	unsigned int	i, attr_type, val_len;
	int		val_off;
	__u64		nr_clusters;
	blkid_loff_t off;
	unsigned char *buf_mft, *val;

	ns = (struct ntfs_super_block *) buf;

	bytes_per_sector = ns->bios_parameter_block[0] +
		(ns->bios_parameter_block[1]  << 8);
	sectors_per_cluster = ns->bios_parameter_block[2];

	if ((bytes_per_sector < 512) || (sectors_per_cluster == 0))
		return 1;

	if (ns->cluster_per_mft_record < 0)
		mft_record_size = 1 << (0-ns->cluster_per_mft_record);
	else
		mft_record_size = ns->cluster_per_mft_record * 
			sectors_per_cluster * bytes_per_sector;
	nr_clusters = blkid_le64(ns->number_of_sectors) / sectors_per_cluster;

	if ((blkid_le64(ns->mft_cluster_location) > nr_clusters) ||
	    (blkid_le64(ns->mft_mirror_cluster_location) > nr_clusters))
		return 1;

	off = blkid_le64(ns->mft_mirror_cluster_location) * 
		bytes_per_sector * sectors_per_cluster;

	buf_mft = get_buffer(probe, off, mft_record_size);
	if (!buf_mft)
		return 1;

	if (memcmp(buf_mft, "FILE", 4))
		return 1;

	off = blkid_le64(ns->mft_cluster_location) * bytes_per_sector * 
		sectors_per_cluster;

	buf_mft = get_buffer(probe, off, mft_record_size);
	if (!buf_mft)
		return 1;

	if (memcmp(buf_mft, "FILE", 4))
		return 1;

	off += MFT_RECORD_VOLUME * mft_record_size;

	buf_mft = get_buffer(probe, off, mft_record_size);
	if (!buf_mft)
		return 1;

	if (memcmp(buf_mft, "FILE", 4))
		return 1;

	mft = (struct master_file_table_record *) buf_mft;

	attr_off = blkid_le16(mft->attrs_offset);
	label_str[0] = 0;
	
	while (1) {
		attr = (struct file_attribute *) (buf_mft + attr_off);
		attr_len = blkid_le16(attr->len);
		attr_type = blkid_le32(attr->type);
		val_off = blkid_le16(attr->value_offset);
		val_len = blkid_le32(attr->value_len);

		attr_off += attr_len;

		if ((attr_off > mft_record_size) ||
		    (attr_len == 0))
			break;

		if (attr_type == MFT_RECORD_ATTR_END)
			break;

		if (attr_type == MFT_RECORD_ATTR_VOLUME_NAME) {
			if (val_len > sizeof(label_str))
				val_len = sizeof(label_str)-1;

			for (i=0, cp=label_str; i < val_len; i+=2,cp++) {
				val = ((__u8 *) attr) + val_off + i;
				*cp = val[0];
				if (val[1])
					*cp = '?';
			}
			*cp = 0;
		}
	}

	sprintf(uuid_str, "%016llX", blkid_le64(ns->volume_serial));
	blkid_set_tag(probe->dev, "UUID", uuid_str, 0);
	if (label_str[0])
		blkid_set_tag(probe->dev, "LABEL", label_str, 0);
	return 0;
}


static int probe_xfs(struct blkid_probe *probe,
		     struct blkid_magic *id __BLKID_ATTR((unused)), 
		     unsigned char *buf)
{
	struct xfs_super_block *xs;
	const char *label = 0;

	xs = (struct xfs_super_block *)buf;

	if (strlen(xs->xs_fname))
		label = xs->xs_fname;
	blkid_set_tag(probe->dev, "LABEL", label, sizeof(xs->xs_fname));
	set_uuid(probe->dev, xs->xs_uuid, 0);
	return 0;
}

static int probe_reiserfs(struct blkid_probe *probe,
			  struct blkid_magic *id, unsigned char *buf)
{
	struct reiserfs_super_block *rs = (struct reiserfs_super_block *) buf;
	unsigned int blocksize;
	const char *label = 0;

	blocksize = blkid_le16(rs->rs_blocksize);

	/* The blocksize must be at least 1k */
	if ((blocksize >> 10) == 0)
		return -BLKID_ERR_PARAM;

	/* If the superblock is inside the journal, we have the wrong one */
	if (id->bim_kboff/(blocksize>>10) > blkid_le32(rs->rs_journal_block))
		return -BLKID_ERR_BIG;

	/* LABEL/UUID are only valid for later versions of Reiserfs v3.6. */
	if (id->bim_magic[6] == '2' || id->bim_magic[6] == '3') {
		if (strlen(rs->rs_label))
			label = rs->rs_label;
		set_uuid(probe->dev, rs->rs_uuid, 0);
	}
	blkid_set_tag(probe->dev, "LABEL", label, sizeof(rs->rs_label));

	return 0;
}

static int probe_reiserfs4(struct blkid_probe *probe,
			   struct blkid_magic *id __BLKID_ATTR((unused)), 
			   unsigned char *buf)
{
	struct reiser4_super_block *rs4 = (struct reiser4_super_block *) buf;
	const unsigned char *label = 0;

	if (strlen((char *) rs4->rs4_label))
		label = rs4->rs4_label;
	set_uuid(probe->dev, rs4->rs4_uuid, 0);
	blkid_set_tag(probe->dev, "LABEL", (const char *) label, 
		      sizeof(rs4->rs4_label));

	return 0;
}

static int probe_jfs(struct blkid_probe *probe,
		     struct blkid_magic *id __BLKID_ATTR((unused)), 
		     unsigned char *buf)
{
	struct jfs_super_block *js;
	const char *label = 0;

	js = (struct jfs_super_block *)buf;

	if (strlen((char *) js->js_label))
		label = (char *) js->js_label;
	blkid_set_tag(probe->dev, "LABEL", label, sizeof(js->js_label));
	set_uuid(probe->dev, js->js_uuid, 0);
	return 0;
}

static int probe_luks(struct blkid_probe *probe,
		       struct blkid_magic *id __BLKID_ATTR((unused)),
		       unsigned char *buf)
{
	char uuid[40];
	/* 168 is the offset to the 40 character uuid:
	 * http://luks.endorphin.org/LUKS-on-disk-format.pdf */
	strncpy(uuid, (char *) buf+168, 40);
	blkid_set_tag(probe->dev, "UUID", uuid, sizeof(uuid));
	return 0;
}

static int probe_romfs(struct blkid_probe *probe,
		       struct blkid_magic *id __BLKID_ATTR((unused)), 
		       unsigned char *buf)
{
	struct romfs_super_block *ros;
	const char *label = 0;

	ros = (struct romfs_super_block *)buf;

	if (strlen((char *) ros->ros_volume))
		label = (char *) ros->ros_volume;
	blkid_set_tag(probe->dev, "LABEL", label, 0);
	return 0;
}

static int probe_cramfs(struct blkid_probe *probe,
			struct blkid_magic *id __BLKID_ATTR((unused)), 
			unsigned char *buf)
{
	struct cramfs_super_block *csb;
	const char *label = 0;

	csb = (struct cramfs_super_block *)buf;

	if (strlen((char *) csb->name))
		label = (char *) csb->name;
	blkid_set_tag(probe->dev, "LABEL", label, 0);
	return 0;
}

static int probe_swap0(struct blkid_probe *probe,
		       struct blkid_magic *id __BLKID_ATTR((unused)),
		       unsigned char *buf __BLKID_ATTR((unused)))
{
	blkid_set_tag(probe->dev, "UUID", 0, 0);
	blkid_set_tag(probe->dev, "LABEL", 0, 0);
	return 0;
}

static int probe_swap1(struct blkid_probe *probe,
		       struct blkid_magic *id __BLKID_ATTR((unused)),
		       unsigned char *buf __BLKID_ATTR((unused)))
{
	struct swap_id_block *sws;

	probe_swap0(probe, id, buf);
	/*
	 * Version 1 swap headers are always located at offset of 1024
	 * bytes, although the swap signature itself is located at the
	 * end of the page (which may vary depending on hardware
	 * pagesize).
	 */
	sws = (struct swap_id_block *) get_buffer(probe, 1024, 1024);
	if (!sws)
		return 1;

	/* arbitrary sanity check.. is there any garbage down there? */
	if (sws->sws_pad[32] == 0 && sws->sws_pad[33] == 0)  {
		if (sws->sws_volume[0])
			blkid_set_tag(probe->dev, "LABEL", sws->sws_volume, 
				      sizeof(sws->sws_volume));
		if (sws->sws_uuid[0])
			set_uuid(probe->dev, sws->sws_uuid, 0);
	}
	return 0;
}

static int probe_iso9660(struct blkid_probe *probe,
			 struct blkid_magic *id __BLKID_ATTR((unused)), 
			 unsigned char *buf)
{
	struct iso_volume_descriptor *iso;
	const unsigned char *label;

	iso = (struct iso_volume_descriptor *) buf;
	label = iso->volume_id;

	blkid_set_tag(probe->dev, "LABEL", (const char *) label, 
		      figure_label_len(label, 32));
	return 0;
}


static const char
*udf_magic[] = { "BEA01", "BOOT2", "CD001", "CDW02", "NSR02",
		 "NSR03", "TEA01", 0 };

static int probe_udf(struct blkid_probe *probe,
		     struct blkid_magic *id __BLKID_ATTR((unused)), 
		     unsigned char *buf __BLKID_ATTR((unused)))
{
	int j, bs;
	struct iso_volume_descriptor *isosb;
	const char ** m;

	/* determine the block size by scanning in 2K increments
	   (block sizes larger than 2K will be null padded) */
	for (bs = 1; bs < 16; bs++) {
		isosb = (struct iso_volume_descriptor *) 
			get_buffer(probe, bs*2048+32768, sizeof(isosb));
		if (!isosb)
			return 1;
		if (isosb->vd_id[0])
			break;
	}

	/* Scan up to another 64 blocks looking for additional VSD's */
	for (j = 1; j < 64; j++) {
		if (j > 1) {
			isosb = (struct iso_volume_descriptor *) 
				get_buffer(probe, j*bs*2048+32768, 
					   sizeof(isosb));
			if (!isosb)
				return 1;
		}
		/* If we find NSR0x then call it udf:
		   NSR01 for UDF 1.00
		   NSR02 for UDF 1.50
		   NSR03 for UDF 2.00 */
		if (!memcmp(isosb->vd_id, "NSR0", 4))
			return probe_iso9660(probe, id, buf);
		for (m = udf_magic; *m; m++)
			if (!memcmp(*m, isosb->vd_id, 5))
				break;
		if (*m == 0)
			return 1;
	}
	return 1;
}

static int probe_ocfs(struct blkid_probe *probe,
		      struct blkid_magic *id __BLKID_ATTR((unused)), 
		      unsigned char *buf)
{
	struct ocfs_volume_header ovh;
	struct ocfs_volume_label ovl;
	__u32 major;

	memcpy(&ovh, buf, sizeof(ovh));
	memcpy(&ovl, buf+512, sizeof(ovl));

	major = ocfsmajor(ovh);
	if (major == 1)
		blkid_set_tag(probe->dev,"SEC_TYPE","ocfs1",sizeof("ocfs1"));
	else if (major >= 9)
		blkid_set_tag(probe->dev,"SEC_TYPE","ntocfs",sizeof("ntocfs"));
	
	blkid_set_tag(probe->dev, "LABEL", ovl.label, ocfslabellen(ovl));
	blkid_set_tag(probe->dev, "MOUNT", ovh.mount, ocfsmountlen(ovh));
	set_uuid(probe->dev, ovl.vol_id, 0);
	return 0;
}

static int probe_ocfs2(struct blkid_probe *probe,
		       struct blkid_magic *id __BLKID_ATTR((unused)), 
		       unsigned char *buf)
{
	struct ocfs2_super_block *osb;

	osb = (struct ocfs2_super_block *)buf;

	blkid_set_tag(probe->dev, "LABEL", osb->s_label, sizeof(osb->s_label));
	set_uuid(probe->dev, osb->s_uuid, 0);
	return 0;
}

static int probe_oracleasm(struct blkid_probe *probe,
			   struct blkid_magic *id __BLKID_ATTR((unused)), 
			   unsigned char *buf)
{
	struct oracle_asm_disk_label *dl;

	dl = (struct oracle_asm_disk_label *)buf;

	blkid_set_tag(probe->dev, "LABEL", dl->dl_id, sizeof(dl->dl_id));
	return 0;
}

static int probe_gfs(struct blkid_probe *probe,
		     struct blkid_magic *id __BLKID_ATTR((unused)),
		     unsigned char *buf)
{
	struct gfs2_sb *sbd;
	const char *label = 0;

	sbd = (struct gfs2_sb *)buf;

	if (blkid_be32(sbd->sb_fs_format) == GFS_FORMAT_FS &&
	    blkid_be32(sbd->sb_multihost_format) == GFS_FORMAT_MULTI)
	{	
		blkid_set_tag(probe->dev, "UUID", 0, 0);
	
		if (strlen(sbd->sb_locktable))
			label = sbd->sb_locktable;
		blkid_set_tag(probe->dev, "LABEL", label, sizeof(sbd->sb_locktable));
		return 0;
	}
	return 1;
}

static int probe_gfs2(struct blkid_probe *probe,
		     struct blkid_magic *id __BLKID_ATTR((unused)),
		     unsigned char *buf)
{
	struct gfs2_sb *sbd;
	const char *label = 0;

	sbd = (struct gfs2_sb *)buf;

	if (blkid_be32(sbd->sb_fs_format) == GFS2_FORMAT_FS &&
	    blkid_be32(sbd->sb_multihost_format) == GFS2_FORMAT_MULTI)
	{	
		blkid_set_tag(probe->dev, "UUID", 0, 0);
	
		if (strlen(sbd->sb_locktable))
			label = sbd->sb_locktable;
		blkid_set_tag(probe->dev, "LABEL", label, sizeof(sbd->sb_locktable));
		return 0;
	}
	return 1;
}

static int probe_hfsplus(struct blkid_probe *probe __BLKID_ATTR((unused)),
			 struct blkid_magic *id __BLKID_ATTR((unused)),
			 unsigned char *buf)
{
	struct hfs_mdb *sbd = (struct hfs_mdb *)buf;

	/* Check for a HFS+ volume embedded in a HFS volume */
	if (memcmp(sbd->embed_sig, "H+", 2) == 0)
		return 0;

	return 1;
}

#define LVM2_LABEL_SIZE 512
static unsigned int lvm2_calc_crc(const void *buf, unsigned int size)
{
	static const unsigned int crctab[] = {
		0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
		0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
		0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
		0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
	};
	unsigned int i, crc = 0xf597a6cf;
	const __u8 *data = (const __u8 *) buf;

	for (i = 0; i < size; i++) {
		crc ^= *data++;
		crc = (crc >> 4) ^ crctab[crc & 0xf];
		crc = (crc >> 4) ^ crctab[crc & 0xf];
	}
	return crc;
}

static int probe_lvm2(struct blkid_probe *probe,
			struct blkid_magic *id __BLKID_ATTR((unused)),
			unsigned char *buf)
{
	int sector = (id->bim_kboff) << 1;;
	struct lvm2_pv_label_header *label;
	label = (struct lvm2_pv_label_header *)buf;
	char *p, *q, uuid[40];
	unsigned int i, b;

	/* buf is at 0k or 1k offset; find label inside */
	if (memcmp(buf, "LABELONE", 8) == 0) {
		label = (struct lvm2_pv_label_header *)buf;
	} else if (memcmp(buf + 512, "LABELONE", 8) == 0) {
		label = (struct lvm2_pv_label_header *)(buf + 512);
		sector++;
	} else {
		return 1;
	}

	if (blkid_le64(label->sector_xl) != (unsigned) sector) {
		DBG(DEBUG_PROBE,
		    printf("LVM2: label for sector %llu found at sector %d\n",
			   blkid_le64(label->sector_xl), sector));
		return 1;
	}

	if (lvm2_calc_crc(&label->offset_xl, LVM2_LABEL_SIZE -
			  ((char *)&label->offset_xl - (char *)label)) !=
			blkid_le32(label->crc_xl)) {
		DBG(DEBUG_PROBE,
		    printf("LVM2: label checksum incorrect at sector %d\n",
			   sector));
		return 1;
	}

	for (i=0, b=1, p=uuid, q= (char *) label->pv_uuid; i <= 32;
	     i++, b <<= 1) {
		if (b & 0x4444440)
			*p++ = '-';
		*p++ = *q++;
	}

	blkid_set_tag(probe->dev, "UUID", uuid, LVM2_ID_LEN+6);

	return 0;
}
#define BLKID_BLK_OFFS	64	/* currently reiserfs */

static struct blkid_magic type_array[] = {
/*  type     kboff   sboff len  magic			probe */
  { "oracleasm", 0,	32,  8, "ORCLDISK",		probe_oracleasm },
  { "ntfs",	 0,	 3,  8, "NTFS    ",		probe_ntfs },
  { "jbd",	 1,   0x38,  2, "\123\357",		probe_jbd },
  { "ext4dev",	 1,   0x38,  2, "\123\357",		probe_ext4dev },
  { "ext4",	 1,   0x38,  2, "\123\357",		probe_ext4 },
  { "ext3",	 1,   0x38,  2, "\123\357",		probe_ext3 },
  { "ext2",	 1,   0x38,  2, "\123\357",		probe_ext2 },
  { "reiserfs",	 8,   0x34,  8, "ReIsErFs",		probe_reiserfs },
  { "reiserfs", 64,   0x34,  9, "ReIsEr2Fs",		probe_reiserfs },
  { "reiserfs", 64,   0x34,  9, "ReIsEr3Fs",		probe_reiserfs },
  { "reiserfs", 64,   0x34,  8, "ReIsErFs",		probe_reiserfs },
  { "reiserfs",	 8,	20,  8, "ReIsErFs",		probe_reiserfs },
  { "reiser4",  64,	 0,  7, "ReIsEr4",		probe_reiserfs4 },
  { "gfs2",     64,      0,  4, "\x01\x16\x19\x70",     probe_gfs2 },
  { "gfs",      64,      0,  4, "\x01\x16\x19\x70",     probe_gfs },
  { "vfat",      0,   0x52,  5, "MSWIN",                probe_fat },
  { "vfat",      0,   0x52,  8, "FAT32   ",             probe_fat },
  { "vfat",      0,   0x36,  5, "MSDOS",                probe_fat },
  { "vfat",      0,   0x36,  8, "FAT16   ",             probe_fat },
  { "vfat",      0,   0x36,  8, "FAT12   ",             probe_fat },
  { "vfat",      0,      0,  1, "\353",                 probe_fat_nomagic },
  { "vfat",      0,      0,  1, "\351",                 probe_fat_nomagic },
  { "vfat",      0,  0x1fe,  2, "\125\252",             probe_fat_nomagic },
  { "minix",     1,   0x10,  2, "\177\023",             0 },
  { "minix",     1,   0x10,  2, "\217\023",             0 },
  { "minix",	 1,   0x10,  2, "\150\044",		0 },
  { "minix",	 1,   0x10,  2, "\170\044",		0 },
  { "vxfs",	 1,	 0,  4, "\365\374\001\245",	0 },
  { "xfs",	 0,	 0,  4, "XFSB",			probe_xfs },
  { "romfs",	 0,	 0,  8, "-rom1fs-",		probe_romfs },
  { "bfs",	 0,	 0,  4, "\316\372\173\033",	0 },
  { "cramfs",	 0,	 0,  4, "E=\315\050",		probe_cramfs },
  { "qnx4",	 0,	 4,  6, "QNX4FS",		0 },
  { "udf",	32,	 1,  5, "BEA01",		probe_udf },
  { "udf",	32,	 1,  5, "BOOT2",		probe_udf },
  { "udf",	32,	 1,  5, "CD001",		probe_udf },
  { "udf",	32,	 1,  5, "CDW02",		probe_udf },
  { "udf",	32,	 1,  5, "NSR02",		probe_udf },
  { "udf",	32,	 1,  5, "NSR03",		probe_udf },
  { "udf",	32,	 1,  5, "TEA01",		probe_udf },
  { "iso9660",	32,	 1,  5, "CD001",		probe_iso9660 },
  { "iso9660",	32,	 9,  5, "CDROM",		probe_iso9660 },
  { "jfs",	32,	 0,  4, "JFS1",			probe_jfs },
  { "hfsplus",	 1,	 0,  2, "BD",			probe_hfsplus },
  { "hfsplus",	 1,	 0,  2, "H+",			0 },
  { "hfs",	 1,	 0,  2, "BD",			0 },
  { "ufs",	 8,  0x55c,  4, "T\031\001\000",	0 },
  { "hpfs",	 8,	 0,  4, "I\350\225\371",	0 },
  { "sysv",	 0,  0x3f8,  4, "\020~\030\375",	0 },
  { "swap",	 0,  0xff6, 10, "SWAP-SPACE",		probe_swap0 },
  { "swap",	 0,  0xff6, 10, "SWAPSPACE2",		probe_swap1 },
  { "swsuspend", 0,  0xff6,  9, "S1SUSPEND",		probe_swap1 },
  { "swsuspend", 0,  0xff6,  9, "S2SUSPEND",		probe_swap1 },
  { "swap",	 0, 0x1ff6, 10, "SWAP-SPACE",		probe_swap0 },
  { "swap",	 0, 0x1ff6, 10, "SWAPSPACE2",		probe_swap1 },
  { "swsuspend", 0, 0x1ff6,  9, "S1SUSPEND",		probe_swap1 },
  { "swsuspend", 0, 0x1ff6,  9, "S2SUSPEND",		probe_swap1 },
  { "swap",	 0, 0x3ff6, 10, "SWAP-SPACE",		probe_swap0 },
  { "swap",	 0, 0x3ff6, 10, "SWAPSPACE2",		probe_swap1 },
  { "swsuspend", 0, 0x3ff6,  9, "S1SUSPEND",		probe_swap1 },
  { "swsuspend", 0, 0x3ff6,  9, "S2SUSPEND",		probe_swap1 },
  { "swap",	 0, 0x7ff6, 10, "SWAP-SPACE",		probe_swap0 },
  { "swap",	 0, 0x7ff6, 10, "SWAPSPACE2",		probe_swap1 },
  { "swsuspend", 0, 0x7ff6,  9, "S1SUSPEND",		probe_swap1 },
  { "swsuspend", 0, 0x7ff6,  9, "S2SUSPEND",		probe_swap1 },
  { "swap",	 0, 0xfff6, 10, "SWAP-SPACE",		probe_swap0 },
  { "swap",	 0, 0xfff6, 10, "SWAPSPACE2",		probe_swap1 },
  { "swsuspend", 0, 0xfff6,  9, "S1SUSPEND",		probe_swap1 },
  { "swsuspend", 0, 0xfff6,  9, "S2SUSPEND",		probe_swap1 },
  { "ocfs",	 0,	 8,  9,	"OracleCFS",		probe_ocfs },
  { "ocfs2",	 1,	 0,  6,	"OCFSV2",		probe_ocfs2 },
  { "ocfs2",	 2,	 0,  6,	"OCFSV2",		probe_ocfs2 },
  { "ocfs2",	 4,	 0,  6,	"OCFSV2",		probe_ocfs2 },
  { "ocfs2",	 8,	 0,  6,	"OCFSV2",		probe_ocfs2 },
  { "crypt_LUKS", 0,	 0,  6,	"LUKS\xba\xbe",		probe_luks },
  { "squashfs",	 0,	 0,  4,	"sqsh",			0 },
  { "squashfs",	 0,	 0,  4,	"hsqs",			0 },
  { "lvm2pv",	 0,  0x218,  8, "LVM2 001",		probe_lvm2 },
  { "lvm2pv",	 0,  0x018,  8, "LVM2 001",		probe_lvm2 },
  { "lvm2pv",	 1,  0x018,  8, "LVM2 001",		probe_lvm2 },
  { "lvm2pv",	 1,  0x218,  8, "LVM2 001",		probe_lvm2 },
  {   NULL,	 0,	 0,  0, NULL,			NULL }
};

blkid_dev blkid_verify(blkid_cache cache, blkid_dev dev)
{
	struct blkid_magic *id;
	struct blkid_probe probe;
	blkid_tag_iterate iter;
	unsigned char *buf;
	const char *type, *value;
	struct stat st;
	time_t diff, now;
	int idx;

	if (!dev)
		return NULL;

	now = time(0);
	diff = now - dev->bid_time;

	if ((now > dev->bid_time) && (diff > 0) && 
	    ((diff < BLKID_PROBE_MIN) || 
	     (dev->bid_flags & BLKID_BID_FL_VERIFIED &&
	      diff < BLKID_PROBE_INTERVAL)))
		return dev;

	DBG(DEBUG_PROBE,
	    printf("need to revalidate %s (time since last check %llu)\n", 
		   dev->bid_name, (unsigned long long)diff));

	if (((probe.fd = open(dev->bid_name, O_RDONLY)) < 0) ||
	    (fstat(probe.fd, &st) < 0)) {
		if (probe.fd >= 0) close(probe.fd);
		if (errno != EPERM) {
			blkid_free_dev(dev);
			return NULL;
		}
		/* We don't have read permission, just return cache data. */
		DBG(DEBUG_PROBE,
		    printf("returning unverified data for %s\n",
			   dev->bid_name));
		return dev;
	}

	probe.cache = cache;
	probe.dev = dev;
	probe.sbbuf = 0;
	probe.buf = 0;
	probe.buf_max = 0;
	
	/*
	 * Iterate over the type array.  If we already know the type,
	 * then try that first.  If it doesn't work, then blow away
	 * the type information, and try again.
	 * 
	 */
try_again:
	type = 0;
	if (!dev->bid_type || !strcmp(dev->bid_type, "mdraid")) {
		uuid_t	uuid;

		if (check_mdraid(probe.fd, uuid) == 0) {
			set_uuid(dev, uuid, 0);
			type = "mdraid";
			goto found_type;
		}
	}
	for (id = type_array; id->bim_type; id++) {
		if (dev->bid_type &&
		    strcmp(id->bim_type, dev->bid_type))
			continue;

		idx = id->bim_kboff + (id->bim_sboff >> 10);
		buf = get_buffer(&probe, idx << 10, 1024);
		if (!buf)
			continue;

		if (memcmp(id->bim_magic, buf + (id->bim_sboff&0x3ff),
			   id->bim_len))
			continue;

		if ((id->bim_probe == NULL) ||
		    (id->bim_probe(&probe, id, buf) == 0)) {
			type = id->bim_type;
			goto found_type;
		}
	}

	if (!id->bim_type && dev->bid_type) {
		/*
		 * Zap the device filesystem information and try again
		 */
		DBG(DEBUG_PROBE,
		    printf("previous fs type %s not valid, "
			   "trying full probe\n", dev->bid_type));
		iter = blkid_tag_iterate_begin(dev);
		while (blkid_tag_next(iter, &type, &value) == 0)
			blkid_set_tag(dev, type, 0, 0);
		blkid_tag_iterate_end(iter);
		goto try_again;
	}

	if (!dev->bid_type) {
		blkid_free_dev(dev);
		dev = 0;
		goto found_type;
	}
		
found_type:
	if (dev && type) {
		dev->bid_devno = st.st_rdev;
		dev->bid_time = time(0);
		dev->bid_flags |= BLKID_BID_FL_VERIFIED;
		cache->bic_flags |= BLKID_BIC_FL_CHANGED;

		blkid_set_tag(dev, "TYPE", type, 0);
				
		DBG(DEBUG_PROBE, printf("%s: devno 0x%04llx, type %s\n",
			   dev->bid_name, (long long)st.st_rdev, type));
	}

	if (probe.sbbuf)
		free(probe.sbbuf);
	if (probe.buf)
		free(probe.buf);
	if (probe.fd >= 0) 
		close(probe.fd);

	return dev;
}

int blkid_known_fstype(const char *fstype)
{
	struct blkid_magic *id;

	for (id = type_array; id->bim_type; id++) {
		if (strcmp(fstype, id->bim_type) == 0)
			return 1;
	}
	return 0;
}

#ifdef TEST_PROGRAM
int main(int argc, char **argv)
{
	blkid_dev dev;
	blkid_cache cache;
	int ret;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s device\n"
			"Probe a single device to determine type\n", argv[0]);
		exit(1);
	}
	if ((ret = blkid_get_cache(&cache, "/dev/null")) != 0) {
		fprintf(stderr, "%s: error creating cache (%d)\n",
			argv[0], ret);
		exit(1);
	}
	dev = blkid_get_dev(cache, argv[1], BLKID_DEV_NORMAL);
	if (!dev) {
		printf("%s: %s has an unsupported type\n", argv[0], argv[1]);
		return (1);
	}
	printf("TYPE='%s'\n", dev->bid_type ? dev->bid_type : "(null)");
	if (dev->bid_label)
		printf("LABEL='%s'\n", dev->bid_label);
	if (dev->bid_uuid)
		printf("UUID='%s'\n", dev->bid_uuid);
	
	blkid_free_dev(dev);
	return (0);
}
#endif
