#include "CMemory.h"
#include <conio.h>
#include <stdio.h>

#define pl(s) printf("%s\n", s)

using mem = Memory;

#ifdef _WIN64
#define Func1		0x00007FF69B4111D0
#define Func2		0x00007FF69B4111F0
#define Func3		0x00007FF69B411210
#define Func4		0x00007FF69B411230
#define ObjectPtr	0x00007FF69B415040
#define MemberFunc	0x00007FF69B411290
#define UpdateStr	0x00007FF69B411370
#define HookLoc		0x00007FF69B411541
#else
#define Func1		0x00D41180
#define Func2		0x00D411A0
#define Func3		0x00D411C0
#define Func4		0x00D411E0
#define ObjectPtr	0x00D44018
#define MemberFunc	0x00D41220
#define UpdateStr	0x00D412E0
#define HookLoc		0x00D41442
#endif

void CallFunc1(HANDLE hProc)
{
	pl("Calling Function1.\n");

	HANDLE hThread = CreateRemoteThread(hProc, 0, 0, (LPTHREAD_START_ROUTINE)Func1, 0, 0, 0);
	if (!hThread)
	{
		pl("Failed to create thread.");
		return;
	}
	WaitForSingleObject(hThread, INFINITE);
	CloseHandle(hThread);
}

void CallFunc2(HANDLE hProc)
{
	pl("Calling Function2.\n");

	HANDLE hThread = CreateRemoteThread(hProc, 0, 0, (LPTHREAD_START_ROUTINE)Func2, (void*)(1337), 0, 0);
	if (!hThread)
	{
		pl("Failed to create thread.");
		return;
	}
	WaitForSingleObject(hThread, INFINITE);
	CloseHandle(hThread);
}

