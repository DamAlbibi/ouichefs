// SPDX-License-Identifier: GPL-2.0
/*
 * ouiche_fs - a simple educational filesystem for Linux
 *
 * Copyright (C) 2018  Redha Gouicem <redha.gouicem@lip6.fr>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/fdtable.h>
#include <linux/buffer_head.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/ioctl.h>
#include <linux/uuid.h>
#include <linux/namei.h>

#include "ouichefs.h"
#include "ioctl_interface.h"

#define DEVICE_NAME "ouichefs"

static unsigned long sys_fun_addr = 0xffffffff811f9da0; /* sys_close_x86_64 */
static unsigned long *syscall_table_addr; /* adresse de la table syscall */

module_param(sys_fun_addr, long, 0660); /* pour mettre une autre adresse de syscall en param */

static u32 old_cr0, cr0;
int part_total;
struct dentry_kobj tab_d_kobj[MAXMOUNT];
struct kobject *ouichfs_part;



/**
 * force l'ecriture sur CR0, car la fonction du noyau fait une verification
 * du bit WP oblige de passer par l'assembleur pour bypass cette verification
 */
static inline void bypass_write_cr0(unsigned long new_CR0)
{
	/* repris du code de la fonction originelle */
	asm volatile("mov %0,%%cr0" : "+r" (new_CR0) : : "memory");
}


/* retrouver l'adresse de base de la table des syscall */
static void find_sc_table_addr(void)
{
	unsigned long i = PAGE_OFFSET;
	unsigned long **addr;

	while (i < ULONG_MAX) { /* on parcours l'espace du kernel */
		addr = (unsigned long **)i;
		if ((unsigned long *)(addr[__NR_close]) == (unsigned long *)sys_fun_addr) {
			syscall_table_addr = (unsigned long *)addr;
			return;
		}
		i += sizeof(void *);
	}
}


static ssize_t ouichfs_part_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	struct super_block *sb;
	struct ouichefs_sb_info *sbi;
	struct inode *inode;
	__u8 tab[UUID_SIZE];
	unsigned long ino;
	int i;
	uint32_t nbr_inode;
	uint32_t nbr_inode_2_hard_link;

	for (i = 0; i < MAXMOUNT; ++i) {
		if (&tab_d_kobj[i].kobj_att == attr)
			break;
	}

	sb = tab_d_kobj[i].kobj_dentry->d_sb;
	sbi = OUICHEFS_SB(sb);
	nbr_inode = sbi->nr_inodes;
	nbr_inode_2_hard_link = 0;
	inode = NULL;

	for (ino = 0; ino < nbr_inode; ++ino) {
		inode = ouichefs_iget(sb, ino);
		if (inode->i_nlink >= 2 && !S_ISDIR(inode->i_mode))
			++nbr_inode_2_hard_link;
	}

	export_uuid(tab, &sb->s_uuid);
	return snprintf(buf, PAGE_SIZE, "UUID = %pUb\nNombre total inode = %d\
		\nNombre d'inode pointe par au moins 2 fichiers = %d\n",
		tab, nbr_inode, nbr_inode_2_hard_link);
}


/*
 * Mount a ouiche_fs partition
 */
struct dentry *ouichefs_mount(struct file_system_type *fs_type, int flags,
			      const char *dev_name, void *data)
{
	struct dentry *dentry = NULL;
	char *nombre;
	int ret;

	dentry = mount_bdev(fs_type, flags, dev_name, data,
			ouichefs_fill_super);

	if (IS_ERR(dentry))
		pr_err("'%s' mount failure\n", dev_name);
	else {
		nombre = kmalloc(GFP_KERNEL, 4*sizeof(char));
		if (nombre == NULL)
			return ERR_PTR(-ENOMEM);

		sprintf(nombre, "%d", part_total);
		nombre[4] = '\0';
		tab_d_kobj[part_total].kobj_dentry = dentry;
		tab_d_kobj[part_total].kobj_att.attr.name = nombre;
		tab_d_kobj[part_total].kobj_att.attr.mode = 0600;
		tab_d_kobj[part_total].kobj_att.show = ouichfs_part_show;
		tab_d_kobj[part_total].kobj_att.store = NULL;

		ret = sysfs_create_file(ouichfs_part, &(tab_d_kobj[part_total].kobj_att.attr));
		if (ret < 0)
			pr_info("fail to create sysfs\n");
		pr_info("'%s' mount success\n", dev_name);
		++part_total;
	}

	return dentry;
}


