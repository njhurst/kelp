# look for /data{number}/kelp/volumes

import logging
import os
import re
import sys
import glob
import pwd

logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(name)s - %(levelname)s - %(message)s (%(filename)s:%(lineno)d)')

def pretty_size(size):
    for unit in ['B', 'KB', 'MB', 'GB', 'TB']:
        if size < 1024.0:
            break
        size /= 1024.0
    return f"{size:.2f} {unit}"

def pretty_size_for_hdd(size):
    for unit in ['B', 'KB', 'MB', 'GB', 'TB']:
        if size < 1000.0:
            break
        size /= 1000.0
    return f"{size:.2f} {unit}"

def find_volume_dirs():
    # look for /data{number}/kelp/volumes
    for mnt_pnt in glob.glob("/data*/"):
        # ensure that /data... is a mount point
        if not os.path.ismount(mnt_pnt):
            logging.info(f"Skipping data {mnt_pnt}: Not a mount point")
            continue

        d = os.path.join(mnt_pnt, "kelp")
        if not os.path.isdir(d):
            logging.info(f"Skipping directory {d}: Does not exist")
            continue

        # check name matches /data{number}/kelp/volumes
        if not re.match(r"/data\d+/kelp", d):
            logging.info(f"Skipping directory {d}: Name does not match /data{{number}}/kelp")
            continue
        # check owner is 'kelp' and group is 'kelp' looking up the uid and gid from /etc/passwd
        # Look up uid for 'kelp'
        try:
            kelp_user = pwd.getpwnam('kelp')
            kelp_uid = kelp_user.pw_uid

            kelp_group = pwd.getpwnam('kelp')
            kelp_gid = kelp_group.pw_gid
        except KeyError:
            # Handle the case when 'kelp' user does not exist
            logging.info("Error: 'kelp' user does not exist")
            sys.exit(1)

        # confirm owner and group for the directory
        if os.stat(d).st_uid != kelp_uid or os.stat(d).st_gid != kelp_gid:
            logging.info(f"Skipping directory {d}: Owner or group does not match 'kelp'")
            continue

        # check permissions are read write execute for owner
        if os.stat(d).st_mode & 0o700 != 0o700:
            logging.info(f"Skipping directory {d}: Permissions are not read write execute for owner")
            continue

        # look for kelp-volume-README.txt
        if not os.path.exists(os.path.join(d, "kelp-volume-README.txt")):
            logging.info(f"Skipping directory {d}: 'kelp-volume-README.txt' does not exist")
            continue

        # look at the contents of the directory
        for f in os.listdir(d):
            if os.path.isdir(f) and re.match(r"volume_[0-9a-f]{16}", f):
                logging.info(f"Found directory {f}")

        # work out how much free space is available on the filesystem
        stat = os.statvfs(d)
        free_space = stat.f_bavail * stat.f_frsize
        logging.info(f"Free space: {free_space} bytes")

        # determine the disk size
        disk_size = stat.f_blocks * stat.f_frsize
        # convert to human readable format
        logging.info(f"Disk size: {pretty_size_for_hdd(disk_size)}")

def configure_volume(volume_id):
    # create the volume directory
    volume_dir = f"/data{volume_id}/kelp/volumes"
    os.makedirs(volume_dir, exist_ok=True)

    # set the owner and group to 'kelp'
    kelp_user = pwd.getpwnam('kelp')
    kelp_uid = kelp_user.pw_uid

    kelp_group = pwd.getpwnam('kelp')
    kelp_gid = kelp_group.pw_gid

    os.chown(volume_dir, kelp_uid, kelp_gid)

    # set the permissions to 700
    os.chmod(volume_dir, 0o700)

    # determine the disk size
    stat = os.statvfs(volume_dir)
    disk_size = stat.f_blocks * stat.f_frsize
    # convert to human readable format

    # make a globally unique volume ID using randomness
    volume_id = os.urandom(8).hex()
    # write the volume ID to a file in the directory
    with open(os.path.join(volume_dir, "volume_id"), "w") as f:
        f.write(f"{volume_id}\n")

    # put a documentation file in the directory desribing the volume and how to recover it
    with open(os.path.join(volume_dir, "kelp-volume-README.txt"), "w") as f:
        f.write(f"Volume ID: {volume_id}\n")
        f.write(f"Disk size: {pretty_size_for_hdd(disk_size)}\n")
        f.write("This volume is part of a distributed storage system. To recover the data, you will need to use the recovery tool provided by the system administrator.\n")
        f.write("Version: 1.0\n")

# logging.info(f"Disk size: {pretty_size_for_hdd(30*1024*1024*1024)}")

if __name__ == "__main__":
    find_volume_dirs()
