# XFS Realtime Files

To enable the usage of realtime files in XFS, it must be mounted
with an external realtime device, and to mkfs specifying that reflink
is disabled.

```
# mount -t xfs -o logdev=/dev/ubuntu-vg/xfs-log,rtdev=/dev/ubuntu-vg/xfs-rt /dev/ubuntu-vg/xfs-main  /xfs
```

So in this case:
  *  log data is on 253:4
  *  main dataset extents are on 253:4
  *  realtime data on 253:5

```
# dmsetup info -c | grep xfs
ubuntu--vg-xfs--log   253   3 L--w    1    1      0 LVM-2LevZb1buMNu5Vz7aqGeZV3pcD3HlkMwQPVuGGGL76mOHh2CGUhS8pnu38OFximc
ubuntu--vg-xfs--main  253   4 L--w    1    1      0 LVM-2LevZb1buMNu5Vz7aqGeZV3pcD3HlkMwxKMoWVL3K3r5JLvIFOstA28lHeck9ZdG
ubuntu--vg-xfs--rt    253   5 L--w    1    1      0 LVM-2LevZb1buMNu5Vz7aqGeZV3pcD3HlkMw14yZqwvDO87qZQhCle9TBSmwtMTUx8Ea
```

### Enabling realtime file usage

Files can only have data in the realtime device if the file is set to
realtime mode before having any data added to it. There are therefore two
viable ways of doing that:

  * creating a file and then setting its mode to realtime
  * setting realtime on a directory with inheritance, 
    so all files within the directory will be realtime

The flags for this are defined in `/usr/include/xfs/xfs_fs.h`, and are
respectively:

```
XFS_XFLAG_REALTIME
XFS_XFLAG_RTINHERIT
```

[Here is a sample C program](../main/src/xfs_rt.c) that sets those 
flags using the `xfsctl(3)` library call from libxfs. 
The same can be done a lot more simply by using the `xfs_io` tool.

### xfs_io(8)

To see the attributes of a directory or file, use xfs_io as follows:

```
xfs_io -c 'lsattr' /xfs/
xfs_io -c 'lsattr' /xfs/file
```

For example, below we can see that a filesystem is indeed XFS, then
the listing of xfs xattrs on its mountpoint, and the enabling of rt
inheritance:


```
# stat -f -c %T,%t /xfs
xfs,58465342
# xfs_io -c 'lsattr' /xfs
----------------- /xfs 
# xfs_io -c 'chattr +t' /xfs
# xfs_io -c 'lsattr' /xfs
-------t--------- /xfs 
```

From this point onwards all the files created inside this filesystem
will be realtime, and will have their data extents in the realtime device.

```
# touch /xfs/newfile
# ls -li /xfs/newfile
138 -rw-r--r-- 1 root root 0 Apr 17 22:04 /xfs/newfile
# xfs_io -c 'lsattr' /xfs/newfile
r---------------- /xfs/newfile 
```

This can also be disabled for some subdirs if we want to.

### Examining the location of a realtime file


```
# dd if=/dev/zero of=/xfs/100M_file bs=1M count=100
...

# xfs_io -c 'inode -v' /xfs/100M_file 
1048705:32          

# xfs_io -c 'bmap -vv' /xfs/100M_file 
/xfs/100M_file:
 EXT: FILE-OFFSET      RT-BLOCK-RANGE      TOTAL
   0: [0..204799]:     786432..991231     204800

```

Note the devices:
  * 253:3 - log dev
  * 253:4 - data
  * 253:5 - realtime  

```
# xfs_io -c 'fsmap' /xfs/100M_file 
	0: 253:3 [0..2097151]: journalling log 2097152   <<log is here
	1: 253:4 [0..111]: unknown 112
	2: 253:4 [112..127]: free space 16
	3: 253:4 [128..671]: unknown 544
	4: 253:4 [672..524287]: free space 523616
	5: 253:4 [524288..524383]: unknown 96
	6: 253:4 [524384..524415]: free space 32
	7: 253:4 [524416..524479]: unknown 64
	8: 253:4 [524480..1048575]: free space 524096
	9: 253:4 [1048576..1048671]: unknown 96
	10: 253:4 [1048672..1048703]: free space 32
	11: 253:4 [1048704..1048767]: unknown 64         <<inode is here
	12: 253:4 [1048768..1572863]: free space 524096
	13: 253:4 [1572864..1572959]: unknown 96
	14: 253:4 [1572960..2097151]: free space 524192
	15: 253:5 [0..786431]: free space 786432
	16: 253:5 [786432..991231]: unknown 204800       <<inode data is here
	17: 253:5 [991232..2097159]: free space 1105928
```

