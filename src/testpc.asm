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

        farptr  0x40:def_handler
        farptr  0x40:def_handler
        farptr  0x40:def_handler
        farptr  0x40:def_handler3
rept 0x20 - 4 {
        farptr  0x40:def_handler
}
        farptr  0x40:def_handler2
times 0x400 - $% db 0

org 0
        jmp     start
def_handler:
        hlt
        jmp     def_handler
def_handler2:
        push    ax
        mov     ax, 1
        out     0, al
        pop     ax;
def_handler3:
        iret
start:  mov     ax, data_seg
        mov     ds, ax
        mov     ax, 0x9000
        mov     ss, ax
        xor     sp, sp
        add     ah, 0x10
        mov     es, ax

        mov     cx, palete_words
        mov     di, 64000
        mov     si, palete
        rep     movsw

        mov     al, 1
        out     0, al

        mov     cx, palete_words
        mov     di, 64000
        mov     si, palete
        rep     movsw

        sti
.loop:  hlt

        mov     al, 0
        xor     di, di
        mov     dx, 16

.loop2: mov     cx, 16
@@:     stosb
        inc     al
        loop    @b
        add     di, 304
        dec     dx
        jnz     .loop2

        jmp     .loop

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

        dd 0
        dd 0x111111
        dd 0x222222
        dd 0x333333
        dd 0x444444
        dd 0x555555
        dd 0x666666
        dd 0x777777
        dd 0x888888
        dd 0x999999
        dd 0xAAAAAA
        dd 0xBBBBBB
        dd 0xCCCCCC
        dd 0xDDDDDD
        dd 0xEEEEEE
        dd 0xFFFFFF

        dd 0x0000FF
        dd 0x4000FF
        dd 0x8000FF
        dd 0xBF00FF
        dd 0xFF00FF
        dd 0xFF00BF
        dd 0xFF0080
        dd 0xFF0040
        dd 0xFF0000
        dd 0xFF4000
        dd 0xFF8000
        dd 0xFFBF00
        dd 0xFFFF00
        dd 0xBFFF00
        dd 0x80FF00
        dd 0x40FF00
        dd 0x00FF00
        dd 0x00FF40
        dd 0x00FF80
        dd 0x00FFBF
        dd 0x00FFFF
        dd 0x00BFFF
        dd 0x0080FF
        dd 0x0040FF

        dd 0x8080FF
        dd 0x9F80FF
        dd 0xBF80FF
        dd 0xDF80FF
        dd 0xFF80FF
        dd 0xFF80DF
        dd 0xFF80BF
        dd 0xFF809F
        dd 0xFF8080
        dd 0xFF9F80
        dd 0xFFBF80
        dd 0xFFDF80
        dd 0xFFFF80
        dd 0xDFFF80
        dd 0xBFFF80
        dd 0x9FFF80
        dd 0x80FF80
        dd 0x80FF9F
        dd 0x80FFBF
        dd 0x80FFDF
        dd 0x80FFFF
        dd 0x80DFFF
        dd 0x80BFFF
        dd 0x809FFF

        dd 0xBFBFFF
        dd 0xCFBFFF
        dd 0xDFBFFF
        dd 0xEFBFFF
        dd 0xFFBFFF
        dd 0xFFBFEF
        dd 0xFFBFDF
        dd 0xFFBFCF
        dd 0xFFBFBF
        dd 0xFFCFBF
        dd 0xFFDFBF
        dd 0xFFEFBF
        dd 0xFFFFBF
        dd 0xEFFFBF
        dd 0xDFFFBF
        dd 0xCFFFBF
        dd 0xBFFFBF
        dd 0xBFFFCF
        dd 0xBFFFDF
        dd 0xBFFFEF
        dd 0xBFFFFF
        dd 0xBFEFFF
        dd 0xBFDFFF
        dd 0xBFCFFF

        dd 0x000080
        dd 0x200080
        dd 0x400080
        dd 0x600080
        dd 0x800080
        dd 0x800060
        dd 0x800040
        dd 0x800020
        dd 0x800000
        dd 0x802000
        dd 0x804000
        dd 0x806000
        dd 0x808000
        dd 0x608000
        dd 0x408000
        dd 0x208000
        dd 0x008000
        dd 0x008020
        dd 0x008040
        dd 0x008060
        dd 0x008080
        dd 0x006080
        dd 0x004080
        dd 0x002080

        dd 0x404080
        dd 0x504080
        dd 0x604080
        dd 0x704080
        dd 0x804080
        dd 0x804070
        dd 0x804060
        dd 0x804050
        dd 0x804040
        dd 0x805040
        dd 0x806040
        dd 0x807040
        dd 0x808040
        dd 0x708040
        dd 0x608040
        dd 0x508040
        dd 0x408040
        dd 0x408050
        dd 0x408060
        dd 0x408070
        dd 0x408080
        dd 0x407080
        dd 0x406080
        dd 0x405080

        dd 0x606080
        dd 0x686080
        dd 0x706080
        dd 0x786080
        dd 0x806080
        dd 0x806078
        dd 0x806070
        dd 0x806068
        dd 0x806060
        dd 0x806860
        dd 0x807060
        dd 0x807860
        dd 0x808060
        dd 0x788060
        dd 0x708060
        dd 0x688060
        dd 0x608060
        dd 0x608068
        dd 0x608070
        dd 0x608078
        dd 0x608080
        dd 0x607880
        dd 0x607080
        dd 0x606880

        dd 0x000040
        dd 0x100040
        dd 0x200040
        dd 0x300040
        dd 0x400040
        dd 0x400030
        dd 0x400020
        dd 0x400040
        dd 0x400000
        dd 0x401000
        dd 0x402000
        dd 0x403000
        dd 0x404000
        dd 0x304000
        dd 0x204000
        dd 0x104000
        dd 0x004000
        dd 0x004010
        dd 0x004020
        dd 0x004030
        dd 0x004040
        dd 0x003040
        dd 0x002040
        dd 0x001040

        dd 0x202040
        dd 0x282040
        dd 0x302040
        dd 0x382040
        dd 0x402040
        dd 0x402038
        dd 0x402030
        dd 0x402028
        dd 0x402020
        dd 0x402820
        dd 0x403020
        dd 0x403820
        dd 0x404020
        dd 0x384020
        dd 0x304020
        dd 0x284020
        dd 0x204020
        dd 0x204028
        dd 0x204030
        dd 0x204038
        dd 0x204040
        dd 0x203840
        dd 0x203040
        dd 0x202840

        dd 0x303040
        dd 0x343040
        dd 0x383040
        dd 0x3C3040
        dd 0x403040
        dd 0x40303C
        dd 0x403038
        dd 0x403034
        dd 0x403030
        dd 0x403430
        dd 0x403830
        dd 0x403C30
        dd 0x404030
        dd 0x3C4030
        dd 0x384030
        dd 0x344030
        dd 0x304030
        dd 0x304034
        dd 0x304038
        dd 0x30403C
        dd 0x304040
        dd 0x303C40
        dd 0x303840
        dd 0x303440
times palete + 0x400 - $ db 0
palete_words = ($ - palete) / 2
