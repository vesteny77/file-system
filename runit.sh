#!/usr/bin/env bash

fusermount -u /tmp/yuanson3

# create an image
truncate -s 1048576 test.img

# format the image
./mkfs.a1fs -i 128 -f -z test.img

# mount the image
./a1fs test.img /tmp/yuanson3

# perform a few operations on the file system such as creating a directory,
# displaying the contents of a directory, creating a file, adding data to a file (Keep it simple)
ls -la /tmp/yuanson3
mkdir /tmp/yuanson3/a
mkdir /tmp/yuanson3/b
ls -la /tmp/yuanson3
mkdir /tmp/yuanson3/a/a1
ls -la /tmp/yuanson3/a
rm -df /tmp/yuanson3/a/a1
rm -df /tmp/yuanson3/b
touch /tmp/yuanson3/f1
touch /tmp/yuanson3/f2
rm -f /tmp/yuanson3/a/f1
echo "Hello World!" > /tmp/yuanson3/f2
cat /tmp/yuanson3/f2

# unmount the image
fusermount -u /tmp/yuanson3

# mount the image again and display some contents of the file system to show that
# the relevant state was saved to the disk image.
./a1fs test.img /tmp/yuanson3
ls -la /tmp/yuanson3
ls -la /tmp/yuanson3/a
cat /tmp/yuanson3/f2