#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "sysbak-extfs.h"

static void test (GObject      *source_object,
           GAsyncResult *res,
           gpointer      user_data)
{
    device_info *dev_info;
	GError *error = NULL;

	dev_info = sysbak_extfs_finish (res,&error);
	if (dev_info == NULL)
	{
		g_print ("NULLLLLLLLLLL %s\r\n",error->message);
	}
	else
	{
		g_print ("dev_info->fs = %s\r\n",dev_info->fs);
	}
}    

void progress (progress_data *data,gpointer p_data)
{
    printf ("Percentile ============%.2f speed ================%.2f KB/S\r\n",data->percent,data->speed);
}    
int main(int argc, char **argv)
{
	const char *a = "abcd";
    const char *b = "qwer";
	GCancellable *cancellable;
	GMainLoop *loop;

	cancellable = g_cancellable_new ();
	loop = g_main_loop_new (NULL, FALSE);
/*
	if (!sysbak_extfs_ptp_async ("/dev/sdc1","/dev/sdc2",0,cancellable,test,(gpointer)a,progress,(gpointer)b))
	{
		printf ("faild\r\n");
	} 
*/
	if (!sysbak_extfs_ptf_async ("/dev/sdb1","sdb1.img",1,cancellable,test,(gpointer)a,progress,(gpointer)b))
	{
		printf ("faild\r\n");
	}    
/*
	if (!sysbak_extfs_restore_async ("sdb1.img","/dev/sdb1",1,cancellable,test,(gpointer)a,progress,(gpointer)b))
	{
		printf ("faild\r\n");
	} 
*/    
	g_main_loop_run (loop);
	g_object_unref (cancellable);
	g_main_loop_unref (loop);
	sleep (10);
	g_main_loop_quit (loop);
}    
