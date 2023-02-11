#include <windows.h>
#include <shlwapi.h>
#include <Richedit.h>
#include "dshow.h"
#include <winsock.h>
#include <stdio.h>

#pragma comment(lib,"Strmiids.lib")
#pragma comment(lib,"Shlwapi.lib") 
#pragma comment(lib,"wsock32.lib")

#define DsHook(a,b,c) if (!c##_) {	INT_PTR* p=b+*(INT_PTR**)a;  VirtualProtect(&c##_,4,PAGE_EXECUTE_READWRITE,&no); *(INT_PTR*)&c##_=*p;  VirtualProtect(p,4,PAGE_EXECUTE_READWRITE,&no);  *p=(INT_PTR)c; }

HRESULT ( __stdcall * SyncReadAlligned_ ) ( void* inst, IMediaSample *smp ) ; HANDLE out;
HRESULT   __stdcall   SyncReadAlligned    ( void* inst, IMediaSample *smp ) {	
    HRESULT ret =     SyncReadAlligned_   ( inst, smp );
    BYTE*   buf;      smp->GetPointer(&buf); 
    DWORD   len =     smp->GetActualDataLength(),no;	WriteFile(out,buf,len,&no,0);
    return  ret;  
}

int WINAPI WinMain(HINSTANCE inst,HINSTANCE prev,LPSTR cmd,int show) {
    MSG msg={0}; WSADATA wsa; DWORD no; HRESULT hr; 

    CoInitialize(0);   WSAStartup(MAKEWORD(1,1),&wsa);   LoadLibraryA("RichEd20"); 

    // connect to google translate for text language autodetection
    SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);  sockaddr_in addr = {AF_INET,htons(80),66,249,89,104}; 
    if(connect(s,(sockaddr*)&addr,sizeof(addr)) != 0)  return 0;

    HWND hwnd = CreateWindowA("RICHEDIT20W",0,WS_SIZEBOX|ES_MULTILINE|WS_VISIBLE|ES_AUTOVSCROLL|ES_AUTOHSCROLL|WS_SYSMENU|WS_CAPTION|WS_MINIMIZE|WS_HSCROLL|WS_VSCROLL,500,500,500,300,0,0,0,0);

    while ( IsWindowVisible(hwnd) ) {
        if( PeekMessage(&msg,0,0,0,1) ) { TranslateMessage( &msg ); DispatchMessage( &msg ); }
        if( msg.wParam==VK_RETURN && msg.message == WM_KEYDOWN ) {

            DWORD  len =  2+GetWindowTextLength(hwnd)*2; CHARRANGE ch={0,-1}; SendMessage(hwnd,EM_EXSETSEL,0,(LPARAM)&ch);
            WCHAR* Txt =  (WCHAR*)calloc(len,1),*e,*txt=Txt; SendMessage(hwnd, EM_GETSELTEXT, 0, (LPARAM)Txt); ch.cpMin=-1; SendMessage(hwnd,EM_EXSETSEL,0,(LPARAM)&ch); 
                   out =  CreateFile("out.mp3",GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_ALWAYS,0,0);

            while(*txt) { 
                // since sended text can not be larger than 100 we try to break sentences
                if((e=wcschr(txt,L'.')))                              *e=0; 
                if(wcslen(txt)>100 &&(e=wcschr(txt,L',')))            *e=0;
                if(wcslen(txt)>100) { e=txt+100; while(*e!=L' ') e--; *e=0; }
                
                // detect language by asking google translate service so we can switch voice language per sentence as needed                
                char utf[1000],esc[1000]={0},*a,*b=utf; WideCharToMultiByte(CP_UTF8,0,txt,-1,utf,1000,0,0); 
                while(*b) sprintf(esc+strlen(esc),"%%%0.2x",*(BYTE*)b++); txt+=wcslen(txt)+1; //escape utf-8 chars

                char buf[1000]; sprintf(buf,"GET /translate_a/t?client=t&sl=auto&text=%s HTTP/1.1\r\nUser-Agent: Mozilla/5.0\r\n\r\n\r\n\r\n",esc);
                send(s,buf,strlen(buf),0);    // we send text sentence to google translate server
                recv(s,buf,sizeof(buf),0);    // and receive detected language
                char lng[3]={"en"}; if((a=strstr(buf,"src\":"))&&(b=strchr(a+=6,'\"'))) { *b=0; strncpy(lng,a,2); } 			

                // This triplet with RenderFile is all you need to play anything with aprropriate codec on windows. 
                IGraphBuilder* graph= 0; CoCreateInstance( CLSID_FilterGraph, 0, CLSCTX_INPROC,IID_IGraphBuilder, (void **)&graph );
                IMediaControl* ctrl = 0; graph->QueryInterface( IID_IMediaControl, (void **)&ctrl );
                IMediaEvent*   event= 0; graph->QueryInterface( IID_IMediaEventEx, (void **)&event ); 

                // This sends text (sentence) encoded in get request  and progressively plays mp3 stream from google as it is received. 
                // So all TTS is done on server and this is only work that client does
                WCHAR url[1000];     wsprintfW(url,L"http://translate.google.com/translate_tts?tl=%S&q=%S",lng,esc); 
                if((hr=ctrl->RenderFile(url))) continue;   

                // we hook the source filter and append to global mp3 file on disk
                IBaseFilter*  filter;  graph->FindFilterByName(url,&filter);
                IPin*         pin;     filter->FindPin(L"Output",&pin); 
                IAsyncReader* reader;  pin->QueryInterface(IID_IAsyncReader,(void**)&reader);
                
                //  redirect  7th member func of IAsyncReader (SyncReadAlligned) to grab mp3 data from output pin of source filter
                DsHook(reader,6,SyncReadAlligned);

                // we run and wait for mp3 to finish before we ask another sentence
                hr=ctrl->Run(); long code=0,c; 
                while( code != EC_COMPLETE ) { 
                    if( PeekMessage(&msg,0,0,0,1) ) { TranslateMessage( &msg ); DispatchMessage( &msg ); } event->GetEvent(&code, &c, &c, 0); 
                    Sleep(1); 
                } 

                ctrl->Release(); event->Release(); graph->Release();
            } 
            free(Txt); 
            CloseHandle(out);
        }
    }
	return 0;
} 