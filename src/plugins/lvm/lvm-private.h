#ifndef BD_LVM_PRIVATE
#define BD_LVM_PRIVATE

#define SECTOR_SIZE 512
#define DEFAULT_PE_SIZE (4 MiB)
#define USE_DEFAULT_PE_SIZE 0
#define RESOLVE_PE_SIZE(size) ((size) == USE_DEFAULT_PE_SIZE ? DEFAULT_PE_SIZE : (size))

#define LVM_MIN_VERSION "2.02.116"
#define LVM_VERSION_FSRESIZE "2.03.19"

extern GMutex global_config_lock;
extern gchar *global_config_str;

extern gchar *global_devices_str;

#endif /* BD_LVM_PRIVATE */
