  .file	"hook3.s"
  .intel_syntax noprefix
  .text
  .globl	hook3
  .def	hook3;	.scl	2;	.type	32;	.endef
  .seh_proc	hook3
hook3:
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
  mov edi, [r14+0x21bc]
  mov ebx, r15d
  test edi, edi
  jle .jump_to_original_else_branch

  .normal_jump:
  push rax
  mov rax, QWORD PTR .refptr.hook3return[rip]
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

  # in this case, instead of zeroing the loop
  # variable, it is actually safe and expected
  # to just jump unconditionally to the else branch
  #
  # we can't use push/pop as we normally do, but
  # rbx is about to be clobbered, so we can use that:
  #
  # 140745909: mov rbx, qword [rbp+0x1228]
  .abnormal_replacement:
  mov edi, [r14+0x21bc]
  mov ebx, r15d

  .jump_to_original_else_branch:
  ; mov rbx, 0x1407458f5 # this
  ; mov rbx, 0x1407458f5 # this
  mov rbx, QWORD PTR .refptr.hook3returnalt[rip]
  mov rbx, [rbx]
  jmp rbx;

  #.abnormal_jump:
  #push rax
  #mov rax, QWORD PTR .refptr.hook3return[rip]
  #mov rax, [rax]
  #jmp rax

  .seh_endproc
  .ident	"GCC: (GNU) 14.2.1 20240910"
  .def	dynamic_should_run_hook;	.scl	2;	.type	32;	.endef
  .section	.rdata$.refptr.hook3return, "dr"
  .globl	.refptr.hook3return
  .linkonce	discard
  .refptr.hook3return:
  .quad	hook3return
  .section	.rdata$.refptr.hook3returnalt, "dr"
  .globl	.refptr.hook3returnalt
  .linkonce	discard
  .refptr.hook3returnalt:
  .quad	hook3returnalt
