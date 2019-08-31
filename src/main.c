#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "sysbak-extfs.h"

static void test (GObject      *source_object,
           GAsyncResult *res,
           gpointer      user_data)
{
    file_system_info *fs_info;
	GError *error = NULL;

	fs_info = sysbak_extfs_ptf_finish (res,&error);
	if (fs_info == NULL)
	{
		g_print ("NULLLLLLLLLLL %s\r\n",error->message);
	}
	else
	{
		g_print ("fs_info->totalblock = %llu",fs_info->totalblock);
	}
}    

void progress (progress_data *data,gpointer p_data)
{
    printf ("p ============%f s================%f\r\n",data->percent,data->speed);
}    
int main(int argc, char **argv)
{
	const char *a = "abcd";
    const char *b = "qwer";
	GCancellable *cancellable;
	GMainLoop *loop;

	cancellable = g_cancellable_new ();
	loop = g_main_loop_new (NULL, FALSE);

	if (!sysbak_extfs_ptp_async ("/dev/sdc1","/dev/sdc2",0,cancellable,test,(gpointer)a,progress,(gpointer)b))
	{
		printf ("faild\r\n");
	} 
/*	
	if (!sysbak_extfs_ptf_async ("/dev/sdc1","sdc.img",0,cancellable,test,(gpointer)a,progress,(gpointer)b))
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
