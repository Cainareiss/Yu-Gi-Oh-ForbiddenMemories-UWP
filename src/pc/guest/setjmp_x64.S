    .text
    .globl Psx_setjmp
    .def Psx_setjmp; .scl 2; .type 32; .endef
Psx_setjmp:
    lea s_psx_x64_jump_buf(%rip), %rax
    movq %rbx, 0(%rax)
    movq %rbp, 8(%rax)
    movq %rdi, 16(%rax)
    movq %rsi, 24(%rax)
    movq %r12, 32(%rax)
    movq %r13, 40(%rax)
    movq %r14, 48(%rax)
    movq %r15, 56(%rax)
    leaq 8(%rsp), %rdx
    movq %rdx, 64(%rax)
    movq (%rsp), %rdx
    movq %rdx, 72(%rax)
    movdqu %xmm6, 80(%rax)
    movdqu %xmm7, 96(%rax)
    movdqu %xmm8, 112(%rax)
    movdqu %xmm9, 128(%rax)
    movdqu %xmm10, 144(%rax)
    movdqu %xmm11, 160(%rax)
    movdqu %xmm12, 176(%rax)
    movdqu %xmm13, 192(%rax)
    movdqu %xmm14, 208(%rax)
    movdqu %xmm15, 224(%rax)
    xorl %eax, %eax
    ret

    .globl Psx_longjmp
    .def Psx_longjmp; .scl 2; .type 32; .endef
Psx_longjmp:
    movl %edx, %eax
    testl %eax, %eax
    jnz 1f
    movl $1, %eax
1:
    lea s_psx_x64_jump_buf(%rip), %rdx
    movq 0(%rdx), %rbx
    movq 8(%rdx), %rbp
    movq 16(%rdx), %rdi
    movq 24(%rdx), %rsi
    movq 32(%rdx), %r12
    movq 40(%rdx), %r13
    movq 48(%rdx), %r14
    movq 56(%rdx), %r15
    movdqu 80(%rdx), %xmm6
    movdqu 96(%rdx), %xmm7
    movdqu 112(%rdx), %xmm8
    movdqu 128(%rdx), %xmm9
    movdqu 144(%rdx), %xmm10
    movdqu 160(%rdx), %xmm11
    movdqu 176(%rdx), %xmm12
    movdqu 192(%rdx), %xmm13
    movdqu 208(%rdx), %xmm14
    movdqu 224(%rdx), %xmm15
    movq 64(%rdx), %rsp
    jmp *72(%rdx)

    .bss
    .align 16
s_psx_x64_jump_buf:
    .space 240
