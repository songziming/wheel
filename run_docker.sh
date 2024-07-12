#!/bin/bash

docker run --rm -v .:/mnt/wheel -w /mnt/wheel --privileged=true $@
