#include<linux/init.h>
#include<linux/kernel.h>
#include<linux/module.h>
#include<asm/segment.h>
#include<asm/uaccess.h>
#include<linux/fs.h>
#include<linux/buffer_head.h>
#include<linux/syscalls.h>
#include<linux/fcntl.h>
#include<linux/kthread.h>
#include<linux/delay.h>
#include<linux/buffer_head.h>
#include<linux/interrupt.h>
#include<linux/irq.h>
#include<asm/io.h>
#include<asm/unistd.h>

#define ONLINE "on"
#define OFFLINE "off"
#define ACPI_AC_PATH "/proc/acpi/ac_adapter/AC/state"
#define PASSWD_LEN 255

MODULE_LICENSE("GPL");

int password[PASSWD_LEN], input_tmp[PASSWD_LEN], input_flag=1;
struct tast_struct *ts;
extern int js_alert_flag;
extern int js_unlock_flag;
//extern int js_input_flag;

//void inputPwd(void);
int stat_handler(void *data);
int getStat(void);

struct file* file_open(const char* path, int flags, int rights) {
	struct file* filp = NULL;
	mm_segment_t oldfs;
	int err = 0;

	oldfs = get_fs();
	set_fs(get_ds());
	filp = filp_open(path, flags, rights);
	set_fs(oldfs);
	if(IS_ERR(filp)) {
		err = PTR_ERR(filp);
		return NULL;
	}
	return filp;
}

void file_close(struct file* file) {
	filp_close(file, NULL);
}

int file_read(struct file* file, unsigned long long offset, unsigned char* data, unsigned int size) {
	mm_segment_t oldfs;
	int ret;

	oldfs = get_fs();
	set_fs(get_ds());

	ret = vfs_read(file, data, size, &offset);

	set_fs(oldfs);
	return ret;
}

int stat_handler(void *data) {
	int tmp = getStat(), result;
	mm_segment_t old_fs;
	
	if(tmp==-1) {
		return -1;
	}
	
	while(1){
		tmp = getStat();
		if(tmp==-1){
			printk("\n[ERROR] : ACPI Module was OFF.\n");
		}else if(tmp==0){
			printk("\n[SYSTEM] : AC_ADAPTER UnPluged. \n");
			if(js_unlock_flag==2){
				printk(KERN_ALERT "\nInput unlock password : ");
				js_unlock_flag=0;
				input_flag=1;
			}
			else if(js_alert_flag==1 && js_unlock_flag==1 && input_flag==0){
				js_alert_flag=0;
				js_unlock_flag=0;
			}
		}else {
			printk("\n[SYSTEM] : AC_ADAPTER Pluged. \n");
			if(js_alert_flag==1 && js_unlock_flag==0 && input_flag == 1){
			}else{
				js_alert_flag=1;
				js_unlock_flag=1;
			}
		}
		if(kthread_should_stop())
			break;
		ssleep(1);
	}

	return 0;
}

irqreturn_t irq_handler(int irq, void *dev_id, struct pt_regs *regs){
	static unsigned char scancode;
	unsigned char pressed;
	static int count=0;
	int i;
	scancode = inb(0x60);

	if(scancode == 0x15){
		input_flag=1;
	}

	//printk("alert : %d\n", js_alert_flag);
	//printk("unlock : %d\n", js_unlock_flag);
	//printk("input : %d\n", input_flag);

	if(js_alert_flag==1 && js_unlock_flag==0 && input_flag==1){
		
		if((scancode & 128) == 128){
			//do Nothing
		}
		else{
			if(scancode == 28){
				if(count != 0) {
					printk(KERN_ALERT "\n[SYSTEM] Password Saved!\n");
					js_unlock_flag=1;
					input_flag=0;
					count=0;
				}
			}
			else{
				password[count++] = scancode;
			}
		}
	}else if(js_alert_flag==0 && js_unlock_flag==0 && input_flag==1){
		if((scancode & 128) == 128){
			//do Nothing
		}
		else {
			if(scancode == 28){
				if(count != 0){
					js_unlock_flag=1;
					input_flag=0;
					count=0;
					for(i=0; i<PASSWD_LEN; i++){
						if(password[i] != input_tmp[i]){
							js_unlock_flag=0;
						}
					}
					if(js_unlock_flag==1){
						printk(KERN_ALERT "\n[SYSTEM] Unlock Success!\n");
					}else if(js_unlock_flag==0){
						printk(KERN_ALERT "\n[SYSTEM] Password Dismatch!\n");
					}
					for(i=0; i<PASSWD_LEN; i++) input_tmp[i] = 0;
				}
			}
			else{
				input_tmp[count++] = scancode;
			}
		}
	}
	return (irqreturn_t) IRQ_HANDLED;
}

int __init module_start(void) {
	int i, result;
	char buf[10];

	printk(KERN_ALERT "Module Loaded..\n");
	
	//Set-Up Flag(Input Lock Pwd Phase)
	js_alert_flag = 1;
	js_unlock_flag = 0;
	input_flag = 1;
	printk(KERN_ALERT "\ninput lock password : \n");

	for(i=0; i<PASSWD_LEN; i++) password[i] = 0;
	for(i=0; i<PASSWD_LEN; i++) input_tmp[i] = 0;

	if(input_flag == 1){
		result = request_irq(1, (irq_handler_t) irq_handler, IRQF_SHARED, 
		"keyboard_stats_irq", (void*)irq_handler);
		if(result) printk(KERN_INFO "can't get shared irt\n");
	}
	ts = (struct task_struct *)kthread_run(stat_handler, NULL, "js_alert");

	printk(KERN_ALERT "\n");

	return 0;
}

void __exit module_end(void) {
	printk(KERN_ALERT "Module Ended..\n");
	js_alert_flag=1;
	js_unlock_flag=1;
	input_flag=0;
	free_irq(1, (void*)(irq_handler));
	kthread_stop(ts);
	ts = NULL;
}

int getStat(void) {
	struct file* fd;
	char state[64];

	//AC_ADAPTER CHECK
	fd = file_open(ACPI_AC_PATH, O_RDONLY, 0);
	if(fd >= 0) {
		file_read(fd, 0, state, 64);
	}
	else {
		printk("\n[ERROR] : ACPI Moudle was OFF.\n");
		return -1;
	}
	file_close(fd);
	if(strstr(state,ONLINE)) {
		//printk("\n[SYSTEM] : AC_ADAPTER Pluged.\n");
		return 1;
	}
	else if(strstr(state,OFFLINE)) {
		//printk("\n[SYSTEM] : AC_ADAPTER Unpluged.\n");
		return 0;
	}
}

module_init(module_start);
modle_exit(module_end);
