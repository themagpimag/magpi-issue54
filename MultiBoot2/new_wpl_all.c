/*
 * (C) Copyright 2000
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 *
 * SPDX-License-Identifier: GPL-2.0+
 *
 * Winfried Plappert, August 2016 - January 2017
 */

#include <common.h>
#include <config.h>
#include <command.h>
#include <fs.h>
#include <ext4fs.h>
#include <vsprintf.h>
#include <ext_common.h>
#include <linux/string.h>
#include <os.h>
#define SUPERBLOCK_SIZE 1024

typedef struct {
      char display_string[16];
      char boot_partition[12];
} os_liste;

/* local forward declarations */
int  wpl_cmd (cmd_tbl_t *, int, int, char *const []);
int  gather_partition_info(os_liste *);
int  display_menu(int, os_liste *);
int  sleepy_check(void);
int  check_for_filename(char *, char *);
void check_usb_storage(void);
int  master_sleep(void);


/* define a new command */
U_BOOT_CMD(wpl_prepare, 1, 0, wpl_cmd, "prepare for boot from /dev/sd<n>",
                                       "hard disk boot menu");

int wpl_cmd (cmd_tbl_t *cmdtp, int flag, int argc, char *const argv[])
{
  /****************************************************************************
   * insert a new command wpl_prepare into the subdirectory cmd/
   * the command does the following:
   *  1.) checks for the existence of a usb device -- this is not fool proof
   *      since one can insert a usb memory stick which satisfies the
   *      "usb storage" condition.
   *  2.) read partition table entries and check for ext4 file systems
   *  3.) clears the screen
   *  4.) it generates a list of available operating systems from the
   *      partition list of the attached usb disk and reading "volume_name"
   *      from the superblock of the ext4 filesystem
   *  5.) loads fdt file
   *  6.) loads kernel
   *  7.) generates appropriate bootargs environment variable
   *  8.) WARNING: hdmi buffer frame is fixed at 1920x1080 - the default inside
   *      the kernel boot code is strangely set at 800x480.
   *      Needs to be set here since I have no idea how to interpret
   *      the raspberry pi2 device tree dynamically.
   *  9.) check for the existence of file /etc/fstab.
   * 10.) boots the kernel.
   ****************************************************************************/
  int selection_number = 0, rc, len_os_liste = 0;
  os_liste os_list[16];
  char buffer[1000], *usb_eth_addr = getenv("usbethaddr");
  char *boot_partition;
  char *default_kernel = "kernel7.img";
  /* rpi-2 specific! */
  char *default_fdt_file = "bcm2709-rpi-2-b.dtb";

  /* step 1: check for existence usb disk */
  check_usb_storage();
  /* step 2: read partition table and
   * access the volume label entry in the superblock
   * return number of valid ext4 file systems */
  len_os_liste = gather_partition_info(os_list);
  /* step 3: cls */
  run_command("cls", 0);
  /* step 4: generate menu from "os_list", return selected menu entry number */
  selection_number = display_menu(len_os_liste, os_list);
  /* step 5: load device tree */
  sprintf(buffer, "fatload mmc 0:1 ${fdt_addr_r} %s", default_fdt_file);
  rc = run_command(buffer, 0);
  if (rc != 0) {
    printf("Could not load %s, rc=%d\n", default_fdt_file, rc);
    return -1;
  }
  /* step 6: load kernel */
  sprintf(buffer, "fatload mmc 0:1 ${kernel_addr_r} %s", default_kernel);
  rc = run_command(buffer, 0);
  if (rc != 0) {
    printf("Could not load kernel %s rc=%d\n", default_kernel, rc);
    return -1;
  }
  /* step 7: prepare bootargs */
  boot_partition = os_list[selection_number - 1].boot_partition;
  sprintf (buffer, "bcm2708_fb.fbwidth=1920 bcm2708_fb.fbheight=1080 bcm2708_fb.fbdepth=32 bcm2708_fb.fbswap=1 dwc_otg.lpm_enable=0 earlyprintk console=tty1 console=ttyAMA0,115200 rootfstype=ext4 elevator=deadline rootwait rootdelay=5 noinitrd root=%s smsc95xx.macaddr=%s",
    boot_partition, usb_eth_addr);
  setenv ("bootargs", buffer);
  printf ("setenv bootargs %s\n", buffer);
  mdelay(2000);
  /* step 9: check for existence of /etc/fstab */
  rc = check_for_filename("/etc/fstab", boot_partition);
  if (rc != 1) {
    printf("File /etc/fstab does not exist in partition %s\n", boot_partition);
    return -1;
  }
  /* step 10: boot the loaded kernel */
  rc = run_command("bootz ${kernel_addr_r} - ${fdt_addr_r}", 0);
  if (rc != 0) {
    printf("Could not boot kernel, rc=%d\n", rc);
    return -1;
  }
  return 0;
}

