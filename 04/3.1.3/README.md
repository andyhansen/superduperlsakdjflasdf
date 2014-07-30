do this in 3.1.3
sudo insmod seek.ko
sudo mknod /dev/mycdrv c 700 0
sudo chown pi:pi /dev/mycdrv

do this in 3.2.1
sudo insmod wait_queue.ko

