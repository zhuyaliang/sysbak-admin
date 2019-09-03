#include <stdio.h>
#include <gio/gio.h>
#include <glib.h>
#include <fcntl.h>
#include <assert.h>
#include <io-generated.h>

#define ORG_NAME  "org.io.operation.gdbus"
#define DBS_NAME  "/org/io/operation/gdbus"

static GMainLoop* loop = NULL;
static gboolean gdbus_write_file(IoGdbus               *object,
                                 GDBusMethodInvocation *invocation,
                                 gint                   fd,
                                 const gchar           *data,
                                 guint                  size)
{   
    int len;
    g_return_val_if_fail (fd   >= 0,FALSE);
    g_return_val_if_fail (data != NULL,FALSE);

    len = write(fd,data,size);
    if(len <= 0 )
    {
        return FALSE;
    }    
    io_gdbus_complete_write_file (object,invocation,len);
    return TRUE;
}    
static gboolean gdbus_read_file (IoGdbus               *object,
                                 GDBusMethodInvocation *invocation,
                                 gint                   fd,
                                 guint                  size)
{   
    int  len;
    char *ReadBuf = NULL; 
    g_return_val_if_fail (fd >= 0,FALSE);
    g_return_val_if_fail (fd >= 0,FALSE);
    
	ReadBuf = (char*)malloc(size);
    len = read (fd,ReadBuf,size);
    if(len <= 0 )
    {
        return FALSE;
    }    
    io_gdbus_complete_read_file (object,invocation,len);
    g_print ("ReadBuf = %s\r\n",ReadBuf);
    io_gdbus_set_read_date (object,ReadBuf);
    free (ReadBuf);
    return TRUE;
}    
 
static gboolean gdbus_open_file (IoGdbus               *object,
                                 GDBusMethodInvocation *invocation,
                                 const gchar           *filename,
                                 guint                  flag,
                                 guint                  mode)
{   
    int fd;

    g_return_val_if_fail (filename != NULL,FALSE);
    
    fd = open (filename,flag,mode);
    if(fd <= 0 )
    {
        perror ("open ");
        return FALSE;
    }    
    io_gdbus_complete_open_file (object,invocation,fd);

    return TRUE;
}    
static void AcquiredCallback (GDBusConnection *Connection,
                              const gchar     *name,
                              gpointer         data)
{
    GError       *error = NULL;
    IoGdbus      *io_gdbus;
    IoGdbusIface *iface;

    io_gdbus = io_gdbus_skeleton_new ();
    iface = IO_GDBUS_GET_IFACE (io_gdbus);
    iface->handle_write_file = gdbus_write_file;
    iface->handle_read_file  = gdbus_read_file;
    iface->handle_open_file  = gdbus_open_file;
    if(!g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(io_gdbus), 
                                         Connection, 
                                         DBS_NAME, 
                                         &error))
    {
        if(error != NULL)
        {
            g_print("Error: Failed to export object. Reason: %s.\n", error->message);
            g_error_free(error);
        }    
        g_error("qiut !!!\r\n");
    }    
}
static void NameLostCallback (GDBusConnection *connection,
                              const gchar     *name,
                              gpointer         data)
{
    printf("Lost Lost !!!!\r\n");
    g_main_loop_quit (loop);
}

int main(int argc,char* argv[])
{
    guint  dbus_id;

    dbus_id = g_bus_own_name (G_BUS_TYPE_SYSTEM,
                              ORG_NAME,
                              G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT,
                              AcquiredCallback,
                              NULL,
                              NameLostCallback,
                              NULL,
                              NULL);
    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    g_bus_unown_name(dbus_id);
    return 0;
}
