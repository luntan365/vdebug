#include "ProcDbg.h"
#include "TitanEngine/TitanEngine.h"
#include "view/SyntaxDescHlpr.h"
#include "common/common.h"
#include "view/MainView.h"
#include "Command.h"
#include "symbol.h"
#include "BreakPoint.h"

#if WIN64 || _WIN64
#pragma comment(lib, "TitanEngine/TitanEngine_x64.lib")
#else
#pragma comment(lib, "TitanEngine/TitanEngine_x86.lib")
#endif

CProcDbgger *GetProcDbgger()
{
    static CProcDbgger *s_ptr = new CProcDbgger();
    return s_ptr;
}

CProcDbgger::CProcDbgger()
{
    m_hRunNotify = CreateEventW(NULL, FALSE, FALSE, NULL);
}

CProcDbgger::~CProcDbgger()
{}

VOID CProcDbgger::Wait()
{
    DWORD dwId = ((DEBUG_EVENT*)GetDebugData())->dwThreadId;
    m_eDbggerStat = em_dbg_status_free;
    SetCmdNotify(em_dbg_status_free, ustring().format(L"�߳� %04x >>", dwId).c_str());
    WaitForSingleObject(m_hRunNotify, INFINITE);
    SetCmdNotify(em_dbg_status_busy, ustring().format(L"�߳� %04x >>", dwId).c_str());
}

void CProcDbgger::Run()
{
    SetEvent(m_hRunNotify);
}

BOOL CProcDbgger::Connect(LPCWSTR wszTarget, LPVOID pParam)
{
    DbgProcUserContext *ptr = (DbgProcUserContext *)pParam;
    m_vDbgProcInfo.m_eType = em_dbgproc_open;
    m_vDbgProcInfo.m_dwPid = 0;
    m_vDbgProcInfo.m_wstrPePath = wszTarget;

    if (ptr)
    {
        m_vDbgProcInfo.m_wstrCmd = ptr->m_wstrCmd;
        m_vDbgProcInfo.m_wstrCurrentDir = ptr->m_wstrCurrentDir;
    }
    else
    {
        m_vDbgProcInfo.m_wstrCmd.clear();
        m_vDbgProcInfo.m_wstrCurrentDir.clear();
    }
    m_vDbgProcInfo.m_pfn = CustomBreakPoint;
    CloseHandle(CreateThread(NULL, 0, DebugThread, NULL, 0, NULL));
    return TRUE;
}

BOOL CProcDbgger::Connect(DWORD dwPid)
{
    m_vDbgProcInfo.m_eType = em_dbgproc_attach;
    m_vDbgProcInfo.m_dwPid = dwPid;
    CloseHandle(CreateThread(NULL, 0, DebugThread, NULL, 0, NULL));
    return TRUE;
}

BOOL CProcDbgger::DisConnect()
{
    DetachDebugger(m_dwCurrentThreadId);
    ResetCache();
    return TRUE;
}

BOOL CProcDbgger::IsConnect()
{
    return (0 != m_dwCurDebugProc);
}

void CProcDbgger::ResetCache()
{
    m_vThreadMap.clear();
    m_dwCurDebugProc = 0;
    m_vDbgProcInfo.Clear();
}

DWORD CProcDbgger::DebugThread(LPVOID pParam)
{
    if (em_dbgproc_attach == GetProcDbgger()->m_vDbgProcInfo.m_eType)
    {
        AttachDebugger((DWORD)GetProcDbgger()->m_vDbgProcInfo.m_dwPid, true, NULL, NULL);
    }
    else if (em_dbgproc_open == GetProcDbgger()->m_vDbgProcInfo.m_eType)
    {
        LPCWSTR wszDir = NULL;
        if (!GetProcDbgger()->m_vDbgProcInfo.m_wstrCurrentDir.empty())
        {
            wszDir = GetProcDbgger()->m_vDbgProcInfo.m_wstrCurrentDir.c_str();
        }

        PROCESS_INFORMATION *process = (PROCESS_INFORMATION *)InitDebugW(
            GetProcDbgger()->m_vDbgProcInfo.m_wstrPePath.c_str(),
            GetProcDbgger()->m_vDbgProcInfo.m_wstrCmd.c_str(),
            NULL
            );
        DebugLoop();
    }
    return 0;
}

