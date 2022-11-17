#include <linux/init.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/slab.h>


// Meta Information
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Melis");
MODULE_DESCRIPTION("Welcome to psvis module for comp304 project 1");

int pid;
module_param(pid, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(pid, "PID of the process");

void traverse_proc_tree(struct task_struct* ts, int depth){
   struct list_head *list;
   struct task_struct *child;
   
   int i;
   for(i = 0; i < depth; i++){
	printk("---");	
	if (i == depth -1 ){
	printk(">");	
	}
   }
   
   
   printk("%d - PID: %d (start time: %lld)\n", depth, ts->pid, ts->start_time);
  
   list_for_each(list, &ts->children){
         child = list_entry(list, struct task_struct, sibling);
         traverse_proc_tree(child, depth+1);
   }
   
}

int psvis_init(void){
  
   struct task_struct *ts;
   ts = get_pid_task(find_get_pid(pid), PIDTYPE_PID);
  
   if (ts != NULL){
  	traverse_proc_tree(ts, 0);
   }else{
  	printk("Given PID: %d doesnt exist\n", pid);
  	return 1;
   }
   return 0;
}

void psvis_exit(void){
   printk("Leaving psvis...\n");
}



module_init(psvis_init);
module_exit(psvis_exit);