/*
 * Unmount a ouiche_fs partition
 */
void ouichefs_kill_sb(struct super_block *sb)
{
	int i;

	// Supression du sysFs
	for (i = 0; i < MAXMOUNT; ++i) {
		if (tab_d_kobj[i].kobj_dentry->d_sb == sb)
			break;
	}
	if (i != MAXMOUNT) {
		sysfs_remove_file(ouichfs_part, &(tab_d_kobj[i].kobj_att.attr));
		--part_total;
	}

	kill_block_super(sb);
	pr_info("unmounted disk\n");
}


static struct file_system_type ouichefs_file_system_type = {
	.owner = THIS_MODULE,
	.name = "ouichefs",
	.mount = ouichefs_mount,
	.kill_sb = ouichefs_kill_sb,
	.fs_flags = FS_REQUIRES_DEV,
	.next = NULL,
};


// Définition de L'ioctl
static long ouichefs_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct super_block *sb;
	struct ouichefs_sb_info *sbi;
	struct ouichefs_inode_info *ci_dir = NULL;
	struct ouichefs_dir_block *dblock = NULL;
	struct inode *inode = NULL;
	struct inode *inode_parcours = NULL;
	struct buffer_head *bh = NULL;
	struct dentry *dentry = NULL;
	struct files_struct *files;
	struct file *file = NULL;
	struct fdtable *fdt;
	unsigned long  ino;
	unsigned long ino_p;
	int lock_result;
	int ret = 0;
	int fd;
	int i;
	uint32_t nbr_inode;

	if (_IOC_TYPE(cmd) != MAGIC_IOCTL)
		return -EINVAL;

	switch (cmd)  {
	case OUICHEFSG_HARD_LIST:
		fd = *((int *)arg); // récuperation du fd
		files = current->files;
		if (files) {
			// Can I run lockless? What files_fdtable() does?
			lock_result = spin_trylock(&files->file_lock);
			if (lock_result) {
				// fdt is never NULL
				fdt = files_fdtable(files);
				if (fdt) {
					//This can not be wrong in syscall open
					if (fd < fdt->max_fds)
						file = fdt->fd[fd];
				}
				spin_unlock(&files->file_lock);
			}
		}
		if (file) {
			inode = file->f_inode; // on récupère l'inode
			dentry = file->f_path.dentry; // On récupère la dentry
		}

		ino = inode->i_ino;
		// On récupère le sb et on itère sur toute les inodes
		sb = dentry->d_sb;
		sbi = OUICHEFS_SB(sb);
		nbr_inode = sbi->nr_inodes;

		for (ino_p = 0; ino_p < nbr_inode; ++ino_p) {
			inode_parcours = ouichefs_iget(sb, ino_p);
			if (S_ISDIR(inode_parcours->i_mode)) {
				// Parcourir tous les fichiers
				ci_dir = OUICHEFS_INODE(inode_parcours);
				bh = sb_bread(sb, ci_dir->index_block);
				if (!bh)
					return -EIO;

				dblock = (struct ouichefs_dir_block *)bh->b_data;

				for (i = 0; i < OUICHEFS_MAX_SUBFILES; ++i) {
					if (dblock->files[i].inode == ino)
						pr_info("%s\n",
							dblock->files[i].filename);
				}
			}
		}
		break;
	default:
		return -ENOTTY;
	}
	return ret;
}


static struct file_operations fops = {
	.unlocked_ioctl = ouichefs_ioctl
};
int major_allocated;


struct dentry *get_dentry_from_path(char *pathname)
{
	struct dentry *dentry;
	struct path path;
	int error;

	error = kern_path(pathname, LOOKUP_FOLLOW, &path);
	if (error)
		return ERR_PTR(error);

	dentry = path.dentry;
	return dentry;
}


/**
 * syscall
 */