void CProcDbgger::CustomBreakPoint()
{
    return;
}

DEBUG_EVENT *CProcDbgger::GetDebugProcData()
{
    return (DEBUG_EVENT *)GetDebugData();
}

TITAN_ENGINE_CONTEXT_t CProcDbgger::GetCurrentContext()
{
    DEBUG_EVENT *ptr = GetDebugProcData();

    TITAN_ENGINE_CONTEXT_t context = {0};
    map<DWORD, DbgProcThreadInfo>::const_iterator it = m_vThreadMap.find(ptr->dwThreadId);
    if (it == m_vThreadMap.end())
    {
        return context;
    }
    GetFullContextDataEx(it->second.m_hThread, &context);
    return context;
}

DbgProcModuleInfo CProcDbgger::GetModuleFromAddr(DWORD64 dwAddr)
{
    for (map<DWORD64, DbgProcModuleInfo>::const_iterator it = m_vModuleInfo.begin() ; it != m_vModuleInfo.end() ; it++)
    {
        if (dwAddr >= it->second.m_dwBaseOfImage && dwAddr <= it->second.m_dwEndAddr)
        {
            return it->second;
        }
    }

    return DbgProcModuleInfo();
}

ustring CProcDbgger::GetSymFromAddr(DWORD64 dwAddr)
{
    CTaskSymbolFromAddr task;
    task.m_dwAddr = dwAddr;
    CSymbolTaskHeader header;
    header.m_dwSize = sizeof(CTaskSymbolFromAddr) + sizeof(CSymbolTaskHeader);
    header.m_eTaskType = em_task_symaddr;
    header.m_pParam = &task;
    GetSymbolHlpr()->SendTask(&header);

    DbgProcModuleInfo module = GetModuleFromAddr(dwAddr);
    if (module.m_wstrDllName.empty())
    {
        return L"";
    }

    ustring wstr = module.m_wstrDllName;
    size_t pos = wstr.rfind(L'.');
    if (ustring::npos != pos)
    {
        wstr.erase(pos, wstr.size() - pos);
    }
    return FormatW(L"%ls!%ls", wstr.c_str(), task.m_wstrSymbol.c_str());
}

void CProcDbgger::OnCreateProcess(CREATE_PROCESS_DEBUG_INFO* pCreateProcessInfo)
{
    DWORD dwId = ((DEBUG_EVENT*)GetDebugData())->dwThreadId;
    CSyntaxDescHlpr hlpr;
    hlpr.FormatDesc(L"����·��", COLOUR_MSG, 16);
    hlpr.FormatDesc(GetProcDbgger()->m_vDbgProcInfo.m_wstrPePath.c_str(), COLOUR_MSG);
    hlpr.NextLine();
    hlpr.FormatDesc(L"���̻���ַ", COLOUR_MSG, 16);
    hlpr.FormatDesc(FormatW(L"0x%08x", pCreateProcessInfo->lpBaseOfImage), COLOUR_MSG);
    hlpr.NextLine();
    hlpr.FormatDesc(L"������ڵ�ַ", COLOUR_MSG, 16);
    hlpr.FormatDesc(FormatW(L"0x%08x", pCreateProcessInfo->lpStartAddress), COLOUR_MSG);
    hlpr.AddEmptyLine();
    GetSyntaxView()->AppendSyntaxDesc(hlpr.GetResult());

    DbgProcThreadInfo tp;
    tp.m_dwThreadId = dwId;
    tp.m_hThread = pCreateProcessInfo->hThread;
    tp.m_dwLocalBase = (DWORD64)pCreateProcessInfo->lpThreadLocalBase;
    tp.m_dwStartAddr = (DWORD64)pCreateProcessInfo->lpStartAddress;
    tp.m_wstrName = L"���߳�";

    (GetProcDbgger()->m_vThreadMap)[dwId] = tp;
    GetProcDbgger()->m_dwCurDebugProc = GetProcessId(pCreateProcessInfo->hProcess);

    GetProcDbgger()->m_vDbgProcInfo.m_hProcess = pCreateProcessInfo->hProcess;
    GetProcDbgger()->LoadModuleInfo(pCreateProcessInfo->hFile, (DWORD64)pCreateProcessInfo->lpBaseOfImage);
}

