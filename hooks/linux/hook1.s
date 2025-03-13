  .file	"hoo1.s"
  .intel_syntax noprefix
  .text
  .p2align 4
  .globl	hook1
  .type	hook1, @function
hook1:
  .setup:
  pop rax
  push rax
  push rcx
  push rdx
  push rbx
  push rbp
  push rsi
  push rdi
  push r8
  push r9
  push r10
  push r11
  push r12
  push r13
  push r14
  push r15

  .start:
  xor	eax, eax
  call	dynamic_should_run_hook@PLT
  test	al, al # &'s together, sets flags from result, discards
  jnz .abnormal

  .normal:
  pop r15
  pop r14
  pop r13
  pop r12
  pop r11
  pop r10
  pop r9
  pop r8
  pop rdi
  pop rsi
  pop rbp
  pop rbx
  pop rdx
  pop rcx
  pop rax

  .normal_stolen:
  mov rax, [r15+0x1c68]
  sub rax, [r15+0x1c60]
  shr rax, 0x3

  .normal_jump:
  push rax
  mov rax, QWORD PTR hook1return@GOTPCREL[rip]
  mov rax, [rax]
  jmp rax

  .abnormal:
  pop r15
  pop r14
  pop r13
  pop r12
  pop r11
  pop r10
  pop r9
  pop r8
  pop rdi
  pop rsi
  pop rbp
  pop rbx
  pop rdx
  pop rcx
  pop rax

  .abnormal_replacement:
  mov rax, 0

  .abnormal_jump:
  push rax
  mov rax, QWORD PTR hook1return@GOTPCREL[rip]
  mov rax, [rax]
  jmp rax


  .LFE0:
  .size	hook1, .-hook1
  .ident	"GCC: (GNU) 14.2.1 20240910"
  .section	.note.GNU-stack,"",@progbits
