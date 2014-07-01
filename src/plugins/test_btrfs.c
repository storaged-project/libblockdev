#include <stdio.h>
#include "btrfs.c"

int main (void) {
    gboolean ret = FALSE;

    ret = path_is_mountpoint ("/home");
    if (ret)
        puts ("/home is a mountpoint");
    else
        puts ("/home is not a mountpoint");

    /* trailing slash shouldn't be a problem */
    ret = path_is_mountpoint ("/home/");
    if (ret)
        puts ("/home/ is a mountpoint");
    else
        puts ("/home/ is not a mountpoint");

    ret = path_is_mountpoint ("/etc");
    if (ret)
        puts ("/etc is a mountpoint");
    else
        puts ("/etc is not a mountpoint");

    /* /mnt is (probably) not a mountpoint, but it is a prefix of many
       mountpoints */
    ret = path_is_mountpoint ("/mnt");
    if (ret)
        puts ("/mnt is a mountpoint");
    else
        puts ("/mnt is not a mountpoint");
    ret = path_is_mountpoint ("/mnt/");
    if (ret)
        puts ("/mnt/ is a mountpoint");
    else
        puts ("/mnt/ is not a mountpoint");

    return 0;
}
