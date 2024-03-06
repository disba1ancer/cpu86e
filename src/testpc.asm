format binary as "img"
use16

macro farptr arg {
    match seg:off,arg \{
        dw off, seg
    \}
}

struc farptr arg {
    label . dword
    farptr arg
}

rept 0x20 { farptr 0x40:def_handler }
farptr 0x40:def_handler2
times 0x400 - $%% db 0

org 0
        jmp start
def_handler:
        hlt
        jmp def_handler
def_handler2:
        push ax
        mov ax, 1
        out 0, al
        pop ax;
        iret
start:  mov ax, data_seg
        mov ds, ax
        mov ax, 0x9000
        mov ss, ax
        xor sp, sp
        add ah, 0x10
        mov es, ax

        mov cx, palete_words
        mov di, 64000
        mov si, palete
        rep movsw

        mov al, 1
        out 0, al

        mov cx, palete_words
        mov di, 64000
        mov si, palete
        rep movsw

        sti
@@:     hlt
        mov byte [es:0], 1
        mov byte [es:1], 2
        mov byte [es:2], 3
        mov byte [es:3], 4
        mov byte [es:4], 5
        mov byte [es:5], 6
        mov byte [es:6], 7
        mov byte [es:7], 8
        hlt
        mov byte [es:0], 1
        mov byte [es:1], 2
        mov byte [es:2], 3
        mov byte [es:3], 4
        mov byte [es:4], 5
        mov byte [es:5], 6
        mov byte [es:6], 7
        mov byte [es:7], 8
        jmp @b

align 16
data_seg = $% / 16
org 0
palete: dd 0x0
        dd 0xAA
        dd 0xAA00
        dd 0xAAAA
        dd 0xAA0000
        dd 0xAA00AA
        dd 0xAA5500
        dd 0xAAAAAA
        dd 0x555555
        dd 0x5555FF
        dd 0x55FF55
        dd 0x55FFFF
        dd 0xFF5555
        dd 0xFF55FF
        dd 0xFFFF55
        dd 0xFFFFFF
times palete + 0x400 - $ db 0
palete_words = ($ - palete) / 2
