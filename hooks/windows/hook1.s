  .file	"hook1.s"
  .intel_syntax noprefix
  .text
  .globl	hook1
  .def	hook1;	.scl	2;	.type	32;	.endef
  .seh_proc	hook1
hook1:
  .setup:
  # jump should push
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
  call	dynamic_should_run_hook
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
  mov rax, [rdi+0x1c98]
  sub rax, [rdi+0x1c90]
  sar rax, 0x3

  .normal_jump:
  push rax
  mov rax, QWORD PTR .refptr.hook1return[rip]
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
  mov rax, QWORD PTR .refptr.hook1return[rip]
  mov rax, [rax]
  jmp rax

  .seh_endproc
  .ident	"GCC: (GNU) 14.2.1 20240910"
  .def	dynamic_should_run_hook;	.scl	2;	.type	32;	.endef
  .section	.rdata$.refptr.hook1return, "dr"
  .globl	.refptr.hook1return
  .linkonce	discard
  .refptr.hook1return:
  .quad	hook1return
