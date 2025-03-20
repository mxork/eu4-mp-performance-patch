  .file	"hook4.s"
  .intel_syntax noprefix
  .text
  .globl	hook4
  .type	hook4, @function
hook4:
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

  .thehook:
  # rbp points at ping command + 0x48 (date)
  #  ping command + 0x4c points at player ID
  #  so rbp+4 points at player ID
  # r12d contains game day
  # eax contains ping day
  mov ecx, r12d
  sub ecx, eax
  mov edi, ecx
  mov esi, r12d
  mov edx, eax
  mov ecx, [rbp+0x4]
  call	handle_clientping@PLT

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
  # mov ecx, r12d
  # sub ecx, eax
  # cmp ecx, dword [rel 0x2fc59d8]
  # jle 0x1780d79
  mov ecx, r12d
  sub ecx, eax
  cmp ecx, [0x2fc59d8]
  jle .normal_else_jump

  .normal_jump:
  push rax
  mov rax, hook4return@GOTPCREL[rip]
  mov rax, [rax]
  jmp rax

  # clobbering rax here should be safe, since
  # the three branches from that point
  # either clobber it unconditionally, or
  # return from the function.
  .normal_else_jump:
  mov rax, hook4returnalt@GOTPCREL[rip]
  mov rax, [rax]
  jmp rax

  .LFE0:
  .size	hook4, .-hook4
  .ident	"GCC: (GNU) 14.2.1 20240910"
  .section	.note.GNU-stack,"",@progbits