void CProcDbgger::OnExitProcess(EXIT_PROCESS_DEBUG_INFO* ExitProcess)
{}

void CProcDbgger::OnCreateThread(CREATE_THREAD_DEBUG_INFO* CreateThread)
{
    DbgProcThreadInfo newThread;
    newThread.m_dwThreadNum = GetProcDbgger()->m_vThreadMap.size();
    newThread.m_dwThreadId = ((DEBUG_EVENT*)GetDebugData())->dwThreadId;
    newThread.m_dwStartAddr = (DWORD64)CreateThread->lpStartAddress;
    newThread.m_dwLocalBase = (DWORD64)CreateThread->lpThreadLocalBase;
    newThread.m_hThread = CreateThread->hThread;

    GetProcDbgger()->m_vThreadMap[newThread.m_dwThreadId] = newThread;
}

void CProcDbgger::OnExitThread(EXIT_THREAD_DEBUG_INFO* ExitThread)
{
    DWORD dwId = ((DEBUG_EVENT*)GetDebugData())->dwThreadId;
    GetProcDbgger()->m_vThreadMap.erase(dwId);
}

void CProcDbgger::OnSystemBreakpoint(void* ExceptionData)
{
    DWORD dwId = ((DEBUG_EVENT*)GetDebugData())->dwThreadId;
    GetProcDbgger()->m_dwCurrentThreadId = dwId;
    if (GetProcDbgger()->m_vThreadMap.end() == GetProcDbgger()->m_vThreadMap.find(dwId))
    {
        return;
    }

    HANDLE hThread = GetProcDbgger()->m_vThreadMap[dwId].m_hThread;
    UINT cip = GetContextDataEx(hThread, UE_CIP);
    GetProcDbgger()->RunCommand(L"r");
    GetProcDbgger()->Wait();
}

bool CProcDbgger::LoadModuleInfo(HANDLE hFile, DWORD64 dwBaseOfModule)
{
    CSymbolTaskHeader task;
    CTaskLoadSymbol loadInfo;
    loadInfo.m_dwBaseOfModule = dwBaseOfModule;
    loadInfo.m_hImgaeFile = hFile;
    task.m_dwSize = sizeof(CSymbolTaskHeader) + sizeof(CTaskLoadSymbol);
    task.m_pParam = &loadInfo;
    task.m_eTaskType = em_task_loadsym;

    GetSymbolHlpr()->SendTask(&task);

    CSyntaxDescHlpr hlpr;
    WCHAR wszStart[64] = {0};
    WCHAR wszEnd[64] = {0};
    _i64tow_s(loadInfo.m_ModuleInfo.m_dwBaseOfImage, wszStart, 64, 16);
    _i64tow_s(loadInfo.m_ModuleInfo.m_dwEndAddr, wszEnd, 16, 16);
    hlpr.FormatDesc(
        FormatW(L"ģ����� 0x%016ls 0x%016ls  %ls", wszStart, wszEnd, loadInfo.m_ModuleInfo.m_wstrDllName.c_str()).c_str(),
        COLOUR_MSG
        );
    GetSyntaxView()->AppendSyntaxDesc(hlpr.GetResult());
    return true;
}

void CProcDbgger::OnLoadDll(LOAD_DLL_DEBUG_INFO* LoadDll)
{
    GetProcDbgger()->LoadModuleInfo(LoadDll->hFile, (DWORD64)LoadDll->lpBaseOfDll);
}

void CProcDbgger::OnUnloadDll(UNLOAD_DLL_DEBUG_INFO* UnloadDll)
{
    //CSyntaxDescHlpr hlpr;
    //hlpr.FormatDesc(L"ģ��ж��", COLOUR_MSG, 10);

    //DeleteIndex(UnloadDll->lpBaseOfDll);
}

void CProcDbgger::OnOutputDebugString(OUTPUT_DEBUG_STRING_INFO* DebugString)
{}

void CProcDbgger::OnException(EXCEPTION_DEBUG_INFO* ExceptionData)
{
    if (EXCEPTION_BREAKPOINT == ExceptionData->ExceptionRecord.ExceptionCode)
    {
        int d = 0;
    }
}

void CProcDbgger::OnDebugEvent(DEBUG_EVENT* DebugEvent)
{}

