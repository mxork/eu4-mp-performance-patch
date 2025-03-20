  .file	"hook4.s"
  .intel_syntax noprefix
  .text
  .globl	hook4
  .def	hook4;	.scl	2;	.type	32;	.endef
  .seh_proc	hook4
hook4:
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

  .thehook:
  # r15 points at ping command + 0x50
  #    r15            0x761fc959e050      129878894174288
  # player ID is at 0x54
  #  so r15+4 points at player ID
  mov eax, r13d
  sub eax, ebx

  # eax contains days behind
  # r13d contains game days (ish)
  # ebx contains ping days
  mov ecx, eax
  mov edx, r13d
  mov r8d, ebx
  mov r9d, [r15+0x4]
  call	handle_clientping

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
  mov eax, r13d
  sub eax, ebx
  # a little wrangling to get the right address
  push r13
  mov r13, .refptr.hook4ndefine[rip]
  cmp eax, [r13]
  pop r13
  jle .normal_else_jump

  .normal_jump:
  push rax
  mov rax, QWORD PTR .refptr.hook4return[rip]
  mov rax, [rax]
  jmp rax

  # :warn this clobbers the register used. careful it is
  #       safe to use it.
  .normal_else_jump:
  mov rcx, QWORD PTR .refptr.hook4returnalt[rip]
  mov rcx, [rcx]
  jmp rcx

  .seh_endproc
  .ident	"GCC: (GNU) 14.2.1 20240910"
  .def	dynamic_should_run_hook;	.scl	2;	.type	32;	.endef
  .def	handle_clientping;	.scl	2;	.type	32;	.endef

  .section	.rdata$.refptr.hook4return, "dr"
  .globl	.refptr.hook4return
  .linkonce	discard
  .refptr.hook4return:
  .quad	hook4return

  .section	.rdata$.refptr.hook4returnalt, "dr"
  .globl	.refptr.hook4returnalt
  .linkonce	discard
  .refptr.hook4returnalt:
  .quad	hook4returnalt

  .section	.rdata$.refptr.hook4ndefine, "dr"
  .globl	.refptr.hook4ndefine
  .linkonce	discard
  .refptr.hook4ndefine:
  .quad	hook4ndefine