asmlinkage long unlinkall(const struct pt_regs *regs)
{
	struct super_block *sb;
	struct ouichefs_sb_info *sbi;
	struct inode *inode;
	struct ouichefs_inode_info *ci_dir = NULL;
	struct inode *inode_parcours = NULL;
	struct ouichefs_dir_block *dblock = NULL;
	struct dentry *dentry;
	struct buffer_head *bh = NULL;
	char __user *path;
	char *kernel_copy;
	unsigned long ino_p;
	int i, ret;
	int len_buff;
	uint32_t ino, bno;
	uint32_t nbr_inode;

	pr_warn("In syscall unlinkall\n");

	path = (char *)regs->di;
	len_buff = strlen(path)+1;
	kernel_copy = kmalloc(len_buff*sizeof(char), GFP_ATOMIC);

	ret = copy_from_user(kernel_copy, path, len_buff);
	if (ret)
		return -EINVAL;

	// Début de l'algorithme !
	dentry = get_dentry_from_path(kernel_copy);
	inode = dentry->d_inode; //Récuperation de l'inode du fichier
	// On récupère le numero de l'inode !
	// Récuperation de la dentry du parent

	ino = inode->i_ino;
	bno = OUICHEFS_INODE(inode)->index_block;
	sb = dentry->d_sb;
	sbi = OUICHEFS_SB(sb);
	nbr_inode = sbi->nr_inodes;

	for (ino_p = 0; ino_p < nbr_inode; ++ino_p) {
		inode_parcours = ouichefs_iget(sb, ino_p);
		if (S_ISDIR(inode_parcours->i_mode)) {
			ci_dir = OUICHEFS_INODE(inode_parcours);
			bh = sb_bread(sb, ci_dir->index_block);

			if (!bh)
				return -EIO;

			dblock = (struct ouichefs_dir_block *)bh->b_data;
			for (i = 0; i < OUICHEFS_MAX_SUBFILES; ++i) {
				if (dblock->files[i].inode == ino) {
				/**
				 *  do_unlinkat n'est pas exporte et on a compris
				 * qu'il fallait l'implementer en local mais c'etait trop tard
				 * niveau temps...
				 */
				//do_unlinkat(AT_FDCWD, getname(dblock->files[i].filename));
				//ouichefs_unlink(inode_parcours, dentry);
				}
			}
		}
		//iput(inode_parcours);
		
	}
	//dput(dentry);

	return 0;
}


static int __init ouichefs_init(void)
{
	int ret;

	part_total = 0;
	ret = ouichefs_init_inode_cache();
	if (ret) {
		pr_err("inode cache creation failed\n");
		goto end;
	}

	ret = register_filesystem(&ouichefs_file_system_type);
	if (ret) {
		pr_err("register_filesystem() failed\n");
		goto end;
	}

	// Creation du kernel object
	ouichfs_part = kobject_create_and_add("ouichfs_part", kernel_kobj);
	if (!ouichfs_part)
		pr_err("kobject_create_and_add failed\n");

	// Ajout de l'ioctl
	major_allocated = register_chrdev(0, DEVICE_NAME, &fops);
	pr_info("Le major %d\n", major_allocated);
	if (ret < 0)
		return -ENOMEM;

	/* insertion du syscall */
	find_sc_table_addr(); /* chercher l'adresse de la table */

	/**
	 * 16eme bit, pour retirer la protection du registre sur des zones memoire
	 * protegees
	 */
	old_cr0 = native_read_cr0();
	cr0 = old_cr0;
	cr0 &= ~CR0_MASK;
	bypass_write_cr0(cr0);

	syscall_table_addr[SYSCALL_OFFSET_INSERT] = (unsigned long *)unlinkall;
	bypass_write_cr0(old_cr0); /* restaurer la protection */

	pr_info("module loaded\n");
end:
	return ret;
}

static void __exit ouichefs_exit(void)
{
	int ret;

	ret = unregister_filesystem(&ouichefs_file_system_type);
	if (ret)
		pr_err("unregister_filesystem() failed\n");

	ouichefs_destroy_inode_cache();

	kobject_put(ouichfs_part);

	unregister_chrdev(major_allocated, DEVICE_NAME);

	pr_info("module unloaded\n");
}

module_init(ouichefs_init);
module_exit(ouichefs_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Redha Gouicem, <redha.gouicem@lip6.fr>");
MODULE_DESCRIPTION("ouichefs, a simple educational filesystem for Linux");