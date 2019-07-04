void sys_init_bsp() {
    char * v = (char *) 0xb8000;
    char * s = "hello, world!";
    for (int i = 0; s[i]; ++i) {
        v[2 * i + 0] = s[i];
        v[2 * i + 1] = 0x0f;
    }

    while (1) {}
}

void sys_init_ap() {
    //
}
