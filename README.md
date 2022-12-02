`blkram` is a simple RAM-backed block device kernel module based on
Linux 6.1 for educational purposes. A detailed article about this driver can be found [here](https://blog.pankajraghav.com/2022/11/30/BLKRAM.html) in
my blog.

## Compilation:
- Clone the Linux kernel from [here](https://github.com/torvalds/linux)
- [Compile](https://wiki.archlinux.org/title/Kernel/Traditional_compilation#Compilation) the Linux kernel
- Clone this repo
- Run the following command in the `blkram` repo:
```bash
blkram$ make kdir="/path/to/linux/repo"
```
## Installing the module:
To install the module in your running system, simply run the following:
```bash
$ insmod ./blkram.ko
```