void CallFunc3(HANDLE hProc)
{
	pl("Calling Function3.\n");

	const char* szStr = "I'm a new string.";

	//allocate space in remote process for our string
	void* pMemory = VirtualAllocEx(hProc, 0, strlen(szStr), MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
	if (!pMemory)
	{
		pl("Failed to allocate memory.");
		return;
	}

	//write string to the remote process
	if (!WriteProcessMemory(hProc, pMemory, szStr, strlen(szStr), nullptr))
	{
		pl("Fail to write to memory.");
		return;
	}

	//call function, notice we pass the pointer of the memory we allocated (where we wrote our string to) as the parameter
	HANDLE hThread = CreateRemoteThread(hProc, 0, 0, (LPTHREAD_START_ROUTINE)Func3, pMemory, 0, 0);
	if (!hThread)
	{
		pl("Failed to create thread.");
		return;
	}

	WaitForSingleObject(hThread, INFINITE);
	VirtualFreeEx(hProc, pMemory, 0, MEM_RELEASE);
	CloseHandle(hThread);
}

struct _Args
{
	int a = 0; //arg 1
	int b = 0; //arg 2
	int c = 0; //return
};

void CallFunc4SC(HANDLE hProc)
{
	pl("Calling Function4 with shellcode.\n");

	//buffer for our shellcode
	DWORD dwBufferSize = 0x1000;

	//allocate space for our shellcode
	void* pMemory = VirtualAllocEx(hProc, 0, dwBufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!pMemory)
	{
		pl("Failed to allocate memory.");
		return;
	}

	printf("%p\nPress ENTER to continue...\n", pMemory);
	_getch();

	int offset1, offset2;
	constexpr DWORD dwCodeSize = 0x200;
#ifndef _WIN64
	offset1 = 1;
	offset2 = 11;
	char shellcode[dwCodeSize] = {
		0xB8, 0x00 ,0x00, 0x00, 0x00,	//mov eax, 0	  // move address of arguments into eax
		0xFF, 0x30,						//push [eax]	  // push first arg
		0xFF, 0x70, 0x04,				//push [eax+4]	  // push second arg
		0xB8, 0x00, 0x00, 0x00, 0x00,	//mov eax, 0	  // move address of target function into eax
		0xFF, 0xD0,						//call eax		  // call function
		0x83, 0xC4, 0x08,				//add esp, 8	  // clean stack (cdecl function, we're responsible for this)
		0xC3,							//ret			  // return
		0x90							//nop			  // just for alignment.
	};
#else
	offset1 = 2;
	offset2 = 17;
	char shellcode[dwCodeSize] = {
		0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0X00, 0x00, 0x00, //movabs rax, ArgumentsAddress
		0x8B, 0x08,												    //mov eax, [rax]
		0x8B, 0x50, 0x04,										    //mov eax, [rax+4]
		0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //movabs rax, FunctionAddress
		0x48, 0x83, 0xEC, 0x32,									    //add rsp, 32
		0xFF, 0xD0,												    //call rax
		0x48, 0x83, 0xC4, 0x32,									    //sub rsp, 32
		0xC3													    //retn
	};
#endif
	// Let's finish setting up the shellcode.
	*(uintptr_t*)(&shellcode[offset1]) = (uintptr_t)pMemory;
	*(uintptr_t*)(&shellcode[offset2]) = (uintptr_t)Func4;

	//arguments to pass to target function
	_Args args = { 2, 3 };

	pl("Writing arguments to process...");

	if (!WriteProcessMemory(hProc, pMemory, &args, sizeof(args), nullptr))
	{
		pl("Failed to write arguments to process.");
		VirtualFreeEx(hProc, pMemory, dwBufferSize, MEM_RELEASE);
		return;
	}

	pl("Writing code to process...");

	void* pCode = (void*)((uintptr_t)pMemory + sizeof(args));
	if (!WriteProcessMemory(hProc, pCode, shellcode, dwCodeSize, nullptr))
	{
		pl("Failed to write code to process.");
		VirtualFreeEx(hProc, pMemory, dwBufferSize, MEM_RELEASE);
		return;
	}

	HANDLE hThread = CreateRemoteThread(hProc, 0, 0, (LPTHREAD_START_ROUTINE)pCode, 0, 0, 0);
	if (!hThread)
	{
		pl("Failed to create thread.");
		VirtualFreeEx(hProc, pMemory, dwBufferSize, MEM_RELEASE);
		return;
	}

	WaitForSingleObject(hThread, INFINITE);

	//Read return data
	DWORD dwExit = 0;
	GetExitCodeThread(hThread, &dwExit);
	printf("Function returned: %i\n", dwExit);

	VirtualFreeEx(hProc, pMemory, dwBufferSize, MEM_RELEASE);
}


void MappedFunc(void* pArgs)
{
#ifndef _WIN64
	using _Function4 = int __cdecl(int, int);
#else
	using _Function4 = int __fastcall(int, int);
#endif
	_Args * args = (_Args*)pArgs;
	_Function4 * Function4 = (_Function4*)Func4;
	args->c = Function4(args->a, args->b);
	Function4(args->c, args->b);
}

void MarkerFunc()
{
	return;
}

void CallFunc4FM(HANDLE hProc)
{
	pl("Calling Function4 with function mapping.");

	_Args args = { 1,2 };

	uintptr_t dwFuncSize = (uintptr_t)MarkerFunc - (uintptr_t)MappedFunc;
	uintptr_t dwSize = dwFuncSize + sizeof(_Args);

	void* pMemory = VirtualAllocEx(hProc, 0, dwSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
	if (!pMemory)
	{
		pl("Failed to allocate memory.");
		return;
	}

	void* pArgs = (void*)((uintptr_t)pMemory + dwFuncSize);

	if (!WriteProcessMemory(hProc, pMemory, MappedFunc, dwFuncSize, nullptr) ||
		!WriteProcessMemory(hProc, pArgs, &args, sizeof(args), nullptr))
	{
		pl("Failed to write function and/or data to process.");
		return;
	}

	HANDLE hThread = CreateRemoteThread(hProc, 0, 0, (LPTHREAD_START_ROUTINE)pMemory, pArgs, 0, 0);
	if (!hThread)
	{
		pl("Failed to create thread.");
		return;
	}
	WaitForSingleObject(hThread, INFINITE);
	DWORD dwExit = 0;
	GetExitCodeThread(hThread, &dwExit);

	if (!ReadProcessMemory(hProc, pArgs, &args, sizeof(args), nullptr))
		pl("Failed to read return value.");

	printf("Function returned: %i\n", args.c);
	printf("Function returned: %i\n", dwExit);
	_getch();
	VirtualFreeEx(hProc, pMemory, dwSize, MEM_RELEASE);
	CloseHandle(hThread);

}

struct _Class
{
	int x, y;
	char str[16];
	//using _MemberFunc = void __thiscall(void* pThis);
	typedef void(__thiscall * _MemberFunc)(void* pThis);
	_MemberFunc fMemberFunc = nullptr;
	//using _UpdateStr = void __thiscall(void* pThis, const char* str);
	typedef void(__thiscall * _UpdateStr)(void* pThis, const char* str);
	_UpdateStr fUpdateStr = nullptr;
};

void MappedFunc2(void* pArgs)
{
	_Class* pObject = (_Class*)ObjectPtr;
	pObject->fMemberFunc = (_Class::_MemberFunc)MemberFunc;
	pObject->fUpdateStr = (_Class::_UpdateStr)UpdateStr;

	pObject->x = 9;
	pObject->y = 10;
	pObject->fUpdateStr(pObject, (const char*)pArgs);
	pObject->fMemberFunc(pObject);
}

void MarkerFunc2()
{
	return;
}

void CallMemberFunc(HANDLE hProc)
{
	pl("Calling MemberFunc with function mapping.");

	uintptr_t dwSize = 0;
	uintptr_t dwFuncSize = (uintptr_t)MarkerFunc2 - (uintptr_t)MappedFunc2;
	printf("MappedFunc2 Size: %i\n", dwFuncSize);
	_getch();
	void* pMemory = VirtualAllocEx(hProc, 0, dwFuncSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
	if (!pMemory)
	{
		pl("Failed to allocate memory.");
		return;
	}

	void* pArgs = (void*)((uintptr_t)pMemory + dwFuncSize);
	const char* newStr = "NotTrax";

	WriteProcessMemory(hProc, pMemory, newStr, 16, nullptr);
	void* pCode = (void*)((uintptr_t)pMemory + 16);

	if (!WriteProcessMemory(hProc, pCode, MappedFunc2, dwFuncSize, nullptr))
	{
		pl("Failed to write function and/or data to process.");
		return;
	}

	printf("0x%p\n0x%p\n", pMemory, pCode);
	_getch();

	HANDLE hThread = CreateRemoteThread(hProc, 0, 0, (LPTHREAD_START_ROUTINE)pCode, pMemory, 0, 0);
	if (!hThread)
	{
		pl("Failed to create thread.");
		return;
	}

	WaitForSingleObject(hThread, INFINITE);
	VirtualFreeEx(hProc, pMemory, dwSize, MEM_RELEASE);
	CloseHandle(hThread);
}

void HookMethodx86(HANDLE hProc)
{
	static bool bToggled = false;
	static char* pOriginalBytes = nullptr;
	static int hookSize = 0;

	bToggled = !bToggled;
	if (bToggled)
	{
		pl("Using hook method to call Function1");

		// allocate some memory for our shellcode...
		void* pMemory = VirtualAllocEx(hProc, 0, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		if (!pMemory)
		{
			pl("Failed to allocate memory.");
			return;
		}

		// some bullshit setup...
		uintptr_t pFunc = Func1;
		int offset1, offset2, jmpBackOffset;
		offset1 = 3;  // how many bytes from the beginning of shellcode
		offset2 = 17; // how many bytes from the beginning of shellcode
		jmpBackOffset = 21;
		hookSize = 5;

		// our shellcode...
		char buffer[0x100] = {
			0x60,														// pushad
			0x9C,														// pushaf
			0xB8, 0x00, 0x00, 0x00, 0x00,								// mov eax, Function1Address
			0xFF, 0xD0,													// call eax
			0x9D,														// popaf
			0x61,														// popad
			0x0F, 0xBF, 0xC8,											// movsx ecx, ax		// Overwritten instruction.
			0x85, 0xC9,													// test ecx, ecx		// Overwritten instruction.
			0xE9, 0x00, 0x00, 0x00, 0x00								// jmp JmpBackOffset
		};

		// copy Function1Address into shell code.
		memcpy(&buffer[offset1], (void*)&pFunc, sizeof(uintptr_t));

		// calculate the address we'll be jumping back to from our hook
		uintptr_t jumpBackAddress = (HookLoc + hookSize) - ((uintptr_t)pMemory + jmpBackOffset);

		// copy address into our shell code.
		memcpy(&buffer[offset2], (void*)&jumpBackAddress, sizeof(uintptr_t));

		//write shellcode to our allocated memory.
		if (!WriteProcessMemory(hProc, pMemory, buffer, 0x100, nullptr))
		{
			pl("Failed to write shellcode to target memory...");
			return;
		}

		//copy the original bytes that we'll be overwriting when we write the hook.
		if (!pOriginalBytes)
			pOriginalBytes = new char[hookSize] { 0 };
		if (!ReadProcessMemory(hProc, (void*)HookLoc, pOriginalBytes, hookSize, nullptr))
		{
			pl("Failed to read original bytes from hook location...");
			return;
		}

		// finally place the hook.
		if (!Memory::HookEx(hProc, (void*)HookLoc, pMemory, hookSize))
		{
			pl("Failed to hook function...");
			return;
		}
	}
	else
	{
		pl("Removing hook...");
		// disable hook.
		if (!WriteProcessMemory(hProc, (void*)HookLoc, pOriginalBytes, hookSize, nullptr))
		{
			pl("Failed to disable hook...");
			return;
		}
	}
}

void HookMethodx64(HANDLE hProc)
{
	static bool bToggled = false;
	static char* pOriginalBytes = nullptr;
	static int hookSize = 0;

	bToggled = !bToggled;
	if (bToggled)
	{
		pl("Using hook method to call Function1");

		// since we're on x64 we need to take into account that a 5 byte relative jmp instruction is limited to a 4GB address space.
		// what we're going to do is essentially try to allocate a page as close as possible to the location we want to hook.
		// this is going to let us use a normal 5 byte relative jmp to the memory we allocate.
		uintptr_t page = 0;
		void* pMemory = nullptr;
		while (!pMemory)
		{
			pMemory = VirtualAllocEx(hProc, (void*)(HookLoc + page), 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
			page += 0x1000;
		}

		// some bullshit setup...
		uintptr_t pFunc = Func1;
		int offset1, offset2, jmpBackOffset;
		offset1 = 4;
		offset2 = 24;
		jmpBackOffset = 35;
		hookSize = 6;

		// le shell code
		char buffer[0x100] = {
			0x9C,														// pushfq
			0x50,														// push rax
			0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	// movabs rax, Function1Address
			0xFF, 0xD0,													// call rax
			0x58,														// pop rax
			0x9D,														// popfq
			0x98,														// cwde
			0x83, 0xE0, 0x01,											// and eax, 1
			0x85, 0xC0,													// test eax, eax
			0x49, 0xBF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	// movabs r15, JumpbackAddress
			0x41, 0xFF, 0xE7											// jmp r15
		};

		// write Function1Address to shellcode.
		memcpy(&buffer[offset1], (void*)&pFunc, sizeof(uintptr_t));

		// "calculate" JumpBackAddress and populate shellcode.
		uintptr_t jumpBackAddress = HookLoc + hookSize;
		memcpy(&buffer[offset2], (void*)&jumpBackAddress, sizeof(uintptr_t));

		//write shellcode to our buffer
		if (!WriteProcessMemory(hProc, pMemory, buffer, 0x100, nullptr))
		{
			pl("Failed to write shellcode to target memory...");
			return;
		}

		// copy original bytes we'll be overwriting...
		if (!pOriginalBytes)
			pOriginalBytes = new char[hookSize] { 0 };
		if (!ReadProcessMemory(hProc, (void*)HookLoc, pOriginalBytes, hookSize, nullptr))
		{
			pl("Failed to read original bytes from hook location...");
			return;
		}

		// hook it!
		if (!Memory::HookEx(hProc, (void*)HookLoc, pMemory, hookSize))
		{
			pl("Failed to hook function...");
			return;
		}
	}
	else
	{
		pl("Removing hook...");
		// disable hook.
		if (!WriteProcessMemory(hProc, (void*)HookLoc, pOriginalBytes, hookSize, nullptr))
		{
			pl("Failed to disable hook...");
			return;
		}
	}
}

int main()
{
	HANDLE hProc = mem::GetProcHandle(L"EFCT_Target.exe");

	if (hProc)
	{
		pl("Found Process!");
	}
	else
	{
		pl("Run target then run me again.");
	}

	pl("[NUM1] Call Function1");
	pl("[NUM2] Call Function2");
	pl("[NUM3] Call Function3");
	pl("[NUM4] Call Function4 w/ Shellcode");
	pl("[NUM5] Call Function4 w/ Code Mapping");
	pl("[NUM6] Call Member Function");
	pl("[NUM9] Toggle hook method.");

	while (true)
	{
		if (GetAsyncKeyState(VK_NUMPAD1) & 1)
			CallFunc1(hProc);
		else if (GetAsyncKeyState(VK_NUMPAD2) & 1)
			CallFunc2(hProc);
		else if (GetAsyncKeyState(VK_NUMPAD3) & 1)
			CallFunc3(hProc);
		else if (GetAsyncKeyState(VK_NUMPAD4) & 1)
			CallFunc4SC(hProc);
		else if (GetAsyncKeyState(VK_NUMPAD5) & 1)
			CallFunc4FM(hProc);
		else if (GetAsyncKeyState(VK_NUMPAD6) & 1)
			CallMemberFunc(hProc);
		else if (GetAsyncKeyState(VK_NUMPAD9) & 1)
		{
#ifdef _WIN64
			HookMethodx64(hProc);
#else
			HookMethodx86(hProc);
#endif
		}
		else if (GetAsyncKeyState(VK_END) & 1)
			break;
	}

	if (hProc)
		CloseHandle(hProc);
	return 0;
}

