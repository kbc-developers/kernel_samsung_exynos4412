#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include <asm/uaccess.h>    /* copy_from_user */

static int cmdline_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s\n", saved_command_line);
	return 0;
}

static int proc_write( struct file* filp, const char* buffer, unsigned long count, void* data )
{
	char str[1024];
	printk( KERN_INFO "[proc/cmdline.c]buffer:\ %s: \n", buffer);
	if ( copy_from_user( str, buffer, count ) ) {
	   printk( KERN_INFO "[proc/cmdline.c] : copy_from_user failed\n" );
	   return -EFAULT;
	}
	printk( KERN_INFO "[proc/cmdline.c]str; %s: \n", str);
	strcpy (saved_command_line, str);
	
	return count;
}

static int cmdline_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, cmdline_proc_show, NULL);
}

static const struct file_operations cmdline_proc_fops = {
	.open		= cmdline_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write 		= proc_write,
};

static int __init proc_cmdline_init(void)
{
	proc_create("cmdline", 0, NULL, &cmdline_proc_fops);
	return 0;
}
module_init(proc_cmdline_init);
