/*
 * Audacious Bluetooth headset suport plugin
 *
 * Copyright (c) 2008 Paula Stanciu paula.stanciu@gmail.com
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; under version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses>.
 */

#include "bluetooth.h"
#include "marshal.h"
#include "gui.h"
#include "scan_gui.h"
#include "agent.h"
#define DEBUG 1
static gboolean plugin_active = FALSE,exiting=FALSE;
GList * current_device = NULL;
gint config = 0;
gint devices_no = 0;
GStaticMutex mutex = G_STATIC_MUTEX_INIT;
static GThread *connect_th;
void bluetooth_init ( void );
void bluetooth_cleanup ( void );
void bt_cfg(void);
void bt_about(void);
static void remote_device_found(DBusGProxy *object, char *address, const unsigned int class, const int rssi, gpointer user_data);
static void discovery_started(DBusGProxy *object, gpointer user_data);
static void remote_name_updated(DBusGProxy *object, const char *address,  char *name, gpointer user_data);
static void print_results(void);
static void discovery_completed(DBusGProxy *object, gpointer user_data);
void discover_devices(void);
void disconnect_dbus_signals(void);


GeneralPlugin bluetooth_gp =
{
    .description = "Bluetooth audio support",
    .init = bluetooth_init,
    .about = bt_about,
    .configure = bt_cfg,
    .cleanup = bluetooth_cleanup
};
GeneralPlugin *bluetooth_gplist[] = { &bluetooth_gp, NULL };
DECLARE_PLUGIN(bluetooth_gp, NULL, NULL, NULL, NULL, NULL, bluetooth_gplist, NULL, NULL)

void bluetooth_init ( void )
{
    audio_devices = NULL;
    bus = NULL;
    obj = NULL;
    discover_devices();
}

void bluetooth_cleanup ( void )
{
    printf("bluetooth: exit\n");
    if (config ==1 ){
        close_window();
        config =0;
    }
    if(discover_finish == 2) {
        dbus_g_connection_flush (bus);
        dbus_g_connection_unref(bus);
        disconnect_dbus_signals();

    }
}

void bt_about( void )
{
    printf("about call\n");
}

void bt_cfg(void)
{   
    printf("bt_cfg\n");
    config =1;
    if(discover_finish == 2){
        if (devices_no == 0){
            printf("no devs!\n");
            show_scan();
            show_no_devices();
        }else 
            results_ui();
    }
    else show_scan();    
    printf("end of bt_cfg\n");
}

void disconnect_dbus_signals()
{
    dbus_g_proxy_disconnect_signal(obj, "RemoteDeviceFound", G_CALLBACK(remote_device_found), bus);
    dbus_g_proxy_disconnect_signal(obj, "DiscoveryStarted", G_CALLBACK(discovery_started), bus);
    dbus_g_proxy_disconnect_signal(obj, "DiscoveryCompleted", G_CALLBACK(discovery_completed), bus);
    dbus_g_proxy_disconnect_signal(obj, "RemoteNameUpdated", G_CALLBACK(remote_name_updated), NULL);

}

void clean_devices_list(){
    g_list_free(audio_devices);
    dbus_g_connection_flush (bus);
    dbus_g_connection_unref(bus);
    audio_devices = NULL;
    //g_list_free(current_device);
}

void refresh_call(void)
{
    printf("refresh function called\n");
    disconnect_dbus_signals();
    clean_devices_list();
    if(discover_finish == 0 ||discover_finish== 2){
        discover_finish = 0;

        discover_devices();
        close_window();
        show_scan();
    }
    else 
        printf("Scanning please wait!\n");
}

void bounding_created(gchar* address)
{
    printf("Signal BoundingCreated : %s\n",address);

}

void bounding_removed(gchar* address)
{
    printf("Signal: BoundingRemoved: %s\n",address);

}


gpointer connect_call_th(void)
{

    //I will have to enable the audio service if necessary 

    dbus_g_object_register_marshaller(marshal_VOID__STRING_UINT_INT, G_TYPE_NONE, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INT, G_TYPE_INVALID);
    dbus_g_proxy_add_signal(obj, "BondingCreated", G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INT, G_TYPE_INVALID);
    dbus_g_proxy_connect_signal(obj, "BondingCreated", G_CALLBACK(bounding_created), bus, NULL);

    dbus_g_proxy_add_signal(obj, "BondingRemoved", G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INT, G_TYPE_INVALID);
    dbus_g_proxy_connect_signal(obj, "BondingRemoved", G_CALLBACK(bounding_removed), bus, NULL);
    run_agents();
    dbus_g_proxy_call(obj,"CreateBonding",NULL,G_TYPE_STRING,((DeviceData*)(selected_dev->data))->address,G_TYPE_INVALID,G_TYPE_INVALID); 
     
}
void connect_call(void)
{
 connect_th = g_thread_create((GThreadFunc)connect_call_th,NULL,TRUE,NULL) ;  
}


