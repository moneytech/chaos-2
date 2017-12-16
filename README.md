ChaOS is an SMP-aware kernel that we are doing as a part of our studies at Epitech.

# Build Dependencies
* `make`
* `gcc` or `clang` (latest version, ideally)
* `grub-mkrescue` and `libisoburn` (generally packed with other binaries as `grub`)
* `mtools`
* `qemu` (cpu emulator) *optional*

If you are using `apt-get` as your package manager (`Debian`, `Ubuntu` etc.), you can use this command to install all dependencies:
```bash
apt-get install qemu grub-pc-bin xorriso mtools
```

If you are using `pacman` as your package manager (`ArchLinux`, `Manjaro` etc.), you can use this command:
```bash
pacman -Sy qemu grub libisoburn mtools
```

If you are using `portage` as your package manager (`Gentoo`), you can use this command instead:
```bash
emerge --ask sys-boot/libisoburn sys-fs/dosfstools sys-fs/mtools
```

If you are using an other package manager, well... Good luck! :p

# Building an iso

First, tune as you wish the kernel's configuration with
```bash
make config
```

Then, build the kernel:
```bash
make kernel
```

To build a complete iso with grub installed (suitable for USB flash drives or virtual machines), run
```bash
make iso
```

# Running with QEMU

If you want to run ChaOS through QEMU even if it's boring & useless right now, run
```bash
make run
```

# Roadmap

- [X] Kernel options
- [X] Kernel architecture
- [X] High-address Kernel
- [ ] Boot
  - [X] Multiboot
  - [X] Paging setup
  - [X] GDT setup
  - [X] IDT setup
  - [ ] TSS setup
  - [X] SMP setup
- [ ] Pc drivers
  - [X] VGA
  - [ ] Serial
- [ ] Memory
  - [X] Physical Memory Management
  - [X] Virtual Memory Management (`mmap()`, `munmap()` etc.)
  - [ ] Virtual segments of memory (`brk()`, `sbrk()`)
  - [ ] Kernel heap (`kalloc()`, `kfree()`, `krealloc()`)
- [ ] ELF Program execution (`execve()`)
- [ ] Syscall interface and userspace (ring 3)
- [ ] Multi process / threads
  - [ ] Scheduling
  - [ ] Kernel threads
  - [ ] Processes (`fork()` and `exit()`)
- [ ] Initrd loading, reading and writing
- [ ] Filesystem
  - [ ] Directory listing (`opendir()`, `readdir()`, `closedir()`)
  - [ ] Creating and removing files/directories (`unlink()`)
  - [ ] File basic IO operations (`read()`, `write()`)
  - [ ] File advanced IO operations (`pipe()`, `dup()`)
  - [ ] File informations (`stat()`)
- [ ] User space programs (init, tty, shell, basic binaries such as `echo`, `ls`, `rm` etc.)
- [ ] Virtual filesystems (`/proc`, `/dev`)

# :rocket: Wanna participate?

Fork me!