VOID CProcDbgger::InitEngine()
{
    SetCustomHandler(UE_CH_CREATEPROCESS, (void *)OnCreateProcess);
    SetCustomHandler(UE_CH_EXITPROCESS, (void *)OnExitProcess);
    SetCustomHandler(UE_CH_CREATETHREAD, (void*)OnCreateThread);
    SetCustomHandler(UE_CH_EXITTHREAD, (void*)OnExitThread);
    SetCustomHandler(UE_CH_SYSTEMBREAKPOINT, (void*)OnSystemBreakpoint);
    SetCustomHandler(UE_CH_LOADDLL, (void*)OnLoadDll);
    SetCustomHandler(UE_CH_UNLOADDLL, (void*)OnUnloadDll);
    SetCustomHandler(UE_CH_OUTPUTDEBUGSTRING, (void*)OnOutputDebugString);
    SetCustomHandler(UE_CH_UNHANDLEDEXCEPTION, (void*)OnException);
    SetCustomHandler(UE_CH_DEBUGEVENT, (void*)OnDebugEvent);
}

DbgCmdResult CProcDbgger::OnCommand(const ustring &wstrCmd, const ustring &wstrCmdParam, BOOL bShow, const CmdUserParam *pParam)
{
    DbgCmdResult res;
    if (wstrCmd == L"bp")
    {
        return OnCmdBp(wstrCmdParam, bShow, pParam);
    }
    else if (wstrCmd == L"u")
    {
        return OnCmdDisass(wstrCmdParam, bShow, pParam);
    }
    else if (wstrCmd == L"g")
    {
        return OnCmdGo(wstrCmdParam, bShow, pParam);
    }
    else if (wstrCmd == L"kv")
    {
        return OnCmdKv(wstrCmdParam, bShow, pParam);
    }
    else if (wstrCmd == L"du")
    {
        return OnCmdDu(wstrCmdParam, bShow, pParam);
    }
    else if (wstrCmd == L"r")
    {
        return OnCmdReg(wstrCmdParam, bShow, pParam);
    }
    else if (wstrCmd == L"sc")
    {
        return OnCmdScript(wstrCmdParam, bShow, pParam);
    }
    return res;
}

DbgCmdResult CProcDbgger::OnCmdBp(const ustring &wstrCmdParam, BOOL bShow, const CmdUserParam *pParam)
{
    ustring wstr(wstrCmdParam);
    wstr.trim();

    if (wstr.empty())
    {
        return DbgCmdResult();
    }

    DWORD64 dwProcAddr = 0;
    if (!GetNumFromStr(wstr, dwProcAddr))
    {
        map<ustring, DWORD64>::const_iterator it = m_vProcMap.find(wstr);
        if (it != m_vProcMap.end())
        {
            dwProcAddr = it->second;
        }
    }

    if (dwProcAddr)
    {
        if (GetBreakPointMgr()->SetBreakPoint(dwProcAddr, pParam))
        {
            return DbgCmdResult(em_dbgstat_succ, L"");
        }
    }
    return DbgCmdResult(em_dbgstat_faild, L"bp����ִ��ʧ��");
}

DbgCmdResult CProcDbgger::OnCmdDisass(const ustring &wstrCmdParam, BOOL bShow, const CmdUserParam *pParam)
{
    DbgCmdResult res;
    return res;
}

DbgCmdResult CProcDbgger::OnCmdGo(const ustring &wstrCmdParam, BOOL bShow, const CmdUserParam *pParam)
{
    DbgCmdResult res;
    return res;
}

DbgCmdResult CProcDbgger::OnCmdKv(const ustring &wstrCmdParam, BOOL bShow, const CmdUserParam *pParam)
{
    DbgCmdResult res;
    return res;
}

DbgCmdResult CProcDbgger::OnCmdDu(const ustring &wstrCmdParam, BOOL bShow, const CmdUserParam *pParam)
{
    DbgCmdResult res;
    return res;
}

DbgCmdResult CProcDbgger::OnCmdReg(const ustring &wstrCmdParam, BOOL bShow, const CmdUserParam *pParam)
{
    DbgCmdResult res;
    return res;
}

DbgCmdResult CProcDbgger::OnCmdScript(const ustring &wstrCmdParam, BOOL bShow, const CmdUserParam *pParam)
{
    DbgCmdResult res;
    return res;
}