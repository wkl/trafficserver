#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
.text
	.align 4
.globl ink_atomic_swap
	.type	 ink_atomic_swap,@function
.globl ink_atomic_swap_ptr
	.type	 ink_atomic_swap_ptr,@function
ink_atomic_swap_ptr:
ink_atomic_swap:
	pushl %ebp
	movl %esp,%ebp
	movl 8(%ebp),%eax
	movl 12(%ebp),%edx
	lock
	xchgl %edx,(%eax)
	movl %edx,%eax
	leave
	ret
	
	.align 4
.globl ink_atomic_swap64
	.type	 ink_atomic_swap64,@function
ink_atomic_swap64:
	pushl %ebp
	movl %esp,%ebp
	subl $4,%esp
	pushl %edi
	pushl %esi
	pushl %ebx
	movl 8(%ebp),%edi
	movl %edi,-4(%ebp)
	.align 4
Link_atomic_swap64:
	movl -4(%ebp),%ecx
	movl (%ecx),%ebx
	movl 4(%ecx),%esi
	pushl 16(%ebp)
	pushl 12(%ebp)
	pushl %esi
	pushl %ebx
	pushl %edi
	call ink_atomic_cas64
	addl $20,%esp
	testl %eax,%eax
	je Link_atomic_swap64
	movl %ebx,%eax
	movl %esi,%edx
	leal -16(%ebp),%esp
	popl %ebx
	popl %esi
	popl %edi
	leave
	ret

	.align 4
.globl ink_atomic_cas64
	.type	 ink_atomic_cas64,@function
ink_atomic_cas64:
	pushl %ebp
	movl %esp,%ebp
	pushl %edi
	pushl %ebx
	movl 8(%ebp),%edi
	movl 12(%ebp),%eax
	movl 16(%ebp),%edx
	movl 20(%ebp),%ebx
	movl 24(%ebp),%ecx
	lock 
	cmpxchg8b (%edi)
	setz %al
	andl $255,%eax
	leal -8(%ebp),%esp
	popl %ebx
	popl %edi
        leave		
	ret
	