int display_menu(int len_os_liste, os_liste *os_list) {
  /****************************************************************************
   * display os_list and wait for input
   * after 20 seconds choose first entry
   ****************************************************************************/
  int rc = 0, i, selection_number = 0;
  unsigned char input[4];

  while (1) {
    printf("Select an Operating System to boot ...\n\n");
    for(i = 0; i < len_os_liste; i++) {
      printf("%15x %s\n", i + 1, os_list[i].display_string);
    }
    printf("Enter selection number ... \n\n");

    /* check for keyboard input every 0.1 seconds,
     * in total wait a maximum of 20 seconds */
    rc = master_sleep();

    /* if nothing has happened after 20 seconds, boot first selectable
     * partition */
    input[0] = (rc == 0) ? '1' : rc;
    input[1] = 0;
    selection_number = (int)simple_strtol((const char *)input, NULL, 16);

    /* check boundaries */
    if (1 <= selection_number && selection_number <= len_os_liste) {
      break;
    }
    else {
      printf ("Invalid selection, please try again ...\n");
    }
  }
  return selection_number;
}

void check_usb_storage(void) {
  /****************************************************************************
   * check for usb storage device being available:
   * loop until usb device has been found
   * sleep in between
   ****************************************************************************/
  int rc = 1, retry_count = 10;
  while (rc) {
    rc = run_command("usb storage", 0);
    if (rc != 0) {
      mdelay(2000);
      run_command("usb reset", 0);
      printf("\n\n");
      retry_count--;
      if (retry_count == 0) {
        printf("Could not find a USB disk after 20 seconds ... arborting!\n\n");
        break;
      }
    }
  }
}

int gather_partition_info(os_liste *os_list) {
  /****************************************************************************
   * read up to 15 partition table entries, msdos partitioning assumed.
   * Check for ext4 file system and read volume_name for this partition.
   *
   * Assemble a list for all recognized partitions together with
   * the linux name "/dev/sda<x>"
   * return length of assembled list.
   *
   * There will be error messages for not existent partitions
   * or partitions of the wrong type.
   ****************************************************************************/
  int i, index = 0;
  char dev_id[20];
  struct ext2_data *data;
  char opaque[SUPERBLOCK_SIZE];
  data = (struct ext2_data *)opaque;

  for(i = 1; i < 16; i++) {
    sprintf(dev_id, "0:%x", i);
    if (!fs_set_blk_dev("usb", dev_id, FS_TYPE_EXT) &&
        ext4_read_superblock((char *)&data->sblock)) {
      strcpy(os_list[index].display_string,
             (const char *)&data->sblock.volume_name);
      sprintf(dev_id, "/dev/sda%d", i);
      strcpy(os_list[index].boot_partition, dev_id);
      index++;
    }
  }
  return index;
}

int sleepy_check(void) {
  /****************************************************************************
   * sleep for 100 milliseconds,
   * check for keyboard input
   * return character from keybord or 0 if nothing has happened
   ****************************************************************************/
  mdelay(100);
  if (tstc())
    return getc();
  return 0;
}

int master_sleep(void) {
  /****************************************************************************
   * sleep 200 times for 0.1 seconds each
   * check for input after each sleep
   ****************************************************************************/
  int rc = 0, j, k = 19;
  for(j = 1; j < 200; j++) {
    rc = sleepy_check();

    if (rc == ' ' || rc == '\r')
        rc = '1';
    if (rc > ' ')
        break;
    if (j % 10 == 0) {
        printf("booting in %d seconds\r", k);
        k --;
    }
  }
  return rc;
}

int check_for_filename(char *filename, char *device_path) {
    /**************************************************************************
     * check for a filename in an ext4 filesystem:
     *  locate the filesystem
     *  call ext4fs_exists
     **************************************************************************/
    char dev_id[10];

    /* convert /dev/sda<x> to "dev_id", "dev_id" is in hex */
    sprintf(dev_id, "0:%x",
      (int)simple_strtol((const char *)device_path + 8, NULL, 10));
    fs_set_blk_dev("usb", dev_id, FS_TYPE_EXT);
    return ext4fs_exists(filename);
}
