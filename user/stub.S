.global syscall64
.global syscall80

.text
.code64

syscall64:
    syscall
    ret

syscall80:
    int $0x80
    ret
