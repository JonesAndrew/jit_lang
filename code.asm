L0:
sub rsp, 264
mov eax, 0                              ; mov eax, 0
push eax                                ; push eax
mov rax, 0                              ; mov i, 0
pop ebx                                 ; pop ebx
mov dword [rsp+rax], ebx                ; mov dword [&v0+i], ebx
L2:
mov eax, 3                              ; mov eax, 3
push eax                                ; push eax
mov eax, 5                              ; mov eax, 5
push eax                                ; push eax
pop ebx                                 ; pop ebx
pop eax                                 ; pop eax
sub eax, ebx                            ; sub eax, ebx
push eax                                ; push eax
pop eax                                 ; pop eax
cmp eax, 0                              ; cmp eax, 0
je L3                                   ; je L3
mov rax, 0                              ; mov i, 0
mov ebx, dword [rsp+rax]                ; mov ebx, dword [&v0+i]
push ebx                                ; push ebx
mov eax, 1                              ; mov eax, 1
push eax                                ; push eax
pop ebx                                 ; pop ebx
pop eax                                 ; pop eax
add eax, ebx                            ; add eax, ebx
push eax                                ; push eax
mov rax, 0                              ; mov i, 0
pop ebx                                 ; pop ebx
mov dword [rsp+rax], ebx                ; mov dword [&v0+i], ebx
short jp L2                             ; jp L2
L3:
mov rax, 0                              ; mov i, 0
mov ebx, dword [rsp+rax]                ; mov ebx, dword [&v0+i]
push ebx                                ; push ebx
pop eax                                 ; pop eax
L1:
add rsp, 264
ret