static void remote_device_found(DBusGProxy *object, char *address, const unsigned int class, const int rssi, gpointer user_data)
{
    int found_in_list=FALSE; 
    g_static_mutex_lock(&mutex);
    current_device = audio_devices;
    if((class & 0x200404)==0x200404)
    {
        while(current_device != NULL)
        {
            if(g_str_equal(address,((DeviceData*)(current_device->data))->address)) 
            {
                found_in_list = TRUE;
                break;
            }
            current_device=g_list_next(current_device);
        }
        if(!found_in_list) 
        {        
            DeviceData *dev= g_new0(DeviceData, 1);
            dev->class = class;
            dev->address = g_strdup(address);
            dev->name = NULL;
            audio_devices=g_list_prepend(audio_devices, dev);
        }
    }
    g_static_mutex_unlock(&mutex);
}

static void discovery_started(DBusGProxy *object, gpointer user_data)
{
    g_print("Signal: DiscoveryStarted()\n");
    discover_finish = 1;
}

static void remote_name_updated(DBusGProxy *object, const char *address,  char *name, gpointer user_data)
{
    g_static_mutex_lock(&mutex);
    current_device=audio_devices;
    while(current_device != NULL)
    {
        if(g_str_equal(address,((DeviceData*)(current_device->data))->address)) 
        {
            ((DeviceData*)(current_device->data))->name=g_strdup(name);
            break;
        }
        current_device=g_list_next(current_device);
    }
    g_static_mutex_unlock(&mutex);
}

static void print_results()
{
    int i=0;
    g_print("Final Scan results:\n");
    devices_no = g_list_length(audio_devices);
    g_print("Number of audio devices: %d \n",devices_no);
    if(devices_no==0 ) {
        if(config ==1) show_no_devices();        
    } else {
        current_device=audio_devices;
        while(current_device != NULL)
        {
            g_print("Device %d: Name: %s, Class: 0x%x, Address: %s\n",++i,
                    ((DeviceData*)(current_device->data))-> name,
                    ((DeviceData*)(current_device->data))-> class,
                    ((DeviceData*)(current_device->data))-> address);
            current_device=g_list_next(current_device);
        }
        destroy_scan_window();
        if(config==1) {
            destroy_scan_window();
            results_ui();
        }
        //   refresh_tree();
    }
}



static void discovery_completed(DBusGProxy *object, gpointer user_data)
{
    g_print("Signal: DiscoveryCompleted()\n");
    discover_finish =2;
    print_results();
}



void discover_devices(void)
{
    GError *error = NULL;
    //  g_type_init();
    g_log_set_always_fatal (G_LOG_LEVEL_WARNING);
    bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, &error);
    if (error != NULL)
    {
        g_printerr("Connecting to system bus failed: %s\n", error->message);
        g_error_free(error);
    }
    obj = dbus_g_proxy_new_for_name(bus, "org.bluez", "/org/bluez/hci0", "org.bluez.Adapter");
    printf("bluetooth plugin - start discovery \n");
    dbus_g_object_register_marshaller(marshal_VOID__STRING_UINT_INT, G_TYPE_NONE, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INT, G_TYPE_INVALID);

    dbus_g_proxy_add_signal(obj, "RemoteDeviceFound", G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INT, G_TYPE_INVALID);
    dbus_g_proxy_connect_signal(obj, "RemoteDeviceFound", G_CALLBACK(remote_device_found), bus, NULL);

    dbus_g_proxy_add_signal(obj, "DiscoveryStarted", G_TYPE_INVALID);
    dbus_g_proxy_connect_signal(obj, "DiscoveryStarted", G_CALLBACK(discovery_started), bus, NULL);

    dbus_g_proxy_add_signal(obj, "DiscoveryCompleted", G_TYPE_INVALID);
    dbus_g_proxy_connect_signal(obj, "DiscoveryCompleted", G_CALLBACK(discovery_completed), bus, NULL);

    dbus_g_object_register_marshaller(marshal_VOID__STRING_STRING, G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);

    dbus_g_proxy_add_signal(obj, "RemoteNameUpdated", G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
    dbus_g_proxy_connect_signal(obj, "RemoteNameUpdated", G_CALLBACK(remote_name_updated), NULL, NULL);

    dbus_g_proxy_call(obj, "DiscoverDevices", &error, G_TYPE_INVALID, G_TYPE_INVALID);
    if (error != NULL)
    {
        g_printerr("Failed to discover devices: %s\n", error->message);
        g_error_free(error);
    }

}   
