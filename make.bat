@ REM wsl -e bash -li -c "make %*"
@ docker run --rm -v .:/mnt/wheel -w /mnt/wheel --privileged=true wheel make %*