### Space usage

Space usage becomes really counter intuitive when realtime devices
are in use. Specifically:

devices:
  * 253:3 - log dev   - 1GB
  * 253:4 - main/data - 1GB
  * 253:5 - realtime  - 3GB 

when there is no realtime information set on the mountpoint we get
the size from the main/data volume:

```
# df -h /xfs
Filesystem                        Size  Used Avail Use% Mounted on
/dev/mapper/ubuntu--vg-xfs--main  1.0G   34M  991M   4% /xfs
```

We can create a RT file and fill up the RT disk (the file must not
have any data written to it before we set it to RT):

```
# touch /xfs/bigfile
# xfs_io -c 'chattr +r' /xfs/bigfile
# xfs_io -c 'lsattr' /xfs/bigfile
r---------------- /xfs/bigfile 
# ls -li /xfs/bigfile 
131 -rw-r--r-- 1 root root 0 Apr 17 23:30 /xfs/bigfile
```

then we dd onto the file, with care not to replace the inode:

```
# dd if=/dev/zero of=/xfs/bigfile oflag=append,sync conv=notrunc bs=1M
dd: error writing '/xfs/bigfile': No space left on device
3073+0 records in
3072+0 records out
3221225472 bytes (3.2 GB, 3.0 GiB) copied, 5.81301 s, 554 MB/s
# ls -li /xfs/bigfile 
131 -rw-r--r-- 1 root root 3221225472 Apr 17 23:31 /xfs/bigfile
# xfs_io -c 'lsattr' /xfs/bigfile
r---------------- /xfs/bigfile 
# xfs_bmap /xfs/bigfile 
/xfs/bigfile:
	0: [0..6291455]: 0..6291455
# xfs_bmap -v /xfs/bigfile 
/xfs/bigfile:
 EXT: FILE-OFFSET      RT-BLOCK-RANGE       TOTAL
   0: [0..6291455]:    0..6291455         6291456
```

the RT extent is now full, and we cannot add anything to it anymore, but
this info is not displayed in df in any place:

```
# df -h /xfs
Filesystem                        Size  Used Avail Use% Mounted on
/dev/mapper/ubuntu--vg-xfs--main  1.0G   34M  991M   4% /xfs
```

However, if we remove the `bigfile` and create a subdir with inherited
RT, we get the phenomenon:

```
# mkdir /xfs/subdir
# xfs_io -c 'chattr +t' /xfs/subdir/
# df -h /xfs
Filesystem                        Size  Used Avail Use% Mounted on
/dev/mapper/ubuntu--vg-xfs--main  1.0G   34M  991M   4% /xfs
# df -h /xfs/subdir/
Filesystem                        Size  Used Avail Use% Mounted on
/dev/mapper/ubuntu--vg-xfs--main  3.0G     0  3.0G   0% /xfs
```

so df will report the RT free extent size for directories set to RT with
inheritance, even though it still shows the LV xfs-main, which is 1G in size.
The same applies to the mountpoint itself. If the mountpoint itself is 
set to RT with inheritance:

```
# df -h /xfs
Filesystem                        Size  Used Avail Use% Mounted on
/dev/mapper/ubuntu--vg-xfs--main  1.0G   34M  991M   4% /xfs
# xfs_io -c 'chattr +t' /xfs
# df -h /xfs
Filesystem                        Size  Used Avail Use% Mounted on
/dev/mapper/ubuntu--vg-xfs--main  3.0G     0  3.0G   0% /xfs
```

we can see this counter intuitive behaviour where the LV holding the
filesystem shown in df is 1G, but the space is actually showing the
size from the RT LV.

Introspecting the data usage becomes less obvious specially when the
number of inodes becomes very large, and it is not clear if a `ENOSPC`
is produced because of a real inode limitation, or a block limitation
on the data volume. 

It is trivial though to create a subdir and disable RT on it, and then
it is possible to see the normal data volume with its usage.

```
# mkdir /xfs/main
# xfs_io -c 'chattr -t' /xfs/main
# df -h /xfs/main
Filesystem                        Size  Used Avail Use% Mounted on
/dev/mapper/ubuntu--vg-xfs--main  1.0G   34M  991M   4% /xfs
```

### Other points on RT files/dirs

RT files have a number of limitations imposed onto them, eg:
  * reflinking is not available for RT files.
  * DAX is not available for RT files.
  * files will contain an integer multiple of a base rtextent size
  * certain aspects of iomap'ping to page cache are disabled for RT files.
  * fsync behaves differently forcing flush of RT file data before
    updating the inode's st_size attribute in case of extending writes.
  * RT files cannot be exported over pNFS

